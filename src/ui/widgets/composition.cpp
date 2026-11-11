#include <ui/ui.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace ui
{
namespace
{
bool validLength(const Length& value)
{
    return std::isfinite(value.basis) && value.basis >= -1 && std::isfinite(value.weight) && value.weight >= 0 && std::isfinite(value.minimum) && value.minimum >= 0 && !std::isnan(value.maximum) && value.maximum >= value.minimum;
}

bool equal(const Length& a, const Length& b)
{
    return a.basis == b.basis && a.weight == b.weight && a.minimum == b.minimum && a.maximum == b.maximum;
}

// An element without an ID is identified by its view type, or by its position under its parent.
std::string identity(const Element& node, std::string_view parent, size_t index)
{
    if (!node.id.empty()) return node.id;
    if (node.kind == ElementKind::View) return node.source;
    return parent.empty() ? "root" : std::string(parent) + "/" + std::to_string(index);
}

void identify(Element& node, std::string_view parent, size_t index)
{
    node.id = identity(node, parent, index);
    for (size_t i = 0; i < node.children.size(); ++i)
        identify(node.children[i], node.id, i);
}

bool check(const Element& node, const std::string& id, size_t depth, std::set<std::string>& ids, std::string& error)
{
    auto fail = [&](const char* reason) {
        error = (id.empty() ? "element" : id) + ": " + reason;
        return false;
    };
    if (depth > 64 || ids.size() >= 2048) return fail("composition exceeds its depth or element limit");
    // A positional ID changes when siblings move, which would detach retained state.
    if (node.id.empty() && node.kind != ElementKind::View && (node.kind == ElementKind::Input || node.kind == ElementKind::Menu || node.resizable)) return fail("inputs, menus and resizable elements require an ID");
    if (id.empty() || id.find('\0') != std::string::npos || !ids.insert(id).second) return fail("IDs must be nonempty and unique");
    if (!validLength(node.width) || !validLength(node.height)) return fail("invalid length constraint");
    if (!std::isfinite(node.gap) || node.gap < 0 || !std::isfinite(node.padding) || node.padding < 0) return fail("gap and padding must be finite nonnegative pixels");
    if (int(node.kind) < 0 || node.kind > ElementKind::Shape) return fail("invalid element kind");
    if (!std::isfinite(node.x) || !std::isfinite(node.y)) return fail("position must be finite");
    bool container = node.kind == ElementKind::Row || node.kind == ElementKind::Column || node.kind == ElementKind::Stack;
    if (!container && !node.children.empty()) return fail("only row, column and stack accept children");
    if (node.kind == ElementKind::View && node.source.empty()) return fail("view requires a source type");
    if (node.kind == ElementKind::Button && node.command.empty()) return fail("button requires a command ID");
    if (!node.icon.empty() && ((node.kind != ElementKind::Button && node.kind != ElementKind::Menu) || node.icon.find('\0') != std::string::npos)) return fail("icon requires a button or menu and a name without NUL bytes");
    if (node.active && node.kind != ElementKind::Menu && node.command.empty()) return fail("active requires a command or menu");
    if (node.compact && node.kind != ElementKind::Menu) return fail("compact is only available on menus");
    if (node.kind != ElementKind::Menu && !node.items.empty()) return fail("only menus accept items");
    const auto& a = node.appearance;
    if (!std::isfinite(a.radius) || a.radius < -1 || a.radius > 4096 || !std::isfinite(a.borderWidth) || a.borderWidth < 0 || a.borderWidth > 4096
        || !std::isfinite(a.fontSize) || a.fontSize < 0 || a.fontSize > 512) return fail("invalid appearance dimensions");
    if (!std::isfinite(node.opacity) || node.opacity < 0 || node.opacity > 1) return fail("opacity must be between 0 and 1");
    if (node.kind == ElementKind::Image && (node.source.empty() || node.source.find('\0') != std::string::npos)) return fail("image requires an asset name");
    if (int(node.fit) < 0 || node.fit > ImageFit::Stretch) return fail("invalid image fit");
    if (node.kind == ElementKind::Shape) {
        const auto& data = node.shape;
        if (int(data.kind) < 0 || data.kind > ShapeKind::Path) return fail("invalid shape kind");
        if (!std::isfinite(data.viewWidth) || data.viewWidth <= 0 || !std::isfinite(data.viewHeight) || data.viewHeight <= 0) return fail("invalid shape view box");
        if (data.kind == ShapeKind::Path && (data.path.empty() || data.path.size() > 4096 || data.path.front().command != PathCommand::Move)) return fail("path must start with M and contain at most 4096 segments");
        for (const auto& segment : data.path) {
            if (int(segment.command) < 0 || segment.command > PathCommand::Close) return fail("invalid path command");
            for (auto value : segment.points)
                if (!std::isfinite(value) || std::abs(value) > 1000000) return fail("invalid path coordinates");
        }
    }
    std::function<bool(const std::vector<MenuItem>&, size_t)> menus = [&](const auto& items, size_t level) {
        if (level > 32) return fail("menu exceeds its depth limit");
        for (const auto& item : items) {
            if (ids.size() >= 2048 || item.id.empty() || item.id.find('\0') != std::string::npos || !ids.insert(item.id).second) return fail("menu IDs must be nonempty and unique");
            if (item.title.empty() && (!item.command.empty() || !item.items.empty())) return fail("separator cannot contain actions");
            if (!item.title.empty() && item.command.empty() && item.items.empty()) return fail("menu item requires a command or submenu");
            if (!item.command.empty() && !item.items.empty()) return fail("menu item cannot contain both a command and submenu");
            if (!menus(item.items, level + 1)) return false;
        }
        return true;
    };
    if (!menus(node.items, 0)) return false;
    for (size_t i = 0; i < node.children.size(); ++i) {
        if (!check(node.children[i], identity(node.children[i], id, i), depth + 1, ids, error)) return false;
    }
    return true;
}

Element* find(Element& node, std::string_view id)
{
    if (node.id == id) return &node;
    for (auto& child : node.children) {
        if (auto found = find(child, id)) return found;
    }
    return nullptr;
}
}  // namespace

bool validate(const Element& root, std::string& error)
{
    error.clear();
    std::set<std::string> ids;
    return check(root, identity(root, {}, 0), 0, ids, error);
}

struct Composition::Impl
{
    // Requested sizes as applied, compared on the next apply() to keep or drop a user resize.
    struct Local
    {
        ElementKind kind;
        Length width, height;
        std::optional<float> resizedWidth, resizedHeight;
        TextInput input;
    };

    Element root;
    std::map<std::string, Local, std::less<>> states;
    std::shared_ptr<Images> images;

    void draw(Composition& owner, Context& context, const Element& node, Rect bounds, std::string_view prefix, const std::vector<ViewType>& types, const CommandRegistry& commands);
};

Composition::Composition() :
    impl(std::make_unique<Impl>()) {}
Composition::~Composition() = default;

Composition::Composition(const Composition& other) :
    openMenu(other.openMenu), menuAnchor(other.menuAnchor), tooltip(other.tooltip), resized(other.resized), impl(std::make_unique<Impl>(*other.impl)) {}

Composition& Composition::operator=(const Composition& other)
{
    if (this == &other) return *this;
    *impl = *other.impl;
    openMenu = other.openMenu;
    menuAnchor = other.menuAnchor;
    tooltip = other.tooltip;
    resized = other.resized;
    return *this;
}

bool Composition::apply(Element value, std::string& error)
{
    if (!validate(value, error)) return false;
    identify(value, {}, 0);
    std::map<std::string, Impl::Local, std::less<>> next;
    std::function<bool(Element&)> visit = [&](Element& node) {
        if (node.kind == ElementKind::Image && (!impl->images || !impl->images->contains(node.source))) {
            error = node.id + ": unknown image: " + node.source;
            return false;
        }
        Impl::Local state{node.kind, node.width, node.height, {}, {}, {}};
        auto previous = impl->states.find(node.id);
        if (previous != impl->states.end() && previous->second.kind == node.kind) {
            state.input = previous->second.input;
            if (equal(previous->second.width, node.width)) state.resizedWidth = previous->second.resizedWidth;
            if (equal(previous->second.height, node.height)) state.resizedHeight = previous->second.resizedHeight;
        }
        if (state.resizedWidth) node.width.basis = *state.resizedWidth;
        if (state.resizedHeight) node.height.basis = *state.resizedHeight;
        next.emplace(node.id, std::move(state));
        for (auto& child : node.children)
            if (!visit(child)) return false;
        return true;
    };
    if (!visit(value)) return false;
    impl->root = std::move(value);
    impl->states = std::move(next);
    return true;
}

bool Composition::images(std::vector<ImageAsset> assets, std::string& error)
{
    auto next = std::make_shared<Images>();
    if (!next->replace(std::move(assets), error)) return false;
    impl->images = std::move(next);
    return true;
}

const Element& Composition::definition() const
{
    return impl->root;
}

const TextInput* Composition::input(std::string_view id) const
{
    auto found = impl->states.find(id);
    return found == impl->states.end() || found->second.kind != ElementKind::Input ? nullptr : &found->second.input;
}

void Composition::Impl::draw(Composition& owner, Context& context, const Element& node, Rect bounds, std::string_view prefix, const std::vector<ViewType>& types, const CommandRegistry& commands)
{
    static constexpr const char* kinds[] = {"row", "column", "stack", "view", "label", "button", "input", "menu", "image", "shape"};
    auto instance = std::string(prefix) + "/" + node.id + "/" + (node.kind == ElementKind::View ? node.source : kinds[int(node.kind)]);
    const auto& a = node.appearance;
    const auto& t = context.theme();
    auto command = commands.find(node.command);
    bool enabled = node.enabled && (node.command.empty() || (command && command->enabled()));
    bool widget = node.kind == ElementKind::Button || node.kind == ElementKind::Menu || node.kind == ElementKind::Input;
    State state{false, false, false};
    if (!widget && !node.command.empty()) {
        state = context.control(instance, bounds, [&commands, id = node.command](const Event& event) {
            if (event.type == EventType::Activate || (event.type == EventType::Key && (event.key == Key::Enter || event.key == Key::Space))) commands.execute(id); }, enabled);
    }
    auto fill = resolve(a.fill, t, node.kind == ElementKind::Shape ? t.accent : Color{0, 0, 0, 0});
    if (node.active) fill = resolve(a.activeFill, t, fill);
    if (state.hovered) fill = resolve(a.hoverFill, t, fill);
    if (state.pressed) fill = resolve(a.pressedFill, t, fill);
    if (!enabled) fill = resolve(a.disabledFill, t, fill);
    auto border = resolve(a.border, t, {0, 0, 0, 0});
    auto ink = resolve(a.text, t, enabled ? t.text : t.muted);
    auto radius = a.radius >= 0 ? a.radius : t.radius;
    bool background = a.fill || a.border || a.hoverFill || a.pressedFill || a.activeFill || a.disabledFill;
    if (!widget && node.kind != ElementKind::Shape && background) context.rect(bounds, fill, radius, border, a.borderWidth);
    if (node.kind == ElementKind::View) {
        auto type = std::find_if(types.begin(), types.end(), [&](const ViewType& type) { return type.id == node.source; });
        if (type != types.end() && type->render) type->render(context, bounds, instance);
    } else if (node.kind == ElementKind::Label) {
        context.text(bounds.inset(node.padding), node.text, ink, a.fontSize, a.bold, a.textAlign.value_or(Align::Start));
    } else if (node.kind == ElementKind::Button) {
        auto title = node.text.empty() && node.icon.empty() && command ? std::string_view(command->title) : std::string_view(node.text);
        state = button(context, instance, bounds, title, [&commands, id = node.command] { commands.execute(id); }, {.style = node.style, .icon = node.icon, .enabled = enabled, .active = node.active, .appearance = a});
        if (state.hovered && owner.tooltip) owner.tooltip(node.tooltip.empty() && command ? command->title : node.tooltip, bounds);
    } else if (node.kind == ElementKind::Menu) {
        if (owner.menuAnchor) owner.menuAnchor(node.id, bounds.intersect(context.clip()));
        state = button(context, instance, bounds, node.text, [&owner, id = node.id, bounds] { if (owner.openMenu) owner.openMenu(id, bounds); }, {.style = node.style, .icon = node.icon, .enabled = node.enabled, .active = node.active, .appearance = a});
        if (state.hovered && owner.tooltip) owner.tooltip(node.tooltip, bounds);
    } else if (node.kind == ElementKind::Input) {
        textField(context, instance, bounds, states.at(node.id).input, node.text, node.enabled, {}, node.icon, a);
    } else if (node.kind == ElementKind::Shape) {
        shape(context, bounds.inset(node.padding), node.shape, fill, border, a.borderWidth, radius);
    } else if (node.kind == ElementKind::Image && images) {
        images->render(context, instance, node.source, bounds.inset(node.padding), node.fit, node.opacity);
    }
    if (!widget && state.focused && a.focusRing) context.focusRing(bounds.inset(4));
    if (!widget && state.hovered && owner.tooltip && !node.tooltip.empty()) owner.tooltip(node.tooltip, bounds);
}

void Composition::render(Context& context, std::string_view id, Rect bounds, const std::vector<ViewType>& types, const CommandRegistry& commands)
{
    if (impl->states.empty()) return;
    auto layout = arrange(bounds, impl->root);
    if (impl->images) impl->images->begin();
    context.clip(bounds, [&] {
        for (const auto& placement : layout.placements)
            context.clip(placement.clip, [&] { impl->draw(*this, context, *placement.element, placement.bounds, id, types, commands); });
        for (const auto& divider : layout.dividers) {
            const auto& target = divider.element->id;
            splitter(context, std::string(id) + "/resize/" + target, divider.bounds, divider.axis, divider.value, divider.minimum, divider.maximum, [this, target, axis = divider.axis](float value) {
                auto state = impl->states.find(target);
                auto element = find(impl->root, target);
                if (state == impl->states.end() || !element) return;
                (axis == Axis::Horizontal ? state->second.resizedWidth : state->second.resizedHeight) = value;
                (axis == Axis::Horizontal ? element->width : element->height).basis = value;
                if (resized) resized(target, value); }, divider.element->resizeEdge);
        }
    });
    if (impl->images) impl->images->end();
}

std::optional<MenuDefinition> Composition::menu(std::string_view id) const
{
    std::optional<MenuDefinition> result;
    std::function<void(const std::vector<MenuItem>&, bool)> items = [&](const auto& entries, bool compact) {
        for (const auto& entry : entries) {
            if (entry.id == id) result = MenuDefinition{entry.id, entry.title, entry.items, compact};
            items(entry.items, compact);
        }
    };
    std::function<void(const Element&)> visit = [&](const auto& node) {
        if (node.kind == ElementKind::Menu) {
            if (node.id == id) result = MenuDefinition{node.id, node.menuTitle, node.items, node.compact};
            items(node.items, node.compact);
        }
        for (const auto& child : node.children)
            visit(child);
    };
    visit(impl->root);
    return result;
}

}  // namespace ui
