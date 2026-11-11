#pragma once

#include <cmath>
#include <cstdint>

namespace tn
{
class TitleBarGesture
{
public:
    enum class Action
    {
        None,
        Move,
        DoubleClick
    };
    explicit TitleBarGesture(uint32_t interval = 500, float radius = 4, float drag = 8) :
        interval(interval), radius(radius), drag(drag) {}
    void press(uint32_t time, float x, float y)
    {
        if (pressed && time == pressedAt) return;
        paired = clicked && uint32_t(time - clickedAt) < interval && std::abs(x - clickX) <= radius && std::abs(y - clickY) <= radius;
        pressed = true;
        moving = clicked = false;
        pressedAt = time;
        pressX = x;
        pressY = y;
    }
    Action motion(float x, float y)
    {
        if (!pressed || moving || (std::abs(x - pressX) < drag && std::abs(y - pressY) < drag)) return Action::None;
        moving = true;
        paired = false;
        return Action::Move;
    }
    Action release(float x, float y, bool inside)
    {
        if (!pressed) return Action::None;
        pressed = false;
        clicked = inside && !moving && std::abs(x - pressX) < drag && std::abs(y - pressY) < drag;
        if (!clicked) return Action::None;
        if (paired) {
            clicked = paired = false;
            return Action::DoubleClick;
        }
        clickedAt = pressedAt;
        clickX = pressX;
        clickY = pressY;
        return Action::None;
    }
    bool active() const { return pressed; }
    bool dragging() const { return moving; }
    void cancel() { pressed = moving = paired = clicked = false; }

private:
    uint32_t interval, pressedAt = 0, clickedAt = 0;
    float radius, drag, pressX = 0, pressY = 0, clickX = 0, clickY = 0;
    bool pressed = false, moving = false, paired = false, clicked = false;
};
}  // namespace tn
