#pragma once

#include <thornote/path.h>
#include <filesystem>
#include <string>
#include <string_view>

namespace tn
{
// Composed (NFC) copy of text for display. macOS hands out decomposed file names, and the
// renderer has no shaping to rejoin them, so a decomposed name would be drawn as loose letters.
std::string composedText(std::string_view text);
// File names and paths as shown to the user; the paths themselves keep their stored bytes.
inline std::string displayName(const std::filesystem::path& path)
{
    return composedText(utf8(path.filename()));
}
inline std::string displayPath(const std::filesystem::path& path)
{
    return composedText(utf8(path));
}
}  // namespace tn
