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
    view.measure([this](size_t row) { measureLine(row); });
    view.writeClipboard = [this](std::string_view) {
        this->owner.copy();
    };
}

TextEditor::TextEditor(tn::Document& document) :
    impl(std::make_unique<Impl>(*this, document)) {}

TextEditor::~TextEditor() = default;

void TextEditor::font(FontId font, FontId numberFont)
{
    if (impl->preferredFont == font && impl->preferredNumberFont == numberFont) return;
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
    viewportReady = true;
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
    bool layout = value.font != impl->themeTokens.font || value.boldFont != impl->themeTokens.boldFont;
    impl->themeTokens = std::move(value);
    if (layout) impl->restyle();
    else {
        ++impl->textStyleRevision;
        impl->textContentDirty = true;
        impl->invalidate();
    }
}

void TextEditor::options(TextEditorOptions value)
{
    if (!std::isfinite(value.fontSize) || value.fontSize <= 0 || value.fontSize > TextEditorOptions::maxFontSize) value.fontSize = impl->configuration.fontSize;
    value.padding = std::clamp(value.padding, 0.0f, 64.0f);
    impl->editing.readOnly(value.readOnly);
    const auto& current = impl->configuration;
    if (current.fontSize == value.fontSize && current.padding == value.padding && current.lineNumbers == value.lineNumbers && current.readOnly == value.readOnly && current.markdown == value.markdown) return;
    impl->configuration = value;
    impl->restyle();
}

