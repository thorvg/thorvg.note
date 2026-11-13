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
    bool orderedOffsets = false, orderedRows = false;
    double origin = 0;
    std::function<void(size_t)> measure;
    const TextLine& line(size_t index) const
    {
        if (measure && lines[index].offsets.empty()) measure(index);
        return lines[index];
    }

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

void TextView::measure(std::function<void(size_t)> line)
{
    impl->measure = std::move(line);
}

void TextView::discard(size_t index)
{
    if (index >= impl->lines.size()) return;
    auto& line = impl->lines[index];
    auto start = line.start();
    line.length = line.end() - start;
    line.base = start;
    std::vector<size_t>{}.swap(line.offsets);
    std::vector<float>{}.swap(line.x);
}

void TextView::origin(double y)
{
    impl->origin = y;
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

bool TextView::layout(size_t first, size_t count, std::vector<TextLine> value, ptrdiff_t offset, double y)
{
    if (first > impl->lines.size() || count > impl->lines.size() - first || !std::isfinite(y)) return false;
    for (const auto& line : value) {
        const auto& r = line.bounds;
        if ((!std::isnan(line.top) && !std::isfinite(line.top)) || line.offsets.size() != line.x.size() || !std::is_sorted(line.offsets.begin(), line.offsets.end()) || !std::is_sorted(line.x.begin(), line.x.end()) || !std::all_of(line.x.begin(), line.x.end(), [](float x) { return std::isfinite(x); }) || !std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) || !std::isfinite(r.h) || r.w < 0 || r.h < 0 || !std::isfinite(line.breakWidth) || line.breakWidth < 0) return false;
    }
    bool ordered = impl->orderedOffsets || (first == 0 && count == impl->lines.size());
    bool orderedRows = impl->orderedRows || (first == 0 && count == impl->lines.size());
    auto inserted = value.size();
    auto shift = [&](TextLine& line) {
        if (offset) line.base += offset;
        if (y) {
            if (!std::isnan(line.top)) {
                line.top += y;
                line.bounds.y = float(line.top);
            } else line.bounds.y += y;
        }
    };
    if (count == inserted) {
        if (offset || y)
            for (auto it = impl->lines.begin() + first + count; it != impl->lines.end(); ++it)
                shift(*it);
        std::move(value.begin(), value.end(), impl->lines.begin() + first);
    } else if (first == 0 && count == impl->lines.size()) {
        impl->lines = std::move(value);
    } else {
        auto oldSize = impl->lines.size();
        auto size = oldSize - count + inserted;
        if (inserted > count) {
            if (size > impl->lines.capacity() && size > 65536)
                impl->lines.reserve(size + std::min(size / 8, impl->lines.max_size() - size));
            impl->lines.resize(size);
            for (size_t row = oldSize; row > first + count; --row) {
                auto& line = impl->lines[row - 1 + inserted - count];
                line = std::move(impl->lines[row - 1]);
                shift(line);
            }
        } else {
            for (size_t row = first + count; row < oldSize; ++row) {
                auto& line = impl->lines[row - count + inserted];
                line = std::move(impl->lines[row]);
                shift(line);
            }
            impl->lines.resize(size);
        }
        std::move(value.begin(), value.end(), impl->lines.begin() + first);
    }
    if (ordered) {
        auto begin = impl->lines.begin() + (first ? first - 1 : 0);
        auto end = impl->lines.begin() + std::min(impl->lines.size(), first + inserted + 1);
        ordered = std::adjacent_find(begin, end, [this](const auto& left, const auto& right) { return (!impl->measure && (left.offsets.empty() || right.offsets.empty())) || left.end() > right.start(); }) == end;
    }
    if (orderedRows) {
        auto begin = impl->lines.begin() + (first ? first - 1 : 0);
        auto end = impl->lines.begin() + std::min(impl->lines.size(), first + inserted + 1);
        orderedRows = std::adjacent_find(begin, end, [](const auto& left, const auto& right) { return left.y() >= right.y() || left.y() + left.bounds.h > right.y(); }) == end;
    }
    impl->orderedRows = orderedRows;
    impl->orderedOffsets = ordered;
    return true;
}

