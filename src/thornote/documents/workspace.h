#pragma once

#include <ui/ui.h>
#include <storage/storage.h>
#include "session.h"

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
    uint64_t id;
    std::string title;
    tn::Document document;
    ui::TextEditor editor;
    // Distinct non-ASCII codepoints seen in the document, and the state they were collected for.
    std::vector<uint32_t> codepoints;
    uint64_t fontRevision = uint64_t(-1);
    ui::FontId preparedFont = 0;
    storage::Path path;
    storage::FileFormat format, savedFormat;
    storage::DiskStamp stamp;
    std::string savedText;
    bool loading = false, saving = false, uncertain = false, configured = false, persistenceFailed = false;
    mutable uint64_t checkedRevision = uint64_t(-1);
    mutable bool textChanged = false;
    struct ViewRestore
    {
        size_t anchor, caret;
        float x, y;
    };
    std::optional<ViewRestore> restoreView;
};

// The application owns documents and save state. The file worker receives owned snapshots.
class Workspace
{
public:
    explicit Workspace(std::function<void()> wake = {}) :
        files(std::move(wake)) {}
    DocumentTab& create();
    uint64_t open(const storage::Path&, bool allowMissing = true);
    void restore(SessionDocument);
    bool save(uint64_t id);
    bool saveAs(uint64_t id, const storage::Path&);
    bool close(uint64_t id, bool discard = false);
    void select(uint64_t id);
    DocumentTab* find(uint64_t id) const;
    DocumentTab* active() const { return find(selected); }
    const std::vector<std::unique_ptr<DocumentTab>>& documents() const { return tabs; }
    size_t poll() { return files.poll(); }
    bool busy() const;
    std::string error, message;
    std::function<void(const storage::Path&)> fileAccessed;
    // A backend can defer save completion until its durable storage has synchronized.
    std::function<void(std::function<void(std::string)>)> persist;

private:
    void write(uint64_t id, storage::Path, storage::DiskStamp, std::string, storage::FileFormat);
    void erase(uint64_t id);
    std::vector<std::unique_ptr<DocumentTab>> tabs;
    uint64_t nextId = 1, selected = 0, pendingSelection = 0;
    // Join the worker before destroying its documents.
    storage::FileService files;
};

}  // namespace tn
