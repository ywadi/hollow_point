#include <hp/Yaml.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <charconv>
#include <cstring>

#include <ryml.hpp>
#include <ryml_std.hpp>

namespace hp {
namespace {

const LogCategory kLog("yaml");

c4::csubstr toRyml(std::string_view text) {
    return {text.data(), text.size()};
}

std::string_view fromRyml(c4::csubstr text) {
    return {text.data(), text.size()};
}

/// Routes rapidyaml's errors through the engine log instead of aborting.
///
/// **Not optional.** rapidyaml's default error callback calls `std::abort`, so a
/// malformed file -- which is *data*, and therefore always possible -- would
/// take the editor down rather than being reported. The longjmp-free way to
/// survive it is to throw and catch, which is what `parse` does.
[[noreturn]] void onRymlError(const char* message, std::size_t length, c4::yml::Location location,
                              void* /*user*/) {
    throw std::runtime_error(std::string(message, length) + " at line "
                             + std::to_string(location.line));
}

/// Installs the error handler once per process.
void ensureCallbacks() {
    static const bool installed = [] {
        c4::yml::Callbacks callbacks = c4::yml::get_callbacks();
        callbacks.m_error = &onRymlError;
        c4::yml::set_callbacks(callbacks);
        return true;
    }();
    (void)installed;
}

} // namespace

struct YamlDocument::Impl {
    /// The source text. rapidyaml parses in place and its nodes point into this,
    /// so it must outlive every node -- which is exactly why the document owns
    /// it rather than the caller.
    std::string text;
    ryml::Tree tree;

    /// Every fragment grafted in after the parse, kept alive for the document's
    /// lifetime.
    ///
    /// **`Tree::duplicate` copies spans, not characters** -- `_copy_props`
    /// assigns `m_key` and `m_val` straight across -- so a grafted node points
    /// into the buffer *and the arena* of the tree it was parsed from. Both are
    /// held here. This is the same rule the class comment already states about
    /// `text`; a graft would break it silently otherwise, and the symptom would
    /// be garbage in an emitted document long after the call that caused it.
    ///
    /// Indirect because the addresses must not move: `std::string` has a small
    /// buffer, so moving one relocates short text, and a `std::vector` grows by
    /// moving its elements.
    std::vector<std::unique_ptr<std::string>> graftedText;
    std::vector<std::unique_ptr<ryml::Tree>> graftedTrees;
};

YamlDocument::YamlDocument() : impl_(std::make_unique<Impl>()) {
    ensureCallbacks();
    impl_->tree.rootref() |= ryml::MAP;
}

YamlDocument::~YamlDocument() = default;

YamlDocument::YamlDocument(YamlDocument&& other) noexcept = default;

YamlDocument& YamlDocument::operator=(YamlDocument&& other) noexcept = default;

std::optional<YamlDocument> YamlDocument::parse(std::string_view text, std::string_view name) {
    HP_PROFILE_ZONE();

    ensureCallbacks();

    YamlDocument document;
    document.impl_->text.assign(text);

    try {
        document.impl_->tree =
            ryml::parse_in_place(toRyml(name), c4::to_substr(document.impl_->text));
    } catch (const std::exception& error) {
        HP_LOG_ERROR(kLog, "could not parse '{}': {}", name, error.what());
        return std::nullopt;
    }

    // An empty document parses to a tree whose root is neither map nor
    // sequence. Callers universally expect a map, and an empty file is a
    // legitimate thing to read, so normalise rather than making every caller
    // handle it.
    if (document.impl_->tree.empty()) {
        document.impl_->tree.rootref() |= ryml::MAP;
    }
    return document;
}

YamlNode YamlDocument::root() {
    return YamlNode{this, impl_->tree.root_id()};
}

std::string YamlDocument::emit() const {
    HP_PROFILE_ZONE();

    if (impl_->tree.empty()) {
        return {};
    }
    return ryml::emitrs_yaml<std::string>(impl_->tree);
}

void* YamlNode::treeHandle() const {
    // YamlNode is a friend of YamlDocument, which is what lets this one
    // function reach the impl. Everything else goes through the void*.
    return document_ != nullptr ? static_cast<void*>(&document_->impl_->tree) : nullptr;
}

namespace {

/// The tree behind a node, or nullptr when it has no document.
ryml::Tree* treeOf(const YamlNode& node, void* handle) {
    (void)node;
    return static_cast<ryml::Tree*>(handle);
}

} // namespace

bool YamlNode::valid() const {
    const ryml::Tree* tree = static_cast<const ryml::Tree*>(treeHandle());
    return tree != nullptr && id_ != static_cast<std::size_t>(-1)
           && id_ < static_cast<std::size_t>(tree->capacity());
}

bool YamlNode::isMap() const {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    return valid() && tree->is_map(id_);
}

bool YamlNode::isSequence() const {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    return valid() && tree->is_seq(id_);
}

bool YamlNode::isScalar() const {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    return valid() && tree->has_val(id_);
}

std::size_t YamlNode::size() const {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid()) {
        return 0;
    }
    return tree->num_children(id_);
}

