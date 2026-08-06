#include <hp/Serialize.hpp>

#include <hp/Guid.hpp>
#include <hp/Layers.hpp>
// No <hp/Light.hpp>, and its absence is the point: this layer used to name
// `LightType` in four places, and enums now go through one generic path. A leaf
// list that has to grow by one case per enum is the "four switches" failure
// T0053 exists to prevent, arriving one type at a time.
#include <hp/Log.hpp>
#include <hp/Math.hpp>
#include <hp/Profiling.hpp>
// `ShaderValue` is a leaf and not a maths type: a shader parameter's value is
// one to four floats **whose count the document decides**, so it is the one
// leaf here that is not a fixed shape. See `writeLeaf` below for why the arity
// is carried rather than normalised away (T0160.3).
#include <hp/ShaderParams.hpp>

#include <hp/Cook.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace hp {
namespace {

const LogCategory kLog("serialize");

/// Where a leaf value is written: under a key in a mapping, or appended to a
/// sequence.
///
/// **One type list, two destinations, and that is the entire reason this exists.**
/// A sequence of leaves has to read as `materials: [a, b, c]`, not as a list of
/// single-key maps — which is what it was, because `writeLeaf` could only
/// `set(key, ...)`. The obvious fix is a second leaf list that appends instead,
/// and a second list is exactly the drift this file's own comments warn about:
/// the two would disagree the first time a type was added to one of them.
///
/// So the destination becomes a parameter and the list stays singular.
///
/// Passed and held **by value**, like `YamlNode` itself is everywhere in this
/// file: the node is a handle -- a pointer and an index -- and its mutating
/// methods are non-const, so a `const LeafSink&` cannot write through one.
class LeafSink {
public:
    /// @param parent the mapping to write into.
    /// @param key the key to write under.
    /// @returns a sink that writes `key: value` into @p parent.
    static LeafSink keyed(YamlNode parent, std::string_view key) {
        return LeafSink{parent, key, true};
    }

    /// @param sequence the sequence to append to.
    /// @returns a sink that appends a bare value, with no key.
    static LeafSink appended(YamlNode sequence) { return LeafSink{sequence, {}, false}; }

    /// @param value the value to write. @returns nothing.
    void write(bool value) { keyed_ ? node_.set(key_, value) : node_.append(value); }

    /// @param value the value to write. @returns nothing.
    void write(std::int64_t value) { keyed_ ? node_.set(key_, value) : node_.append(value); }

    /// @param value the value to write. @returns nothing.
    void write(std::uint64_t value) { keyed_ ? node_.set(key_, value) : node_.append(value); }

    /// @param value the value to write. @returns nothing.
    void write(double value) { keyed_ ? node_.set(key_, value) : node_.append(value); }

    /// @param value the value to write. @returns nothing.
    void write(std::string_view value) {
        keyed_ ? node_.set(key_, value) : node_.append(value);
    }

    /// @returns a new sequence at this sink's position, for the maths types.
    [[nodiscard]] YamlNode sequence() {
        return keyed_ ? node_.addSequence(key_) : node_.appendSequence();
    }

    /// @returns a new mapping at this sink's position, for a nested object.
    [[nodiscard]] YamlNode map() { return keyed_ ? node_.addMap(key_) : node_.appendMap(); }

private:
    LeafSink(YamlNode node, std::string_view key, bool keyed)
        : node_(node), key_(key), keyed_(keyed) {}

    YamlNode node_;
    std::string_view key_;
    bool keyed_;
};

/// Writes a fixed-length sequence of floats, which is how every maths type is
/// represented: `[1, 2, 3]` reads well in a diff and survives hand-editing.
void writeFloats(LeafSink out, const float* values, std::size_t count) {
    YamlNode sequence = out.sequence();
    for (std::size_t i = 0; i < count; ++i) {
        sequence.append(static_cast<double>(values[i]));
    }
}

/// Reads a fixed-length sequence of floats.
///
/// **All or nothing.** A sequence of the wrong length is refused rather than
/// partially applied, because a half-written vector is a value that looks
/// plausible and is wrong -- the failure mode this whole layer exists to avoid.
bool readFloats(YamlNode node, float* values, std::size_t count) {
    if (!node.isSequence() || node.size() != count) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        double value = 0.0;
        if (!node.at(i).tryRead(value)) {
            return false;
        }
        values[i] = static_cast<float>(value);
    }
    return true;
}

