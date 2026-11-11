#include "module.h"
#include <stdexcept>

namespace tn::input
{
namespace
{
constexpr const char* editorType = "ThorNote.Editor.v1";
enum Operation
{
    Id,
    Revision,
    Length,
    Read,
    Line,
    LineCount,
    LineStart,
    LineEnd,
    LineAt,
    Previous,
    Next,
    WordPrevious,
    WordNext,
    Selection,
    Select,
    Edit,
    Insert,
    Move,
    Undo,
    Redo,
    BeginGroup,
    EndGroup,
    Cursor,
    TextInput,
    Status,
    Clipboard,
    Command,
    ReadOnly,
    Composing,
    Focused
};

std::string_view string(lua_State* state, int index)
{
    if (lua_type(state, index) != LUA_TSTRING) throw std::runtime_error("expected a string");
    size_t size;
    auto value = lua_tolstring(state, index, &size);
    return {value, size};
}

size_t integer(lua_State* state, int index)
{
    if (!lua_isinteger(state, index) || lua_tointeger(state, index) < 0) throw std::runtime_error("expected a nonnegative integer");
    return size_t(lua_tointeger(state, index));
}

bool boolean(lua_State* state, int index)
{
    if (!lua_isboolean(state, index)) throw std::runtime_error("expected a boolean");
    return lua_toboolean(state, index);
}

using lua::field;

size_t offset(lua_State* state, int index, const tn::Document& document)
{
    auto value = integer(state, index);
    if (value > document.text().size() || document.clamp(value) != value) throw std::runtime_error("offset is not a UTF-8 byte boundary");
    return value;
}

size_t row(lua_State* state, int index, const tn::Document& document)
{
    auto value = integer(state, index);
    if (value >= document.lineCount()) throw std::runtime_error("line is out of range");
    return value;
}

void selection(lua_State* state, tn::Selection value)
{
    lua_newtable(state);
    lua_pushinteger(state, lua_Integer(value.anchor));
    lua_setfield(state, -2, "anchor");
    lua_pushinteger(state, lua_Integer(value.caret));
    lua_setfield(state, -2, "caret");
}

void readBatch(lua_State* state, tn::EditBatch& batch)
{
    auto count = arraySize(state, 2, 1024, "edits");
    size_t total = 0;
    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(state, 2, lua_Integer(i));
        if (!lua_istable(state, -1)) throw std::runtime_error("edit range must be a table");
        tn::TextEdit edit;
        field(state, -1, "start");
        edit.start = integer(state, -1);
        lua_pop(state, 1);
        field(state, -1, "finish");
        edit.end = integer(state, -1);
        lua_pop(state, 1);
        field(state, -1, "text");
        edit.text = string(state, -1);
        lua_pop(state, 1);
        total += edit.text.size();
        if (total > 8 * 1024 * 1024) throw std::runtime_error("edit exceeds 8 MiB");
        batch.edits.push_back(std::move(edit));
        lua_pop(state, 1);
    }
    if (lua_isnoneornil(state, 3)) return;
    if (!lua_istable(state, 3)) throw std::runtime_error("edit options must be a table");
    field(state, 3, "revision");
    if (!lua_isnil(state, -1)) batch.revision = integer(state, -1);
    lua_pop(state, 1);
    field(state, 3, "selection");
    if (!lua_isnil(state, -1)) {
        if (!lua_istable(state, -1)) throw std::runtime_error("selection must be a table");
        tn::Selection value;
        field(state, -1, "anchor");
        value.anchor = integer(state, -1);
        lua_pop(state, 1);
        field(state, -1, "caret");
        value.caret = integer(state, -1);
        lua_pop(state, 1);
        batch.selection = value;
    }
    lua_pop(state, 1);
}
}  // namespace

