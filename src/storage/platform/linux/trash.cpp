#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <storage/internal.h>

// Follows the freedesktop.org trash specification.
namespace storage::detail
{
namespace
{
Error systemFailure(const char* message)
{
    return failure(ErrorCode::Io, message, {errno, std::generic_category()});
}

// Creates a private directory, accepting one that is already there.
bool makeDirectory(const Path& path)
{
    struct stat info;
    if (mkdir(path.c_str(), 0700) && errno != EEXIST) return false;
    return !lstat(path.c_str(), &info) && S_ISDIR(info.st_mode);
}

bool makeTrash(const Path& trash)
{
    return makeDirectory(trash) && makeDirectory(trash / "files") && makeDirectory(trash / "info");
}

// The home trash when it shares the file's volume, otherwise one at the top of that volume.
Result<Path> trashDirectory(const Path& source, dev_t device)
{
    Path home;
    if (auto data = getenv("XDG_DATA_HOME"); data && *data == '/') home = Path(data) / "Trash";
    else if (auto user = getenv("HOME"); user && *user == '/') home = Path(user) / ".local/share/Trash";
    struct stat info;
    if (!home.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(home.parent_path(), ec);
        if (!ec && !stat(home.parent_path().c_str(), &info) && info.st_dev == device) {
            if (!makeTrash(home)) return {{}, systemFailure("cannot create the trash")};
            return {home, {}};
        }
    }
    auto top = source.parent_path();
    while (top.has_relative_path() && !stat(top.parent_path().c_str(), &info) && info.st_dev == device)
        top = top.parent_path();
    auto uid = std::to_string(getuid());
    // An administrator's shared trash must be a sticky directory, never a link.
    if (!lstat((top / ".Trash").c_str(), &info) && S_ISDIR(info.st_mode) && (info.st_mode & S_ISVTX) && makeTrash(top / ".Trash" / uid)) return {top / ".Trash" / uid, {}};
    auto own = top / (".Trash-" + uid);
    if (!makeTrash(own) || lstat(own.c_str(), &info) || info.st_uid != getuid()) return {{}, failure(ErrorCode::Io, "the volume has no usable trash")};
    return {own, {}};
}

std::string trashInfo(const Path& source)
{
    std::string text = "[Trash Info]\nPath=";
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char ch : source.native()) {
        if (isalnum(ch) || ch == '/' || ch == '-' || ch == '_' || ch == '.' || ch == '~') text += char(ch);
        else text += {'%', hex[ch >> 4], hex[ch & 15]};
    }
    char date[32];
    auto now = time(nullptr);
    struct tm local;
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", localtime_r(&now, &local));
    return text + "\nDeletionDate=" + date + "\n";
}
}  // namespace

Result<Path> moveToTrash(const Path& source, const FileInfo& expected)
{
    auto trash = trashDirectory(source, dev_t(expected.device));
    if (!trash) return trash;
    auto text = trashInfo(source);
    auto stem = source.stem().native(), extension = source.extension().native();
    for (unsigned copy = 1; copy < 1000; ++copy) {
        auto name = copy == 1 ? source.filename().native() : stem + " " + std::to_string(copy) + extension;
        // The info file claims the name before the file arrives.
        auto info = trash.value / "info" / (name + ".trashinfo");
        auto file = open(info.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (file < 0) {
            if (errno == EEXIST) continue;
            return {{}, systemFailure("cannot record the trashed file")};
        }
        auto written = write(file, text.data(), text.size());
        if (close(file) || written != ssize_t(text.size())) {
            auto error = systemFailure("cannot record the trashed file");
            unlink(info.c_str());
            return {{}, error};
        }
        auto target = trash.value / "files" / name;
        auto error = changeName(source, target, expected);
        if (!error) return {target, {}};
        unlink(info.c_str());
        struct stat occupied;
        if (error.code != ErrorCode::Conflict || lstat(target.c_str(), &occupied)) return {{}, error};
    }
    return {{}, failure(ErrorCode::Io, "the trash has no free name")};
}
}  // namespace storage::detail
