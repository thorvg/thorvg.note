#include "internal.h"
#include <algorithm>
#include <cmath>

namespace ui::lua
{
namespace
{
// Option names follow the order of the enumeration they select.
constexpr std::initializer_list<std::string_view> kinds = {"row", "column", "stack", "view", "label", "button", "input", "menu", "image", "shape"};
constexpr std::initializer_list<std::string_view> alignments = {"start", "center", "end"};

template<typename T>
bool choice(lua_State* state, int table, const char* name, std::initializer_list<std::string_view> options, T& value, std::string& error)
{
    Stack stack(state);
    field(state, table, name);
    if (lua_isnil(state, -1)) return true;
    size_t size = 0;
    auto text = lua_type(state, -1) == LUA_TSTRING ? lua_tolstring(state, -1, &size) : nullptr;
    auto found = text ? std::find(options.begin(), options.end(), std::string_view(text, size)) : options.end();
    if (found != options.end()) {
        value = T(found - options.begin());
        return true;
    }
    error = std::string(name) + " must be one of:";
    for (auto option : options)
        (error += ' ') += option;
    return false;
}

bool length(lua_State* state, int table, const char* name, Length& output, std::string& error)
{
    Stack stack(state);
    field(state, table, name);
    if (lua_isnil(state, -1)) return true;
    if (lua_type(state, -1) == LUA_TNUMBER) {
        float value = float(lua_tonumber(state, -1));
        if (!std::isfinite(value) || value < 0) {
            error = std::string(name) + " must be finite nonnegative pixels";
            return false;
        }
        output = fixed(value);
        return true;
    }
    if (!lua_istable(state, -1)) {
        error = std::string(name) + " must be pixels or a length table";
        return false;
    }
    auto index = lua_gettop(state);
    if (!fields(state, index, {"basis", "flex", "min", "max"}, error)) return false;
    float basis = -1, weight = -1;
    if (!read(state, index, "basis", basis, error) || !read(state, index, "flex", weight, error) || !read(state, index, "min", output.minimum, error) || !read(state, index, "max", output.maximum, error)) return false;
    if (basis >= 0 && weight >= 0) {
        error = "length cannot specify both basis and flex";
        return false;
    }
    output.basis = basis;
    if (weight >= 0) output.weight = weight;
    return true;
}

bool menuItems(lua_State* state, int table, const std::string& prefix, std::vector<MenuItem>& output, size_t depth, size_t& count, std::string& error)
{
    Stack stack(state);
    if (depth > 32 || !lua_checkstack(state, 12)) {
        error = "menu exceeds its depth limit";
        return false;
    }
    field(state, table, "items");
    if (lua_isnil(state, -1)) return true;
    size_t size = 0;
    if (!array(state, -1, 512, "items", size, error)) return false;
    int entries = lua_gettop(state);
    for (size_t i = 1; i <= size; ++i) {
        if (++count > 2048) {
            error = "composition exceeds its element limit";
            return false;
        }
        lua_rawgeti(state, entries, lua_Integer(i));
        if (!lua_istable(state, -1)) {
            error = "menu item must be a table";
            return false;
        }
        int entry = lua_gettop(state);
        if (!fields(state, entry, {"id", "text", "command", "detail", "icon", "checked", "enabled", "separator", "items"}, error)) return false;
        MenuItem item;
        bool separator = false;
        if (!read(state, entry, "id", item.id, error) || !read(state, entry, "text", item.title, error) || !read(state, entry, "command", item.command, error) || !read(state, entry, "detail", item.detail, error) || !read(state, entry, "icon", item.icon, error) || !read(state, entry, "checked", item.checked, error) || !read(state, entry, "enabled", item.enabled, error) || !read(state, entry, "separator", separator, error)) return false;
        if (item.id.find('/') != std::string::npos) {
            error = "menu item ID cannot contain /";
            return false;
        }
        item.id = prefix + "/" + (item.id.empty() ? std::to_string(i) : item.id);
        if (!menuItems(state, entry, item.id, item.items, depth + 1, count, error)) return false;
        if (separator) {
            if (!item.command.empty() || !item.items.empty() || !item.title.empty()) {
                error = "separator cannot contain text or actions";
                return false;
            }
        } else if (item.title.empty()) {
            error = "menu item requires text";
            return false;
        }
        output.push_back(std::move(item));
        lua_pop(state, 1);
    }
    return true;
}

bool appearance(lua_State* state, int table, Appearance& output, std::string& error)
{
    Stack stack(state);
    field(state, table, "appearance");
    if (lua_isnil(state, -1)) return true;
    if (!lua_istable(state, -1)) {
        error = "appearance must be a table";
        return false;
    }
    auto index = lua_gettop(state);
    if (!fields(state, index, {"fill", "border", "text", "hoverFill", "pressedFill", "activeFill", "disabledFill", "radius", "borderWidth", "fontSize", "bold", "textAlign", "focusRing"}, error)) return false;
    const std::pair<const char*, std::optional<StyleColor> Appearance::*> colors[] = {
        {"fill", &Appearance::fill},
        {"border", &Appearance::border},
        {"text", &Appearance::text},
        {"hoverFill", &Appearance::hoverFill},
        {"pressedFill", &Appearance::pressedFill},
        {"activeFill", &Appearance::activeFill},
        {"disabledFill", &Appearance::disabledFill},
    };
    for (const auto& entry : colors) {
        field(state, index, entry.first);
        if (!lua_isnil(state, -1)) {
            std::string value;
            StyleColor color;
            if (!read(state, index, entry.first, value, error)) return false;
            if (!styleColor(value, color)) {
                error = std::string(entry.first) + ": expected a theme token, transparent, #RRGGBB or #RRGGBBAA";
                return false;
            }
            output.*entry.second = color;
        }
        lua_pop(state, 1);
    }
    if (!read(state, index, "radius", output.radius, error) || !read(state, index, "borderWidth", output.borderWidth, error)
        || !read(state, index, "fontSize", output.fontSize, error) || !read(state, index, "bold", output.bold, error)
        || !read(state, index, "focusRing", output.focusRing, error)) return false;
    field(state, index, "textAlign");
    if (lua_isnil(state, -1)) return true;
    auto align = Align::Start;
    if (!choice(state, index, "textAlign", alignments, align, error)) return false;
    output.textAlign = align;
    return true;
}

bool shapeData(lua_State* state, int table, ShapeData& output, std::string& error)
{
    Stack stack(state);
    if (!choice(state, table, "shape", {"rect", "ellipse", "path"}, output.kind, error)) return false;
    field(state, table, "viewBox");
    if (!lua_isnil(state, -1)) {
        if (!lua_istable(state, -1) || !fields(state, lua_gettop(state), {"width", "height"}, error)) {
            error = "viewBox requires width and height";
            return false;
        }
        if (!read(state, lua_gettop(state), "width", output.viewWidth, error) || !read(state, lua_gettop(state), "height", output.viewHeight, error)) return false;
    }
    lua_pop(state, 1);
    field(state, table, "path");
    if (lua_isnil(state, -1)) return true;
    size_t size = 0, length = 0;
    if (!array(state, -1, 4096, "path", size, error)) return false;
    auto entries = lua_gettop(state);
    for (size_t i = 1; i <= size; ++i) {
        lua_rawgeti(state, entries, lua_Integer(i));
        if (!array(state, -1, 7, "path segment", length, error)) return false;
        auto segment = lua_gettop(state);
        lua_rawgeti(state, segment, 1);
        std::string_view letter = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : "";
        constexpr std::string_view letters = "MLCZ";
        constexpr size_t counts[] = {2, 2, 6, 0};
        auto command = letter.size() == 1 ? letters.find(letter) : std::string_view::npos;
        lua_pop(state, 1);
        if (command == std::string_view::npos || length != counts[command] + 1) {
            error = "path segment must be {M,x,y}, {L,x,y}, {C,x1,y1,x2,y2,x,y} or {Z}";
            return false;
        }
        auto coordinates = counts[command];
        PathSegment next;
        next.command = PathCommand(command);
        for (size_t p = 0; p < coordinates; ++p) {
            lua_rawgeti(state, segment, lua_Integer(p + 2));
            if (lua_type(state, -1) != LUA_TNUMBER) {
                error = "path coordinates must be numbers";
                return false;
            }
            next.points[p] = float(lua_tonumber(state, -1));
            lua_pop(state, 1);
        }
        output.path.push_back(next);
        lua_pop(state, 1);
    }
    return true;
}

bool element(lua_State* state, int index, Element& output, size_t depth, size_t& count, std::string& error)
{
    Stack stack(state);
    if (depth > 64 || ++count > 2048) {
        error = "composition exceeds its depth or element limit";
        return false;
    }
    if (!lua_checkstack(state, 12)) {
        error = "cannot grow Lua conversion stack";
        return false;
    }
    if (!lua_istable(state, index)) {
        error = "element must be a table";
        return false;
    }
    index = lua_absindex(state, index);
    if (!fields(state, index, {"type", "id", "source", "text", "command", "width", "height", "gap", "padding", "align", "visible", "enabled", "resizable", "resizeEdge", "children", "icon", "active", "style", "tooltip", "menuTitle", "items", "overflow", "compact", "appearance", "x", "y", "shape", "path", "viewBox", "fit", "opacity"}, error, true)) return false;
    int kind = -1;
    if (!choice(state, index, "type", kinds, kind, error)) return false;
    if (kind < 0) {
        error = "element requires a type";
        return false;
    }
    output.kind = ElementKind(kind);
    bool read = lua::read(state, index, "id", output.id, error) && lua::read(state, index, "source", output.source, error) && lua::read(state, index, "text", output.text, error)
        && lua::read(state, index, "command", output.command, error) && lua::read(state, index, "tooltip", output.tooltip, error) && lua::read(state, index, "menuTitle", output.menuTitle, error)
        && lua::read(state, index, "icon", output.icon, error) && length(state, index, "width", output.width, error) && length(state, index, "height", output.height, error)
        && lua::read(state, index, "gap", output.gap, error) && lua::read(state, index, "padding", output.padding, error) && lua::read(state, index, "x", output.x, error)
        && lua::read(state, index, "y", output.y, error) && lua::read(state, index, "opacity", output.opacity, error) && lua::read(state, index, "visible", output.visible, error)
        && lua::read(state, index, "enabled", output.enabled, error) && lua::read(state, index, "resizable", output.resizable, error) && lua::read(state, index, "active", output.active, error)
        && lua::read(state, index, "compact", output.compact, error) && choice(state, index, "align", alignments, output.align, error)
        && choice(state, index, "resizeEdge", {"leading", "trailing"}, output.resizeEdge, error) && choice(state, index, "style", {"default", "primary", "quiet"}, output.style, error)
        && choice(state, index, "overflow", {"shrink", "clip"}, output.clipOverflow, error) && choice(state, index, "fit", {"contain", "cover", "stretch"}, output.fit, error)
        && appearance(state, index, output.appearance, error) && menuItems(state, index, output.id, output.items, 0, count, error);
    if (!read || (output.kind == ElementKind::Shape && !shapeData(state, index, output.shape, error))) return false;
    // Children are the array entries of the element table, or its children field.
    size_t size = lua_rawlen(state, index);
    auto children = index;
    field(state, index, "children");
    if (!lua_isnil(state, -1)) {
        if (size) {
            error = "children cannot be both a field and array entries";
            return false;
        }
        if (!array(state, -1, 2048, "children", size, error)) return false;
        children = lua_gettop(state);
    }
    for (size_t i = 1; i <= size; ++i) {
        lua_rawgeti(state, children, lua_Integer(i));
        if (!element(state, -1, output.children.emplace_back(), depth + 1, count, error)) return false;
        lua_pop(state, 1);
    }
    return true;
}

int constructor(lua_State* state)
{
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_settop(state, 1);
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_setfield(state, 1, "type");
    return 1;
}

// ui.flex(weight, min, max) and ui.preferred(pixels, min, max); the upvalue names the first field.
int lengthTable(lua_State* state)
{
    bool flexible = lua_toboolean(state, lua_upvalueindex(1));
    auto size = flexible ? luaL_optnumber(state, 1, 1) : luaL_checknumber(state, 1);
    auto minimum = luaL_optnumber(state, 2, 0);
    auto maximum = luaL_optnumber(state, 3, INFINITY);
    lua_newtable(state);
    lua_pushnumber(state, size);
    lua_setfield(state, -2, flexible ? "flex" : "basis");
    lua_pushnumber(state, minimum);
    lua_setfield(state, -2, "min");
    if (std::isfinite(maximum)) {
        lua_pushnumber(state, maximum);
        lua_setfield(state, -2, "max");
    }
    return 1;
}
}  // namespace

void open(lua_State* state)
{
    lua_newtable(state);
    for (auto name : kinds) {
        lua_pushlstring(state, name.data(), name.size());
        lua_pushcclosure(state, constructor, 1);
        lua_setfield(state, -2, name.data());
    }
    lua_newtable(state);
    for (auto name : builtinIcons()) {
        lua_pushlstring(state, name.data(), name.size());
        lua_setfield(state, -2, name.data());
    }
    lua_setfield(state, -2, "icons");
    for (auto flexible : {true, false}) {
        lua_pushboolean(state, flexible);
        lua_pushcclosure(state, lengthTable, 1);
        lua_setfield(state, -2, flexible ? "flex" : "preferred");
    }
}

bool readElement(lua_State* state, int index, Element& output, std::string& error)
{
    error.clear();
    Element next;
    size_t count = 0;
    if (!element(state, index, next, 0, count, error) || !validate(next, error)) return false;
    output = std::move(next);
    return true;
}

}  // namespace ui::lua
