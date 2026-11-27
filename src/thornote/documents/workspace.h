#pragma once

#include <ui/ui.h>
#include <storage/storage.h>
#include <thornote/preferences/layout.h>
#include "session.h"
#include "widgets.h"

namespace tn
{

struct DocumentTab
{
    DocumentTab(uint64_t id, std::string title) :
        id(id), title(std::move(title)), editor(document) {}
    bool dirty() const;
    bool busy() const { return loading || saving; }
    // Drafts use the first line; named files keep their filename unless a note view requests content.
    std::string displayTitle(bool preferContent = false) const;
    std::string label(bool preferContent = false) const;
    bool in(Layout layout) const { return layouts >> size_t(layout) & 1; }
    uint64_t id;
    std::string title;
    tn::Document document;
    ui::TextEditor editor;
    DocumentWidgets widgets;
    // Distinct non-ASCII codepoints seen in the document, and the state they were collected for.
    std::vector<uint32_t> codepoints;
    uint64_t fontRevision = uint64_t(-1);
    ui::FontId preparedFont = 0;
    storage::Path path;
    storage::FileFormat format, savedFormat;
    storage::DiskStamp stamp;
    std::string savedText;
    bool loading = false, saving = false, uncertain = false, configured = false, persistenceFailed = false;
    // Layouts holding the document, one bit each.
    uint8_t layouts = 0;
    mutable uint64_t checkedRevision = uint64_t(-1);
    mutable bool textChanged = false;
    struct ViewRestore
    {
        size_t anchor, caret;
        float x;
        double y;
    };
    std::optional<ViewRestore> restoreView;
};

// The application owns documents and save state. The file worker receives owned snapshots.
// Layouts share the documents; each shows those it holds and selects among them.
class Workspace
{
public:
    explicit Workspace(std::function<void()> wake = {}) :
        files(std::move(wake)) {}
    // Created, opened and selected documents join the shown layout.
    DocumentTab& create();
    uint64_t open(const storage::Path&, bool allowMissing = true);
    // A record without layouts joins the shown one.
    void restore(SessionDocument);
    bool save(uint64_t id);
    bool saveAs(uint64_t id, const storage::Path&);
    // Writes a copy of the document to another file; the tab stays on its own file.
    bool saveCopy(uint64_t id, const storage::Path&);
    bool rename(uint64_t id, const storage::Path& filename);
    // Moves the saved file into another folder; the tab follows it.
    bool move(uint64_t id, const storage::Path& directory);
    bool remove(uint64_t id);
    // Leaves the shown layout; a document no other layout holds closes, discarding changes only when asked.
    bool close(uint64_t id, bool discard = false);
    void select(uint64_t id);
    // Shows the documents of a layout with the one it selected last.
    void show(Layout);
    Layout shown() const { return layout; }
    uint64_t selection(Layout value) const { return value == layout ? selected : selections[size_t(value)]; }
    DocumentTab* find(uint64_t id) const;
    DocumentTab* active() const { return find(selected); }
    const std::vector<std::unique_ptr<DocumentTab>>& documents() const { return tabs; }
    size_t poll() { return files.poll(); }
    bool busy() const;
    std::string error, message;
    std::function<void(const storage::Path&, bool saved)> fileAccessed;
    // A backend can defer save completion until its durable storage has synchronized.
    std::function<void(std::function<void(std::string)>)> persist;
    // Deleted files go to the system trash where the platform has one.
    bool useTrash = storage::trashAvailable();

private:
    void write(uint64_t id, const storage::Path&, storage::DiskStamp, std::string, storage::FileFormat);
    // Renames the file to target within its folder, or moves it into the target folder.
    bool relocate(uint64_t id, const storage::Path& target, bool rename);
    // Moves the selection off the document to the nearest one of the shown layout.
    void deselect(uint64_t id);
    void erase(uint64_t id);
    std::vector<std::unique_ptr<DocumentTab>> tabs;
    uint64_t nextId = 1, selected = 0, pendingSelection = 0;
    Layout layout = Layout::Memo;
    std::array<uint64_t, layoutCount> selections{};  // Of the hidden layouts.
    // Join the worker before destroying its documents.
    storage::FileService files;
};

}  // namespace tn
