#include "note_browser.h"
#include <thornote/platform/display_text.h>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace tn
{
void NoteBrowser::show(const std::vector<storage::Path>& paths, bool force)
{
    if (!force && paths == folders) return;
    service.cancel();
    if (paths != folders) {
        entries.clear();
        ++filesVersion;
    }
    folders = paths;
    auto version = ++generation;
    incoming.clear();
    message.clear();
    pending = paths.size();
    needsSort = false;
    if (!pending) entries.clear();
    storage::ListOptions options;
    options.textOnly = true;
    for (const auto& path : paths)
        service.list(path, [this, version](auto result) {
        if (version != generation) return;
        --pending;
        if (result) incoming.insert(incoming.end(), std::make_move_iterator(result.value.begin()), std::make_move_iterator(result.value.end()));
        else if (message.empty()) message = result.error.message;
        if (!pending) { entries = std::move(incoming); needsSort = true; } }, options);
}

bool NoteBrowser::poll()
{
    if (!service.poll()) return false;
    if (!needsSort) return true;
    needsSort = false;
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.createdAt != b.createdAt) return a.createdAt > b.createdAt;
        auto nameA = a.path.filename(), nameB = b.path.filename();
        return nameA != nameB ? nameA < nameB : a.path < b.path;
    });
    entries.erase(std::unique(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path == b.path; }), entries.end());
    ++filesVersion;
    return true;
}

bool NoteFileList::update(const std::vector<storage::DirectoryEntry>& files, uint64_t version, std::string_view search)
{
    if (revision == version && query == search) return false;
    revision = version;
    query = search;
    entries.clear();
    matches = 0;
    contentHeight = 0;
    std::string previousDate;
    auto contains = [&](const std::string& value) {
        return std::search(value.begin(), value.end(), query.begin(), query.end(), [](unsigned char a, unsigned char b) {
                   return std::tolower(a) == std::tolower(b);
               })
            != value.end();
    };
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];
        if (!query.empty() && !contains(displayName(file.path)) && !contains(displayPath(file.path.parent_path()))) continue;
        auto date = creationDate(file.createdAt);
        if (date != previousDate) {
            entries.push_back({{}, date, contentHeight, 28});
            contentHeight += 28;
            previousDate = std::move(date);
        }
        entries.push_back({i, {}, contentHeight, 60});
        contentHeight += 60;
        ++matches;
    }
    return true;
}

std::pair<size_t, size_t> NoteFileList::visibleRows(float top, float height) const
{
    if (height <= 0) return {0, 0};
    auto first = std::lower_bound(entries.begin(), entries.end(), top, [](const Row& row, float y) { return row.top + row.height - 4 <= y; });
    auto last = std::lower_bound(first, entries.end(), top + height, [](const Row& row, float y) { return row.top < y; });
    return {size_t(first - entries.begin()), size_t(last - entries.begin())};
}

std::string creationDate(const std::optional<int64_t>& milliseconds)
{
    if (!milliseconds) return "Date unavailable";
    auto seconds = std::time_t(*milliseconds / 1000);
    auto date = std::localtime(&seconds);
    if (!date) return "Date unavailable";
    std::ostringstream output;
    output << std::put_time(date, "%Y-%m-%d");
    return output.str();
}
}  // namespace tn
