#include "window_shadow.h"
#include <algorithm>
#include <cmath>

namespace tn
{
WindowShadowImage windowShadowImage(SDL_Point content, int scale, bool focused)
{
    if (content.x <= 0 || content.y <= 0 || scale <= 0) return {};
    WindowShadowImage image{(content.x + 2 * windowShadowMargin) * scale, (content.y + 2 * windowShadowMargin) * scale, {}};
    std::vector<double> horizontal(image.width), vertical(image.height);
    auto coverage = [scale](int pixel, int extent, double offset) {
        double position = (pixel + 0.5) / scale - windowShadowMargin - offset;
        return (std::erf(position / (12 * std::sqrt(2.0))) - std::erf((position - extent) / (12 * std::sqrt(2.0)))) / 2;
    };
    for (int x = 0; x < image.width; ++x)
        horizontal[x] = coverage(x, content.x, 0);
    for (int y = 0; y < image.height; ++y)
        vertical[y] = coverage(y, content.y, 10);
    // The floating shadow uses a 24px blur and 18% black; inactive windows recede.
    double opacity = focused ? 46 : 25;
    image.pixels.resize(size_t(image.width) * image.height);
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            image.pixels[size_t(y) * image.width + x] = uint32_t(std::lround(opacity * horizontal[x] * vertical[y])) << 24;
    return image;
}
}  // namespace tn
