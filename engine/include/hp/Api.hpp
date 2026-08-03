// Symbol visibility for the engine shared library (T0013, D12).
//
// The engine is a *shared* library: the editor, the runtime and every gameplay
// module link the same copy, so engine state exists exactly once per process.
// That is measured rather than assumed -- see tests/integration/module_boundary_test.cpp.
//
// These macros exist from the very first header on purpose. Retrofitting export
// annotations across hundreds of headers once they exist is mechanical churn
// worth an hour now to avoid, and it is the reason T0095 had to settle the
// linkage model before this ticket could start.
#pragma once

#if defined(_WIN32)
#define HP_EXPORT __declspec(dllexport)
#define HP_IMPORT __declspec(dllimport)
#else
// The engine builds with -fvisibility=hidden, so anything crossing the boundary
// has to say so. Types crossing it need this too, not just functions: vtables
// and typeinfo are what make dynamic_cast work across the boundary, which is
// allowed by the conventions and verified by the boundary suite.
#define HP_EXPORT __attribute__((visibility("default")))
#define HP_IMPORT __attribute__((visibility("default")))
#endif

// HP_ENGINE_BUILD is defined only while compiling the engine itself; consumers
// see the import side. CMake sets it via a PRIVATE compile definition, so a
// consumer cannot accidentally inherit it.
#if defined(HP_ENGINE_BUILD)
#define HP_API HP_EXPORT
#else
#define HP_API HP_IMPORT
#endif
