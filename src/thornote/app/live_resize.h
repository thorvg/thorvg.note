#pragma once

#include <SDL3/SDL.h>
#include <functional>

namespace tn
{

// Native move/resize loops can deliver expose events before SDL_PollEvent returns.
// Keep presentation on the main thread and suppress recursive rendering.
class LiveResize
{
public:
    LiveResize() = default;
    LiveResize(const LiveResize&) = delete;
    LiveResize& operator=(const LiveResize&) = delete;
    ~LiveResize() { stop(); }
    bool start(SDL_Window* window, std::function<bool()> render)
    {
        stop();
        this->window = window;
        windowId = SDL_GetWindowID(window);
        drawFrame = std::move(render);
        failed = false;
        hasFrame = false;
        exposedAt = 0;
        watching = SDL_AddEventWatch(watch, this);
        return watching;
    }
    void stop()
    {
        if (watching) SDL_RemoveEventWatch(watch, this);
        watching = false;
        drawFrame = {};
        window = nullptr;
    }
    // Input still requires a rebuild between events. Window notifications can
    // describe geometry already presented by the expose watch or a prior event.
    bool needsDraw(const SDL_Event& event) const
    {
        switch (event.type) {
            case SDL_EVENT_WINDOW_EXPOSED:
                return event.window.windowID != windowId || !event.common.timestamp || event.common.timestamp > exposedAt;
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
                return event.window.windowID != windowId || !hasFrame || !(frameSize == size());
            default: return true;
        }
    }
    bool draw()
    {
        return draw(drawFrame);
    }
    // True while the expose watch draws; the window contents must be presented again.
    bool exposing() const { return exposed; }
    bool draw(const std::function<bool()>& render)
    {
        if (failed || !render) return false;
        if (drawing) return true;
        drawing = true;
        auto currentSize = size();
        bool result = render();
        drawing = false;
        failed |= !result;
        if (result) {
            frameSize = currentSize;
            hasFrame = true;
        }
        return result;
    }

private:
    struct Size
    {
        int windowWidth = 0, windowHeight = 0, pixelWidth = 0, pixelHeight = 0;
        float scale = 0;
        SDL_WindowFlags flags = 0;
        SDL_Rect safeArea{};
        bool operator==(const Size& other) const
        {
            return windowWidth == other.windowWidth && windowHeight == other.windowHeight && pixelWidth == other.pixelWidth && pixelHeight == other.pixelHeight && scale == other.scale && flags == other.flags && safeArea.x == other.safeArea.x && safeArea.y == other.safeArea.y && safeArea.w == other.safeArea.w && safeArea.h == other.safeArea.h;
        }
    };
    Size size() const
    {
        Size value;
        SDL_GetWindowSize(window, &value.windowWidth, &value.windowHeight);
        SDL_GetWindowSizeInPixels(window, &value.pixelWidth, &value.pixelHeight);
        value.scale = SDL_GetWindowDisplayScale(window);
        value.flags = SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_HIDDEN);
        SDL_GetWindowSafeArea(window, &value.safeArea);
        return value;
    }
    static bool SDLCALL watch(void* data, SDL_Event* event)
    {
        if (SDL_IsMainThread() && event->type == SDL_EVENT_WINDOW_EXPOSED) {
            auto& self = *static_cast<LiveResize*>(data);
            if (event->window.windowID == self.windowId && !self.drawing) {
                self.exposed = true;
                if (self.draw()) self.exposedAt = event->common.timestamp;
                self.exposed = false;
            }
        }
        return true;
    }
    SDL_WindowID windowId = 0;
    SDL_Window* window = nullptr;
    std::function<bool()> drawFrame;
    Size frameSize;
    Uint64 exposedAt = 0;
    bool watching = false, drawing = false, failed = false, hasFrame = false, exposed = false;
};

}  // namespace tn
