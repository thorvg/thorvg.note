#include <ui/ui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace ui
{
namespace
{
bool space(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}
bool sentenceMargin(std::string_view ch)
{
    return (ch.size() == 1 && space(ch.front())) || ch == "-" || ch == "–" || ch == "—" || ch == "－" || ch == "*" || ch == "+" || ch == "•";
}
enum class SelectionUnit
{
    Word,
    Sentence,
    Line,
    Paragraph,
    Document
};

// The stage reached by repeated double-clicks at one position.
struct SelectionCycle
{
    uint64_t revision;
    size_t offset;
    float x, y;
    SelectionUnit unit;
    tn::Selection selection;
};
}  // namespace

struct TextView::Impl
{
    tn::EditSession& selecting;
    std::vector<TextLine> lines;
    std::optional<float> preferredX;
    std::optional<tn::Selection> dragSelection;
    SelectionUnit dragUnit = SelectionUnit::Word;
    std::optional<SelectionCycle> selectionCycle;
    bool dragging = false;

    explicit Impl(tn::EditSession& session) :
        selecting(session) {}

    size_t lineAt(size_t offset) const;
    size_t hitLine(float x, float y) const;
    size_t hit(float x, float y) const;
    std::optional<size_t> characterAt(float x, float y) const;
    tn::Selection wordAt(float x, float y) const;
    tn::Selection sentenceAt(size_t offset) const;
    tn::Selection paragraphAt(size_t offset) const;
    tn::Selection selectionAt(float x, float y, SelectionUnit unit) const;
    bool continuesSelectionCycle(float x, float y) const;
};

TextView::TextView(tn::EditSession& session) :
    impl(std::make_unique<Impl>(session)) {}
TextView::~TextView() = default;

bool TextView::layout(std::vector<TextLine> value)
{
    return layout(0, impl->lines.size(), std::move(value), 0, 0);
}

const std::vector<TextLine>& TextView::layout() const
{
    return impl->lines;
}

size_t TextView::hit(float x, float y) const
{
    return impl->hit(x, y);
}

void TextView::pointerUp()
{
    impl->dragging = false;
    impl->dragSelection.reset();
}

void TextView::cancelPointer()
{
    pointerUp();
    impl->selectionCycle.reset();
}

void TextView::resetPreferredX()
{
    impl->preferredX.reset();
}

bool TextView::layout(size_t first, size_t count, std::vector<TextLine> value, ptrdiff_t offset, float y)
{
    if (first > impl->lines.size() || count > impl->lines.size() - first || !std::isfinite(y)) return false;
    for (const auto& line : value) {
        const auto& r = line.bounds;
        if (line.offsets.size() != line.x.size() || !std::is_sorted(line.offsets.begin(), line.offsets.end()) || !std::is_sorted(line.x.begin(), line.x.end()) || !std::all_of(line.x.begin(), line.x.end(), [](float x) { return std::isfinite(x); }) || !std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) || !std::isfinite(r.h) || r.w < 0 || r.h < 0 || !std::isfinite(line.breakWidth) || line.breakWidth < 0) return false;
    }
    for (auto it = impl->lines.begin() + first + count; it != impl->lines.end(); ++it) {
        for (auto& o : it->offsets)
            o = size_t(ptrdiff_t(o) + offset);
        it->bounds.y += y;
    }
    auto at = impl->lines.erase(impl->lines.begin() + first, impl->lines.begin() + first + count);
    impl->lines.insert(at, std::make_move_iterator(value.begin()), std::make_move_iterator(value.end()));
    return true;
}

size_t TextView::Impl::lineAt(size_t offset) const
{
    size_t best = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].offsets.empty()) continue;
        if (lines[i].offsets.front() > offset) break;
        best = i;
        if (offset < lines[i].offsets.back()) break;
    }
    return best;
}

size_t TextView::Impl::hitLine(float x, float y) const
{
    size_t best = 0;
    float vertical = std::numeric_limits<float>::infinity(), horizontal = vertical;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& r = lines[i].bounds;
        float dy = std::max({r.y - y, y - r.y - r.h, 0.0f});
        if (y >= r.y + r.h && dy == 0) dy = 0.0001f;
        float dx = std::max({r.x - x, x - r.x - r.w, 0.0f});
        if (dy < vertical || (dy == vertical && dx < horizontal)) {
            best = i;
            vertical = dy;
            horizontal = dx;
        }
    }
    return best;
}

size_t TextView::Impl::hit(float x, float y) const
{
    return lines.empty() ? 0 : lines[hitLine(x, y)].hit(x);
}

std::optional<size_t> TextView::Impl::characterAt(float x, float y) const
{
    if (lines.empty()) return {};
    const auto& line = lines[hitLine(x, y)];
    if (line.offsets.size() < 2 || line.x.size() != line.offsets.size() || x < line.x.front() || x >= line.x.back()) return {};
    // Use the glyph under the pointer, even when its nearest caret is after it.
    return line.offsets[size_t(std::upper_bound(line.x.begin(), line.x.end(), x) - line.x.begin() - 1)];
}

