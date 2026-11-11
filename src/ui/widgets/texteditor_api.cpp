#include "texteditor.h"
#include <algorithm>

namespace ui
{

const TextEditorOptions& TextEditor::options() const
{
    return impl->configuration;
}

bool TextEditor::focused() const
{
    return impl->hasFocus;
}

bool TextEditor::wantsTextInput() const
{
    return impl->hasFocus && !impl->configuration.readOnly && impl->input.text;
}

bool TextEditor::composing() const
{
    return !impl->preedit.empty();
}

const EditorInputState& TextEditor::inputState() const
{
    return impl->input;
}

const tn::EditSession& TextEditor::session() const
{
    return impl->editing;
}

float TextEditor::scrollX() const
{
    return impl->horizontal;
}

float TextEditor::scrollY() const
{
    return impl->vertical;
}

void TextEditor::beginUndoGroup()
{
    impl->editing.beginUndoGroup();
}

void TextEditor::endUndoGroup()
{
    impl->editing.endUndoGroup();
}

void TextEditor::inputState(EditorInputState value)
{
    if (impl->input.text == value.text && impl->input.caret == value.caret && impl->input.status == value.status) return;
    impl->input = std::move(value);
    if (!impl->input.text) impl->preedit.clear();
    impl->invalidate();
}

bool TextEditor::applyEdits(const tn::EditBatch& batch, std::string& error)
{
    return impl->commitEdits(batch, error);
}

bool TextEditor::insert(std::string_view text, std::string& error)
{
    auto selection = impl->editing.selection();
    return impl->commitEdits({.revision = impl->editing.document().revision(), .edits = {{selection.start(), selection.end(), std::string(text)}}}, error, true);
}

void TextEditor::selectAll()
{
    select(0, impl->editing.document().text().size());
}

bool TextEditor::Impl::commitEdits(const tn::EditBatch& batch, std::string& error, bool collapseUnchanged)
{
    error.clear();
    if (configuration.readOnly || owner.composing()) {
        error = configuration.readOnly ? "editor is read-only" : "IME composition is active";
        return false;
    }
    auto revision = editing.document().revision();
    if (!editing.apply(batch)) {
        switch (editing.error()) {
            case tn::EditError::ReadOnly: error = "editor is read-only"; break;
            case tn::EditError::StaleRevision: error = "document revision changed"; break;
            case tn::EditError::InvalidRange: error = "invalid edit or selection ranges"; break;
            case tn::EditError::InvalidText: error = "edit text must be UTF-8 without NUL bytes"; break;
            case tn::EditError::None: break;
        }
        return false;
    }
    if (collapseUnchanged && revision == editing.document().revision()) {
        auto after = editing.selection();
        editing.select(after.end(), after.end());
    }
    edited(revision != editing.document().revision());
    return true;
}

bool TextEditor::navigate(std::string_view motion, size_t count, bool extend)
{
    if (composing() || count > 100000) return false;
    Key key;
    Modifiers modifiers{extend, false, false};
    if (motion == "left" || motion == "word_left") {
        key = Key::Left;
        modifiers.word = motion == "word_left";
    } else if (motion == "right" || motion == "word_right") {
        key = Key::Right;
        modifiers.word = motion == "word_right";
    } else if (motion == "up") key = Key::Up;
    else if (motion == "down") key = Key::Down;
    else if (motion == "page_up") key = Key::PageUp;
    else if (motion == "page_down") key = Key::PageDown;
    else if (motion == "line_start" || motion == "start") {
        key = Key::Home;
        modifiers.primary = motion == "start";
    } else if (motion == "line_end" || motion == "end") {
        key = Key::End;
        modifiers.primary = motion == "end";
    } else return false;
    impl->relayout();
    for (size_t i = 0; i < count; ++i)
        impl->view.key(key, modifiers, impl->textArea().h);
    if (key != Key::Up && key != Key::Down && key != Key::PageUp && key != Key::PageDown) impl->view.resetPreferredX();
    impl->ensureCaret();
    impl->invalidate();
    return true;
}

bool TextEditor::undo()
{
    if (impl->configuration.readOnly || composing()) return false;
    auto changed = impl->editing.undo();
    impl->edited(changed);
    return changed;
}

bool TextEditor::redo()
{
    if (impl->configuration.readOnly || composing()) return false;
    auto changed = impl->editing.redo();
    impl->edited(changed);
    return changed;
}

Rect TextEditor::Impl::paintCaret()
{
    auto rect = owner.caretRect();
    if (input.caret == CaretStyle::Bar || owner.composing()) return rect;
    auto& document = editing.document();
    auto offset = editing.selection().caret;
    auto row = document.lineAt(offset);
    const auto& lines = view.layout();
    float advance = measure(" ");
    if (row < lines.size() && offset < document.lineEnd(row)) advance = lines[row].position(document.next(offset)) - lines[row].position(offset);
    rect.w = std::max(1.5f, advance);
    if (input.caret == CaretStyle::Underline) {
        rect.y += rect.h - 2;
        rect.h = 2;
    }
    return rect;
}

}  // namespace ui
