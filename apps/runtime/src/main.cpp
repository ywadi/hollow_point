// Runtime entry point (T0013, skeleton).
//
// The second engine consumer, and the one that ships. It exists this early
// precisely so the engine cannot quietly grow a dependency on editor concerns:
// a rule with only one consumer is not enforced by anything, and the export
// pipeline in Phase 8 is where that debt would otherwise come due.
#include <hp/Engine.hpp>

#include <cstdio>

int main() {
    hp::engineRegisterConsumer("runtime");

    std::printf("HollowPoint runtime\n");
    std::printf("  engine %s, %u instance(s), %u consumer(s)\n", hp::engineVersion(),
                hp::engineInstanceCount(), hp::engineConsumerCount());
    return 0;
}
