// Editor entry point (T0013, skeleton).
//
// The editor is a consumer of the engine, never part of it. That boundary is
// structural rather than stylistic: on export the editor disappears and the
// runtime ships, so anything the game needs at run time has to live in the
// engine library. Establishing it now, while there is nothing to move, is the
// entire point of this ticket.
//
// The editor is also a *module host* -- it loads the gameplay module so the
// inspector can see game-defined types (T0032, T0035). That is not built yet;
// T0014 brings the Application and main loop.
#include <hp/Engine.hpp>
#include <hp/Log.hpp>

#include <cstdio>

namespace {
const hp::LogCategory kLog("editor");
} // namespace

int main() {
    hp::logAddConsoleSink();
    hp::engineRegisterConsumer("editor");

    HP_LOG_INFO(kLog, "HollowPoint editor");
    HP_LOG_INFO(kLog, "engine {}, {} instance(s), {} consumer(s)", hp::engineVersion(),
                hp::engineInstanceCount(), hp::engineConsumerCount());

    hp::logFlush();
    return 0;
}
