#include <thornote/app/editor_app.h>
#include <thornote/app/app_identity.h>
#include <thornote/platform/display_text.h>
#include <ui/ui.h>
#include <algorithm>

namespace tn
{
void EditorApp::findFile(const storage::Path& directory)
{
    if (dialog || !chooseFile) return;
    findDirectory = directory.empty() && !recent.directories.empty() ? recent.directories.front() : directory;
    dialog = true;
    focusEditor(false);
    chooseFile(false, 0, findDirectory);
}

bool EditorApp::openLatest(const storage::Path& directory)
{
    if (directory.empty()) return false;
    std::error_code error;
    storage::Path latest;
    // The files used last come first; a folder never used offers its newest file.
    for (const auto& path : recent.files) {
        if (std::mismatch(directory.begin(), directory.end(), path.begin(), path.end()).first != directory.end() || !std::filesystem::is_regular_file(path, error)) continue;
        latest = path;
        break;
    }
    if (latest.empty()) {
        std::filesystem::file_time_type newest;
        for (std::filesystem::directory_iterator entry(directory, error), end; !error && entry != end; entry.increment(error)) {
            std::error_code unreadable;
            if (entry->path().filename().native().front() == '.' || !entry->is_regular_file(unreadable)) continue;
            auto modified = entry->last_write_time(unreadable);
            if (unreadable || (!latest.empty() && modified <= newest)) continue;
            latest = entry->path();
            newest = modified;
        }
    }
    if (latest.empty()) return false;
    activateDocument(workspace.open(latest));
    return true;
}

void EditorApp::openRecentDirectory(const storage::Path& path)
{
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        workspace.error = "Directory is unavailable: " + displayPath(path);
        return;
    }
    recent.rememberDirectory(path);
    findFile(path);
}

void EditorApp::openRecentFiles()
{
    if (prompt != Prompt::None || dialog) return;
    if (!menu.empty()) closeMenu();
    paletteTarget = visibleDocument() ? visibleDocument()->id : 0;
    paletteState = {};
    recentCommands.release("recent");
    for (size_t i = 0; i < recent.files.size(); ++i) {
        auto path = recent.files[i];
        recentCommands.add({.id = "recent/" + std::to_string(i), .title = displayName(path), .category = displayPath(path.parent_path()), .run = [this, path] { activateDocument(workspace.open(path, false)); }, .owner = "recent"});
    }
    palette = recentPalette = true;
    focusEditor(false);
    context.focus("palette/search");
}

