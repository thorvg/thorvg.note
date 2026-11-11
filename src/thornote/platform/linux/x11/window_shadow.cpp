#include "window_shadow.h"
#include <thornote/platform/linux/window_shadow.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace tn
{
namespace
{
Pixmap createPixmap(Display* display, const XVisualInfo& visual, const WindowShadowImage& shadow, SDL_Rect bounds)
{
    auto image = XCreateImage(display, visual.visual, 32, ZPixmap, 0, nullptr, bounds.w, bounds.h, 32, 0);
    if (!image) return 0;
    image->data = static_cast<char*>(std::malloc(size_t(image->bytes_per_line) * bounds.h));
    if (!image->data) {
        XDestroyImage(image);
        return 0;
    }
    for (int y = 0; y < bounds.h; ++y)
        std::copy_n(shadow.pixels.data() + size_t(bounds.y + y) * shadow.width + bounds.x, bounds.w,
                    reinterpret_cast<uint32_t*>(image->data + y * image->bytes_per_line));
    auto pixmap = XCreatePixmap(display, RootWindow(display, visual.screen), bounds.w, bounds.h, 32);
    auto gc = XCreateGC(display, pixmap, 0, nullptr);
    XPutImage(display, pixmap, gc, image, 0, 0, 0, 0, bounds.w, bounds.h);
    XFreeGC(display, gc);
    XDestroyImage(image);
    return pixmap;
}
}  // namespace

X11WindowShadow::~X11WindowShadow()
{
    auto current = Window(SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    if (visible && current == xid) XDeleteProperty(display, xid, atom);
    freePixmaps();
}

void X11WindowShadow::freePixmaps()
{
    for (auto& pixmap : pixmaps) {
        if (pixmap) XFreePixmap(display, pixmap);
        pixmap = 0;
    }
}

void X11WindowShadow::update(bool custom, bool focused)
{
    auto properties = SDL_GetWindowProperties(window);
    auto current = Window(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    if (current != xid) {
        xid = current;
        visible = false;
    }
    if (!xid) return;
    bool enabled = custom && !(SDL_GetWindowFlags(window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN));
    if (!enabled) {
        if (visible) {
            XDeleteProperty(display, xid, atom);
            visible = false;
            XFlush(display);
        }
        return;
    }
    auto density = SDL_GetWindowPixelDensity(window), displayScale = SDL_GetWindowDisplayScale(window);
    int scale = std::max(1, int(std::ceil(density > 0 ? displayScale / density : 1)));
    bool changed = this->focused != focused || this->scale != scale || !pixmaps[0];
    if (visible && !changed) return;
    if (changed) {
        int screen = int(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_SCREEN_NUMBER, 0));
        XVisualInfo visual{};
        if (!XMatchVisualInfo(display, screen, 32, TrueColor, &visual)) return;
        auto shadow = windowShadowImage({2 * windowShadowMargin, 2 * windowShadowMargin}, scale, focused);
        int extent = 2 * windowShadowMargin * scale;
        // Corner tiles overlap the opaque content so the blur joins each edge continuously.
        SDL_Rect tiles[] = {{extent, 0, 1, extent}, {extent, 0, extent, extent}, {extent, extent, extent, 1}, {extent, extent, extent, extent}, {extent, extent, 1, extent}, {0, extent, extent, extent}, {0, extent, extent, 1}, {0, 0, extent, extent}};
        std::array<Pixmap, 8> next{};
        for (size_t i = 0; i < next.size(); ++i) {
            next[i] = createPixmap(display, visual, shadow, tiles[i]);
            if (!next[i]) {
                for (auto pixmap : next)
                    if (pixmap) XFreePixmap(display, pixmap);
                return;
            }
        }
        freePixmaps();
        pixmaps = next;
        this->scale = scale;
        this->focused = focused;
    }
    if (!atom) atom = XInternAtom(display, "_KDE_NET_WM_SHADOW", False);
    unsigned long values[12]{};
    std::copy(pixmaps.begin(), pixmaps.end(), values);
    std::fill(values + 8, values + 12, windowShadowMargin * scale);
    XChangeProperty(display, xid, atom, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(values), 12);
    XFlush(display);
    visible = true;
}
}  // namespace tn
