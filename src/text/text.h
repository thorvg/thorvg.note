#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <functional>
#include <vector>
#include <memory>

#define TN_DECLARE_IMPL_TYPE(access) \
    access:                          \
    struct Impl

#define TN_DECLARE_IMPL_STORAGE(access) \
    access:                             \
    std::unique_ptr<Impl> impl

#define TN_DECLARE_IMPL(access)   \
    TN_DECLARE_IMPL_TYPE(access); \
    TN_DECLARE_IMPL_STORAGE(access)

namespace tn
{

/**
 * @brief A directed selection expressed as UTF-8 byte offsets.
 *
 * @note Equal endpoints represent a caret. Offsets identify codepoint boundaries, not grapheme clusters.
 *
 * @code
 *     tn::Selection caret{4, 4};
 *     assert(caret.empty());
 *
 *     tn::Selection backward{11, 6};  // Anchor at 11; caret at 6.
 *     assert(backward.start() == 6);
 *     assert(backward.end() == 11);
 * @endcode
 */
struct Selection
{
    /**
     * @brief Fixed and moving endpoints of the selection, respectively.
     */
    size_t anchor = 0, caret = 0;

    /**
     * @brief Returns the lower endpoint of the half-open selection range.
     *
     * @return Lower endpoint as a document-relative UTF-8 byte offset.
     */
    size_t start() const { return std::min(anchor, caret); }

    /**
     * @brief Returns the upper endpoint of the half-open selection range.
     *
     * @return Exclusive upper endpoint as a document-relative UTF-8 byte offset.
     */
    size_t end() const { return std::max(anchor, caret); }

    /**
     * @brief Returns whether the selection contains no bytes.
     *
     * @return True if anchor and caret are equal; otherwise false.
     */
    bool empty() const { return anchor == caret; }
};

/**
 * @brief A replacement of a half-open UTF-8 byte range.
 *
 * @code
 *     tn::TextEdit insert{3, 3, "!"};
 *     tn::TextEdit erase{0, 3, ""};
 *     tn::TextEdit replace{0, 3, "new"};
 * @endcode
 */
struct TextEdit
{
    /**
     * @brief Byte boundaries of the range to replace; equal values insert text.
     */
    size_t start = 0, end = 0;

    /**
     * @brief Replacement text encoded as UTF-8 without NUL bytes.
     */
    std::string text;
};

/**
 * @brief Edits expressed against one document revision.
 *
 * @note Ranges must not overlap or share a start offset. An optional selection refers to the resulting text.
 *
 * @code
 *     tn::Document document;
 *     document.text("one two");
 *     tn::EditSession session(document);
 *
 *     tn::EditBatch batch;
 *     batch.revision = document.revision();
 *     batch.edits = {{0, 3, "1"}, {4, 7, "2"}};
 *     batch.selection = tn::Selection{3, 3};
 *
 *     // Edit ranges use the original text; the caret uses the resulting text.
 *     session.apply(batch);
 *
 *     assert(document.text() == "1 2");
 *     assert(session.selection().caret == 3);
 * @endcode
 */
struct EditBatch
{
    /**
     * @brief Document revision against which the edit ranges were prepared.
     */
    uint64_t revision = 0;

    /**
     * @brief Replacements to apply atomically; callers need not sort them.
     */
    std::vector<TextEdit> edits;

