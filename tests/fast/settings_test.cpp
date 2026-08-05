// Settings, and the layer names they carry (T0078, T0085.1).
//
// Bucket: fast. The properties worth pinning here are the ones a config system
// gets wrong in ways that are only discovered in the field: a missing key that
// crashes, a corrupt file that blocks startup, and a name lookup that silently
// returns the default layer instead of admitting it does not know.

#include <doctest/doctest.h>

#include <hp/Settings.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path scratchDir() {
    return std::filesystem::temp_directory_path() / "hp_settings_test";
}

} // namespace

TEST_CASE("every getter falls back rather than failing") {
    // **The property that makes a config file unable to break a program.** A
    // missing key is the normal state of a setting nobody has changed.
    const hp::SettingsStore store;

    CHECK(store.getBool("render.vsync", true));
    CHECK_FALSE(store.getBool("render.vsync", false));
    CHECK(store.getInt("physics.tickRate", 60) == 60);
    CHECK(store.getFloat("audio.volume", 0.8) == doctest::Approx(0.8));
    CHECK(store.getString("startupScene", "main.scene") == "main.scene");
    CHECK_FALSE(store.has("anything"));
}

TEST_CASE("dotted keys nest in the file and round-trip") {
    hp::SettingsStore store;
    store.setInt("physics.tickRate", 120);
    store.setBool("render.shadows.enabled", true);
    store.setString("startupScene", "levels/hub.scene");

    CHECK(store.getInt("physics.tickRate", 60) == 120);
    CHECK(store.getBool("render.shadows.enabled", false));
    CHECK(store.getString("startupScene", "") == "levels/hub.scene");
    CHECK(store.has("physics.tickRate"));

    // Nesting is real, not a key with dots in it -- which is what makes the file
    // read as a grouped document.
    const std::string text = store.toString();
    CHECK(text.find("physics:") != std::string::npos);
    CHECK(text.find("shadows:") != std::string::npos);

    hp::SettingsStore reloaded;
    REQUIRE(reloaded.loadFromString(text));
    CHECK(reloaded.getInt("physics.tickRate", 0) == 120);
    CHECK(reloaded.getBool("render.shadows.enabled", false));
    CHECK(reloaded.getString("startupScene", "") == "levels/hub.scene");
}

TEST_CASE("a corrupt file falls back to defaults instead of blocking startup") {
    // **The failure this exists to prevent is not a crash, it is a refusal to
    // start.** A malformed preferences file that stops the editor launching is
    // infuriating and entirely avoidable.
    hp::SettingsStore store;
    CHECK_FALSE(store.loadFromString("this: [is: not: valid: yaml"));
    // And it is still usable afterwards -- every getter answers.
    CHECK(store.getInt("physics.tickRate", 60) == 60);
    store.setInt("physics.tickRate", 30);
    CHECK(store.getInt("physics.tickRate", 60) == 30);
}

TEST_CASE("a missing file is not an error, but an unreadable one is reported") {
    std::error_code ec;
    std::filesystem::remove_all(scratchDir(), ec);

    hp::SettingsStore store;
    // Missing means "never saved", which is the normal state of a new project.
    CHECK(store.load((scratchDir() / "absent.yaml").string()));
    CHECK(store.getInt("a", 7) == 7);

    // A file that exists and will not parse is a different thing and says so.
    std::filesystem::create_directories(scratchDir(), ec);
    const std::filesystem::path bad = scratchDir() / "bad.yaml";
    {
        std::ofstream file(bad);
        file << "key: [unterminated\n  - broken: : :\n";
    }
    hp::SettingsStore corrupt;
    CHECK_FALSE(corrupt.load(bad.string()));
    CHECK(corrupt.getInt("key", 3) == 3);

    std::filesystem::remove_all(scratchDir(), ec);
}

TEST_CASE("settings survive a save and load through the filesystem") {
    std::error_code ec;
    std::filesystem::remove_all(scratchDir(), ec);
    // Parent directories are created, so a caller does not have to.
    const std::filesystem::path path = scratchDir() / "nested" / "project.yaml";

    hp::SettingsStore store;
    store.setString("name", "hollow point");
    store.setInt("physics.tickRate", 90);
    REQUIRE(store.save(path.string()));
    REQUIRE(std::filesystem::exists(path));

    hp::SettingsStore reloaded;
    REQUIRE(reloaded.load(path.string()));
    CHECK(reloaded.getString("name", "") == "hollow point");
    CHECK(reloaded.getInt("physics.tickRate", 0) == 90);

    std::filesystem::remove_all(scratchDir(), ec);
}

