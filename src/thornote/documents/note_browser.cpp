#include "note_browser.h"
#include <thornote/platform/display_text.h>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace tn
{
void NoteBrowser::show(const std::vector<storage::Path>& paths, bool force)
{
    auto roots = paths;
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    if (!force && roots == folders) return;
    service.cancel();
    previews.cancel();
    if (roots != folders) {
        snippets.clear();
        entries.clear();
        ++filesVersion;
    }
    folders = std::move(roots);
    auto version = ++generation;
    for (const auto& snippet : snippets)
        loadPreview(snippet.first);
    incoming.clear();
    message.clear();
    pending = folders.size();
    needsSort = false;
    if (!pending) entries.clear();
    storage::ListOptions options;
    options.textOnly = true;
    for (const auto& path : folders)
        service.list(path, [this, version](auto result) {
        if (version != generation) return;
        --pending;
        if (result) incoming.insert(incoming.end(), std::make_move_iterator(result.value.begin()), std::make_move_iterator(result.value.end()));
        else if (message.empty()) message = result.error.message;
        if (!pending) { entries = std::move(incoming); needsSort = true; } }, options);
}

bool NoteBrowser::poll()
{
    bool changed = previews.poll() != 0;
    if (!service.poll()) return changed;
    if (!needsSort) return true;
    needsSort = false;
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.modifiedAt != b.modifiedAt) return a.modifiedAt > b.modifiedAt;
        auto nameA = a.path.filename(), nameB = b.path.filename();
        return nameA != nameB ? nameA < nameB : a.path < b.path;
    });
    entries.erase(std::unique(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path == b.path; }), entries.end());
    ++filesVersion;
    return true;
}

const std::string& NoteBrowser::preview(size_t index)
{
    const auto& path = entries[index].path;
    auto [found, inserted] = snippets.try_emplace(path);
    if (inserted) loadPreview(path);
    return found->second;
}

void NoteBrowser::loadPreview(const storage::Path& path)
{
    auto version = generation;
    previews.submit([this, path, version](task::TaskContext& context) -> task::TaskQueue::Completion {
        if (context.cancelled()) return {};
        std::ifstream input(path, std::ios::binary);
        char buffer[1028];
        input.read(buffer, sizeof(buffer));
        auto count = size_t(input.gcount());
        auto length = std::min(count, size_t(1024));
        while (length < count && length && (uint8_t(buffer[length]) & 0xc0) == 0x80)
            --length;
        auto snippet = documentPreview(std::string_view(buffer, length));
        return [this, path, version, snippet = std::move(snippet)]() mutable {
            if (version == generation) snippets[path] = std::move(snippet);
        };
    });
}

std::string documentPreview(std::string_view text)
{
    if (text.substr(0, 3) == "\xef\xbb\xbf") text.remove_prefix(3);
    auto first = text.find_first_of("\r\n");
    if (first != std::string_view::npos) text.remove_prefix(first + 1);
    auto start = text.find_first_not_of("\r\n \t");
    if (start == std::string_view::npos) return {};
    text.remove_prefix(start);
    text = text.substr(0, text.find_first_of("\r\n"));
    auto length = std::min(text.size(), size_t(160));
    while (length < text.size() && length && (uint8_t(text[length]) & 0xc0) == 0x80)
        --length;
    return std::string(text.substr(0, length));
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
        auto date = modificationDate(file.modifiedAt);
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

void NoteFileList::rowHeight(size_t row, float height)
{
    auto shift = height - entries[row].height;
    if (shift == 0) return;
    entries[row].height = height;
    contentHeight += shift;
    for (size_t i = row + 1; i < entries.size(); ++i)
        entries[i].top += shift;
}

std::string modificationDate(const std::optional<int64_t>& milliseconds)
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
