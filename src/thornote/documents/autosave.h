#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace tn
{
// Saves encoded contents some time after they change. A restarting delay waits for idle input;
// otherwise saves recur while the contents keep changing.
struct Autosave
{
    uint64_t delay;
    bool restart, enabled;
    std::string observed{}, saved{};
    uint64_t at = 0;

    // Records `contents` at `now`; true when they differ from the saved contents and a save is due.
    bool due(const std::string& contents, uint64_t now, bool force)
    {
        if (contents != observed) {
            observed = contents;
            if (restart || !at) at = now + delay;
        }
        return enabled && contents != saved && (force || (at && now >= at));
    }
    uint64_t deadline() const { return enabled && at && observed != saved ? at : std::numeric_limits<uint64_t>::max(); }
};
}  // namespace tn
