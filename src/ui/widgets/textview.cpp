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
    struct Chunk
    {
        size_t first = 0;
        std::vector<TextLine> lines;
        ptrdiff_t offset = 0;
        double y = 0;
        size_t floatRows = 0;
    };
    static constexpr size_t chunkRows = 1024;
    tn::EditSession& selecting;
    mutable std::vector<Chunk> chunks;
    size_t count = 0;
    mutable std::vector<TextLine> snapshot;
    mutable bool observed = false;
    std::optional<float> preferredX;
    std::optional<tn::Selection> dragSelection;
    SelectionUnit dragUnit = SelectionUnit::Word;
    std::optional<SelectionCycle> selectionCycle;
    bool dragging = false;
    bool orderedOffsets = false, orderedRows = false;
    double origin = 0;
    std::function<void(size_t)> measure;

    explicit Impl(tn::EditSession& session) :
        selecting(session) {}

    size_t chunkAt(size_t row) const
    {
        return size_t(std::upper_bound(chunks.begin(), chunks.end(), row, [](size_t value, const auto& chunk) { return value < chunk.first; }) - chunks.begin() - 1);
    }
    static void shift(TextLine& line, ptrdiff_t offset, double y)
    {
        line.base += offset;
        if (y) {
            if (!std::isnan(line.top)) {
                line.top += y;
                line.bounds.y = float(line.top);
            } else line.bounds.y += y;
        }
    }
    static void materialize(Chunk& chunk)
    {
        if (!chunk.offset && !chunk.y) return;
        for (auto& line : chunk.lines)
            shift(line, chunk.offset, chunk.y);
        chunk.offset = 0;
        chunk.y = 0;
    }
    static void shift(Chunk& chunk, ptrdiff_t offset, double y)
    {
        chunk.offset += offset;
        if (y && chunk.floatRows) {
            // Legacy float-only rows retain the rounding of each individual displacement.
            materialize(chunk);
            for (auto& line : chunk.lines)
                shift(line, 0, y);
        } else chunk.y += y;
    }
    TextLine& metadata(size_t index) const
    {
        auto& chunk = chunks[chunkAt(index)];
        materialize(chunk);
        return chunk.lines[index - chunk.first];
    }
    const TextLine& line(size_t index) const
    {
        if (measure && metadata(index).offsets.empty()) measure(index);
        // Measurement can replace or rebalance the chunk that held this row.
        return metadata(index);
    }
    size_t start(size_t index) const
    {
        const auto& chunk = chunks[chunkAt(index)];
        return chunk.lines[index - chunk.first].start() + chunk.offset;
    }
    double top(size_t index) const
    {
        const auto& chunk = chunks[chunkAt(index)];
        return chunk.lines[index - chunk.first].y() + chunk.y;
    }
    double bottom(size_t index) const
    {
        const auto& chunk = chunks[chunkAt(index)];
        const auto& line = chunk.lines[index - chunk.first];
        return line.y() + chunk.y + line.bounds.h;
    }
    void refreshSnapshot() const
    {
        if (!observed) return;
        snapshot.clear();
        if (count > snapshot.capacity()) snapshot.reserve(count + std::min(count / 8, snapshot.max_size() - count));
        for (auto& chunk : chunks) {
            materialize(chunk);
            snapshot.insert(snapshot.end(), chunk.lines.begin(), chunk.lines.end());
        }
    }
    void checkOrder(size_t first, size_t last, bool& offsets, bool& rows) const
    {
        if (first == last || (!offsets && !rows)) return;
        bool previous = false, hasOffsets = false;
        size_t end = 0;
        double y = 0, bottom = 0;
        for (size_t i = chunkAt(first); i < chunks.size() && chunks[i].first < last; ++i) {
            const auto& chunk = chunks[i];
            auto begin = std::max(first, chunk.first) - chunk.first;
            auto stop = std::min(last - chunk.first, chunk.lines.size());
            for (size_t j = begin; j < stop; ++j) {
                const auto& line = chunk.lines[j];
                auto top = line.y() + chunk.y;
                if (previous) {
                    if (offsets && ((!measure && (!hasOffsets || line.offsets.empty())) || end > line.start() + chunk.offset)) offsets = false;
                    if (rows && (y >= top || bottom > top)) rows = false;
                }
                hasOffsets = !line.offsets.empty();
                end = line.end() + chunk.offset;
                y = top;
                bottom = top + line.bounds.h;
                previous = true;
            }
        }
    }

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
    return layout(0, impl->count, std::move(value), 0, 0);
}

const std::vector<TextLine>& TextView::layout() const
{
    if (!impl->observed) {
        impl->observed = true;
        impl->refreshSnapshot();
    }
    return impl->snapshot;
}

size_t TextView::lineCount() const
{
    return impl->count;
}

const TextLine& TextView::line(size_t index) const
{
    return impl->metadata(index);
}

