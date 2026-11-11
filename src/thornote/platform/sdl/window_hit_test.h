#pragma once

#include <SDL3/SDL.h>

namespace tn
{
inline SDL_HitTestResult borderlessWindowHitTest(SDL_Point size, SDL_Point point, SDL_WindowFlags flags, float border, bool draggable)
{
    if ((flags & SDL_WINDOW_FULLSCREEN) || point.x < 0 || point.y < 0 || point.x >= size.x || point.y >= size.y)
        return SDL_HITTEST_NORMAL;
    if ((flags & SDL_WINDOW_RESIZABLE) && !(flags & SDL_WINDOW_MAXIMIZED)) {
        bool left = point.x < border, right = point.x >= size.x - border;
        bool top = point.y < border, bottom = point.y >= size.y - border;
        if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (top) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
    }
    return draggable ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}
}  // namespace tn
