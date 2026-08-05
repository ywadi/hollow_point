#include <hp/Serialize.hpp>

#include <hp/Guid.hpp>
#include <hp/Layers.hpp>
#include <hp/Log.hpp>
#include <hp/Math.hpp>
#include <hp/Profiling.hpp>

#include <hp/Cook.hpp>

#include <cstdint>
#include <cstring>
#include <string>

namespace hp {
namespace {

const LogCategory kLog("serialize");

/// Writes a fixed-length sequence of floats, which is how every maths type is
/// represented: `[1, 2, 3]` reads well in a diff and survives hand-editing.
void writeFloats(YamlNode parent, std::string_view key, const float* values, std::size_t count) {
    YamlNode sequence = parent.addSequence(key);
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
bool writeLeaf(YamlNode parent, std::string_view key, const entt::meta_any& value) {
    if (const auto* v = value.try_cast<bool>()) {
        parent.set(key, *v);
        return true;
    }
    if (const auto* v = value.try_cast<std::string>()) {
        parent.set(key, std::string_view{*v});
        return true;
    }
    if (const auto* v = value.try_cast<Guid>()) {
        // As its canonical string, not as a raw integer: a GUID in a diff is
        // something a person has to be able to match against another file.
        parent.set(key, v->toString());
        return true;
    }
    if (const auto* v = value.try_cast<LayerMask>()) {
        // As a plain integer, so the on-disk shape is what it was before
        // `cullingMask` became a typed mask (T0085) rather than a nested
        // `{bits: N}` map. **Names, not numbers, is the eventual goal** — T0085.1
        // puts layer names in project settings, and that is the point at which
        // this should write a sequence of names instead. Recorded here because
        // this is the line that would have to change.
        parent.set(key, static_cast<std::uint64_t>(v->bits));
        return true;
    }
    if (const auto* v = value.try_cast<float>()) {
        parent.set(key, static_cast<double>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<double>()) {
        parent.set(key, *v);
        return true;
    }
    if (const auto* v = value.try_cast<float3>()) {
        writeFloats(parent, key, &v->x, 3);
        return true;
    }
    if (const auto* v = value.try_cast<float4>()) {
        writeFloats(parent, key, &v->x, 4);
        return true;
    }
    if (const auto* v = value.try_cast<Quaternion>()) {
        // x, y, z, w -- the order Diligent stores them in, so what is written
        // matches what a debugger shows.
        const float parts[4] = {v->q.x, v->q.y, v->q.z, v->q.w};
        writeFloats(parent, key, parts, 4);
        return true;
    }
    if (const auto* v = value.try_cast<float4x4>()) {
        writeFloats(parent, key, &v->_11, 16);
        return true;
    }

    // Integers last: bool and the floating types are matched above, so anything
    // reaching here that converts to an integer really is one.
    if (const auto* v = value.try_cast<std::int8_t>()) {
        parent.set(key, static_cast<std::int64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::int16_t>()) {
        parent.set(key, static_cast<std::int64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::int32_t>()) {
        parent.set(key, static_cast<std::int64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::int64_t>()) {
        parent.set(key, *v);
        return true;
    }
    if (const auto* v = value.try_cast<std::uint8_t>()) {
        parent.set(key, static_cast<std::uint64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::uint16_t>()) {
        parent.set(key, static_cast<std::uint64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::uint32_t>()) {
        parent.set(key, static_cast<std::uint64_t>(*v));
        return true;
    }
    if (const auto* v = value.try_cast<std::uint64_t>()) {
        parent.set(key, *v);
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
    if (writeLeaf(parent, key, value)) {
        return true;
    }

    // Sequence containers come from entt::meta's container support, which
    // Reflect.hpp turns on for everything -- a std::vector member is reachable
    // without the type having to say so (53.6).
    if (auto sequence = value.as_sequence_container()) {
        YamlNode out = parent.addSequence(key);
        std::size_t index = 0;
        for (auto element : sequence) {
            // Elements are written through the same path, so a vector of
            // structs nests correctly rather than needing its own case.
            const std::string elementKey = std::to_string(index);
            YamlNode holder = out.appendMap();
            if (!writeReflected(holder, elementKey, element)) {
                return false;
            }
            ++index;
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
            YamlNode holder = node.at(i);
            entt::meta_any elementRef = element.as_ref();
            if (!readInto(holder[std::to_string(i)], elementRef)) {
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
