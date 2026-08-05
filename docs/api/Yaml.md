# `<hp/Yaml.hpp>`

*Generated from `engine/include/hp/Yaml.hpp` — do not edit.*

```cpp
#include <hp/Yaml.hpp>
```

47 public declaration(s), 47 documented.

## `YamlNode`

```cpp
class YamlNode
```

 A node in a YAML document: a map, a sequence, or a scalar.

 **A view, not an owner.** It is valid only while its `YamlDocument` lives,
 and it is cheap to copy — pass it by value. An invalid node (a missing key,
 an out-of-range index) is not an error: it reports `valid() == false` and
 every read from it returns the supplied default, so a chain of lookups can be
 written without a check at each step and still not read garbage.

## `YamlNode::YamlNode`

```cpp
YamlNode()
```

 Constructs an invalid node, which reads as absent rather than crashing.

## `YamlNode::valid`

```cpp
bool valid() const
```

 @returns whether this node refers to something in the document.

## `YamlNode::isMap`

```cpp
bool isMap() const
```

 @returns whether this node is a mapping (key/value).

## `YamlNode::isSequence`

```cpp
bool isSequence() const
```

 @returns whether this node is a sequence.

## `YamlNode::isScalar`

```cpp
bool isScalar() const
```

 @returns whether this node holds a scalar value.

## `YamlNode::size`

```cpp
std::size_t size() const
```

 @returns the number of children, or 0 for a scalar or invalid node.

## `YamlNode::has`

```cpp
bool has(std::string_view key) const
```

 @param key the mapping key to look for.
 @returns whether this map has that key.

## `YamlNode::operator[]`

```cpp
YamlNode operator[](std::string_view key) const
```

 Looks a key up in a mapping.
 @param key the key.
 @returns the value node, or an invalid node when absent. **Never
          throws** — an absent key is an ordinary outcome when reading a
          file written by an older version of the engine.

## `YamlNode::at`

```cpp
YamlNode at(std::size_t index) const
```

 Indexes a sequence.
 @param index zero-based position.
 @returns the element, or an invalid node when out of range.

## `YamlNode::keyAt`

```cpp
std::string_view keyAt(std::size_t index) const
```

 @param index zero-based child position.
 @returns the child's key, or empty for a sequence element.

## `YamlNode::scalar`

```cpp
std::string_view scalar() const
```

 @returns the scalar text, or empty when this is not a scalar. The view
          points into the document's buffer and lives as long as it does.

## `YamlNode::read`

```cpp
bool read(bool fallback) const
```

 Reads a boolean scalar.
 @param fallback returned when absent or not a boolean.
 @returns the value, or `fallback`.

## `YamlNode::read`

```cpp
std::int64_t read(std::int64_t fallback) const
```

 Reads a signed integer scalar.
 @param fallback returned when absent or not an integer.
 @returns the value, or `fallback`.

## `YamlNode::read`

```cpp
std::uint64_t read(std::uint64_t fallback) const
```

 Reads an unsigned integer scalar. A negative number does not parse.
 @param fallback returned when absent or not an unsigned integer.
 @returns the value, or `fallback`.

## `YamlNode::read`

```cpp
double read(double fallback) const
```

 Reads a floating-point scalar.
 @param fallback returned when absent or not a number.
 @returns the value, or `fallback`.

## `YamlNode::read`

```cpp
std::string read(const std::string & fallback) const
```

 Reads a string scalar.
 @param fallback returned when absent or not a scalar.
 @returns a copy of the text, or `fallback`.

## `YamlNode::tryRead`

```cpp
bool tryRead(bool & out) const
```

 Reads a boolean scalar, reporting whether it was there and well-formed.
 @param out receives the value, untouched on failure.
 @returns whether the read succeeded.

## `YamlNode::tryRead`

```cpp
bool tryRead(std::int64_t & out) const
```

 Reads a signed integer scalar, reporting whether it was there and well-formed.
 @param out receives the value, untouched on failure.
 @returns whether the read succeeded.

## `YamlNode::tryRead`

```cpp
bool tryRead(std::uint64_t & out) const
```

 Reads a unsigned integer scalar, reporting whether it was there and well-formed.
 @param out receives the value, untouched on failure.
 @returns whether the read succeeded.

## `YamlNode::tryRead`

```cpp
bool tryRead(double & out) const
```

 Reads a floating-point scalar, reporting whether it was there and well-formed.
 @param out receives the value, untouched on failure.
 @returns whether the read succeeded.

## `YamlNode::tryRead`

```cpp
bool tryRead(std::string & out) const
```

 Reads a string scalar, reporting whether it was there and well-formed.
 @param out receives the value, untouched on failure.
 @returns whether the read succeeded.

## `YamlNode::emitSubtree`

```cpp
std::string emitSubtree() const
```

 Emits this node as YAML text, **including its key** when it has one.

 The key is included so the result grafts back where it came from without
 the caller having to reattach it — which is also why a fragment is
 self-describing enough to be stored on its own.
 @returns the text, or empty for an invalid node.