void Module::openEditorApi()
{
    luaL_newmetatable(state, editorType);
    lua_pushvalue(state, -1);
    lua_setfield(state, -2, "__index");
    lua_pushliteral(state, "Editor API v1");
    lua_setfield(state, -2, "__metatable");
    for (auto entry : {std::pair{"id", Id}, {"revision", Revision}, {"length", Length}, {"read", Read}, {"line", Line}, {"line_count", LineCount}, {"line_start", LineStart}, {"line_end", LineEnd}, {"line_at", LineAt}, {"previous", Previous}, {"next", Next}, {"word_previous", WordPrevious}, {"word_next", WordNext}, {"selection", Selection}, {"select", Select}, {"edit", Edit}, {"insert", Insert}, {"move", Move}, {"undo", Undo}, {"redo", Redo}, {"begin_undo_group", BeginGroup}, {"end_undo_group", EndGroup}, {"cursor", Cursor}, {"text_input", TextInput}, {"status", Status}, {"clipboard", Clipboard}, {"command", Command}, {"read_only", ReadOnly}, {"composing", Composing}, {"focused", Focused}}) {
        lua_pushinteger(state, entry.second);
        lua_pushcclosure(state, api, 1);
        lua_setfield(state, -2, entry.first);
    }
    lua_pop(state, 1);
}

void Module::pushEditor(uint64_t id)
{
    auto ref = static_cast<EditorRef*>(lua_newuserdatauv(state, sizeof(EditorRef), 0));
    *ref = {this, id};
    luaL_setmetatable(state, editorType);
}

