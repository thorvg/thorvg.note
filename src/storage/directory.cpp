#include <chrono>
#include "internal.h"

namespace storage::detail
{
namespace
{
constexpr size_t cacheLimit = 1 << 16;

// A false value without an error marks an unsupported or unreadable file.
Result<bool> supportedText(const Path& path, const Control* control, TextFileCache* cache)
{
    auto target = resolve(path, false);
    if (!target) return {false, {}};
    auto input = openFile(target.value);
    if (!input) return {false, {}};
    auto info = fileInfo(input.value);
    if (!info) return {false, {}};
    if (cache) {
        auto found = cache->find(target.value);
        if (found != cache->end() && found->second.info == info.value) return {found->second.supported, {}};
    }
    Decoder decoder({}, true);
    bool rejected = false;
    auto error = readAll(input.value, info.value, {}, control, [&](std::string_view bytes) {
        auto error = decoder.append(bytes);
        rejected = bool(error);
        return error;
    });
    if (error.code == ErrorCode::Cancelled) return {false, error};
    // Unreadable or changing files are examined again by the next scan.
    if (error && !rejected) return {false, {}};
    bool supported = !error && decoder.finish();
    if (cache) {
        if (cache->size() >= cacheLimit) cache->clear();
        (*cache)[target.value] = {info.value, supported};
    }
    return {supported, {}};
}
}  // namespace

Result<std::vector<DirectoryEntry>> list(const Path& directory, ListOptions options, const Control* control, TextFileCache* cache)
{
    using Entries = Result<std::vector<DirectoryEntry>>;
    return guarded<Entries>([&]() -> Entries {
        Entries result;
        if (auto error = cancelled(control)) return {{}, error};
        if (directory.empty()) return {{}, failure(ErrorCode::InvalidPath, "empty directory path")};
        std::error_code error;
        auto path = std::filesystem::absolute(directory, error).lexically_normal();
        if (error) return {{}, failure(ErrorCode::InvalidPath, "cannot resolve directory", error)};
        // Report inaccessible roots while allowing unreadable descendants to be skipped.
        {
            std::filesystem::directory_iterator root(path, error);
        }
        std::filesystem::recursive_directory_iterator iterator, end;
        if (!error) iterator = std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, error);
        if (error)
            return {{}, failure(error == std::errc::no_such_file_or_directory ? ErrorCode::NotFound : ErrorCode::Io, "cannot list directory: " + utf8(path), error)};
        for (; iterator != end; iterator.increment(error)) {
            if (error || (result.error = cancelled(control))) break;
            std::error_code statusError;
            if (!iterator->is_regular_file(statusError)) continue;
            auto file = iterator->path().lexically_normal();
            if (options.textOnly) {
                auto supported = supportedText(file, control, cache);
                if (!supported) {
                    result.error = supported.error;
                    break;
                }
                if (!supported.value) continue;
            }
            auto modified = iterator->last_write_time(statusError);
            std::optional<int64_t> modifiedAt;
            if (!statusError) modifiedAt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::file_clock::to_sys(modified).time_since_epoch()).count();
            result.value.push_back({file, creationTime(file), modifiedAt});
        }
        if (error) result.error = failure(ErrorCode::Io, "cannot finish listing directory: " + utf8(path), error);
        return result;
    });
}
}  // namespace storage::detail
