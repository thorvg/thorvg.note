#include <ui/ui.h>
#include <algorithm>

namespace ui
{

void menu(Context& context, std::string_view id, Rect viewport, Rect anchor, const MenuDefinition& definition, size_t& selection,
          const CommandRegistry& commands, std::function<void(size_t)> activate, std::function<void()> back, std::function<void()> close, bool constrainVertical)
{
    auto prefix = std::string(id);
    const auto& theme = context.theme();
    const auto& title = definition.title;
    bool compact = definition.compact;
    const auto& items = definition.items;
    selection = items.empty() ? 0 : std::min(selection, items.size() - 1);
    auto available = [&items, &commands](size_t index) {
        const auto& item = items[index];
        auto command = commands.find(item.command);
        return !item.title.empty() && item.enabled && (!item.items.empty() || (command && command->enabled()));
    };
    auto keyboard = [&items, &selection, available, activate, back](const Event& event) {
        if (event.type != EventType::Key) return;
        auto key = event.key;
        if (key == Key::Escape || key == Key::Left) back();
        else if (key == Key::Right) {
            if (selection < items.size() && available(selection) && !items[selection].items.empty()) activate(selection);
        } else if (event.activated() && selection < items.size() && available(selection)) activate(selection);
        else if ((key == Key::Up || key == Key::Down || key == Key::Tab) && !items.empty()) {
            int direction = key == Key::Up || (key == Key::Tab && event.modifiers.shift) ? -1 : 1;
            for (size_t i = 0; i < items.size(); ++i) {
                selection = size_t((int(selection) + direction + int(items.size())) % int(items.size()));
                if (available(selection)) break;
            }
        }
    };
    float padding = compact ? 4 : 8, inset = compact ? 3 : 6, rowHeight = compact ? 24 : 32;
    float separator = compact ? 8 : 12, radius = compact ? 4 : 9;
    float panelHeight = title.empty() ? padding * 2 : compact ? 40
                                                              : 54;
    for (const auto& item : definition.items)
        panelHeight += item.title.empty() ? separator : rowHeight;
    float menuWidth = std::min(compact ? 128.0f : 296.0f, std::max(0.0f, viewport.w - 16));
    Rect menuBounds = {std::clamp(anchor.x, viewport.x + 8, std::max(viewport.x + 8, viewport.x + viewport.w - menuWidth - 8)), anchor.y + anchor.h + 9, menuWidth, panelHeight};
    if (constrainVertical) menuBounds.y = std::clamp(menuBounds.y, viewport.y + 8, std::max(viewport.y + 8, viewport.y + viewport.h - panelHeight - 8));
    context.modal(prefix + "/panel", back);
    context.control(prefix + "/backdrop", viewport, [close](const Event& event) { if (event.type == EventType::Activate) close(); }, true, ControlFlag::None);
    float shadow = compact ? 1 : 2;
    context.rect({menuBounds.x - shadow, menuBounds.y + shadow + 1, menuBounds.w + shadow * 2, menuBounds.h + shadow}, {0, 0, 0, 25}, radius + 1);
    context.rect(menuBounds, theme.surface, radius, theme.border);
    context.control(prefix + "/panel", menuBounds, keyboard, true, ControlFlag::Focusable | ControlFlag::TabInput);
    context.focus(prefix + "/panel");
    float y = menuBounds.y + padding;
    if (!title.empty()) {
        float backSize = compact ? 24 : 30, titleInset = compact ? 32 : 42;
        button(context, prefix + "/back", {menuBounds.x + padding, y, backSize, backSize}, "", back, {.style = ButtonStyle::Quiet, .icon = "chevron-left"});
        context.text({menuBounds.x + titleInset, y, menuBounds.w - titleInset * 2, backSize}, title, theme.text, 12, true, Align::Center);
        y += backSize + padding;
        context.line(menuBounds.x + 12, y, menuBounds.x + menuBounds.w - 12, y, theme.border);
        y += padding;
    }
    for (size_t i = 0; i < definition.items.size(); ++i) {
        const auto& item = definition.items[i];
        Rect row{menuBounds.x + inset, y, menuBounds.w - inset * 2, item.title.empty() ? separator : rowHeight};
        if (item.title.empty()) {
            context.line(row.x + 8, row.y + separator / 2, row.x + row.w - 8, row.y + separator / 2, theme.border);
            y += separator;
            continue;
        }
        bool enabled = available(i);
        auto command = commands.find(item.command);
        auto detail = item.detail.empty() && command ? platform::shortcutLabel(command->shortcut) : item.detail;
        auto state = context.control(prefix + "/item/" + std::to_string(i), row, [&selection, keyboard, activate, i](const Event& event) {
            if (event.type == EventType::Hover || event.type == EventType::Press) selection = i;
            else if (event.type == EventType::Key) keyboard(event);
            else if (event.activated()) activate(i); }, enabled, ControlFlag::Focusable | ControlFlag::TabInput | ControlFlag::Hover);
        if (state.hovered || i == selection) context.rect(row, theme.selection, compact ? 2 : 5);
        auto iconId = prefix + "/" + item.id;
        if (item.checked || !item.icon.empty())
            context.icon(iconId + "/icon", {row.x + 4, row.y, 20, row.h}, item.checked ? "check" : std::string_view(item.icon),
                         item.checked && enabled ? theme.accent : enabled ? theme.text
                                                                          : theme.muted,
                         {state.hovered, state.pressed, item.checked, enabled, state.focused || i == selection});
        float textInset = compact && !item.checked && item.icon.empty() ? 10 : 28;
        float detailWidth = compact ? std::min(row.w * 0.4f, context.textWidth(detail, 10)) : 80;
        float trailing = compact ? 8 + (item.items.empty() ? 0 : 20) + (detail.empty() ? 0 : detailWidth + 12) : 98;
        float detailInset = compact && item.items.empty() ? 8 : 24;
        context.text({row.x + textInset, row.y, std::max(0.0f, row.w - textInset - trailing), row.h}, item.title, enabled ? theme.text : theme.muted, compact ? 11 : 12);
        context.text({row.x + row.w - detailInset - detailWidth, row.y, detailWidth, row.h}, detail, theme.muted, 10, false, Align::End);
        if (!item.items.empty()) context.icon(iconId + "/submenu", {row.x + row.w - 22, row.y, 20, row.h}, "chevron-right", theme.muted,
                                              {state.hovered, state.pressed, false, enabled, state.focused || i == selection});
        y += rowHeight;
    }
}

}  // namespace ui
