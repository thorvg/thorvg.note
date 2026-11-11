#pragma once

#include "host.h"
#include <thornote/scripting/lua_state.h>
#include <lua.hpp>
#include <map>
#include <optional>
#include <set>

namespace tn::input
{
// Size of a dense array; throws std::runtime_error naming `name` otherwise.
size_t arraySize(lua_State*, int index, size_t maximum, const char* name);

struct Binding
{
    std::vector<ui::KeyStroke> keys;
    std::string command;
    int run = LUA_NOREF, when = LUA_NOREF;
};

struct Presentation
{
    std::optional<bool> text;
    std::optional<ui::CaretStyle> caret;
    std::optional<std::string> status;
};

struct Snapshot
{
    uint64_t revision = 0;
    tn::Selection selection;
    bool focused = false;
};

struct Event
{
    uint64_t editor;
    std::string type, origin;
    Snapshot state;
};

class Module;
struct Services
{
    InputExtensions::Resolve resolve;
    std::function<bool(uint64_t)> focused;
    std::function<void(Module&, uint64_t, Snapshot)> changed;
    std::function<void(Module&, std::string)> command;
    std::function<bool(Module&, uint64_t, bool)> undoGroup;
};

class Module
{
public:
    explicit Module(Services&);
    ~Module();
    bool load(std::string_view source, std::string_view filename);
    ui::InputResult key(uint64_t, const ui::KeyStroke&);
    ui::InputResult text(uint64_t, std::string_view);
    void event(const Event&);
    bool predicate(uint64_t, int handler);
    void run(uint64_t, int handler);
    void release();
    void forget(uint64_t);
    static Snapshot snapshot(const ui::TextEditor&, bool focused);
    static bool matches(const ui::KeyStroke& pattern, const ui::KeyStroke& key);

    std::string id, error;
    int priority = 0;
    bool live = true;
    bool consumedInput = false;
    std::map<std::string, int, std::less<>> commands;
    std::map<std::string, std::string, std::less<>> titles;
    std::vector<Binding> bindings;
    std::map<uint64_t, Presentation> presentation;
    std::set<uint64_t> groups;

private:
    struct EditorRef
    {
        Module* module;
        uint64_t id;
    };
    void openEditorApi();
    void pushEditor(uint64_t);
    bool call(int args, int results, bool writable = true);
    ui::InputResult result();
    int referenceField(const char* name);
    static int api(lua_State*);
    Services& services;
    lua_State* state = nullptr;
    int onKey = LUA_NOREF, onText = LUA_NOREF, onEvent = LUA_NOREF;
    bool executing = false, writable = false;
};
}  // namespace tn::input
