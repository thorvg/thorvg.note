#include "recent.h"
#include <algorithm>
#include <set>

namespace tn
{
namespace
{
storage::Path normalizedDirectory(const storage::Path& path)
{
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error) normalized = path.lexically_normal();
    if (normalized != normalized.root_path() && normalized.filename().empty()) normalized = normalized.parent_path();
    return normalized;
}
bool contains(const storage::Path& directory, const storage::Path& path)
{
    return std::mismatch(directory.begin(), directory.end(), path.begin(), path.end()).first == directory.end();
}
void remember(std::vector<storage::Path>& entries, const storage::Path& path)
{
    if (path.empty() || !path.is_absolute()) return;
    auto normalized = normalizedDirectory(path);
    entries.erase(std::remove(entries.begin(), entries.end(), normalized), entries.end());
    entries.insert(entries.begin(), std::move(normalized));
}
}  // namespace

void RecentItems::rememberFile(const storage::Path& path, bool rememberParent)
{
    auto normalized = path.lexically_normal();
    remember(files, normalized);
    if (files.size() > fileLimit) files.resize(fileLimit);
    if (rememberParent && !normalized.empty()) rememberDirectory(normalized.parent_path());
}
void RecentItems::rememberDirectory(const storage::Path& path)
{
    remember(directories, path);
}
void RecentItems::removeDirectory(const storage::Path& path)
{
    if (path.empty() || !path.is_absolute()) return;
    auto normalized = normalizedDirectory(path);
    directories.erase(std::remove_if(directories.begin(), directories.end(), [&](const auto& entry) { return contains(normalized, entry); }), directories.end());
}

const std::vector<storage::Path>& RecentItems::rootDirectories() const
{
    if (directorySnapshot == directories) return directoryRoots;
    directorySnapshot = directories;
    directoryRoots.clear();
    std::vector<storage::Path> candidates;
    for (const auto& path : directories)
        if (!path.empty() && path.is_absolute()) candidates.push_back(normalizedDirectory(path));
    std::set<storage::Path> registered(candidates.begin(), candidates.end()), emitted;
    for (const auto& path : candidates) {
        bool nested = false;
        for (auto parent = path.parent_path(); !parent.empty() && parent != path;) {
            if (registered.count(parent)) {
                nested = true;
                break;
            }
            auto next = parent.parent_path();
            if (next == parent) break;
            parent = std::move(next);
        }
        if (!nested && emitted.insert(path).second) directoryRoots.push_back(path);
    }
    return directoryRoots;
}

storage::Path RecentItems::rootDirectory(const storage::Path& path) const
{
    if (path.empty() || !path.is_absolute()) return {};
    auto normalized = normalizedDirectory(path);
    for (const auto& root : rootDirectories())
        if (contains(root, normalized)) return root;
    return {};
}
}  // namespace tn
