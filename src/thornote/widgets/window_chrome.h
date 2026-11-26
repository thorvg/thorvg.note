#pragma once

#include <thornote/shell/shell.h>
#include <thornote/widgets/hint.h>
#include <ui/ui.h>
#include <memory>
#include <string_view>
#include <vector>

namespace tn
{
// The title bar parts the app draws in place of the system: the icon, the title, the window buttons
// and the areas that move the window.
class WindowChrome
{
public:
    explicit WindowChrome(ui::CommandRegistry& commands) :
        commands(commands) {}

    // Starts a frame, forgetting the areas of the previous one.
    void begin(float width);
    // Whether a press at the point moves the window.
    bool draggable(const ui::Context&, float x, float y) const;
    void addDragArea(const ui::Context&, ui::Rect);
    void drawDragArea(ui::Context&, ui::Rect);
    // The title, centered on the window, in an area that moves it.
    void drawTitle(ui::Context&, ui::Rect, std::string_view title, bool modified, bool divider);
    void drawIcon(ui::Context&, ui::Rect, bool divider);
    void drawButton(ui::Context&, ui::Rect, std::string_view instance, const char* command, Hint&);
    void drawControls(ui::Context&, ui::Rect, std::string_view instance, bool divider, Hint&);
    // A hairline frame around a window without system decorations.
    void drawBorder(ui::Context&, float width, float height) const;
    // The command of the button that maximizes or restores the window.
    const char* zoomCommand() const;

    WindowState window;
    WindowControls controls{nativeWindowControls()};
    bool focused = true;

private:
    ui::CommandRegistry& commands;
    ui::Rect header{};
    std::vector<ui::Rect> areas;
    std::unique_ptr<tvg::Picture> icon;
    float windowWidth = 0;
};
}  // namespace tn
