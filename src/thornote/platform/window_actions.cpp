#include "native_window.h"

namespace tn
{
bool performWindowAction(SDL_Window* window, WindowAction action)
{
    auto flags = SDL_GetWindowFlags(window);
    switch (action) {
        case WindowAction::Minimize: return SDL_MinimizeWindow(window);
        case WindowAction::Maximize: return flags & SDL_WINDOW_MAXIMIZED ? SDL_RestoreWindow(window) : SDL_MaximizeWindow(window);
        case WindowAction::Fullscreen: return SDL_SetWindowFullscreen(window, !(flags & SDL_WINDOW_FULLSCREEN));
        case WindowAction::TitleBarDoubleClick: return performNativeTitleBarDoubleClick(window);
    }
    return false;
}
}  // namespace tn
