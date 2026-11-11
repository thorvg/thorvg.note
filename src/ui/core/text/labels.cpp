#include "internal.h"

namespace ui
{
namespace
{
constexpr float fontPointsPerLogicalPixel = 72.0f / 96.0f;
}
void Context::text(Rect bounds, std::string_view value, Color color, float size, bool bold, Align align, float horizontalOffset)
{
    if (bounds.w <= 0 || bounds.h <= 0 || value.empty()) return;
    auto fontId = fontFor ? fontFor(value, bold) : findFont(bold ? theme().boldFont : theme().font);
    auto font = detail::fontManager().textFont(fontId);
    if (!font) font = bold ? theme().boldFont.c_str() : theme().font.c_str();
    size = (size > 0 ? size : theme().fontSize) * fontPointsPerLogicalPixel;
    paint(paintHash(value, paintHash(font, paintKey('T', bounds, color, size, align, horizontalOffset))), [&] {
        auto label = tvg::Text::gen();
        label->font(font);
        label->size(size);
        label->text(std::string(value).c_str());
        label->fill(color.r, color.g, color.b);
        label->opacity(color.a);
        float x = 0, y = 0, w = 0, h = 0;
        label->bounds(&x, &y, &w, &h);
        tvg::TextMetrics metrics{};
        label->metrics(metrics);
        float offset = align == Align::Center ? (bounds.w - w) / 2 : align == Align::End ? bounds.w - w
                                                                                         : 0;
        label->translate(bounds.x + offset - x - horizontalOffset, bounds.y + (bounds.h - (metrics.ascent - metrics.descent)) / 2);
        auto clip = tvg::Shape::gen();
        clip->appendRect(bounds.x, bounds.y, bounds.w, bounds.h);
        clip->fill(255, 255, 255);
        auto group = tvg::Scene::gen();
        group->add(label);
        group->clip(clip);
        return group;
    });
}

float Context::textWidth(std::string_view value, float size, bool bold, std::string_view fontContext) const
{
    if (value.empty()) return 0;
    std::unique_ptr<tvg::Text> label(tvg::Text::gen());
    auto fontId = fontFor ? fontFor(fontContext.empty() ? value : fontContext, bold) : findFont(bold ? theme().boldFont : theme().font);
    auto font = detail::fontManager().textFont(fontId);
    label->font(font ? font : bold ? theme().boldFont.c_str()
                                   : theme().font.c_str());
    label->size((size > 0 ? size : theme().fontSize) * fontPointsPerLogicalPixel);
    label->text(std::string(value).c_str());
    float x = 0, y = 0, w = 0, h = 0;
    label->bounds(&x, &y, &w, &h);
    return w;
}

std::string Context::elideText(std::string_view value, float width, float size, bool bold, std::string_view suffix) const
{
    if (width <= 0 || value.empty()) return {};
    std::string full(value);
    full += suffix;
    if (textWidth(full, size, bold) <= width) return full;
    std::vector<size_t> ends;
    for (size_t end = 0; end < value.size();) {
        ++end;
        while (end < value.size() && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80)
            ++end;
        if (end < value.size()) ends.push_back(end);
    }
    auto shortened = [&](size_t end) {
        std::string text(value.substr(0, end));
        text += "…";
        text += suffix;
        return text;
    };
    size_t low = 0, high = ends.size();
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (textWidth(shortened(ends[middle]), size, bold) <= width) low = middle + 1;
        else high = middle;
    }
    return low ? shortened(ends[low - 1]) : std::string{};
}

}  // namespace ui
