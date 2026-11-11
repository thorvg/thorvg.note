#include "module.h"
#include <algorithm>
#include <deque>
#include <fstream>

namespace tn
{
struct InputExtensions::Impl
{
    using Module = input::Module;
    using Result = ui::InputResult;
    using Files = std::map<std::filesystem::path, std::pair<std::filesystem::file_time_type, uintmax_t>>;
    ui::CommandRegistry& commands;
    Resolve resolve;
    std::function<uint64_t()> activeEditor;
    input::Services services;
    std::filesystem::path directory;
    Files files;
    std::vector<std::shared_ptr<Module>> modules;
    std::map<uint64_t, input::Snapshot> observed;
    std::deque<input::Event> events;
    std::deque<std::pair<std::weak_ptr<Module>, std::string>> queuedCommands;
    std::string message;
    bool enabled = true, finishing = false;
    struct Prefix
    {
        Module* owner = nullptr;
        uint64_t editor = 0;
        size_t depth = 0;
        std::vector<size_t> candidates;
    } prefix;

    Impl(ui::CommandRegistry& commands, Resolve resolve, std::function<uint64_t()> active, std::function<bool(uint64_t)> focused) :
        commands(commands), resolve(std::move(resolve)), activeEditor(std::move(active))
    {
        services.resolve = this->resolve;
        services.focused = std::move(focused);
        services.changed = [this](Module& origin, uint64_t id, input::Snapshot before) {
            if (auto editor = this->resolve(id)) {
                auto after = Module::snapshot(*editor, services.focused(id));
                origin.consumedInput |= before.revision != after.revision || before.selection.anchor != after.selection.anchor || before.selection.caret != after.selection.caret || before.focused != after.focused;
                record(id, before, after, origin.id);
            } else origin.consumedInput = true;
        };
        services.command = [this](Module& origin, std::string command) {
            queue(origin, std::move(command));
        };
        services.undoGroup = [this](Module& owner, uint64_t id, bool begin) {
            for (const auto& module : modules)
                if (module.get() != &owner && module->groups.count(id)) return false;
            if (auto editor = this->resolve(id)) {
                if (begin) editor->beginUndoGroup();
                else editor->endUndoGroup();
            }
            return true;
        };
    }

    ~Impl() { clear(); }

    void queue(Module& origin, std::string command)
    {
        for (auto& module : modules)
            if (module.get() == &origin) {
                if (queuedCommands.size() < 128) queuedCommands.emplace_back(module, std::move(command));
                else {
                    module->error = "too many queued commands";
                    module->live = false;
                    module->release();
                }
                break;
            }
    }

    void record(uint64_t id, input::Snapshot before, input::Snapshot after, const std::string& origin)
    {
        observed[id] = after;
        if (auto editor = resolve(id); editor && !editor->session().undoGroupOpen())
            for (auto& module : modules)
                module->groups.erase(id);
        if (before.revision != after.revision) events.push_back({id, "change", origin, after});
        if (before.selection.anchor != after.selection.anchor || before.selection.caret != after.selection.caret)
            events.push_back({id, "selection", origin, after});
        if (before.focused != after.focused) {
            events.push_back({id, "focus", origin, after});
            if (!after.focused) {
                if (prefix.editor == id) prefix = {};
                for (auto& module : modules)
                    if (module->groups.erase(id))
                        if (auto editor = resolve(id)) editor->endUndoGroup();
            }
        }
    }

    bool present()
    {
        bool changed = false;
        for (auto& [id, snapshot] : observed) {
            auto editor = resolve(id);
            if (!editor) continue;
            ui::EditorInputState state;
            // Higher priority providers own each presentation property they explicitly set.
            for (auto it = modules.rbegin(); it != modules.rend(); ++it) {
                if (!(*it)->live) continue;
                auto found = (*it)->presentation.find(id);
                if (found == (*it)->presentation.end()) continue;
                auto& value = found->second;
                if (value.text) state.text = *value.text;
                if (value.caret) state.caret = *value.caret;
                if (value.status) state.status = *value.status;
            }
            if (prefix.editor == id && prefix.owner) state.status += (state.status.empty() ? "" : " · ") + std::string("Prefix…");
            auto& old = editor->inputState();
            changed |= old.text != state.text || old.caret != state.caret || old.status != state.status;
            editor->inputState(std::move(state));
        }
        return changed;
    }

    bool finish()
    {
        if (finishing) return false;
        finishing = true;
        bool changed = false;
        size_t count = 0;
        while ((!events.empty() || !queuedCommands.empty()) && count++ < 128) {
            if (!events.empty()) {
                auto event = std::move(events.front());
                events.pop_front();
                for (auto& module : modules)
                    if (module->live && module->id != event.origin) module->event(event);
            } else {
                auto item = std::move(queuedCommands.front());
                queuedCommands.pop_front();
                if (auto owner = item.first.lock(); owner && owner->live) {
                    changed |= commands.execute(item.second);
                }
            }
        }
        if (!events.empty() || !queuedCommands.empty()) {
            message = "Input extensions: event/command recursion limit exceeded";
            for (auto& module : modules) {
                module->live = false;
                module->release();
            }
            events.clear();
            queuedCommands.clear();
            prefix = {};
            changed = true;
        }
        for (auto& module : modules)
            if (!module->live) {
                if (!module->error.empty()) message = module->id + ": " + module->error;
                for (const auto& [id, handler] : module->commands)
                    changed |= commands.remove(id);
                if (prefix.owner == module.get()) prefix = {};
            }
        changed |= present();
        finishing = false;
        return changed;
    }

