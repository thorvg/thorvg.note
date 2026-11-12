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
void browserExport(std::string_view name, std::string_view text, storage::FileFormat format);
}  // namespace tn
