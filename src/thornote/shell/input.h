#pragma once

#include "shell.h"
#include <SDL3/SDL.h>
#include <deque>
#include <optional>

namespace tn
{
KeyEvent keyEvent(const SDL_KeyboardEvent&);
// wheelPixels gives the pixels per unit of a precise wheel event, or 0 for wheel steps.
WheelEvent wheelEvent(const SDL_MouseWheelEvent&, float windowToLogicalX, float windowToLogicalY, float wheelPixels);
// A pixel scroll at the current pointer position and modifiers.
WheelEvent pixelWheel(float x, float y, float windowToLogicalX, float windowToLogicalY);
// Hands an input event to the app; returns whether the frame may change.
bool send(App&, const SDL_Event&, float windowToLogicalX, float windowToLogicalY);

// Spreads each precise wheel delta over the time since the previous one, so display frames
// receive even shares of input that arrives at another rate. Wheel notches ease out instead.
class WheelResampler
{
public:
    void add(const WheelEvent&, double seconds);
    void ease(const WheelEvent&, double seconds);
    // Returns the deltas due by the given time, or nothing when none are.
    std::optional<WheelEvent> take(double seconds);
    bool active() const { return !pending.empty(); }

private:
    struct Share
    {
        float x, y;
        double start, duration, taken;
        bool eased;
    };
    std::deque<Share> pending;
    WheelEvent latest;
    double previous = -1;
};

// Tells mouse wheel notches from touchpad streams, which browsers report alike in pixels.
class WheelNotches
{
public:
    bool notch(float delta, double seconds);

private:
    double previous = -1, gap = 1;
};
}  // namespace tn
