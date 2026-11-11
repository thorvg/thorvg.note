#include "module.h"
#include <ui/ui.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace tn::input
{
size_t arraySize(lua_State* state, int index, size_t maximum, const char* name)
{
    size_t size = 0;
    std::string error;
    if (!lua::array(state, index, maximum, name, size, error)) throw std::runtime_error(error);
    return size;
}

namespace
{
using lua::field;

std::string string(lua_State* state, int index)
{
    if (lua_type(state, index) != LUA_TSTRING) throw std::runtime_error("expected a string");
    size_t size;
    auto value = lua_tolstring(state, index, &size);
    return {value, size};
}

bool identifier(std::string_view value)
{
    return !value.empty() && value.size() <= 96 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.';
    });
}

std::vector<ui::KeyStroke> sequence(const std::string& value)
{
    std::vector<ui::KeyStroke> keys;
    std::istringstream stream(value);
    std::string token;
    while (stream >> token) {
        ui::KeyStroke key;
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        size_t begin = 0, end;
        while ((end = token.find('+', begin)) != std::string::npos) {
            auto modifier = token.substr(begin, end - begin);
            if (modifier == "ctrl" || modifier == "control") key.control = true;
            else if (modifier == "alt") key.alt = true;
            else if (modifier == "meta" || modifier == "cmd" || modifier == "super") key.meta = true;
            else if (modifier == "shift") key.shift = true;
            else if (modifier == "primary" || modifier == "mod") key.primary = true;
            else throw std::runtime_error("unknown key modifier: " + modifier);
            begin = end + 1;
        }
        key.key = token.substr(begin);
        if (key.key == "space") key.key = " ";
        else if (key.key == "plus") key.key = "+";
        else if (key.key == "esc") key.key = "escape";
        else if (key.key == "return") key.key = "enter";
        if (key.key.empty() || keys.size() == 8) throw std::runtime_error("expected 1 to 8 keys");
        keys.push_back(std::move(key));
    }
    if (keys.empty()) throw std::runtime_error("binding keys cannot be empty");
    return keys;
}
}  // namespace

Module::Module(Services& services) :
    services(services), state(lua::newState())
{
    if (state) {
        *static_cast<void**>(lua_getextraspace(state)) = this;
        openEditorApi();
    }
}

Module::~Module()
{
    release();
    lua::close(state);
}

int Module::referenceField(const char* name)
{
    field(state, -1, name);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return LUA_NOREF;
    }
    if (!lua_isfunction(state, -1)) throw std::runtime_error(std::string(name) + " must be a function");
    return luaL_ref(state, LUA_REGISTRYINDEX);
}

bool Module::load(std::string_view source, std::string_view filename)
{
    if (!state) {
        error = "cannot allocate Lua state";
        return false;
    }
    auto name = std::string(filename);
    if (luaL_loadbufferx(state, source.data(), source.size(), name.c_str(), "t") != LUA_OK) {
        error = lua::errorMessage(state);
        lua_pop(state, 1);
        return false;
    }
    if (!call(0, 1, false)) return false;
    try {
        if (!lua_istable(state, -1)) throw std::runtime_error("extension must return a table");
        field(state, -1, "schema");
        bool version = lua_isinteger(state, -1) && lua_tointeger(state, -1) == 1;
        lua_pop(state, 1);
        if (!version) throw std::runtime_error("extension schema must be 1");
        field(state, -1, "id");
        id = string(state, -1);
        lua_pop(state, 1);
        if (!identifier(id)) throw std::runtime_error("invalid extension id");
        field(state, -1, "priority");
        if (!lua_isnil(state, -1)) {
            if (!lua_isinteger(state, -1) || lua_tointeger(state, -1) < -10000 || lua_tointeger(state, -1) > 10000)
                throw std::runtime_error("priority must be an integer in [-10000, 10000]");
            priority = int(lua_tointeger(state, -1));
        }
        lua_pop(state, 1);
        onKey = referenceField("on_key");
        onText = referenceField("on_text");
        onEvent = referenceField("on_event");
        field(state, -1, "commands");
        if (!lua_isnil(state, -1)) {
            if (!lua_istable(state, -1)) throw std::runtime_error("commands must be a table");
            lua_pushnil(state);
            while (lua_next(state, -2)) {
                auto name = string(state, -2);
                if (!identifier(name) || commands.size() >= 256) throw std::runtime_error("invalid or too many commands");
                auto title = name;
                int run;
                if (lua_isfunction(state, -1)) {
                    lua_pushvalue(state, -1);
                    run = luaL_ref(state, LUA_REGISTRYINDEX);
                } else if (lua_istable(state, -1)) {
                    field(state, -1, "title");
                    title = string(state, -1);
                    lua_pop(state, 1);
                    run = referenceField("run");
                    if (run == LUA_NOREF) throw std::runtime_error("command requires run");
                } else throw std::runtime_error("command must be a function or table");
                commands.emplace(id + "." + name, run);
                titles.emplace(id + "." + name, title);
                lua_pop(state, 1);
            }
        }
        lua_pop(state, 1);
        field(state, -1, "bindings");
        if (!lua_isnil(state, -1)) {
            auto count = arraySize(state, -1, 512, "bindings");
            for (size_t i = 1; i <= count; ++i) {
                lua_rawgeti(state, -1, lua_Integer(i));
                if (!lua_istable(state, -1)) throw std::runtime_error("binding must be a table");
                Binding binding;
                field(state, -1, "keys");
                binding.keys = sequence(string(state, -1));
                lua_pop(state, 1);
                field(state, -1, "command");
                if (!lua_isnil(state, -1)) binding.command = string(state, -1);
                lua_pop(state, 1);
                binding.run = referenceField("run");
                binding.when = referenceField("when");
                if ((binding.run != LUA_NOREF) == !binding.command.empty()) throw std::runtime_error("binding requires either command or run");
                bindings.push_back(std::move(binding));
                lua_pop(state, 1);
            }
        }
        lua_settop(state, 0);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        lua_settop(state, 0);
        return false;
    }
}

