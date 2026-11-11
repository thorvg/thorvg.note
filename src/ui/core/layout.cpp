#include <ui/ui.h>
#include <algorithm>

namespace ui
{

namespace
{
const Length& spec(const Element& node, Axis axis)
{
    return axis == Axis::Horizontal ? node.width : node.height;
}

bool container(const Element& node)
{
    return node.kind == ElementKind::Row || node.kind == ElementKind::Column || node.kind == ElementKind::Stack;
}

float lower(const Length& length)
{
    return std::max(0.0f, length.minimum);
}

float upper(const Length& length)
{
    return std::max(lower(length), length.maximum);
}

float minimum(const Element& node, Axis axis)
{
    if (!node.visible) return 0;
    auto& length = spec(node, axis);
    float content = 0;
    size_t count = 0;
    bool along = (node.kind == ElementKind::Row && axis == Axis::Horizontal) || (node.kind == ElementKind::Column && axis == Axis::Vertical);
    for (const auto& child : node.children) {
        if (!child.visible) continue;
        float value = minimum(child, axis);
        if (node.kind == ElementKind::Stack) value += std::max(0.0f, axis == Axis::Horizontal ? child.x : child.y);
        content = along ? content + value : std::max(content, value);
        ++count;
    }
    if (along && count > 1) content += (count - 1) * std::max(0.0f, node.gap);
    content += 2 * std::max(0.0f, node.padding);
    return std::clamp(content, lower(length), upper(length));
}

struct Allocation
{
    const Element* node;
    float minimum, maximum, size;
};

void allocate(std::vector<Allocation>& slots, Axis axis, float available, bool clipOverflow)
{
    float minima = 0, extraFixed = 0;
    for (auto& slot : slots) {
        const auto& length = spec(*slot.node, axis);
        slot.minimum = minimum(*slot.node, axis);
        slot.maximum = std::max(slot.minimum, upper(length));
        slot.size = slot.minimum;
        minima += slot.minimum;
        if (length.basis >= 0) extraFixed += std::clamp(length.basis, slot.minimum, slot.maximum) - slot.minimum;
    }
    if (minima > available) {
        if (!clipOverflow)
            for (auto& slot : slots)
                slot.size *= available / minima;
        return;
    }
    float remaining = available - minima;
    float fraction = extraFixed > 0 ? std::min(1.0f, remaining / extraFixed) : 0;
    for (auto& slot : slots) {
        const auto& length = spec(*slot.node, axis);
        if (length.basis < 0) continue;
        float added = (std::clamp(length.basis, slot.minimum, slot.maximum) - slot.minimum) * fraction;
        slot.size += added;
        remaining -= added;
    }
    // Redistribute the spare space when a weighted child reaches its maximum.
    for (size_t pass = 0; pass <= slots.size() && remaining > 0.0001f; ++pass) {
        float weights = 0;
        for (const auto& slot : slots) {
            const auto& length = spec(*slot.node, axis);
            if (length.basis < 0 && slot.size < slot.maximum) weights += std::max(0.0f, length.weight);
        }
        if (weights <= 0) break;
        float budget = remaining, used = 0;
        for (auto& slot : slots) {
            const auto& length = spec(*slot.node, axis);
            if (length.basis >= 0 || slot.size >= slot.maximum) continue;
            float added = std::min(slot.maximum - slot.size, budget * std::max(0.0f, length.weight) / weights);
            slot.size += added;
            used += added;
        }
        remaining -= used;
        if (used <= 0) break;
    }
}

void place(const Element& node, Rect bounds, Rect clip, Layout& result)
{
    if (!node.visible) return;
    clip = clip.intersect(bounds);
    result.placements.push_back({&node, bounds, clip});
    if (!container(node)) return;
    float px = std::min(std::max(0.0f, node.padding), bounds.w / 2);
    float py = std::min(std::max(0.0f, node.padding), bounds.h / 2);
    Rect inner{bounds.x + px, bounds.y + py, bounds.w - 2 * px, bounds.h - 2 * py};
    if (node.kind == ElementKind::Stack) {
        for (const auto& child : node.children) {
            const auto& w = child.width;
            const auto& h = child.height;
            float width = std::clamp(w.basis >= 0 ? w.basis : std::max(0.0f, inner.w - child.x), lower(w), upper(w));
            float height = std::clamp(h.basis >= 0 ? h.basis : std::max(0.0f, inner.h - child.y), lower(h), upper(h));
            place(child, {inner.x + child.x, inner.y + child.y, width, height}, clip, result);
        }
        return;
    }
    Axis axis = node.kind == ElementKind::Row ? Axis::Horizontal : Axis::Vertical;
    Axis cross = axis == Axis::Horizontal ? Axis::Vertical : Axis::Horizontal;
    float extent = axis == Axis::Horizontal ? inner.w : inner.h;
    float crossExtent = axis == Axis::Horizontal ? inner.h : inner.w;
    std::vector<Allocation> slots;
    for (const auto& child : node.children) {
        if (child.visible) slots.push_back({&child, 0, 0, 0});
    }
    if (slots.empty()) return;
    float gap = slots.size() > 1 ? (node.clipOverflow ? std::max(0.0f, node.gap) : std::clamp(node.gap, 0.0f, extent / (slots.size() - 1))) : 0;
    float available = std::max(0.0f, extent - gap * (slots.size() - 1));
    allocate(slots, axis, available, node.clipOverflow);
    float totalMinimum = 0;
    for (const auto& slot : slots)
        totalMinimum += slot.minimum;
    float cursor = 0;
    for (const auto& slot : slots) {
        const auto& child = *slot.node;
        const auto& length = spec(child, cross);
        float crossSize = std::min(crossExtent, std::clamp(length.basis >= 0 ? length.basis : crossExtent, minimum(child, cross), upper(length)));
        float offset = node.align == Align::Center ? (crossExtent - crossSize) / 2 : node.align == Align::End ? crossExtent - crossSize
                                                                                                              : 0;
        float size = node.clipOverflow ? slot.size : std::min(slot.size, std::max(0.0f, extent - cursor));
        Rect cell = axis == Axis::Horizontal ? Rect{inner.x + cursor, inner.y + offset, size, crossSize} : Rect{inner.x + offset, inner.y + cursor, crossSize, size};
        place(child, cell, clip, result);
        if (child.resizable && size > 0 && crossSize > 0) {
            bool leading = child.resizeEdge == ResizeEdge::Leading;
            float edge = cursor + (leading ? 0 : size);
            Rect handle = axis == Axis::Horizontal ? Rect{inner.x + edge - 3, cell.y, 6, cell.h} : Rect{cell.x, inner.y + edge - 3, cell.w, 6};
            float low = std::min(slot.minimum, size);
            float high = std::max(size, std::min(slot.maximum, available - (totalMinimum - slot.minimum)));
            if (high > low) result.dividers.push_back({&child, handle, axis, size, low, high});
        }
        cursor += size + gap;
    }
}
}  // namespace

Length fixed(float pixels)
{
    pixels = std::max(0.0f, pixels);
    return {pixels, 0, pixels, pixels};
}

Length preferred(float pixels, float minimum, float maximum)
{
    return {std::max(0.0f, pixels), 0, minimum, maximum};
}

Length flex(float weight, float minimum, float maximum)
{
    return {-1, weight, minimum, maximum};
}

Element view(std::string source, Length width, Length height)
{
    Element node;
    node.id = source;
    node.source = std::move(source);
    node.width = width;
    node.height = height;
    return node;
}

Element row(std::vector<Element> children, float gap)
{
    Element node;
    node.kind = ElementKind::Row;
    node.children = std::move(children);
    node.gap = gap;
    return node;
}

Element column(std::vector<Element> children, float gap)
{
    auto node = row(std::move(children), gap);
    node.kind = ElementKind::Column;
    return node;
}

Element stack(std::vector<Element> children)
{
    auto node = row(std::move(children));
    node.kind = ElementKind::Stack;
    return node;
}

const Placement* Layout::find(std::string_view id) const
{
    for (const auto& placement : placements) {
        if (placement.element->id == id) return &placement;
    }
    return nullptr;
}

Layout arrange(Rect bounds, const Element& root)
{
    Layout result;
    bounds.w = std::max(0.0f, bounds.w);
    bounds.h = std::max(0.0f, bounds.h);
    place(root, bounds, bounds, result);
    return result;
}

}  // namespace ui
