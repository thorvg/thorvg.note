#pragma once

#include <thornote/documents/workspace.h>
#include <ui/ui.h>
#include <functional>
#include <optional>

namespace tn
{
// Document tabs, with the settings page as the last tab, that scroll when they overflow.
class TabStrip
{
public:
    TabStrip(const Workspace& workspace, ui::CommandRegistry& commands) :
        workspace(workspace), commands(commands) {}

    // A title bar strip has no background and ends with a new document button.
    void draw(ui::Context&, ui::Rect, bool titlebar, bool settingsOpen, bool settingsActive, bool notes, bool enabled);

    ui::TabOptions options{ui::TabStyle::Connected, 7, 6};
    // The settings tab has id zero.
    std::function<void(uint64_t)> activate, close;
    // A drag that scrolled the tabs ended.
    std::function<void()> dragged;

private:
    const Workspace& workspace;
    ui::CommandRegistry& commands;
    float x = 0, width = 0, origin = 0, limit = 0;
    size_t count = 0;
    uint64_t selected = 0, pressed = 0;
    std::optional<float> grab;
    bool dragging = false;
};
}  // namespace tn
