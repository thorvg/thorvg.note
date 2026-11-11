#include "preferences.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace tn
{
namespace
{
// Preserve version 1's IDs: Memo 0, retired Rich 1, Note 2, retired Minimal 3.
int storedLayout(Layout layout)
{
    return layout == Layout::Note ? 2 : 0;
}
}  // namespace

std::string encodePreferences(const Preferences& value)
{
    std::ostringstream out;
    out << "thornote-settings 1\nfont " << std::quoted(utf8(value.font.path)) << ' ' << value.font.index << ' ' << std::quoted(value.font.name)
        << "\nfont-size " << value.fontSize << "\ndark " << value.dark << "\ntabs " << value.tabStyle << "\ncomposition " << storedLayout(value.composition) << '\n';
    for (size_t i = 0; i < value.layouts.size(); ++i) {
        const auto& state = value.layouts[i];
        out << "layout " << storedLayout(Layout(i)) << ' ' << state.sidebar << ' ' << state.properties << ' ' << state.lineNumbers << ' '
            << state.sidebarWidth << ' ' << state.notesWidth << ' ' << state.propertiesWidth << '\n';
    }
    for (const auto& path : value.recent.files)
        out << "recent-file " << std::quoted(utf8(path)) << '\n';
    for (const auto& path : value.recent.directories)
        out << "recent-directory " << std::quoted(utf8(path)) << '\n';
    return out.str();
}

bool decodePreferences(std::string_view text, Preferences& result)
{
    std::istringstream in{std::string(text)};
    std::string key;
    unsigned version = 0;
    if (!(in >> key >> version) || key != "thornote-settings" || version != 1) return false;
    Preferences value;
    while (in >> key) {
        if (key == "font") {
            std::string path;
            if (!(in >> std::quoted(path) >> value.font.index >> std::quoted(value.font.name)) || value.font.index > 255) return false;
            value.font.path = utf8Path(path);
        } else if (key == "font-size") {
            if (!(in >> value.fontSize) || !std::isfinite(value.fontSize) || value.fontSize <= 0) return false;
        } else if (key == "dark") {
            if (!(in >> value.dark)) return false;
        } else if (key == "tabs") {
            if (!(in >> value.tabStyle) || value.tabStyle < 0 || value.tabStyle > 2) return false;
        } else if (key == "composition") {
            int index;
            if (!(in >> index) || index < 0 || index > 3) return false;
            value.composition = index == 2 ? Layout::Note : Layout::Memo;
        } else if (key == "layout") {
            size_t index = 0;
            if (!(in >> index) || index > 3) return false;
            LayoutState state{};
            if (!(in >> state.sidebar >> state.properties >> state.lineNumbers >> state.sidebarWidth >> state.notesWidth >> state.propertiesWidth)) return false;
            for (auto width : {state.sidebarWidth, state.notesWidth, state.propertiesWidth})
                if (!std::isfinite(width) || width < 100 || width > 1000) return false;
            if (index == 0) value.layouts[size_t(Layout::Memo)] = state;
            else if (index == 2) value.layouts[size_t(Layout::Note)] = state;
        } else if (key == "recent-file" || key == "recent-directory") {
            std::string text;
            if (!(in >> std::quoted(text))) return false;
            auto path = utf8Path(text).lexically_normal();
            if (path.empty() || !path.is_absolute()) return false;
            auto& entries = key == "recent-file" ? value.recent.files : value.recent.directories;
            if ((key != "recent-file" || entries.size() < RecentItems::fileLimit) && std::find(entries.begin(), entries.end(), path) == entries.end()) entries.push_back(std::move(path));
        } else {
            std::string ignored;
            std::getline(in, ignored);
        }
    }
    if (!in.eof()) return false;
    result = std::move(value);
    return true;
}
}  // namespace tn
