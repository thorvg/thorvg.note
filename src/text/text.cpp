#include <text/text.h>
#include <cctype>
#include <list>

namespace tn
{
struct Document::Impl
{
    /**
     * @brief Records one applied replacement: remove @p removed bytes at @p start, then insert @p inserted bytes at the same position.
     *
     * @note The offset refers to the document immediately before this replacement.
     *
     * @see changesSince(), Document::changedSince(), Document::revision(), EditSession
     * @see undo(), redo()
     */
    struct Change
    {
        /**
         * @brief Document revision produced by the edit containing this replacement.
         */
        uint64_t revision;

        /**
         * @brief Replacement offset, removed byte count, and inserted byte count.
         */
        size_t start, removed, inserted;
    };

    /**
     * @brief Retrieves replacements applied after a document revision.
     *
     * @param[in] revision Previously observed document revision.
     * @param[out] list Replacements in application order.
     * @param[out] count Number of replacements.
     *
     * @retval false If the revision is no longer tracked.
     * @retval true If the revision is tracked.
     *
     * @note The returned list remains valid until the next document mutation.
     *
     * @par Usage
     * Map cached positions through each replacement in order. Consume the list before editing the document again.
     *
     * @see Change, Document::changedSince(), Document::revision()
     */
    bool changesSince(uint64_t revision, const Change*& list, size_t& count) const;

    /**
     * @brief Replaces ordered selections with the same text as one edit.
     *
     * @param[in] document Owner used to clamp byte offsets.
     * @param[in] ranges Array of non-overlapping selections with distinct start offsets, in document order.
     * @param[in] count Number of selections; @p ranges must be valid when nonzero.
     * @param[in] text UTF-8 replacement without NUL bytes; line endings are normalized to LF.
     * @param[in] group Latest undo group to join, or zero to create a new group.
     *
     * @return EditError::None if accepted; otherwise InvalidText or InvalidRange.
     *
     * @note Range endpoints are clamped to valid UTF-8 boundaries. No-op edits keep the revision unchanged.
     *
     * @note Called by EditSession, which updates selections and invokes callbacks.
     */
    EditError replace(const Document& document, const Selection* ranges, size_t count, std::string_view text, uint64_t group = 0);

    /**
     * @brief Applies a batch atomically against its recorded revision.
     *
     * @param[in] document Owner used to validate revision and byte offsets.
     * @param[in] batch Edits and optional resulting selection to validate.
     * @param[in] group Latest undo group to join, or zero to create a new group.
     *
     * @return EditError::None if accepted; otherwise a validation error. Rejected batches leave text and history unchanged.
     *
     * @note Document validates the optional selection but does not store it.
     */
    EditError apply(const Document& document, const EditBatch&, uint64_t group = 0);

    /**
     * @brief Returns the latest applied undo group ID, or zero when there is none.
     */
    uint64_t group() const;

    /**
     * @brief Undoes one group and returns its ID, or zero when no undo is available.
     *
     * @see redo(), Change, changesSince(), Document::changedSince()
     */
    uint64_t undo();

    /**
     * @brief Redoes one group and returns its ID, or zero when no redo is available.
     *
     * @see undo(), Change, changesSince(), Document::changedSince()
     */
    uint64_t redo();

    static Impl& get(Document& document) { return *document.impl; }
    static const Impl& get(const Document& document) { return *document.impl; }

    struct Edit
    {
        size_t offset;
        std::string removed, inserted;
    };
    struct EditGroup
    {
        uint64_t id;  // revision that created the group
        std::vector<Edit> edits;
    };
    struct Subscriber
    {
        uint64_t id;
        std::function<void()> callback;
        bool active = true;
    };
    void notify();
    void commit(std::vector<Edit>, uint64_t group);
    void splice(size_t offset, size_t removed, std::string_view inserted);
    std::string value;
    std::vector<size_t> starts{0};
    std::vector<EditGroup> history;
    std::vector<Change> changes;
    size_t historyPosition = 0;
    uint64_t version = 0, tracked = 0;
    std::list<Subscriber> subscribers;
    uint64_t nextSubscription = 1;
    bool notifying = false;

