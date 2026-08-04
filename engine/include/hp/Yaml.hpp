// YAML reading and writing (T0020.2).
//
// **YAML is the source of truth; the binary form is a derived cache.** That is
// T0020's model and it is what this header serves: YAML is what gets written on
// save, what diffs in git and what a person can hand-edit, and `hp/Cook.hpp`
// turns it into something fast to load. Only the binary ships.
//
// **No `ryml::` or `c4::` type appears here**, which is the whole point of 20.2.
// rapidyaml was chosen over yaml-cpp for speed and maintenance, and this header
// is what makes that choice reversible. It also keeps rapidyaml's tree-based,
// span-oriented API — which is fast precisely because it does not own its
// buffer — from leaking lifetime rules into every caller.
//
// **The document owns its text.** rapidyaml parses in place and its nodes point
// into the source buffer, which is a real performance decision and a real
// footgun: a node outliving the string it was parsed from reads freed memory.
// `YamlDocument` keeps the buffer alive for exactly as long as any node can
// reference it, so callers cannot get that wrong.
#pragma once

#include <hp/Api.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hp {

class YamlDocument;

/// A node in a YAML document: a map, a sequence, or a scalar.
///
/// **A view, not an owner.** It is valid only while its `YamlDocument` lives,
/// and it is cheap to copy — pass it by value. An invalid node (a missing key,
/// an out-of-range index) is not an error: it reports `valid() == false` and
/// every read from it returns the supplied default, so a chain of lookups can be
/// written without a check at each step and still not read garbage.
class HP_API YamlNode {
public:
    /// Constructs an invalid node, which reads as absent rather than crashing.
    YamlNode() = default;

    /// @returns whether this node refers to something in the document.
    [[nodiscard]] bool valid() const;

    /// @returns whether this node is a mapping (key/value).
    [[nodiscard]] bool isMap() const;

    /// @returns whether this node is a sequence.
    [[nodiscard]] bool isSequence() const;

    /// @returns whether this node holds a scalar value.
    [[nodiscard]] bool isScalar() const;

    /// @returns the number of children, or 0 for a scalar or invalid node.
    [[nodiscard]] std::size_t size() const;

    /// @param key the mapping key to look for.
    /// @returns whether this map has that key.
    [[nodiscard]] bool has(std::string_view key) const;

    /// Looks a key up in a mapping.
    /// @param key the key.
    /// @returns the value node, or an invalid node when absent. **Never
    ///          throws** — an absent key is an ordinary outcome when reading a
    ///          file written by an older version of the engine.
    [[nodiscard]] YamlNode operator[](std::string_view key) const;

    /// Indexes a sequence.
    /// @param index zero-based position.
    /// @returns the element, or an invalid node when out of range.
    [[nodiscard]] YamlNode at(std::size_t index) const;

    /// @param index zero-based child position.
    /// @returns the child's key, or empty for a sequence element.
    [[nodiscard]] std::string_view keyAt(std::size_t index) const;

    /// @returns the scalar text, or empty when this is not a scalar. The view
    ///          points into the document's buffer and lives as long as it does.
    [[nodiscard]] std::string_view scalar() const;

    // Reading a scalar is **silent** by design across all of these: a serialized
    // document is data, and a malformed field is a data problem the caller
    // decides how loud to be about. Use `tryRead` when the difference matters.

    /// Reads a boolean scalar.
    /// @param fallback returned when absent or not a boolean.
    /// @returns the value, or `fallback`.
    [[nodiscard]] bool read(bool fallback) const;

    /// Reads a signed integer scalar.
    /// @param fallback returned when absent or not an integer.
    /// @returns the value, or `fallback`.
    [[nodiscard]] std::int64_t read(std::int64_t fallback) const;

    /// Reads an unsigned integer scalar. A negative number does not parse.
    /// @param fallback returned when absent or not an unsigned integer.
    /// @returns the value, or `fallback`.
    [[nodiscard]] std::uint64_t read(std::uint64_t fallback) const;

    /// Reads a floating-point scalar.
    /// @param fallback returned when absent or not a number.
    /// @returns the value, or `fallback`.
    [[nodiscard]] double read(double fallback) const;

    /// Reads a string scalar.
    /// @param fallback returned when absent or not a scalar.
    /// @returns a copy of the text, or `fallback`.
    [[nodiscard]] std::string read(const std::string& fallback) const;

    /// Reads a boolean scalar, reporting whether it was there and well-formed.
    /// @param out receives the value, untouched on failure.
    /// @returns whether the read succeeded.
    [[nodiscard]] bool tryRead(bool& out) const;

    /// Reads a signed integer scalar, reporting whether it was there and well-formed.
    /// @param out receives the value, untouched on failure.
    /// @returns whether the read succeeded.
    [[nodiscard]] bool tryRead(std::int64_t& out) const;

    /// Reads a unsigned integer scalar, reporting whether it was there and well-formed.
    /// @param out receives the value, untouched on failure.
    /// @returns whether the read succeeded.
    [[nodiscard]] bool tryRead(std::uint64_t& out) const;

