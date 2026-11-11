#pragma once

#include <SDL3/SDL.h>
#include <X11/Xlib.h>
#include <array>

namespace tn
{
class X11WindowShadow
{
public:
    X11WindowShadow(SDL_Window* window, Display* display) :
        window(window), display(display) {}
    ~X11WindowShadow();
    void update(bool custom, bool focused);

private:
    void freePixmaps();
    SDL_Window* window;
    Display* display;
    Window xid = 0;
    Atom atom = 0;
    std::array<Pixmap, 8> pixmaps{};
    bool visible = false, focused = false;
    int scale = 0;
};
}  // namespace tn