bool YamlNode::has(std::string_view key) const {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid() || !tree->is_map(id_)) {
        return false;
    }
    return tree->find_child(id_, toRyml(key)) != ryml::NONE;
}

YamlNode YamlNode::operator[](std::string_view key) const {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid() || !tree->is_map(id_)) {
        return {};
    }
    const ryml::id_type child = tree->find_child(id_, toRyml(key));
    if (child == ryml::NONE) {
        return {};
    }
    return YamlNode{document_, child};
}

YamlNode YamlNode::at(std::size_t index) const {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid() || index >= tree->num_children(id_)) {
        return {};
    }
    return YamlNode{document_, tree->child(id_, static_cast<ryml::id_type>(index))};
}

std::string_view YamlNode::keyAt(std::size_t index) const {
    const YamlNode child = at(index);
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!child.valid() || !tree->has_key(child.id_)) {
        return {};
    }
    return fromRyml(tree->key(child.id_));
}

std::string_view YamlNode::scalar() const {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid() || !tree->has_val(id_)) {
        return {};
    }
    return fromRyml(tree->val(id_));
}

bool YamlNode::tryRead(bool& out) const {
    const std::string_view text = scalar();
    if (text == "true" || text == "True" || text == "yes" || text == "on" || text == "1") {
        out = true;
        return true;
    }
    if (text == "false" || text == "False" || text == "no" || text == "off" || text == "0") {
        out = false;
        return true;
    }
    return false;
}

bool YamlNode::tryRead(std::int64_t& out) const {
    const std::string_view text = scalar();
    if (text.empty()) {
        return false;
    }
    std::int64_t value = 0;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;
    }
    out = value;
    return true;
}

bool YamlNode::tryRead(std::uint64_t& out) const {
    const std::string_view text = scalar();
    if (text.empty() || text.front() == '-') {
        return false;
    }
    std::uint64_t value = 0;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;
    }
    out = value;
    return true;
}

bool YamlNode::tryRead(double& out) const {
    const std::string_view text = scalar();
    if (text.empty()) {
        return false;
    }
    // `std::from_chars` for floating point is the one piece of <charconv> that
    // libc++ was late to implement; rapidyaml's own conversion is used instead
    // so this does not depend on which standard library the target has.
    double value = 0.0;
    if (!c4::atod(toRyml(text), &value)) {
        return false;
    }
    out = value;
    return true;
}

bool YamlNode::tryRead(std::string& out) const {
    if (!isScalar()) {
        return false;
    }
    out.assign(scalar());
    return true;
}

bool YamlNode::read(bool fallback) const {
    bool value = fallback;
    return tryRead(value) ? value : fallback;
}

std::int64_t YamlNode::read(std::int64_t fallback) const {
    std::int64_t value = fallback;
    return tryRead(value) ? value : fallback;
}

std::uint64_t YamlNode::read(std::uint64_t fallback) const {
    std::uint64_t value = fallback;
    return tryRead(value) ? value : fallback;
}

double YamlNode::read(double fallback) const {
    double value = fallback;
    return tryRead(value) ? value : fallback;
}

std::string YamlNode::read(const std::string& fallback) const {
    std::string value;
    return tryRead(value) ? value : fallback;
}

std::string YamlNode::emitSubtree() const {
    HP_PROFILE_ZONE();

    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid() || tree->empty()) {
        return {};
    }
    try {
        return ryml::emitrs_yaml<std::string>(*tree, static_cast<ryml::id_type>(id_));
    } catch (const std::exception& error) {
        // Reachable: the emitter reports through the same callback the parser
        // does, and this header's contract is that a data problem is reported
        // rather than fatal.
        HP_LOG_ERROR(kLog, "could not emit a subtree: {}", error.what());
        return {};
    }
}

