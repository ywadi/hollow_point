// The engine's own shader source reaching a real compiler (T0141, D26).
//
// Bucket: gpu. **This one genuinely needs a device**, and not incidentally: the
// claim under test is that a shader written in *our* tree compiles against
// DiligentFX's *public* headers with neither side modified, and "it compiles" is
// only true of a real shader compiler on a real backend. Asserting that the
// factory returns a non-null pointer would say nothing at all about D26.
//
// **This is the load-bearing assumption of D26.** If it does not hold, the
// decision to own the surface stage rather than patch a vendored submodule is
// wrong, and it is far better to find that out here than after a renderer has
// been built on top of it.
//
// Skips cleanly with no device, like the rest of this bucket.

#include <doctest/doctest.h>

#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/RenderStack.hpp>
#include <hp/ShaderSources.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

// **No Diligent RHI header here, deliberately.** No test in this repo includes
// one: D21 exports only the math subset to consumers, and widening that for a
// test's convenience would erode a boundary the engine keeps on purpose. The
// device pointer is passed straight through `hp::compileEngineShader` without
// ever being dereferenced on this side.

namespace {

struct Device {
    std::unique_ptr<hp::Window> window;
    std::unique_ptr<hp::RenderLayer> render;

    [[nodiscard]] bool ok() const { return render && render->ready(); }
};

Device bringUp() {
    static const bool sink = [] {
        hp::logAddConsoleSink();
        return true;
    }();
    (void)sink;

    hp::WindowConfig windowConfig;
    windowConfig.title = "hp shader sources test";
    windowConfig.width = 320;
    windowConfig.height = 240;

    Device device;
    device.window = hp::Window::create(windowConfig);
    if (!device.window) {
        return device;
    }

    hp::RenderConfig renderConfig;
    renderConfig.vsync = false;
    device.render = std::make_unique<hp::RenderLayer>(*device.window, renderConfig);
    device.render->onAttach();
    return device;
}

void tearDown(Device& device) {
    if (device.render) {
        device.render->onDetach();
        device.render.reset();
    }
    device.window.reset();
}

} // namespace

TEST_CASE("the engine's shaders are embedded in the binary") {
    // Needs no device. An empty table compiles and links, then fails at
    // *pipeline creation*, which reads like a device fault rather than a build
    // one — so this assertion keeps the diagnostic next to its cause.
    CHECK(hp::embeddedShaderCount() > 0);
}

// **The standalone-compile case that used to sit here has been retired, not
// weakened.** It compiled `HpSurface.psh` on its own to prove our source reached
// the compiler and could include a DiligentFX header. That shader is no longer
// standalone: it consumes the per-pipeline `VSOutputStruct.generated` that
// `SurfacePipeline` supplies, so compiling it alone now fails *correctly*.
//
// The pipeline case at the bottom of this file is a strictly stronger claim --
// it exercises the same includes and then requires the device to accept the
// result -- so keeping a weaker test alive by feeding it a different shader
// would be testing scaffolding rather than the engine.

TEST_CASE("a shader neither side has fails, rather than resolving to something else") {
    // The compound factory asks ours first and DiligentFX's second. A miss in
    // both has to *be* a miss: silently resolving to a different shader is how a
    // renderer draws plausible nonsense.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    CHECK_FALSE(hp::compileEngineShader(device.render->device(), "ThisShaderDoesNotExist.slang",
                                        hp::ShaderStage::Pixel));

    tearDown(device);
}

TEST_CASE("a null device is refused rather than crashing") {
    CHECK_FALSE(hp::compileEngineShader(nullptr, "HpSurface.slang", hp::ShaderStage::Pixel));
}

TEST_CASE("a shader mounted through the VFS compiles, and cannot shadow the engine's") {
    // **T0142.14: a game's shader is content** (D13). The factory chain is
    // engine → VFS → DiligentFX, and both halves of that order are asserted
    // here — resolution *from* a mount, and the engine's names *winning over*
    // a mount.
    //
    // **It used to say the last sentence differently, and the difference was a
    // bug.** The claim was "the same compound factory feeds the slang bridge,
    // so what this proves for Diligent's compiler holds for slang's resolution
    // too" — and it did not. T0142.13 moved this probe onto slang, and it
    // failed: the probe sits in `shaders/`, so slang asked for
    // `shaders/HpMaterial.slang`, which the VFS refuses (reserved basename) and
    // the engine's embedded set does not carry — and slang, unlike Diligent's
    // front end, never tried the bare name. The contract header could not be
    // included from any subdirectory. `readSource` now mirrors Diligent's
    // relative-then-bare order, and **this test is what holds it there**: the
    // probe stays in a subdirectory precisely because a root-level one would
    // pass either way.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-vfs-shaders";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "shaders", ec);
    {
        // A game shader that also includes the engine's contract — the include
        // must resolve through the same chain the top-level name did. `.slang`,
        // because since T0142.13 that is the only shader language there is.
        std::ofstream file(scratch / "shaders" / "game_probe.slang");
        file << "#include \"HpMaterial.slang\"\n"
                "float4 main() : SV_Target0 { return float4(0.0, 1.0, 0.0, 1.0); }\n";
    }
    {
        // A file named like the contract header, with contents that cannot
        // compile. If the mount could shadow the engine, the include above
        // would pull this in and the compile below would fail.
        std::ofstream file(scratch / "shaders" / "HpMaterial.slang");
        file << "#error a game shader must not be able to redefine the contract\n";
    }

    hp::Vfs::shutdown();
    REQUIRE(hp::Vfs::init(nullptr));
    REQUIRE(hp::Vfs::mount(scratch.string()));

    // Resolution from the mount, includes resolved engine-first: if the
    // mounted `#error` copy of `HpMaterial.slang` could shadow the engine's,
    // this compile would fail on it. **The reservation follows the rename** —
    // the reserved set is the embedded set, so renaming the contract renamed
    // what is protected, and this is the assertion that proves it rather than
    // assuming it.
    CHECK(hp::compileEngineShader(device.render->device(), "shaders/game_probe.slang",
                                  hp::ShaderStage::Pixel));

    // With the mount gone the game shader stops resolving — a miss stays a
    // miss, exactly as the unknown-name case above requires.
    hp::Vfs::shutdown();
    CHECK_FALSE(hp::compileEngineShader(device.render->device(), "shaders/game_probe.slang",
                                        hp::ShaderStage::Pixel));

    tearDown(device);
    std::filesystem::remove_all(scratch, ec);
}

TEST_CASE("a pipeline state is built from the engine's own shaders") {
    // **Compiling and pipelining are different questions**, and only the second
    // is the claim D26 rests on. A shader can compile in isolation and still be
    // refused once it has to agree with a vertex layout, a resource signature
    // and a render-pass format — which is exactly the seam between our pixel
    // shader and DiligentFX's vertex shader, and exactly where "own the surface
    // stage" would fail if it were going to.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    CHECK(hp::buildEngineSurfacePipeline(device.render->device(), device.render->context()));

    tearDown(device);
}
