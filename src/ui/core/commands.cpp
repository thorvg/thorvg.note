#include <ui/ui.h>
#include <algorithm>
#include <cctype>

namespace ui
{

namespace
{
std::string folded(std::string_view value)
{
    std::string result(value);
    for (auto& ch : result)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return result;
}
}  // namespace

struct CommandRegistry::Impl
{
    std::vector<Command> entries;
};

CommandRegistry::CommandRegistry() :
    impl(std::make_unique<Impl>()) {}
CommandRegistry::~CommandRegistry() = default;

bool CommandRegistry::add(Command command)
{
    if (command.id.empty() || find(command.id)) return false;
    impl->entries.push_back(std::move(command));
    return true;
}

bool CommandRegistry::remove(std::string_view id)
{
    auto& entries = impl->entries;
    auto found = std::find_if(entries.begin(), entries.end(), [&](const Command& command) { return command.id == id; });
    if (found == entries.end()) return false;
    entries.erase(found);
    return true;
}

size_t CommandRegistry::release(std::string_view owner)
{
    if (owner.empty()) return 0;
    auto& entries = impl->entries;
    auto kept = std::remove_if(entries.begin(), entries.end(), [&](const Command& command) { return command.owner == owner; });
    auto count = size_t(entries.end() - kept);
    entries.erase(kept, entries.end());
    return count;
}

bool CommandRegistry::execute(std::string_view id) const
{
    auto command = find(id);
    if (!command || !command->enabled()) return false;
    // The callback may change the registry.
    auto run = command->run;
    run();
    return true;
}

const Command* CommandRegistry::find(std::string_view id) const
{
    for (const auto& command : impl->entries) {
        if (command.id == id) return &command;
    }
    return nullptr;
}

const std::vector<Command>& CommandRegistry::commands() const
{
    return impl->entries;
}

std::vector<size_t> CommandRegistry::matching(std::string_view query) const
{
    auto needle = folded(query);
    std::vector<size_t> matches;
    for (size_t i = 0; i < impl->entries.size(); ++i) {
        const auto& command = impl->entries[i];
        auto haystack = folded(command.title + " " + command.category + " " + command.id);
        if (haystack.find(needle) != std::string::npos) matches.push_back(i);
    }
    return matches;
}

}  // namespace ui
