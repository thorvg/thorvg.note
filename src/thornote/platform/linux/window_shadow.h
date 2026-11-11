#pragma once

#include <SDL3/SDL.h>
#include <vector>

namespace tn
{
constexpr int windowShadowMargin = 48;
struct WindowShadowImage
{
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;
};
WindowShadowImage windowShadowImage(SDL_Point content, int scale, bool focused);
}  // namespace tn
