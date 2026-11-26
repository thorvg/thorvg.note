#include "window_chrome.h"
#include <thornote/app/app_identity.h>
#include <algorithm>
#include <string>

namespace tn
{
void WindowChrome::begin(float width)
{
    windowWidth = width;
    header = {};
    areas.clear();
}

bool WindowChrome::draggable(const ui::Context& context, float x, float y) const
{
    if (context.controlAt(x, y)) return false;
    return header.contains(x, y) || std::any_of(areas.begin(), areas.end(), [&](ui::Rect r) { return r.contains(x, y); });
}

void WindowChrome::addDragArea(const ui::Context& context, ui::Rect r)
{
    areas.push_back(r.intersect(context.clip()));
}

void WindowChrome::drawDragArea(ui::Context& context, ui::Rect r)
{
    addDragArea(context, r);
    context.rect(r, context.theme().surface);
}

void WindowChrome::drawTitle(ui::Context& context, ui::Rect r, std::string_view title, bool modified, bool divider)
{
    const auto& theme = context.theme();
    drawDragArea(context, r);
    if (divider) context.line(r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h - 1, theme.border);
    header = r.intersect(context.clip());
    auto available = r.inset(4);
    float center = windowWidth / 2;
    float radius = std::max(0.0f, std::min(center - available.x, available.x + available.w - center));
    ui::Rect bounds{center - radius, available.y, radius * 2, available.h};
    auto fitted = context.elideText(title, bounds.w, 12, true, modified ? " ·" : "");
    context.text(bounds, fitted, focused ? theme.text : theme.muted, 12, true, ui::Align::Center);
}

void WindowChrome::drawIcon(ui::Context& context, ui::Rect r, bool divider)
{
    if (!window.customTitleBar || controls.leading()) return;
    const auto& theme = context.theme();
    context.rect(r, theme.surface);
    if (divider) context.line(r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h - 1, theme.border);
    addDragArea(context, r);
    if (!icon) icon.reset(createAppIconPicture());
    float size = std::max(0.0f, std::min({20.0f, r.w - 8, r.h - 8}));
    if (!icon || size <= 0) return;
    uint8_t opacity = focused ? 255 : 192;
    context.paint(ui::paintKey('A', r, size, opacity), [&]() -> tvg::Paint* {
        std::unique_ptr<tvg::Picture> picture(static_cast<tvg::Picture*>(icon->duplicate()));
        float width = 0, height = 0;
        if (!picture || picture->size(&width, &height) != tvg::Result::Success || width <= 0 || height <= 0) return nullptr;
        float scale = size / std::max(width, height);
        width *= scale;
        height *= scale;
        if (picture->size(width, height) != tvg::Result::Success) return nullptr;
        picture->translate(r.x + (r.w - width) / 2, r.y + (r.h - height) / 2);
        picture->opacity(opacity);
        return picture.release();
    });
}

void WindowChrome::drawButton(ui::Context& context, ui::Rect r, std::string_view instance, const char* command, Hint& hint)
{
    if (controls.systemDrawn()) {
        drawDragArea(context, r);
        return;
    }
    const auto& theme = context.theme();
    auto found = std::find_if(commands.commands().begin(), commands.commands().end(), [&](const auto& entry) { return entry.id == command; });
    bool enabled = found != commands.commands().end() && found->enabled();
    auto state = context.control(instance, r, [this, command = std::string(command)](const ui::Event& event) {
        if (event.activated()) commands.execute(command); }, enabled);
    bool close = std::string_view(command) == "window.close", minimize = std::string_view(command) == "window.minimize";
    context.rect(r, theme.surface);
    bool highlighted = state.hovered || state.pressed || state.focused;
    auto fill = highlighted ? (close ? theme.danger : state.pressed ? theme.border
                                                                    : theme.raised)
                            : theme.surface;
    if (highlighted) context.rect(r, fill);
    bool fullscreen = std::string_view(command) == "window.fullscreen";
    auto glyph = close ? "close" : minimize     ? "window-minimize"
        : window.maximized || window.fullscreen ? "window-restore"
        : fullscreen                            ? "window-fullscreen"
                                                : "window-maximize";
    auto ink = close && highlighted ? theme.accentText : theme.text;
    if (!enabled || !focused) ink = theme.muted;
    context.icon(std::string(instance) + "/icon", r, glyph, ink,
                 {state.hovered, state.pressed, false, enabled && focused, state.focused});
    if (state.hovered && found != commands.commands().end()) hint = {found->title, r};
}

void WindowChrome::drawControls(ui::Context& context, ui::Rect r, std::string_view instance, bool divider, Hint& hint)
{
    auto prefix = std::string(instance);
    auto zoom = zoomCommand();
    if (controls.systemDrawn()) drawDragArea(context, r);
    else {
        float cell = r.w / 3;
        drawButton(context, {r.x, r.y, cell, r.h}, prefix + "/minimize", "window.minimize", hint);
        drawButton(context, {r.x + cell, r.y, cell, r.h}, prefix + "/zoom", zoom, hint);
        drawButton(context, {r.x + 2 * cell, r.y, cell, r.h}, prefix + "/close", "window.close", hint);
    }
    if (divider) context.line(r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h - 1, context.theme().border);
}

void WindowChrome::drawBorder(ui::Context& context, float width, float height) const
{
    if (!window.customTitleBar || window.maximized || window.fullscreen) return;
    auto border = context.theme().border;
    if (!focused) border.a = 128;
    context.rect({0.5f, 0.5f, width - 1, height - 1}, {0, 0, 0, 0}, 0, border);
}

const char* WindowChrome::zoomCommand() const
{
    return window.fullscreen ? "window.fullscreen" : nativeWindowZoomCommand();
}
}  // namespace tn
