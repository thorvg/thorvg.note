#pragma once

#include <thornote/widgets/hint.h>
#include <storage/storage.h>
#include <ui/ui.h>
#include <functional>
#include <optional>
#include <vector>

namespace tn
{
// The start page: the logo, common actions and the most recent files.
class WelcomePage
{
public:
    explicit WelcomePage(ui::CommandRegistry& commands) :
        commands(commands) {}

    void draw(ui::Context&, ui::Rect body, const std::vector<storage::Path>& files, bool enabled, Hint&);
    // Scrolls the page with a navigation key; returns whether the key moved it.
    bool scrollKey(ui::Context&, ui::Key) const;

    std::function<void(const storage::Path&)> open;

private:
    ui::CommandRegistry& commands;
    ui::Rect viewport{};
    float x = 0, y = 0, limitX = 0, limitY = 0;
    std::optional<float> grabX, grabY;
};
}  // namespace tn
