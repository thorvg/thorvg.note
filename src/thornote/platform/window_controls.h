#pragma once

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

enum class WindowAction
{
    Minimize,
    Maximize,
    Fullscreen,
    TitleBarDoubleClick
};

// Lua owns the title bar; the platform owns window movement and decorations.
bool nativeWindowChromeAvailable();
const char* nativeWindowZoomCommand();
}  // namespace tn
