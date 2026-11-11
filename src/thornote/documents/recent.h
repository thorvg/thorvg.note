#pragma once

#include <storage/storage.h>
#include <vector>

namespace tn
{
struct RecentItems
{
    static constexpr size_t fileLimit = 10;
    std::vector<storage::Path> files, directories;
    void rememberFile(const storage::Path&, bool rememberParent = true);
    void rememberDirectory(const storage::Path&);
    void removeDirectory(const storage::Path&);
    const std::vector<storage::Path>& rootDirectories() const;
    storage::Path rootDirectory(const storage::Path&) const;

private:
    mutable std::vector<storage::Path> directorySnapshot, directoryRoots;
};
}  // namespace tn
