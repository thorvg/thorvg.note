#include <thornote/app/editor_app.h>
#include <ui/ui.h>

namespace tn
{
bool EditorApp::draggable(float x, float y) const
{
    return menu.empty() && !palette && prompt == Prompt::None && !dialog && chrome.draggable(context, x, y);
}

void EditorApp::drawTitleBar(ui::Rect r, bool headerOnly)
{
    auto state = layoutScriptState();
    state["headerOnly"] = headerOnly;
    state["titlebarWidth"] = double(r.w);
    layoutScript.refreshTitlebar(state);
    context.rect(r, theme.surface);
    // A bar taller than its 40 point row keeps the row at the bottom and drags the window above it.
    if (auto band = r.h - 40; band > 0) {
        chrome.drawDragArea(context, {r.x, r.y, r.w, band});
        r.y += band;
        r.h -= band;
    }
    auto& bar = layoutScript.titlebar();
    auto types = windowViewTypes(false);
    bar.render(context, "titlebar", r, types, commands);
    if (composition != Layout::Memo) {
        if (width >= 660) context.line(r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h, theme.border);
        else context.line(r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h - 1, theme.border);
    }
}

std::vector<ui::ViewType> EditorApp::windowViewTypes(bool divider)
{
    std::vector<ui::ViewType> types{
        {"window-icon", [this, divider](ui::Context&, ui::Rect r, std::string_view) {
             chrome.drawIcon(context, r, divider);
         }},
        {"window-controls", [this, divider](ui::Context&, ui::Rect r, std::string_view instance) {
             chrome.drawControls(context, r, instance, divider, hint);
         }},
        {"window-title", [this, divider](ui::Context&, ui::Rect r, std::string_view) {
             auto tab = visibleDocument();
             auto title = settings.active ? std::string("Settings") : tab ? tab->displayTitle(composition == Layout::Note)
                                                                          : std::string("ThorNote");
             chrome.drawTitle(context, r, title, tab && tab->dirty(), divider);
         }},
        {"window-tabs", [this](ui::Context& context, ui::Rect r, std::string_view) {
             drawTabs(context, r, true);
         }},
        {"window-drag", [this](ui::Context&, ui::Rect r, std::string_view) {
             chrome.drawDragArea(context, r);
         }},
    };
    for (auto action : {"close", "minimize", "maximize", "fullscreen"}) {
        std::string command = std::string("window.") + action;
        types.push_back({std::string("window-") + action, [this, command](ui::Context&, ui::Rect r, std::string_view instance) {
                             chrome.drawButton(context, r, instance, command.c_str(), hint);
                         }});
    }
    types.push_back({"window-zoom", [this](ui::Context&, ui::Rect r, std::string_view instance) {
                         chrome.drawButton(context, r, instance, chrome.zoomCommand(), hint);
                     }});
    return types;
}

void EditorApp::drawTabs(ui::Context& context, ui::Rect r, bool titlebar)
{
    if (titlebar) chrome.addDragArea(context, r);
    tabs.draw(context, r, titlebar, settings.open, settings.active, composition == Layout::Note, !dialog);
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

}  // namespace tn
