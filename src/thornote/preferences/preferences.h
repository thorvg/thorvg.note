#pragma once

#include "font_catalog.h"
#include "layout.h"
#include <thornote/documents/autosave.h>
#include <thornote/documents/recent.h>
#include <array>
#include <functional>
#include <string_view>

namespace tn
{
inline constexpr const char* preferencesOrganization = "ThorVG";
inline constexpr const char* preferencesAppId = "ThorNote";

struct Preferences
{
    FontChoice font;
    float fontSize = 16;
    bool dark = false;
    int tabStyle = 1;
    std::array<LayoutState, layoutCount> layouts = defaultLayouts;
    Layout composition = Layout::Memo;
    RecentItems recent;
    float lineNumberScale = 1, lineNumberOffset = 0;
};
std::string encodePreferences(const Preferences&);
bool decodePreferences(std::string_view text, Preferences&);

// Keeps the settings file in step with the preferences, saving them shortly after they change.
class PreferenceFile
{
public:
    // Makes a finished save durable where the platform needs that, then reports the outcome.
    using Persist = std::function<void(std::function<void(std::string)>)>;

    // Reads the file into the preferences and returns whether it had any; a file that cannot be read or decoded stops saving.
    bool load(const storage::Path& path, Preferences&);
    // Takes the preferences as saved, such as after applying the loaded ones.
    void track(const Preferences& value) { autosave.saved = autosave.observed = encodePreferences(value); }
    // Saves the preferences once they changed and a save is due, or at once when forced; returns whether a save ran.
    bool flush(const Preferences&, uint64_t now, bool force, const Persist&);
    uint64_t deadline() const { return autosave.deadline(); }
    const std::string& error() const { return message; }

private:
    storage::Path path;
    storage::DiskStamp stamp;
    Autosave autosave{500, true, true};
    std::string message;
    bool syncing = false;
};
}  // namespace tn
