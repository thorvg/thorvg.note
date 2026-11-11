#include "font_catalog.h"
#include <thornote/app/app_identity.h>
#include <thornote/platform/font_catalog.h>
#include <algorithm>

namespace tn
{
std::vector<FontChoice> availableFonts()
{
    auto result = platform::installedFonts();
    result.insert(result.begin(), {{}, 0, "System (Automatic)"});
    result.push_back({appData("assets/Pretendard-Regular.ttf"), 0, "Pretendard-Regular"});
    result.push_back({appData("assets/Pretendard-Bold.ttf"), 0, "Pretendard-Bold"});
    std::sort(result.begin() + (result.front().path.empty() ? 1 : 0), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    result.erase(std::unique(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.path == b.path && a.index == b.index; }), result.end());
    return result;
}
}  // namespace tn