## `YamlNode::graft`

```cpp
bool graft(std::string_view yaml)
```

 Parses @p yaml and appends its top-level children to this node.

 **The document takes ownership of the fragment's buffer.** rapidyaml
 copies spans rather than text when it duplicates nodes, so the grafted
 nodes point into the text they were parsed from — the same invariant this
 header already keeps for the parsed document, extended to text that
 arrived later.
 @param yaml a fragment: a mapping's entries, or a sequence's.
 @returns false when the fragment will not parse, carries nothing, or is a
          sequence being grafted into a mapping (or the reverse) — which
          would produce a node rapidyaml cannot emit.

## `YamlNode::addMap`

```cpp
YamlNode addMap(std::string_view key)
```

 Makes this node a mapping and adds a child map under `key`.
 @param key the key to add.
 @returns the new child node.

## `YamlNode::addSequence`

```cpp
YamlNode addSequence(std::string_view key)
```

 Makes this node a mapping and adds a child sequence under `key`.
 @param key the key to add.
 @returns the new child node.

## `YamlNode::appendMap`

```cpp
YamlNode appendMap()
```

 Appends a map to this sequence.
 @returns the new element.

## `YamlNode::appendSequence`

```cpp
YamlNode appendSequence()
```

 Appends a sequence to this sequence.
 @returns the new element.

## `YamlNode::set`

```cpp
void set(std::string_view key, bool value)
```

 Sets `key` to a boolean value, replacing any existing one.
 @param key the key.
 @param value the value.
 @returns nothing.

## `YamlNode::set`

```cpp
void set(std::string_view key, std::int64_t value)
```

 Sets `key` to a signed integer value, replacing any existing one.
 @param key the key.
 @param value the value.
 @returns nothing.

## `YamlNode::set`

```cpp
void set(std::string_view key, std::uint64_t value)
```

 Sets `key` to a unsigned integer value, replacing any existing one.
 @param key the key.
 @param value the value.
 @returns nothing.

## `YamlNode::set`

```cpp
void set(std::string_view key, double value)
```

 Sets `key` to a floating-point value, replacing any existing one.
 @param key the key.
 @param value the value.
 @returns nothing.

## `YamlNode::set`

```cpp
void set(std::string_view key, std::string_view value)
```

 Sets `key` to a string value, replacing any existing one.
 @param key the key.
 @param value the value.
 @returns nothing.

## `YamlNode::append`

```cpp
void append(bool value)
```

 Appends a boolean value to this sequence.
 @param value the value to append.
 @returns nothing.

## `YamlNode::append`

```cpp
void append(std::int64_t value)
```

 Appends a signed integer value to this sequence.
 @param value the value to append.
 @returns nothing.

## `YamlNode::append`

```cpp
void append(std::uint64_t value)
```

 Appends a unsigned integer value to this sequence.
 @param value the value to append.
 @returns nothing.

## `YamlNode::append`

```cpp
void append(double value)
```

 Appends a floating-point value to this sequence.
 @param value the value to append.
 @returns nothing.

## `YamlNode::append`

```cpp
void append(std::string_view value)
```

 Appends a string value to this sequence.
 @param value the value to append.
 @returns nothing.

## `YamlDocument`

```cpp
class YamlDocument
```

 A parsed or constructed YAML document.

 Owns the text its nodes point into, so a node can never outlive its buffer.

## `YamlDocument::YamlDocument`

```cpp
YamlDocument()
```

 Constructs an empty document whose root is an empty map.

## `YamlDocument::YamlDocument`

```cpp
YamlDocument(const YamlDocument &)
```

 Not copyable: nodes hold a pointer to their document, and copying would
 silently give some of them the wrong one.

## `YamlDocument::operator=`

```cpp
YamlDocument & operator=(const YamlDocument &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `YamlDocument::YamlDocument`

```cpp
YamlDocument(YamlDocument && other)
```

 Moves the document.
 @param other the document to move from.

## `YamlDocument::operator=`

```cpp
YamlDocument & operator=(YamlDocument && other)
```

 Moves the document.
 @param other the document to move from.
 @returns this document.

## `YamlDocument::parse`

```cpp
static std::optional<YamlDocument> parse(std::string_view text, std::string_view name)
```

 Parses YAML text.

 @param text the document source. Copied, so the caller's buffer need not
        outlive the result.
 @param name a name used in error messages — usually the virtual path it
        came from. Nothing else uses it.
 @returns the document, or nothing when the text is not valid YAML. The
          reason is logged under the `yaml` category.

## `YamlDocument::root`

```cpp
YamlNode root()
```

 @returns the root node, which is a map for a default-constructed
          document.

## `YamlDocument::emit`

```cpp
std::string emit() const
```

 Serializes the document back to text.
 @returns the YAML text, ending in a newline.
