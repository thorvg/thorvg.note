#include <ui/ui.h>
#include <algorithm>
#include <cmath>

namespace ui
{

void commandList(Context& context, std::string_view id, Rect bounds, const CommandRegistry& registry, CommandListState& state, std::function<void()> executed, CommandListOptions options)
{
    const auto& t = context.theme();
    auto matches = registry.matching(state.query.value);
    state.selected = matches.empty() ? 0 : std::min(state.selected, matches.size() - 1);
    auto prefix = std::string(id);
    context.control(prefix + "/scroll", bounds, [&registry, &state](const Event& event) {
        auto matches = registry.matching(state.query.value);
        if (event.type == EventType::Scroll && event.y != 0 && !matches.empty()) state.selected = size_t(std::clamp(int(state.selected) - (event.y > 0 ? 1 : -1), 0, int(matches.size()) - 1)); }, true, ControlFlag::Scrollable);
    textField(context, prefix + "/search", {bounds.x, bounds.y, bounds.w, 34}, state.query, options.placeholder, true, [&registry, &state, executed](Key key) {
        auto matches = registry.matching(state.query.value);
        if (matches.empty()) return key == Key::Enter || key == Key::Up || key == Key::Down;
        state.selected = std::min(state.selected, matches.size() - 1);
        if (key == Key::Down) state.selected = (state.selected + 1) % matches.size();
        else if (key == Key::Up) state.selected = (state.selected + matches.size() - 1) % matches.size();
        else if (key == Key::Enter) {
            auto commandId = registry.commands()[matches[state.selected]].id;
            if (registry.execute(commandId) && executed) executed();
        } else return false;
        return true; }, "search");
    constexpr float rowHeight = 43;
    size_t count = size_t(std::max(0.0f, std::floor((bounds.h - 43) / rowHeight)));
    size_t first = count > 0 && state.selected >= count ? state.selected - count + 1 : 0;
    if (matches.empty()) context.text({bounds.x, bounds.y + 50, bounds.w, 32}, options.emptyMessage, t.muted, 12, false, Align::Center);
    for (size_t i = first; i < std::min(matches.size(), first + count); ++i) {
        const auto& command = registry.commands()[matches[i]];
        Rect row{bounds.x, bounds.y + 43 + (i - first) * rowHeight, bounds.w, rowHeight - 2};
        auto stateId = prefix + "/" + command.id;
        auto interaction = context.control(stateId, row, [&registry, &state, commandId = command.id, i, executed](const Event& event) {
            if (event.type == EventType::Activate || (event.type == EventType::Key && event.key == Key::Enter)) {
                state.selected = i;
                if (registry.execute(commandId) && executed) executed();
            } }, command.enabled());
        if (state.selected == i || interaction.hovered) context.rect(row, state.selected == i ? t.selection : t.raised, 5);
        float shortcutWidth = command.shortcut.empty() ? 0 : std::min(row.w / 3, context.textWidth(command.shortcut, 10) + 12);
        context.text({row.x + 10, row.y + 1, row.w - 20 - shortcutWidth, 22}, command.title, command.enabled() ? t.text : t.muted, 12);
        context.text({row.x + 10, row.y + 23, row.w - 20, 15}, command.enabled() ? command.category : command.unavailableReason, t.muted, 9);
        context.text({row.x + row.w - shortcutWidth - 8, row.y + 1, shortcutWidth, 22}, command.shortcut, t.muted, 10, false, Align::End);
        if (interaction.focused) context.focusRing(row.inset(3));
    }
}

void commandPalette(Context& context, Rect viewport, const CommandRegistry& registry, CommandListState& state, std::function<void()> close, CommandListOptions options)
{
    const auto& t = context.theme();
    context.modal("palette/search", close);
    context.rect(viewport, {0, 0, 0, 125});
    context.control("palette/backdrop", viewport, [close](const Event& event) { if (event.type == EventType::Activate) close(); }, true, ControlFlag::None);
    float width = std::min(550.0f, viewport.w - 32);
    float top = options.title.empty() ? 12 : 43;
    Rect panel{viewport.x + (viewport.w - width) / 2, viewport.y + std::min(90.0f, viewport.h / 8), width, std::min(top + 356, viewport.h - 40)};
    context.rect(panel, t.surface, t.radius + 3, t.border);
    // An inert panel target blocks backdrop clicks in empty areas.
    context.control("palette/panel", panel, {}, true, ControlFlag::None);
    if (!options.title.empty()) context.text({panel.x + 12, panel.y + 10, panel.w - 24, 27}, options.title, t.text, 14, true);
    commandList(context, "palette", {panel.x + 12, panel.y + top, panel.w - 24, panel.h - top - 12}, registry, state, close, options);
}

}  // namespace ui
