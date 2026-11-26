#include <thornote/app/editor_app.h>
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
    chooseFile(false, findDirectory, [this](const std::vector<std::string>& paths, const std::string& error) { dialogResult(false, 0, paths, error); });
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
    welcome.draw(context, {viewport.x, viewport.y + titleHeight, viewport.w, std::max(0.0f, viewport.h - titleHeight - statusHeight)}, recent.files, !dialog, hint);
    drawTitleBar({0, 0, viewport.w, titleHeight});
    drawStatus({viewport.x, viewport.y + viewport.h - statusHeight, viewport.w, statusHeight});
}
}  // namespace tn