void EditorApp::drawWelcome(ui::Rect viewport)
{
    float titleHeight = 40;
    constexpr float statusHeight = 22;
    ui::Rect body{viewport.x, viewport.y + titleHeight, viewport.w, std::max(0.0f, viewport.h - titleHeight - statusHeight)};
    constexpr float contentWidth = 480, pageWidth = contentWidth + 80, margin = 40, scrollbarSize = 12;
    constexpr float rowHeight = 34, directoryHeight = 32;
    constexpr float actionsGap = 22, directoriesGap = 20, headingHeight = 24;
    constexpr float logoWidth = 144, logoHeight = logoWidth * 185 / 172.359f;
    auto directoryCount = std::min(recent.directories.size(), size_t(5));
    float historyHeight = directoryCount ? directoryCount * directoryHeight : 32;
    float fixedHeight = actionsGap + 5 * rowHeight + directoriesGap + headingHeight + historyHeight;
    float contentHeight = margin * 2 + logoHeight + fixedHeight;
    bool vertical = contentHeight > body.h;
    bool horizontal = pageWidth > body.w - (vertical ? scrollbarSize : 0);
    vertical = vertical || contentHeight > body.h - (horizontal ? scrollbarSize : 0);
    auto& scroll = welcomeScroll;
    scroll.viewport = {body.x, body.y, std::max(0.0f, body.w - (vertical ? scrollbarSize : 0)),
                       std::max(0.0f, body.h - (horizontal ? scrollbarSize : 0))};
    scroll.limitX = std::max(0.0f, pageWidth - scroll.viewport.w);
    scroll.limitY = std::max(0.0f, contentHeight - scroll.viewport.h);
    context.scroll("welcome/scroll", body, scroll.x, scroll.y, scroll.limitX, scroll.limitY, {.enabled = !dialog});
    float x = scroll.viewport.x + std::max(margin, (scroll.viewport.w - contentWidth) / 2) - scroll.x;
    float y = scroll.viewport.y + std::max(margin, (scroll.viewport.h - logoHeight - fixedHeight) / 2) - scroll.y;
    ui::Rect logoBounds{x + (contentWidth - logoWidth) / 2, y, logoWidth, logoHeight};
    float actionsX = x, actionsWidth = contentWidth;
    y += logoHeight + actionsGap;

    context.clip(scroll.viewport, [&] {
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
            ui::Rect row{actionsX, y + i * rowHeight, actionsWidth, rowHeight};
            auto state = context.control(std::string("welcome/") + action.id, row, [this, id = std::string(action.id)](const ui::Event& event) {
                if (event.activated()) commands.execute(id); }, !dialog);
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
        float directoriesY = y + 5 * rowHeight + directoriesGap;
        context.text({actionsX + 12, directoriesY, actionsWidth - 24, headingHeight}, "Recent Directories", theme.muted, 12, true);
        if (recent.directories.empty())
            context.text({actionsX + 12, directoriesY + headingHeight, actionsWidth - 24, directoryHeight}, "Folders appear here as you open files.", theme.muted, 11);
        for (size_t i = 0; i < directoryCount; ++i) {
            auto path = recent.directories[i];
            ui::Rect row{actionsX, directoriesY + headingHeight + i * directoryHeight, actionsWidth, directoryHeight};
            auto state = context.control("welcome/directory/" + std::to_string(i), row, [this, path](const ui::Event& event) {
                if (event.activated()) openRecentDirectory(path); }, !dialog);
            if (state.hovered || state.pressed) context.rect(row, theme.raised, theme.radius);
            context.icon("welcome/directory/" + utf8(path) + "/icon", {row.x + 12, row.y + (row.h - 18) / 2, 18, 18}, "folder", theme.muted,
                         {state.hovered, state.pressed, false, !dialog, state.focused});
            auto name = displayName(path);
            if (name.empty()) name = displayPath(path);
            context.text({row.x + 40, row.y, row.w - 52, row.h / 2}, name, theme.text, 11);
            context.text({row.x + 40, row.y + row.h / 2, row.w - 52, row.h / 2}, displayPath(path), theme.muted, 9);
            if (state.hovered) {
                hint = displayPath(path);
                hintBounds = row;
            }
            if (state.focused) context.focusRing(row.inset(2));
        }
    });
    if (vertical && scroll.viewport.h > 0) ui::scrollbar(context, "welcome/vertical-scrollbar",
                                                         {scroll.viewport.x + scroll.viewport.w, body.y, std::min(scrollbarSize, body.w), scroll.viewport.h},
                                                         contentHeight, scroll.y, scroll.grabY, ui::Axis::Vertical, !dialog);
    else scroll.grabY.reset();
    if (horizontal && body.h > 0 && scroll.viewport.w > 0) ui::scrollbar(context, "welcome/horizontal-scrollbar",
                                                                         {body.x, scroll.viewport.y + scroll.viewport.h, scroll.viewport.w, std::min(scrollbarSize, body.h)},
                                                                         pageWidth, scroll.x, scroll.grabX, ui::Axis::Horizontal, !dialog);
    else scroll.grabX.reset();
    drawTitleBar({0, 0, viewport.w, titleHeight});
    drawStatus({viewport.x, viewport.y + viewport.h - statusHeight, viewport.w, statusHeight});
}
}  // namespace tn
