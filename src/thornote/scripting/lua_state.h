#pragma once

#include <cstddef>
#include <string>

struct lua_State;

namespace tn::lua
{
// A state with the base, table, string, math and utf8 libraries and no file or chunk loaders.
// Allocations beyond `memory` bytes fail while script code runs (0: unlimited). Free it with close().
lua_State* newState(size_t memory = 0);
void close(lua_State*);
std::string errorMessage(lua_State*);
// Instruction-budgeted call; removes the call values on failure.
bool protectedCall(lua_State*, int arguments, int results, std::string& error);
// Pushes table[name] without metamethods.
void field(lua_State*, int table, const char* name);
// Size of the dense array at `index` with at most `limit` entries; `name` labels errors.
bool array(lua_State*, int index, size_t limit, const char* name, size_t& size, std::string& error);
}  // namespace tn::lua
