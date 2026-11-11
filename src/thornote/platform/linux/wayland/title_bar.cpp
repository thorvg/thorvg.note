#include <thornote/platform/linux/title_bar.h>
#include "window_shadow.h"
#include "xdg_shell.h"
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <cstring>
#include <vector>

namespace tn
{
namespace
{
class WaylandTitleBarInput : public LinuxTitleBarInput
{
    struct Seat
    {
        WaylandTitleBarInput& owner;
        uint32_t name, serial = 0;
        wl_seat* seat;
        wl_pointer* pointer = nullptr;
        TitleBarGesture clicks{gesture()};
        bool focused = false;
        float x = 0, y = 0;

        ~Seat()
        {
            releasePointer();
            if (wl_seat_get_version(seat) >= WL_SEAT_RELEASE_SINCE_VERSION) wl_seat_release(seat);
            else wl_seat_destroy(seat);
        }
        void releasePointer()
        {
            if (!pointer) return;
            if (wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) wl_pointer_release(pointer);
            else wl_pointer_destroy(pointer);
            pointer = nullptr;
            focused = false;
            clicks.cancel();
        }
        void position(wl_fixed_t sx, wl_fixed_t sy)
        {
            auto display = SDL_GetWindowDisplayScale(owner.window), density = SDL_GetWindowPixelDensity(owner.window);
            auto scale = display > 0 && density > 0 ? display / density : 1;
            x = float(wl_fixed_to_double(sx)) * scale;
            y = float(wl_fixed_to_double(sy)) * scale;
        }
        static const wl_pointer_listener& listener()
        {
            static const auto value = [] {
                wl_pointer_listener result{};
                result.enter = [](void* data, wl_pointer*, uint32_t, wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
                    auto& self = *static_cast<Seat*>(data);
                    self.focused = surface == SDL_GetPointerProperty(SDL_GetWindowProperties(self.owner.window), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
                    self.clicks.cancel();
                    self.position(x, y);
                };
                result.leave = [](void* data, wl_pointer*, uint32_t, wl_surface*) {
                    auto& self = *static_cast<Seat*>(data);
                    self.focused = false;
                    self.clicks.cancel();
                };
                result.motion = [](void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
                    auto& self = *static_cast<Seat*>(data);
                    self.position(x, y);
                    if (!self.focused || self.clicks.motion(self.x, self.y) != TitleBarGesture::Action::Move) return;
                    auto top = static_cast<xdg_toplevel*>(SDL_GetPointerProperty(SDL_GetWindowProperties(self.owner.window), SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_POINTER, nullptr));
                    if (top) moveXdgToplevel(top, self.seat, self.serial);
                };
                result.button = [](void* data, wl_pointer*, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
                    auto& self = *static_cast<Seat*>(data);
                    if (button != BTN_LEFT) {
                        self.clicks.cancel();
                        return;
                    }
                    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
                        if (!self.focused || !self.owner.hit(self.x, self.y)) {
                            self.clicks.cancel();
                            return;
                        }
                        self.serial = serial;
                        self.clicks.press(time, self.x, self.y);
                    } else if (self.clicks.release(self.x, self.y, self.focused && self.owner.hit(self.x, self.y)) == TitleBarGesture::Action::DoubleClick) {
                        self.owner.doubleClick();
                    }
                };
                result.axis = [](void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {
                };
                result.frame = [](void*, wl_pointer*) {
                };
                result.axis_source = [](void*, wl_pointer*, uint32_t) {
                };
                result.axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) {
                };
                result.axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) {
                };
                return result;
            }();
            return value;
        }
    };

public:
    WaylandTitleBarInput(SDL_Window* window, wl_display* display, std::function<bool(float, float)> draggable, std::function<void()> doubleClick) :
        LinuxTitleBarInput(window, std::move(draggable), std::move(doubleClick)), display(display), shadow(window) {}
    void update(bool custom, bool focused) override { shadow.update(custom, focused); }
    ~WaylandTitleBarInput() override
    {
        seats.clear();
        if (registry) wl_registry_destroy(registry);
    }
    bool initialize()
    {
        registry = wl_display_get_registry(display);
        if (!registry) return false;
        static const wl_registry_listener listener{
            [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
                auto& self = *static_cast<WaylandTitleBarInput*>(data);
                if (self.shadow.bind(registry, name, interface, version) || std::strcmp(interface, wl_seat_interface.name)) return;
                auto seat = std::unique_ptr<Seat>(new Seat{self, name, 0,
                                                           static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)))});
                static const wl_seat_listener listener{
                    [](void* data, wl_seat* seat, uint32_t capabilities) {
                        auto& self = *static_cast<Seat*>(data);
                        if (!(capabilities & WL_SEAT_CAPABILITY_POINTER)) self.releasePointer();
                        else if (!self.pointer) {
                            self.pointer = wl_seat_get_pointer(seat);
                            wl_pointer_add_listener(self.pointer, &Seat::listener(), &self);
                        }
                    },
                    [](void*, wl_seat*, const char*) {
                    }};
                wl_seat_add_listener(seat->seat, &listener, seat.get());
                self.seats.push_back(std::move(seat));
            },
            [](void* data, wl_registry*, uint32_t name) {
                auto& seats = static_cast<WaylandTitleBarInput*>(data)->seats;
                seats.erase(std::remove_if(seats.begin(), seats.end(), [name](const auto& seat) { return seat->name == name; }), seats.end());
            }};
        wl_registry_add_listener(registry, &listener, this);
        // SDL dispatches the same display queue; only our registry and input proxies are owned here.
        return wl_display_roundtrip(display) >= 0 && wl_display_roundtrip(display) >= 0;
    }

private:
    wl_display* display;
    wl_registry* registry = nullptr;
    WaylandWindowShadow shadow;
    std::vector<std::unique_ptr<Seat>> seats;
};
}  // namespace

std::unique_ptr<LinuxTitleBarInput> createWaylandTitleBarInput(SDL_Window* window, std::function<bool(float, float)> draggable, std::function<void()> doubleClick)
{
    auto properties = SDL_GetWindowProperties(window);
    auto display = static_cast<wl_display*>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr));
    auto surface = static_cast<wl_surface*>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr));
    if (!display || !surface) return {};
    auto input = std::make_unique<WaylandTitleBarInput>(window, display, std::move(draggable), std::move(doubleClick));
    return input->initialize() ? std::move(input) : nullptr;
}
}  // namespace tn
