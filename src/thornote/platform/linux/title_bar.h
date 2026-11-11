#pragma once

#include <thornote/platform/native_window.h>
#include <thornote/platform/sdl/title_bar_gesture.h>
#include <algorithm>

namespace tn
{
class LinuxTitleBarInput
{
public:
    LinuxTitleBarInput(SDL_Window* window, std::function<bool(float, float)> draggable, std::function<void()> doubleClick) :
        window(window), draggable(std::move(draggable)), doubleClick(std::move(doubleClick)) {}
    virtual ~LinuxTitleBarInput() = default;
    virtual void update(bool, bool) {}
    bool event(const SDL_Event& event)
    {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID == SDL_GetWindowID(window)) suppressed = false;
        if ((event.type != SDL_EVENT_MOUSE_BUTTON_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_UP) || event.button.windowID != SDL_GetWindowID(window) || event.button.button != SDL_BUTTON_LEFT) return false;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) return suppressed = hit(event.button.x, event.button.y);
        bool handled = suppressed;
        suppressed = false;
        return handled;
    }

protected:
    bool hit(float x, float y) const
    {
        return draggable(x, y) && nativeWindowHitTest(window, {int(x), int(y)}, true) == SDL_HITTEST_DRAGGABLE;
    }
    static TitleBarGesture gesture()
    {
        auto time = SDL_GetHint(SDL_HINT_MOUSE_DOUBLE_CLICK_TIME), radius = SDL_GetHint(SDL_HINT_MOUSE_DOUBLE_CLICK_RADIUS);
        return TitleBarGesture(time ? uint32_t(std::max(1, SDL_atoi(time))) : 500, radius ? float(std::max(1, SDL_atoi(radius))) : 4);
    }
    SDL_Window* window;
    std::function<bool(float, float)> draggable;
    std::function<void()> doubleClick;

private:
    bool suppressed = false;
};

std::unique_ptr<LinuxTitleBarInput> createX11TitleBarInput(SDL_Window*, std::function<bool(float, float)>, std::function<void()>);
std::unique_ptr<LinuxTitleBarInput> createWaylandTitleBarInput(SDL_Window*, std::function<bool(float, float)>, std::function<void()>);
}  // namespace tn
