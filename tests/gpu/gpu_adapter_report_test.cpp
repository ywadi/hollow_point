// What device did the gpu bucket actually run on? (T0135)
//
// Bucket: gpu. This file exists because a green `gpu` bucket looked like
// hardware coverage and silently was not: **every gpu test written before
// T0135 ran on a software rasteriser**, and two closed tickets claimed
// otherwise -- T0027 said "a real GPU", T0028 said "Vulkan on an NVIDIA RTX
// 4070". Neither was checked, because nothing in the output said.
//
// That is the failure `CLAUDE.md` names: the check did not fail, it *passed on
// the wrong device and reported success*. So the fix is not a better assertion
// somewhere, it is making the device impossible to not notice.
//
// Two things happen here, and the split is deliberate (T0135.5):
//
//   - **Always**: a banner naming each backend, its adapter, and whether that
//     adapter is a software rasteriser. Unconditional, on stdout, so it
//     survives the build summary. This is the half that fixes the actual bug,
//     which was silence.
//   - **On request**: `-Dgpu-require-hardware` (which reaches this process as
//     `HP_GPU_REQUIRE_HARDWARE=1`) turns a software adapter into a failure.
//     Use it before closing a rendering ticket. CI never sets it.
//
// **Why enforcement is opt-in rather than the default**, since that looks like
// the weaker choice: this project is developed on two machines, and one of them
// has no Vulkan hardware path at all -- Mesa ICDs only, no NVIDIA ICD, and
// Dozen absent from Ubuntu's `mesa-vulkan-drivers`. Failing by default would
// make `-Dtest=gpu` permanently red there for half the backends, on a laptop
// with a perfectly good GPU. A bucket a developer cannot run is a bucket they
// stop running, which is a quieter version of the problem this file exists to
// fix. The full argument, and the two rejected alternatives, are on T0135.

#include <doctest/doctest.h>

#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/Window.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

/// A window and a device, or nothing.
struct Device {
    std::unique_ptr<hp::Window> window;
    std::unique_ptr<hp::RenderLayer> render;

    [[nodiscard]] bool ok() const { return render && render->ready(); }
};

/// Brings up a window and a device on the requested backend.
///
/// Same shape as the other gpu suites', deliberately -- see the note at the
/// bottom of this file about the six copies.
Device bringUp(hp::RenderBackend backend) {
    Device device;

    static const bool sink = [] {
        hp::logAddConsoleSink();
        return true;
    }();
    (void)sink;

    hp::WindowConfig windowConfig;
    windowConfig.title = "hp gpu adapter report";
    windowConfig.width = 320;
    windowConfig.height = 240;
    windowConfig.resizable = true;

    device.window = hp::Window::create(windowConfig);
    if (!device.window) {
        return device;
    }

    hp::RenderConfig renderConfig;
    renderConfig.backend = backend;
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

/// @returns @p text lowercased, for case-insensitive substring matching.
std::string lowered(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// Whether an adapter description names a known software rasteriser.
///
/// **Matched by name, and that is a real limitation worth stating**: there is no
/// portable "is this hardware" query -- Vulkan's `VkPhysicalDeviceType` has
/// `CPU`, but the engine does not surface it and OpenGL has no equivalent at
/// all, so the renderer string is what there is. The list below is therefore
/// the rasterisers this project has actually met or expects to:
///
/// | Substring | What it is |
/// |---|---|
/// | `llvmpipe` | Mesa's LLVM software rasteriser -- what every gpu test ran on before T0135 |
/// | `lavapipe` / `lvp` | the same thing behind Vulkan; the only ICD that loads on the WSL laptop |
/// | `swrast` / `softpipe` | Mesa's older software paths; `swrast` is the fallback when `/dev/dri` is absent |
/// | `swiftshader` | Google's software Vulkan |
/// | `basic render` | "Microsoft Basic Render Driver", D3D's software adapter |
/// | `gdi generic` | Windows' generic OpenGL 1.1 -- the CI host, where the suite died before printing |
///
/// A false negative here reports hardware when it is software, which is exactly
/// the lie T0135 exists to end -- so when in doubt, add the string.
bool looksLikeSoftware(const std::string& adapter) {
    static const char* const kSoftware[] = {
        "llvmpipe", "lavapipe", "lvp", "swrast", "softpipe",
        "swiftshader", "basic render", "gdi generic", "microsoft basic",
    };
    const std::string lower = lowered(adapter);
    for (const char* needle : kSoftware) {
        if (lower.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// Whether `-Dgpu-require-hardware` was passed to the build.
bool requireHardware() {
    const char* value = std::getenv("HP_GPU_REQUIRE_HARDWARE");
    return value != nullptr && value[0] == '1';
}

/// Prints one banner line, on stdout, so it survives the build summary.
///
/// `std::printf` rather than doctest's `MESSAGE` on purpose: a MESSAGE is
/// attached to a test case and is easy to lose in a long run, and the whole
/// point of this file is a line nobody can miss.
void report(const char* backendName, const std::string& adapter, bool software) {
    std::printf("[hp gpu] %-7s -> %s  [%s]\n", backendName, adapter.c_str(),
                software ? "SOFTWARE RASTERISER" : "hardware");
    std::fflush(stdout);
}

/// Brings up one backend, reports it, and enforces the policy if asked.
void reportBackend(hp::RenderBackend backend, const char* backendName) {
    Device device = bringUp(backend);
    if (!device.ok()) {
        // Not a failure. A backend with no device here is an environment fact
        // -- no window (headless), or no driver -- and the other gpu suites
        // already skip on it. Saying so is the point.
        std::printf("[hp gpu] %-7s -> no device on this host\n", backendName);
        std::fflush(stdout);
        MESSAGE("no " << std::string(backendName) << " device available -- skipping");
        tearDown(device);
        return;
    }

    const std::string adapter = device.render->adapterDescription();
    const bool software = looksLikeSoftware(adapter);
    report(backendName, adapter, software);

    if (software && requireHardware()) {
        // The message names the flag, because someone hitting this months from
        // now needs to know it is a policy they opted into and not a broken
        // device.
        FAIL_CHECK("-Dgpu-require-hardware was passed and "
                   << std::string(backendName) << " came up on a software rasteriser ("
                   << adapter << ")");
    }

    tearDown(device);
}

} // namespace

// Named so it sorts and reads as the bucket's header line.
TEST_CASE("gpu adapter report") {
    std::printf("\n[hp gpu] ---- adapters this run (T0135) ----\n");
    std::fflush(stdout);

    // Vulkan only: the OpenGL half of this report went with the backend (D29).
    reportBackend(hp::RenderBackend::Vulkan, "Vulkan");

    if (requireHardware()) {
        MESSAGE("-Dgpu-require-hardware is in force: a software adapter fails this case");
    } else {
        std::printf("[hp gpu] (informational -- pass -Dgpu-require-hardware to enforce)\n");
        std::fflush(stdout);
    }
    std::printf("[hp gpu] ------------------------------------\n\n");
    std::fflush(stdout);

    // The case itself always has an assertion, so it is never "empty" in the
    // summary. What it asserts is that the report ran -- the enforcement above
    // is what can fail.
    CHECK(true);
}

// **Known duplication, not an oversight**: `bringUp`/`tearDown` now exist in
// seven files in this directory, this one included. Collapsing them into a
// shared header is worth doing and is *not* T0135's job -- this ticket is about
// what the harness reports, and rewriting six working gpu suites to land it
// would put unrelated risk in the same change. Recorded on T0135 so it is a
// decision rather than a thing nobody noticed.
