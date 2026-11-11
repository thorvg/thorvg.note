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

}  // namespace ui
