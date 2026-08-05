// The action layer (T0068). See hp/Input.hpp for the decisions.
#include <hp/Input.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace hp {

void InputMap::bindKey(ActionId action, KeyCode key) {
    keys_.push_back({action, key});
}

void InputMap::bindMouseButton(ActionId action, MouseButton button) {
    buttons_.push_back({action, button});
}

void InputMap::bindAxis2D(ActionId action, KeyCode negativeX, KeyCode positiveX, KeyCode negativeY,
                          KeyCode positiveY, AxisTuning tuning) {
    axes_.push_back({action, negativeX, positiveX, negativeY, positiveY, tuning});
}

bool InputMap::binds(ActionId action) const {
    const auto sameAction = [action](const auto& binding) { return binding.action == action; };
    return std::any_of(keys_.begin(), keys_.end(), sameAction) ||
           std::any_of(buttons_.begin(), buttons_.end(), sameAction) ||
           std::any_of(axes_.begin(), axes_.end(), sameAction);
}

namespace {

/// A context in the stack.
struct Context {
    std::size_t handle = 0;
    InputMap map;
    InputContextConfig config;
};

/// Edges accumulated since the last snapshot, plus the level.
struct Accumulated {
    bool pressed = false;
    bool released = false;
    bool held = false;
};

} // namespace

struct InputSystem::Impl {
    std::vector<Context> contexts; ///< sorted: highest priority first
    std::size_t nextHandle = 1;

    /// Keys and buttons currently down, by raw code. The level half of the
    /// state, and the reason `reset()` exists: a focus loss eats the key-up.
    std::unordered_set<std::uint16_t> keysDown;
    std::unordered_set<std::uint8_t> buttonsDown;

    /// Edges since the last snapshot, per action.
    std::unordered_map<std::uint64_t, Accumulated> accumulating;
    /// What the current step sees. Published by snapshot().
    std::unordered_map<std::uint64_t, DigitalAction> published;

    void applyDigital(ActionId action, bool down) {
        Accumulated& state = accumulating[action.value()];
        if (down) {
            state.pressed = true;
            state.held = true;
        } else {
            state.released = true;
            state.held = false;
        }
    }
};

InputSystem::InputSystem() : impl_(new Impl) {}

InputSystem::~InputSystem() {
    delete impl_;
}

std::size_t InputSystem::pushContext(InputMap map, InputContextConfig config) {
    const std::size_t handle = impl_->nextHandle++;
    impl_->contexts.push_back({handle, std::move(map), config});

    // Highest priority first; ties newest-first, so pushing a modal context
    // puts it above an existing one of equal priority without the caller having
    // to invent a number. stable_sort is what makes that tie rule hold.
    std::stable_sort(impl_->contexts.begin(), impl_->contexts.end(),
                     [](const Context& a, const Context& b) {
                         if (a.config.priority != b.config.priority) {
                             return a.config.priority > b.config.priority;
                         }
                         return a.handle > b.handle;
                     });
    return handle;
}

void InputSystem::removeContext(std::size_t handle) {
    impl_->contexts.erase(std::remove_if(impl_->contexts.begin(), impl_->contexts.end(),
                                         [handle](const Context& c) { return c.handle == handle; }),
                          impl_->contexts.end());
}

bool InputSystem::onEvent(Event& event) {
    // Level state is tracked regardless of bindings, so a context pushed while
    // a key is already down still sees it as held at the next snapshot.
    if (event.type() == EventType::KeyPressed || event.type() == EventType::KeyReleased) {
        auto& key = static_cast<KeyEvent&>(event);
        const bool down = event.type() == EventType::KeyPressed;
        const auto raw = static_cast<std::uint16_t>(key.key());

        // Key repeat is not an edge. A held key generating presses every few
        // milliseconds would make "pressed" mean "held" for anything that fires
        // on it, which is the whole distinction 68.3 asks for.
        if (down && key.isRepeat()) {
            return false;
        }

        if (down) {
            impl_->keysDown.insert(raw);
        } else {
            impl_->keysDown.erase(raw);
        }

        bool consumed = false;
        for (const Context& context : impl_->contexts) {
            bool bound = false;
            for (const auto& binding : context.map.keys_) {
                if (binding.key == key.key()) {
                    impl_->applyDigital(binding.action, down);
                    bound = true;
                }
            }
            // An axis binding claims its keys too, or a context that owns WASD
            // would let movement leak to the context below it.
            for (const auto& axis : context.map.axes_) {
                if (axis.negativeX == key.key() || axis.positiveX == key.key() ||
                    axis.negativeY == key.key() || axis.positiveY == key.key()) {
                    bound = true;
                }
            }
            if (bound && context.config.consumes) {
                consumed = true;
                break;
            }
        }
        return consumed;
    }

    if (event.type() == EventType::MouseButtonPressed ||
        event.type() == EventType::MouseButtonReleased) {
        auto& mouse = static_cast<MouseButtonEvent&>(event);
        const bool down = event.type() == EventType::MouseButtonPressed;
        const auto raw = static_cast<std::uint8_t>(mouse.button());

        if (down) {
            impl_->buttonsDown.insert(raw);
        } else {
            impl_->buttonsDown.erase(raw);
        }

        bool consumed = false;
        for (const Context& context : impl_->contexts) {
            bool bound = false;
            for (const auto& binding : context.map.buttons_) {
                if (binding.button == mouse.button()) {
                    impl_->applyDigital(binding.action, down);
                    bound = true;
                }
            }
            if (bound && context.config.consumes) {
                consumed = true;
                break;
            }
        }
        return consumed;
    }

    return false;
}