bool Module::call(int arguments, int results, bool allowWrites)
{
    consumedInput = false;
    executing = true;
    writable = allowWrites;
    auto success = lua::protectedCall(state, arguments, results, error);
    executing = writable = false;
    if (!success) {
        live = false;
        release();
    }
    return success;
}

ui::InputResult Module::result()
{
    ui::InputResult result = ui::InputResult::Pass;
    if (lua_isboolean(state, -1)) result = lua_toboolean(state, -1) ? ui::InputResult::Handled : ui::InputResult::Pass;
    else if (!lua_isnil(state, -1)) {
        size_t size = 0;
        auto text = lua_type(state, -1) == LUA_TSTRING ? lua_tolstring(state, -1, &size) : nullptr;
        auto value = text ? std::string_view(text, size) : std::string_view{};
        if (value == "handled") result = ui::InputResult::Handled;
        else if (value == "pending") result = ui::InputResult::Pending;
        else if (value != "pass") {
            error = "input handler must return pass, handled, pending, boolean or nil";
            live = false;
            release();
        }
    }
    lua_pop(state, 1);
    return !live && consumedInput ? ui::InputResult::Handled : result;
}

ui::InputResult Module::key(uint64_t id, const ui::KeyStroke& key)
{
    if (!live || onKey == LUA_NOREF) return ui::InputResult::Pass;
    lua_rawgeti(state, LUA_REGISTRYINDEX, onKey);
    pushEditor(id);
    lua_newtable(state);
    lua_pushlstring(state, key.key.data(), key.key.size());
    lua_setfield(state, -2, "key");
    for (auto item : {std::pair{"shift", key.shift}, {"ctrl", key.control}, {"alt", key.alt}, {"meta", key.meta}, {"primary", key.primary}, {"repeat", key.repeated}, {"down", key.down}}) {
        lua_pushboolean(state, item.second);
        lua_setfield(state, -2, item.first);
    }
    return call(2, 1) ? result() : consumedInput ? ui::InputResult::Handled
                                                 : ui::InputResult::Pass;
}

ui::InputResult Module::text(uint64_t id, std::string_view text)
{
    if (!live || onText == LUA_NOREF) return ui::InputResult::Pass;
    lua_rawgeti(state, LUA_REGISTRYINDEX, onText);
    pushEditor(id);
    lua_pushlstring(state, text.data(), text.size());
    return call(2, 1) ? result() : consumedInput ? ui::InputResult::Handled
                                                 : ui::InputResult::Pass;
}

bool Module::predicate(uint64_t id, int handler)
{
    if (handler == LUA_NOREF) return true;
    lua_rawgeti(state, LUA_REGISTRYINDEX, handler);
    pushEditor(id);
    if (!call(1, 1, false)) return false;
    bool value = lua_toboolean(state, -1);
    lua_pop(state, 1);
    return value;
}

void Module::run(uint64_t id, int handler)
{
    if (!live) return;
    lua_rawgeti(state, LUA_REGISTRYINDEX, handler);
    pushEditor(id);
    call(1, 0);
}

void Module::event(const Event& event)
{
    if (!live || onEvent == LUA_NOREF) return;
    lua_rawgeti(state, LUA_REGISTRYINDEX, onEvent);
    pushEditor(event.editor);
    lua_newtable(state);
    lua_pushlstring(state, event.type.data(), event.type.size());
    lua_setfield(state, -2, "type");
    lua_pushlstring(state, event.origin.data(), event.origin.size());
    lua_setfield(state, -2, "origin");
    lua_pushinteger(state, lua_Integer(event.state.revision));
    lua_setfield(state, -2, "revision");
    lua_pushinteger(state, lua_Integer(event.state.selection.anchor));
    lua_setfield(state, -2, "anchor");
    lua_pushinteger(state, lua_Integer(event.state.selection.caret));
    lua_setfield(state, -2, "caret");
    lua_pushboolean(state, event.state.focused);
    lua_setfield(state, -2, "focused");
    call(2, 0);
}

Snapshot Module::snapshot(const ui::TextEditor& editor, bool focused)
{
    return {editor.session().document().revision(), editor.session().selection(), focused};
}

bool Module::matches(const ui::KeyStroke& pattern, const ui::KeyStroke& key)
{
    auto primaryControl = ui::platform::modifiers({false, true, false, false}).primary;
    return pattern.key == key.key && pattern.shift == key.shift && pattern.alt == key.alt && (pattern.control || (pattern.primary && primaryControl)) == key.control && (pattern.meta || (pattern.primary && !primaryControl)) == key.meta;
}

void Module::forget(uint64_t id)
{
    if (groups.erase(id)) services.undoGroup(*this, id, false);
    presentation.erase(id);
}

void Module::release()
{
    while (!groups.empty())
        forget(*groups.begin());
    presentation.clear();
}
}  // namespace tn::input
