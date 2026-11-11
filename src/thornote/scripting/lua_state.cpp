#include "lua_state.h"
#include <lua.hpp>
#include <cstdlib>
#include <utility>

namespace tn::lua
{
namespace
{
struct Budget;
thread_local Budget* activeBudget = nullptr;
struct Budget
{
    lua_State* state;
    const void* registry;
    Budget* previous = activeBudget;
    lua_Hook hook;
    int mask, count, remaining = 200;
    bool exhausted = false;

    explicit Budget(lua_State* state) :
        state(state), registry(lua_topointer(state, LUA_REGISTRYINDEX)), hook(lua_gethook(state)), mask(lua_gethookmask(state)), count(lua_gethookcount(state))
    {
        activeBudget = this;
    }
    ~Budget()
    {
        lua_sethook(state, hook, mask, count);
        activeBudget = previous;
    }
};
Budget* budgetFor(lua_State* state)
{
    auto registry = lua_topointer(state, LUA_REGISTRYINDEX);
    auto budget = activeBudget;
    while (budget && budget->registry != registry)
        budget = budget->previous;
    return budget;
}
void limit(lua_State* state, lua_Debug*)
{
    auto budget = budgetFor(state);
    if (!budget || --budget->remaining > 0) return;
    budget->exhausted = true;
    // Catching the error must not allow the next Lua instruction to run.
    lua_sethook(state, limit, LUA_MASKCOUNT, 1);
    luaL_error(state, "Lua instruction budget exceeded");
}
int handleError(lua_State* state)
{
    // Lua disables hooks while a hook error invokes an xpcall handler.
    auto budget = budgetFor(state);
    if (budget && budget->exhausted) return 1;
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, 1, 1);
    return 1;
}
// Bounds allocations while protectedCall() runs script code; host-side reads stay unbounded.
struct Memory
{
    size_t used = 0, limit = 0;
    bool running = false;
    static void* allocate(void* data, void* pointer, size_t oldSize, size_t newSize)
    {
        auto& memory = *static_cast<Memory*>(data);
        oldSize = pointer ? oldSize : 0;
        if (!newSize) {
            std::free(pointer);
            memory.used -= oldSize;
            return nullptr;
        }
        if (memory.running && memory.limit && (newSize > memory.limit || memory.used - oldSize > memory.limit - newSize)) return nullptr;
        auto next = std::realloc(pointer, newSize);
        if (next) memory.used = memory.used - oldSize + newSize;
        return next;
    }
};
int guardedXpcall(lua_State* state)
{
    auto arguments = lua_gettop(state);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    lua_pushvalue(state, 2);
    lua_pushcclosure(state, handleError, 1);
    lua_replace(state, 2);
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, arguments, LUA_MULTRET);
    return lua_gettop(state);
}
}  // namespace

lua_State* newState(size_t memory)
{
    auto budget = new Memory{0, memory};
#if LUA_VERSION_NUM >= 505
    auto state = lua_newstate(Memory::allocate, budget, 0);
#else
    auto state = lua_newstate(Memory::allocate, budget);
#endif
    if (!state) {
        delete budget;
        return nullptr;
    }
    for (const auto& library : {std::pair<const char*, lua_CFunction>{"_G", luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}}) {
        luaL_requiref(state, library.first, library.second, 1);
        lua_pop(state, 1);
    }
    // Error handlers must not outlive an exhausted instruction budget.
    lua_getglobal(state, "xpcall");
    lua_pushcclosure(state, guardedXpcall, 1);
    lua_setglobal(state, "xpcall");
    for (auto name : {"dofile", "loadfile", "load"}) {
        lua_pushnil(state);
        lua_setglobal(state, name);
    }
    return state;
}

void close(lua_State* state)
{
    if (!state) return;
    void* budget = nullptr;
    lua_getallocf(state, &budget);
    lua_close(state);
    delete static_cast<Memory*>(budget);
}

std::string errorMessage(lua_State* state)
{
    size_t size = 0;
    auto message = lua_tolstring(state, -1, &size);
    return message ? std::string(message, size) : "Lua raised a non-string error";
}

bool protectedCall(lua_State* state, int arguments, int results, std::string& error)
{
    auto base = lua_gettop(state) - arguments - 1;
    Budget budget(state);
    lua_sethook(state, limit, LUA_MASKCOUNT, 1000);
    void* data = nullptr;
    auto memory = lua_getallocf(state, &data) == Memory::allocate ? static_cast<Memory*>(data) : nullptr;
    auto running = memory && std::exchange(memory->running, true);
    auto status = lua_pcall(state, arguments, results, 0);
    if (memory) memory->running = running;
    if (budget.exhausted) {
        lua_settop(state, base);
        error = "Lua instruction budget exceeded";
        return false;
    }
    if (status == LUA_OK) {
        error.clear();
        return true;
    }
    error = errorMessage(state);
    lua_pop(state, 1);
    return false;
}

void field(lua_State* state, int table, const char* name)
{
    table = lua_absindex(state, table);
    lua_pushstring(state, name);
    lua_rawget(state, table);
}

bool array(lua_State* state, int index, size_t limit, const char* name, size_t& size, std::string& error)
{
    if (!lua_istable(state, index)) {
        error = std::string(name) + " must be an array";
        return false;
    }
    auto top = lua_gettop(state);
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
    lua_settop(state, top);
    if (entries == size) return true;
    error = std::string(name) + " must be a dense array";
    return false;
}
}  // namespace tn::lua