    /**
     * @brief Selection to restore after a successful batch through EditSession.
     */
    std::optional<Selection> selection;
};

/**
 * @brief Result of querying a document's changed range.
 */
enum class ChangeStatus
{
    Unchanged,  ///< No edits since the requested revision.
    Changed,    ///< Edits occurred; the range uses current document offsets.
    Untracked   ///< History is unavailable; rebuild the view's full layout.
};

/**
 * @brief Result of the last edit request.
 */
enum class EditError
{
    None,           ///< Accepted request, including a no-op.
    ReadOnly,       ///< The session rejects text mutations.
    StaleRevision,  ///< The batch refers to a different document revision.
    InvalidRange,   ///< Invalid, overlapping, or non-boundary edit or selection ranges.
    InvalidText     ///< Invalid UTF-8 or embedded NUL bytes.
};

/**
 * @brief A UTF-8 text buffer with line indexing and grouped undo/redo.
 *
 * @note Positions are byte offsets. Navigation advances by codepoint rather than grapheme cluster.
 * @note Input line endings are normalized from CRLF or CR to LF.
 *
 * @code
 *     tn::Document document;
 *     document.text("one\r\ntwo");  // Load content and clear history.
 *
 *     assert(document.text() == "one\ntwo");
 *     assert(document.lineCount() == 2);
 *     assert(document.lineStart(1) == 4);
 *     assert(document.lineEnd(0) == 3);  // Excludes LF.
 * @endcode
 */
struct Document
{
    Document();
    ~Document();
    Document(const Document&);
    /**
     * @brief Copies document content and history while retaining destination subscriptions.
     *
     * @return This document after assignment.
     */
    Document& operator=(const Document&);
    /**
     * @brief Replaces the entire document and clears undo/redo history.
     *
     * @param[in] text UTF-8 content without NUL bytes.
     *
     * @retval true If the content is accepted.
     * @retval false If the input contains NUL bytes or invalid UTF-8; text, revision, and history remain unchanged.
     *
     * @note Invalid UTF-8 includes invalid leading or continuation bytes, incomplete sequences, overlong encodings,
     * surrogate codepoints (U+D800..U+DFFF), and values above U+10FFFF.
     *
     * @note CRLF and CR are normalized to LF. Previously tracked changes are discarded.
     */
    bool text(std::string_view text);

    /**
     * @brief Returns the document content as UTF-8.
     *
     * @return Borrowed LF-normalized UTF-8 content; copy it if it must survive document mutation.
     *
     * @note The content may change on the next document mutation.
     */
    const std::string& text() const;

    /**
     * @brief Returns the current revision used to validate edits and track changes.
     *
     * @return Current document revision for edit validation and change tracking.
     *
     * @note Increments on text changes, successful session undo/redo, and every accepted text() replacement.
     *
     * @see changedSince()
     */
    uint64_t revision() const;

    /**
     * @brief Subscribes to completed document changes, including undo/redo and whole-text replacement.
     *
     * @return Subscription ID, or zero for an empty callback.
     * @note Callbacks must not throw or mutate the document recursively. Subscribe and unsubscribe are allowed.
     * New subscriptions start with the next change. Copies do not copy subscriptions; assignment keeps destination subscriptions.
     *
     * @code
     *     tn::Document document;
     *     tn::EditSession session(document);
     *     bool redraw = false;
     *     bool save = false;
     *
     *     auto view = document.subscribe([&] { redraw = true; });
     *     auto autosave = document.subscribe([&] { save = true; });
     *
     *     session.insert("hello");
     *     assert(redraw && save);
     *
     *     // Disconnect before destroying the view's captured state.
     *     document.unsubscribe(view);
     *     redraw = false;
     *     save = false;
     *
     *     // Whole-text replacement also notifies subscribers.
     *     document.text("reloaded");
     *     assert(!redraw && save);
     *
     *     document.unsubscribe(autosave);
     * @endcode
     *
     * @see unsubscribe()
     */
    uint64_t subscribe(std::function<void()> callback);

    /**
     * @brief Removes a subscription; unknown IDs are ignored.
     *
     * @note Unsubscribe before destroying state captured by the callback.
     * @see subscribe()
     */
    void unsubscribe(uint64_t subscription);

    /**
     * @brief Finds the smallest current byte range containing changes after a revision.
     *
     * @param[in] revision Previously observed document revision.
     * @param[out] start Beginning of the affected range.
     * @param[out] end End of the affected range, exclusive.
     *
     * @retval ChangeStatus::Unchanged If no edits occurred; start and end are zero.
     * @retval ChangeStatus::Changed If edits occurred; start and end enclose them, even for a deletion with an empty range.
     * @retval ChangeStatus::Untracked If the revision cannot be tracked; start and end are zero.
     *
     * @note An EditBatch increments the revision once if text changes. The range encloses all its edits,
     * including unchanged gaps.
     * For example, replacing [2, 4) and [20, 23) with equal-length text returns [2, 23).
     *
     * @par Usage
     * Save revision() with cached layout. Update the range on Changed; rebuild the full layout on Untracked.
     *
     * @see revision(), ChangeStatus
     */
    ChangeStatus changedSince(uint64_t revision, size_t& start, size_t& end) const;

