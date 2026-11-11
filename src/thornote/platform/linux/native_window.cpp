#include <thornote/platform/native_window.h>
#include <thornote/platform/sdl/window_hit_test.h>
#include "title_bar.h"
#include <string_view>

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
    return true;
}
const char* nativeWindowZoomCommand()
{
    return "window.maximize";
}

bool nativeWindowChromeSupported(SDL_Window* window)
{
    auto driver = SDL_GetCurrentVideoDriver();
    return window && driver && (std::string_view(driver) == "x11" || std::string_view(driver) == "wayland");
}

bool updateNativeWindowChrome(SDL_Window* window, bool nativeTitleBar)
{
    if (!nativeWindowChromeSupported(window) || (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) return true;
    return SDL_SetWindowBordered(window, nativeTitleBar);
}

SDL_HitTestResult nativeWindowHitTest(SDL_Window* window, const SDL_Point& point, bool draggable)
{
    SDL_Point size{};
    if (!SDL_GetWindowSize(window, &size.x, &size.y)) return SDL_HITTEST_NORMAL;
    auto display = SDL_GetWindowDisplayScale(window), density = SDL_GetWindowPixelDensity(window);
    float scale = display > 0 && density > 0 ? display / density : 1;
    return borderlessWindowHitTest(size, point, SDL_GetWindowFlags(window), 6 * scale, draggable);
}

bool performNativeTitleBarDoubleClick(SDL_Window* window)
{
    auto flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_FULLSCREEN) return true;
    return flags & SDL_WINDOW_MAXIMIZED ? SDL_RestoreWindow(window) : SDL_MaximizeWindow(window);
}

struct NativeTitleBarInput::Impl
{
    std::unique_ptr<LinuxTitleBarInput> input;
};
NativeTitleBarInput::NativeTitleBarInput() :
    impl(std::make_unique<Impl>()) {}
NativeTitleBarInput::~NativeTitleBarInput() = default;
bool NativeTitleBarInput::initialize(SDL_Window* window, std::function<bool(float, float)> draggable, std::function<void()> doubleClick)
{
    stop();
    auto driver = SDL_GetCurrentVideoDriver();
    if (!driver) return false;
    if (std::string_view(driver) == "x11") impl->input = createX11TitleBarInput(window, std::move(draggable), std::move(doubleClick));
    else if (std::string_view(driver) == "wayland") impl->input = createWaylandTitleBarInput(window, std::move(draggable), std::move(doubleClick));
    return bool(impl->input);
}
SDL_HitTestResult NativeTitleBarInput::hitTest(SDL_HitTestResult result) const
{
    // SDL keeps resize handling; native input distinguishes title clicks from dragging.
    return impl->input && result == SDL_HITTEST_DRAGGABLE ? SDL_HITTEST_NORMAL : result;
}
bool NativeTitleBarInput::event(SDL_Event& event)
{
    return impl->input && impl->input->event(event);
}
void NativeTitleBarInput::update(bool customTitleBar, bool focused)
{
    if (impl->input) impl->input->update(customTitleBar, focused);
}
void NativeTitleBarInput::stop()
{
    impl->input.reset();
}
}  // namespace tn
