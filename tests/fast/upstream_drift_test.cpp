// The upstream drift guard for the mirrored light loop (T0145.6, D30).
//
// **The engine's lighting stage is a maintained copy of DiligentFX's**, because
// there is no seam inside `ApplyPunctualLight` where a shading model can be
// substituted — the BRDF is called inline at `PBR_Shading.fxh:690`, and D30
// records the measurement. The owner accepted that cost with one condition on
// how it is paid: *"Claude Code will be doing the work so we should optimize
// for the right path"* — so this is not a tripwire that spares a human a chore.
// It re-extracts every upstream function the engine copied or published, diffs
// it against the pin, and **prints what moved**.
//
// The failure it exists to prevent is specific and silent: a DiligentFX
// submodule bump changes the range attenuation curve or the accumulation's
// expression order, our mirror keeps the old one, and the engine renders
// *plausibly wrong* light for as long as nobody compares two builds. Nothing
// else in the tree would notice — the mirror still compiles, the byte-identical
// guards compare us against ourselves, and the pixels are only subtly off.
//
// When it fails, the working order is: read the diff, port what moved into
// `HpSurface.slang`, re-run the gpu byte-identity guards, and *then*
// `python3 tools/pin_upstream_shading.py`. Bumping the pin first turns the
// guard into a ritual.
//
// Bucket: fast. No device, no window — it reads two files.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char* kMarker = "### hp-pin:";

/// One pinned function: where it came from, and its text.
struct Pinned {
    std::string file;     ///< path under `DiligentFX/Shaders`
    std::string function; ///< the function's name
    std::string why;      ///< `copied` or `published`
    std::vector<std::string> lines;
};

/// Walks up from the working directory to the repository root.
///
/// The same shape `rockcube_mesh_test.cpp` uses, and for the same reason: a
/// test binary's working directory depends on how the suite was invoked, and
/// baking a configure-time path into it produces a *host* path the Windows
/// target cannot open.
std::filesystem::path findRepoRoot() {
    std::filesystem::path here = std::filesystem::current_path();
    for (int up = 0; up < 6; ++up) {
        if (std::filesystem::exists(here / "tests" / "fixtures" / "upstream_shading.pinned")) {
            return here;
        }
        if (!here.has_parent_path()) {
            break;
        }
        here = here.parent_path();
    }
    return {};
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// Line endings and trailing whitespace out — must match
/// `tools/pin_upstream_shading.py::normalise` exactly, or the guard fires on a
/// checkout setting rather than on a change.
std::string normalise(std::string line) {
    // Tabs first, so a trailing tab is still trailing whitespace afterwards.
    std::string expanded;
    expanded.reserve(line.size());
    for (const char c : line) {
        if (c == '\t') {
            expanded += "    ";
        } else if (c != '\r') {
            expanded += c;
        }
    }
    const std::size_t end = expanded.find_last_not_of(" \t");
    return end == std::string::npos ? std::string{} : expanded.substr(0, end + 1);
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(normalise(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        lines.push_back(normalise(current));
    }
    return lines;
}

std::vector<Pinned> parsePin(const std::string& text) {
    std::vector<Pinned> pinned;
    for (const std::string& line : splitLines(text)) {
        if (line.rfind(kMarker, 0) == 0) {
            std::istringstream fields(line.substr(std::string(kMarker).size()));
            Pinned entry;
            fields >> entry.file >> entry.function >> entry.why;
            pinned.push_back(entry);
            continue;
        }
        if (pinned.empty() || (line.empty() && !pinned.back().lines.empty() &&
                               pinned.back().lines.back().empty())) {
            continue;
        }
        if (line.rfind('#', 0) == 0 && pinned.empty()) {
            continue;
        }
        pinned.back().lines.push_back(line);
    }
    // The generator writes one blank line after each block; drop it so the
    // comparison is over the function's own text.
    for (Pinned& entry : pinned) {
        while (!entry.lines.empty() && entry.lines.back().empty()) {
            entry.lines.pop_back();
        }
    }
    return pinned;
}

/// Does this line *define* `function`, rather than call it?
///
/// Column zero is the whole test, and it is enough because every call site in
/// DiligentFX's shaders is indented inside a body. Mirrors the Python
/// extractor's regex.
bool definesAt(const std::string& line, const std::string& function) {
    if (line.empty() || (!std::isalpha(static_cast<unsigned char>(line[0])) && line[0] != '_')) {
        return false;
    }
    const std::size_t at = line.find(function);
    if (at == std::string::npos || at == 0) {
        return false;
    }
    const char before = line[at - 1];
    if (std::isalnum(static_cast<unsigned char>(before)) || before == '_') {
        return false;
    }
    std::size_t after = at + function.size();
    while (after < line.size() && std::isspace(static_cast<unsigned char>(line[after]))) {
        ++after;
    }
    return after < line.size() && line[after] == '(';
}

/// The function's text out of the header, signature through closing brace.
std::vector<std::string> extract(const std::vector<std::string>& source,
                                 const std::string& function) {
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (!definesAt(source[i], function)) {
            continue;
        }
        int depth = 0;
        bool opened = false;
        for (std::size_t j = i; j < source.size(); ++j) {
            depth += static_cast<int>(std::count(source[j].begin(), source[j].end(), '{'));
            depth -= static_cast<int>(std::count(source[j].begin(), source[j].end(), '}'));
            if (source[j].find('{') != std::string::npos) {
                opened = true;
            }
            if (opened && depth == 0) {
                return {source.begin() + static_cast<std::ptrdiff_t>(i),
                        source.begin() + static_cast<std::ptrdiff_t>(j) + 1};
            }
        }
        return {};
    }
    return {};
}

/// A readable diff of two line runs: common prefix and suffix trimmed, the rest
/// printed as `-` (pinned) and `+` (upstream).
///
/// **Not a general diff, deliberately.** The realistic change is one localised
/// edit, and trimming the matching ends shows exactly it. A Myers implementation
/// would read better on a wholesale rewrite, which is the case where the answer
/// is "read the file" regardless.
std::string diff(const std::vector<std::string>& pinned,
                 const std::vector<std::string>& upstream) {
    std::size_t front = 0;
    while (front < pinned.size() && front < upstream.size() &&
           pinned[front] == upstream[front]) {
        ++front;
    }
    std::size_t back = 0;
    while (back < pinned.size() - front && back < upstream.size() - front &&
           pinned[pinned.size() - 1 - back] == upstream[upstream.size() - 1 - back]) {
        ++back;
    }

    std::ostringstream out;
    out << "\n  @@ line " << (front + 1) << " of the function @@\n";
    for (std::size_t i = front; i < pinned.size() - back; ++i) {
        out << "  - " << pinned[i] << "\n";
    }
    for (std::size_t i = front; i < upstream.size() - back; ++i) {
        out << "  + " << upstream[i] << "\n";
    }
    return out.str();
}

} // namespace

