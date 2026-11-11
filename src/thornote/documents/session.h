#pragma once

#include <storage/storage.h>
#include <task/task.h>
#include <thornote/path.h>
#include <array>
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
    size_t anchor = 0, caret = 0;
    float scrollX = 0, scrollY = 0;
};

struct SessionState
{
    std::vector<SessionDocument> documents;
    uint64_t active = 0;
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
    explicit SessionStore(std::function<void()> wake = {});
    SessionState load(const storage::Path& directory);
    bool submit(SessionState);
    bool poll();
    bool finish();
    bool busy() const { return writing; }
    bool available() const { return writable; }
    const std::string& error() const { return message; }

private:
    std::string write(SessionState);
    storage::Path directory;
    storage::DiskStamp stamp;
    storage::RecoveryStore buffers{storage::Path{}};
    // Metadata of each document's committed buffer.
    std::map<uint64_t, SessionDocument> blobs;
    std::string message;
    bool writable = false, lastSucceeded = true, writing = false;
    // Join the worker before destroying the state it writes.
    task::TaskQueue queue;
};

std::string encodeSession(const SessionState&);
bool decodeSession(std::string_view, SessionState&);
}  // namespace tn