void InputSystem::snapshot() {
    impl_->published.clear();

    // Edges accumulated since the last snapshot, published whole. A key pressed
    // and released between two snapshots reports both, which is the case that
    // polling loses.
    for (const auto& [action, state] : impl_->accumulating) {
        DigitalAction& out = impl_->published[action];
        out.pressed = state.pressed;
        out.released = state.released;
        out.held = state.held;
    }
    for (auto& [action, state] : impl_->accumulating) {
        (void)action;
        state.pressed = false;
        state.released = false;
    }

    // Anything still held but with no edge this step is still held.
    for (const auto& [action, state] : impl_->accumulating) {
        if (state.held) {
            impl_->published[action].held = true;
        }
    }
}

DigitalAction InputSystem::digital(ActionId action) const {
    const auto it = impl_->published.find(action.value());
    return it == impl_->published.end() ? DigitalAction{} : it->second;
}

Axis2D InputSystem::axis2D(ActionId action) const {
    // Resolved from the level state at query time rather than accumulated,
    // because an axis has no edges -- it is a value, and the value is "which of
    // these keys are down right now".
    const auto down = [this](KeyCode key) {
        return key != KeyCode::Unknown &&
               impl_->keysDown.count(static_cast<std::uint16_t>(key)) != 0;
    };

    for (const Context& context : impl_->contexts) {
        for (const auto& axis : context.map.axes_) {
            if (!(axis.action == action)) {
                continue;
            }
            Axis2D value;
            value.x = (down(axis.positiveX) ? 1.0F : 0.0F) - (down(axis.negativeX) ? 1.0F : 0.0F);
            value.y = (down(axis.positiveY) ? 1.0F : 0.0F) - (down(axis.negativeY) ? 1.0F : 0.0F);

            float magnitude = std::sqrt(value.x * value.x + value.y * value.y);
            if (magnitude <= axis.tuning.deadZone || magnitude == 0.0F) {
                return {};
            }
            // Normalise on the *composed* magnitude, not per component. Doing
            // it per component leaves a diagonal at length sqrt(2), which is
            // the "faster diagonally" bug.
            if (axis.tuning.normalise && magnitude > 1.0F) {
                value.x /= magnitude;
                value.y /= magnitude;
                magnitude = 1.0F;
            }
            value.x *= axis.tuning.sensitivity;
            value.y *= axis.tuning.sensitivity;
            return value;
        }
        // A consuming context that binds this axis stops the search, so a lower
        // context's WASD does not leak through a menu.
        if (context.config.consumes && context.map.binds(action)) {
            return {};
        }
    }
    return {};
}

void InputSystem::reset() {
    impl_->keysDown.clear();
    impl_->buttonsDown.clear();
    impl_->accumulating.clear();
    impl_->published.clear();
}

} // namespace hp

namespace hp {

void InputMap::bindKeyNamed(std::string_view action, KeyCode key) {
    nameAction(action);
    bindKey(ActionId{action}, key);
}

void InputMap::bindMouseButtonNamed(std::string_view action, MouseButton button) {
    nameAction(action);
    bindMouseButton(ActionId{action}, button);
}

void InputMap::bindAxis2DNamed(std::string_view action, KeyCode negativeX, KeyCode positiveX,
                               KeyCode negativeY, KeyCode positiveY, AxisTuning tuning) {
    nameAction(action);
    bindAxis2D(ActionId{action}, negativeX, positiveX, negativeY, positiveY, tuning);
}

void InputMap::nameAction(std::string_view action) {
    if (action.empty()) {
        return;
    }
    // Keyed by the same hash the bindings use, so a name registered through any
    // route is found by all of them.
    actionNames_.insert_or_assign(ActionId{action}.value(), std::string(action));
}

std::string_view InputMap::actionName(ActionId action) const {
    const auto found = actionNames_.find(action.value());
    return found != actionNames_.end() ? std::string_view{found->second} : std::string_view{};
}

} // namespace hp
