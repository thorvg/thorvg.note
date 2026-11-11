#include <charconv>
#include <chrono>
#include "internal.h"

namespace storage::detail
{
namespace
{
constexpr size_t metadataLimit = 64 * 1024;
constexpr std::string_view magic = "thornote-recovery 1";

Error corrupt()
{
    return failure(ErrorCode::InvalidRecovery, "invalid, damaged or unsupported recovery record");
}

std::string hex(std::string_view bytes)
{
    constexpr char chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        result += chars[byte >> 4];
        result += chars[byte & 15];
    }
    return result;
}

std::string digest(std::string_view bytes)
{
    Sha256 hash;
    hash.append(bytes);
    auto value = hash.finish();
    return hex({reinterpret_cast<const char*>(value.data()), value.size()});
}

Result<std::string> unhex(std::string_view bytes)
{
    if (bytes.size() % 2) return {{}, corrupt()};
    std::string result;
    result.reserve(bytes.size() / 2);
    auto value = [](char ch) {
        return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                                                                          : -1;
    };
    for (size_t offset = 0; offset < bytes.size(); offset += 2) {
        auto high = value(bytes[offset]), low = value(bytes[offset + 1]);
        if (high < 0 || low < 0) return {{}, corrupt()};
        result += char(high * 16 + low);
    }
    return {std::move(result), {}};
}

template<typename Number>
bool number(std::string_view bytes, Number& value)
{
    auto result = std::from_chars(bytes.data(), bytes.data() + bytes.size(), value);
    return !bytes.empty() && result.ec == std::errc{} && result.ptr == bytes.data() + bytes.size();
}

bool validId(std::string_view id)
{
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

bool validPath(const Path& path)
{
    return path.is_absolute() && path.native().find(Path::value_type(0)) == Path::string_type::npos;
}

std::optional<size_t> recordLimit(std::optional<size_t> textLimit)
{
    if (!textLimit || *textLimit > std::numeric_limits<size_t>::max() - metadataLimit) return {};
    return *textLimit + metadataLimit;
}

Result<std::string> serialize(const RecoverySnapshot& snapshot, std::optional<size_t> maxBytes)
{
    if (!validId(snapshot.id)) return {{}, failure(ErrorCode::InvalidPath, "invalid recovery ID")};
    if (auto error = validateOutput(snapshot.text, {}, maxBytes)) return {{}, error};
    if (auto error = validateOutput(snapshot.title, {}, metadataLimit / 2)) return {{}, error};
    if (!snapshot.originalPath.empty() && !validPath(snapshot.originalPath))
        return {{}, failure(ErrorCode::InvalidPath, "original recovery path must be absolute or empty")};
    if (unsigned(snapshot.format.ending) > unsigned(LineEnding::Mixed)) return {{}, failure(ErrorCode::InvalidText, "invalid recovery line ending")};
    auto original = utf8(snapshot.originalPath);
    if (original.size() > metadataLimit / 2) return {{}, failure(ErrorCode::TooLarge, "recovery metadata exceeds limit")};
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::string body = snapshot.id + '\n' + std::to_string(snapshot.revision) + '\n' + std::to_string(now) + '\n'
        + std::to_string(unsigned(snapshot.format.ending)) + '\n' + (snapshot.format.bom ? "1\n" : "0\n")
        + hex(snapshot.title) + '\n' + hex(original) + '\n' + std::to_string(snapshot.text.size()) + '\n';
    if (magic.size() + 1 + 65 + body.size() > metadataLimit) return {{}, failure(ErrorCode::TooLarge, "recovery metadata exceeds limit")};
    body += snapshot.text;
    return {std::string(magic) + '\n' + digest(body) + '\n' + body, {}};
}

Result<RecoverySnapshot> deserialize(const Text& record, std::string_view id, std::optional<size_t> maxBytes)
{
    if (record.format.bom || record.format.ending != LineEnding::Lf) return {{}, corrupt()};
    std::string_view bytes = record.text;
    std::array<std::string_view, 10> fields;
    size_t offset = 0, bodyOffset = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        auto end = bytes.find('\n', offset);
        if (end == std::string_view::npos) return {{}, corrupt()};
        if (end >= metadataLimit) return {{}, failure(ErrorCode::TooLarge, "recovery metadata exceeds limit")};
        fields[i] = bytes.substr(offset, end - offset);
        offset = end + 1;
        if (i == 1) bodyOffset = offset;
    }
    RecoverySnapshot snapshot;
    unsigned ending = 0, bom = 0;
    size_t textSize = 0;
    if (fields[0] != magic || fields[1] != digest(bytes.substr(bodyOffset)) || fields[2] != id
        || !number(fields[3], snapshot.revision) || !number(fields[4], snapshot.savedAtUnixMilliseconds)
        || !number(fields[5], ending) || ending > unsigned(LineEnding::Mixed)
        || !number(fields[6], bom) || bom > 1 || !number(fields[9], textSize) || textSize != bytes.size() - offset)
        return {{}, corrupt()};
    if (maxBytes && textSize > *maxBytes) return {{}, failure(ErrorCode::TooLarge, "recovery text exceeds limit")};
    auto title = unhex(fields[7]), original = unhex(fields[8]);
    if (!title) return {{}, title.error};
    if (!original) return {{}, original.error};
    if (auto error = validateOutput(title.value, {}, metadataLimit / 2)) return {{}, error};
    try {
        snapshot.originalPath = utf8Path(original.value);
    } catch (const std::exception&) {
        return {{}, corrupt()};
    }
    if (!snapshot.originalPath.empty() && !validPath(snapshot.originalPath)) return {{}, corrupt()};
    snapshot.id = id;
    snapshot.title = std::move(title.value);
    snapshot.format = {LineEnding(ending), bom != 0};
    snapshot.text = bytes.substr(offset);
    return {std::move(snapshot), {}};
}

Error makeDirectories(const Path& directory)
{
    std::error_code ec;
    if (std::filesystem::is_directory(directory, ec)) return {};
    if (ec && ec != std::errc::no_such_file_or_directory) return failure(ErrorCode::Io, "cannot inspect recovery directory", ec);
    if (directory == directory.parent_path()) return failure(ErrorCode::InvalidPath, "recovery root is not a directory");
    if (auto error = makeDirectories(directory.parent_path())) return error;
    if (auto error = createPrivateDirectory(directory)) return error;
    if (!std::filesystem::is_directory(directory, ec)) return failure(ErrorCode::Io, "recovery path is not a directory", ec);
    return {};
}

Result<Path> rootPath(const Path& directory, bool create)
{
    if (!validPath(directory)) return {{}, failure(ErrorCode::InvalidPath, "recovery directory must be an absolute path")};
    auto path = directory;
    while (path != path.root_path() && path.filename().empty())
        path = path.parent_path();
    std::error_code ec;
    auto status = std::filesystem::symlink_status(path, ec);
    if (status.type() == std::filesystem::file_type::not_found) {
        if (!create) return {{}, failure(ErrorCode::NotFound, "recovery directory does not exist")};
        if (auto error = makeDirectories(path)) return {{}, error};
        status = std::filesystem::symlink_status(path, ec);
    }
    if (ec) return {{}, failure(ErrorCode::Io, "cannot inspect recovery directory", ec)};
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
        return {{}, failure(ErrorCode::InvalidPath, "recovery root must be a directory, not a symbolic link")};
    auto canonical = std::filesystem::canonical(path, ec);
    if (ec) return {{}, failure(ErrorCode::Io, "cannot resolve recovery directory", ec)};
    return {std::move(canonical), {}};
}

// Sets exists when a regular record file is present.
Result<Path> recordPath(const Path& directory, std::string_view id, bool create, bool* exists = nullptr)
{
    if (!validId(id)) return {{}, failure(ErrorCode::InvalidPath, "invalid recovery ID")};
    auto root = rootPath(directory, create);
    if (!root) return root;
    auto path = root.value / (std::string(id) + ".recovery");
    std::error_code ec;
    auto status = std::filesystem::symlink_status(path, ec);
    auto found = status.type() != std::filesystem::file_type::not_found;
    if (found) {
        if (ec) return {{}, failure(ErrorCode::Io, "cannot inspect recovery record", ec)};
        if (!std::filesystem::is_regular_file(status)) return {{}, failure(ErrorCode::InvalidPath, "recovery record must be a regular file, not a symbolic link")};
    }
    if (exists) *exists = found;
    return {std::move(path), {}};
}
}  // namespace
}  // namespace storage::detail

