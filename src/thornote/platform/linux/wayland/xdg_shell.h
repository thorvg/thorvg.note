#pragma once

#include <wayland-client.h>

struct xdg_toplevel;
struct xdg_surface;

namespace tn
{
// These requests have fixed opcodes and arguments since xdg-shell version 1.
inline void moveXdgToplevel(xdg_toplevel* top, wl_seat* seat, uint32_t serial)
{
    constexpr uint32_t move = 5;
    wl_proxy_marshal(reinterpret_cast<wl_proxy*>(top), move, seat, serial);
}

inline void setXdgWindowGeometry(xdg_surface* surface, int32_t x, int32_t y, int32_t width, int32_t height)
{
    constexpr uint32_t setWindowGeometry = 3;
    wl_proxy_marshal(reinterpret_cast<wl_proxy*>(surface), setWindowGeometry, x, y, width, height);
}
}  // namespace tn
