#include "window_shadow.h"
#include <thornote/platform/linux/window_shadow.h>
#include "xdg_shell.h"
#include <wayland-client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace tn
{
namespace
{
wl_buffer* createBuffer(wl_shm* shm, const WindowShadowImage& image)
{
    auto bytes = image.pixels.size() * sizeof(uint32_t);
    int fd = memfd_create("ThorNote window shadow", MFD_CLOEXEC);
    if (fd < 0) return nullptr;
    if (ftruncate(fd, off_t(bytes))) {
        close(fd);
        return nullptr;
    }
    auto memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) {
        close(fd);
        return nullptr;
    }
    std::memcpy(memory, image.pixels.data(), bytes);
    munmap(memory, bytes);
    auto pool = wl_shm_create_pool(shm, fd, int(bytes));
    close(fd);
    auto buffer = wl_shm_pool_create_buffer(pool, 0, image.width, image.height, image.width * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    static const wl_buffer_listener listener{[](void*, wl_buffer*) {
    }};
    wl_buffer_add_listener(buffer, &listener, nullptr);
    return buffer;
}
}  // namespace

WaylandWindowShadow::~WaylandWindowShadow()
{
    clear();
    if (shm) wl_shm_destroy(shm);
    if (subcompositor) wl_subcompositor_destroy(subcompositor);
    if (compositor) wl_compositor_destroy(compositor);
}

bool WaylandWindowShadow::bind(wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    if (!std::strcmp(interface, wl_compositor_interface.name))
        compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 3u)));
    else if (!std::strcmp(interface, wl_subcompositor_interface.name))
        subcompositor = static_cast<wl_subcompositor*>(wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
    else if (!std::strcmp(interface, wl_shm_interface.name)) {
        shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        static const wl_shm_listener listener{[](void*, wl_shm*, uint32_t) {
        }};
        wl_shm_add_listener(shm, &listener, this);
    } else return false;
    return true;
}

void WaylandWindowShadow::clear()
{
    if (subsurface) wl_subsurface_destroy(subsurface);
    if (surface) wl_surface_destroy(surface);
    if (buffer) wl_buffer_destroy(buffer);
    subsurface = nullptr;
    surface = parent = nullptr;
    buffer = nullptr;
    size = {};
    density = 0;
    visible = false;
}

void WaylandWindowShadow::update(bool custom, bool focused)
{
    if (!compositor || !subcompositor || !shm) return;
    auto current = static_cast<wl_surface*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr));
    if (current != parent) clear();
    auto display = SDL_GetWindowDisplayScale(window), pixels = SDL_GetWindowPixelDensity(window);
    auto inputScale = display > 0 && pixels > 0 ? pixels / display : 1;
    SDL_Point content{};
    SDL_GetWindowSize(window, &content.x, &content.y);
    content = {int(std::lround(content.x * inputScale)), int(std::lround(content.y * inputScale))};
    auto flags = SDL_GetWindowFlags(window);
    if (custom && current && !(flags & SDL_WINDOW_HIDDEN) && content.x > 0 && content.y > 0) {
        auto xdg = static_cast<xdg_surface*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_XDG_SURFACE_POINTER, nullptr));
        // Subsurfaces must stay outside the content bounds used for maximize and snapping.
        if (xdg) setXdgWindowGeometry(xdg, 0, 0, content.x, content.y);
    }
    bool enabled = custom && current && !(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN));
    if (!enabled) {
        if (visible) {
            wl_surface_attach(surface, nullptr, 0, 0);
            wl_surface_commit(surface);
            visible = false;
        }
        return;
    }
    int scale = wl_compositor_get_version(compositor) >= 3 ? std::max(1, int(std::ceil(display))) : 1;
    bool changed = content.x != size.x || content.y != size.y || scale != density || this->focused != focused;
    if (visible && !changed) return;
    if (changed || !buffer) {
        auto image = windowShadowImage(content, scale, focused);
        if (image.pixels.empty()) return;
        auto next = createBuffer(shm, image);
        if (!next) return;
        if (buffer) wl_buffer_destroy(buffer);
        buffer = next;
        size = content;
        density = scale;
        this->focused = focused;
    }
    if (!surface) {
        parent = current;
        surface = wl_compositor_create_surface(compositor);
        static const auto listener = [] {
            wl_surface_listener result{};
            result.enter = [](void*, wl_surface*, wl_output*) {
            };
            result.leave = [](void*, wl_surface*, wl_output*) {
            };
            return result;
        }();
        wl_surface_add_listener(surface, &listener, nullptr);
        subsurface = wl_subcompositor_get_subsurface(subcompositor, surface, parent);
        wl_subsurface_set_position(subsurface, -windowShadowMargin, -windowShadowMargin);
        wl_subsurface_place_below(subsurface, parent);
        auto empty = wl_compositor_create_region(compositor);
        wl_surface_set_input_region(surface, empty);
        wl_region_destroy(empty);
    }
    // A synchronized, input-transparent child leaves SDL's content geometry untouched.
    if (wl_surface_get_version(surface) >= 3) wl_surface_set_buffer_scale(surface, scale);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, content.x + 2 * windowShadowMargin, content.y + 2 * windowShadowMargin);
    wl_surface_commit(surface);
    visible = true;
}
}  // namespace tn
