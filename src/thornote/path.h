#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace tn
{
inline std::string utf8(const std::filesystem::path& path)
{
    auto text = path.u8string();
    return {text.begin(), text.end()};
}

inline std::filesystem::path utf8Path(std::string_view text)
{
    return std::u8string(text.begin(), text.end());
}
}  // namespace tn
