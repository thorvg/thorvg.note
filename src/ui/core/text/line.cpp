#include <ui/ui.h>

namespace ui
{
size_t TextLine::hit(float value) const
{
    if (offsets.empty()) return base;
    if (x.empty()) return base + offsets.front();
    for (size_t i = 1; i < std::min(x.size(), offsets.size()); ++i)
        if (value < (x[i - 1] + x[i]) / 2) return base + offsets[i - 1];
    return base + offsets.back();
}

float TextLine::position(size_t offset) const
{
    if (x.empty()) return bounds.x;
    if (offsets.empty() || offset <= start()) return x.front();
    if (offset >= end()) return x[std::min(offsets.size(), x.size()) - 1];
    auto found = std::lower_bound(offsets.begin(), offsets.end(), offset - base);
    return x[std::min(size_t(found - offsets.begin()), x.size() - 1)];
}

Rect TextLine::rangeRect(size_t start, size_t end) const
{
    if (offsets.empty() || start > end || start > this->end() || end < this->start()) return {};
    if (start == end) return {position(start), float(y()), 1.5f, bounds.h};
    if (end == this->start()) return {};
    float left = position(std::max(start, this->start()));
    float right = position(std::min(end, this->end()));
    if (end > this->end()) right += breakWidth;
    return {left, float(y()), right - left, bounds.h};
}

}  // namespace ui