int Module::api(lua_State* state)
{
    Module* owner = nullptr;
    uint64_t editorId = 0;
    std::optional<Snapshot> before;
    try {
        auto ref = static_cast<EditorRef*>(luaL_testudata(state, 1, editorType));
        if (!ref) throw std::runtime_error("use editor:method(...)");
        auto& module = *ref->module;
        if (!module.executing || !module.live) throw std::runtime_error("extension is inactive");
        auto operation = Operation(lua_tointeger(state, lua_upvalueindex(1)));
        if (operation == Id) {
            lua_pushinteger(state, lua_Integer(ref->id));
            return 1;
        }
        auto editor = module.services.resolve(ref->id);
        if (!editor) throw std::runtime_error("editor is closed");
        auto& document = editor->session().document();
        auto pushString = [&](std::string_view value) {
            lua_pushlstring(state, value.data(), value.size());
            return 1;
        };
        auto pushInteger = [&](size_t value) {
            lua_pushinteger(state, lua_Integer(value));
            return 1;
        };
        auto pushBoolean = [&](bool value) {
            lua_pushboolean(state, value);
            return 1;
        };
        switch (operation) {
            case Revision: return pushInteger(document.revision());
            case Length: return pushInteger(document.text().size());
            case LineCount: return pushInteger(document.lineCount());
            case Read: {
                auto begin = lua_isnoneornil(state, 2) ? 0 : offset(state, 2, document);
                auto end = lua_isnoneornil(state, 3) ? document.text().size() : offset(state, 3, document);
                if (end < begin) throw std::runtime_error("finish precedes start");
                return pushString(std::string_view(document.text()).substr(begin, end - begin));
            }
            case Line: {
                auto value = row(state, 2, document);
                return pushString(std::string_view(document.text()).substr(document.lineStart(value), document.lineEnd(value) - document.lineStart(value)));
            }
            case LineStart: return pushInteger(document.lineStart(row(state, 2, document)));
            case LineEnd: return pushInteger(document.lineEnd(row(state, 2, document)));
            case LineAt: return pushInteger(document.lineAt(offset(state, 2, document)));
            case Previous: return pushInteger(document.previous(offset(state, 2, document)));
            case Next: return pushInteger(document.next(offset(state, 2, document)));
            case WordPrevious: return pushInteger(document.wordPrevious(offset(state, 2, document)));
            case WordNext: return pushInteger(document.wordNext(offset(state, 2, document)));
            case Selection: selection(state, editor->session().selection()); return 1;
            case ReadOnly: return pushBoolean(editor->options().readOnly);
            case Composing: return pushBoolean(editor->composing());
            case Focused: return pushBoolean(module.services.focused(ref->id));
            case Cursor:
                if (lua_isnoneornil(state, 2)) return pushString(editor->inputState().caret == ui::CaretStyle::Bar ? "bar" : editor->inputState().caret == ui::CaretStyle::Block ? "block"
                                                                                                                                                                                 : "underline");
                break;
            case TextInput:
                if (lua_isnoneornil(state, 2)) return pushBoolean(editor->inputState().text);
                break;
            case Status:
                if (lua_isnoneornil(state, 2)) return pushString(editor->inputState().status);
                break;
            case Clipboard:
                if (lua_isnoneornil(state, 2)) {
                    if (!editor->readClipboard) throw std::runtime_error("clipboard is unavailable");
                    return pushString(editor->readClipboard());
                }
                break;
            default: break;
        }
        if (!module.writable) throw std::runtime_error("writes are unavailable in predicates");
        before = snapshot(*editor, module.services.focused(ref->id));
        owner = &module;
        editorId = ref->id;
        bool success = true;
        switch (operation) {
            case Select: {
                if (editor->composing()) throw std::runtime_error("IME composition is active");
                auto anchor = offset(state, 2, document), caret = offset(state, 3, document);
                editor->select(anchor, caret);
                break;
            }
            case Edit: {
                tn::EditBatch batch;
                batch.revision = document.revision();
                readBatch(state, batch);
                std::string error;
                if (!editor->applyEdits(batch, error)) throw std::runtime_error(error);
                break;
            }
            case Insert: {
                auto text = string(state, 2);
                if (text.size() > 8 * 1024 * 1024) throw std::runtime_error("insert exceeds 8 MiB");
                std::string error;
                if (!editor->insert(text, error)) throw std::runtime_error(error);
                break;
            }
            case Move: {
                auto motion = string(state, 2);
                auto count = lua_isnoneornil(state, 3) ? 1 : integer(state, 3);
                auto extend = lua_isnoneornil(state, 4) ? false : boolean(state, 4);
                if (!editor->navigate(motion, count, extend)) throw std::runtime_error("invalid motion/count or IME composition is active");
                break;
            }
            case Undo: success = editor->undo(); break;
            case Redo: success = editor->redo(); break;
            case BeginGroup:
            case EndGroup:
                if (operation == BeginGroup && (editor->options().readOnly || editor->composing())) throw std::runtime_error("undo group unavailable while read-only or composing");
                if (!module.services.undoGroup(module, ref->id, operation == BeginGroup)) throw std::runtime_error("undo group belongs to another extension");
                if (operation == BeginGroup) module.groups.insert(ref->id);
                else module.groups.erase(ref->id);
                break;
            case Cursor: {
                auto value = string(state, 2);
                ui::CaretStyle style;
                if (value == "bar") style = ui::CaretStyle::Bar;
                else if (value == "block") style = ui::CaretStyle::Block;
                else if (value == "underline") style = ui::CaretStyle::Underline;
                else throw std::runtime_error("cursor expects bar, block or underline");
                module.presentation[ref->id].caret = style;
                break;
            }
            case TextInput:
                if (editor->composing()) throw std::runtime_error("IME composition is active");
                module.presentation[ref->id].text = boolean(state, 2);
                break;
            case Status:
                if (string(state, 2).size() > 128) throw std::runtime_error("status exceeds 128 bytes");
                module.presentation[ref->id].status = string(state, 2);
                break;
            case Clipboard: {
                if (!editor->writeClipboard) throw std::runtime_error("clipboard is unavailable");
                auto text = string(state, 2);
                if (text.find('\0') != std::string_view::npos) throw std::runtime_error("clipboard text contains NUL");
                module.consumedInput = true;
                editor->writeClipboard(text);
                break;
            }
            case Command: module.services.command(module, std::string(string(state, 2))); break;
            default: throw std::runtime_error("unknown editor operation");
        }
        module.services.changed(module, ref->id, *before);
        before.reset();
        return pushBoolean(success);
    } catch (const std::exception& error) {
        if (owner && before) owner->services.changed(*owner, editorId, *before);
        lua_pushnil(state);
        lua_pushstring(state, error.what());
        return 2;
    }
}
}  // namespace tn::input
