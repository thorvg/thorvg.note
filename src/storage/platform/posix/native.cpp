#include <utility>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <storage/internal.h>
#include "metadata.h"

namespace storage::detail
{
namespace
{
Error systemError(const char* message, ErrorCode code = ErrorCode::Io)
{
    return failure(code, message, {errno, std::generic_category()});
}

int fd(const NativeFile& file)
{
    return int(file.handle);
}
}  // namespace

Error createPrivateDirectory(const Path& path)
{
    if (::mkdir(path.c_str(), 0700)) {
        if (errno == EEXIST) return {};  // Caller checks the resulting directory type.
        return systemError("cannot create recovery directory");
    }
    // Persist newly created ancestors as well as the eventual snapshot's directory.
    auto handle = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (handle < 0) return systemError("cannot open recovery parent directory");
    NativeFile parent(handle);
    return syncFile(parent);
}

Error closeHandle(intptr_t handle)
{
    // Do not retry close(EINTR): the descriptor may already have been released.
    return ::close(int(handle)) ? systemError("file close failed") : Error{};
}

void discardHandle(intptr_t handle) noexcept
{
    ::close(int(handle));
}

Result<NativeFile> openFile(const Path& path, bool writable)
{
    int handle;
    do {
        handle = ::open(path.c_str(), (writable ? O_WRONLY : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    } while (handle < 0 && errno == EINTR);
    if (handle < 0) return {{}, systemError("cannot open file", errno == ENOENT ? ErrorCode::NotFound : writable && (errno == EACCES || errno == EPERM) ? ErrorCode::ReadOnly
                                                                                                                                                        : ErrorCode::Io)};
    return {NativeFile(handle), {}};
}

Result<FileInfo> fileInfo(const NativeFile& file)
{
    struct stat st;
    if (fstat(fd(file), &st)) return {{}, systemError("cannot inspect file")};
    if (!S_ISREG(st.st_mode)) return {{}, failure(ErrorCode::InvalidPath, "only regular files are supported")};
    FileInfo info;
    info.device = uint64_t(st.st_dev);
    info.id = uint64_t(st.st_ino);
    info.size = uint64_t(st.st_size);
    info.links = uint64_t(st.st_nlink);
    info.owner = st.st_uid;
    info.group = st.st_gid;
    info.mode = st.st_mode;
    readMetadata(info, st);
    return {info, {}};
}

Result<size_t> readBytes(const NativeFile& file, char* buffer, size_t size)
{
    ssize_t count;
    do {
        count = ::read(fd(file), buffer, size);
    } while (count < 0 && errno == EINTR);
    if (count < 0) return {0, systemError("file read failed")};
    return {size_t(count), {}};
}

Result<size_t> writeBytes(const NativeFile& file, std::string_view bytes)
{
    ssize_t count;
    do {
        count = ::write(fd(file), bytes.data(), bytes.size());
    } while (count < 0 && errno == EINTR);
    if (count < 0) return {0, systemError("file write failed")};
    return {size_t(count), {}};
}

StagingFile::~StagingFile()
{
    if (!committed && !path.empty() && directory.handle != -1) {
        const auto& native = path.native();
        auto name = native.c_str() + (native.find_last_of('/') + 1);
        unlinkat(fd(directory), name, 0);
    }
}

Error createStaging(StagingFile& staging, const Path& target, bool)
{
    auto directory = ::open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return systemError("cannot open destination directory");
    staging.directory = NativeFile(directory);
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        auto candidate = target.parent_path() / (".thornote-" + randomId());
        auto handle = openat(directory, candidate.filename().c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (handle >= 0) {
            staging.path = std::move(candidate);
            staging.file = NativeFile(handle);
            return {};
        }
        if (errno != EEXIST) return systemError("cannot create sibling staging file");
    }
    return failure(ErrorCode::Io, "cannot allocate a unique staging file");
}

Error writable(const Path& target, const FileInfo& info)
{
    if (!(info.mode & 0222)) return failure(ErrorCode::ReadOnly, "file has no write permission");
    if (info.links != 1) return failure(ErrorCode::UnsupportedMetadata, "hard-linked files require in-place saving");
    if (info.owner != uint64_t(geteuid()) || info.flags != 0)
        return failure(ErrorCode::UnsupportedMetadata, "file ownership or flags cannot be preserved");
    auto handle = openFile(target, true);  // Permission check without truncating or writing.
    if (!handle) return handle.error;
    auto current = fileInfo(handle.value);
    if (!current || !(current.value == info)) return failure(ErrorCode::Conflict, "file changed during permission check");
    return {};
}

Error copyMetadata(const NativeFile& source, const NativeFile& output, const FileInfo& info)
{
    struct stat current;
    if (fstat(fd(output), &current)) return systemError("cannot inspect staging file");
    if ((uint64_t(current.st_uid) != info.owner || uint64_t(current.st_gid) != info.group) && fchown(fd(output), uid_t(info.owner), gid_t(info.group)))
        return systemError("cannot preserve owner/group", ErrorCode::UnsupportedMetadata);
    if (fchmod(fd(output), mode_t(info.mode & 07777)))
        return systemError("cannot preserve file mode", ErrorCode::UnsupportedMetadata);
    if (auto error = copyExtendedMetadata(fd(source), fd(output))) return error;
    auto saved = fileInfo(output);
    if (!saved) return saved.error;
    if (saved.value.mode != info.mode || saved.value.owner != info.owner || saved.value.group != info.group)
        return failure(ErrorCode::UnsupportedMetadata, "staging metadata does not match original");
    return {};
}

Error syncFile(const NativeFile& file)
{
    int result;
    do {
        result = fsync(fd(file));
    } while (result && errno == EINTR);
    return result ? systemError("file sync failed") : Error{};
}

Error commit(StagingFile& staging, const Path& target, bool existing)
{
    struct stat opened, named;
    if (fstat(fd(staging.directory), &opened) || stat(target.parent_path().c_str(), &named))
        return systemError("cannot inspect destination directory");
    if (opened.st_dev != named.st_dev || opened.st_ino != named.st_ino)
        return failure(ErrorCode::Conflict, "destination directory changed");
    auto source = staging.path.filename(), destination = target.filename();
    if (existing) {
        if (renameat(fd(staging.directory), source.c_str(), fd(staging.directory), destination.c_str()))
            return systemError("atomic replacement failed");
        staging.committed = true;
    } else {
#ifdef __EMSCRIPTEN__
        // Emscripten file systems have no hard links, and they belong to this process alone: no new arrival can slip in before the rename.
        struct stat present;
        if (!fstatat(fd(staging.directory), destination.c_str(), &present, AT_SYMLINK_NOFOLLOW)) errno = EEXIST;
        else if (errno == ENOENT && !renameat(fd(staging.directory), source.c_str(), fd(staging.directory), destination.c_str())) {
            staging.committed = true;
            return {};
        }
        return systemError("create-only commit failed", errno == EEXIST ? ErrorCode::Conflict : ErrorCode::Io);
#else
        // linkat is an atomic create-if-absent; ordinary rename would overwrite a new arrival.
        if (linkat(fd(staging.directory), source.c_str(), fd(staging.directory), destination.c_str(), 0))
            return systemError("create-only commit failed", errno == EEXIST ? ErrorCode::Conflict : ErrorCode::Io);
        staging.committed = true;
        if (unlinkat(fd(staging.directory), source.c_str(), 0)) return systemError("committed but staging cleanup failed");
#endif
    }
    return {};
}

Error syncDirectory(const StagingFile& staging)
{
    return syncFile(staging.directory);
}
}  // namespace storage::detail