tn::Selection TextView::Impl::wordAt(float x, float y) const
{
    auto character = characterAt(x, y);
    auto offset = character.value_or(hit(x, y));
    if (!character) return {offset, offset};
    const auto& document = selecting.document();
    const auto& text = document.text();
    if (offset >= text.size() || text[offset] == '\n') return {offset, offset};
    bool whitespace = space(text[offset]);
    size_t begin = offset, end = document.next(offset);
    while (begin && text[document.previous(begin)] != '\n' && space(text[document.previous(begin)]) == whitespace)
        begin = document.previous(begin);
    while (end < text.size() && text[end] != '\n' && space(text[end]) == whitespace)
        end = document.next(end);
    return {begin, end};
}

tn::Selection TextView::Impl::paragraphAt(size_t offset) const
{
    const auto& document = selecting.document();
    const auto& text = document.text();
    auto blank = [&](size_t row) {
        auto begin = text.begin() + document.lineStart(row), end = text.begin() + document.lineEnd(row);
        return std::all_of(begin, end, space);
    };
    size_t first = document.lineAt(offset), last = first;
    if (!blank(first)) {
        while (first && !blank(first - 1))
            --first;
        while (last + 1 < document.lineCount() && !blank(last + 1))
            ++last;
    }
    return {document.lineStart(first), document.lineEnd(last)};
}

tn::Selection TextView::Impl::sentenceAt(size_t offset) const
{
    const auto& document = selecting.document();
    std::string_view text = document.text();
    auto row = document.lineAt(offset);
    auto begin = document.lineStart(row), end = document.lineEnd(row);
    while (begin < end) {
        auto next = document.next(begin);
        if (!sentenceMargin(text.substr(begin, next - begin))) break;
        begin = next;
    }
    while (end > begin) {
        auto previous = document.previous(end);
        if (!sentenceMargin(text.substr(previous, end - previous))) break;
        end = previous;
    }
    return {begin, end};
}

tn::Selection TextView::Impl::selectionAt(float x, float y, SelectionUnit unit) const
{
    auto offset = characterAt(x, y).value_or(hit(x, y));
    switch (unit) {
        case SelectionUnit::Word: return wordAt(x, y);
        case SelectionUnit::Sentence: return sentenceAt(offset);
        case SelectionUnit::Line: {
            const auto& document = selecting.document();
            auto row = document.lineAt(offset);
            return {document.lineStart(row), document.lineEnd(row)};
        }
        case SelectionUnit::Paragraph: return paragraphAt(offset);
        case SelectionUnit::Document: return {0, selecting.document().text().size()};
    }
    return {offset, offset};
}

bool TextView::Impl::continuesSelectionCycle(float x, float y) const
{
    if (!selectionCycle) return false;
    auto current = selecting.selection();
    return selectionCycle->revision == selecting.document().revision() && selectionCycle->offset == characterAt(x, y).value_or(hit(x, y)) && std::abs(x - selectionCycle->x) <= 4 && std::abs(y - selectionCycle->y) <= 4 && current.anchor == selectionCycle->selection.anchor && current.caret == selectionCycle->selection.caret;
}

Rect TextView::caretRect() const
{
    if (impl->lines.empty()) return {};
    auto offset = impl->selecting.selection().caret;
    const auto& line = impl->lines[impl->lineAt(offset)];
    return {line.position(offset), line.bounds.y, 1.5f, line.bounds.h};
}

std::vector<Rect> TextView::selectionRects(std::optional<Rect> viewport) const
{
    auto selection = impl->selecting.selection();
    if (selection.empty()) return {};
    return rangeRects(selection.start(), selection.end(), viewport);
}

std::vector<Rect> TextView::rangeRects(size_t start, size_t end, std::optional<Rect> viewport) const
{
    std::vector<Rect> result;
    for (const auto& line : impl->lines) {
        if (viewport && (line.bounds.y + line.bounds.h <= viewport->y || line.bounds.y >= viewport->y + viewport->h)) continue;
        auto rect = line.rangeRect(start, end);
        if (viewport) rect = rect.intersect(*viewport);
        if (rect.w > 0 && rect.h > 0) result.push_back(rect);
    }
    return result;
}

