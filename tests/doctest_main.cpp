// The single translation unit that provides doctest's main().
//
// Kept in its own file so the ~9k-line header is compiled with the test runner
// exactly once per bucket binary; every other test TU includes the header with
// DOCTEST_CONFIG_IMPLEMENT already defined elsewhere and stays cheap.
//
// Shared by every bucket (fast, integration, gpu, perf) -- see tests/CMakeLists.txt.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
