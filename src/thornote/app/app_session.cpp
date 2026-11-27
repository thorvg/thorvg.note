#include "editor_app.h"
#include <algorithm>
#ifdef __EMSCRIPTEN__
#include <thornote/platform/emscripten/browser_files.h>
#endif

namespace tn
{
std::optional<ui::Rect> EditorApp::restoreSession()
{
    if (settingsDirectory.empty() || !workspace.documents().empty()) return {};
    auto saved = session.load(settingsDirectory / "session");
    if (saved.documents.size() == 1 && !saved.settingsOpen) {
        const auto& only = saved.documents.front();
        // Older versions created and checkpointed an empty startup document automatically.
        if (only.content.originalPath.empty() && only.content.text.empty() && !only.dirty && !only.favorite && !only.uncertain) {
            saved.documents.clear();
            saved.active = 0;
        }
    }
    if (!session.error().empty()) workspace.error = session.error();
    restoredWidth = saved.width;
    restoredHeight = saved.height;
    collection = std::move(saved.collection);
    if (collection.compare(0, 7, "folder:") == 0 && std::find(recent.directories.begin(), recent.directories.end(), utf8Path(collection.substr(7))) == recent.directories.end()) collection = "all";
    folderWorkspace = saved.folderWorkspace && !recent.directories.empty();
    noteSearch.value(std::move(saved.noteQuery));
    commandList = saved.commands;
    settings.category = saved.settingsCategory;
    settings.open = saved.settingsOpen;
    for (auto& record : saved.documents) {
#ifdef __EMSCRIPTEN__
        browserRestoreFile(record.content);
#endif
        if (record.favorite) favorites.insert(record.id);
        workspace.restore(std::move(record));
        if (!workspace.error.empty()) session.disable();
    }
    if (saved.active) workspace.select(saved.active);
    if (saved.settingsActive) openSettings();
    return ui::Rect{0, 0, restoredWidth, restoredHeight};
}

SessionState EditorApp::captureSession(bool contents) const
{
    SessionState result;
    result.active = workspace.active() ? workspace.active()->id : 0;
    result.width = width > 0 ? width : restoredWidth;
    result.height = height > 0 ? height : restoredHeight;
    result.settingsOpen = settings.open;
    result.settingsActive = settings.active;
    result.settingsCategory = settings.category;
    result.commands = commandList;
    result.folderWorkspace = folderWorkspace;
    result.collection = collection;
    result.noteQuery = noteSearch.value();
    for (const auto& tab : workspace.documents()) {
        SessionDocument doc;
        doc.id = tab->id;
        doc.content.title = tab->title;
        doc.content.originalPath = tab->path;
        doc.content.revision = tab->document.revision();
        doc.content.format = tab->format;
        if (contents) doc.content.text = tab->document.text();
        doc.hadFile = tab->stamp.exists();
        doc.diskDigest = tab->stamp.digest();
        doc.dirty = tab->dirty();
        doc.uncertain = tab->uncertain;
        doc.favorite = favorites.count(tab->id);
        auto selection = tab->editor.session().selection();
        doc.anchor = selection.anchor;
        doc.caret = selection.caret;
        // A hidden document keeps its font size until shown, while the session restores it at the current one.
        auto scale = options.fontSize / tab->editor.options().fontSize;
        doc.scrollX = tab->editor.scrollX() * scale;
        doc.scrollY = tab->editor.scrollY() * scale;
        if (tab->restoreView) {
            doc.anchor = tab->restoreView->anchor;
            doc.caret = tab->restoreView->caret;
            doc.scrollX = tab->restoreView->x;
            doc.scrollY = tab->restoreView->y;
        }
        result.documents.push_back(std::move(doc));
    }
    return result;
}

bool EditorApp::checkpointSession(bool force)
{
    return !workspace.busy() && session.checkpoint([this](bool contents) { return captureSession(contents); }, now(), force, workspace.error);
}
}  // namespace tn