void TextView::pointerDown(float x, float y, bool extend, int clicks)
{
    auto offset = hit(x, y);
    if (extend || !impl->continuesSelectionCycle(x, y)) impl->selectionCycle.reset();
    impl->dragSelection.reset();
    if (clicks >= 2 && clicks % 2 == 0) {
        bool wholeLine = false;
        if (!impl->lines.empty()) {
            const auto& line = impl->lines[impl->hitLine(x, y)];
            wholeLine = line.x.size() > 1 && x >= line.x.back() && y >= line.bounds.y && y < line.bounds.y + line.bounds.h;
        }
        impl->dragUnit = wholeLine ? SelectionUnit::Line : SelectionUnit::Word;
        if (impl->selectionCycle) {
            auto previous = impl->selectionCycle->unit;
            if (previous == SelectionUnit::Word) impl->dragUnit = SelectionUnit::Sentence;
            else if (previous == SelectionUnit::Sentence || previous == SelectionUnit::Line) impl->dragUnit = SelectionUnit::Paragraph;
            else if (previous == SelectionUnit::Paragraph) impl->dragUnit = SelectionUnit::Document;
        }
        auto range = impl->selectionAt(x, y, impl->dragUnit);
        if (impl->dragUnit == SelectionUnit::Paragraph) {
            auto sentence = impl->selectionAt(x, y, wholeLine ? SelectionUnit::Line : SelectionUnit::Sentence);
            if (range.start() == sentence.start() && range.end() == sentence.end()) {
                impl->dragUnit = SelectionUnit::Document;
                range = impl->selectionAt(x, y, impl->dragUnit);
            }
        }
        auto anchor = extend ? impl->selecting.selection().anchor : range.start();
        impl->selecting.select(anchor, range.end() < anchor ? range.start() : range.end());
        impl->dragSelection = impl->selecting.selection();
        if (!extend && (impl->dragUnit != SelectionUnit::Word || !range.empty()))
            impl->selectionCycle = SelectionCycle{impl->selecting.document().revision(), impl->characterAt(x, y).value_or(offset), x, y, impl->dragUnit, impl->selecting.selection()};
    } else impl->selecting.select(extend ? impl->selecting.selection().anchor : offset, offset);
    // Keep the previous stage through the first press of the next double-click.
    if (impl->selectionCycle) impl->selectionCycle->selection = impl->selecting.selection();
    impl->dragging = true;
    impl->preferredX.reset();
}
void TextView::pointerMove(float x, float y)
{
    if (!impl->dragging) return;
    if (!impl->continuesSelectionCycle(x, y)) impl->selectionCycle.reset();
    if (impl->dragSelection) {
        auto range = impl->selectionAt(x, y, impl->dragUnit);
        if (range.start() < impl->dragSelection->start()) impl->selecting.select(impl->dragSelection->end(), range.start());
        else impl->selecting.select(impl->dragSelection->start(), std::max(impl->dragSelection->end(), range.end()));
    } else impl->selecting.select(impl->selecting.selection().anchor, hit(x, y));
    if (!impl->continuesSelectionCycle(x, y)) impl->selectionCycle.reset();
}

bool TextView::key(Key key, Modifiers mods, float pageHeight)
{
    impl->selectionCycle.reset();
    if (mods.primary && key == Key::A) {
        impl->selecting.selectAll();
        impl->preferredX.reset();
        return true;
    }
    if (mods.primary && (key == Key::C || key == Key::X)) {
        if (writeClipboard && !impl->selecting.selection().empty()) writeClipboard(impl->selecting.selectedText());
        return true;
    }
    if (key == Key::Left || key == Key::Right || key == Key::Home || key == Key::End) {
        if ((key == Key::Home || key == Key::End) && !mods.primary && !impl->lines.empty() && !impl->lines[impl->lineAt(impl->selecting.selection().caret)].offsets.empty()) {
            auto selection = impl->selecting.selection();
            const auto& line = impl->lines[impl->lineAt(selection.caret)];
            auto offset = key == Key::Home ? line.offsets.front() : line.offsets.back();
            impl->selecting.select(mods.shift ? selection.anchor : offset, offset);
        } else {
            auto move = key == Key::Left ? (mods.word ? tn::Move::WordLeft : tn::Move::Left) : key == Key::Right ? (mods.word ? tn::Move::WordRight : tn::Move::Right)
                : key == Key::Home                                                                               ? (mods.primary ? tn::Move::Start : tn::Move::LineStart)
                                                                                                                 : (mods.primary ? tn::Move::End : tn::Move::LineEnd);
            impl->selecting.move(move, mods.shift);
        }
        impl->preferredX.reset();
        return true;
    }
    if (key == Key::Up || key == Key::Down || key == Key::PageUp || key == Key::PageDown) {
        if (impl->lines.empty()) return true;
        auto selection = impl->selecting.selection();
        size_t current = impl->lineAt(selection.caret), target = current;
        const auto& line = impl->lines[current];
        if (!impl->preferredX) impl->preferredX = line.position(selection.caret);
        bool up = key == Key::Up || key == Key::PageUp;
        auto height = std::max(1.0f, line.bounds.h);
        float distance = key == Key::PageUp || key == Key::PageDown ? std::max(1.0f, std::floor(pageHeight / height)) * height : height;
        float y = line.bounds.y + (up ? -distance : distance);
        if (key == Key::Up) target = current ? current - 1 : 0;
        else if (key == Key::Down) target = std::min(current + 1, impl->lines.size() - 1);
        else if (up)
            while (target && impl->lines[target].bounds.y > y)
                --target;
        else
            while (target + 1 < impl->lines.size() && impl->lines[target].bounds.y < y)
                ++target;
        auto offset = impl->lines[target].hit(*impl->preferredX);
        impl->selecting.select(mods.shift ? selection.anchor : offset, offset);
        return true;
    }
    return false;
}

}  // namespace ui