    /**
     * @brief Returns the number of lines, including one line for an empty document.
     *
     * @return Number of logical lines, including a trailing empty line after a final LF; at least one.
     */
    size_t lineCount() const;

    /**
     * @brief Returns the first byte offset of a zero-based line.
     *
     * @param[in] line Line index, clamped to the last line.
     *
     * @return Document-relative byte offset of the requested line start, or the last line start if out of range.
     *
     * @code
     *     tn::Document document;
     *     document.text("ab\ncd\n");
     *
     *     // Byte offsets: a(0) b(1) LF(2) c(3) d(4) LF(5); end at 6.
     *     assert(document.lineStart(0) == 0);   // "ab"
     *     assert(document.lineStart(1) == 3);   // "cd"
     *     assert(document.lineStart(2) == 6);   // Trailing empty line.
     *     assert(document.lineStart(99) == 6);  // Clamped to the last line.
     * @endcode
     *
     * @see lineEnd(), lineCount()
     */
    size_t lineStart(size_t line) const;

    /**
     * @brief Returns the byte offset after a line's content, excluding its LF separator.
     *
     * @param[in] line Zero-based line index.
     *
     * @return The document size for the last line or an out-of-range index.
     *
     * @code
     *     tn::Document document;
     *     document.text("ab\ncd\n");
     *
     *     assert(document.lineEnd(0) == 2);   // After 'b', before LF.
     *     assert(document.lineEnd(1) == 5);   // After 'd', before LF.
     *     assert(document.lineEnd(2) == 6);   // Trailing empty line.
     *     assert(document.lineEnd(99) == 6);  // Document size.
     *
     *     auto start = document.lineStart(1);
     *     auto length = document.lineEnd(1) - start;
     *     assert(document.text().substr(start, length) == "cd");
     * @endcode
     *
     * @see lineStart(), lineCount()
     */
    size_t lineEnd(size_t line) const;

    /**
     * @brief Returns the zero-based line containing a byte offset.
     *
     * @param[in] offset Byte offset, clamped to the document size.
     *
     * @return Zero-based index of the line containing the clamped byte offset.
     *
     * @code
     *     tn::Document document;
     *     document.text("ab\ncd\n");
     *
     *     // Byte offsets: a(0) b(1) LF(2) c(3) d(4) LF(5); end at 6.
     *     assert(document.lineAt(0) == 0);   // 'a'
     *     assert(document.lineAt(2) == 0);   // LF belongs to the preceding line.
     *     assert(document.lineAt(3) == 1);   // 'c'
     *     assert(document.lineAt(5) == 1);   // Second LF.
     *     assert(document.lineAt(6) == 2);   // Trailing empty line.
     *     assert(document.lineAt(99) == 2);  // Clamped to the document size.
     * @endcode
     *
     * @see lineStart(), lineEnd(), lineCount()
     */
    size_t lineAt(size_t offset) const;

    /**
     * @brief Clamps an offset to the preceding valid UTF-8 boundary.
     *
     * @param[in] offset Byte offset, first limited to the document size.
     *
     * @return Document-relative byte offset at or before the limited input, on a valid UTF-8 codepoint boundary.
     *
     * @code
     *     tn::Document document;
     *     document.text("Aé");  // 'A' uses one byte; 'é' uses two.
     *
     *     assert(document.clamp(2) == 1);
     *     assert(document.next(1) == 3);
     *     assert(document.previous(3) == 1);
     * @endcode
     */
    size_t clamp(size_t offset) const;

    /**
     * @brief Returns the preceding codepoint boundary, or zero at the document start.
     *
     * @return Document-relative byte offset before the clamped position, or zero at the document start.
     *
     * @code
     *     tn::Document document;
     *     document.text("AéB");
     *
     *     // Boundaries: A(0), é(1), B(3), end(4).
     *     assert(document.previous(4) == 3);  // Start of 'B'.
     *     assert(document.previous(3) == 1);  // Start of 'é', two bytes back.
     *     assert(document.previous(1) == 0);  // Start of 'A'.
     *     assert(document.previous(0) == 0);  // Already at the document start.
     *
     *     // Clamp inside 'é' to 1, then move backward.
     *     assert(document.previous(2) == 0);
     * @endcode
     *
     * @see next(), clamp()
     */
    size_t previous(size_t offset) const;

