#include <emscripten.h>
#include <cstdlib>
#include <memory>
#include "browser_files.h"
#include <thornote/path.h>

extern "C" {
bool thornoteRecoveryStart(void*, const char*);
char* thornoteRecoveryAdd(void*, const char*, const char*, const char*, unsigned, bool, const char*, size_t);
void thornoteRecoverySubmit(void*, const char*);

EMSCRIPTEN_KEEPALIVE bool thornoteRecoveryPrepared(void* callback)
{
    return static_cast<tn::BrowserRecoveryCallbacks*>(callback)->prepare();
}

EMSCRIPTEN_KEEPALIVE bool thornoteRecoveryCommitted(void* callback)
{
    return static_cast<tn::BrowserRecoveryCallbacks*>(callback)->commit();
}

EMSCRIPTEN_KEEPALIVE void thornoteRecoveryDone(void* callback, const char* error)
{
    std::unique_ptr<tn::BrowserRecoveryCallbacks> complete(static_cast<tn::BrowserRecoveryCallbacks*>(callback));
    complete->complete(error ? error : "");
}
}

extern "C" EMSCRIPTEN_KEEPALIVE void thornoteStorageSynced(void* callback, const char* error)
{
    std::unique_ptr<std::function<void(std::string)>> complete(static_cast<std::function<void(std::string)>*>(callback));
    (*complete)(error ? error : "");
}

extern "C" EMSCRIPTEN_KEEPALIVE void thornoteFilesOpened(void* callback, const char* const* paths, const char* error)
{
    std::unique_ptr<std::function<void(const char* const*, const char*)>> complete(static_cast<std::function<void(const char* const*, const char*)>*>(callback));
    (*complete)(paths, error);
}

namespace tn
{
void* browserRecoveryStart(const storage::Path& directory, BrowserRecoveryCallbacks callbacks)
{
    auto pending = std::make_unique<BrowserRecoveryCallbacks>(std::move(callbacks));
    if (!thornoteRecoveryStart(pending.get(), utf8(directory).c_str())) return nullptr;
    return pending.release();
}

std::string browserRecoveryAdd(void* pending, const storage::RecoverySnapshot& snapshot)
{
    auto id = thornoteRecoveryAdd(pending, std::to_string(snapshot.revision).c_str(), snapshot.title.c_str(), utf8(snapshot.originalPath).c_str(), unsigned(snapshot.format.ending), snapshot.format.bom, snapshot.text.data(), snapshot.text.size());
    std::string result = id ? id : "";
    std::free(id);
    return result;
}

void browserRecoverySubmit(void* pending, const std::string& obsolete)
{
    thornoteRecoverySubmit(pending, obsolete.c_str());
}

void browserOpenFiles(std::function<void(const char* const*, const char*)> complete)
{
    auto callback = new std::function<void(const char* const*, const char*)>(std::move(complete));
    EM_ASM({
        var finish = function(paths, error) {
            var names = paths.map(function(path) { return stringToNewUTF8(path); });
            var list = _malloc((names.length + 1) * 4);
            names.forEach(function(name, index) { HEAPU32[(list >> 2) + index] = name; });
            HEAPU32[(list >> 2) + names.length] = 0;
            var message = error ? stringToNewUTF8(String(error)) : 0;
            try { _thornoteFilesOpened($0, list, message); }
            finally {
                names.forEach(function(name) { _free(name); });
                _free(list);
                if (message) _free(message);
            }
        };
        if (Module['onOpenFiles']) Module['onOpenFiles'](finish);
        else finish([], 'Browser file import is unavailable'); }, callback);
}

void browserSync(std::function<void(std::string)> complete)
{
    auto callback = new std::function<void(std::string)>(std::move(complete));
    EM_ASM({
        var finish = function(error) {
            var message = error ? stringToNewUTF8(String(error)) : 0;
            _thornoteStorageSynced($0, message);
            if (message) _free(message);
        };
        if (Module['onSyncStorage']) Module['onSyncStorage'](finish);
        else finish('Browser storage is unavailable'); }, callback);
}

std::string browserSavePath(std::string_view suggested)
{
    auto path = static_cast<char*>(EM_ASM_PTR({
        var path = Module['onSaveAs'] ? Module['onSaveAs'](UTF8ToString($0, $1)) : null;
        return path ? stringToNewUTF8(path) : 0; }, suggested.data(), suggested.size()));
    std::string result = path ? path : "";
    std::free(path);
    return result;
}

void browserExport(std::string_view name, std::string_view text, storage::FileFormat format)
{
    std::string bytes;
    if (format.bom) bytes = "\xef\xbb\xbf";
    for (auto ch : text) {
        if (ch == '\n' && format.ending == storage::LineEnding::Crlf) bytes += '\r';
        bytes += ch == '\n' && format.ending == storage::LineEnding::Cr ? '\r' : ch;
    }
    EM_ASM({
        if (Module['onExport']) Module['onExport'](UTF8ToString($0, $1), HEAPU8.slice($2, $2 + $3)); }, name.data(), name.size(), bytes.data(), bytes.size());
}
}  // namespace tn
