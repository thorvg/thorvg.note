#include "texteditor.h"
#include <algorithm>
#include <cmath>

namespace ui
{
namespace
{
constexpr float scrollbarHeight = 12;
constexpr double caretInterval = .55;
// Scroll offsets this close to a line edge, in lines, count as on it, so float error never cuts a line.
constexpr float lineEdge = .01f;
}  // namespace

TextEditor::Impl::Impl(TextEditor& owner, tn::Document& document) :
    owner(owner), editing(document), view(editing), measurer(tvg::Text::gen())
{
    view.writeClipboard = [this](std::string_view text) {
        if (this->owner.writeClipboard) this->owner.writeClipboard(text);
    };
}

TextEditor::TextEditor(tn::Document& document) :
    impl(std::make_unique<Impl>(*this, document)) {}

TextEditor::~TextEditor() = default;

void TextEditor::font(FontId font, FontId numberFont)
{
    impl->preferredFont = font;
    impl->preferredNumberFont = numberFont;
    impl->restyle();
}

void TextEditor::Impl::invalidate()
{
    dirty = true;
    restartCaret = true;
}

void TextEditor::Impl::restyle()
{
    ++textStyleRevision;
    layoutDirty = true;
    invalidate();
}

void TextEditor::Impl::resize(float w, float h)
{
    w = std::max(1.0f, w);
    h = std::max(1.0f, h);
    if (width == w && height == h) return;
    width = w;
    height = h;
    relayout();
    clampScroll();
    invalidate();
}

void TextEditor::theme(Theme value)
{
    impl->themeTokens = std::move(value);
    impl->restyle();
}

void TextEditor::options(TextEditorOptions value)
{
    if (!std::isfinite(value.fontSize) || value.fontSize <= 0 || value.fontSize > TextEditorOptions::maxFontSize) value.fontSize = impl->configuration.fontSize;
    value.padding = std::clamp(value.padding, 0.0f, 64.0f);
    impl->configuration = value;
    impl->editing.readOnly(impl->configuration.readOnly);
    impl->restyle();
}

void TextEditor::Impl::focus(bool value)
{
    if (hasFocus == value) return;
    hasFocus = value;
    if (!value) {
        owner.endUndoGroup();
        dragging = false;
        horizontalGrab.reset();
        view.cancelPointer();
        preedit.clear();
    }
    invalidate();
}

Rect TextEditor::Impl::textArea() const
{
    float available = std::max(1.0f, width - gutter - configuration.padding * 2 - 8);
    float bottom = widest > available ? scrollbarHeight : 0;
    return {gutter + configuration.padding, 0, available, std::max(1.0f, height - bottom)};
}

float TextEditor::Impl::maxHorizontalScroll() const
{
    auto area = textArea();
    return widest > area.w ? widest + configuration.fontSize - area.w : 0;
}

Rect TextEditor::Impl::horizontalTrack() const
{
    auto area = textArea();
    if (maxHorizontalScroll() == 0) return {};
    float h = std::min(height, scrollbarHeight);
    return {area.x, height - h, area.w, h};
}

Rect TextEditor::Impl::horizontalThumb() const
{
    auto track = horizontalTrack();
    if (track.w == 0) return {};
    return scrollbarThumb(track, textArea().w, widest + configuration.fontSize, horizontal, Axis::Horizontal);
}

float TextEditor::Impl::maxVerticalScroll() const
{
    // The last page starts on a line edge too, so it may end less than a line below the text;
    // a viewport shorter than a line still shows the last line.
    auto total = contentHeight(), overflow = total - textArea().h;
    if (overflow <= 0) return 0;
    if (configuration.markdown) return overflow;
    return std::max(0.0f, std::min(std::ceil(overflow / lineHeight - lineEdge) * lineHeight, total - lineHeight));
}

void TextEditor::Impl::clampScroll()
{
    horizontal = std::clamp(horizontal, 0.0f, maxHorizontalScroll());
    vertical = std::clamp(vertical, 0.0f, maxVerticalScroll());
}

float TextEditor::Impl::contentHeight() const
{
    const auto& layout = view.layout();
    return layout.empty() ? 0 : layout.back().bounds.y + layout.back().bounds.h;
}

Rect TextEditor::Impl::logicalCaret() const
{
    return preedit.empty() ? view.caretRect() : preeditCaret;
}

void TextEditor::Impl::ensureCaret()
{
    relayout();
    auto area = textArea();
    auto caret = logicalCaret();
    if (caret.x < horizontal) horizontal = caret.x;
    if (caret.x + 3 > horizontal + area.w) horizontal = caret.x + 3 - area.w;
    if (caret.y < vertical) vertical = caret.y;
    if (caret.y + caret.h > vertical + area.h) vertical = caret.y + caret.h - area.h;
    clampScroll();
}

void TextEditor::Impl::edited(bool changed, bool revealCaret)
{
    view.resetPreferredX();
    preedit.clear();
    if (revealCaret) ensureCaret();
    // Pointer selection follows the pointer; an offscreen word end must not move
    // the viewport and change the word under the next mouse event.
    else paintedSelection = editing.selection();
    invalidate();
    if (changed && owner.onChange) owner.onChange();
}

void TextEditor::Impl::pointerDown(float x, float y, bool extend, int clicks)
{
    relayout();
    horizontalGrab.reset();
    auto track = horizontalTrack();
    if (track.contains(x, y)) {
        dragging = false;
        view.cancelPointer();
        auto thumb = horizontalThumb();
        horizontalGrab = thumb.contains(x, y) ? x - thumb.x : thumb.w / 2;
        pointerMove(x, y);
        invalidate();
        return;
    }
    if (!preedit.empty()) {
        preedit.clear();
        relayout();
    }
    auto area = textArea();
    view.pointerDown(x - area.x + horizontal, y - area.y + vertical, extend, clicks);
    dragging = true;
    edited(false, false);
}

void TextEditor::Impl::pointerMove(float x, float y)
{
    if (horizontalGrab) {
        relayout();
        auto track = horizontalTrack(), thumb = horizontalThumb();
        if (track.w > thumb.w) owner.scroll((x - track.x - *horizontalGrab) * maxHorizontalScroll() / (track.w - thumb.w), vertical);
        return;
    }
    if (!dragging) return;
    auto area = textArea();
    if (x < area.x) horizontal -= configuration.fontSize;
    if (x > area.x + area.w) horizontal += configuration.fontSize;
    if (y < area.y) vertical -= lineHeight;
    if (y > area.y + area.h) vertical += lineHeight;
    clampScroll();
    view.pointerMove(x - area.x + horizontal, y - area.y + vertical);
    edited(false, false);
}

void TextEditor::Impl::pointerUp()
{
    dragging = false;
    bool scrolling = horizontalGrab.has_value();
    horizontalGrab.reset();
    view.pointerUp();
    if (scrolling) invalidate();
}
void TextEditor::Impl::cancelPointer()
{
    pointerUp();
    view.cancelPointer();
}

void TextEditor::Impl::wheel(float y, float x)
{
    if (!std::isfinite(y) || !std::isfinite(x)) return;
    relayout();
    if (configuration.markdown) {
        // Use the body line height so headings do not change wheel sensitivity.
        vertical -= y * lineHeight;
        horizontal -= x * configuration.fontSize * 3;
        wheelCarry = 0;
        clampScroll();
        invalidate();
        return;
    }
    // One line a tick, counted from a line edge so the top line stays whole. Trackpads and
    // high-resolution wheels send fractions, which carry over until they make a line; turning
    // back drops what was carried the other way.
    float lines = -y;
    if (lines * wheelCarry < 0) wheelCarry = 0;
    wheelCarry += lines;
    float steps = std::trunc(wheelCarry);
    wheelCarry -= steps;
    if (steps != 0) {
        float top = vertical / lineHeight, edge = std::round(top);
        if (std::abs(top - edge) < lineEdge) top = edge;
        vertical = (steps > 0 ? std::floor(top + steps) : std::ceil(top + steps)) * lineHeight;
    }
    horizontal -= x * configuration.fontSize * 3;
    clampScroll();
    invalidate();
}

void TextEditor::scroll(float x, float y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    impl->relayout();
    impl->horizontal = x;
    impl->vertical = y;
    impl->clampScroll();
    // Selection restoration must not scroll the saved viewport back to the caret on the next render().
    impl->paintedSelection = impl->editing.selection();
    impl->invalidate();
}

void TextEditor::Impl::textInput(std::string_view text)
{
    if (owner.wantsTextInput()) edited(editing.insert(text));
}

void TextEditor::select(size_t anchor, size_t caret)
{
    impl->editing.select(anchor, caret);
    impl->edited(false);
}

void TextEditor::composition(std::string_view text, size_t cursor)
{
    if (!wantsTextInput()) return;
    impl->preedit = text;
    impl->preeditCursor = std::min(cursor, impl->preedit.size());
    while (impl->preeditCursor && impl->preeditCursor < impl->preedit.size() && (static_cast<unsigned char>(impl->preedit[impl->preeditCursor]) & 0xc0) == 0x80)
        --impl->preeditCursor;
    impl->invalidate();
}

bool TextEditor::Impl::key(Key key, Modifiers mods)
{
    if (!hasFocus) return false;
    if (!preedit.empty() && key != Key::Escape) return false;
    relayout();
    if (mods.primary) {
        if (key == Key::X) {
            view.key(key, mods);
            if (!configuration.readOnly && owner.writeClipboard) edited(editing.insert(""));
            return true;
        }
        if (key == Key::V) {
            if (!configuration.readOnly && owner.readClipboard) textInput(owner.readClipboard());
            return true;
        }
        if (key == Key::Z || key == Key::Y) {
            if (key == Key::Y || mods.shift) owner.redo();
            else owner.undo();
            return true;
        }
    }
    if (view.key(key, mods, textArea().h)) {
        if (mods.primary && key == Key::C) return true;
        bool verticalMove = key == Key::Up || key == Key::Down || key == Key::PageUp || key == Key::PageDown;
        if (!verticalMove) view.resetPreferredX();
        preedit.clear();
        ensureCaret();
        invalidate();
        return true;
    }
    if (key == Key::Escape) {
        preedit.clear();
        invalidate();
        return true;
    }
    if (configuration.readOnly) return false;
    if (key == Key::Backspace) edited(editing.backspace(mods.word));
    else if (key == Key::Delete) edited(editing.deleteForward(mods.word));
    else if (key == Key::Enter) textInput("\n");
    else if (key == Key::Tab) textInput("\t");
    else return false;
    return true;
}

Rect TextEditor::caretRect()
{
    impl->relayout();
    auto area = impl->textArea();
    auto caret = impl->logicalCaret();
    caret.x += area.x - impl->horizontal;
    caret.y += area.y - impl->vertical;
    return caret;
}

Rect TextEditor::textViewport()
{
    impl->relayout();
    return impl->textArea();
}

void TextEditor::Impl::handle(Context& host, std::string_view id, Rect bounds, const Event& event)
{
    float x = event.x - bounds.x, y = event.y - bounds.y;
    switch (event.type) {
        case EventType::Press:
            // The scrollbar is a separate control that does not take focus by itself.
            host.focus(id);
            focus(true);
            pointerDown(x, y, event.modifiers.shift, event.clicks);
            break;
        case EventType::Drag: pointerMove(x, y); break;
        case EventType::Release: pointerUp(); break;
        case EventType::Cancel: cancelPointer(); break;
        case EventType::Scroll:
            relayout();
            event.handled = (event.y > 0 && vertical > 0) || (event.y < 0 && vertical < maxVerticalScroll()) || (event.x > 0 && horizontal > 0) || (event.x < 0 && horizontal < maxHorizontalScroll());
            if (event.handled) wheel(event.y, event.x);
            break;
        case EventType::Key:
            focus(true);
            key(event.key, event.modifiers);
            break;
        case EventType::Text:
            focus(true);
            textInput(event.text);
            break;
        default: break;
    }
}

void TextEditor::render(Context& host, std::string_view id, Rect bounds)
{
    impl->resize(bounds.w, bounds.h);
    impl->relayout();
    // The handler outlives this call; the host keeps it until its next frame.
    auto handler = [state = impl.get(), &host, id = std::string(id), bounds](const Event& event) {
        state->handle(host, id, bounds, event);
    };
    auto control = host.control(id, bounds, handler, true, ControlFlag::Focusable | ControlFlag::Scrollable | ControlFlag::Capture | ControlFlag::TabInput, Cursor::Text);
    auto track = impl->horizontalTrack();
    if (track.w > 0) host.control(std::string(id) + "/scroll", {bounds.x + track.x, bounds.y + track.y, track.w, track.h}, handler, true, ControlFlag::Capture);
    impl->focus(control.focused);
    auto scene = impl->context.scene();
    transform(scene, {1, 0, bounds.x, 0, 1, bounds.y, 0, 0, 1});
    // Queue the scene before rebuilding it, so damage covers its previous content.
    host.add(scene);
    impl->update(host.time());
    if (!impl->nextBlink) return;
    auto caret = caretRect();
    caret.x += bounds.x;
    caret.y += bounds.y;
    auto visible = caret.intersect(bounds).intersect(host.clip());
    if (visible.w > 0 && visible.h > 0) host.requestFrameAt(*impl->nextBlink);
}

bool TextEditor::Impl::update(double now)
{
    seconds = std::max(seconds, now);
    bool restyled = layoutDirty || preedit != measuredPreedit || preeditCursor != measuredCursor;
    relayout();
    auto current = editing.selection();
    if (restyled || current.anchor != paintedSelection.anchor || current.caret != paintedSelection.caret) {
        ensureCaret();
        dirty = true;
        restartCaret = true;
    }
    if (paintedRevision != editing.document().revision()) restartCaret = true;
    if (restartCaret) {
        caretEpoch = seconds;
        restartCaret = false;
    }
    auto phase = std::floor((seconds - caretEpoch + 1e-9) / caretInterval);
    bool caretVisible = hasFocus && std::fmod(phase, 2) == 0;
    nextBlink.reset();
    auto visibleCaret = owner.caretRect().intersect(textArea());
    if (hasFocus && visibleCaret.w > 0 && visibleCaret.h > 0) nextBlink = caretEpoch + (phase + 1) * caretInterval;
    if (caretVisible != previousCaretVisible) dirty = true;
    if (!dirty) return false;
    previousCaretVisible = caretVisible;
    context.begin(themeTokens, {0, 0, width, height}, seconds);
    context.rect({0, 0, width, height}, themeTokens.background);
    auto area = textArea();
    auto selection = editing.selection();
    const auto& geometry = view.layout();
    auto firstRow = std::lower_bound(geometry.begin(), geometry.end(), vertical, [](const auto& row, float y) { return row.bounds.y + row.bounds.h <= y; });
    auto endRow = std::lower_bound(firstRow, geometry.end(), vertical + area.h, [](const auto& row, float y) { return row.bounds.y < y; });
    size_t first = size_t(firstRow - geometry.begin());
    size_t end = size_t(endRow - geometry.begin());
    for (auto it = linePaints.begin(); it != linePaints.end();) {
        if (it->first < first || it->first >= end) it = linePaints.erase(it);
        else ++it;
    }
    for (size_t row = first; row < end; ++row)
        prepareLine(row);
    context.clip({0, area.y, width, area.h}, [&] {
        if (configuration.lineNumbers)
            for (size_t row = first; row < end; ++row) {
                auto& line = *linePaints.at(row);
                transform(line.number, {1, 0, gutter - configuration.padding - line.numberWidth, 0, 1, area.y + geometry[row].bounds.y - vertical, 0, 0, 1});
                context.add(line.number);
            }
    });
    context.clip(area, [&] {
        auto caret = owner.caretRect();
        if (hasFocus) context.rect({area.x, caret.y, area.w, caret.h}, themeTokens.surface);
        if (configuration.markdown)
            for (size_t row = first; row < end; ++row)
                for (const auto& run : rowRuns[row])
                    if (run.format.code) context.rect({area.x + run.left - horizontal, area.y + geometry[row].bounds.y - vertical, run.width, geometry[row].bounds.h}, markdownColor(&Theme::markdownCodeBackground), 2);
        if (preedit.empty()) paintDecorations(first, end, false);
        if (!preedit.empty()) {
            for (auto rect : preeditRects) {
                rect.x += area.x - horizontal;
                rect.y += area.y - vertical;
                context.rect(rect, themeTokens.raised);
            }
        } else if (!selection.empty())
            for (size_t row = first; row < end; ++row) {
                auto rect = rangeRect(geometry[row], selection.start(), selection.end());
                if (rect.w > 0 && rect.h > 0) context.rect(rect, themeTokens.selection);
            }
        if (preedit.empty()) paintDecorations(first, end, true);
        for (size_t row = first; row < end; ++row) {
            auto& line = *linePaints.at(row);
            if (configuration.markdown) {
                for (size_t i = 0; i < rowRuns[row].size(); ++i) {
                    const auto& run = rowRuns[row][i];
                    auto text = i ? line.runs[i - 1]->text : line.body;
                    float x = area.x + run.left - horizontal, y = area.y + geometry[row].bounds.y - vertical;
                    transform(text, {1, 0, x + run.leading, 0, 1, y + run.top, 0, 0, 1});
                    context.add(text);
                    auto color = markdownColor(run.format.color);
                    if (run.format.strike) context.line(x, y + geometry[row].bounds.h * 0.55f, x + run.width, y + geometry[row].bounds.h * 0.55f, color);
                    if (run.format.link) context.line(x, y + geometry[row].bounds.h * 0.85f, x + run.width, y + geometry[row].bounds.h * 0.85f, color);
                }
            } else {
                auto text = line.body;
                transform(text, {1, 0, area.x + line.bodyLeft - horizontal, 0, 1, area.y + geometry[row].bounds.y + documentTop - vertical, 0, 0, 1});
                context.add(text);
            }
        }
        if (!preedit.empty()) {
            for (auto rect : preeditRects) {
                float x = area.x + rect.x - horizontal, y = area.y + rect.y + rect.h - 2 - vertical;
                context.line(x, y, x + rect.w, y, themeTokens.accent);
            }
        }
        if (caretVisible) {
            auto color = themeTokens.accent;
            if (input.caret == CaretStyle::Block) color.a = std::min<uint8_t>(color.a, 100);
            context.rect(paintCaret(), color);
        }
    });
    float total = contentHeight(), range = maxVerticalScroll();
    if (range > 0) {
        float thumb = std::max(18.0f, area.h * area.h / total);
        float y = area.y + (area.h - thumb) * vertical / range;
        context.rect({width - 6, y, 3, thumb}, themeTokens.border, 1.5f);
    }
    auto track = horizontalTrack();
    if (track.w > 0) {
        context.rect(track, themeTokens.surface);
        auto thumb = horizontalThumb();
        float thickness = std::min(4.0f, thumb.h);
        context.rect({thumb.x, thumb.y + (thumb.h - thickness) / 2, thumb.w, thickness}, horizontalGrab ? themeTokens.muted : themeTokens.border, thickness / 2);
    }
    context.end();
    paintedSelection = selection;
    paintedRevision = editing.document().revision();
    dirty = false;
    return true;
}

}  // namespace ui