    /**
     * @brief Returns the following codepoint boundary, or the document size at its end.
     *
     * @return Document-relative byte offset after the clamped codepoint, or the document size at its end.
     *
     * @code
     *     tn::Document document;
     *     document.text("AéB");
     *
     *     // Boundaries: A(0), é(1), B(3), end(4).
     *     assert(document.next(0) == 1);  // After 'A'.
     *     assert(document.next(1) == 3);  // After 'é', two bytes forward.
     *     assert(document.next(3) == 4);  // After 'B', at the document end.
     *     assert(document.next(4) == 4);  // Already at the document end.
     *
     *     // Clamp inside 'é' to 1, then move forward.
     *     assert(document.next(2) == 3);
     * @endcode
     *
     * @see previous(), clamp()
     */
    size_t next(size_t offset) const;

    /**
     * @brief Moves backward over whitespace and the preceding character category.
     *
     * @return Document-relative byte offset at the start of the preceding character category, or zero.
     *
     * @note Word navigation uses whitespace, ASCII punctuation, and word-character categories; it is not linguistic segmentation.
     */
    size_t wordPrevious(size_t offset) const;

    /**
     * @brief Moves forward over the current character category and following whitespace.
     *
     * @return Document-relative byte offset after the current character category and following whitespace, or the document size.
     *
     * @see wordPrevious()
     */
    size_t wordNext(size_t offset) const;

    /**
     * @brief Returns whether an applied group is available to undo.
     *
     * @return True if an applied edit group is available to undo; otherwise false.
     */
    bool canUndo() const;

    /**
     * @brief Returns whether an undone group is available to redo.
     *
     * @return True if an undone edit group is available to redo; otherwise false.
     */
    bool canRedo() const;

    struct Impl;
    TN_DECLARE_IMPL_STORAGE(private);
};

/**
 * @brief Navigation commands for a selection session.
 */
enum class Move
{
    Left,       ///< Previous codepoint.
    Right,      ///< Next codepoint.
    WordLeft,   ///< Previous word boundary.
    WordRight,  ///< Next word boundary.
    LineStart,  ///< Start of the current line.
    LineEnd,    ///< End of the current line.
    Start,      ///< Start of the document.
    End         ///< End of the document.
};

/**
 * @brief An editing session with independent selections and selection restoration during undo/redo.
 *
 * @note The document must outlive the session. The read-only policy applies only to mutations through this session.
 * @note Selections follow external document edits when read. Overlapping selections and touching carets merge;
 * adjacent nonempty selections remain separate.
 *
 * @code
 *     tn::Document document;
 *     document.text("one two");
 *
 *     // Split views share text and history, with independent selections.
 *     tn::EditSession left(document);
 *     tn::EditSession right(document);
 *     left.select(0, 3);
 *     right.select(4, 7);
 *
 *     // Replace only the left session's selection.
 *     left.insert("1");
 *     assert(document.text() == "1 two");
 *     assert(right.selectedText() == "two");  // Follows the edit to [2, 5).
 * @endcode
 */
struct EditSession
{
    /**
     * @brief Creates an editable session with a caret at the document start.
     *
     * @param[in] document Document edited by this session; ownership is not transferred.
     */
    explicit EditSession(Document& document);
    ~EditSession();

    /**
     * @brief Copies session state while sharing the same document and its undo/redo history.
     *
     * @param[in] other Session whose selections, primary index, selection-restoration records,
     * read-only policy, undo-group state, and error are copied independently.
     *
     * @note onSelection is also copied, including its captures. Rebind it for a different view.
     * @see EditSession(Document&)
     */
    EditSession(const EditSession& other);
    EditSession& operator=(const EditSession&) = delete;

    /**
     * @brief Returns the document observed by this session.
     *
     * @return Borrowed document reference; ownership remains with the caller that created the session.
     */
    const Document& document() const;

