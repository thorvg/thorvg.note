#include <thornote/platform/font_catalog.h>
#include "font_directories.h"
#include <algorithm>
#include <cctype>

namespace tn::platform
{
std::vector<FontChoice> installedFonts()
{
    std::vector<FontChoice> result;
    for (const auto& directory : fontDirectories()) {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            auto extension = utf8(it->path().extension());
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (extension == ".ttf" || extension == ".otf")
                result.push_back({it->path(), 0, utf8(it->path().stem())});
        }
    }
    return result;
}
}  // namespace tn::platform