    Impl() = default;
    Impl(const Impl& other) :
        value(other.value), starts(other.starts), history(other.history), changes(other.changes),
        historyPosition(other.historyPosition), version(other.version), tracked(other.tracked) {}
    Impl& operator=(const Impl& other)
    {
        value = other.value;
        starts = other.starts;
        history = other.history;
        changes = other.changes;
        historyPosition = other.historyPosition;
        version = other.version;
        tracked = other.tracked;
        return *this;
    }
};

namespace
{
bool _continuation(unsigned char ch)
{
    return (ch & 0xc0) == 0x80;
}

bool _normalize(std::string_view source, std::string& output)
{
    for (size_t i = 0; i < source.size();) {
        auto ch = static_cast<unsigned char>(source[i]);
        if (ch == '\r') {
            output += '\n';
            i += i + 1 < source.size() && source[i + 1] == '\n' ? 2 : 1;
            continue;
        }
        // NUL marks binary data; the loader and the encoder reject it as well.
        size_t length = !ch ? 0 : ch < 0x80 ? 1
            : ch >= 0xc2 && ch <= 0xdf      ? 2
            : ch >= 0xe0 && ch <= 0xef      ? 3
            : ch >= 0xf0 && ch <= 0xf4      ? 4
                                            : 0;
        if (!length || i + length > source.size()) return false;
        for (size_t j = 1; j < length; ++j)
            if (!_continuation(static_cast<unsigned char>(source[i + j]))) return false;
        if (length >= 3) {
            auto second = static_cast<unsigned char>(source[i + 1]);
            if ((ch == 0xe0 && second < 0xa0) || (ch == 0xed && second >= 0xa0) || (ch == 0xf0 && second < 0x90) || (ch == 0xf4 && second >= 0x90)) return false;
        }
        output.append(source.substr(i, length));
        i += length;
    }
    return true;
}

int _category(unsigned char ch)
{
    return std::isspace(ch) ? 0 : ch >= 0x80 || std::isalnum(ch) || ch == '_' ? 1
                                                                              : 2;
}
}  // namespace

Document::Document() :
    impl(new Impl) {}
Document::~Document() = default;
Document::Document(const Document& other) :
    impl(new Impl(*other.impl)) {}
Document& Document::operator=(const Document& other)
{
    if (this == &other) return *this;
    auto revision = std::max(impl->version, other.impl->version) + 1;
    *impl = *other.impl;
    impl->tracked = impl->version = revision;
    impl->changes.clear();
    impl->notify();
    return *this;
}
const std::string& Document::text() const
{
    return impl->value;
}
uint64_t Document::revision() const
{
    return impl->version;
}
uint64_t Document::subscribe(std::function<void()> callback)
{
    if (!callback) return 0;
    auto id = impl->nextSubscription++;
    impl->subscribers.push_back({id, std::move(callback)});
    return id;
}

void Document::unsubscribe(uint64_t subscription)
{
    for (auto& subscriber : impl->subscribers)
        if (subscriber.id == subscription) subscriber.active = false;
    // Keep callback storage alive while a callback may be executing.
    if (!impl->notifying) impl->subscribers.remove_if([](const auto& subscriber) { return !subscriber.active; });
}

void Document::Impl::notify()
{
    auto limit = nextSubscription;
    notifying = true;
    for (auto& subscriber : subscribers) {
        if (subscriber.id >= limit) break;
        if (subscriber.active) subscriber.callback();
    }
    notifying = false;
    subscribers.remove_if([](const auto& subscriber) { return !subscriber.active; });
}

size_t Document::lineCount() const
{
    return impl->starts.size();
}
uint64_t Document::Impl::group() const
{
    return historyPosition ? history[historyPosition - 1].id : 0;
}
bool Document::canUndo() const
{
    return impl->historyPosition > 0;
}
bool Document::canRedo() const
{
    return impl->historyPosition < impl->history.size();
}

void Document::Impl::splice(size_t offset, size_t removed, std::string_view inserted)
{
    value.replace(offset, removed, inserted);
    // Line starts follow each newline: drop the removed ones, shift later ones and add inserted ones.
    auto first = std::upper_bound(starts.begin(), starts.end(), offset);
    auto last = std::upper_bound(first, starts.end(), offset + removed);
    for (auto it = last; it != starts.end(); ++it)
        *it = *it + inserted.size() - removed;
    std::vector<size_t> added;
    for (size_t i = 0; i < inserted.size(); ++i)
        if (inserted[i] == '\n') added.push_back(offset + i + 1);
    starts.insert(starts.erase(first, last), added.begin(), added.end());
    // Kept until the text is replaced, so a session can follow it from whichever revision it saw last.
    changes.push_back({version, offset, removed, inserted.size()});
}

ChangeStatus Document::changedSince(uint64_t revision, size_t& start, size_t& end) const
{
    start = end = 0;
    const Impl::Change* list = nullptr;
    size_t count = 0;
    if (!impl->changesSince(revision, list, count)) return ChangeStatus::Untracked;
    if (!count) return ChangeStatus::Unchanged;
    for (size_t i = 0; i < count; ++i) {
        const auto& change = list[i];
        auto map = [&](size_t offset) {
            if (offset <= change.start) return offset;
            return offset >= change.start + change.removed ? offset - change.removed + change.inserted : change.start + change.inserted;
        };
        start = i ? std::min(map(start), change.start) : change.start;
        end = i ? std::max(map(end), change.start + change.inserted) : change.start + change.inserted;
    }
    return ChangeStatus::Changed;
}

bool Document::Impl::changesSince(uint64_t revision, const Change*& list, size_t& count) const
{
    list = nullptr;
    count = 0;
    if (revision < tracked || revision > version) return false;
    auto first = std::upper_bound(changes.begin(), changes.end(), revision, [](uint64_t seen, const Change& change) { return seen < change.revision; });
    list = changes.data() + (first - changes.begin());
    count = size_t(changes.end() - first);
    return true;
}

bool Document::text(std::string_view source)
{
    std::string next;
    if (!_normalize(source, next)) return false;
    impl->value = std::move(next);
    impl->history.clear();
    impl->historyPosition = 0;
    impl->tracked = ++impl->version;
    impl->changes.clear();
    impl->starts = {0};
    for (size_t i = 0; i < impl->value.size(); ++i)
        if (impl->value[i] == '\n') impl->starts.push_back(i + 1);
    impl->notify();
    return true;
}

size_t Document::lineStart(size_t line) const
{
    return impl->starts[std::min(line, impl->starts.size() - 1)];
}
size_t Document::lineEnd(size_t line) const
{
    return line < impl->starts.size() - 1 ? impl->starts[line + 1] - 1 : impl->value.size();
}
size_t Document::lineAt(size_t offset) const
{
    return size_t(std::upper_bound(impl->starts.begin(), impl->starts.end(), std::min(offset, impl->value.size())) - impl->starts.begin() - 1);
}

size_t Document::clamp(size_t offset) const
{
    offset = std::min(offset, impl->value.size());
    while (offset > 0 && offset < impl->value.size() && _continuation(static_cast<unsigned char>(impl->value[offset])))
        --offset;
    return offset;
}

size_t Document::previous(size_t offset) const
{
    offset = clamp(offset);
    return offset ? clamp(offset - 1) : 0;
}

size_t Document::next(size_t offset) const
{
    offset = clamp(offset);
    if (offset == impl->value.size()) return offset;
    ++offset;
    while (offset < impl->value.size() && _continuation(static_cast<unsigned char>(impl->value[offset])))
        ++offset;
    return offset;
}

size_t Document::wordPrevious(size_t offset) const
{
    offset = previous(offset);
    while (offset && _category(static_cast<unsigned char>(impl->value[offset])) == 0)
        offset = previous(offset);
    if (offset == impl->value.size()) return offset;
    auto group = _category(static_cast<unsigned char>(impl->value[offset]));
    while (offset && _category(static_cast<unsigned char>(impl->value[previous(offset)])) == group)
        offset = previous(offset);
    return offset;
}

size_t Document::wordNext(size_t offset) const
{
    offset = clamp(offset);
    if (offset == impl->value.size()) return offset;
    auto group = _category(static_cast<unsigned char>(impl->value[offset]));
    while (offset < impl->value.size() && _category(static_cast<unsigned char>(impl->value[offset])) == group)
        offset = next(offset);
    while (offset < impl->value.size() && _category(static_cast<unsigned char>(impl->value[offset])) == 0)
        offset = next(offset);
    return offset;
}

EditError Document::Impl::replace(const Document& document, const Selection* ranges, size_t count, std::string_view source, uint64_t group)
{
    std::string inserted;
    if (!_normalize(source, inserted)) return EditError::InvalidText;
    std::vector<Impl::Edit> edits;
    edits.reserve(count);
    size_t previous = 0, end = 0;
    for (size_t i = 0; i < count; ++i) {
        auto start = document.clamp(ranges[i].start());
        // Two ranges at the same start would leave the order of their new texts open.
        if (i && (start < end || start == previous)) return EditError::InvalidRange;
        previous = start;
        end = document.clamp(ranges[i].end());
        auto removed = value.substr(start, end - start);
        if (removed != inserted) edits.push_back({start, std::move(removed), inserted});
    }
    commit(std::move(edits), group);
    return EditError::None;
}

void Document::Impl::commit(std::vector<Edit> edits, uint64_t group)
{
    if (edits.empty()) return;
    ++version;
    // Splice from the end so earlier offsets stay valid; undo walks the group backwards.
    std::reverse(edits.begin(), edits.end());
    for (const auto& edit : edits)
        splice(edit.offset, edit.removed.size(), edit.inserted);
    history.resize(historyPosition);
    if (group && !history.empty() && history.back().id == group) {
        for (auto& edit : edits)
            history.back().edits.push_back(std::move(edit));
        return;
    }
    history.push_back({version, std::move(edits)});
    ++historyPosition;
}

EditError Document::Impl::apply(const Document& document, const EditBatch& batch, uint64_t group)
{
    if (batch.revision != document.revision()) return EditError::StaleRevision;
    auto replacements = batch.edits;
    std::sort(replacements.begin(), replacements.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    std::vector<Impl::Edit> edits;
    for (size_t i = 0; i < replacements.size(); ++i) {
        auto& change = replacements[i];
        if (change.start > change.end || change.end > value.size() || document.clamp(change.start) != change.start || document.clamp(change.end) != change.end)
            return EditError::InvalidRange;
        if (i && (replacements[i - 1].end > change.start || replacements[i - 1].start == change.start)) return EditError::InvalidRange;
        std::string inserted;
        if (!_normalize(change.text, inserted)) return EditError::InvalidText;
        change.text = std::move(inserted);
        auto removed = value.substr(change.start, change.end - change.start);
        if (removed != change.text) edits.push_back({change.start, std::move(removed), change.text});
    }
    if (batch.selection) {
        auto next = value;
        for (auto it = edits.rbegin(); it != edits.rend(); ++it)
            next.replace(it->offset, it->removed.size(), it->inserted);
        auto boundary = [&](size_t offset) {
            return offset <= next.size() && (offset == next.size() || !_continuation(static_cast<unsigned char>(next[offset])));
        };
        if (!boundary(batch.selection->anchor) || !boundary(batch.selection->caret)) return EditError::InvalidRange;
    }
    commit(std::move(edits), group);
    return EditError::None;
}

uint64_t Document::Impl::undo()
{
    if (!historyPosition) return 0;
    const auto& group = history[--historyPosition];
    ++version;
    for (auto it = group.edits.rbegin(); it != group.edits.rend(); ++it)
        splice(it->offset, it->inserted.size(), it->removed);
    return group.id;
}

uint64_t Document::Impl::redo()
{
    if (historyPosition == history.size()) return 0;
    const auto& group = history[historyPosition++];
    ++version;
    for (const auto& edit : group.edits)
        splice(edit.offset, edit.removed.size(), edit.inserted);
    return group.id;
}

struct EditSession::Impl
{
    struct Record
    {
        uint64_t group;
        std::vector<Selection> before, after;
        size_t primaryBefore, primaryAfter;
    };
    bool replace(EditSession& session, const std::vector<Selection>& targets, std::string_view text, size_t primaryIndex);
    void record(EditSession& session, uint64_t top, std::vector<Selection> before, size_t primaryBefore);
    const Record* find(uint64_t group) const;
    Document& buffer;
    // Brought up to date when read; `seen` is the revision they refer to.
    std::vector<Selection> ranges;
    size_t primary = 0;
    uint64_t seen;
    std::vector<Record> records;  // ordered by group
    EditError failure = EditError::None;
    uint64_t open = 0;
    bool grouping = false, locked = false;
    explicit Impl(Document& document) :
        buffer(document), ranges(1), seen(document.revision()) {}
};

namespace
{
// An offset inside or at the end of a replaced range moves past its new text, while its start stays before it.
// Where text is only inserted, the end of a selection stays before it, so selections do not grow at their edges.
Selection _follow(Selection range, const Document::Impl::Change& change)
{
    auto map = [&](size_t offset, bool end) {
        if (offset < change.start || (offset == change.start && (change.removed || end))) return offset;
        return offset <= change.start + change.removed ? change.start + change.inserted : offset - change.removed + change.inserted;
    };
    auto start = map(range.start(), false), end = map(range.end(), !range.empty());
    return range.anchor <= range.caret ? Selection{start, end} : Selection{end, start};
}

// Merge selections in document order where they overlap or where a caret touches another selection;
// touching selections stay apart. `primary` moves to the selection that takes in the primary one.
void _merge(std::vector<Selection>& ranges, size_t& primary)
{
    size_t last = 0;
    for (size_t i = 1; i < ranges.size(); ++i) {
        auto& kept = ranges[last];
        auto next = ranges[i];
        if (next.start() < kept.end() || (next.start() == kept.end() && (kept.empty() || next.empty()))) {
            // The merged selection keeps the direction of the earlier one unless that one is a caret.
            const auto& side = kept.empty() ? next : kept;
            auto reversed = side.anchor > side.caret;
            auto start = kept.start(), end = std::max(kept.end(), next.end());
            kept = reversed ? Selection{end, start} : Selection{start, end};
        } else ranges[++last] = next;
        if (primary == i) primary = last;
    }
    ranges.resize(last + 1);
}

// Put selections that were placed freely back in document order, then merge them.
void _normalize(std::vector<Selection>& ranges, size_t& primary)
{
    auto primarySelection = ranges[primary];
    std::sort(ranges.begin(), ranges.end(), [](const Selection& a, const Selection& b) { return a.start() < b.start(); });
    primary = size_t(std::find_if(ranges.begin(), ranges.end(), [&](const Selection& range) { return range.anchor == primarySelection.anchor && range.caret == primarySelection.caret; }) - ranges.begin());
    _merge(ranges, primary);
}
}  // namespace

EditSession::EditSession(Document& document) :
    impl(new Impl(document)) {}
EditSession::~EditSession() = default;
EditSession::EditSession(const EditSession& other) :
    onSelection(other.onSelection), impl(new Impl(*other.impl)) {}
const Document& EditSession::document() const
{
    return impl->buffer;
}

void EditSession::readOnly(bool value)
{
    auto& state = *impl;
    if (state.locked == value) return;
    state.locked = value;
    endUndoGroup();
}
bool EditSession::readOnly() const
{
    return impl->locked;
}
void EditSession::endUndoGroup()
{
    impl->grouping = false;
}
bool EditSession::undoGroupOpen() const
{
    return impl->grouping;
}
EditError EditSession::error() const
{
    return impl->failure;
}

const std::vector<Selection>& EditSession::selections() const
{
    auto& state = *impl;
    if (state.seen == state.buffer.revision()) return state.ranges;
    const Document::Impl::Change* changes = nullptr;
    size_t count = 0;
    // Once the whole text was replaced there is nothing to follow, so the old offsets are only clamped.
    if (!Document::Impl::get(state.buffer).changesSince(state.seen, changes, count)) {
        for (auto& range : state.ranges)
            range = {state.buffer.clamp(range.anchor), state.buffer.clamp(range.caret)};
        _merge(state.ranges, state.primary);
    }
    // Merging after every change gives the same selections however rarely they are read.
    for (size_t i = 0; i < count; ++i) {
        for (auto& range : state.ranges)
            range = _follow(range, changes[i]);
        _merge(state.ranges, state.primary);
    }
    state.seen = state.buffer.revision();
    return state.ranges;
}

Selection EditSession::selection() const
{
    auto& state = *impl;
    const auto& list = selections();
    return list[state.primary];
}

void EditSession::select(size_t anchor, size_t caret)
{
    auto& state = *impl;
    state.ranges.assign(1, Selection{state.buffer.clamp(anchor), state.buffer.clamp(caret)});
    state.primary = 0;
    state.seen = state.buffer.revision();
    if (onSelection) onSelection();
}

void EditSession::select(const std::vector<Selection>& list, size_t primaryIndex)
{
    auto& state = *impl;
    if (list.empty()) return;
    state.ranges = list;
    for (auto& range : state.ranges)
        range = {state.buffer.clamp(range.anchor), state.buffer.clamp(range.caret)};
    state.primary = std::min(primaryIndex, state.ranges.size() - 1);
    state.seen = state.buffer.revision();
    _normalize(state.ranges, state.primary);
    if (onSelection) onSelection();
}

void EditSession::add(size_t anchor, size_t caret)
{
    auto& state = *impl;
    selections();
    state.ranges.push_back({state.buffer.clamp(anchor), state.buffer.clamp(caret)});
    state.primary = state.ranges.size() - 1;
    _normalize(state.ranges, state.primary);
    if (onSelection) onSelection();
}

void EditSession::selectAll()
{
    auto& state = *impl;
    select(0, state.buffer.text().size());
}

std::string EditSession::selectedText() const
{
    auto& state = *impl;
    const auto& list = selections();
    std::string text;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i) text += '\n';
        text.append(state.buffer.text(), list[i].start(), list[i].end() - list[i].start());
    }
    return text;
}

