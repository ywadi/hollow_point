// Binding files: InputMap to and from YAML (T0068.7).
//
// Separate from `Input.cpp` so the action layer does not pull in the serializer,
// and so this file is the only place that knows what a binding file looks like.
//
// **The format stores names for everything.** An action is written as "Jump",
// not as its FNV-1a hash and not as an index into a list. The hash would work
// perfectly and produce a file nobody can read or repair by hand; an index would
// break the moment somebody inserts an action. Since the whole point of 68.7 is
// that a *player* can rebind, and a rebinding file has to survive being edited,
// readability is a requirement rather than a nicety.

#include <hp/Input.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Yaml.hpp>

#include <array>
#include <utility>

namespace hp {
namespace {

const LogCategory kLog("input.bindings");

/// Key codes and the identifiers written to a file.
///
/// **These strings are a data format and must never change.** Changing one
/// silently invalidates that binding in every file a player has saved, and the
/// symptom is a control that stopped working with no error anywhere. What a
/// rebinding UI *shows* is a different string, localised, and T0112's problem.
constexpr std::array<std::pair<KeyCode, std::string_view>, 55> kKeyNames{{
    {KeyCode::Escape, "Escape"},
    {KeyCode::Space, "Space"},
    {KeyCode::Enter, "Enter"},
    {KeyCode::Tab, "Tab"},
    {KeyCode::Backspace, "Backspace"},
    {KeyCode::Left, "Left"},
    {KeyCode::Right, "Right"},
    {KeyCode::Up, "Up"},
    {KeyCode::Down, "Down"},
    {KeyCode::A, "A"},
    {KeyCode::B, "B"},
    {KeyCode::C, "C"},
    {KeyCode::D, "D"},
    {KeyCode::E, "E"},
    {KeyCode::F, "F"},
    {KeyCode::G, "G"},
    {KeyCode::H, "H"},
    {KeyCode::I, "I"},
    {KeyCode::J, "J"},
    {KeyCode::K, "K"},
    {KeyCode::L, "L"},
    {KeyCode::M, "M"},
    {KeyCode::N, "N"},
    {KeyCode::O, "O"},
    {KeyCode::P, "P"},
    {KeyCode::Q, "Q"},
    {KeyCode::R, "R"},
    {KeyCode::S, "S"},
    {KeyCode::T, "T"},
    {KeyCode::U, "U"},
    {KeyCode::V, "V"},
    {KeyCode::W, "W"},
    {KeyCode::X, "X"},
    {KeyCode::Y, "Y"},
    {KeyCode::Z, "Z"},
    {KeyCode::Num0, "Num0"},
    {KeyCode::Num1, "Num1"},
    {KeyCode::Num2, "Num2"},
    {KeyCode::Num3, "Num3"},
    {KeyCode::Num4, "Num4"},
    {KeyCode::Num5, "Num5"},
    {KeyCode::Num6, "Num6"},
    {KeyCode::Num7, "Num7"},
    {KeyCode::Num8, "Num8"},
    {KeyCode::Num9, "Num9"},
    {KeyCode::F1, "F1"},
    {KeyCode::F2, "F2"},
    {KeyCode::F3, "F3"},
    {KeyCode::F4, "F4"},
    {KeyCode::F5, "F5"},
    {KeyCode::F6, "F6"},
    {KeyCode::F7, "F7"},
    {KeyCode::F8, "F8"},
    {KeyCode::F9, "F9"},
    {KeyCode::F10, "F10"},
}};

constexpr std::array<std::pair<MouseButton, std::string_view>, 5> kButtonNames{{
    {MouseButton::Left, "Left"},
    {MouseButton::Right, "Right"},
    {MouseButton::Middle, "Middle"},
    {MouseButton::X1, "X1"},
    {MouseButton::X2, "X2"},
}};

/// Reads a key from a mapping field, reporting whether it was usable.
bool readKey(YamlNode node, std::string_view field, KeyCode& out) {
    std::string text;
    if (!node[field].tryRead(text)) {
        return false;
    }
    out = keyCodeFromName(text);
    if (out == KeyCode::Unknown) {
        HP_LOG_WARN(kLog, "unknown key '{}' in binding file; skipping this binding", text);
        return false;
    }
    return true;
}

} // namespace

std::string_view keyCodeName(KeyCode key) {
    for (const auto& [code, name] : kKeyNames) {
        if (code == key) {
            return name;
        }
    }
    return {};
}

KeyCode keyCodeFromName(std::string_view name) {
    for (const auto& [code, text] : kKeyNames) {
        if (text == name) {
            return code;
        }
    }
    return KeyCode::Unknown;
}

std::string_view mouseButtonName(MouseButton button) {
    for (const auto& [code, name] : kButtonNames) {
        if (code == button) {
            return name;
        }
    }
    return {};
}

MouseButton mouseButtonFromName(std::string_view name) {
    for (const auto& [code, text] : kButtonNames) {
        if (text == name) {
            return code;
        }
    }
    return MouseButton::Unknown;
}

std::string writeInputMap(const InputMap& map) {
    HP_PROFILE_ZONE();

    YamlDocument document;
    YamlNode root = document.root();
    root.set("version", static_cast<std::uint64_t>(kInputMapVersion));

    std::size_t unnamed = 0;

    YamlNode keys = root.addSequence("keys");
    for (const auto& binding : map.keys_) {
        const std::string_view action = map.actionName(binding.action);
        const std::string_view key = keyCodeName(binding.key);
        if (action.empty() || key.empty()) {
            ++unnamed;
            continue;
        }
        YamlNode entry = keys.appendMap();
        entry.set("action", action);
        entry.set("key", key);
    }

    YamlNode buttons = root.addSequence("buttons");
    for (const auto& binding : map.buttons_) {
        const std::string_view action = map.actionName(binding.action);
        const std::string_view button = mouseButtonName(binding.button);
        if (action.empty() || button.empty()) {
            ++unnamed;
            continue;
        }
        YamlNode entry = buttons.appendMap();
        entry.set("action", action);
        entry.set("button", button);
    }

    YamlNode axes = root.addSequence("axes");
    for (const auto& binding : map.axes_) {
        const std::string_view action = map.actionName(binding.action);
        if (action.empty()) {
            ++unnamed;
            continue;
        }
        YamlNode entry = axes.appendMap();
        entry.set("action", action);
        entry.set("negativeX", keyCodeName(binding.negativeX));
        entry.set("positiveX", keyCodeName(binding.positiveX));
        entry.set("negativeY", keyCodeName(binding.negativeY));
        entry.set("positiveY", keyCodeName(binding.positiveY));
        entry.set("deadZone", static_cast<double>(binding.tuning.deadZone));
        entry.set("sensitivity", static_cast<double>(binding.tuning.sensitivity));
        entry.set("normalise", binding.tuning.normalise);
    }

    if (unnamed > 0) {
        // Loud rather than quiet. A binding silently absent from a saved file
        // is a control the player rebound and lost, and the cause -- an action
        // bound by id and never named -- is invisible from the file.
        HP_LOG_ERROR(kLog,
                     "{} binding(s) were not written because their action has no registered name. "
                     "Bind with bindKeyNamed/bindMouseButtonNamed/bindAxis2DNamed, or call "
                     "nameAction, or they cannot be saved.",
                     unnamed);
    }
    return document.emit();
}

std::optional<InputMap> parseInputMap(std::string_view yaml, std::string_view name) {
    HP_PROFILE_ZONE();

    auto document = YamlDocument::parse(yaml, name);
    if (!document) {
        return std::nullopt;
    }
    YamlNode root = document->root();

    const auto version = static_cast<std::uint32_t>(root["version"].read(std::uint64_t{0}));
    if (version == 0 || version > kInputMapVersion) {
        HP_LOG_WARN(kLog, "binding file '{}' has version {}; this build reads {}. Ignoring it.",
                    name, version, kInputMapVersion);
        return std::nullopt;
    }

    InputMap map;

    YamlNode keys = root["keys"];
    for (std::size_t i = 0; i < keys.size(); ++i) {
        YamlNode entry = keys.at(i);
        std::string action;
        KeyCode key = KeyCode::Unknown;
        // A single unusable line is skipped rather than failing the file. A
        // binding file is user-editable and may name a key this build does not
        // have; losing every other binding over it would be the worse outcome.
        if (!entry["action"].tryRead(action) || action.empty()
            || !readKey(entry, "key", key)) {
            continue;
        }
        map.bindKeyNamed(action, key);
    }

    YamlNode buttons = root["buttons"];
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        YamlNode entry = buttons.at(i);
        std::string action;
        std::string buttonText;
        if (!entry["action"].tryRead(action) || action.empty()
            || !entry["button"].tryRead(buttonText)) {
            continue;
        }
        const MouseButton button = mouseButtonFromName(buttonText);
        if (button == MouseButton::Unknown) {
            HP_LOG_WARN(kLog, "unknown mouse button '{}' in '{}'; skipping", buttonText, name);
            continue;
        }
        map.bindMouseButtonNamed(action, button);
    }

