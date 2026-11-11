#pragma once

#include <SDL3/SDL.h>

struct wl_registry;
struct wl_compositor;
struct wl_subcompositor;
struct wl_shm;
struct wl_surface;
struct wl_subsurface;
struct wl_buffer;

namespace tn
{
class WaylandWindowShadow
{
public:
    explicit WaylandWindowShadow(SDL_Window* window) :
        window(window) {}
    ~WaylandWindowShadow();
    bool bind(wl_registry*, uint32_t name, const char* interface, uint32_t version);
    void update(bool custom, bool focused);

private:
    void clear();
    SDL_Window* window;
    wl_compositor* compositor = nullptr;
    wl_subcompositor* subcompositor = nullptr;
    wl_shm* shm = nullptr;
    wl_surface* parent = nullptr;
    wl_surface* surface = nullptr;
    wl_subsurface* subsurface = nullptr;
    wl_buffer* buffer = nullptr;
    SDL_Point size{};
    int density = 0;
    bool focused = false, visible = false;
};
}  // namespace tn