void EditSession::move(Move direction, bool extend)
{
    auto& state = *impl;
    selections();
    for (auto& range : state.ranges) {
        size_t position = range.caret;
        if (!extend && !range.empty() && (direction == Move::Left || direction == Move::Right)) position = direction == Move::Left ? range.start() : range.end();
        else switch (direction) {
                case Move::Left: position = state.buffer.previous(position); break;
                case Move::Right: position = state.buffer.next(position); break;
                case Move::WordLeft: position = state.buffer.wordPrevious(position); break;
                case Move::WordRight: position = state.buffer.wordNext(position); break;
                case Move::LineStart: position = state.buffer.lineStart(state.buffer.lineAt(position)); break;
                case Move::LineEnd: position = state.buffer.lineEnd(state.buffer.lineAt(position)); break;
                case Move::Start: position = 0; break;
                case Move::End: position = state.buffer.text().size(); break;
            }
        range = {extend ? range.anchor : position, position};
    }
    _normalize(state.ranges, state.primary);
    if (onSelection) onSelection();
}

bool EditSession::Impl::replace(EditSession& session, const std::vector<Selection>& targets, std::string_view text, size_t primaryIndex)
{
    if (locked) {
        failure = EditError::ReadOnly;
        return false;
    }
    failure = EditError::None;
    auto top = Document::Impl::get(buffer).group();
    auto revision = buffer.revision();
    // Selections from before the edit are needed only when it starts a new undo group.
    std::vector<Selection> before;
    if (!grouping || !open || top != open) before = ranges;
    auto primaryBefore = primary;
    failure = Document::Impl::get(buffer).replace(buffer, targets.data(), targets.size(), text, grouping ? open : 0);
    if (failure != EditError::None) return false;
    // The targets follow the edit like any selection, so each one ends after its new text.
    ranges = targets;
    primary = primaryIndex;
    seen = revision;
    session.selections();
    for (auto& range : ranges)
        range = {range.end(), range.end()};
    _merge(ranges, primary);
    if (buffer.revision() == revision) {
        if (session.onSelection) session.onSelection();
        return false;
    }
    record(session, top, std::move(before), primaryBefore);
    if (session.onSelection) session.onSelection();
    Document::Impl::get(buffer).notify();
    return true;
}

