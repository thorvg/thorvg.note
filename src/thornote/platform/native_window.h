#pragma once

#include <SDL3/SDL.h>
#include <functional>
#include <memory>

namespace tn
{
enum class WindowControlStyle
{
    TrafficLights,
    CaptionIcons
};
struct WindowControls
{
    WindowControlStyle style;
    bool leading() const { return style == WindowControlStyle::TrafficLights; }
    // AppKit draws the traffic lights above the title bar; the app reserves their area and keeps it draggable.
    bool systemDrawn() const { return style == WindowControlStyle::TrafficLights; }
    float buttonWidth() const { return leading() ? 22 : 46; }
    float width() const { return buttonWidth() * 3 + (leading() ? 12 : 0); }
};
WindowControls nativeWindowControls();

// How the platform shows the app icon that a window icon would otherwise supply.
struct AppIconStyle
{
    // macOS already shows the bundle icon in the Dock; a window icon would replace it at another size.
    bool provided;
    // Share of each edge left transparent: macOS draws icon artwork in the central 824 of 1024 pixels.
    float margin;
};
AppIconStyle nativeAppIconStyle();

enum class WindowAction
{
    Minimize,
    Maximize,
    Fullscreen,
    TitleBarDoubleClick
};
bool performWindowAction(SDL_Window*, WindowAction);

// Lua owns the title bar; the platform owns window movement and decorations.
bool nativeWindowChromeAvailable();
bool updateNativeWindowChrome(SDL_Window*, bool nativeTitleBar);
bool nativeWindowChromeSupported(SDL_Window*);
SDL_HitTestResult nativeWindowHitTest(SDL_Window*, const SDL_Point&, bool draggable);
const char* nativeWindowZoomCommand();
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
