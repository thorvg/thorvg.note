#include <thornote/platform/native_window.h>

namespace tn
{
WindowControls nativeWindowControls()
{
    return {WindowControlStyle::CaptionIcons};
}
AppIconStyle nativeAppIconStyle()
{
    return {false, 0};
}
bool nativeWindowChromeAvailable()
{
    return false;
}
bool nativeWindowChromeSupported(SDL_Window*)
{
    return false;
}
bool updateNativeWindowChrome(SDL_Window*, bool)
{
    return true;
}
SDL_HitTestResult nativeWindowHitTest(SDL_Window*, const SDL_Point&, bool)
{
    return SDL_HITTEST_NORMAL;
}
const char* nativeWindowZoomCommand()
{
    return "window.maximize";
}

bool performNativeTitleBarDoubleClick(SDL_Window* window)
{
    return SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED ? SDL_RestoreWindow(window) : SDL_MaximizeWindow(window);
}

struct NativeTitleBarInput::Impl
{
};
NativeTitleBarInput::NativeTitleBarInput() = default;
NativeTitleBarInput::~NativeTitleBarInput() = default;
SDL_HitTestResult NativeTitleBarInput::hitTest(SDL_HitTestResult result) const
{
    return result;
}
bool NativeTitleBarInput::event(SDL_Event&)
{
    return false;
}
void NativeTitleBarInput::update(bool, bool) {}
bool NativeTitleBarInput::initialize(SDL_Window*, std::function<bool(float, float)>, std::function<void()>)
{
    return false;
}
void NativeTitleBarInput::stop() {}
}  // namespace tn
