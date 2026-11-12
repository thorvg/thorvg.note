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

std::array<std::string, 3> EditorApp::previewLines(std::string_view text, float width) const
{
    std::array<std::string, 3> lines;
    for (size_t line = 0; line < lines.size(); ++line) {
        auto start = text.find_first_not_of(" \t");
        if (start == std::string_view::npos) break;
        text.remove_prefix(start);
        if (line + 1 == lines.size()) {
            lines[line] = context.elideText(text, width, 10);
            break;
        }
        size_t end = 0, word = 0;
        while (end < text.size()) {
            auto next = end + 1;
            while (next < text.size() && (uint8_t(text[next]) & 0xc0) == 0x80)
                ++next;
            if (end && context.textWidth(text.substr(0, next), 10) > width) break;
            if (text[end] == ' ' || text[end] == '\t') word = next;
            end = next;
        }
        if (end < text.size() && word) end = word;
        auto value = text.substr(0, end);
        auto last = value.find_last_not_of(" \t");
        if (last != std::string_view::npos) lines[line] = value.substr(0, last + 1);
        text.remove_prefix(end);
    }
    return lines;
}

void EditorApp::drawNoteSearch(ui::Rect r, std::string_view placeholder)
{
    searchBounds = {r.x + 10, r.y + 10, r.w - 56, 30};
    ui::textField(context, "notes/search", searchBounds, noteSearch, placeholder, true, {}, "search");
    ui::Rect button{r.x + r.w - 40, r.y + 10, 30, 30};
    auto state = ui::button(context, "notes/new", button, "", [this] { commands.execute("file.new"); },
                            {.style = ui::ButtonStyle::Quiet, .icon = "plus", .enabled = !dialog});
    if (state.hovered) {
        hint = "New document";
        hintBounds = button;
    }
}

