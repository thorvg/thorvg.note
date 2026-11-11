#pragma once

#include <exception>
#include <limits>
#include <new>
#include <unordered_map>
#include <utility>
#include <storage/storage.h>
#include <storage/platform/native.h>
#include <task/task.h>

namespace storage::detail
{
using Control = task::TaskContext;
using Digest = std::array<uint8_t, 32>;
using Sink = std::function<Error(std::string_view)>;

inline Error failure(ErrorCode code, std::string message, std::error_code system = {})
{
    return {code, system, std::move(message)};
}

inline Error cancelled(const Control* control)
{
    return control && control->cancelled() ? failure(ErrorCode::Cancelled, "cancelled") : Error{};
}

// UTF-8 text of a path; the standard keeps it in char8_t, which this module does not use.
inline std::string utf8(const Path& path)
{
    auto text = path.u8string();
    return {text.begin(), text.end()};
}

inline Path utf8Path(std::string_view text)
{
    return std::u8string(text.begin(), text.end());
}

inline Error& errorOf(Error& error)
{
    return error;
}

template<typename ResultType>
Error& errorOf(ResultType& result)
{
    return result.error;
}

// Reports allocation and library exceptions through the operation's result.
template<typename ResultType, typename Function>
ResultType guarded(Function function)
{
    ResultType result;
    try {
        return function();
    } catch (const std::bad_alloc&) {
        errorOf(result) = failure(ErrorCode::TooLarge, "out of memory");
    } catch (const std::exception& exception) {
        errorOf(result) = failure(ErrorCode::Io, exception.what());
    }
    return result;
}

// Save boundaries at which tests inject failures or external changes.
enum class Stage
{
    Create,
    Write,
    Metadata,
    FileSync,
    Close,
    Verify,
    Commit,
    DirectorySync
};

struct Hooks
{
    std::function<std::error_code(Stage)> before;
    size_t maxWrite = std::numeric_limits<size_t>::max();
};

// Null outside tests; consulted by saves running on the installing thread.
extern thread_local const Hooks* hooks;

class Sha256
{
public:
    void append(std::string_view bytes);
    Digest finish();

private:
    void block();
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buffer[64]{};
    size_t used = 0;
    uint64_t length = 0;
};

class Decoder
{
public:
    explicit Decoder(std::optional<size_t> limit = {}, bool validationOnly = false) :
        limit(limit), validationOnly(validationOnly) {}
    Error append(std::string_view bytes);
    Result<Text> finish();

private:
    Error consume(std::string_view bytes);
    Error character(std::string_view bytes);
    Error output(std::string_view bytes);
    Error headerReady();
    Text result;
    std::string header;
    std::string sequence;
    std::optional<size_t> limit;
    size_t inspected = 0;
    bool validationOnly;
    unsigned remaining = 0;
    bool started = false, cr = false;
    bool lfSeen = false, crSeen = false, crlfSeen = false;
    Error error;
};

Error validateOutput(std::string_view text, FileFormat format, std::optional<size_t> limit);
Error encodeChunks(std::string_view text, FileFormat format, const Sink& sink);

struct StampData
{
    Path target;
    FileInfo info;
    Digest digest{};
    bool exists = false;
};

struct Access
{
    static const StampData* get(const DiskStamp& stamp) { return stamp.data.get(); }
    static DiskStamp stamp(StampData data);
};

struct CachedTextFile
{
    FileInfo info;
    bool supported = false;
};
using TextFileCache = std::unordered_map<Path, CachedTextFile>;

// 32 lowercase hexadecimal characters.
std::string randomId();
Result<Path> resolve(const Path& path, bool allowMissing);

// Streams an open file to the sink and rejects files that change while being read.
Error readAll(const NativeFile& input, const FileInfo& info, std::optional<size_t> limit, const Control* control, const Sink& sink);
Result<LoadedFile> load(const Path& path, LoadOptions options, const Control* control);
Error check(const Path& path, const DiskStamp& expected, const Control* control);
SaveResult save(const Path& path, std::string_view text, SaveOptions options, Control* control);
Result<std::vector<DirectoryEntry>> list(const Path& directory, ListOptions options, const Control* control, TextFileCache* cache);
}  // namespace storage::detail
