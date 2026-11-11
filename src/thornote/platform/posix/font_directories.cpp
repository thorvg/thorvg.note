#include <thornote/platform/sdl/font_directories.h>
#include <cstdlib>

namespace tn::platform
{
std::vector<std::filesystem::path> fontDirectories()
{
    std::vector<std::filesystem::path> directories{"/usr/share/fonts", "/usr/local/share/fonts"};
    auto home = std::getenv("HOME");
    auto data = std::getenv("XDG_DATA_HOME");
    if (data && std::filesystem::path(data).is_absolute()) directories.emplace_back(std::filesystem::path(data) / "fonts");
    else if (home && *home) directories.emplace_back(std::filesystem::path(home) / ".local/share/fonts");
    if (home && *home) directories.emplace_back(std::filesystem::path(home) / ".fonts");
    return directories;
}
}  // namespace tn::platform