size_t TextView::Impl::lineAt(size_t offset) const
{
    if (orderedOffsets) {
        auto line = std::upper_bound(lines.begin(), lines.end(), offset, [](size_t value, const auto& line) { return value < line.start(); });
        return line == lines.begin() ? 0 : static_cast<size_t>(line - lines.begin() - 1);
    }
    size_t best = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].offsets.empty() && !measure) continue;
        if (lines[i].start() > offset) break;
        best = i;
        if (offset < lines[i].end()) break;
    }
    return best;
}

size_t TextView::Impl::hitLine(float x, float y) const
{
    size_t best = 0, begin = 0, end = lines.size();
    auto documentY = double(y) + origin;
    if (orderedRows) {
        auto at = std::upper_bound(lines.begin(), lines.end(), documentY, [](double value, const auto& line) { return value < line.y(); });
        begin = at == lines.begin() ? 0 : size_t(at - lines.begin() - 1);
        end = std::min(lines.size(), begin + 2);
    }
    double vertical = std::numeric_limits<double>::infinity();
    float horizontal = std::numeric_limits<float>::infinity();
    for (size_t i = begin; i < end; ++i) {
        const auto& r = lines[i].bounds;
        auto top = lines[i].y();
        double dy = std::max({top - documentY, documentY - top - r.h, 0.0});
        if (documentY >= top + r.h && dy == 0) dy = 0.0001;
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
    return lines.empty() ? 0 : line(hitLine(x, y)).hit(x);
}

std::optional<size_t> TextView::Impl::characterAt(float x, float y) const
{
    if (lines.empty()) return {};
    const auto& line = this->line(hitLine(x, y));
    if (line.offsets.size() < 2 || line.x.size() != line.offsets.size() || x < line.x.front() || x >= line.x.back()) return {};
    // Use the glyph under the pointer, even when its nearest caret is after it.
    return line.base + line.offsets[size_t(std::upper_bound(line.x.begin(), line.x.end(), x) - line.x.begin() - 1)];
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
    const auto& line = impl->line(impl->lineAt(offset));
    return {line.position(offset), float(line.y() - impl->origin), 1.5f, line.bounds.h};
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
    auto first = impl->lines.begin(), last = impl->lines.end();
    if (viewport && impl->orderedRows) {
        auto top = double(viewport->y) + impl->origin;
        first = std::lower_bound(first, last, top, [](const auto& line, double y) { return line.y() + line.bounds.h <= y; });
        last = std::lower_bound(first, last, top + viewport->h, [](const auto& line, double y) { return line.y() < y; });
    }
    for (auto it = first; it != last; ++it) {
        if (it->start() > end || it->end() < start) continue;
        auto y = float(it->y() - impl->origin);
        if (viewport && (y + it->bounds.h <= viewport->y || y >= viewport->y + viewport->h)) continue;
        const auto& line = impl->line(size_t(it - impl->lines.begin()));
        auto rect = line.rangeRect(start, end);
        rect.y = y;
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
            const auto& line = impl->line(impl->hitLine(x, y));
            wholeLine = line.x.size() > 1 && x >= line.x.back() && double(y) + impl->origin >= line.y() && double(y) + impl->origin < line.y() + line.bounds.h;
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
            auto offset = key == Key::Home ? line.start() : line.end();
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
        const auto& line = impl->line(current);
        if (!impl->preferredX) impl->preferredX = line.position(selection.caret);
        bool up = key == Key::Up || key == Key::PageUp;
        auto height = std::max(1.0f, line.bounds.h);
        float distance = key == Key::PageUp || key == Key::PageDown ? std::max(1.0f, std::floor(pageHeight / height)) * height : height;
        double y = line.y() + (up ? -distance : distance);
        if (key == Key::Up) target = current ? current - 1 : 0;
        else if (key == Key::Down) target = std::min(current + 1, impl->lines.size() - 1);
        else if (up)
            while (target && impl->lines[target].y() > y)
                --target;
        else
            while (target + 1 < impl->lines.size() && impl->lines[target].y() < y)
                ++target;
        auto offset = impl->line(target).hit(*impl->preferredX);
        impl->selecting.select(mods.shift ? selection.anchor : offset, offset);
        return true;
    }
    return false;
}

}  // namespace ui
