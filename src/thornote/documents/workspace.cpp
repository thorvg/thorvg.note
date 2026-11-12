#include "workspace.h"
#include <thornote/platform/display_text.h>
#include <algorithm>

namespace tn
{
namespace
{
constexpr size_t fileLimit = 16 * 1024 * 1024;
storage::Path key(const storage::Path& path)
{
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) return path;
    auto canonical = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}
}  // namespace

bool DocumentTab::dirty() const
{
    if (checkedRevision != document.revision()) {
        checkedRevision = document.revision();
        textChanged = document.text() != savedText;
    }
    return uncertain || persistenceFailed || textChanged || format.ending != savedFormat.ending || format.bom != savedFormat.bom;
}

std::string DocumentTab::displayTitle(bool preferContent) const
{
    if (!preferContent && !path.empty()) return title;
    auto first = std::string_view(document.text());
    first = first.substr(0, first.find('\n'));
    auto start = first.find_first_not_of("# \t");
    if (start == std::string_view::npos) return title;
    first.remove_prefix(start);
    auto length = std::min(first.size(), size_t(160));
    while (length < first.size() && length && (uint8_t(first[length]) & 0xc0) == 0x80)
        --length;
    return std::string(first.substr(0, length));
}

std::string DocumentTab::label(bool preferContent) const
{
    return displayTitle(preferContent) + (loading ? " ..." : dirty() ? " *"
                                                                     : "");
}

DocumentTab& Workspace::create()
{
    auto id = nextId++;
    tabs.push_back(std::make_unique<DocumentTab>(id, "Untitled " + std::to_string(id)));
    selected = id;
    pendingSelection = 0;
    return *tabs.back();
}

DocumentTab* Workspace::find(uint64_t id) const
{
    for (const auto& tab : tabs)
        if (tab->id == id) return tab.get();
    return nullptr;
}

void Workspace::select(uint64_t id)
{
    auto tab = find(id);
    if (!tab) return;
    auto current = active();
    pendingSelection = tab->loading && current && !current->loading ? id : 0;
    if (!pendingSelection) selected = id;
}

bool Workspace::busy() const
{
    return std::any_of(tabs.begin(), tabs.end(), [](const auto& tab) { return tab->busy(); });
}

uint64_t Workspace::open(const storage::Path& path, bool allowMissing)
{
    auto destination = key(path);
    for (const auto& tab : tabs) {
        if (!tab->path.empty() && key(tab->path) == destination) {
            select(tab->id);
            if (!tab->loading && tab->stamp.exists() && fileAccessed) fileAccessed(key(tab->path));
            return tab->id;
        }
    }
    auto previous = selected;
    auto& tab = create();
    tab.path = destination;
    tab.title = displayName(path);
    tab.loading = true;
    auto id = tab.id;
    if (previous) selected = previous;
    select(id);
    error.clear();
    files.open(destination, {allowMissing, fileLimit}, [this, id](storage::Result<storage::LoadedFile> result) {
        auto tab = find(id);
        if (!tab) return;
        if (!result) {
            error = "Open failed: " + tab->title + ": " + result.error.message;
            erase(id);
            return;
        }
        if (!tab->document.text(result.value.text)) {
            error = "Open failed: " + tab->title;
            erase(id);
            return;
        }
        tab->path = result.value.path;
        tab->format = tab->savedFormat = result.value.format;
        tab->stamp = result.value.stamp;
        tab->savedText = tab->document.text();
        tab->checkedRevision = uint64_t(-1);
        tab->loading = false;
        if (pendingSelection == id) select(id);
        message = "Opened " + tab->title;
        if (tab->stamp.exists() && fileAccessed) fileAccessed(key(tab->path));
    });
    return id;
}

