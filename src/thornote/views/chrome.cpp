#include <thornote/app/editor_app.h>
#include <thornote/app/app_identity.h>
#include <thornote/platform/native_window.h>
#include <ui/ui.h>
#include <algorithm>
#include <cmath>

namespace tn
{
bool EditorApp::customTitleBarVisible() const
{
    return customChrome;
}

bool EditorApp::windowDraggableAt(float x, float y) const
{
    if (!menu.empty() || palette || prompt != Prompt::None || dialog || context.controlAt(x, y)) return false;
    return headerDragBounds.contains(x, y) || std::any_of(windowDragBounds.begin(), windowDragBounds.end(), [&](ui::Rect r) { return r.contains(x, y); });
}

void EditorApp::drawWindowTitle(ui::Rect r, bool text, bool divider)
{
    windowDragBounds.push_back(r.intersect(context.clip()));
    context.rect(r, theme.surface);
    if (text) {
        if (divider) context.line(r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h - 1, theme.border);
        headerDragBounds = r.intersect(context.clip());
        auto tab = visibleDocument();
        auto title = settings.active ? std::string("Settings") : tab ? tab->displayTitle(composition == Layout::Note)
                                                                     : std::string("ThorNote");
        drawCenteredTitle(r.inset(4), title, 12, tab && tab->dirty());
    }
}

void EditorApp::drawCenteredTitle(ui::Rect available, std::string_view title, float size, bool modified)
{
    float center = width / 2;
    float radius = std::max(0.0f, std::min(center - available.x, available.x + available.w - center));
    ui::Rect bounds{center - radius, available.y, radius * 2, available.h};
    auto fitted = context.elideText(title, bounds.w, size, true, modified ? " ·" : "");
    context.text(bounds, fitted, windowFocused ? theme.text : theme.muted, size, true, ui::Align::Center);
}

void EditorApp::drawWindowButton(ui::Rect r, std::string_view instance, const char* command)
{
    if (windowControls.systemDrawn()) {
        drawWindowTitle(r, false);
        return;
    }
    auto found = std::find_if(commands.commands().begin(), commands.commands().end(), [&](const auto& entry) { return entry.id == command; });
    bool enabled = found != commands.commands().end() && found->enabled();
    auto state = context.control(instance, r, [this, command = std::string(command)](const ui::Event& event) {
        if (event.type == ui::EventType::Activate || (event.type == ui::EventType::Key && (event.key == ui::Key::Enter || event.key == ui::Key::Space))) commands.execute(command); }, enabled);
    bool close = std::string_view(command) == "window.close", minimize = std::string_view(command) == "window.minimize";
    context.rect(r, theme.surface);
    bool highlighted = state.hovered || state.pressed || state.focused;
    auto fill = highlighted ? (close ? theme.danger : state.pressed ? theme.border
                                                                    : theme.raised)
                            : theme.surface;
    if (highlighted) context.rect(r, fill);
    bool fullscreen = std::string_view(command) == "window.fullscreen";
    auto icon = close ? "close" : minimize    ? "window-minimize"
        : windowMaximized || windowFullscreen ? "window-restore"
        : fullscreen                          ? "window-fullscreen"
                                              : "window-maximize";
    auto ink = close && highlighted ? theme.accentText : theme.text;
    if (!enabled || !windowFocused) ink = theme.muted;
    context.icon(std::string(instance) + "/icon", r, icon, ink,
                 {state.hovered, state.pressed, false, enabled && windowFocused, state.focused});
    if (state.hovered && found != commands.commands().end()) {
        hint = found->title;
        hintBounds = r;
    }
}

void EditorApp::drawTitleBar(ui::Rect r, bool headerOnly)
{
    auto state = layoutScriptState();
    state["headerOnly"] = headerOnly;
    layoutScript.refreshTitlebar(state);
    context.rect(r, theme.surface);
    auto& bar = layoutScript.titlebar();
    auto types = windowViewTypes(false);
    bar.render(context, "titlebar", r, types, commands);
    if (composition != Layout::Memo) context.line(r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h - 1, theme.border);
}

void EditorApp::drawAppIcon(ui::Rect r, bool divider)
{
    if (!customTitleBarVisible() || windowControls.leading()) return;
    context.rect(r, theme.surface);
    if (divider) context.line(r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h - 1, theme.border);
    windowDragBounds.push_back(r.intersect(context.clip()));
    if (!titleBarIcon) titleBarIcon.reset(createAppIconPicture());
    float size = std::max(0.0f, std::min({20.0f, r.w - 8, r.h - 8}));
    if (!titleBarIcon || size <= 0) return;
    uint8_t opacity = windowFocused ? 255 : 192;
    context.paint(ui::paintKey('A', r, size, opacity), [&]() -> tvg::Paint* {
        std::unique_ptr<tvg::Picture> icon(static_cast<tvg::Picture*>(titleBarIcon->duplicate()));
        float width = 0, height = 0;
        if (!icon || icon->size(&width, &height) != tvg::Result::Success || width <= 0 || height <= 0) return nullptr;
        float scale = size / std::max(width, height);
        width *= scale;
        height *= scale;
        if (icon->size(width, height) != tvg::Result::Success) return nullptr;
        icon->translate(r.x + (r.w - width) / 2, r.y + (r.h - height) / 2);
        icon->opacity(opacity);
        return icon.release();
    });
}

void EditorApp::drawWindowControls(ui::Rect r, std::string_view instance, bool divider)
{
    auto prefix = std::string(instance);
    auto zoom = windowFullscreen ? "window.fullscreen" : nativeWindowZoomCommand();
    if (windowControls.systemDrawn()) drawWindowTitle(r, false);
    else {
        float cell = r.w / 3;
        drawWindowButton({r.x, r.y, cell, r.h}, prefix + "/minimize", "window.minimize");
        drawWindowButton({r.x + cell, r.y, cell, r.h}, prefix + "/zoom", zoom);
        drawWindowButton({r.x + 2 * cell, r.y, cell, r.h}, prefix + "/close", "window.close");
    }
    if (divider) context.line(r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h - 1, theme.border);
}

std::vector<ui::ViewType> EditorApp::windowViewTypes(bool divider)
{
    std::vector<ui::ViewType> types{
        {"window-icon", [this, divider](ui::Context&, ui::Rect r, std::string_view) {
             drawAppIcon(r, divider);
         }},
        {"window-controls", [this, divider](ui::Context&, ui::Rect r, std::string_view instance) {
             drawWindowControls(r, instance, divider);
         }},
        {"window-title", [this, divider](ui::Context&, ui::Rect r, std::string_view) {
             drawWindowTitle(r, true, divider);
         }},
        {"window-tabs", [this](ui::Context& context, ui::Rect r, std::string_view) {
             drawTabs(context, r, true);
         }},
        {"window-drag", [this, divider](ui::Context&, ui::Rect r, std::string_view) {
             drawWindowTitle(r, false, divider);
         }},
    };
    for (auto action : {"close", "minimize", "maximize", "fullscreen"}) {
        std::string command = std::string("window.") + action;
        types.push_back({std::string("window-") + action, [this, command](ui::Context&, ui::Rect r, std::string_view instance) {
                             drawWindowButton(r, instance, command.c_str());
                         }});
    }
    types.push_back({"window-zoom", [this](ui::Context&, ui::Rect r, std::string_view instance) {
                         drawWindowButton(r, instance, windowFullscreen ? "window.fullscreen" : nativeWindowZoomCommand());
                     }});
    return types;
}

void EditorApp::drawHeader(ui::Rect r)
{
    drawTitleBar(r, true);
}

void EditorApp::drawTabs(ui::Context& context, ui::Rect r, bool titlebar)
{
    if (titlebar) windowDragBounds.push_back(r.intersect(context.clip()));
    else {
        context.rect(r, theme.surface);
        context.line(r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h - 1, theme.border);
    }
    const auto& documents = workspace.documents();
    size_t total = documents.size() + settings.open;
    float edge = tabOptions.style == ui::TabStyle::Connected ? std::max(6.0f, tabOptions.shoulder) : 6;
    auto add = [&](float x) {
        ui::button(context, "tabs/new", {x, r.y + (r.h - 30) / 2, 30, 30}, "", [this] { commands.execute("file.new"); },
                   {.style = ui::ButtonStyle::Quiet, .icon = "new", .enabled = !dialog});
    };
    if (!total) {
        tabScroll = {};
        if (titlebar) add(r.x + edge);
        return;
    }
    float available = std::max(1.0f, r.w - (titlebar ? 98 : 60) - 2 * edge);
    float minimum = titlebar ? std::min(128.0f, available) : 128;
    float cellWidth = std::min(210.0f, std::max(minimum, available / total));
    size_t selected = settings.active ? documents.size() : 0;
    size_t pressed = tabScroll.pressed == 0 && settings.open ? documents.size() : total;
    for (size_t i = 0; i < documents.size(); ++i) {
        if (!settings.active && documents[i].get() == workspace.active()) selected = i;
        if (documents[i]->id == tabScroll.pressed) pressed = i;
    }
    float content = total * cellWidth;
    float visible = std::min(content, available);
    float limit = std::max(0.0f, content - visible);
    uint64_t selectedId = selected < documents.size() ? documents[selected]->id : 0;
    if (tabScroll.selected != selectedId || tabScroll.count != total || tabScroll.width != visible) {
        float left = selected * cellWidth, right = left + cellWidth;
        if (left < tabScroll.x) tabScroll.x = left;
        if (right > tabScroll.x + visible) tabScroll.x = right - visible;
    }
    tabScroll.x = std::clamp(tabScroll.x, 0.0f, limit);
    tabScroll.selected = selectedId;
    tabScroll.count = total;
    tabScroll.width = visible;
    tabScroll.limit = limit;
    if (pressed == total) tabScroll.grab.reset();
    auto interact = [this](uint64_t id, const ui::Event& event) {
        if (event.type == ui::EventType::Press) {
            tabScroll.grab = event.x;
            tabScroll.origin = tabScroll.x;
            tabScroll.pressed = id;
            tabScroll.dragging = false;
        } else if (event.type == ui::EventType::Drag && tabScroll.grab) {
            float distance = event.x - *tabScroll.grab;
            if (tabScroll.dragging || std::abs(distance) >= 4) {
                tabScroll.dragging = true;
                tabScroll.x = std::clamp(tabScroll.origin - distance, 0.0f, tabScroll.limit);
            }
        } else if (event.type == ui::EventType::Release || event.type == ui::EventType::Cancel) {
            tabScroll.grab.reset();
            if (tabScroll.dragging) focusEditor(true);
        } else if ((event.type == ui::EventType::Activate && !tabScroll.dragging) || (event.type == ui::EventType::Key && event.key == ui::Key::Enter)) {
            if (id) activateDocument(id);
            else openSettings();
        }
    };
    size_t first = size_t(tabScroll.x / cellWidth);
    size_t end = std::min(total, size_t(std::ceil((tabScroll.x + visible) / cellWidth)));
    auto flags = ui::ControlFlag::Focusable | ui::ControlFlag::Capture;
    if (tabScroll.grab && (pressed < first || pressed >= end)) {
        // Keep receiving drag events after the pressed tab leaves the viewport.
        auto id = tabScroll.pressed;
        context.control("tabs/" + (id ? std::to_string(id) : "settings"), {}, [interact, id](const ui::Event& event) { interact(id, event); }, !dialog, flags);
    }
    float top = tabOptions.style == ui::TabStyle::Connected ? 4 : tabOptions.style == ui::TabStyle::Segmented ? 2
                                                                                                              : 0;
    ui::Rect strip{r.x, r.y + top, visible + 2 * edge, r.h - top};
    context.scroll("tabs/scroll", strip, tabScroll.x, tabScroll.limit, {.step = 48, .horizontal = true, .enabled = !dialog});
    context.clip({r.x, r.y, strip.w, r.h}, [&] {
        for (size_t i = first; i < end; ++i) {
            auto tab = i < documents.size() ? documents[i].get() : nullptr;
            uint64_t id = tab ? tab->id : 0;
            std::string key = tab ? std::to_string(id) : "settings";
            ui::Rect cell{r.x + edge + i * cellWidth - tabScroll.x, r.y + top, cellWidth - 2, r.h - top - (tabOptions.style == ui::TabStyle::Segmented ? 2 : 0)};
            auto state = context.control("tabs/" + key, {cell.x, cell.y, cell.w - 34, cell.h}, [interact, id](const ui::Event& event) { interact(id, event); }, !dialog, flags);
            bool active = tab ? !settings.active && tab == workspace.active() : settings.active;
            ui::tabSurface(context, cell, active, state.hovered, tabOptions);
            context.text({cell.x + 12, r.y, cell.w - 52, r.h}, tab ? tab->displayTitle(composition == Layout::Note) : "Settings", active ? theme.text : theme.muted, 11);
            ui::Rect close{cell.x + cell.w - 32, r.y + (r.h - 30) / 2, 30, 30};
            auto closing = context.control("tabs/close/" + key, close, [this, id](const ui::Event& e) {
            if (e.type == ui::EventType::Activate || (e.type == ui::EventType::Key && e.key == ui::Key::Enter)) { if (id) requestClose(id); else closeSettings(); } }, !tab || !tab->busy());
            if (closing.hovered) context.rect(close, theme.raised, 4);
            if (closing.hovered || closing.focused || active) context.icon("tabs/close/" + key + "/icon", close, "close", theme.muted,
                                                                           {closing.hovered, closing.pressed, false, !tab || !tab->busy(), closing.focused});
            else if (tab && tab->dirty()) context.icon("tabs/dirty/" + key, close, "dirty", theme.muted,
                                                       {state.hovered, state.pressed, true, true, state.focused});
            if (state.focused) context.focusRing(cell.inset(5));
            if (i + 1 < total && i != selected && i + 1 != selected)
                context.line(cell.x + cellWidth - 1, r.y + r.h / 2 - 8, cell.x + cellWidth - 1, r.y + r.h / 2 + 8, theme.border);
        }
    });
    if (titlebar) add(r.x + edge + visible);
    if (limit > 0) {
        ui::button(context, "tabs/previous", {r.x + r.w - 64, r.y + (r.h - 30) / 2, 30, 30}, "", [this] { commands.execute("tab.previous"); }, {.style = ui::ButtonStyle::Quiet, .icon = "chevron-left"});
        ui::button(context, "tabs/next", {r.x + r.w - 34, r.y + (r.h - 30) / 2, 30, 30}, "", [this] { commands.execute("tab.next"); }, {.style = ui::ButtonStyle::Quiet, .icon = "chevron-right"});
    }
}

void EditorApp::openEditorMenu(float x, float y)
{
    auto tab = visibleDocument();
    if (!tab || tab->loading) return;
    context.cancelPointer();
    // A secondary click on plain text selects the word under it, as a double click does.
    if (tab->editor.session().selection().empty()) {
        context.pointerDown(x, y, {}, 2);
        context.cancelPointer();
    }
    focusEditor(true);
    if (showTextMenu && showTextMenu(x, y, tab->editor.session().selectedText(), commands)) return;
    contextMenuAnchor = menuButton = ui::Rect{x, y, 1, 1};
    openMenu("editor-menu");
}

void EditorApp::openMenu(std::string_view value)
{
    if (dialog || prompt != Prompt::None || palette) return;
    if (!layoutScript.menu(value)) return;
    if (menuRoot.empty()) menuRoot = value.substr(0, value.find('/'));
    menu = value;
    menuSelection = 0;
    focusEditor(false);
}

void EditorApp::closeMenu(bool preserveFolder)
{
    menu.clear();
    menuRoot.clear();
    contextMenuAnchor.reset();
    if (!preserveFolder) menuFolder.clear();
    focusEditor(!palette && prompt == Prompt::None && !dialog);
}

void EditorApp::activateMenu(size_t index)
{
    if (index >= menuItems.size()) return;
    auto item = menuItems[index];
    if (!item.enabled) return;
    if (!item.items.empty()) {
        openMenu(item.id);
        return;
    }
    for (const auto& command : commands.commands()) {
        if (command.id == item.command && command.enabled()) {
            closeMenu(true);
            commands.execute(item.command);
            menuFolder.clear();
            break;
        }
    }
}

void EditorApp::menuKey(SDL_Keycode key, bool shift)
{
    if (key == SDLK_ESCAPE || key == SDLK_LEFT) {
        if (menu == menuRoot) closeMenu();
        else openMenu(menu.substr(0, menu.rfind('/')));
    } else if (key == SDLK_RIGHT) {
        if (menuSelection < menuItems.size() && !menuItems[menuSelection].items.empty()) activateMenu(menuSelection);
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) activateMenu(menuSelection);
    else if ((key == SDLK_UP || key == SDLK_DOWN || key == SDLK_TAB) && !menuItems.empty()) {
        int direction = key == SDLK_UP || (key == SDLK_TAB && shift) ? -1 : 1;
        for (size_t i = 0; i < menuItems.size(); ++i) {
            menuSelection = size_t((int(menuSelection) + direction + int(menuItems.size())) % int(menuItems.size()));
            const auto& item = menuItems[menuSelection];
            if (!item.enabled || item.title.empty()) continue;
            if (!item.items.empty()) break;
            auto command = std::find_if(commands.commands().begin(), commands.commands().end(), [&](const auto& value) { return value.id == item.command; });
            if (command != commands.commands().end() && command->enabled()) break;
        }
    }
}

void EditorApp::drawMenu()
{
    auto definition = layoutScript.menu(menu);
    if (!definition) {
        closeMenu();
        return;
    }
    auto title = definition->title;
    bool compact = definition->compact;
    menuItems = std::move(definition->items);
    menuSelection = menuItems.empty() ? 0 : std::min(menuSelection, menuItems.size() - 1);
    float padding = compact ? 4 : 8, inset = compact ? 3 : 6, rowHeight = compact ? 24 : 32;
    float separator = compact ? 8 : 12, radius = compact ? 4 : 9;
    float panelHeight = title.empty() ? padding * 2 : compact ? 40
                                                              : 54;
    for (const auto& item : menuItems)
        panelHeight += item.title.empty() ? separator : rowHeight;
    float menuWidth = std::min(compact ? 128.0f : 296.0f, std::max(0.0f, width - 16));
    menuBounds = {std::clamp(menuButton.x, 8.0f, std::max(8.0f, width - menuWidth - 8)), menuButton.y + menuButton.h + 9, menuWidth, panelHeight};
    if (contextMenuAnchor) menuBounds.y = std::clamp(menuBounds.y, 8.0f, std::max(8.0f, height - panelHeight - 8));
    context.modal("menu/item/0", [this] { closeMenu(); });
    context.control("menu/backdrop", {0, 0, width, height}, [this](const ui::Event& e) { if (e.type == ui::EventType::Activate) closeMenu(); }, true, ui::ControlFlag::None);
    float shadow = compact ? 1 : 2;
    context.rect({menuBounds.x - shadow, menuBounds.y + shadow + 1, menuBounds.w + shadow * 2, menuBounds.h + shadow}, {0, 0, 0, 25}, radius + 1);
    context.rect(menuBounds, theme.surface, radius, theme.border);
    context.control("menu/panel", menuBounds, {}, true, ui::ControlFlag::None);
    float y = menuBounds.y + padding;
    if (!title.empty()) {
        float back = compact ? 24 : 30, titleInset = compact ? 32 : 42;
        ui::button(context, "menu/back", {menuBounds.x + padding, y, back, back}, "", [this] { menuKey(SDLK_LEFT, false); }, {.style = ui::ButtonStyle::Quiet, .icon = "chevron-left"});
        context.text({menuBounds.x + titleInset, y, menuBounds.w - titleInset * 2, back}, title, theme.text, 12, true, ui::Align::Center);
        y += back + padding;
        context.line(menuBounds.x + 12, y, menuBounds.x + menuBounds.w - 12, y, theme.border);
        y += padding;
    }
    menuRows.clear();
    for (size_t i = 0; i < menuItems.size(); ++i) {
        const auto& item = menuItems[i];
        ui::Rect row{menuBounds.x + inset, y, menuBounds.w - inset * 2, item.title.empty() ? separator : rowHeight};
        menuRows.push_back(row);
        if (item.title.empty()) {
            context.line(row.x + 8, row.y + separator / 2, row.x + row.w - 8, row.y + separator / 2, theme.border);
            y += separator;
            continue;
        }
        bool enabled = item.enabled;
        std::string detail = item.detail;
        for (const auto& command : commands.commands())
            if (command.id == item.command) {
                enabled = enabled && command.enabled();
                if (detail.empty()) detail = ui::platform::shortcutLabel(command.shortcut);
                break;
            }
        auto state = context.control("menu/item/" + std::to_string(i), row, [this, i](const ui::Event& e) {
            if (e.type == ui::EventType::Activate || (e.type == ui::EventType::Key && e.key == ui::Key::Enter)) activateMenu(i); }, enabled);
        if (state.hovered || i == menuSelection) context.rect(row, theme.selection, compact ? 2 : 5);
        auto iconId = "menu/" + item.id;
        if (item.checked || !item.icon.empty())
            context.icon(iconId + "/icon", {row.x + 4, row.y, 20, row.h}, item.checked ? "check" : std::string_view(item.icon),
                         item.checked && enabled ? theme.accent : enabled ? theme.text
                                                                          : theme.muted,
                         {state.hovered, state.pressed, item.checked, enabled, state.focused || i == menuSelection});
        float textInset = compact && !item.checked && item.icon.empty() ? 10 : 28;
        float detailWidth = compact ? std::min(row.w * 0.4f, context.textWidth(detail, 10)) : 80;
        float trailing = compact ? 8 + (item.items.empty() ? 0 : 20) + (detail.empty() ? 0 : detailWidth + 12) : 98;
        float detailInset = compact && item.items.empty() ? 8 : 24;
        context.text({row.x + textInset, row.y, std::max(0.0f, row.w - textInset - trailing), row.h}, item.title, enabled ? theme.text : theme.muted, compact ? 11 : 12);
        context.text({row.x + row.w - detailInset - detailWidth, row.y, detailWidth, row.h}, detail, theme.muted, 10, false, ui::Align::End);
        if (!item.items.empty()) context.icon(iconId + "/submenu", {row.x + row.w - 22, row.y, 20, row.h}, "chevron-right", theme.muted,
                                              {state.hovered, state.pressed, false, enabled, state.focused || i == menuSelection});
        y += rowHeight;
    }
}

}  // namespace tn