    YamlNode axes = root["axes"];
    for (std::size_t i = 0; i < axes.size(); ++i) {
        YamlNode entry = axes.at(i);
        std::string action;
        KeyCode negativeX = KeyCode::Unknown;
        KeyCode positiveX = KeyCode::Unknown;
        KeyCode negativeY = KeyCode::Unknown;
        KeyCode positiveY = KeyCode::Unknown;
        if (!entry["action"].tryRead(action) || action.empty()
            || !readKey(entry, "negativeX", negativeX) || !readKey(entry, "positiveX", positiveX)
            || !readKey(entry, "negativeY", negativeY)
            || !readKey(entry, "positiveY", positiveY)) {
            continue;
        }

        // Tuning defaults come from AxisTuning, so a file that omits them --
        // which a hand-written one usually will -- gets the same behaviour as
        // code that never set them.
        AxisTuning tuning;
        tuning.deadZone = static_cast<float>(
            entry["deadZone"].read(static_cast<double>(tuning.deadZone)));
        tuning.sensitivity = static_cast<float>(
            entry["sensitivity"].read(static_cast<double>(tuning.sensitivity)));
        tuning.normalise = entry["normalise"].read(tuning.normalise);

        map.bindAxis2DNamed(action, negativeX, positiveX, negativeY, positiveY, tuning);
    }

    return map;
}

} // namespace hp
