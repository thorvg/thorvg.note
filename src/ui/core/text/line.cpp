#include <ui/ui.h>

namespace ui
{
size_t TextLine::hit(float value) const
{
    if (offsets.empty()) return 0;
    if (x.empty()) return offsets.front();
    for (size_t i = 1; i < std::min(x.size(), offsets.size()); ++i)
        if (value < (x[i - 1] + x[i]) / 2) return offsets[i - 1];
    return offsets.back();
}

float TextLine::position(size_t offset) const
{
    auto found = std::lower_bound(offsets.begin(), offsets.end(), offset);
    return x.empty() ? bounds.x : x[std::min(size_t(found - offsets.begin()), x.size() - 1)];
}

Rect TextLine::rangeRect(size_t start, size_t end) const
{
    if (offsets.empty() || start > end || start > offsets.back() || end < offsets.front()) return {};
    if (start == end) return {position(start), bounds.y, 1.5f, bounds.h};
    if (end == offsets.front()) return {};
    float left = position(std::max(start, offsets.front()));
    float right = position(std::min(end, offsets.back()));
    if (end > offsets.back()) right += breakWidth;
    return {left, bounds.y, right - left, bounds.h};
}

}  // namespace ui
