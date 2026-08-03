#include <hp/Engine.hpp>

#include <string>
#include <vector>

namespace hp {
namespace {

/// Engine-owned state. The point of the structure this ticket builds is that
/// there is exactly one of these in a process, however many consumers link the
/// engine -- see engineConsumerCount().
struct EngineState {
    std::uint32_t instances = 1;
    std::vector<std::string> consumers;
};

EngineState& state() {
    static EngineState s;
    return s;
}

} // namespace

const char* engineVersion() {
    return "0.0.1-skeleton";
}

std::uint32_t engineInstanceCount() {
    return state().instances;
}

void engineRegisterConsumer(const char* name) {
    if (name == nullptr) {
        return;
    }
    auto& consumers = state().consumers;
    for (const auto& existing : consumers) {
        if (existing == name) {
            return;
        }
    }
    consumers.emplace_back(name);
}

std::uint32_t engineConsumerCount() {
    return static_cast<std::uint32_t>(state().consumers.size());
}

} // namespace hp
