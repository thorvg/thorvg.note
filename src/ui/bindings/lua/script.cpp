#include "internal.h"
#include <cmath>

namespace ui::lua
{
namespace
{
bool name(const std::string& value)
{
    return !value.empty() && value.find('\0') == std::string::npos;
}

bool readIcons(lua_State* state, int module, std::vector<IconFile>& icons, std::string& error)
{
    Stack stack(state);
    field(state, module, "icons");
    if (lua_isnil(state, -1)) return true;
    size_t size = 0;
    if (!array(state, -1, 128, "icons", size, error)) return false;
    int entries = lua_gettop(state);
    for (size_t i = 1; i <= size; ++i) {
        lua_rawgeti(state, entries, lua_Integer(i));
        if (!lua_istable(state, -1)) {
            error = "icon must be a table";
            return false;
        }
        int entry = lua_gettop(state);
        IconFile icon;
        auto& asset = icon.asset;
        if (!fields(state, entry, {"name", "file", "poses", "tint", "duration"}, error) || !read(state, entry, "name", asset.name, error) || !read(state, entry, "file", icon.file, error) || !read(state, entry, "tint", asset.tint, error) || !read(state, entry, "duration", asset.duration, error)) return false;
        if (!name(asset.name) || !name(icon.file)) {
            error = "icon name and file must be nonempty strings without NUL bytes";
            return false;
        }
        field(state, entry, "poses");
        if (!lua_isnil(state, -1)) {
            if (!lua_istable(state, -1)) {
                error = "icon poses must be a table";
                return false;
            }
            int aliases = lua_gettop(state);
            if (!fields(state, aliases, {"idle", "hover", "pressed", "active", "disabled", "focused"}, error)) return false;
            // The default markers are the pose names.
            for (auto& marker : asset.markers)
                if (!read(state, aliases, std::string(marker).c_str(), marker, error)) return false;
        }
        icons.push_back(std::move(icon));
        lua_settop(state, entries);
    }
    return true;
}

bool readImages(lua_State* state, int module, std::vector<ImageFile>& images, std::string& error)
{
    Stack stack(state);
    field(state, module, "images");
    if (lua_isnil(state, -1)) return true;
    size_t size = 0;
    if (!array(state, -1, 128, "images", size, error)) return false;
    auto entries = lua_gettop(state);
    for (size_t i = 1; i <= size; ++i) {
        lua_rawgeti(state, entries, lua_Integer(i));
        ImageFile image;
        if (!lua_istable(state, -1) || !fields(state, -1, {"name", "file"}, error) || !read(state, -1, "name", image.name, error) || !read(state, -1, "file", image.file, error) || !name(image.name) || !name(image.file)) {
            error = "image requires a name and a file";
            return false;
        }
        images.push_back(std::move(image));
        lua_pop(state, 1);
    }
    return true;
}

struct Push
{
    lua_State* state;

    void operator()(bool value) const { lua_pushboolean(state, value); }
    void operator()(double value) const { lua_pushnumber(state, value); }
    void operator()(const std::string& value) const { lua_pushlstring(state, value.data(), value.size()); }
    void operator()(const std::vector<Record>& records) const
    {
        lua_createtable(state, int(records.size()), 0);
        for (size_t i = 0; i < records.size(); ++i) {
            table(records[i]);
            lua_rawseti(state, -2, lua_Integer(i + 1));
        }
    }
    template<typename T>
    void table(const T& values) const
    {
        lua_createtable(state, 0, int(values.size()));
        for (const auto& entry : values) {
            std::visit(*this, entry.second);
            lua_setfield(state, -2, entry.first.c_str());
        }
    }
};
}  // namespace

struct Script::Impl
{
    const CommandRegistry& host;
    lua_State* state;
    int builder = LUA_NOREF;
    bool executing = false, dispatching = false;
    std::vector<std::string> queued;
    std::vector<Command> commands;
    std::vector<IconFile> icons;
    std::vector<ImageFile> images;
    uint64_t revision = 0;
    std::string message;

    Impl(const CommandRegistry& host, const Limits& limits) :
        host(host), state(newState(limits)) {}
    ~Impl() { close(state); }

    // app.commands.execute(id): queues a host command while a script command runs.
    static int execute(lua_State* state)
    {
        auto self = static_cast<Impl*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!self->executing) return luaL_error(state, "app commands are only available inside command handlers");
        if (self->queued.size() >= 128) return luaL_error(state, "too many queued commands");
        size_t size;
        auto id = luaL_checklstring(state, 1, &size);
        self->queued.emplace_back(id, size);
        return 0;
    }