// --- enums ------------------------------------------------------------------
//
// **Enums are written by name, and this is generic rather than a list.** The
// leaf layer used to special-case `LightType` and write its integer, with a
// comment saying names would read better and needed the enum reflected. They
// do, and it does, and `TypeBuilder::value` had already existed for exactly
// that since T0053 without anything using it.
//
// The cost of the integer was not only readability. `type: 2` silently means
// something else the moment a value is inserted into the middle of an enum —
// and worse, `readLeaf` parsed *only* an integer, so a hand-authored
// `Light: {type: Spot}` (T0139's whole point) failed to read, hit the
// lenient-read rule that leaves an unreadable field alone, and produced a
// **directional** light with no warning. Measured on `tests/fast`'s own
// authoring fixture, which had `type: Directional` in it and passed because
// Directional is the default.
//
// An enum nobody reflected still works: it falls back to its integer, which is
// what every enum did before. So this adds a capability rather than a
// requirement, and no type has to be edited to keep working.

/// @param type an enum's reflected type.
/// @returns whether any enumerator was registered on it — i.e. whether it was
///          passed to `hp::reflect` and given `value()` calls.
bool hasEnumerators(const entt::meta_type& type) {
    for (auto&& [id, data] : type.data()) {
        if (data.is_static() && data.name() != nullptr) {
            return true;
        }
    }
    return false;
}

/// @param value an enum value.
/// @returns the registered name of the enumerator it equals, or nullptr when
///          the enum has no enumerator with this value — which covers both an
///          unreflected enum and a value that is not one of them.
const char* enumeratorName(const entt::meta_any& value) {
    for (auto&& [id, data] : value.type().data()) {
        if (!data.is_static() || data.name() == nullptr) {
            continue;
        }
        if (data.get({}) == value) {
            return data.name();
        }
    }
    return nullptr;
}

/// @param type the enum type to search.
/// @param name an enumerator's registered name.
/// @param out receives the enumerator's value when one matches.
/// @returns whether the type has an enumerator with that name.
bool enumeratorByName(const entt::meta_type& type, std::string_view name, entt::meta_any& out) {
    for (auto&& [id, data] : type.data()) {
        if (!data.is_static() || data.name() == nullptr) {
            continue;
        }
        if (name == data.name()) {
            out = data.get({});
            return true;
        }
    }
    return false;
}

/// Assigns an integer to an enum, refusing a value the enum does not have.
///
/// **Refused rather than trusted**, matching what the hand-written `LightType`
/// case did: a file naming a value this build does not have would otherwise
/// produce an out-of-range enum, and the switches that read one have no default
/// case by design. An *unreflected* enum cannot be checked this way and is
/// trusted, because refusing every value of one would be worse.
/// @param value a reference to the target enum.
/// @param n the integer read from the document.
/// @returns whether it was assigned.
bool assignEnumInteger(entt::meta_any& value, std::int64_t n) {
    const entt::meta_type type = value.type();

    // **`allow_cast` on a mutable `meta_any` returns `bool`, not `meta_any`.**
    // The const overload converts and returns the result; the non-const one
    // converts *in place* and reports whether it could. Writing
    // `meta_any converted = anyInt.allow_cast(type)` therefore compiles, and
    // builds an any holding **`true`** -- which is a perfectly valid `meta_any`,
    // so every check downstream passes and the value is silently wrong. Calling
    // `.cast<std::int64_t>()` on it then dereferences null: an assert in a debug
    // build, a **segfault** in release. That is measured, not theoretical -- it
    // cost this change two rounds, and the release-only crash is why.
    //
    // So: construct, convert in place, check the bool.
    entt::meta_any converted{n};
    if (!converted.allow_cast(type)) {
        return false;
    }
    if (hasEnumerators(type) && enumeratorName(converted) == nullptr) {
        return false;
    }
    return value.assign(std::move(converted));
}

/// Floats are cooked as their exact bit pattern, not as text.
///
/// The YAML path spends 17 significant digits to survive a round trip; the
/// binary path does not have to, and a bit pattern is both smaller and exactly
/// reversible by construction.
void writeFloat(std::vector<std::byte>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    writeU32(out, bits);
}

bool readFloat(const std::vector<std::byte>& bytes, std::size_t& cursor, float& out) {
    std::uint32_t bits = 0;
    if (!readU32(bytes, cursor, bits)) {
        return false;
    }
    std::memcpy(&out, &bits, sizeof bits);
    return true;
}

bool cookFloats(const float* values, std::size_t count, std::vector<std::byte>& out) {
    for (std::size_t i = 0; i < count; ++i) {
        writeFloat(out, values[i]);
    }
    return true;
}

bool uncookFloats(const std::vector<std::byte>& bytes, std::size_t& cursor, float* values,
                  std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (!readFloat(bytes, cursor, values[i])) {
            return false;
        }
    }
    return true;
}

