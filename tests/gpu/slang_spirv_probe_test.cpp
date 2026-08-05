// **EXPERIMENT (T0141, Slang evaluation) — delete or promote, do not leave.**
//
// Feeds Diligent SPIR-V produced by `slangc` from a Slang shader that includes
// DiligentFX's unmodified `PBR_Shading.fxh` and calls `GetSurfaceReflectanceMR`
// through a Slang `interface` with default implementations.
//
// The claim under test: **Diligent accepts SPIR-V from a compiler that is not
// its own, reflects it, builds a pipeline, and runs it — and DiligentFX's own
// PBR code, compiled by Slang rather than DXC, produces the right number.**
// Reading names out of a `.spv` with `strings` proves none of that.
//
// The SPIR-V is read from a path given by HP_SLANG_PROBE_DIR so that no
// generated binary enters the repository while this is still an experiment.
// Skips cleanly if it is absent.

#include <doctest/doctest.h>

#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/RenderStack.hpp>
#include <hp/ShaderSources.hpp>
#include <hp/Window.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace {

std::vector<std::uint8_t> readAll(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

} // namespace

TEST_CASE("Diligent runs SPIR-V that slangc produced from DiligentFX's own PBR") {
    const char* dir = std::getenv("HP_SLANG_PROBE_DIR");
    if (dir == nullptr) {
        MESSAGE("HP_SLANG_PROBE_DIR not set; skipping the Slang probe");
        return;
    }
    const std::vector<std::uint8_t> vs = readAll(std::filesystem::path(dir) / "probe_vs.spv");
    const std::vector<std::uint8_t> ps = readAll(std::filesystem::path(dir) / "probe_ps.spv");
    if (vs.empty() || ps.empty()) {
        MESSAGE("probe SPIR-V not found in " << dir << "; skipping");
        return;
    }

    static const bool sink = [] {
        hp::logAddConsoleSink();
        return true;
    }();
    (void)sink;

    hp::WindowConfig windowConfig;
    windowConfig.title = "hp slang probe";
    windowConfig.width = 64;
    windowConfig.height = 64;
    auto window = hp::Window::create(windowConfig);
    if (!window) {
        MESSAGE("no window; skipping");
        return;
    }
    hp::RenderConfig renderConfig;
    renderConfig.vsync = false;
    auto render = std::make_unique<hp::RenderLayer>(*window, renderConfig);
    render->onAttach();
    if (!render->ready()) {
        MESSAGE("no graphics device; skipping");
        render->onDetach();
        return;
    }

    std::vector<std::uint8_t> pixels;
    const bool ok = hp::probePrecompiledSpirvPipeline(render->device(), render->context(), vs.data(),
                                                      vs.size(), ps.data(), ps.size(), pixels);
    CHECK(ok);
    if (ok) {
        REQUIRE(pixels.size() >= 4);
        MESSAGE("slang probe pixel = (" << int(pixels[0]) << ", " << int(pixels[1]) << ", "
                                        << int(pixels[2]) << ")");
        // `GetSurfaceReflectanceMR((1.0, 0.5, 0.2), metallic 0, roughness 0.6)`
        // sets `DiffuseColor = BaseColor * 0.96` — so (245, 122, 49) is *their*
        // arithmetic, executed on the GPU, from Slang-generated SPIR-V.
        CHECK(int(pixels[0]) == doctest::Approx(245).epsilon(0.02));
        CHECK(int(pixels[1]) == doctest::Approx(122).epsilon(0.02));
        CHECK(int(pixels[2]) == doctest::Approx(49).epsilon(0.05));
    }

    render->onDetach();
}
