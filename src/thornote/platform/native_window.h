#pragma once

#include "window_controls.h"
#include <SDL3/SDL.h>
#include <functional>
#include <memory>

namespace tn
{
// How the platform shows the app icon that a window icon would otherwise supply.
struct AppIconStyle
{
    // macOS already shows the bundle icon in the Dock; a window icon would replace it at another size.
    bool provided;
    // Share of each edge left transparent: macOS draws icon artwork in the central 824 of 1024 pixels.
    float margin;
};
AppIconStyle nativeAppIconStyle();

bool performWindowAction(SDL_Window*, WindowAction);

bool updateNativeWindowChrome(SDL_Window*, bool nativeTitleBar);
bool nativeWindowChromeSupported(SDL_Window*);
SDL_HitTestResult nativeWindowHitTest(SDL_Window*, const SDL_Point&, bool draggable);
bool performNativeTitleBarDoubleClick(SDL_Window*);

class NativeTitleBarInput
{
public:
    NativeTitleBarInput();
    ~NativeTitleBarInput();
    bool initialize(SDL_Window*, std::function<bool(float, float)> draggable, std::function<void()> doubleClick);
    SDL_HitTestResult hitTest(SDL_HitTestResult) const;
    // Preserve native pointer metadata before dispatching to the application.
    bool event(SDL_Event&);
    void update(bool customTitleBar, bool focused);
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace tn