    /// Reads a floating-point scalar, reporting whether it was there and well-formed.
    /// @param out receives the value, untouched on failure.
    /// @returns whether the read succeeded.
    [[nodiscard]] bool tryRead(double& out) const;

    /// Reads a string scalar, reporting whether it was there and well-formed.
    /// @param out receives the value, untouched on failure.
    /// @returns whether the read succeeded.
    [[nodiscard]] bool tryRead(std::string& out) const;


    /// Makes this node a mapping and adds a child map under `key`.
    /// @param key the key to add.
    /// @returns the new child node.
    YamlNode addMap(std::string_view key);

    /// Makes this node a mapping and adds a child sequence under `key`.
    /// @param key the key to add.
    /// @returns the new child node.
    YamlNode addSequence(std::string_view key);

    /// Appends a map to this sequence.
    /// @returns the new element.
    YamlNode appendMap();

    /// Appends a sequence to this sequence.
    /// @returns the new element.
    YamlNode appendSequence();

    // Every `set` copies the key and the text into the document's arena, so a
    // temporary string is safe to pass -- which is **not** true of rapidyaml's
    // own API and is the single easiest way to corrupt a document with it.

    /// Sets `key` to a boolean value, replacing any existing one.
    /// @param key the key.
    /// @param value the value.
    /// @returns nothing.
    void set(std::string_view key, bool value);

    /// Sets `key` to a signed integer value, replacing any existing one.
    /// @param key the key.
    /// @param value the value.
    /// @returns nothing.
    void set(std::string_view key, std::int64_t value);

    /// Sets `key` to a unsigned integer value, replacing any existing one.
    /// @param key the key.
    /// @param value the value.
    /// @returns nothing.
    void set(std::string_view key, std::uint64_t value);

    /// Sets `key` to a floating-point value, replacing any existing one.
    /// @param key the key.
    /// @param value the value.
    /// @returns nothing.
    void set(std::string_view key, double value);

    /// Sets `key` to a string value, replacing any existing one.
    /// @param key the key.
    /// @param value the value.
    /// @returns nothing.
    void set(std::string_view key, std::string_view value);


    /// Appends a boolean value to this sequence.
    /// @param value the value to append.
    /// @returns nothing.
    void append(bool value);

    /// Appends a signed integer value to this sequence.
    /// @param value the value to append.
    /// @returns nothing.
    void append(std::int64_t value);

    /// Appends a unsigned integer value to this sequence.
    /// @param value the value to append.
    /// @returns nothing.
    void append(std::uint64_t value);

    /// Appends a floating-point value to this sequence.
    /// @param value the value to append.
    /// @returns nothing.
    void append(double value);

    /// Appends a string value to this sequence.
    /// @param value the value to append.
    /// @returns nothing.
    void append(std::string_view value);


private:
    friend class YamlDocument;
    YamlNode(YamlDocument* document, std::size_t id) : document_(document), id_(id) {}

    /// The underlying rapidyaml tree, as an opaque pointer.
    ///
    /// Opaque because naming `ryml::Tree` here would defeat the whole purpose of
    /// this header (20.2). The one translation unit that implements this casts
    /// it back; nothing else can, because nothing else knows the type.
    /// @returns a `ryml::Tree*`, or nullptr for an invalid node.
    [[nodiscard]] void* treeHandle() const;

    YamlDocument* document_ = nullptr;
    std::size_t id_ = static_cast<std::size_t>(-1);
};

/// A parsed or constructed YAML document.
///
/// Owns the text its nodes point into, so a node can never outlive its buffer.
class HP_API YamlDocument {
public:
    /// Constructs an empty document whose root is an empty map.
    YamlDocument();

    /// Destroys the document. Every node referring to it becomes invalid.
    ~YamlDocument();

    /// Not copyable: nodes hold a pointer to their document, and copying would
    /// silently give some of them the wrong one.
    YamlDocument(const YamlDocument&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    YamlDocument& operator=(const YamlDocument&) = delete;

    /// Moves the document.
    /// @param other the document to move from.
    YamlDocument(YamlDocument&& other) noexcept;

    /// Moves the document.
    /// @param other the document to move from.
    /// @returns this document.
    YamlDocument& operator=(YamlDocument&& other) noexcept;

    /// Parses YAML text.
    ///
    /// @param text the document source. Copied, so the caller's buffer need not
    ///        outlive the result.
    /// @param name a name used in error messages — usually the virtual path it
    ///        came from. Nothing else uses it.
    /// @returns the document, or nothing when the text is not valid YAML. The
    ///          reason is logged under the `yaml` category.
    [[nodiscard]] static std::optional<YamlDocument> parse(std::string_view text,
                                                           std::string_view name = "<memory>");

    /// @returns the root node, which is a map for a default-constructed
    ///          document.
    [[nodiscard]] YamlNode root();

    /// Serializes the document back to text.
    /// @returns the YAML text, ending in a newline.
    [[nodiscard]] std::string emit() const;

private:
    friend class YamlNode;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hp