    /**
     * @brief Returns selections in document order after following pending document changes.
     *
     * @return Borrowed normalized selection list, valid until the selections change.
     *
     * @note The list remains valid until the selections change.
     */
    const std::vector<Selection>& selections() const;

    /**
     * @brief Returns the primary selection after following pending document changes.
     *
     * @return A copy of the primary selection using current document byte offsets.
     *
     * @note The most recently added selection, or the selection it merged into, is primary.
     */
    Selection selection() const;

    /**
     * @brief Replaces all selections with one selection.
     *
     * @param[in] anchor Fixed endpoint, clamped to a UTF-8 boundary.
     * @param[in] caret Moving endpoint, clamped to a UTF-8 boundary.
     */
    void select(size_t anchor, size_t caret);

    /**
     * @brief Replaces and normalizes the selection list.
     *
     * @param[in] list Selections whose endpoints are clamped to UTF-8 boundaries. An empty list changes nothing.
     * @param[in] primaryIndex Primary selection index, clamped to the last input selection.
     */
    void select(const std::vector<Selection>& list, size_t primaryIndex);

    /**
     * @brief Adds a selection as primary and merges overlapping ranges.
     *
     * @param[in] anchor Fixed endpoint, clamped to a UTF-8 boundary.
     * @param[in] caret Moving endpoint, clamped to a UTF-8 boundary.
     *
     * @code
     *     tn::Document document;
     *     document.text("one two");
     *     tn::EditSession session(document);
     *
     *     session.select(3, 3);
     *     session.add(7, 7);  // Two carets in this session.
     *
     *     session.insert("!");
     *     assert(document.text() == "one! two!");
     *
     *     // Restore both insertions and the original carets.
     *     session.undo();
     *     assert(document.text() == "one two");
     *     assert(session.selections().size() == 2);
     * @endcode
     */
    void add(size_t anchor, size_t caret);

    /**
     * @brief Selects the entire document.
     */
    void selectAll();

    /**
     * @brief Returns selected text in document order, separated by LF characters.
     *
     * @return Owned UTF-8 text joining all selections with LF separators; carets contribute empty text.
     */
    std::string selectedText() const;

    /**
     * @brief Moves each selection according to a navigation command.
     *
     * @param[in] direction Navigation command.
     * @param[in] extend If true, keeps the anchor and moves only the caret.
     *
     * @code
     *     tn::Document document;
     *     document.text("one two");
     *     tn::EditSession session(document);
     *     session.readOnly(true);
     *
     *     session.move(tn::Move::WordRight);  // Caret at byte 4.
     *     session.move(tn::Move::End, true);  // Extend from the anchor at byte 4.
     *
     *     assert(session.selectedText() == "two");
     * @endcode
     */
    void move(Move direction, bool extend = false);

    /**
     * @brief Callback after selection commands completed through this session.
     *
     * @note Unchanged ranges may also be reported. Following external edits on read does not invoke the callback.
     * @note Callbacks must not throw or edit recursively.
     *
     * @par Usage
     * Refresh the session's caret and selection display after navigation or selection commands.
     *
     * @code
     *     tn::Document document;
     *     document.text("hello");
     *     tn::EditSession session(document);
     *     session.readOnly(true);
     *     bool redraw = false;
     *
     *     session.onSelection = [&] {
     *         redraw = true;
     *     };
     *
     *     // Move the caret without changing text.
     *     session.move(tn::Move::Right);
     *     assert(redraw);
     *     assert(session.selection().caret == 1);
     *     assert(document.text() == "hello");
     *
     *     // Extend the selection; redraw its highlight.
     *     redraw = false;
     *     session.move(tn::Move::Right, true);
     *     assert(redraw);
     *     assert(session.selectedText() == "e");
     *
     *     // Disconnect before destroying captured state.
     *     session.onSelection = {};
     * @endcode
     */
    std::function<void()> onSelection;

    /**
     * @brief Enables or disables text mutations through this session.
     *
     * @param[in] value If true, rejects edit and undo/redo requests.
     *
     * @note Changing the policy ends the current undo group. Navigation remains available.
     *
     * @par Usage
     * Set true for a preview session. Other editable sessions can still change the shared document.
     */
    void readOnly(bool value);