std::pair<size_t, size_t> TextView::visibleLines(double top, double bottom) const
{
    if (!impl->count || std::isnan(top) || std::isnan(bottom) || bottom <= top) return {0, 0};
    if (!impl->orderedRows) return {0, impl->count};
    size_t first = 0, last = impl->count;
    while (first < last) {
        auto middle = first + (last - first) / 2;
        if (impl->bottom(middle) <= top) first = middle + 1;
        else last = middle;
    }
    auto begin = first;
    last = impl->count;
    while (first < last) {
        auto middle = first + (last - first) / 2;
        if (impl->top(middle) < bottom) first = middle + 1;
        else last = middle;
    }
    return {begin, first};
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
    if (index >= impl->count) return;
    auto& line = impl->metadata(index);
    auto start = line.start();
    line.length = line.end() - start;
    line.base = start;
    std::vector<size_t>{}.swap(line.offsets);
    std::vector<float>{}.swap(line.x);
    impl->refreshSnapshot();
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
    if (first > impl->count || count > impl->count - first || !std::isfinite(y)) return false;
    for (const auto& line : value) {
        const auto& r = line.bounds;
        if ((!std::isnan(line.top) && !std::isfinite(line.top)) || line.offsets.size() != line.x.size() || !std::is_sorted(line.offsets.begin(), line.offsets.end()) || !std::is_sorted(line.x.begin(), line.x.end()) || !std::all_of(line.x.begin(), line.x.end(), [](float x) { return std::isfinite(x); }) || !std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) || !std::isfinite(r.h) || r.w < 0 || r.h < 0 || !std::isfinite(line.breakWidth) || line.breakWidth < 0) return false;
    }
    auto inserted = value.size();
    auto full = first == 0 && count == impl->count;
    bool ordered = impl->orderedOffsets || full;
    bool orderedRows = impl->orderedRows || full;
    if (full) std::vector<TextLine>{}.swap(impl->snapshot);
    if (full && inserted != impl->count) {
        impl->chunks.clear();
        auto chunks = (inserted + Impl::chunkRows - 1) / Impl::chunkRows;
        if (impl->chunks.capacity() > chunks + chunks / 8) std::vector<Impl::Chunk>{}.swap(impl->chunks);
        impl->chunks.reserve(chunks);
        for (size_t row = 0; row < inserted;) {
            auto& chunk = impl->chunks.emplace_back();
            chunk.first = row;
            auto stop = std::min(inserted, row + Impl::chunkRows);
            chunk.lines.reserve(stop - row);
            for (; row < stop; ++row) {
                chunk.floatRows += std::isnan(value[row].top);
                chunk.lines.push_back(std::move(value[row]));
            }
        }
    } else if (inserted == count) {
        for (size_t row = first, copied = 0; copied < inserted;) {
            auto& chunk = impl->chunks[impl->chunkAt(row)];
            auto at = row - chunk.first;
            auto length = std::min(inserted - copied, chunk.lines.size() - at);
            if (!at && length == chunk.lines.size()) {
                chunk.offset = 0;
                chunk.y = 0;
            } else Impl::materialize(chunk);
            auto floating = [](const auto& line) {
                return std::isnan(line.top);
            };
            chunk.floatRows -= size_t(std::count_if(chunk.lines.begin() + at, chunk.lines.begin() + at + length, floating));
            chunk.floatRows += size_t(std::count_if(value.begin() + copied, value.begin() + copied + length, floating));
            std::move(value.begin() + copied, value.begin() + copied + length, chunk.lines.begin() + at);
            row += length;
            copied += length;
        }
        if ((offset || y) && first + count < impl->count) {
            auto next = impl->chunkAt(first + count);
            auto& chunk = impl->chunks[next];
            auto at = first + count - chunk.first;
            if (at) {
                Impl::materialize(chunk);
                for (; at < chunk.lines.size(); ++at)
                    Impl::shift(chunk.lines[at], offset, y);
                ++next;
            }
            for (; next < impl->chunks.size(); ++next)
                Impl::shift(impl->chunks[next], offset, y);
        }
    } else {
        auto begin = first == impl->count ? impl->chunks.size() - 1 : impl->chunkAt(first);
        auto last = count ? impl->chunkAt(first + count - 1) : begin;
        auto end = std::min(last + 2, impl->chunks.size());
        Impl::materialize(impl->chunks[begin]);
        Impl::materialize(impl->chunks[last]);
        if (last + 1 < end) Impl::materialize(impl->chunks[last + 1]);
        auto prefix = first - impl->chunks[begin].first;
        auto suffix = first + count - impl->chunks[last].first;
        auto rows = prefix + inserted + impl->chunks[last].lines.size() - suffix + (last + 1 < end ? impl->chunks[last + 1].lines.size() : 0);
        auto chunks = (rows + Impl::chunkRows - 1) / Impl::chunkRows;
        std::vector<Impl::Chunk> replacement;
        replacement.reserve(chunks);
        size_t copied = 0, length = 0;
        auto append = [&](TextLine&& line) {
            if (replacement.empty() || replacement.back().lines.size() == length) {
                length = (rows - copied) / (chunks - replacement.size());
                auto& chunk = replacement.emplace_back();
                chunk.first = impl->chunks[begin].first + copied;
                chunk.lines.reserve(length);
            }
            auto& chunk = replacement.back();
            chunk.floatRows += std::isnan(line.top);
            chunk.lines.push_back(std::move(line));
            ++copied;
        };
        for (size_t row = 0; row < prefix; ++row)
            append(std::move(impl->chunks[begin].lines[row]));
        for (auto& line : value)
            append(std::move(line));
        for (size_t i = last; i < end; ++i) {
            auto& chunk = impl->chunks[i];
            for (size_t row = i == last ? suffix : 0; row < chunk.lines.size(); ++row) {
                Impl::shift(chunk.lines[row], offset, y);
                append(std::move(chunk.lines[row]));
            }
        }
        for (size_t i = end; i < impl->chunks.size(); ++i) {
            auto& chunk = impl->chunks[i];
            chunk.first = chunk.first - count + inserted;
            Impl::shift(chunk, offset, y);
        }
        if (replacement.size() == end - begin) std::move(replacement.begin(), replacement.end(), impl->chunks.begin() + begin);
        else {
            auto at = impl->chunks.erase(impl->chunks.begin() + begin, impl->chunks.begin() + end);
            impl->chunks.insert(at, std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
        }
    }
    impl->count = impl->count - count + inserted;
    impl->checkOrder(first ? first - 1 : 0, std::min(impl->count, first + inserted + 1), ordered, orderedRows);
    impl->orderedRows = orderedRows;
    impl->orderedOffsets = ordered;
    // Release incoming metadata before a legacy observer requests its flat copy.
    std::vector<TextLine>{}.swap(value);
    impl->refreshSnapshot();
    return true;
}

