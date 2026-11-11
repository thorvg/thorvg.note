#include <algorithm>
#include <random>
#include "internal.h"

namespace storage::detail
{
thread_local const Hooks* hooks = nullptr;

namespace
{
Error inject(Stage stage)
{
    if (hooks && hooks->before)
        if (auto error = hooks->before(stage)) return failure(ErrorCode::Io, "injected I/O failure", error);
    return {};
}

Error conflict(const char* message, std::error_code system = {})
{
    return failure(ErrorCode::Conflict, message, system);
}

// Stages and installs the output; stamp describes the installed bytes once staging is committed.
Error stage(const Path& path, std::string_view text, const SaveOptions& options, Control* control, StagingFile& staging, DiskStamp& stamp)
{
    if (auto error = cancelled(control)) return error;
    if (auto error = validateOutput(text, options.format, options.maxBytes)) return error;
    auto target = resolve(path, true);
    if (!target) return target.error;
    auto existing = options.expected.exists();
    NativeFile original;
    FileInfo originalInfo;
    if (existing) {
        auto input = openFile(target.value);
        if (!input) return input.error.code == ErrorCode::NotFound ? conflict("original file disappeared", input.error.system) : input.error;
        original = std::move(input.value);
        auto info = fileInfo(original);
        if (!info) return info.error;
        if (!Access::get(options.expected)->info.sameFile(info.value)) return conflict("file differs from the supplied baseline");
        if (auto error = writable(target.value, info.value)) return error;
        originalInfo = info.value;
    }
    if (auto error = inject(Stage::Create)) return error;
    if (auto error = createStaging(staging, target.value, existing)) return error;
    Sha256 hash;
    auto error = encodeChunks(text, options.format, [&](std::string_view bytes) {
        while (!bytes.empty()) {
            if (auto error = cancelled(control)) return error;
            if (auto error = inject(Stage::Write)) return error;
            auto written = writeBytes(staging.file, bytes.substr(0, hooks ? hooks->maxWrite : bytes.size()));
            if (!written) return written.error;
            if (!written.value) return failure(ErrorCode::Io, "zero length write");
            hash.append(bytes.substr(0, written.value));
            bytes.remove_prefix(written.value);
        }
        return Error{};
    });
    if (error) return error;
    if (auto error = inject(Stage::Metadata)) return error;
    if (existing)
        if (auto error = copyMetadata(original, staging.file, originalInfo)) return error;
    if (auto error = inject(Stage::FileSync)) return error;
    if (auto error = syncFile(staging.file)) return error;
    // Replacement keeps the staged file's identity, so it serves as the next baseline.
    auto staged = fileInfo(staging.file);
    if (!staged) return staged.error;
    if (auto error = inject(Stage::Close)) return error;
    if (auto error = staging.file.close()) return error;
    if (auto error = inject(Stage::Verify)) return error;
    if (auto error = check(path, options.expected, control)) return error;
    if (existing) {
        auto info = fileInfo(original);
        if (!info) return info.error;
        if (!(info.value == originalInfo)) return conflict("file metadata changed while saving");
    }
    // A symlink or parent path may have been redirected while staging was written.
    auto commitTarget = resolve(path, true);
    if (!commitTarget || commitTarget.value != target.value) return conflict("save target changed");
    // Prepared before replacement so that no later failure can lose the new baseline.
    stamp = Access::stamp({target.value, staged.value, hash.finish(), true});
    if (auto error = inject(Stage::Commit)) return error;
    if (control && !control->commit()) return cancelled(control);
    error = commit(staging, target.value, existing);
    if (!error) error = inject(Stage::DirectorySync);
    if (!error) error = syncDirectory(staging);
    return error;
}
}  // namespace

std::string randomId()
{
    std::random_device random;
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned i = 0; i < 4; ++i) {
        auto value = random();
        for (unsigned shift = 0; shift < 32; shift += 4)
            result += hex[(value >> shift) & 15];
    }
    return result;
}

Result<Path> resolve(const Path& path, bool allowMissing)
{
    if (path.empty() || path.native().find(Path::value_type(0)) != Path::string_type::npos)
        return {{}, failure(ErrorCode::InvalidPath, "empty path or embedded NUL")};
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec) return {{}, failure(ErrorCode::InvalidPath, "cannot make path absolute", ec)};
    auto target = std::filesystem::canonical(absolute, ec);
    if (!ec) return {std::move(target), {}};
    if (ec != std::errc::no_such_file_or_directory || !allowMissing)
        return {{}, failure(ec == std::errc::no_such_file_or_directory ? ErrorCode::NotFound : ErrorCode::Io, "cannot resolve file", ec)};
    // A dangling symlink is not a request to replace the link with a new file.
    std::error_code linkError;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(absolute, linkError)))
        return {{}, failure(ErrorCode::NotFound, "dangling symbolic link")};
    auto parent = std::filesystem::canonical(absolute.parent_path(), ec);
    if (ec) return {{}, failure(ErrorCode::NotFound, "parent directory is unavailable", ec)};
    return {parent / absolute.filename(), {}};
}

