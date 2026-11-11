#pragma once

#include <ui/ui.h>
#include <lua.hpp>
#include <initializer_list>

namespace ui::lua
{
// Restores the Lua stack top on scope exit.
struct Stack
{
    lua_State* state;
    int top;
    explicit Stack(lua_State* state) :
        state(state), top(lua_gettop(state)) {}
    ~Stack() { lua_settop(state, top); }
};

// Creates a sandboxed state with the ui table registered as a global; null when out of memory.
lua_State* newState(const Limits& limits);
void close(lua_State* state);
std::string errorMessage(lua_State* state);
// Calls the function below its arguments within the instruction and memory limits of the state.
bool protectedCall(lua_State* state, int arguments, int results, std::string& error);

// Raw table access without metamethods. Optional fields keep the output value when absent.
void field(lua_State* state, int table, const char* name);
bool read(lua_State* state, int table, const char* name, std::string& value, std::string& error);
bool read(lua_State* state, int table, const char* name, float& value, std::string& error);
bool read(lua_State* state, int table, const char* name, bool& value, std::string& error);
// Rejects keys outside the allowed names; a sequence also accepts dense array entries.
bool fields(lua_State* state, int table, std::initializer_list<std::string_view> allowed, std::string& error, bool sequence = false);
bool array(lua_State* state, int index, size_t limit, const char* name, size_t& size, std::string& error);

// Pushes the ui table of element constructors, length helpers and built-in icon names.
void open(lua_State* state);
// Converts and validates the element table at index.
bool readElement(lua_State* state, int index, Element& output, std::string& error);

}  // namespace ui::lua