TEST_CASE("the mirrored light loop still matches DiligentFX, line for line") {
    const std::filesystem::path root = findRepoRoot();
    REQUIRE_MESSAGE(!root.empty(),
                    "tests/fixtures/upstream_shading.pinned not found from "
                        << std::filesystem::current_path().string()
                        << " -- a drift guard that skips is not a guard");

    const std::string pinText = readFile(root / "tests" / "fixtures" / "upstream_shading.pinned");
    REQUIRE(!pinText.empty());

    const std::vector<Pinned> pinned = parsePin(pinText);
    // The count is asserted so that a pin file emptied by a bad edit fails here
    // rather than passing vacuously -- the classic way a guard stops guarding.
    // 6 through T0145; **16 since T0143** widened the mirror to the extended
    // layers' surface-fill derivations and layer resolves.
    REQUIRE_MESSAGE(pinned.size() == 16,
                    "expected 16 pinned functions, parsed " << pinned.size());

    const std::filesystem::path shaders =
        root / "third_party" / "DiligentEngine" / "DiligentFX" / "Shaders";
    REQUIRE_MESSAGE(std::filesystem::is_directory(shaders),
                    "DiligentFX shaders not found at " << shaders.string()
                        << " -- run `git submodule update --init --recursive`");

    int copied = 0;
    int published = 0;
    int moved = 0;
    for (const Pinned& entry : pinned) {
        CAPTURE(entry.file);
        CAPTURE(entry.function);
        REQUIRE_MESSAGE(!entry.lines.empty(),
                        "the pin for " << entry.function << " is empty");

        const std::string source = readFile(shaders / entry.file);
        REQUIRE_MESSAGE(!source.empty(), "could not read " << entry.file);

        const std::vector<std::string> found = extract(splitLines(source), entry.function);
        REQUIRE_MESSAGE(!found.empty(),
                        entry.function
                            << " no longer has a column-zero definition in " << entry.file
                            << ". It was renamed or moved -- the engine's mirror in "
                               "engine/shaders/HpSurface.slang needs the same treatment");

        if (found != entry.lines) {
            std::ostringstream why;
            why << "DiligentFX's " << entry.function << " (" << entry.file << ") changed, and "
                << (entry.why == "copied"
                        ? "engine/shaders/HpSurface.slang copies it -- port the change, "
                          "re-run the gpu byte-identity guards, then re-pin"
                        : "engine/shaders/HpMaterial.slang documents its behaviour as a "
                          "promise to game shaders -- correct the contract's prose, then "
                          "re-pin")
                << ".\n  Re-pin with: python3 tools/pin_upstream_shading.py"
                << diff(entry.lines, found);
            FAIL_CHECK(why.str());
            ++moved;
        }

        (entry.why == "copied" ? copied : published) += 1;
    }

    // **Reported only when it is true.** The first version printed the
    // reassuring line unconditionally, so a failing run ended with "match
    // DiligentFX" three lines under the diff saying they did not -- the same
    // confidently-wrong summary this repository has been bitten by from a
    // truncated `tail`.
    if (moved == 0) {
        MESSAGE("upstream shading pin: " << copied << " copied and " << published
                                         << " published functions match DiligentFX");
    } else {
        MESSAGE("upstream shading pin: " << moved << " of " << pinned.size()
                                         << " functions MOVED -- see the diffs above");
    }
}

TEST_CASE("the drift guard's own diff points at the line that moved") {
    // **The guard is tested, because a guard nobody has seen fail is a guard
    // nobody knows works.** This is the check-the-check discipline this
    // repository keeps relearning: a `sed` that deleted a loop body and a
    // `tail` that hid a failing bucket both passed their own checks.
    const std::vector<std::string> before{"float3 f(float3 x)", "{", "    return x * 2.0;", "}"};
    const std::vector<std::string> after{"float3 f(float3 x)", "{", "    return x * 3.0;", "}"};

    const std::string report = diff(before, after);
    CHECK(report.find("- " "    return x * 2.0;") != std::string::npos);
    CHECK(report.find("+ " "    return x * 3.0;") != std::string::npos);
    // Only the changed line: the signature and braces are common context and
    // must not be reprinted, or a one-line change buries itself.
    CHECK(report.find("float3 f(") == std::string::npos);
    CHECK(report.find("@@ line 3 of the function @@") != std::string::npos);
}
