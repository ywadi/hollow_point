// Runtime entry point (T0013, skeleton).
//
// The second engine consumer, and the one that ships. It exists this early
// precisely so the engine cannot quietly grow a dependency on editor concerns:
// a rule with only one consumer is not enforced by anything, and the export
// pipeline in Phase 8 is where that debt would otherwise come due.
#include <hp/Engine.hpp>
#include <hp/Log.hpp>

#include <cstdio>

namespace {
const hp::LogCategory kLog("runtime");
} // namespace

int main() {
    hp::logAddConsoleSink();
    hp::engineRegisterConsumer("runtime");

    HP_LOG_INFO(kLog, "HollowPoint runtime");
    HP_LOG_INFO(kLog, "engine {}, {} instance(s), {} consumer(s)", hp::engineVersion(),
                hp::engineInstanceCount(), hp::engineConsumerCount());

    hp::logFlush();
    return 0;
}
