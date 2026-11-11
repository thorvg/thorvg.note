#include <thornote/app/editor_app.h>
#include <thornote/platform/display_text.h>
#include <algorithm>

namespace tn
{
bool EditorApp::browsingNoteFiles() const
{
    return composition == Layout::Note && !commandList && (collection == "all" || collection.compare(0, 7, "folder:") == 0);
}

void EditorApp::refreshNoteFiles(bool force)
{
    if (!browsingNoteFiles()) return;
    if (collection == "all") noteBrowser.show(recent.rootDirectories(), force);
    else noteBrowser.show({utf8Path(collection.substr(7))}, force);
}

void EditorApp::selectNoteFolder(const storage::Path& path)
{
    settings.active = false;
    collection = "folder:" + utf8(path.lexically_normal());
    noteSearch = {};
    noteFileScroll = 0;
    refreshNoteFiles(true);
    focusEditor(false);
}

void EditorApp::openFolderMenu(const storage::Path& path, ui::Rect anchor)
{
    if (dialog || palette || prompt != Prompt::None || !layoutScript.menu("folder-menu")) return;
    menuFolder = path;
    menuRoot = "folder-menu";
    contextMenuAnchor = menuButton = anchor;
    openMenu("folder-menu");
}

void EditorApp::removeNoteFolder(const storage::Path& path)
{
    auto selected = collection.compare(0, 7, "folder:") == 0 ? recent.rootDirectory(utf8Path(collection.substr(7))) : storage::Path{};
    recent.removeDirectory(path);
    if (recent.directories.empty()) folderWorkspace = false;
    if (selected == path || collection == "folder:" + utf8(path)) collection = "all";
    collectionOffset = 0;
    noteFileScroll = 0;
    refreshNoteFiles(true);
}

void EditorApp::drawNoteFiles(ui::Rect r)
{
    context.rect(r, theme.surface);
    std::string title = "All";
    if (collection != "all") {
        auto path = utf8Path(collection.substr(7));
        title = displayName(path);
        if (title.empty()) title = displayPath(path);
    }
    context.text({r.x + 14, r.y + 9, r.w - 62, 30}, context.elideText(title, r.w - 66, 13, true), theme.text, 13, true);
    searchBounds = {r.x + 10, r.y + 48, r.w - 20, 30};
    ui::textField(context, "notes/search", searchBounds, noteSearch, "Search files", true, {}, "search");
    if (previousQuery != noteSearch.value) {
        previousQuery = noteSearch.value;
        noteFileScroll = 0;
    }
    noteFiles.update(noteBrowser.files(), noteBrowser.revision(), noteSearch.value);
    context.text({r.x + r.w - 44, r.y + 9, 28, 30}, std::to_string(noteFiles.count()), theme.muted, 11, false, ui::Align::End);
    ui::Rect body{r.x + 6, r.y + 92, r.w - 12, std::max(0.0f, r.h - 98)};
    float limit = std::max(0.0f, noteFiles.height() - body.h);
    noteFileScroll = std::clamp(noteFileScroll, 0.0f, limit);
    context.control("note-files/scroll", body, [this, limit](const ui::Event& event) {
        if (event.type == ui::EventType::Scroll) noteFileScroll = std::clamp(noteFileScroll - event.y * 60, 0.0f, limit); }, !dialog, ui::ControlFlag::Scrollable);
    context.clip(body, [&] {
        auto [first, last] = noteFiles.visibleRows(noteFileScroll, body.h);
        for (size_t i = first; i < last; ++i) {
            const auto& entry = noteFiles.rows()[i];
            ui::Rect row{body.x, body.y + entry.top - noteFileScroll, body.w, entry.height - 4};
            if (!entry.file) {
                context.text({row.x + 10, row.y, row.w - 20, row.h}, entry.date, theme.muted, 10, true);
                continue;
            }
            auto path = noteBrowser.files()[*entry.file].path;
            auto key = "note-files/" + utf8(path);
            auto state = context.control(key, row, [this, path](const ui::Event& event) {
                if (event.type == ui::EventType::Activate || (event.type == ui::EventType::Key && (event.key == ui::Key::Enter || event.key == ui::Key::Space)))
                    activateDocument(workspace.open(path, false)); }, !dialog);
            auto active = workspace.active();
            bool selected = !settings.active && active && active->path == path;
            if (selected || state.hovered) context.rect(row, selected ? theme.selection : theme.raised, 6);
            context.text({row.x + 10, row.y + 4, row.w - 20, 24}, context.elideText(displayName(path), row.w - 20, 12, true), theme.text, 12, true);
            auto folder = path.parent_path();
            auto detail = displayName(folder);
            if (detail.empty()) detail = displayPath(folder);
            else detail += " · " + displayPath(folder.parent_path());
            context.text({row.x + 10, row.y + 29, row.w - 20, 18}, context.elideText(detail, row.w - 20, 10), theme.muted, 10);
            if (state.hovered) {
                hint = displayPath(folder);
                hintBounds = row;
            }
            if (state.focused) context.focusRing(row.inset(4));
        }
    });
    if (noteFiles.rows().empty()) {
        auto message = noteBrowser.loading() ? "Loading files…" : !noteBrowser.error().empty() ? noteBrowser.error()
            : recent.directories.empty()                                                       ? "Open a file to add its folder"
                                                                                               : "No matching files";
        context.text(body.inset(12), message, theme.muted, 12, false, ui::Align::Center);
    }
    if (limit) {
        float thumb = std::max(20.0f, body.h * body.h / noteFiles.height());
        context.rect({r.x + r.w - 4, body.y + (body.h - thumb) * noteFileScroll / limit, 2, thumb}, theme.border, 1);
    }
    context.line(r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h, theme.border);
}
}  // namespace tn