bool YamlNode::graft(std::string_view yaml) {
    HP_PROFILE_ZONE();

    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid() || yaml.empty()) {
        return false;
    }
    ensureCallbacks();

    auto buffer = std::make_unique<std::string>(yaml);
    auto fragment = std::make_unique<ryml::Tree>();
    try {
        *fragment = ryml::parse_in_place(toRyml("<fragment>"), c4::to_substr(*buffer));
    } catch (const std::exception& error) {
        HP_LOG_ERROR(kLog, "could not parse a fragment to graft: {}", error.what());
        return false;
    }
    if (fragment->empty()) {
        return false;
    }

    const auto destination = static_cast<ryml::id_type>(id_);
    const ryml::id_type source = fragment->root_id();
    const bool fragmentIsMap = fragment->is_map(source);
    const bool fragmentIsSeq = fragment->is_seq(source);
    if (!fragmentIsMap && !fragmentIsSeq) {
        // A bare scalar has no children, so there is nothing to append and no
        // key to append it under. Refused rather than silently doing nothing.
        HP_LOG_ERROR(kLog, "a fragment to graft must be a mapping or a sequence");
        return false;
    }
    if ((fragmentIsSeq && tree->is_map(destination))
        || (fragmentIsMap && tree->is_seq(destination))) {
        // Mixing the two sets both bits on the destination, which produces a
        // node the emitter cannot write -- and it would fail at emit time, far
        // from the call that caused it.
        HP_LOG_ERROR(kLog, "cannot graft a {} into a {}", fragmentIsSeq ? "sequence" : "mapping",
                     tree->is_map(destination) ? "mapping" : "sequence");
        return false;
    }
    if (fragment->num_children(source) == 0) {
        return false;
    }

    tree->_p(destination)->m_type |= fragmentIsMap ? ryml::MAP : ryml::SEQ;
    tree->duplicate_children(fragment.get(), source, destination, tree->last_child(destination));

    document_->impl_->graftedText.push_back(std::move(buffer));
    document_->impl_->graftedTrees.push_back(std::move(fragment));
    return true;
}

namespace {

/// Copies text into the tree's arena so the document owns it.
///
/// rapidyaml stores spans, not strings: passing a temporary directly leaves a
/// dangling span that emits garbage much later. Everything written through this
/// header is arena-copied for that reason.
c4::csubstr own(ryml::Tree* tree, std::string_view text) {
    return tree->to_arena(toRyml(text));
}

} // namespace

YamlNode YamlNode::addMap(std::string_view key) {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid()) {
        return {};
    }
    tree->_p(id_)->m_type |= ryml::MAP;
    const ryml::id_type child = tree->append_child(id_);
    tree->to_map(child, own(tree, key));
    return YamlNode{document_, child};
}

YamlNode YamlNode::addSequence(std::string_view key) {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid()) {
        return {};
    }
    tree->_p(id_)->m_type |= ryml::MAP;
    const ryml::id_type child = tree->append_child(id_);
    tree->to_seq(child, own(tree, key));
    return YamlNode{document_, child};
}

YamlNode YamlNode::appendMap() {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid()) {
        return {};
    }
    tree->_p(id_)->m_type |= ryml::SEQ;
    const ryml::id_type child = tree->append_child(id_);
    tree->to_map(child);
    return YamlNode{document_, child};
}

YamlNode YamlNode::appendSequence() {
    auto* tree = static_cast<ryml::Tree*>(treeHandle());
    if (!valid()) {
        return {};
    }
    tree->_p(id_)->m_type |= ryml::SEQ;
    const ryml::id_type child = tree->append_child(id_);
    tree->to_seq(child);
    return YamlNode{document_, child};
}

