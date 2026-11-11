#include <ui/ui.h>
#include <algorithm>
#include <cmath>

namespace ui
{

void tabSurface(Context& context, Rect bounds, bool selected, bool hovered, const TabOptions& options)
{
    if ((!selected && !hovered) || bounds.w <= 1 || bounds.h <= 1) return;
    const auto& theme = context.theme();
    float radius = options.radius < 0 ? std::max(0.0f, theme.radius - (options.style == TabStyle::Segmented ? 2 : 0)) : options.radius;
    radius = std::min(radius, std::min(bounds.w, bounds.h) / 2);
    if (options.style == TabStyle::Segmented) {
        context.rect(bounds, selected ? theme.selection : theme.raised, radius);
        return;
    }
    if (hovered && (!selected || options.style == TabStyle::Underline)) context.rect(bounds.inset(3), theme.raised, std::max(0.0f, radius - 2));
    if (!selected) return;
    if (options.style == TabStyle::Underline) {
        context.rect({bounds.x + 8, bounds.y + bounds.h - 2, bounds.w - 16, 2}, theme.accent, 1);
        return;
    }

    float foot = std::clamp(options.shoulder, 0.0f, bounds.h - radius);
    constexpr float curve = 0.55228475f;
    float left = bounds.x + 0.5f, right = bounds.x + bounds.w - 0.5f;
    float top = bounds.y + 0.5f, bottom = bounds.y + bounds.h;
    context.paint(paintKey('F', bounds, foot, radius, theme.background, theme.border), [&] {
        auto shape = tvg::Shape::gen();
        shape->moveTo(left - foot, bottom);
        shape->cubicTo(left - foot + foot * curve, bottom, left, bottom - foot + foot * curve, left, bottom - foot);
        shape->lineTo(left, top + radius);
        shape->cubicTo(left, top + radius - radius * curve, left + radius - radius * curve, top, left + radius, top);
        shape->lineTo(right - radius, top);
        shape->cubicTo(right - radius + radius * curve, top, right, top + radius - radius * curve, right, top + radius);
        shape->lineTo(right, bottom - foot);
        shape->cubicTo(right, bottom - foot + foot * curve, right + foot - foot * curve, bottom, right + foot, bottom);
        // Fill reaches the content edge; the open stroke omits a bottom border.
        shape->fill(theme.background.r, theme.background.g, theme.background.b, theme.background.a);
        shape->strokeWidth(1);
        shape->strokeFill(theme.border.r, theme.border.g, theme.border.b, theme.border.a);
        return shape;
    });
}

void splitter(Context& context, std::string_view id, Rect bounds, Axis axis, float value, float minimum, float maximum, std::function<void(float)> change, ResizeEdge edge)
{
    // Rebuild after each input; the next drag delta uses the divider's updated position.
    auto state = context.control(id, bounds, [=](const Event& event) {
        float direction = edge == ResizeEdge::Leading ? -1.0f : 1.0f;
        if (event.type == EventType::Drag) {
            float delta = axis == Axis::Horizontal ? event.x - (bounds.x + bounds.w / 2) : event.y - (bounds.y + bounds.h / 2);
            change(std::clamp(value + delta * direction, minimum, maximum));
        } else if (event.type == EventType::Key) {
            auto previous = axis == Axis::Horizontal ? Key::Left : Key::Up;
            auto next = axis == Axis::Horizontal ? Key::Right : Key::Down;
            if (event.key == previous || event.key == next) change(std::clamp(value + (event.key == previous ? -8 : 8) * direction, minimum, maximum));
        } }, true, ControlFlag::Focusable, axis == Axis::Horizontal ? Cursor::ResizeHorizontal : Cursor::ResizeVertical);
    if (state.hovered || state.pressed || state.focused) {
        auto line = axis == Axis::Horizontal ? Rect{bounds.x + bounds.w / 2 - 1, bounds.y, 2, bounds.h} : Rect{bounds.x, bounds.y + bounds.h / 2 - 1, bounds.w, 2};
        context.rect(line, context.theme().accent);
    }
}

}  // namespace ui
