#pragma once

#include "autosave.h"
#include <storage/storage.h>
#include <task/task.h>
#include <thornote/path.h>
#include <thornote/preferences/layout.h>
#include <array>
#include <functional>
#include <map>
#include <vector>

namespace tn
{
struct SessionDocument
{
    uint64_t id = 0;
    storage::RecoverySnapshot content;
    std::array<uint8_t, 32> diskDigest{};
    bool hadFile = false, dirty = false, uncertain = false, favorite = false;
    // Layouts holding the document, one bit each; none in sessions that predate layouts.
    uint8_t layouts = 0;
    size_t anchor = 0, caret = 0;
    float scrollX = 0;
    double scrollY = 0;
};

struct SessionState
{
    std::vector<SessionDocument> documents;
    // The document each layout selected; older sessions selected one for every layout.
    std::array<uint64_t, layoutCount> selections{};
    float width = 1080, height = 720;
    bool settingsOpen = false, settingsActive = false, commands = false;
    bool folderWorkspace = false;
    int settingsCategory = 0;
    std::string collection = "all", noteQuery;
};

// App-owned session metadata and immutable recovery files. No UI or scripting dependency.
class SessionStore
{
public:
    // Makes a finished write durable where the platform needs that, then reports the outcome.
    using Persist = std::function<void(std::function<void(std::string)>)>;

    explicit SessionStore(std::function<void()> wake = {});
    // Checkpoints follow once the folder proves writable.
    SessionState load(const storage::Path& directory);
    // Writes the captured session once it changed and a checkpoint is due, or at once and to the end when forced.
    // Failures replace the error.
    bool checkpoint(const std::function<SessionState(bool contents)>& capture, uint64_t now, bool force, std::string& error);
    // Applies a finished write; returns whether one finished. The error must outlive the persisting.
    bool poll(const Persist&, std::string& error);
    void disable() { autosave.enabled = false; }
    bool enabled() const { return autosave.enabled; }
    bool busy() const { return writing; }
    uint64_t deadline() const { return autosave.deadline(); }
    const std::string& error() const { return message; }

private:
    bool submit(SessionState);
    bool finish();
    std::string write(SessionState);
    storage::Path directory;
    storage::DiskStamp stamp;
    storage::RecoveryStore buffers{storage::Path{}};
    // Metadata of each document's committed buffer.
    std::map<uint64_t, SessionDocument> blobs;
    std::string message;
    // Checkpoints recur while typing instead of waiting for idle input.
    Autosave autosave{750, false, false};
    std::string submitted;
    bool writable = false, lastSucceeded = true, writing = false, syncing = false;
#ifdef __EMSCRIPTEN__
    std::shared_ptr<bool> browserLifetime = std::make_shared<bool>(true);
    bool browserCompleted = false, browserPending = false;
#endif
    // Join the worker before destroying the state it writes.
    task::TaskQueue queue;
};

std::string encodeSession(const SessionState&);
bool decodeSession(std::string_view, SessionState&);
}  // namespace tn
