# The sandbox gameplay module (T0013, stubbed).
#
# **This is not "the game".** HollowPoint is an engine for several games, and a
# game is a separate project: its own assets, scenes and gameplay module, built
# against an installed engine. What lives here is the *sample* module -- the one
# the engine repo builds and loads itself, so that the module boundary is
# exercised by CI rather than only by whichever game happens to be open.
#
# It earns its place three times over: T0013's four-artifact layout is only real
# if something actually crosses the boundary, T0048 needs a module to reload,
# and the boundary suite needs a subject.
#
# MODULE rather than SHARED: loaded with dlopen/LoadLibrary at runtime, never
# linked against. On ELF the distinction is cosmetic; it matters because it
# states the intent, and because nothing should be tempted to put it on a
# target_link_libraries() line.
add_library(hp_sandbox MODULE
    src/Sandbox.cpp
)

target_link_libraries(hp_sandbox PRIVATE hp::engine)
target_compile_features(hp_sandbox PRIVATE cxx_std_20)

set_target_properties(hp_sandbox PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/samples/sandbox"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/samples/sandbox")