bool EditSession::insert(std::string_view text)
{
    auto& state = *impl;
    const auto& targets = selections();
    return state.replace(*this, targets, text, state.primary);
}

bool EditSession::backspace(bool word)
{
    auto& state = *impl;
    // Carets take in the text before them, and deletions that overlap become one.
    auto targets = selections();
    for (auto& range : targets)
        if (range.empty()) range.anchor = word ? state.buffer.wordPrevious(range.caret) : state.buffer.previous(range.caret);
    auto primaryIndex = state.primary;
    _normalize(targets, primaryIndex);
    return state.replace(*this, targets, "", primaryIndex);
}

bool EditSession::deleteForward(bool word)
{
    auto& state = *impl;
    auto targets = selections();
    for (auto& range : targets)
        if (range.empty()) range.caret = word ? state.buffer.wordNext(range.caret) : state.buffer.next(range.caret);
    auto primaryIndex = state.primary;
    _normalize(targets, primaryIndex);
    return state.replace(*this, targets, "", primaryIndex);
}

bool EditSession::apply(const EditBatch& batch)
{
    auto& state = *impl;
    if (state.locked) {
        state.failure = EditError::ReadOnly;
        return false;
    }
    selections();
    auto top = Document::Impl::get(state.buffer).group();
    auto revision = state.buffer.revision();
    std::vector<Selection> before;
    if (!state.grouping || !state.open || top != state.open) before = state.ranges;
    auto primaryBefore = state.primary;
    state.failure = Document::Impl::get(state.buffer).apply(state.buffer, batch, state.grouping ? state.open : 0);
    if (state.failure != EditError::None) return false;
    if (batch.selection) select(batch.selection->anchor, batch.selection->caret);
    if (state.buffer.revision() != revision) {
        state.record(*this, top, std::move(before), primaryBefore);
        if (!batch.selection && onSelection) onSelection();
        Document::Impl::get(state.buffer).notify();
    }
    return true;
}