DiskStamp Access::stamp(StampData data)
{
    DiskStamp result;
    result.data = std::make_shared<const StampData>(std::move(data));
    return result;
}

Error readAll(const NativeFile& input, const FileInfo& info, std::optional<size_t> limit, const Control* control, const Sink& sink)
{
    if (limit && info.size > *limit) return failure(ErrorCode::TooLarge, "file exceeds limit");
    std::array<char, 64 * 1024> buffer;
    uint64_t total = 0;
    for (;;) {
        if (auto error = cancelled(control)) return error;
        auto count = readBytes(input, buffer.data(), buffer.size());
        if (!count) return count.error;
        if (!count.value) break;
        if (count.value > info.size - total) return conflict("file grew while reading");
        total += count.value;
        if (auto error = sink({buffer.data(), count.value})) return error;
    }
    auto after = fileInfo(input);
    if (!after) return after.error;
    if (!(info == after.value) || total != info.size) return conflict("file changed while reading");
    return {};
}

Result<LoadedFile> load(const Path& path, LoadOptions options, const Control* control)
{
    return guarded<Result<LoadedFile>>([&]() -> Result<LoadedFile> {
        if (auto error = cancelled(control)) return {{}, error};
        auto target = resolve(path, options.createIfMissing);
        if (!target) return {{}, target.error};
        std::error_code ec;
        LoadedFile loaded;
        loaded.path = std::filesystem::absolute(path, ec);
        if (ec) return {{}, failure(ErrorCode::InvalidPath, "cannot make path absolute", ec)};
        auto input = openFile(target.value);
        if (!input) {
            if (input.error.code != ErrorCode::NotFound || !options.createIfMissing) return {{}, input.error};
            loaded.stamp = Access::stamp({target.value, {}, {}, false});
            return {std::move(loaded), {}};
        }
        auto info = fileInfo(input.value);
        if (!info) return {{}, info.error};
        Sha256 hash;
        Decoder decoder(options.maxBytes);
        auto error = readAll(input.value, info.value, options.maxBytes, control, [&](std::string_view bytes) {
            hash.append(bytes);
            return decoder.append(bytes);
        });
        if (!error) error = input.value.close();
        if (error) return {{}, error};
        auto text = decoder.finish();
        if (!text) return {{}, text.error};
        static_cast<Text&>(loaded) = std::move(text.value);
        loaded.stamp = Access::stamp({target.value, info.value, hash.finish(), true});
        return {std::move(loaded), {}};
    });
}

Error check(const Path& path, const DiskStamp& expected, const Control* control)
{
    auto baseline = Access::get(expected);
    auto target = resolve(path, true);
    if (!target) return expected.exists() && target.error.code == ErrorCode::NotFound ? conflict("original file disappeared", target.error.system) : target.error;
    auto input = openFile(target.value);
    if (!input) {
        if (input.error.code != ErrorCode::NotFound) return input.error;
        if (expected.exists()) return conflict("original file disappeared", input.error.system);
        return baseline && baseline->target != target.value ? conflict("baseline belongs to another destination") : Error{};
    }
    if (!expected.exists()) return conflict("destination already exists");
    auto info = fileInfo(input.value);
    if (!info) return info.error;
    // OS services may update metadata after a save without changing the file's bytes.
    if (!baseline->info.sameFile(info.value) || baseline->info.size != info.value.size) return conflict("file differs from the supplied baseline");
    Sha256 hash;
    auto error = readAll(input.value, info.value, {}, control, [&](std::string_view bytes) {
        hash.append(bytes);
        return Error{};
    });
    if (error) return error;
    return hash.finish() == baseline->digest ? Error{} : conflict("file differs from the supplied baseline");
}

SaveResult save(const Path& path, std::string_view text, SaveOptions options, Control* control)
{
    SaveResult result;
    StagingFile staging;
    DiskStamp stamp;
    result.error = guarded<Error>([&] { return stage(path, text, options, control, staging, stamp); });
    if (!staging.committed) return result;
    result.state = result.error ? SaveState::CommittedDurabilityUnknown : SaveState::Committed;
    result.stamp = std::move(stamp);
    return result;
}
}  // namespace storage::detail

namespace storage
{
bool DiskStamp::exists() const
{
    return data && data->exists;
}

std::array<uint8_t, 32> DiskStamp::digest() const
{
    return data ? data->digest : std::array<uint8_t, 32>{};
}

Result<LoadedFile> load(const Path& path, LoadOptions options)
{
    return detail::load(path, options, nullptr);
}

SaveResult save(const Path& path, std::string_view text, SaveOptions options)
{
    return detail::save(path, text, options, nullptr);
}
}  // namespace storage
