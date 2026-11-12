#include "texteditor.h"
#include <algorithm>

namespace ui
{

bool TextEditor::decorations(std::string_view owner, uint64_t revision, std::span<const TextDecoration> ranges)
{
    const auto& document = impl->editing.document();
    if (owner.empty() || revision != document.revision()) return false;
    for (const auto& range : ranges) {
        if (range.start > range.end || range.end > document.text().size() || document.clamp(range.start) != range.start || document.clamp(range.end) != range.end) return false;
    }
    auto found = std::find_if(impl->decorations.begin(), impl->decorations.end(), [&](const auto& layer) { return layer.owner == owner; });
    if (found == impl->decorations.end()) {
        if (ranges.empty()) return true;
        impl->decorations.push_back({std::string(owner), revision, {}});
        found = impl->decorations.end() - 1;
    }
    found->revision = revision;
    found->ranges.clear();
    found->ranges.reserve(ranges.size());
    for (const auto& range : ranges)
        found->ranges.push_back({range, 0});
    auto order = [](const auto& a, const auto& b) {
        return a.range.start < b.range.start;
    };
    if (!std::is_sorted(found->ranges.begin(), found->ranges.end(), order)) std::stable_sort(found->ranges.begin(), found->ranges.end(), order);
    size_t end = 0;
    for (auto& range : found->ranges)
        range.maxEnd = end = std::max(end, range.range.end);
    impl->invalidate();
    return true;
}

void TextEditor::clearDecorations(std::string_view owner)
{
    auto& layers = impl->decorations;
    auto found = std::find_if(layers.begin(), layers.end(), [&](const auto& layer) { return layer.owner == owner; });
    if (found == layers.end()) return;
    layers.erase(found);
    impl->invalidate();
}

std::vector<Rect> TextEditor::rangeRects(size_t start, size_t end)
{
    if (composing()) return {};
    impl->relayout();
    auto area = impl->textArea();
    auto result = impl->view.rangeRects(start, end, Rect{impl->horizontal, impl->vertical, area.w, area.h});
    for (auto& rect : result) {
        rect.x += area.x - impl->horizontal;
        rect.y += area.y - impl->vertical;
    }
    return result;
}

Rect TextEditor::Impl::rangeRect(const TextLine& line, size_t start, size_t end) const
{
    auto rect = line.rangeRect(start, end);
    auto area = textArea();
    rect.x += area.x - horizontal;
    rect.y += area.y - vertical;
    return rect;
}

void TextEditor::Impl::paintDecorations(size_t first, size_t end, bool borders)
{
    if (first == end) return;
    const auto& lines = view.layout();
    auto revision = editing.document().revision();
    for (const auto& layer : decorations) {
        if (layer.revision != revision) continue;
        // Prefix maxima also find overlapping ranges that begin above the viewport.
        auto it = std::lower_bound(layer.ranges.begin(), layer.ranges.end(), lines[first].offsets.front(), [](const auto& range, size_t offset) { return range.maxEnd < offset; });
        for (; it != layer.ranges.end() && it->range.start <= lines[end - 1].offsets.back(); ++it) {
            const auto& range = it->range;
            auto color = (borders ? range.border : range.background).resolve(themeTokens);
            if (!color.a) continue;
            for (size_t row = first; row < end; ++row) {
                auto rect = rangeRect(lines[row], range.start, range.end);
                if (rect.w <= 0 || rect.h <= 0) continue;
                if (borders) {
                    // Filled edges share selection rasterization and stay inside the range.
                    auto stroke = std::min({1.0f, rect.w / 2, rect.h / 2});
                    context.rect({rect.x, rect.y, rect.w, stroke}, color);
                    context.rect({rect.x, rect.y + rect.h - stroke, rect.w, stroke}, color);
                    context.rect({rect.x, rect.y + stroke, stroke, rect.h - 2 * stroke}, color);
                    context.rect({rect.x + rect.w - stroke, rect.y + stroke, stroke, rect.h - 2 * stroke}, color);
                } else context.rect(rect, color);
            }
        }
    }
}

}  // namespace ui