void EditSession::beginUndoGroup()
{
    auto& state = *impl;
    if (state.grouping) return;
    state.grouping = true;
    state.open = 0;
}

void EditSession::Impl::record(EditSession& session, uint64_t top, std::vector<Selection> before, size_t primaryBefore)
{
    const auto& after = session.selections();
    auto group = Document::Impl::get(buffer).group();
    if (grouping) open = group;
    if (!records.empty() && records.back().group == group) {
        records.back().after = after;
        records.back().primaryAfter = primary;
        return;
    }
    // A new group drops the redo branch, so records after the previous latest group are stale.
    while (!records.empty() && records.back().group > top)
        records.pop_back();
    records.push_back({group, std::move(before), after, primaryBefore, primary});
}

const EditSession::Impl::Record* EditSession::Impl::find(uint64_t group) const
{
    auto it = std::lower_bound(records.begin(), records.end(), group, [](const Record& record, uint64_t value) { return record.group < value; });
    return it != records.end() && it->group == group ? &*it : nullptr;
}

bool EditSession::undo()
{
    auto& state = *impl;
    if (state.locked) {
        state.failure = EditError::ReadOnly;
        return false;
    }
    state.failure = EditError::None;
    state.grouping = false;
    auto group = Document::Impl::get(state.buffer).undo();
    if (auto entry = state.find(group)) select(entry->before, entry->primaryBefore);
    else if (group) {
        selections();
        if (onSelection) onSelection();
    }
    if (group) Document::Impl::get(state.buffer).notify();
    return group != 0;
}

bool EditSession::redo()
{
    auto& state = *impl;
    if (state.locked) {
        state.failure = EditError::ReadOnly;
        return false;
    }
    state.failure = EditError::None;
    state.grouping = false;
    auto group = Document::Impl::get(state.buffer).redo();
    if (auto entry = state.find(group)) select(entry->after, entry->primaryAfter);
    else if (group) {
        selections();
        if (onSelection) onSelection();
    }
    if (group) Document::Impl::get(state.buffer).notify();
    return group != 0;
}

}  // namespace tn