namespace storage
{
RecoveryStore::RecoveryStore(Path directory, std::optional<size_t> maxBytes) :
    directory(std::move(directory)), maxBytes(maxBytes) {}

SaveResult RecoveryStore::save(RecoverySnapshot& snapshot)
{
    return detail::guarded<SaveResult>([&]() -> SaveResult {
        SaveResult result;
        if (snapshot.id.empty()) snapshot.id = detail::randomId();
        auto bytes = detail::serialize(snapshot, maxBytes);
        if (!bytes) {
            result.error = bytes.error;
            return result;
        }
        auto path = detail::recordPath(directory, snapshot.id, true);
        if (!path) {
            result.error = path.error;
            return result;
        }
        // An unknown record has a default baseline, which only permits creation.
        auto& stamp = stamps[snapshot.id];
        result = detail::save(path.value, bytes.value, {{}, stamp, detail::recordLimit(maxBytes)}, nullptr);
        if (result.stamp.exists()) stamp = result.stamp;
        return result;
    });
}

Result<RecoverySnapshot> RecoveryStore::load(std::string_view id)
{
    return detail::guarded<Result<RecoverySnapshot>>([&]() -> Result<RecoverySnapshot> {
        auto path = detail::recordPath(directory, id, false);
        if (!path) return {{}, path.error};
        auto record = detail::load(path.value, {false, detail::recordLimit(maxBytes)}, nullptr);
        if (!record) return {{}, record.error};
        auto snapshot = detail::deserialize(record.value, id, maxBytes);
        if (snapshot) stamps[snapshot.value.id] = std::move(record.value.stamp);
        return snapshot;
    });
}

Error RecoveryStore::remove(std::string_view id)
{
    return detail::guarded<Error>([&]() -> Error {
        bool exists = false;
        auto path = detail::recordPath(directory, id, false, &exists);
        if (!path && path.error.code != ErrorCode::NotFound) return path.error;
        auto known = stamps.find(std::string(id));
        if (known == stamps.end() || !known->second.exists())
            return detail::failure(ErrorCode::Conflict, "recovery removal requires a record loaded or saved by this store");
        if (exists) {
            if (auto error = detail::check(path.value, known->second, nullptr)) return error;
            std::error_code ec;
            std::filesystem::remove(path.value, ec);
            if (ec) return detail::failure(ErrorCode::Io, "cannot remove recovery record", ec);
        }
        stamps.erase(known);
        return {};
    });
}
}  // namespace storage