    void invoke(int handler)
    {
        if (executing || dispatching) return;
        Stack stack(state);
        message.clear();
        queued.clear();
        executing = true;
        lua_rawgeti(state, LUA_REGISTRYINDEX, handler);
        bool success = protectedCall(state, 0, 0, message);
        executing = false;
        ++revision;
        auto pending = std::move(queued);
        if (!success) return;
        // Dispatch after returning from Lua, so host commands cannot reenter this Lua call.
        dispatching = true;
        for (const auto& id : pending) {
            if (host.execute(id)) continue;
            message = "unknown or unavailable command: " + id;
            break;
        }
        dispatching = false;
    }
};

Script::Script(const CommandRegistry& commands, Limits limits) :
    impl(std::make_shared<Impl>(commands, limits))
{
    auto state = impl->state;
    if (!state) return;
    lua_newtable(state);
    lua_newtable(state);
    lua_pushlightuserdata(state, impl.get());
    lua_pushcclosure(state, Impl::execute, 1);
    lua_setfield(state, -2, "execute");
    lua_setfield(state, -2, "commands");
    lua_setglobal(state, "app");
}

Script::~Script() = default;

bool Script::load(std::string_view source, std::string_view name, std::string& error)
{
    auto state = impl->state;
    if (!state || impl->builder != LUA_NOREF) {
        error = state ? "script is already loaded" : "cannot create Lua state";
        return false;
    }
    Stack stack(state);
    auto chunk = "@" + std::string(name);
    if (luaL_loadbufferx(state, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK) {
        error = errorMessage(state);
        return false;
    }
    if (!protectedCall(state, 0, 1, error)) return false;
    if (!lua_istable(state, -1)) {
        error = "script must return a module table";
        return false;
    }
    int module = lua_gettop(state);
    field(state, module, "schema");
    if (!lua_isinteger(state, -1) || lua_tointeger(state, -1) != 1) {
        error = "unsupported script schema; expected 1";
        return false;
    }
    lua_pop(state, 1);
    if (!readIcons(state, module, impl->icons, error) || !readImages(state, module, impl->images, error)) return false;
    field(state, module, "commands");
    size_t size = 0;
    if (!lua_isnil(state, -1) && !array(state, -1, 128, "commands", size, error)) return false;
    int entries = lua_gettop(state);
    for (size_t i = 1; i <= size; ++i) {
        lua_rawgeti(state, entries, lua_Integer(i));
        if (!lua_istable(state, -1)) {
            error = "command must be a table";
            return false;
        }
        int entry = lua_gettop(state);
        Command command;
        if (!fields(state, entry, {"id", "title", "category", "shortcut", "run"}, error) || !read(state, entry, "id", command.id, error) || !read(state, entry, "title", command.title, error) || !read(state, entry, "category", command.category, error) || !read(state, entry, "shortcut", command.shortcut, error)) return false;
        field(state, entry, "run");
        if (command.id.empty() || !lua_isfunction(state, -1)) {
            error = (command.id.empty() ? "command" : command.id) + " requires an id and run()";
            return false;
        }
        // A command outliving its script does nothing.
        command.run = [script = std::weak_ptr<Impl>(impl), handler = luaL_ref(state, LUA_REGISTRYINDEX)] {
            if (auto live = script.lock()) live->invoke(handler);
        };
        command.owner = name;
        impl->commands.push_back(std::move(command));
        lua_pop(state, 1);
    }
    field(state, module, "build");
    if (!lua_isfunction(state, -1)) {
        error = "script requires build(state)";
        return false;
    }
    impl->builder = luaL_ref(state, LUA_REGISTRYINDEX);
    return true;
}

bool Script::build(const Values& state, Element& root, std::string& error)
{
    auto lua = impl->state;
    if (impl->builder == LUA_NOREF) {
        error = "script is not loaded";
        return false;
    }
    Stack stack(lua);
    lua_rawgeti(lua, LUA_REGISTRYINDEX, impl->builder);
    Push{lua}.table(state);
    return protectedCall(lua, 1, 1, error) && readElement(lua, -1, root, error);
}

std::vector<Command> Script::commands() const
{
    return impl->commands;
}

const std::vector<IconFile>& Script::icons() const
{
    return impl->icons;
}

const std::vector<ImageFile>& Script::images() const
{
    return impl->images;
}

uint64_t Script::revision() const
{
    return impl->revision;
}

const std::string& Script::error() const
{
    return impl->message;
}

}  // namespace ui::lua
