#include "welcome.h"
#include <thornote/app/app_identity.h>
#include <thornote/platform/display_text.h>
#include <algorithm>

namespace tn
{
void WelcomePage::draw(ui::Context& context, ui::Rect body, const std::vector<storage::Path>& files, bool enabled, Hint& hint)
{
    const auto& theme = context.theme();
    constexpr float contentWidth = 480, pageWidth = contentWidth + 80, margin = 40, scrollbarSize = 12;
    constexpr float rowHeight = 34, fileHeight = 32;
    constexpr float actionsGap = 22, historyGap = 20, headingHeight = 24;
    constexpr float logoWidth = 144, logoHeight = logoWidth * 185 / 172.359f;
    auto fileCount = std::min(files.size(), size_t(5));
    float historyHeight = fileCount ? fileCount * fileHeight : 32;
    float fixedHeight = actionsGap + 5 * rowHeight + historyGap + headingHeight + historyHeight;
    float contentHeight = margin * 2 + logoHeight + fixedHeight;
    bool vertical = contentHeight > body.h;
    bool horizontal = pageWidth > body.w - (vertical ? scrollbarSize : 0);
    vertical = vertical || contentHeight > body.h - (horizontal ? scrollbarSize : 0);
    viewport = {body.x, body.y, std::max(0.0f, body.w - (vertical ? scrollbarSize : 0)),
                std::max(0.0f, body.h - (horizontal ? scrollbarSize : 0))};
    limitX = std::max(0.0f, pageWidth - viewport.w);
    limitY = std::max(0.0f, contentHeight - viewport.h);
    context.scroll("welcome/scroll", body, x, y, limitX, limitY, {.enabled = enabled});
    float left = viewport.x + std::max(margin, (viewport.w - contentWidth) / 2) - x;
    float top = viewport.y + std::max(margin, (viewport.h - logoHeight - fixedHeight) / 2) - y;
    ui::Rect logoBounds{left + (contentWidth - logoWidth) / 2, top, logoWidth, logoHeight};
    float actionsX = left, actionsWidth = contentWidth;
    top += logoHeight + actionsGap;

    context.clip(viewport, [&] {
        context.paint(ui::paintKey('B', logoBounds, theme.text), [&]() -> tvg::Paint* {
            auto logo = createBrandPicture(theme.text);
            if (!logo) return nullptr;
            float scale = logoWidth / 172.359f;
            ui::transform(logo, {scale, 0, logoBounds.x, 0, scale, logoBounds.y, 0, 0, 1});
            return logo;
        });
        struct Action
        {
            const char* id;
            const char* label;
        };
        const Action actions[] = {{"file.new", "New File"}, {"file.open", "Find File"}, {"file.recent", "Recent Files"}, {"ui.commands.open", "Open Command Palette"}, {"settings.open", "Settings"}};
        for (size_t i = 0; i < 5; ++i) {
            const auto& action = actions[i];
            ui::Rect row{actionsX, top + i * rowHeight, actionsWidth, rowHeight};
            auto state = context.control(std::string("welcome/") + action.id, row, [this, id = std::string(action.id)](const ui::Event& event) {
                if (event.activated()) commands.execute(id); }, enabled);
            if (state.hovered || state.pressed) context.rect(row, theme.raised, theme.radius);
            auto command = std::find_if(commands.commands().begin(), commands.commands().end(), [&](const auto& command) { return command.id == action.id; });
            auto shortcut = command == commands.commands().end() ? std::string{} : ui::platform::shortcutLabel(command->shortcut);
            float keyWidth = context.textWidth(shortcut, 11) + 18;
            ui::Rect key{row.x + row.w - keyWidth - 10, row.y + (row.h - 23) / 2, keyWidth, 23};
            context.text({row.x + 12, row.y, row.w - keyWidth - 38, row.h}, action.label, theme.text, 13);
            context.rect(key, theme.surface, 4, theme.border);
            context.text(key, shortcut, theme.muted, 11, false, ui::Align::Center);
            if (state.focused) context.focusRing(row.inset(2));
        }
        float historyY = top + 5 * rowHeight + historyGap;
        context.text({actionsX + 12, historyY, actionsWidth - 24, headingHeight}, "Recent Files", theme.muted, 12, true);
        if (files.empty())
            context.text({actionsX + 12, historyY + headingHeight, actionsWidth - 24, fileHeight}, "Files appear here as you open them.", theme.muted, 11);
        for (size_t i = 0; i < fileCount; ++i) {
            const auto& path = files[i];
            ui::Rect row{actionsX, historyY + headingHeight + i * fileHeight, actionsWidth, fileHeight};
            auto state = context.control("welcome/file/" + std::to_string(i), row, [this, path](const ui::Event& event) {
                if (event.activated() && open) open(path); }, enabled);
            if (state.hovered || state.pressed) context.rect(row, theme.raised, theme.radius);
            context.icon("welcome/file/" + utf8(path) + "/icon", {row.x + 12, row.y + (row.h - 18) / 2, 18, 18}, "file", theme.muted,
                         {state.hovered, state.pressed, false, enabled, state.focused});
            auto name = displayName(path);
            if (name.empty()) name = displayPath(path);
            context.text({row.x + 40, row.y, row.w - 52, row.h / 2}, name, theme.text, 11);
            context.text({row.x + 40, row.y + row.h / 2, row.w - 52, row.h / 2}, displayPath(path), theme.muted, 9);
            if (state.hovered) hint = {displayPath(path), row};
            if (state.focused) context.focusRing(row.inset(2));
        }
    });
    if (vertical && viewport.h > 0) ui::scrollbar(context, "welcome/vertical-scrollbar",
                                                  {viewport.x + viewport.w, body.y, std::min(scrollbarSize, body.w), viewport.h},
                                                  contentHeight, y, grabY, ui::Axis::Vertical, enabled);
    else grabY.reset();
    if (horizontal && body.h > 0 && viewport.w > 0) ui::scrollbar(context, "welcome/horizontal-scrollbar",
                                                                  {body.x, viewport.y + viewport.h, viewport.w, std::min(scrollbarSize, body.h)},
                                                                  pageWidth, x, grabX, ui::Axis::Horizontal, enabled);
    else grabX.reset();
}

bool WelcomePage::scrollKey(ui::Context& context, ui::Key key) const
{
    return context.scrollKey("welcome/scroll", key);
}
}  // namespace tn