void EditorApp::drawNoteFiles(ui::Rect r)
{
    context.rect(r, theme.surface);
    drawNoteSearch(r, "Search files");
    if (previousQuery != noteSearch.value()) {
        previousQuery = noteSearch.value();
        noteFileScroll = 0;
    }
    noteFiles.update(noteBrowser.files(), noteBrowser.revision(), noteSearch.value());
    ui::Rect body{r.x + 6, r.y + 54, r.w - 12, std::max(0.0f, r.h - 60)};
    struct Draft
    {
        DocumentTab* tab;
        std::array<std::string, 3> lines;
        float top, height;
    };
    std::vector<Draft> drafts;
    float draftHeight = 0;
    if (collection == "all") {
        for (auto* tab : filteredDocuments()) {
            if (!tab->path.empty()) continue;
            if (drafts.empty()) draftHeight = 28;
            auto lines = previewLines(documentPreview(tab->document.text()), body.w - 20);
            auto count = std::count_if(lines.begin(), lines.end(), [](const auto& line) { return !line.empty(); });
            float height = 40 + count * 16;
            drafts.push_back({tab, std::move(lines), draftHeight, height});
            draftHeight += height;
        }
    }
    auto linesFor = [&](size_t file) {
        const auto& path = noteBrowser.files()[file].path;
        auto opened = std::find_if(workspace.documents().begin(), workspace.documents().end(), [&](const auto& tab) { return tab->path == path && !tab->loading; });
        auto detail = opened != workspace.documents().end() ? documentPreview((*opened)->document.text()) : noteBrowser.preview(file);
        return previewLines(detail, body.w - 20);
    };
    auto firstRow = noteFiles.visibleRows(noteFileScroll - draftHeight, body.h).first;
    for (size_t i = firstRow; i < noteFiles.rows().size() && noteFiles.rows()[i].top + draftHeight < noteFileScroll + body.h; ++i) {
        if (!noteFiles.rows()[i].file) continue;
        auto lines = linesFor(*noteFiles.rows()[i].file);
        auto count = std::count_if(lines.begin(), lines.end(), [](const auto& line) { return !line.empty(); });
        noteFiles.rowHeight(i, 40 + count * 16);
    }
    float limit = std::max(0.0f, draftHeight + noteFiles.height() - body.h);
    context.scroll("note-files/scroll", body, noteFileScroll, limit, {.step = 60, .enabled = !dialog});
    context.clip(body, [&] {
        if (!drafts.empty()) context.text({body.x + 10, body.y - noteFileScroll, body.w - 20, 24}, "Unsaved", theme.muted, 10, true);
        for (const auto& draft : drafts) {
            if (draft.top + draft.height <= noteFileScroll) continue;
            if (draft.top >= noteFileScroll + body.h) break;
            auto* tab = draft.tab;
            auto key = "note-drafts/" + std::to_string(tab->id);
            ui::Rect row{body.x, body.y + draft.top - noteFileScroll, body.w, draft.height - 4};
            auto state = context.control(key, row, [this, id = tab->id](const ui::Event& event) {
                if (event.activated()) activateDocument(id); }, !dialog);
            bool selected = !settings.active && tab == workspace.active();
            if (selected || state.hovered) context.rect(row, selected ? theme.selection : theme.raised, 6);
            bool favorite = favorites.count(tab->id);
            auto title = context.elideText(tab->displayTitle(true), row.w - (tab->dirty() ? 44 : 20) - (favorite ? 20 : 0), 12, true);
            context.text({row.x + 10, row.y + 4, row.w - 20, 24}, title, theme.text, 12, true);
            if (tab->dirty()) context.icon(key + "/dirty", {row.x + 14 + context.textWidth(title, 12, true), row.y + 4, 20, 24}, "dirty", theme.muted,
                                           {.hovered = state.hovered});
            if (favorite) context.icon(key + "/favorite", {row.x + row.w - 22, row.y + 10, 14, 14}, "star", theme.accent,
                                       {state.hovered, state.pressed, true, true, state.focused});
            for (size_t line = 0; line < draft.lines.size(); ++line)
                if (!draft.lines[line].empty()) context.text({row.x + 10, row.y + 29 + line * 16, row.w - 20, 16}, draft.lines[line], theme.muted, 10);
            if (state.focused) context.focusRing(row.inset(4));
        }
        auto [first, last] = noteFiles.visibleRows(noteFileScroll - draftHeight, body.h);
        for (size_t i = first; i < last; ++i) {
            const auto& entry = noteFiles.rows()[i];
            ui::Rect row{body.x, body.y + draftHeight + entry.top - noteFileScroll, body.w, entry.height - 4};
            if (!entry.file) {
                context.text({row.x + 10, row.y, row.w - 20, row.h}, entry.date, theme.muted, 10, true);
                continue;
            }
            auto path = noteBrowser.files()[*entry.file].path;
            auto key = "note-files/" + utf8(path);
            auto state = context.control(key, row, [this, path](const ui::Event& event) {
                if (event.activated())
                    activateDocument(workspace.open(path, false)); }, !dialog);
            auto active = workspace.active();
            bool selected = !settings.active && active && active->path == path;
            if (selected || state.hovered) context.rect(row, selected ? theme.selection : theme.raised, 6);
            auto opened = std::find_if(workspace.documents().begin(), workspace.documents().end(), [&](const auto& tab) { return tab->path == path; });
            bool modified = opened != workspace.documents().end() && (*opened)->dirty();
            bool favorite = opened != workspace.documents().end() && favorites.count((*opened)->id);
            auto title = context.elideText(displayName(path), row.w - (modified ? 44 : 20) - (favorite ? 20 : 0), 12, true);
            context.text({row.x + 10, row.y + 4, row.w - 20, 24}, title, theme.text, 12, true);
            if (modified) context.icon(key + "/dirty", {row.x + 14 + context.textWidth(title, 12, true), row.y + 4, 20, 24}, "dirty", theme.muted,
                                       {.hovered = state.hovered});
            if (favorite) context.icon(key + "/favorite", {row.x + row.w - 22, row.y + 10, 14, 14}, "star", theme.accent,
                                       {state.hovered, state.pressed, true, true, state.focused});
            auto lines = linesFor(*entry.file);
            for (size_t line = 0; line < lines.size(); ++line)
                if (!lines[line].empty()) context.text({row.x + 10, row.y + 29 + line * 16, row.w - 20, 16}, lines[line], theme.muted, 10);
            if (state.focused) context.focusRing(row.inset(4));
        }
    });
    if (noteFiles.rows().empty() && drafts.empty()) {
        auto message = noteBrowser.loading() ? "Loading files…" : !noteBrowser.error().empty() ? noteBrowser.error()
            : recent.directories.empty()                                                       ? "Open a file to add its folder"
                                                                                               : "No matching files";
        context.text(body.inset(12), message, theme.muted, 12, false, ui::Align::Center);
    }
    ui::scrollbar(context, {r.x + r.w - 4, body.y, 2, body.h}, body.h, draftHeight + noteFiles.height(), noteFileScroll, 20);
    context.line(r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h, theme.border);
}
}  // namespace tn
