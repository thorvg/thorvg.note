#include <ui/ui.h>
#include <algorithm>
#include <cmath>

namespace ui
{

Rect scrollbarThumb(Rect track, float viewport, float content, float offset, Axis axis, float minimum)
{
    bool vertical = axis == Axis::Vertical;
    float length = vertical ? track.h : track.w;
    if (length <= 0 || viewport <= 0 || content <= viewport) return {};
    float thumb = std::min(length, std::max(minimum, length * viewport / content));
    float position = (length - thumb) * std::clamp(offset / (content - viewport), 0.0f, 1.0f);
    return vertical ? Rect{track.x, track.y + position, track.w, thumb} : Rect{track.x + position, track.y, thumb, track.h};
}

void scrollbar(Context& context, Rect track, float viewport, float content, float offset, float minimum)
{
    context.rect(scrollbarThumb(track, viewport, content, offset, Axis::Vertical, minimum), context.theme().border, track.w / 2);
}

void scrollbar(Context& context, std::string_view id, Rect track, float content, float& offset, std::optional<float>& grab, Axis axis, bool enabled)
{
    bool vertical = axis == Axis::Vertical;
    float length = vertical ? track.h : track.w;
    auto thumb = scrollbarThumb(track, length, content, offset, axis);
    if (thumb.w <= 0 || thumb.h <= 0) {
        grab.reset();
        return;
    }
    float start = vertical ? track.y : track.x;
    float position = vertical ? thumb.y : thumb.x;
    float size = vertical ? thumb.h : thumb.w;
    float travel = length - size, limit = content - length;
    auto state = context.control(id, track, [=, &offset, &grab](const Event& event) {
        float pointer = vertical ? event.y : event.x;
        if (event.type == EventType::Release || event.type == EventType::Cancel) grab.reset();
        else if (event.type == EventType::Press) grab = pointer >= position && pointer < position + size ? pointer - position : size / 2;
        if ((event.type == EventType::Press || event.type == EventType::Drag) && grab && travel > 0)
            offset = std::clamp((pointer - start - *grab) * limit / travel, 0.0f, limit); }, enabled, ControlFlag::Capture);
    context.rect(track, context.theme().surface);
    thumb = vertical ? Rect{thumb.x + (thumb.w - 4) / 2, thumb.y, 4, thumb.h}
                     : Rect{thumb.x, thumb.y + (thumb.h - 4) / 2, thumb.w, 4};
    context.rect(thumb, state.hovered || state.pressed ? context.theme().muted : context.theme().border, 2);
}

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