size_t TextView::Impl::lineAt(size_t offset) const
{
    if (orderedOffsets) {
        size_t first = 0, last = count;
        while (first < last) {
            auto middle = first + (last - first) / 2;
            if (start(middle) <= offset) first = middle + 1;
            else last = middle;
        }
        return first ? first - 1 : 0;
    }
    size_t best = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto& value = metadata(i);
        if (value.offsets.empty() && !measure) continue;
        if (value.start() > offset) break;
        best = i;
        if (offset < value.end()) break;
    }
    return best;
}

size_t TextView::Impl::hitLine(float x, float y) const
{
    size_t best = 0, begin = 0, end = count;
    auto documentY = double(y) + origin;
    if (orderedRows) {
        size_t first = 0, last = count;
        while (first < last) {
            auto middle = first + (last - first) / 2;
            if (top(middle) <= documentY) first = middle + 1;
            else last = middle;
        }
        begin = first ? first - 1 : 0;
        end = std::min(count, begin + 2);
    }
    double vertical = std::numeric_limits<double>::infinity();
    float horizontal = std::numeric_limits<float>::infinity();
    for (size_t i = begin; i < end; ++i) {
        const auto& line = metadata(i);
        const auto& r = line.bounds;
        auto top = line.y();
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
    return !count ? 0 : line(hitLine(x, y)).hit(x);
}

std::optional<size_t> TextView::Impl::characterAt(float x, float y) const
{
    if (!count) return {};
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
    if (!impl->count) return {};
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
    size_t first = 0, last = impl->count;
    if (viewport && impl->orderedRows) {
        auto top = double(viewport->y) + impl->origin;
        auto visible = visibleLines(top, top + viewport->h);
        first = visible.first;
        last = visible.second;
    }
    for (size_t index = first; index < last; ++index) {
        const auto& value = impl->metadata(index);
        if (value.start() > end || value.end() < start) continue;
        auto y = float(value.y() - impl->origin);
        if (viewport && (y + value.bounds.h <= viewport->y || y >= viewport->y + viewport->h)) continue;
        const auto& line = impl->line(index);
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
        if (impl->count != 0) {
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
        if ((key == Key::Home || key == Key::End) && !mods.primary && impl->count != 0 && !impl->metadata(impl->lineAt(impl->selecting.selection().caret)).offsets.empty()) {
            auto selection = impl->selecting.selection();
            const auto& line = impl->metadata(impl->lineAt(selection.caret));
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
        if (!impl->count) return true;
        auto selection = impl->selecting.selection();
        size_t current = impl->lineAt(selection.caret), target = current;
        const auto& line = impl->line(current);
        if (!impl->preferredX) impl->preferredX = line.position(selection.caret);
        bool up = key == Key::Up || key == Key::PageUp;
        auto height = std::max(1.0f, line.bounds.h);
        float distance = key == Key::PageUp || key == Key::PageDown ? std::max(1.0f, std::floor(pageHeight / height)) * height : height;
        double y = line.y() + (up ? -distance : distance);
        if (key == Key::Up) target = current ? current - 1 : 0;
        else if (key == Key::Down) target = std::min(current + 1, impl->count - 1);
        else if (up)
            while (target && impl->top(target) > y)
                --target;
        else
            while (target + 1 < impl->count && impl->top(target) < y)
                ++target;
        auto offset = impl->line(target).hit(*impl->preferredX);
        impl->selecting.select(mods.shift ? selection.anchor : offset, offset);
        return true;
    }
    return false;
}

}  // namespace ui