// --- layer names (T0085.1) ---------------------------------------------------

TEST_CASE("layer 0 is named by default and the rest are not") {
    // An unnamed default reads in an inspector as though nothing is configured.
    const hp::LayerNames names;
    CHECK(names.name(hp::kDefaultLayer) == "Default");
    CHECK(names.name(1).empty());
    CHECK(names.all().size() == static_cast<std::size_t>(hp::kMaxLayers));
}

TEST_CASE("an unknown layer name returns -1, never 0") {
    // **The one that matters.** Returning 0 would put the object on the default
    // layer, which is visible to everything -- so a typo would not fail, it
    // would quietly do the most permissive thing possible.
    hp::LayerNames names;
    names.setName(3, "Player");

    CHECK(names.indexOf("Player") == 3);
    CHECK(names.indexOf("player") == -1);   // exact, case-sensitive
    CHECK(names.indexOf("Enemy") == -1);
    CHECK(names.indexOf("") == -1);         // not "the first unnamed layer"
}

TEST_CASE("out-of-range layers are ignored rather than corrupting the table") {
    hp::LayerNames names;
    names.setName(-1, "Nope");
    names.setName(hp::kMaxLayers, "AlsoNope");
    CHECK(names.indexOf("Nope") == -1);
    CHECK(names.indexOf("AlsoNope") == -1);
    CHECK(names.name(-1).empty());
    CHECK(names.name(hp::kMaxLayers).empty());
}

TEST_CASE("a mask is built from names, and unknown names contribute nothing") {
    hp::LayerNames names;
    names.setName(1, "Player");
    names.setName(2, "Enemy");
    names.setName(5, "Viewmodel");

    const hp::LayerMask mask = names.mask({"Player", "Viewmodel"});
    CHECK(mask.has(1));
    CHECK(mask.has(5));
    CHECK_FALSE(mask.has(2));

    // A typo is skipped and logged, not silently treated as every layer or as
    // layer 0 -- both of which would render an object somewhere it should not be.
    const hp::LayerMask typo = names.mask({"Playr"});
    CHECK(typo.empty());
}

TEST_CASE("layer names round-trip through settings as a readable sequence") {
    hp::LayerNames names;
    names.setName(1, "Player");
    names.setName(2, "Enemy");

    hp::SettingsStore store;
    store.writeLayerNames(names);

    const std::string text = store.toString();
    CHECK(text.find("layers:") != std::string::npos);
    CHECK(text.find("Player") != std::string::npos);
    // Trailing unnamed layers are dropped, so this is three lines and not 32.
    CHECK(text.find("Viewmodel") == std::string::npos);

    hp::SettingsStore reloaded;
    REQUIRE(reloaded.loadFromString(text));
    const hp::LayerNames restored = reloaded.readLayerNames();
    CHECK(restored.name(0) == "Default");
    CHECK(restored.name(1) == "Player");
    CHECK(restored.name(2) == "Enemy");
    CHECK(restored.indexOf("Enemy") == 2);
}

TEST_CASE("a gap in the table keeps later layers at their own index") {
    // **Position is the layer index**, so an unnamed layer 1 must still occupy a
    // slot -- omitting it would silently renumber everything after it, and a
    // scene authored before the change would light the wrong objects.
    hp::LayerNames names;
    names.setName(3, "Water");

    hp::SettingsStore store;
    store.writeLayerNames(names);
    hp::SettingsStore reloaded;
    REQUIRE(reloaded.loadFromString(store.toString()));

    const hp::LayerNames restored = reloaded.readLayerNames();
    CHECK(restored.indexOf("Water") == 3);
    CHECK(restored.name(1).empty());
    CHECK(restored.name(2).empty());
}

TEST_CASE("reading layer names from a store that has none yields the defaults") {
    const hp::SettingsStore store;
    const hp::LayerNames names = store.readLayerNames();
    CHECK(names.name(hp::kDefaultLayer) == "Default");
    CHECK(names.indexOf("Player") == -1);
}