namespace {

/// Writes or replaces `key: value` on a map node.
void setScalar(ryml::Tree* tree, std::size_t id, std::string_view key, std::string_view value) {
    tree->_p(id)->m_type |= ryml::MAP;
    ryml::id_type child = tree->find_child(id, toRyml(key));
    if (child == ryml::NONE) {
        child = tree->append_child(id);
    }

    // **Reserve before copying, and copy into locals.** This line used to read
    //
    //     tree->to_keyval(child, own(tree, key), own(tree, value));
    //
    // which is a dangling span waiting to happen: `to_arena` may reallocate the
    // arena, so whichever of the two calls runs first can have its `csubstr`
    // invalidated by the second -- and C++ does not specify which runs first.
    //
    // **This is a latent hazard fixed on its own merits, not a confirmed cause.**
    // While working on T0023 an intermittent Windows-only failure was captured
    // -- roughly one run in four, a `key: value` pair missing from an emitted
    // document, so a field saved absent and loaded at its default. Values were
    // recorded: the binary path round-tripped 0xABCD1234 correctly while the
    // YAML path returned the field's default, and the read never reported a
    // failure, which means the key was simply not in the document.
    //
    // That points here. It was **not** proven: after this change the failure
    // stopped reproducing, and so did it with the change reverted -- 0 in 40
    // either way, having been 7 in 25 and 8 in 40 before. Something outside the
    // source moved. So the honest claim is that the double-`to_arena` was a real
    // defect and is gone; whether it was *the* defect is unresolved, and if a
    // key ever goes missing from an emitted document again, start here and read
    // T0020's ticket note.
    tree->reserve_arena(tree->arena_size() + key.size() + value.size() + 2);
    const c4::csubstr ownedKey = own(tree, key);
    const c4::csubstr ownedValue = own(tree, value);
    tree->to_keyval(child, ownedKey, ownedValue);
}

std::string toText(bool value) {
    return value ? "true" : "false";
}

std::string toText(std::int64_t value) {
    return std::to_string(value);
}

std::string toText(std::uint64_t value) {
    return std::to_string(value);
}

/// Formats a double so it round-trips exactly.
///
/// `std::to_string` gives six decimal places and silently loses precision, which
/// for a float would be invisible in a diff and wrong in the world. 17
/// significant digits is what guarantees a double survives text.
std::string toText(double value) {
    char buffer[40];
    const int written = std::snprintf(buffer, sizeof buffer, "%.17g", value);
    return written > 0 ? std::string(buffer, static_cast<std::size_t>(written)) : std::string("0");
}

} // namespace

void YamlNode::set(std::string_view key, bool value) {
    if (valid()) {
        setScalar(static_cast<ryml::Tree*>(treeHandle()), id_, key, toText(value));
    }
}

void YamlNode::set(std::string_view key, std::int64_t value) {
    if (valid()) {
        setScalar(static_cast<ryml::Tree*>(treeHandle()), id_, key, toText(value));
    }
}

void YamlNode::set(std::string_view key, std::uint64_t value) {
    if (valid()) {
        setScalar(static_cast<ryml::Tree*>(treeHandle()), id_, key, toText(value));
    }
}

void YamlNode::set(std::string_view key, double value) {
    if (valid()) {
        setScalar(static_cast<ryml::Tree*>(treeHandle()), id_, key, toText(value));
    }
}

void YamlNode::set(std::string_view key, std::string_view value) {
    if (valid()) {
        setScalar(static_cast<ryml::Tree*>(treeHandle()), id_, key, value);
    }
}

namespace {

void appendScalar(ryml::Tree* tree, std::size_t id, std::string_view value) {
    tree->_p(id)->m_type |= ryml::SEQ;
    const ryml::id_type child = tree->append_child(id);
    tree->to_val(child, own(tree, value));
}

} // namespace

void YamlNode::append(bool value) {
    if (valid()) {
        appendScalar(static_cast<ryml::Tree*>(treeHandle()), id_, toText(value));
    }
}

void YamlNode::append(std::int64_t value) {
    if (valid()) {
        appendScalar(static_cast<ryml::Tree*>(treeHandle()), id_, toText(value));
    }
}

void YamlNode::append(std::uint64_t value) {
    if (valid()) {
        appendScalar(static_cast<ryml::Tree*>(treeHandle()), id_, toText(value));
    }
}

void YamlNode::append(double value) {
    if (valid()) {
        appendScalar(static_cast<ryml::Tree*>(treeHandle()), id_, toText(value));
    }
}

void YamlNode::append(std::string_view value) {
    if (valid()) {
        appendScalar(static_cast<ryml::Tree*>(treeHandle()), id_, value);
    }
}

} // namespace hp
