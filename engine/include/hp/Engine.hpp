// The engine's public surface (T0013).
//
// Deliberately almost empty. This ticket establishes the *structure* -- a shared
// engine library with editor, runtime and gameplay-module consumers -- and the
// systems that fill it in are separate tickets: the Application and main loop
// (T0014), the window and platform layer (T0015), logging (T0054).
//
// What is here exists to prove the structure works end to end: consumers can
// include an engine header, call into the engine, and observe that they share
// one copy of its state.
#pragma once

#include <hp/Api.hpp>

#include <cstdint>

namespace hp {

/// Identifies the engine build a consumer is linked against.
///
/// A placeholder for T0104's build id, which needs to be derived from something
/// that changes when the ABI changes rather than from a hand-written string.
/// Kept deliberately trivial so nobody mistakes it for that.
HP_API const char* engineVersion();

/// Number of live engine instances in this process.
///
/// Exists to make D12's central claim observable rather than theoretical: with
/// the engine as a shared library this counts one no matter how many modules
/// and executables link it. If the engine were ever linked statically into both
/// an executable and a gameplay module, each side would count its own.
HP_API std::uint32_t engineInstanceCount();

/// Registers this consumer with the engine. Idempotent per caller name.
///
/// @param name identifies the caller -- "editor", "runtime", a gameplay
///        module. Registering the same name twice is a no-op. A null pointer
///        is ignored rather than treated as an error.
HP_API void engineRegisterConsumer(const char* name);

/// How many consumers have registered. Shared state, by construction.
HP_API std::uint32_t engineConsumerCount();

} // namespace hp