void TextEditor::Impl::focus(bool value)
{
    if (hasFocus == value) return;
    hasFocus = value;
    if (!value) {
        owner.endUndoGroup();
        dragging = false;
        scrollAxis.reset();
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

Rect TextEditor::Impl::scrollTrack(Axis axis) const
{
    auto area = textArea();
    if (axis == Axis::Vertical) {
        if (maxVerticalScroll() <= 0) return {};
        float w = std::min(width, 8.0f);
        return {width - w, area.y, w, area.h};
    }
    // A row still laying out has no full width to scroll through yet.
    if (maxHorizontalScroll() == 0 || layingOut()) return {};
    float h = std::min(height, scrollbarHeight);
    return {area.x, height - h, area.w, h};
}

Rect TextEditor::Impl::scrollThumb(Axis axis, Rect track) const
{
    if (axis == Axis::Horizontal) return scrollbarThumb(track, track.w, widest + configuration.fontSize, horizontal, axis);
    auto range = maxVerticalScroll();
    if (range <= 0) return {};
    float h = std::min<double>(track.h, std::max(18.0, track.h * track.h / contentHeight()));
    return {track.x, track.y + float((track.h - h) * vertical / range), track.w, h};
}

double TextEditor::Impl::maxVerticalScroll() const
{
    // The last page starts on a line edge too, so it may end less than a line below the text;
    // a viewport shorter than a line still shows the last line.
    auto total = contentHeight(), overflow = total - textArea().h;
    if (overflow <= 0) return 0;
    if (configuration.markdown) return overflow;
    return std::max(0.0, std::min(std::ceil(overflow / lineHeight - lineEdge) * lineHeight, total - lineHeight));
}

void TextEditor::Impl::clampScroll()
{
    // Scrolling past the rows still laying out finishes them first.
    if (horizontal > maxHorizontalScroll() && layingOut()) continueLayout(true);
    horizontal = std::clamp(horizontal, 0.0f, maxHorizontalScroll());
    vertical = std::clamp(vertical, 0.0, maxVerticalScroll());
}

double TextEditor::Impl::contentHeight() const
{
    auto count = view.lineCount();
    if (!count) return 0;
    const auto& last = view.line(count - 1);
    return last.y() + last.bounds.h;
}

Rect TextEditor::Impl::logicalCaret() const
{
    if (preedit.empty()) return view.caretRect();
    auto rect = preeditCaret;
    rect.y += float(preeditOrigin - vertical);
    return rect;
}

void TextEditor::Impl::ensureCaret()
{
    relayout();
    measureTo(editing.selection().caret);
    view.origin(vertical);
    auto caret = logicalCaret();
    auto area = textArea();
    if (caret.x < horizontal) horizontal = caret.x;
    if (caret.x + 3 > horizontal + area.w) horizontal = caret.x + 3 - area.w;
    if (caret.y < 0) vertical += caret.y;
    if (caret.y + caret.h > area.h) vertical += caret.y + caret.h - area.h;
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
    scrollAxis.reset();
    for (auto axis : {Axis::Horizontal, Axis::Vertical}) {
        auto track = scrollTrack(axis);
        if (!track.contains(x, y)) continue;
        dragging = false;
        view.cancelPointer();
        scrollAxis = axis;
        auto thumb = scrollThumb(axis, track);
        bool onThumb = thumb.contains(x, y);
        if (axis == Axis::Vertical) grabOffset = onThumb ? y - thumb.y : thumb.h / 2;
        else grabOffset = onThumb ? x - thumb.x : thumb.w / 2;
        if (!onThumb) pointerMove(x, y);
        invalidate();
        return;
    }
    if (!preedit.empty()) {
        preedit.clear();
        relayout();
    }
    auto area = textArea();
    view.origin(vertical);
    view.pointerDown(x - area.x + horizontal, y - area.y, extend, clicks);
    snapWidgets(true, true);
    dragging = true;
    edited(false, false);
}

void TextEditor::Impl::pointerMove(float x, float y)
{
    if (scrollAxis) {
        relayout();
        auto track = scrollTrack(*scrollAxis), thumb = scrollThumb(*scrollAxis, track);
        if (*scrollAxis == Axis::Vertical) {
            if (track.h > thumb.h) owner.scroll(horizontal, (y - track.y - grabOffset) * maxVerticalScroll() / (track.h - thumb.h));
        } else if (track.w > thumb.w) owner.scroll((x - track.x - grabOffset) * maxHorizontalScroll() / (track.w - thumb.w), vertical);
        return;
    }
    if (!dragging) return;
    auto area = textArea();
    if (x < area.x) horizontal -= configuration.fontSize;
    if (x > area.x + area.w) horizontal += configuration.fontSize;
    if (y < area.y) vertical -= lineHeight;
    if (y > area.y + area.h) vertical += lineHeight;
    clampScroll();
    view.origin(vertical);
    view.pointerMove(x - area.x + horizontal, y - area.y);
    snapWidgets(true, true);
    edited(false, false);
}

void TextEditor::Impl::pointerUp()
{
    dragging = false;
    bool scrolling = scrollAxis.has_value();
    scrollAxis.reset();
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
    // Use the body line height so headings do not change wheel sensitivity.
    vertical -= y * lineHeight;
    horizontal += x * configuration.fontSize * 3;
    clampScroll();
    invalidate();
}

void TextEditor::scroll(float x, double y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    impl->relayout();
    impl->horizontal = x;
    impl->vertical = y;
    if (!impl->viewportReady) impl->pendingScroll = std::pair{x, y};
    impl->clampScroll();
    // Selection restoration must not scroll the saved viewport back to the caret on the next render().
    impl->paintedSelection = impl->editing.selection();
    impl->invalidate();
}

void TextEditor::Impl::textInput(std::string_view text)
{
    if (owner.wantsTextInput()) {
        if (composedRows) owner.composition("", 0);
        snapWidgets(true, true);
        edited(editing.insert(text));
    }
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
    // Restore document geometry before the committed edit changes source offsets.
    if (text.empty() && impl->composedRows) impl->relayout();
}

bool TextEditor::Impl::key(Key key, Modifiers mods)
{
    if (!hasFocus) return false;
    if (!preedit.empty() && key != Key::Escape) return false;
    if (widgetPopup && widgetPopup->suggestion && widgetPopup->event.revision == editing.document().revision() && !mods.primary && !mods.word) {
        if (key == Key::Escape) {
            owner.closeWidget();
            return true;
        }
        auto callback = widgetRules[widgetPopup->rule].rule.popupKey;
        auto event = widgetPopup->event;
        if (callback && callback(key, owner, event)) return true;
    }
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
    view.origin(vertical);
    if (view.key(key, mods, textArea().h)) {
        if (mods.primary && key == Key::C) return true;
        snapWidgets(key != Key::Left && key != Key::Up && key != Key::Home && key != Key::PageUp);
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
    if (key == Key::Backspace || key == Key::Delete) {
        const auto& document = editing.document();
        auto target = [&](tn::Selection range) {
            if (range.empty()) {
                if (key == Key::Backspace) range.anchor = mods.word ? document.wordPrevious(range.caret) : document.previous(range.caret);
                else range.caret = mods.word ? document.wordNext(range.caret) : document.next(range.caret);
            }
            return range;
        };
        const auto& selections = editing.selections();
        bool atomic = !widgetRules.empty() && std::any_of(selections.begin(), selections.end(), [&](auto range) {
            range = target(range);
            return widgetBoundary(range.start(), false) != range.start() || widgetBoundary(range.end(), true) != range.end();
        });
        if (atomic) {
            tn::EditBatch batch;
            batch.revision = document.revision();
            for (auto range : selections) {
                range = target(range);
                auto start = widgetBoundary(range.start(), false), end = widgetBoundary(range.end(), true);
                if (start == end) continue;
                if (!batch.edits.empty() && start <= batch.edits.back().end) batch.edits.back().end = std::max(end, batch.edits.back().end);
                else batch.edits.push_back({start, end, {}});
            }
            edited(editing.apply(batch));
        } else {
            edited(key == Key::Backspace ? editing.backspace(mods.word) : editing.deleteForward(mods.word));
        }
    } else if (key == Key::Enter) textInput("\n");
    else if (key == Key::Tab) textInput("\t");
    else return false;
    return true;
}

Rect TextEditor::caretRect()
{
    impl->relayout();
    impl->measureTo(impl->editing.selection().caret);
    auto area = impl->textArea();
    impl->view.origin(impl->vertical);
    auto caret = impl->logicalCaret();
    caret.x += area.x - impl->horizontal;
    caret.y += area.y;
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
            if (widgetPopup && widgetPopup->suggestion) owner.closeWidget();
            host.focus(id);
            focus(true);
            pointerDown(x, y, event.modifiers.shift, event.clicks);
            break;
        case EventType::Drag: pointerMove(x, y); break;
        case EventType::Release: pointerUp(); break;
        case EventType::Cancel: cancelPointer(); break;
        case EventType::Scroll:
            relayout();
            event.handled = (event.y > 0 && vertical > 0) || (event.y < 0 && vertical < maxVerticalScroll()) || (event.x < 0 && horizontal > 0) || (event.x > 0 && horizontal < maxHorizontalScroll());
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
    for (auto axis : {Axis::Horizontal, Axis::Vertical}) {
        auto track = impl->scrollTrack(axis);
        if (track.w > 0) host.control(std::string(id) + (axis == Axis::Horizontal ? "/scroll" : "/vscroll"), {bounds.x + track.x, bounds.y + track.y, track.w, track.h}, handler, true, ControlFlag::Capture);
    }
    impl->focus(control.focused);
    auto scene = impl->context.scene();
    transform(scene, {1, 0, bounds.x, 0, 1, bounds.y, 0, 0, 1});
    // Queue the scene before rebuilding it, so damage covers its previous content.
    host.add(scene);
    impl->update(host.time());
    impl->paintWidgets(host, id, bounds);
    if (impl->layingOut()) host.requestAnimationFrame();
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
    if (layingOut()) continueLayout();
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
    if (pendingScroll) {
        horizontal = pendingScroll->first;
        vertical = pendingScroll->second;
        clampScroll();
    }
    auto visible = view.visibleLines(vertical, vertical + area.h);
    auto first = visible.first, end = visible.second;
    prepareViewport(first, end);
    if (pendingScroll) {
        horizontal = pendingScroll->first;
        vertical = pendingScroll->second;
        pendingScroll.reset();
        clampScroll();
        area = textArea();
        visible = view.visibleLines(vertical, vertical + area.h);
        first = visible.first;
        end = visible.second;
        prepareViewport(first, end);
    }
    auto measuredArea = textArea();
    if (area.h != measuredArea.h) {
        area = measuredArea;
        visible = view.visibleLines(vertical, vertical + area.h);
        first = visible.first;
        end = visible.second;
        prepareViewport(first, end);
    }
    context.clip({0, area.y, width, area.h}, [&] {
        if (configuration.lineNumbers) {
            auto scene = numberContent.scene();
            context.add(scene);
            transform(scene, {1, 0, 0, 0, 1, area.y + float(renderOrigin - vertical), 0, 0, 1});
        }
    });
    context.clip(area, [&] {
        auto caret = owner.caretRect();
        if (hasFocus) context.rect({area.x, caret.y, area.w, caret.h}, themeTokens.surface);
        if (configuration.markdown) {
            auto scene = codeContent.scene();
            context.add(scene);
            transform(scene, {1, 0, area.x - horizontal, 0, 1, area.y + float(renderOrigin - vertical), 0, 0, 1});
        }
        if (preedit.empty()) paintDecorations(first, end, false);
        if (!preedit.empty()) {
            for (auto rect : preeditRects) {
                rect.x += area.x - horizontal;
                rect.y += area.y + float(preeditOrigin - vertical);
                context.rect(rect, themeTokens.raised);
            }
        } else if (!selection.empty())
            for (size_t row = first; row < end; ++row) {
                auto rect = rangeRect(view.line(row), selection.start(), selection.end());
                if (rect.w > 0 && rect.h > 0) context.rect(rect, themeTokens.selection);
            }
        if (preedit.empty()) paintDecorations(first, end, true);
        auto scene = textContent.scene();
        context.add(scene);
        transform(scene, {1, 0, area.x - horizontal, 0, 1, area.y + float(renderOrigin - vertical), 0, 0, 1});
        if (!preedit.empty()) {
            for (auto rect : preeditRects) {
                float x = area.x + rect.x - horizontal, y = area.y + rect.y + rect.h - 2 + float(preeditOrigin - vertical);
                context.line(x, y, x + rect.w, y, themeTokens.accent);
            }
        }
        if (caretVisible) {
            auto color = themeTokens.accent;
            if (input.caret == CaretStyle::Block) color.a = std::min<uint8_t>(color.a, 100);
            context.rect(paintCaret(), color);
        }
    });
    for (auto axis : {Axis::Vertical, Axis::Horizontal}) {
        auto track = scrollTrack(axis);
        if (!track.w) continue;
        auto thumb = scrollThumb(axis, track);
        auto color = scrollAxis == axis ? themeTokens.muted : themeTokens.border;
        if (axis == Axis::Vertical) {
            float thickness = std::min(3.0f, thumb.w);
            context.rect({thumb.x + std::floor((thumb.w - thickness) / 2), thumb.y, thickness, thumb.h}, color, thickness / 2);
        } else {
            context.rect(track, themeTokens.surface);
            float thickness = std::min(4.0f, thumb.h);
            context.rect({thumb.x, thumb.y + (thumb.h - thickness) / 2, thumb.w, thickness}, color, thickness / 2);
        }
    }
    context.end();
    paintedSelection = selection;
    paintedRevision = editing.document().revision();
    dirty = false;
    return true;
}

}  // namespace ui