void Workspace::restore(SessionDocument state)
{
    if (find(state.id)) return;
    auto restored = std::make_unique<DocumentTab>(state.id, composedText(state.content.title));
    auto& tab = *restored;
    if (!state.content.originalPath.empty()) tab.path = key(state.content.originalPath);
    if (!tab.document.text(state.content.text)) return;
    tab.format = state.content.format;
    tab.restoreView = DocumentTab::ViewRestore{state.anchor, state.caret, state.scrollX, state.scrollY};
    nextId = std::max(nextId, state.id + 1);
    selected = tab.id;
    tabs.push_back(std::move(restored));
    if (tab.path.empty()) return;
    tab.loading = true;
    auto id = tab.id;
    files.open(tab.path, {true, fileLimit}, [this, id, state = std::move(state)](storage::Result<storage::LoadedFile> loaded) {
        auto tab = find(id);
        if (!tab) return;
        tab->loading = false;
        if (!loaded) {
            tab->uncertain = true;
            error = "Recovered buffer; original file is unavailable. Use Save As: " + displayPath(tab->path);
            return;
        }
        auto& file = loaded.value;
        tab->path = file.path;
        tab->stamp = file.stamp;
        tn::Document baseline;
        if (!baseline.text(file.text)) {
            tab->uncertain = true;
            return;
        }
        tab->savedText = baseline.text();
        tab->savedFormat = file.format;
        tab->checkedRevision = uint64_t(-1);
        if (!state.dirty && file.stamp.exists()) {
            // Clean tabs follow current disk contents; local edits keep their recovery buffer.
            if (!tab->document.text(file.text)) {
                tab->uncertain = true;
                return;
            }
            tab->format = file.format;
        } else {
            bool sameBase = state.hadFile == file.stamp.exists() && (!state.hadFile || state.diskDigest == file.stamp.digest());
            bool alreadySaved = file.stamp.exists() && tab->savedText == tab->document.text() && file.format.ending == tab->format.ending && file.format.bom == tab->format.bom;
            tab->uncertain = !alreadySaved && (state.uncertain || !sameBase);
            if (tab->uncertain) error = "Recovered unsaved changes; the original file changed. Use Save As: " + displayPath(tab->path);
            else if (state.hadFile && !file.stamp.exists()) error = "Recovered buffer; the original file was removed: " + displayPath(tab->path);
        }
    });
}

void Workspace::write(uint64_t id, storage::Path path, storage::DiskStamp expected, std::string text, storage::FileFormat format)
{
    auto snapshot = std::make_shared<std::string>(std::move(text));
    files.save(path, *snapshot, {format, std::move(expected), fileLimit},
               [this, id, path, snapshot, format](storage::SaveResult result) {
                   auto tab = find(id);
                   if (!tab) return;
                   if (!result) {
                       tab->saving = false;
                       if (result.state == storage::SaveState::CommittedDurabilityUnknown) {
                           tab->uncertain = true;
                           error = "Save may have completed. Reopen the file before retrying: " + result.error.message;
                       } else error = "Save failed: " + displayPath(path) + ": " + result.error.message;
                       return;
                   }
                   tab->path = key(path);
                   tab->title = displayName(path);
                   tab->stamp = result.stamp;
                   auto complete = [this, id, snapshot, format](std::string failure) {
                       auto tab = find(id);
                       if (!tab) return;
                       tab->saving = false;
                       tab->persistenceFailed = !failure.empty();
                       if (!failure.empty()) {
                           error = "Save failed: " + failure;
                           return;
                       }
                       tab->savedText = *snapshot;
                       tab->savedFormat = format;
                       tab->checkedRevision = uint64_t(-1);
                       tab->uncertain = false;
                       message = "Saved " + tab->title;
                       if (fileAccessed) fileAccessed(key(tab->path));
                   };
                   if (persist) persist(std::move(complete));
                   else complete({});
               });
}

bool Workspace::save(uint64_t id)
{
    auto tab = find(id);
    if (!tab || tab->busy() || tab->path.empty()) return false;
    if (tab->uncertain) {
        error = "Reopen the file or save to another path after the uncertain write.";
        return false;
    }
    error.clear();
    tab->saving = true;
    write(id, tab->path, tab->stamp, tab->document.text(), tab->format);
    return true;
}

bool Workspace::saveAs(uint64_t id, const storage::Path& path)
{
    auto tab = find(id);
    if (!tab || tab->busy() || path.empty()) return false;
    auto destination = key(path);
    if (!tab->path.empty() && key(tab->path) == destination) return save(id);
    for (const auto& other : tabs) {
        if (other->id != id && !other->path.empty() && key(other->path) == destination) {
            error = "The destination is already open in another tab.";
            return false;
        }
    }
    auto snapshot = tab->document.text();
    auto format = tab->format;
    error.clear();
    tab->saving = true;
    // The native Save dialog confirms replacement; the writer still checks a disk baseline.
    files.open(path, {true, fileLimit}, [this, id, path, snapshot = std::move(snapshot), format](storage::Result<storage::LoadedFile> result) {
        auto tab = find(id);
        if (!tab) return;
        if (!result) {
            tab->saving = false;
            error = "Save As failed: " + result.error.message;
            return;
        }
        write(id, path, result.value.stamp, snapshot, format);
    });
    return true;
}

void Workspace::erase(uint64_t id)
{
    auto found = std::find_if(tabs.begin(), tabs.end(), [=](const auto& tab) { return tab->id == id; });
    if (found == tabs.end()) return;
    auto index = size_t(found - tabs.begin());
    tabs.erase(found);
    if (pendingSelection == id) pendingSelection = 0;
    if (selected == id) selected = tabs.empty() ? 0 : tabs[std::min(index, tabs.size() - 1)]->id;
}

bool Workspace::close(uint64_t id, bool discard)
{
    auto tab = find(id);
    if (!tab || tab->busy() || (tab->dirty() && !discard)) return false;
    erase(id);
    return true;
}

}  // namespace tn
