#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <storage/storage.h>

namespace tn
{
void browserOpenFiles(std::function<void(const char* const*, const char*)> complete);
void browserSync(std::function<void(std::string)> complete);
std::string browserSavePath(std::string_view suggested);
void browserRestoreFile(storage::RecoverySnapshot& snapshot);
void browserExport(std::string_view name, std::string_view text, storage::FileFormat format);

struct BrowserRecoveryCallbacks
{
    std::function<bool()> prepare, commit;
    std::function<void(std::string)> complete;
};
void* browserRecoveryStart(const storage::Path&, BrowserRecoveryCallbacks);
std::string browserRecoveryAdd(void*, const storage::RecoverySnapshot&);
void browserRecoverySubmit(void*, const std::string& obsolete);
}  // namespace tn