    void clear()
    {
        prefix = {};
        events.clear();
        queuedCommands.clear();
        for (auto& module : modules) {
            module->release();
            for (const auto& [id, handler] : module->commands)
                commands.remove(id);
        }
        modules.clear();
        present();
    }

    bool scan(Files& next)
    {
        if (directory.empty()) return true;
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec) && !ec) return true;
        std::filesystem::directory_iterator it(directory, ec), end;
        if (ec) {
            message = "Cannot read extensions directory: " + ec.message();
            return false;
        }
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            const auto& entry = *it;
            if (entry.path().extension() != ".lua" || !entry.is_regular_file(ec)) continue;
            auto time = entry.last_write_time(ec);
            if (ec) break;
            auto size = entry.file_size(ec);
            if (ec) break;
            if (next.size() >= 64 || size > 1024 * 1024) {
                message = "Extensions exceed 64 files or 1 MiB per file";
                return false;
            }
            next.emplace(entry.path(), std::make_pair(time, size));
        }
        if (ec) {
            message = "Cannot read extensions directory: " + ec.message();
            return false;
        }
        return true;
    }

    bool reload()
    {
        if (!enabled) return false;
        Files nextFiles;
        if (!scan(nextFiles)) return false;
        files = nextFiles;
        std::vector<std::shared_ptr<Module>> next;
        std::set<std::string> ids, commandIds, previousCommands;
        for (auto& module : modules)
            for (auto& item : module->commands)
                previousCommands.insert(item.first);
        for (auto& command : commands.commands())
            if (!previousCommands.count(command.id)) commandIds.insert(command.id);
        for (auto& [path, stamp] : nextFiles) {
            std::ifstream stream(path, std::ios::binary);
            std::string source(size_t(stamp.second), '\0');
            if (!stream || (!source.empty() && !stream.read(source.data(), std::streamsize(source.size())))) {
                message = "Cannot read input extension: " + utf8(path);
                return false;
            }
            auto candidate = std::make_shared<Module>(services);
            if (!candidate->load(source, utf8(path))) {
                message = utf8(path.filename()) + ": " + candidate->error;
                return false;
            }
            if (!ids.insert(candidate->id).second) {
                message = "Duplicate input extension id: " + candidate->id;
                return false;
            }
            for (auto& [id, handler] : candidate->commands)
                if (!commandIds.insert(id).second) {
                    message = "Duplicate command: " + id;
                    return false;
                }
            next.push_back(std::move(candidate));
        }
        std::stable_sort(next.begin(), next.end(), [](auto& a, auto& b) { return a->priority > b->priority; });
        clear();
        modules = std::move(next);
        message.clear();
        for (auto& module : modules)
            for (auto& [id, handler] : module->commands) {
                auto weak = std::weak_ptr<Module>(module);
                commands.add({id, module->titles[id], "Input extensions", "", [this, weak, handler] {
                if (auto owner = weak.lock(); owner && owner->live) {
                    auto id = activeEditor();
                    if (resolve(id)) owner->run(id, handler);
                    finish();
                } }, [this, weak] { auto owner = weak.lock(); return owner && owner->live && resolve(activeEditor()); }, "No active editor"});
            }
        for (auto& [id, state] : observed) {
            events.push_back({id, "open", "", state});
            if (state.focused) events.push_back({id, "focus", "", state});
        }
        finish();
        return true;
    }

    Result binding(Module& module, uint64_t editor, const ui::KeyStroke& key, std::vector<size_t> candidates, size_t depth)
    {
        std::vector<size_t> pending;
        for (auto i : candidates) {
            auto& binding = module.bindings[i];
            if (!Module::matches(binding.keys[depth], key) || !module.predicate(editor, binding.when) || !module.live) continue;
            if (depth + 1 < binding.keys.size()) {
                pending.push_back(i);
                continue;
            }
            prefix = {};
            if (binding.run != LUA_NOREF) module.run(editor, binding.run);
            else queue(module, binding.command);
            return module.live || module.consumedInput ? Result::Handled : Result::Pass;
        }
        if (!pending.empty()) {
            prefix = {&module, editor, depth + 1, std::move(pending)};
            return Result::Pending;
        }
        return Result::Pass;
    }

    Result dispatch(uint64_t editor, const ui::KeyStroke& key, bool hooks)
    {
        if (!enabled || !resolve(editor)) return Result::Pass;
        if (prefix.owner && (prefix.editor != editor || !prefix.owner->live)) prefix = {};
        if (key.down && prefix.owner) {
            auto previous = std::move(prefix);
            prefix = {};
            if (key.key == "escape") return Result::Handled;
            auto result = binding(*previous.owner, editor, key, std::move(previous.candidates), previous.depth);
            return result == Result::Pass ? Result::Handled : result;
        }
        for (auto& module : modules) {
            if (!module->live) continue;
            if (key.down) {
                std::vector<size_t> candidates(module->bindings.size());
                for (size_t i = 0; i < candidates.size(); ++i)
                    candidates[i] = i;
                auto result = binding(*module, editor, key, std::move(candidates), 0);
                if (result != Result::Pass) return result;
            }
            if (hooks) {
                auto result = module->key(editor, key);
                if (result != Result::Pass) return result;
            }
        }
        return Result::Pass;
    }
};

