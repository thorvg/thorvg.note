#include "internal.h"
#include <algorithm>
#include <cmath>

namespace ui::lua
{

void field(lua_State* state, int table, const char* name)
{
    table = lua_absindex(state, table);
    lua_pushstring(state, name);
    lua_rawget(state, table);
}

bool read(lua_State* state, int table, const char* name, std::string& value, std::string& error)
{
    Stack stack(state);
    field(state, table, name);
    if (lua_isnil(state, -1)) return true;
    if (lua_type(state, -1) != LUA_TSTRING) {
        error = std::string(name) + " must be a string";
        return false;
    }
    size_t size;
    auto data = lua_tolstring(state, -1, &size);
    value.assign(data, size);
    return true;
}

bool read(lua_State* state, int table, const char* name, float& value, std::string& error)
{
    Stack stack(state);
    field(state, table, name);
    if (lua_isnil(state, -1)) return true;
    auto number = float(lua_tonumber(state, -1));
    if (lua_type(state, -1) != LUA_TNUMBER || !std::isfinite(number)) {
        error = std::string(name) + " must be a finite number";
        return false;
    }
    value = number;
    return true;
}

bool read(lua_State* state, int table, const char* name, bool& value, std::string& error)
{
    Stack stack(state);
    field(state, table, name);
    if (lua_isnil(state, -1)) return true;
    if (!lua_isboolean(state, -1)) {
        error = std::string(name) + " must be a boolean";
        return false;
    }
    value = lua_toboolean(state, -1);
    return true;
}

bool fields(lua_State* state, int table, std::initializer_list<std::string_view> allowed, std::string& error, bool sequence)
{
    Stack stack(state);
    table = lua_absindex(state, table);
    auto length = sequence ? lua_Integer(lua_rawlen(state, table)) : 0;
    lua_pushnil(state);
    while (lua_next(state, table)) {
        if (lua_isinteger(state, -2) && lua_tointeger(state, -2) >= 1 && lua_tointeger(state, -2) <= length) {
            lua_pop(state, 1);
            continue;
        }
        size_t size = 0;
        auto key = lua_type(state, -2) == LUA_TSTRING ? lua_tolstring(state, -2, &size) : nullptr;
        if (!key) {
            error = sequence ? "table entries must be named fields or a dense array" : "table fields must have string keys";
            return false;
        }
        if (std::find(allowed.begin(), allowed.end(), std::string_view(key, size)) == allowed.end()) {
            error = "unknown field: " + std::string(key, size);
            return false;
        }
        lua_pop(state, 1);
    }
    return true;
}

bool array(lua_State* state, int index, size_t limit, const char* name, size_t& size, std::string& error)
{
    if (!lua_istable(state, index)) {
        error = std::string(name) + " must be an array";
        return false;
    }
    Stack stack(state);
    index = lua_absindex(state, index);
    size = lua_rawlen(state, index);
    if (size > limit) {
        error = std::string(name) + " exceeds " + std::to_string(limit) + " entries";
        return false;
    }
    size_t entries = 0;
    lua_pushnil(state);
    while (lua_next(state, index)) {
        lua_pop(state, 1);
        if (!lua_isinteger(state, -1) || lua_tointeger(state, -1) < 1 || size_t(lua_tointeger(state, -1)) > size) {
            entries = size + 1;
            break;
        }
        ++entries;
    }
    if (entries == size) return true;
    error = std::string(name) + " must be a dense array";
    return false;
}

}  // namespace ui::lua