/// Writes a leaf type, or reports that it is not one.
///
/// The whole hand-written surface of this layer. Everything else is derived from
/// reflection, so this list is what "the leaf types reflection bottoms out in"
/// actually means -- and it is deliberately short.
bool writeLeaf(LeafSink out, const entt::meta_any& value) {
    if (const auto* v = value.try_cast<bool>()) {
        out.write(*v);
        return true;
    }
    if (const auto* v = value.try_cast<std::string>()) {
        out.write(std::string_view{*v});
        return true;
    }
    if (const auto* v = value.try_cast<Guid>()) {
        // As its canonical string, not as a raw integer: a GUID in a diff is
        // something a person has to be able to match against another file.
        out.write(v->toString());
        return true;
    }
    if (const entt::meta_type type = value.type(); type && type.is_enum()) {
        // By name when the enum was reflected, by integer when it was not --
        // see the block above `writeFloat`. Every enum takes this path; there
        // is deliberately no per-enum case to forget to add.
        if (const char* name = enumeratorName(value)) {
            out.write(std::string_view{name});
            return true;
        }
        if (const entt::meta_any asInteger = value.allow_cast<std::int64_t>(); asInteger) {
            out.write(asInteger.cast<std::int64_t>());
            return true;
        }
        return false;
    }
    if (const auto* v = value.try_cast<LayerMask>()) {
        // As a plain integer, so the on-disk shape is what it was before
        // `cullingMask` became a typed mask (T0085) rather than a nested
        // `{bits: N}` map. **Names, not numbers, is the eventual goal** — T0085.1
        // puts layer names in project settings, and that is the point at which
        // this should write a sequence of names instead. Recorded here because
        // this is the line that would have to change.
        out.write(static_cast<std::uint64_t>(v->bits));
        return true;
    }
    if (const auto* v = value.try_cast<float>()) {
        out.write(static_cast<double>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<double>()) {
        out.write(*v);
        return true;
    }
    if (const auto* v = value.try_cast<ShaderValue>()) {
        // **A scalar stays a scalar** (T0160.3). `value: 0.5` is what a person
        // writes for a `float` parameter, and writing it back as
        // `[0.5, 0, 0, 0]` would rewrite their file into three components it
        // never had. The shader's declared type is what decides how many
        // components reach the GPU; this is only what the document said.
        const std::size_t count = v->count == 0 ? 1U : std::min<std::size_t>(v->count, 4U);
        if (count == 1) {
            out.write(static_cast<double>(v->components[0]));
        } else {
            writeFloats(out, v->components, count);
        }
        return true;
    }
    if (const auto* v = value.try_cast<float2>()) {
        writeFloats(out, &v->x, 2);
        return true;
    }
    if (const auto* v = value.try_cast<float3>()) {
        writeFloats(out, &v->x, 3);
        return true;
    }
    if (const auto* v = value.try_cast<float4>()) {
        writeFloats(out, &v->x, 4);
        return true;
    }
    if (const auto* v = value.try_cast<Quaternion>()) {
        // x, y, z, w -- the order Diligent stores them in, so what is written
        // matches what a debugger shows.
        const float parts[4] = {v->q.x, v->q.y, v->q.z, v->q.w};
        writeFloats(out, parts, 4);
        return true;
    }
    if (const auto* v = value.try_cast<float4x4>()) {
        writeFloats(out, &v->_11, 16);
        return true;
    }

    // Integers last: bool and the floating types are matched above, so anything
    // reaching here that converts to an integer really is one.
    if (const auto* v = value.try_cast<std::int8_t>()) {
        out.write(static_cast<std::int64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::int16_t>()) {
        out.write(static_cast<std::int64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::int32_t>()) {
        out.write(static_cast<std::int64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::int64_t>()) {
        out.write(*v);
        return true;
    }
    if (const auto* v = value.try_cast<std::uint8_t>()) {
        out.write(static_cast<std::uint64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::uint16_t>()) {
        out.write(static_cast<std::uint64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::uint32_t>()) {
        out.write(static_cast<std::uint64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::uint64_t>()) {
        out.write(*v);
        return true;
    }
    return false;
}

/// Reads a leaf type in place, or reports that it is not one.
bool readLeaf(YamlNode node, entt::meta_any& value) {
    if (auto* v = value.try_cast<bool>()) {
        return node.tryRead(*v);
    }
    if (auto* v = value.try_cast<std::string>()) {
        return node.tryRead(*v);
    }
    if (auto* v = value.try_cast<Guid>()) {
        std::string text;
        if (!node.tryRead(text)) {
            return false;
        }
        const auto parsed = Guid::parse(text);
        if (!parsed) {
            return false;
        }
        *v = *parsed;
        return true;
    }
    if (const entt::meta_type type = value.type(); type && type.is_enum()) {
        // **The integer is tried first, and that ordering is what makes both
        // work.** `tryRead(std::int64_t&)` refuses a non-numeric scalar, so
        // `Directional` falls through to the name; reading the string first
        // would swallow `0` as the *name* "0" and never find an enumerator.
        std::int64_t n = 0;
        if (node.tryRead(n)) {
            return assignEnumInteger(value, n);
        }
        std::string name;
        if (!node.tryRead(name)) {
            return false;
        }
        entt::meta_any enumerator;
        if (!enumeratorByName(type, name, enumerator)) {
            // Named, and this build has no such value. Reported rather than
            // defaulted: an enum silently left at its default is exactly the
            // bug this replaced, and a name is a thing a person can misspell.
            HP_LOG_WARN(kLog, "'{}' is not a value of this enum; leaving the field alone", name);
            return false;
        }
        return value.assign(std::move(enumerator));
    }
    if (auto* v = value.try_cast<LayerMask>()) {
        std::uint64_t n = 0;
        if (!node.tryRead(n)) {
            return false;
        }
        v->bits = static_cast<std::uint32_t>(n);
        return true;
    }
    if (auto* v = value.try_cast<float>()) {
        double d = 0.0;
        if (!node.tryRead(d)) {
            return false;
        }
        *v = static_cast<float>(d);
        return true;
    }
    if (auto* v = value.try_cast<double>()) {
        return node.tryRead(*v);
    }
    if (auto* v = value.try_cast<ShaderValue>()) {
        // Both shapes a person may write, and a bare scalar first for the same
        // reason the enum path tries the integer first — `tryRead(double&)`
        // refuses a sequence, so the two cannot swallow each other.
        *v = ShaderValue{};
        double scalar = 0.0;
        if (node.tryRead(scalar)) {
            v->components[0] = static_cast<float>(scalar);
            v->count = 1;
            return true;
        }
        if (!node.isSequence() || node.size() == 0 || node.size() > 4) {
            return false;
        }
        for (std::size_t i = 0; i < node.size(); ++i) {
            double component = 0.0;
            if (!node.at(i).tryRead(component)) {
                // All or nothing, like every other vector here: a half-read
                // parameter is a plausible-looking wrong value.
                *v = ShaderValue{};
                return false;
            }
            v->components[i] = static_cast<float>(component);
        }
        v->count = static_cast<std::uint8_t>(node.size());
        return true;
    }
    if (auto* v = value.try_cast<float2>()) {
        return readFloats(node, &v->x, 2);
    }
    if (auto* v = value.try_cast<float3>()) {
        return readFloats(node, &v->x, 3);
    }
    if (auto* v = value.try_cast<float4>()) {
        return readFloats(node, &v->x, 4);
    }
    if (auto* v = value.try_cast<Quaternion>()) {
        float parts[4] = {0.0F, 0.0F, 0.0F, 1.0F};
        if (!readFloats(node, parts, 4)) {
            return false;
        }
        v->q = float4(parts[0], parts[1], parts[2], parts[3]);
        return true;
    }
    if (auto* v = value.try_cast<float4x4>()) {
        return readFloats(node, &v->_11, 16);
    }

    if (auto* v = value.try_cast<std::int8_t>()) {
        std::int64_t n = 0;
        if (!node.tryRead(n)) {
            return false;
        }
        *v = static_cast<std::int8_t>(n);
        return true;
    }
    if (auto* v = value.try_cast<std::int16_t>()) {
        std::int64_t n = 0;
        if (!node.tryRead(n)) {
            return false;
        }
        *v = static_cast<std::int16_t>(n);
        return true;
    }
    if (auto* v = value.try_cast<std::int32_t>()) {
        std::int64_t n = 0;
        if (!node.tryRead(n)) {
            return false;
        }
        *v = static_cast<std::int32_t>(n);
        return true;
    }
    if (auto* v = value.try_cast<std::int64_t>()) {
        return node.tryRead(*v);
    }
    if (auto* v = value.try_cast<std::uint8_t>()) {
        std::uint64_t n = 0;
        if (!node.tryRead(n)) {
            return false;
        }
        *v = static_cast<std::uint8_t>(n);
        return true;
    }
    if (auto* v = value.try_cast<std::uint16_t>()) {
        std::uint64_t n = 0;
        if (!node.tryRead(n)) {
            return false;
        }
        *v = static_cast<std::uint16_t>(n);
        return true;
    }
    if (auto* v = value.try_cast<std::uint32_t>()) {
        std::uint64_t n = 0;
        if (!node.tryRead(n)) {
            return false;
        }
        *v = static_cast<std::uint32_t>(n);
        return true;
    }
    if (auto* v = value.try_cast<std::uint64_t>()) {
        return node.tryRead(*v);
    }
    return false;
}

} // namespace

bool writeReflected(YamlNode parent, std::string_view key, const entt::meta_any& value) {
    HP_PROFILE_ZONE();

    if (!value || !parent.valid()) {
        return false;
    }
    if (writeLeaf(LeafSink::keyed(parent, key), value)) {
        return true;
    }

    // Sequence containers come from entt::meta's container support, which
    // Reflect.hpp turns on for everything -- a std::vector member is reachable
    // without the type having to say so (53.6).
    if (auto sequence = value.as_sequence_container()) {
        YamlNode out = parent.addSequence(key);
        for (auto element : sequence) {
            // **A leaf is appended bare**, so a vector of GUIDs reads as
            // `materials: [a, b, c]`. It used to be written as a list of
            // single-key maps -- `- 0: a` -- because the leaf writer could only
            // set a key, and that is unreadable in a diff and worse to
            // hand-author, which is what the whole format is for.
            if (writeLeaf(LeafSink::appended(out), element)) {
                continue;
            }
            // Anything else becomes a map in the sequence, which is what a
            // vector of structs should look like.
            YamlNode holder = out.appendMap();
            if (!writeProperties(holder, element)) {
                return false;
            }
        }
        return true;
    }

    // Anything else with registered properties becomes a nested map.
    const entt::meta_type type = value.type();
    if (type && type.data().begin() != type.data().end()) {
        YamlNode nested = parent.addMap(key);
        return writeProperties(nested, value);
    }

    HP_LOG_WARN(kLog,
                "'{}' has a type that is neither a known leaf, a sequence, nor a reflected type; "
                "nothing written. Register it with hp::reflect, or add it to the leaf list.",
                key);
    return false;
}

namespace {

/// The recursive read, threading a **reference** rather than a value.
///
/// This is the whole of the nested-struct fix. `readReflected` and
/// `readProperties` take `meta_any` by value so a `forward_as_meta` temporary
/// binds at the call site; passing that copy down the recursion is what lost
/// the mutations, because each level was writing into its own parameter.
/// Measured: a nested struct wrote correct YAML and read back as all defaults,
/// while a leaf at the same level round-tripped fine -- which is exactly the
/// shape of a bug that only appears one level down.
bool readInto(YamlNode node, entt::meta_any& value);

bool readPropsInto(YamlNode node, entt::meta_any& value);

bool readInto(YamlNode node, entt::meta_any& value) {
    HP_PROFILE_ZONE();

    // An absent field is not an error. It leaves the target at whatever it
    // already holds, which is what lets a component gain a property without
    // invalidating every file written before it existed.
    if (!node.valid()) {
        return true;
    }
    if (!value) {
        return false;
    }
    if (readLeaf(node, value)) {
        return true;
    }

    if (auto sequence = value.as_sequence_container()) {
        if (!node.isSequence()) {
            return false;
        }
        sequence.clear();
        for (std::size_t i = 0; i < node.size(); ++i) {
            if (!sequence.resize(sequence.size() + 1)) {
                return false;
            }
            auto element = sequence[sequence.size() - 1];
            YamlNode item = node.at(i);
            entt::meta_any elementRef = element.as_ref();
            // Mirrors the write: a bare leaf first, a nested map second.
            if (readLeaf(item, elementRef)) {
                continue;
            }
            if (!readPropsInto(item, elementRef)) {
                return false;
            }
        }
        return true;
    }

    const entt::meta_type type = value.type();
    if (type && type.data().begin() != type.data().end()) {
        return readPropsInto(node, value);
    }
    return false;
}

} // namespace

bool writeProperties(YamlNode parent, const entt::meta_any& value) {
    HP_PROFILE_ZONE();

    if (!value || !parent.valid()) {
        return false;
    }
    const entt::meta_type type = value.type();
    if (!type) {
        return false;
    }

    bool any = false;
    for (auto&& [id, data] : type.data()) {
        // The name, not the id. Writing the hash would be a file no person can
        // read or repair, which is the whole argument for YAML being the source
        // of truth -- and it is why T0053's missing names had to be fixed before
        // this function could exist.
        if (data.name() == nullptr) {
            HP_LOG_WARN(kLog, "type '{}' has a property registered without a name; skipping it",
                        type.name() != nullptr ? type.name() : "<unnamed>");
            continue;
        }
        entt::meta_any field = data.get(value);
        if (!field) {
            continue;
        }
        any = true;
        if (!writeReflected(parent, data.name(), field)) {
            return false;
        }
    }
    return any;
}

namespace {

bool readPropsInto(YamlNode node, entt::meta_any& value) {
    HP_PROFILE_ZONE();

    if (!value || !node.valid()) {
        return false;
    }
    const entt::meta_type type = value.type();
    if (!type) {
        return false;
    }

    bool any = false;
    for (auto&& [id, data] : type.data()) {
        if (data.name() == nullptr) {
            continue;
        }
        any = true;

        // Read into a copy, then set it back. `data.get` on a non-reference
        // member yields a value rather than a handle, so mutating it in place
        // would update a temporary and silently drop the field -- which looks
        // exactly like a file that did not contain it.
        entt::meta_any field = data.get(value);
        if (!field) {
            continue;
        }
        YamlNode child = node[data.name()];
        if (!child.valid()) {
            // Absent: keep whatever the target already has.
            continue;
        }
        entt::meta_any fieldRef = field.as_ref();
        if (!readInto(child, fieldRef)) {
            HP_LOG_WARN(kLog, "could not read property '{}'; leaving it at its current value",
                        data.name());
            continue;
        }
        if (!data.set(value, field)) {
            HP_LOG_WARN(kLog, "property '{}' is read-only; skipping", data.name());
        }
    }
    return any;
}

} // namespace

namespace {

/// Cooks one leaf value, or reports that it is not a leaf.
///
/// Mirrors `writeLeaf` exactly. The two lists must stay in step, and the tests
/// round-trip the same components through both paths for that reason -- a type
/// that YAML can write and binary cannot would show up as a cook that silently
/// drops a field.
bool cookLeaf(const entt::meta_any& value, std::vector<std::byte>& out) {
    if (const auto* v = value.try_cast<bool>()) {
        out.push_back(static_cast<std::byte>(*v ? 1 : 0));
        return true;
    }
    if (const auto* v = value.try_cast<std::string>()) {
        writeString(out, *v);
        return true;
    }
    if (const auto* v = value.try_cast<Guid>()) {
        writeU64(out, v->value());
        return true;
    }
    if (const entt::meta_type type = value.type(); type && type.is_enum()) {
        // **As its integer, and deliberately not by name.** The cook is a cache
        // keyed on a hash of its source (`Cook.hpp`), so it is never read by a
        // person and never outlives a rename -- an enum whose enumerators moved
        // invalidates the cook rather than being silently misread from it. The
        // YAML beside it is where the name earns its cost.
        const entt::meta_any asInteger = value.allow_cast<std::int64_t>();
        if (!asInteger) {
            return false;
        }
        writeU64(out, static_cast<std::uint64_t>(asInteger.cast<std::int64_t>()));
        return true;
    }
    if (const auto* v = value.try_cast<LayerMask>()) {
        // U64 rather than U32, matching every other integer in this format:
        // widths are uniform here so the reader never has to know which one a
        // field used.
        writeU64(out, static_cast<std::uint64_t>(v->bits));
        return true;
    }
    if (const auto* v = value.try_cast<float>()) {
        writeFloat(out, *v);
        return true;
    }
    if (const auto* v = value.try_cast<double>()) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, v, sizeof bits);
        writeU64(out, bits);
        return true;
    }
    if (const auto* v = value.try_cast<ShaderValue>()) {
        // Four floats and the count, always — a fixed record, because the cook
        // is a cache read only by this code and paying seventeen bytes for
        // uniformity is cheaper than a variable-length one nobody can skip.
        if (!cookFloats(v->components, 4, out)) {
            return false;
        }
        out.push_back(static_cast<std::byte>(v->count));
        return true;
    }
    if (const auto* v = value.try_cast<float2>()) {
        return cookFloats(&v->x, 2, out);
    }
    if (const auto* v = value.try_cast<float3>()) {
        return cookFloats(&v->x, 3, out);
    }
    if (const auto* v = value.try_cast<float4>()) {
        return cookFloats(&v->x, 4, out);
    }
    if (const auto* v = value.try_cast<Quaternion>()) {
        const float parts[4] = {v->q.x, v->q.y, v->q.z, v->q.w};
        return cookFloats(parts, 4, out);
    }
    if (const auto* v = value.try_cast<float4x4>()) {
        return cookFloats(&v->_11, 16, out);
    }
    if (const auto* v = value.try_cast<std::int8_t>()) {
        writeU64(out, static_cast<std::uint64_t>(static_cast<std::int64_t>(*v)));
        return true;
    }
    if (const auto* v = value.try_cast<std::int16_t>()) {
        writeU64(out, static_cast<std::uint64_t>(static_cast<std::int64_t>(*v)));
        return true;
    }
    if (const auto* v = value.try_cast<std::int32_t>()) {
        writeU64(out, static_cast<std::uint64_t>(static_cast<std::int64_t>(*v)));
        return true;
    }
    if (const auto* v = value.try_cast<std::int64_t>()) {
        writeU64(out, static_cast<std::uint64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::uint8_t>()) {
        writeU64(out, static_cast<std::uint64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::uint16_t>()) {
        writeU64(out, static_cast<std::uint64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::uint32_t>()) {
        writeU64(out, static_cast<std::uint64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::uint64_t>()) {
        writeU64(out, *v);
        return true;
    }
    return false;
}

/// Reads one leaf value in place, or reports that it is not a leaf.
bool uncookLeaf(const std::vector<std::byte>& bytes, std::size_t& cursor, entt::meta_any& value) {
    if (auto* v = value.try_cast<bool>()) {
        if (cursor >= bytes.size()) {
            return false;
        }
        *v = static_cast<unsigned char>(bytes[cursor++]) != 0;
        return true;
    }
    if (auto* v = value.try_cast<std::string>()) {
        return readString(bytes, cursor, *v);
    }
    if (auto* v = value.try_cast<Guid>()) {
        std::uint64_t raw = 0;
        if (!readU64(bytes, cursor, raw)) {
            return false;
        }
        *v = Guid{raw};
        return true;
    }
    if (const entt::meta_type type = value.type(); type && type.is_enum()) {
        std::uint64_t raw = 0;
        if (!readU64(bytes, cursor, raw)) {
            return false;
        }
        // A value this build's enum does not have means "re-cook", which is
        // what every other failure in this path means -- see `Cook.hpp`.
        return assignEnumInteger(value, static_cast<std::int64_t>(raw));
    }
    if (auto* v = value.try_cast<LayerMask>()) {
        std::uint64_t raw = 0;
        if (!readU64(bytes, cursor, raw)) {
            return false;
        }
        v->bits = static_cast<std::uint32_t>(raw);
        return true;
    }
    if (auto* v = value.try_cast<float>()) {
        return readFloat(bytes, cursor, *v);
    }
    if (auto* v = value.try_cast<double>()) {
        std::uint64_t bits = 0;
        if (!readU64(bytes, cursor, bits)) {
            return false;
        }
        std::memcpy(v, &bits, sizeof bits);
        return true;
    }
    if (auto* v = value.try_cast<ShaderValue>()) {
        if (!uncookFloats(bytes, cursor, v->components, 4) || cursor >= bytes.size()) {
            return false;
        }
        const auto count = static_cast<std::uint8_t>(bytes[cursor++]);
        // Clamped rather than trusted: a corrupt cook must mean "re-cook", and
        // a count of 200 read into a four-float array is the one way this
        // record could be worse than useless.
        v->count = count == 0 || count > 4 ? 1U : count;
        return true;
    }
    if (auto* v = value.try_cast<float2>()) {
        return uncookFloats(bytes, cursor, &v->x, 2);
    }
    if (auto* v = value.try_cast<float3>()) {
        return uncookFloats(bytes, cursor, &v->x, 3);
    }
    if (auto* v = value.try_cast<float4>()) {
        return uncookFloats(bytes, cursor, &v->x, 4);
    }
    if (auto* v = value.try_cast<Quaternion>()) {
        float parts[4] = {0.0F, 0.0F, 0.0F, 1.0F};
        if (!uncookFloats(bytes, cursor, parts, 4)) {
            return false;
        }
        v->q = float4(parts[0], parts[1], parts[2], parts[3]);
        return true;
    }
    if (auto* v = value.try_cast<float4x4>()) {
        return uncookFloats(bytes, cursor, &v->_11, 16);
    }

    std::uint64_t raw = 0;
    if (auto* v = value.try_cast<std::int8_t>()) {
        if (!readU64(bytes, cursor, raw)) {
            return false;
        }
        *v = static_cast<std::int8_t>(static_cast<std::int64_t>(raw));
        return true;
    }
    if (auto* v = value.try_cast<std::int16_t>()) {
        if (!readU64(bytes, cursor, raw)) {
            return false;
        }
        *v = static_cast<std::int16_t>(static_cast<std::int64_t>(raw));
        return true;
    }
    if (auto* v = value.try_cast<std::int32_t>()) {
        if (!readU64(bytes, cursor, raw)) {
            return false;
        }
        *v = static_cast<std::int32_t>(static_cast<std::int64_t>(raw));
        return true;
    }
    if (auto* v = value.try_cast<std::int64_t>()) {
        if (!readU64(bytes, cursor, raw)) {
            return false;
        }
        *v = static_cast<std::int64_t>(raw);
        return true;
    }
    if (auto* v = value.try_cast<std::uint8_t>()) {
        if (!readU64(bytes, cursor, raw)) {
            return false;
        }
        *v = static_cast<std::uint8_t>(raw);
        return true;
    }
    if (auto* v = value.try_cast<std::uint16_t>()) {
        if (!readU64(bytes, cursor, raw)) {
            return false;
        }
        *v = static_cast<std::uint16_t>(raw);
        return true;
    }
    if (auto* v = value.try_cast<std::uint32_t>()) {
        if (!readU64(bytes, cursor, raw)) {
            return false;
        }
        *v = static_cast<std::uint32_t>(raw);
        return true;
    }
    if (auto* v = value.try_cast<std::uint64_t>()) {
        return readU64(bytes, cursor, *v);
    }
    return false;
}

bool cookValue(const entt::meta_any& value, std::vector<std::byte>& out) {
    if (cookLeaf(value, out)) {
        return true;
    }

    // **Sequences, which the binary path did not handle at all until T0060.6.**
    // Nothing reflected had a sequence property before `MeshRenderer::materials`,
    // so `cookProperties` simply returned false for one and the whole component
    // failed to cook -- which surfaced as a cooked scene that silently differed
    // from the YAML beside it, not as an error at the point of the omission.
    // The YAML path had handled sequences since T0020; the two lists had drifted
    // exactly the way this file's comments warn about, and only a type that
    // exercised both caught it.
    if (auto sequence = value.as_sequence_container()) {
        writeU64(out, static_cast<std::uint64_t>(sequence.size()));
        for (auto element : sequence) {
            if (!cookValue(element, out)) {
                return false;
            }
        }
        return true;
    }

    const entt::meta_type type = value.type();
    if (type && type.data().begin() != type.data().end()) {
        return cookProperties(value, out);
    }
    return false;
}

bool uncookValue(const std::vector<std::byte>& bytes, std::size_t& cursor,
                 entt::meta_any& value) {
    if (uncookLeaf(bytes, cursor, value)) {
        return true;
    }

    if (auto sequence = value.as_sequence_container()) {
        std::uint64_t count = 0;
        if (!readU64(bytes, cursor, count)) {
            return false;
        }
        // **Bounded by what remains**, before a single element is read. A
        // corrupt or truncated payload claiming four billion elements must not
        // become a four-billion-element resize; a cook is a cache and every way
        // it can be wrong has to mean "re-cook" rather than an allocation the
        // process does not survive.
        if (count > bytes.size() - cursor) {
            return false;
        }
        sequence.clear();
        for (std::uint64_t i = 0; i < count; ++i) {
            if (!sequence.resize(sequence.size() + 1)) {
                return false;
            }
            auto element = sequence[sequence.size() - 1];
            entt::meta_any elementRef = element.as_ref();
            if (!uncookValue(bytes, cursor, elementRef)) {
                return false;
            }
        }
        return true;
    }

    const entt::meta_type type = value.type();
    if (type && type.data().begin() != type.data().end()) {
        return readCookedProperties(bytes, cursor, value.as_ref());
    }
    return false;
}

} // namespace

bool cookProperties(const entt::meta_any& value, std::vector<std::byte>& out) {
    HP_PROFILE_ZONE();

    if (!value) {
        return false;
    }
    const entt::meta_type type = value.type();
    if (!type || type.data().begin() == type.data().end()) {
        return false;
    }

    // Count first, so the reader knows how many records follow without needing
    // a terminator it could mistake for data.
    std::uint32_t count = 0;
    for (auto&& [id, data] : type.data()) {
        if (data.name() != nullptr) {
            ++count;
        }
    }
    writeU32(out, count);

    for (auto&& [id, data] : type.data()) {
        if (data.name() == nullptr) {
            continue;
        }
        entt::meta_any field = data.get(value);
        if (!field) {
            return false;
        }

        // Hash, not position: reordering a type's registrations must not
        // invalidate already-cooked data.
        writeU32(out, static_cast<std::uint32_t>(hashSource(data.name())));

        // The length goes in as a placeholder and is patched once the value is
        // written, because a nested object's size is not known in advance. It
        // is what lets a reader skip a property it does not recognise instead
        // of losing sync and misreading everything after it.
        const std::size_t lengthAt = out.size();
        writeU64(out, 0);
        const std::size_t valueAt = out.size();
        if (!cookValue(field, out)) {
            return false;
        }
        const std::uint64_t length = static_cast<std::uint64_t>(out.size() - valueAt);
        for (int i = 0; i < 8; ++i) {
            out[lengthAt + static_cast<std::size_t>(i)] =
                static_cast<std::byte>((length >> (i * 8)) & 0xFFU);
        }
    }
    return true;
}

bool readCookedProperties(const std::vector<std::byte>& bytes, std::size_t& cursor,
                          entt::meta_any value) {
    HP_PROFILE_ZONE();

    if (!value) {
        return false;
    }
    const entt::meta_type type = value.type();
    if (!type) {
        return false;
    }

    std::uint32_t count = 0;
    if (!readU32(bytes, cursor, count)) {
        return false;
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t nameHash = 0;
        std::uint64_t length = 0;
        if (!readU32(bytes, cursor, nameHash) || !readU64(bytes, cursor, length)) {
            return false;
        }
        if (length > bytes.size() - cursor) {
            return false;
        }
        const std::size_t next = cursor + static_cast<std::size_t>(length);

        // Find the property this record names. A record for a property the type
        // no longer has is skipped by its length -- backward compatibility, and
        // the reason the length is there at all.
        bool handled = false;
        for (auto&& [id, data] : type.data()) {
            if (data.name() == nullptr
                || static_cast<std::uint32_t>(hashSource(data.name())) != nameHash) {
                continue;
            }
            entt::meta_any field = data.get(value);
            if (!field) {
                break;
            }
            entt::meta_any fieldRef = field.as_ref();
            std::size_t inner = cursor;
            if (uncookValue(bytes, inner, fieldRef)) {
                (void)data.set(value, field);
            }
            handled = true;
            break;
        }
        (void)handled;
        cursor = next;
    }
    return true;
}

bool readReflected(YamlNode node, entt::meta_any value) {
    return readInto(node, value);
}

bool readProperties(YamlNode node, entt::meta_any value) {
    return readPropsInto(node, value);
}

} // namespace hp