InputExtensions::InputExtensions(ui::CommandRegistry& commands, Resolve resolve, std::function<uint64_t()> active, std::function<bool(uint64_t)> focused) :
    impl(std::make_unique<Impl>(commands, std::move(resolve), std::move(active), std::move(focused))) {}
InputExtensions::~InputExtensions() = default;
void InputExtensions::setDirectory(std::filesystem::path path)
{
    impl->clear();
    impl->directory = std::move(path);
    impl->files.clear();
    impl->reload();
}
bool InputExtensions::reload()
{
    return impl->reload();
}
bool InputExtensions::enabled() const
{
    return impl->enabled;
}
const std::string& InputExtensions::error() const
{
    return impl->message;
}
void InputExtensions::setEnabled(bool value)
{
    if (impl->enabled == value) return;
    impl->enabled = value;
    if (value) impl->reload();
    else {
        impl->clear();
        impl->message.clear();
    }
}

bool InputExtensions::poll()
{
    if (!impl->enabled) return false;
    Impl::Files next;
    if (!impl->scan(next)) return false;
    if (next == impl->files) return false;
    impl->reload();
    return true;
}

ui::InputResult InputExtensions::key(uint64_t id, const ui::KeyStroke& key)
{
    auto result = impl->dispatch(id, key, true);
    impl->finish();
    return result;
}

ui::InputResult InputExtensions::text(uint64_t id, std::string_view text, bool shift)
{
    auto editor = impl->resolve(id);
    if (!impl->enabled || !editor) return ui::InputResult::Pass;
    if (!editor->inputState().text) return ui::InputResult::Handled;
    auto result = ui::InputResult::Pass;
    if (text.size() == 1 && uint8_t(text[0]) >= 32 && uint8_t(text[0]) < 127) {
        ui::KeyStroke key;
        key.shift = shift;
        key.key = text;
        if (text[0] >= 'A' && text[0] <= 'Z') key.key[0] += 'a' - 'A';
        result = impl->dispatch(id, key, false);
    } else if (impl->prefix.owner) {
        impl->prefix = {};
        result = ui::InputResult::Handled;
    }
    if (result == ui::InputResult::Pass)
        for (auto& module : impl->modules) {
            result = module->text(id, text);
            if (result != ui::InputResult::Pass) break;
        }
    impl->finish();
    return result;
}

bool InputExtensions::observe(const std::vector<uint64_t>& editors)
{
    if (!impl->enabled) return false;
    bool changed = false;
    std::set<uint64_t> alive(editors.begin(), editors.end());
    for (auto it = impl->observed.begin(); it != impl->observed.end();) {
        if (alive.count(it->first)) {
            ++it;
            continue;
        }
        auto id = it->first;
        impl->events.push_back({id, "close", "", it->second});
        for (auto& module : impl->modules)
            module->forget(id);
        if (impl->prefix.editor == id) impl->prefix = {};
        it = impl->observed.erase(it);
        changed = true;
    }
    for (auto id : editors)
        if (auto editor = impl->resolve(id)) {
            auto current = input::Module::snapshot(*editor, impl->services.focused(id));
            auto found = impl->observed.find(id);
            if (found == impl->observed.end()) {
                impl->observed[id] = current;
                impl->events.push_back({id, "open", "", current});
                changed = true;
                if (current.focused) impl->events.push_back({id, "focus", "", current});
            } else {
                auto before = impl->events.size();
                impl->record(id, found->second, current, "");
                changed |= before != impl->events.size();
            }
        }
    return impl->finish() || changed;
}

std::filesystem::path InputExtensions::edit(std::string& error)
{
    error.clear();
    if (impl->directory.empty()) {
        error = "Input extensions directory is unavailable";
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(impl->directory, ec);
    if (ec) {
        error = ec.message();
        return {};
    }
    auto path = impl->directory / "bindings.lua";
    bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        error = ec.message();
        return {};
    }
    if (!exists) {
        std::ofstream stream(path, std::ios::binary);
        stream << "-- Editor API: api/lua/editor.lua\nreturn {\n  schema = 1,\n  id = \"user\",\n  bindings = {\n    -- { keys = \"ctrl+a\", run = function(editor) editor:move(\"line_start\") end },\n  },\n}\n";
        stream.close();
        if (!stream) {
            error = "Cannot create input extension";
            return {};
        }
    }
    return path;
}
}  // namespace tn
