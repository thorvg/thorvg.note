#include "theme_profiles.h"
#include <thornote/scripting/lua_state.h>
#include <thornote/path.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace tn
{
namespace
{
constexpr size_t maxThemeBytes = 64 * 1024;
struct ColorField
{
    const char* name;
    ui::Color ui::Theme::*member;
};
constexpr ColorField colorFields[]{
    {"background", &ui::Theme::background},
    {"surface", &ui::Theme::surface},
    {"raised", &ui::Theme::raised},
    {"border", &ui::Theme::border},
    {"text", &ui::Theme::text},
    {"muted", &ui::Theme::muted},
    {"accent", &ui::Theme::accent},
    {"accentText", &ui::Theme::accentText},
    {"selection", &ui::Theme::selection},
    {"highlight", &ui::Theme::highlight},
    {"highlightActive", &ui::Theme::highlightActive},
    {"success", &ui::Theme::success},
    {"danger", &ui::Theme::danger},
};

using lua::field;

bool color(std::string_view value, ui::Color& result)
{
    if ((value.size() != 7 && value.size() != 9) || value[0] != '#') return false;
    uint32_t rgba = 0;
    auto parsed = std::from_chars(value.data() + 1, value.data() + value.size(), rgba, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return false;
    if (value.size() == 7) rgba = (rgba << 8) | 255;
    result = {uint8_t(rgba >> 24), uint8_t(rgba >> 16), uint8_t(rgba >> 8), uint8_t(rgba)};
    return true;
}
}  // namespace

ThemeProfile defaultTheme(bool dark)
{
    ui::Theme colors = dark
        ? ui::Theme{{30, 30, 30}, {42, 42, 42}, {49, 49, 49}, {64, 64, 64}, {245, 245, 245}, {163, 163, 163}, {120, 174, 237}, {255, 255, 255}, {43, 65, 88}, {74, 180, 125}, {246, 119, 119}, {76, 64, 32}, {107, 80, 26}}
        : ui::Theme{{255, 255, 255}, {250, 250, 250}, {242, 242, 242}, {222, 222, 222}, {38, 38, 38}, {112, 112, 112}, {35, 112, 206}, {255, 255, 255}, {225, 236, 249}, {42, 138, 81}, {190, 53, 53}, {255, 238, 176}, {249, 215, 117}};
    colors.radius = 6;
    return {std::move(colors), dark ? "Dark" : "Light"};
}

bool parseTheme(std::string_view source, const std::string& filename, bool dark, ThemeProfile& result, std::string& error)
{
    error.clear();
    if (source.size() > maxThemeBytes) {
        error = "Theme exceeds 64 KiB";
        return false;
    }
    std::unique_ptr<lua_State, void (*)(lua_State*)> state(lua::newState(4 * 1024 * 1024), lua::close);
    auto raw = state.get();
    if (!raw) {
        error = "Cannot create theme Lua state";
        return false;
    }
    auto chunk = "@" + filename;
    if (luaL_loadbufferx(raw, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK) {
        error = lua::errorMessage(raw);
        return false;
    }
    if (!lua::protectedCall(raw, 0, 1, error)) return false;
    if (!lua_istable(raw, -1)) {
        error = "Theme must return a table";
        return false;
    }
    int table = lua_gettop(raw);
    lua_pushnil(raw);
    while (lua_next(raw, table)) {
        size_t length = 0;
        auto key = lua_type(raw, -2) == LUA_TSTRING ? lua_tolstring(raw, -2, &length) : nullptr;
        auto name = key ? std::string_view(key, length) : std::string_view{};
        if (!key || (name != "schema" && name != "name" && name != "colors" && name != "radius")) {
            error = "Unknown theme field";
            if (key) error += ": " + std::string(key, length);
            return false;
        }
        lua_pop(raw, 1);
    }
    field(raw, table, "schema");
    if (!lua_isinteger(raw, -1) || lua_tointeger(raw, -1) != 1) {
        error = "Expected theme schema 1";
        return false;
    }
    lua_pop(raw, 1);
    auto candidate = defaultTheme(dark);
    field(raw, table, "name");
    if (!lua_isnil(raw, -1)) {
        size_t length = 0;
        auto name = lua_type(raw, -1) == LUA_TSTRING ? lua_tolstring(raw, -1, &length) : nullptr;
        if (!name || !length || length > 80 || std::any_of(name, name + length, [](unsigned char c) { return c < 32 || c == 127; })) {
            error = "Theme name must contain 1 to 80 bytes without control characters";
            return false;
        }
        candidate.name.assign(name, length);
    }
    lua_pop(raw, 1);
    field(raw, table, "radius");
    if (!lua_isnil(raw, -1)) {
        auto radius = lua_type(raw, -1) == LUA_TNUMBER ? lua_tonumber(raw, -1) : -1;
        if (!std::isfinite(radius) || radius < 0 || radius > 32) {
            error = "Theme radius must be between 0 and 32";
            return false;
        }
        candidate.colors.radius = float(radius);
    }
    lua_pop(raw, 1);
    field(raw, table, "colors");
    if (!lua_isnil(raw, -1)) {
        if (!lua_istable(raw, -1)) {
            error = "Theme colors must be a table";
            return false;
        }
        int colors = lua_gettop(raw);
        lua_pushnil(raw);
        while (lua_next(raw, colors)) {
            size_t keyLength = 0, valueLength = 0;
            auto key = lua_type(raw, -2) == LUA_TSTRING ? lua_tolstring(raw, -2, &keyLength) : nullptr;
            auto entry = std::find_if(std::begin(colorFields), std::end(colorFields), [&](const auto& item) {
                return key && std::string_view(key, keyLength) == item.name;
            });
            if (entry == std::end(colorFields)) {
                error = "Unknown theme color";
                if (key) error += ": " + std::string(key, keyLength);
                return false;
            }
            auto value = lua_type(raw, -1) == LUA_TSTRING ? lua_tolstring(raw, -1, &valueLength) : nullptr;
            if (!value || !color(std::string_view(value, valueLength), candidate.colors.*entry->member)) {
                error = std::string(entry->name) + " must be #RRGGBB or #RRGGBBAA";
                return false;
            }
            lua_pop(raw, 1);
        }
    }
    result = std::move(candidate);
    return true;
}

std::string encodeTheme(const ThemeProfile& profile)
{
    std::ostringstream out;
    out << "return {\n  schema = 1,\n  name = " << std::quoted(profile.name) << ",\n  colors = {\n";
    for (const auto& field : colorFields) {
        auto value = profile.colors.*field.member;
        out << "    " << field.name << " = \"#" << std::hex << std::uppercase << std::setfill('0');
        for (auto channel : {value.r, value.g, value.b})
            out << std::setw(2) << int(channel);
        if (value.a != 255) out << std::setw(2) << int(value.a);
        out << "\",\n"
            << std::dec;
    }
    out << "  },\n  radius = " << profile.colors.radius << ",\n}\n";
    return out.str();
}

void ThemeProfiles::setDirectory(storage::Path value)
{
    directory = std::move(value);
    entries = {{{defaultTheme(false)}, {defaultTheme(true)}}};
}

storage::Path ThemeProfiles::path(bool dark) const
{
    return directory / (dark ? "dark.lua" : "light.lua");
}

bool ThemeProfiles::refresh(bool dark, bool force)
{
    if (directory.empty()) return false;
    auto& entry = entries[size_t(dark)];
    std::error_code ec;
    auto file = path(dark);
    auto exists = std::filesystem::exists(file, ec);
    auto modified = exists ? std::filesystem::last_write_time(file, ec) : std::filesystem::file_time_type{};
    auto size = exists && !ec ? std::filesystem::file_size(file, ec) : 0;
    if (!force && entry.watched && entry.exists == exists && entry.modified == modified && entry.size == size) return false;
    entry.watched = true;
    entry.exists = exists;
    entry.modified = modified;
    entry.size = size;
    entry.error.clear();
    if (ec) entry.error = "Cannot inspect theme: " + ec.message();
    else if (!exists) entry.value = defaultTheme(dark);
    else {
        auto loaded = storage::load(file, {false, maxThemeBytes});
        if (!loaded) entry.error = loaded.error.message;
        else parseTheme(loaded.value.text, utf8(file), dark, entry.value, entry.error);
    }
    return true;
}

storage::Path ThemeProfiles::edit(bool dark, std::string& error)
{
    error.clear();
    if (directory.empty()) {
        error = "Cannot locate the theme settings folder";
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = "Cannot create theme folder: " + ec.message();
        return {};
    }
    auto file = path(dark);
    if (std::filesystem::exists(file, ec)) return file;
    storage::SaveOptions options;
    options.maxBytes = maxThemeBytes;
    auto saved = storage::save(file, encodeTheme(defaultTheme(dark)), options);
    if (!saved) {
        error = "Cannot create theme: " + saved.error.message;
        return {};
    }
    return file;
}
}  // namespace tn
