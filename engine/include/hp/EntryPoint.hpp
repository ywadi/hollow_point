// The program entry point (T0014).
//
// Include this in **exactly one** translation unit of an application and it
// becomes an executable. Nothing else is required:
//
//     #include <hp/Application.hpp>
//     #include <hp/EntryPoint.hpp>
//
//     class Editor final : public hp::Application { ... };
//
//     std::unique_ptr<hp::Application> hp::createApplication(int, char**) {
//         return std::make_unique<Editor>();
//     }
//
// A header rather than a library-provided `main` on purpose: a `main` inside
// `libhp_engine` would be pulled in by every consumer including the gameplay
// module and the test binaries, none of which want one.
#pragma once

#include <hp/Application.hpp>
#include <hp/Log.hpp>

#include <memory>

int main(int argc, char** argv) {
    // The application is created, run and destroyed in the *app's* translation
    // unit, so the allocation and the matching deallocation are on the same
    // side of the engine boundary.
    //
    // Worth being explicit, since the conventions forbid freeing across the
    // module boundary: an *app* is not a module. It links the engine directly,
    // is built in lockstep with it, and is never unloaded. The rule exists for
    // hot-reloadable gameplay modules (T0048), where it is load-bearing.
    std::unique_ptr<hp::Application> app = hp::createApplication(argc, argv);
    if (!app) {
        return 1;
    }
    return app->run();
}
