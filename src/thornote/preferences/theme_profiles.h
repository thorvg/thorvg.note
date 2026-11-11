#pragma once

#include <storage/storage.h>
#include <ui/ui.h>
#include <array>

namespace tn
{
struct ThemeProfile
{
    ui::Theme colors;
    std::string name;
};

ThemeProfile defaultTheme(bool dark);
bool parseTheme(std::string_view source, const std::string& filename, bool dark, ThemeProfile&, std::string& error);
std::string encodeTheme(const ThemeProfile&);

class ThemeProfiles
{
public:
    void setDirectory(storage::Path);
    bool refresh(bool dark, bool force = false);
    storage::Path edit(bool dark, std::string& error);
    const ThemeProfile& profile(bool dark) const { return entries[size_t(dark)].value; }
    const std::string& error(bool dark) const { return entries[size_t(dark)].error; }

private:
    struct Entry
    {
        ThemeProfile value;
        std::string error{};
        std::filesystem::file_time_type modified{};
        uintmax_t size = 0;
        bool watched = false, exists = false;
    };
    storage::Path path(bool dark) const;
    storage::Path directory;
    std::array<Entry, 2> entries{{{defaultTheme(false)}, {defaultTheme(true)}}};
};
}  // namespace tn
