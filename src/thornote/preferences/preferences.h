#pragma once

#include "font_catalog.h"
#include "layout.h"
#include <thornote/documents/recent.h>
#include <array>
#include <string_view>

namespace tn
{
inline constexpr const char* preferencesOrganization = "ThorVG";
inline constexpr const char* preferencesAppId = "ThorNote";

struct Preferences
{
    static constexpr float minFontSize = 12;
    FontChoice font;
    float fontSize = 16;
    bool dark = false;
    int tabStyle = 1;
    std::array<LayoutState, layoutCount> layouts = defaultLayouts;
    Layout composition = Layout::Memo;
    RecentItems recent;
    float lineNumberScale = 1, lineNumberOffset = 0;
};
std::string encodePreferences(const Preferences&);
bool decodePreferences(std::string_view text, Preferences&);
}  // namespace tn
