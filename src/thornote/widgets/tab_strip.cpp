#include "tab_strip.h"
#include <algorithm>
#include <cmath>

namespace tn
{
void TabStrip::draw(ui::Context& context, ui::Rect r, bool titlebar, bool settingsOpen, bool settingsActive, bool notes, bool enabled)
{
    const auto& theme = context.theme();
    if (!titlebar) {
        context.rect(r, theme.surface);
        context.line(r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h - 1, theme.border);
    }
    const auto& documents = workspace.documents();
    size_t total = documents.size() + settingsOpen;
    float edge = options.style == ui::TabStyle::Connected ? std::max(6.0f, options.shoulder) : 6;
    auto add = [&](float x) {
        ui::button(context, "tabs/new", {x, r.y + (r.h - 30) / 2, 30, 30}, "", [this] { commands.execute("file.new"); },
                   {.style = ui::ButtonStyle::Quiet, .icon = "new", .enabled = enabled});
    };
    if (!total) {
        x = width = origin = limit = 0;
        count = 0;
        selected = pressed = 0;
        grab.reset();
        dragging = false;
        if (titlebar) add(r.x + edge);
        return;
    }
    float available = std::max(1.0f, r.w - (titlebar ? 98 : 60) - 2 * edge);
    float minimum = titlebar ? std::min(128.0f, available) : 128;
    float cellWidth = std::min(210.0f, std::max(minimum, available / total));
    size_t current = settingsActive ? documents.size() : 0;
    size_t held = pressed == 0 && settingsOpen ? documents.size() : total;
    for (size_t i = 0; i < documents.size(); ++i) {
        if (!settingsActive && documents[i].get() == workspace.active()) current = i;
        if (documents[i]->id == pressed) held = i;
    }
    float content = total * cellWidth;
    float visible = std::min(content, available);
    float overflow = std::max(0.0f, content - visible);
    uint64_t currentId = current < documents.size() ? documents[current]->id : 0;
    if (selected != currentId || count != total || width != visible) {
        float left = current * cellWidth, right = left + cellWidth;
        if (left < x) x = left;
        if (right > x + visible) x = right - visible;
    }
    x = std::clamp(x, 0.0f, overflow);
    selected = currentId;
    count = total;
    width = visible;
    limit = overflow;
    if (held == total) grab.reset();
    auto interact = [this](uint64_t id, const ui::Event& event) {
        if (event.type == ui::EventType::Press) {
            grab = event.x;
            origin = x;
            pressed = id;
            dragging = false;
        } else if (event.type == ui::EventType::Drag && grab) {
            float distance = event.x - *grab;
            if (dragging || std::abs(distance) >= 4) {
                dragging = true;
                x = std::clamp(origin - distance, 0.0f, limit);
            }
        } else if (event.type == ui::EventType::Release || event.type == ui::EventType::Cancel) {
            grab.reset();
            if (dragging && dragged) dragged();
        } else if ((event.type == ui::EventType::Activate && !dragging) || (event.type == ui::EventType::Key && event.key == ui::Key::Enter)) {
            if (activate) activate(id);
        }
    };
    auto first = static_cast<size_t>(x / cellWidth);
    size_t end = std::min(total, size_t(std::ceil((x + visible) / cellWidth)));
    auto flags = ui::ControlFlag::Focusable | ui::ControlFlag::Capture;
    if (grab && (held < first || held >= end)) {
        // Keep receiving drag events after the pressed tab leaves the viewport.
        auto id = pressed;
        context.control("tabs/" + (id ? std::to_string(id) : "settings"), {}, [interact, id](const ui::Event& event) { interact(id, event); }, enabled, flags);
    }
    float top = options.style == ui::TabStyle::Connected ? 4 : options.style == ui::TabStyle::Segmented ? 2
                                                                                                        : 0;
    ui::Rect strip{r.x, r.y + top, visible + 2 * edge, r.h - top};
    context.scroll("tabs/scroll", strip, x, limit, {.step = 48, .horizontal = true, .enabled = enabled});
    context.clip({r.x, r.y, strip.w, r.h}, [&] {
        for (size_t i = first; i < end; ++i) {
            auto tab = i < documents.size() ? documents[i].get() : nullptr;
            uint64_t id = tab ? tab->id : 0;
            std::string key = tab ? std::to_string(id) : "settings";
            ui::Rect cell{r.x + edge + i * cellWidth - x, r.y + top, cellWidth - 2, r.h - top - (options.style == ui::TabStyle::Segmented ? 2 : 0)};
            auto state = context.control("tabs/" + key, {cell.x, cell.y, cell.w - 34, cell.h}, [interact, id](const ui::Event& event) { interact(id, event); }, enabled, flags);
            bool active = tab ? !settingsActive && tab == workspace.active() : settingsActive;
            ui::tabSurface(context, cell, active, state.hovered, options);
            context.text({cell.x + 12, r.y, cell.w - 52, r.h}, tab ? tab->displayTitle(notes) : "Settings", active ? theme.text : theme.muted, 11);
            ui::Rect button{cell.x + cell.w - 32, r.y + (r.h - 30) / 2, 30, 30};
            auto closing = context.control("tabs/close/" + key, button, [this, id](const ui::Event& e) {
            if (e.activated(false) && close) close(id); }, !tab || !tab->busy());
            if (closing.hovered) context.rect(button, theme.raised, 4);
            if (tab && tab->dirty()) context.icon("tabs/dirty/" + key, button, "dirty", theme.muted,
                                                  {.hovered = closing.hovered});
            else if (closing.hovered || closing.focused || active) context.icon("tabs/close/" + key + "/icon", button, "close", theme.muted,
                                                                                {closing.hovered, closing.pressed, false, !tab || !tab->busy(), closing.focused});
            if (state.focused) context.focusRing(cell.inset(5));
            if (i + 1 < total && i != current && i + 1 != current)
                context.line(cell.x + cellWidth - 1, r.y + r.h / 2 - 8, cell.x + cellWidth - 1, r.y + r.h / 2 + 8, theme.border);
        }
    });
    if (titlebar) add(r.x + edge + visible);
    if (limit > 0) {
        ui::button(context, "tabs/previous", {r.x + r.w - 64, r.y + (r.h - 30) / 2, 30, 30}, "", [this] { commands.execute("tab.previous"); }, {.style = ui::ButtonStyle::Quiet, .icon = "chevron-left"});
        ui::button(context, "tabs/next", {r.x + r.w - 34, r.y + (r.h - 30) / 2, 30, 30}, "", [this] { commands.execute("tab.next"); }, {.style = ui::ButtonStyle::Quiet, .icon = "chevron-right"});
    }
}
}  // namespace tn
