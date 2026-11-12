#include <emscripten.h>
#include <cstdlib>
#include <memory>
#include "browser_files.h"

extern "C" EMSCRIPTEN_KEEPALIVE void thornoteStorageSynced(void* callback, const char* error)
{
    std::unique_ptr<std::function<void(std::string)>> complete(static_cast<std::function<void(std::string)>*>(callback));
    (*complete)(error ? error : "");
}

namespace tn
{
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