    /**
     * @brief Returns whether text mutations through this session are disabled.
     *
     * @return True if edits and undo/redo through this session are disabled; otherwise false.
     */
    bool readOnly() const;

    /**
     * @brief Replaces every selection with text and leaves a caret after each insertion.
     *
     * @param[in] text UTF-8 content without NUL bytes; line endings are normalized to LF.
     *
     * @retval false If text is unchanged, read-only, or the edit is rejected.
     * @retval true If text changed.
     *
     * @note A caret inserts text at its position.
     * @note A nonempty selection replaces its selected text.
     * @note Multiple selections receive the same text as one undo step, or join the open undo group.
     * @note EditError::None distinguishes a valid no-op from a rejected edit.
     *
     * @code
     *     tn::Document document;
     *     document.text("hello world");
     *     tn::EditSession session(document);
     *
     *     session.select(11, 6);  // Backward selection of "world".
     *     session.insert("editor");
     *     assert(document.text() == "hello editor");
     *     assert(session.selection().caret == 12);
     *
     *     session.undo();
     *     assert(document.text() == "hello world");
     *     assert(session.selection().caret == 6);
     *
     *     session.redo();
     *     assert(document.text() == "hello editor");
     * @endcode
     */
    bool insert(std::string_view text);

    /**
     * @brief Deletes selections, or text before each caret when empty.
     *
     * @param[in] word If true, deletes to a word boundary; otherwise deletes one codepoint.
     *
     * @retval false If text is unchanged, read-only, or the edit is rejected.
     * @retval true If text changed.
     *
     * @note EditError::None distinguishes a valid no-op from a rejected edit.
     */
    bool backspace(bool word = false);

    /**
     * @brief Deletes selections, or text after each caret when empty.
     *
     * @param[in] word If true, deletes to a word boundary; otherwise deletes one codepoint.
     *
     * @retval false If text is unchanged, read-only, or the edit is rejected.
     * @retval true If text changed.
     *
     * @note EditError::None distinguishes a valid no-op from a rejected edit.
     */
    bool deleteForward(bool word = false);

    /**
     * @brief Applies a batch and restores its optional resulting selection.
     *
     * @param[in] batch Edits referring to the current document revision.
     *
     * @retval false If read-only or batch validation fails.
     * @retval true If the batch is accepted.
     *
     * @note Without an explicit selection, existing selections follow the batch edits.
     *
     * @par Usage
     * Apply formatter or completion edits using the revision captured with their source text.
     * A stale batch is rejected; inspect error() and recompute it from the current document.
     */
    bool apply(const EditBatch& batch);

    /**
     * @brief Groups subsequent edits through this session into one undo step.
     *
     * @note An intervening edit from another session starts a new undo step.
     *
     * @code
     *     tn::Document document;
     *     tn::EditSession session(document);
     *
     *     session.beginUndoGroup();
     *     session.insert("a");
     *     session.insert("b");
     *     session.endUndoGroup();
     *
     *     session.undo();  // Remove both insertions as one step.
     *     assert(document.text().empty());
     * @endcode
     */
    void beginUndoGroup();

    /**
     * @brief Ends explicit grouping for subsequent edits.
     */
    void endUndoGroup();

    /**
     * @brief Returns whether explicit undo grouping is enabled.
     *
     * @return True if explicit grouping of subsequent session edits is enabled; otherwise false.
     */
    bool undoGroupOpen() const;

    /**
     * @brief Undoes one document group and restores selections recorded by this session.
     *
     * @return Whether a group was undone; false when read-only or no undo is available.
     */
    bool undo();

    /**
     * @brief Redoes one document group and restores selections recorded by this session.
     *
     * @return Whether a group was redone; false when read-only or no redo is available.
     */
    bool redo();

    /**
     * @brief Returns the last edit failure reason.
     *
     * @return EditError::None for an accepted request; otherwise the rejection reason.
     *
     * @note An accepted edit or undo/redo request clears the previous reason.
     */
    EditError error() const;

    TN_DECLARE_IMPL(private);
};

}  // namespace tn

#undef TN_DECLARE_IMPL
#undef TN_DECLARE_IMPL_STORAGE
#undef TN_DECLARE_IMPL_TYPE
