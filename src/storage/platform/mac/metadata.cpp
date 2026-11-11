#include <sys/xattr.h>
#include <copyfile.h>
#include <cerrno>
#include <storage/platform/posix/metadata.h>
#include <storage/internal.h>

namespace storage::detail
{
void readMetadata(FileInfo& info, const struct stat& st)
{
    info.modified = st.st_mtimespec.tv_sec;
    info.modifiedNanos = st.st_mtimespec.tv_nsec;
    info.changed = st.st_ctimespec.tv_sec;
    info.changedNanos = st.st_ctimespec.tv_nsec;
    info.flags = st.st_flags;
}

ssize_t listAttributes(int file, char* names, size_t size)
{
    return flistxattr(file, names, size, 0);
}

ssize_t getAttribute(int file, const char* name, void* value, size_t size)
{
    return fgetxattr(file, name, value, size, 0, 0);
}

int removeAttribute(int file, const char* name)
{
    return fremovexattr(file, name, 0);
}

int setAttribute(int file, const char* name, const void* value, size_t size)
{
    return fsetxattr(file, name, value, size, 0, 0);
}

Error copyExtendedMetadata(int source, int output)
{
    if (fcopyfile(source, output, nullptr, COPYFILE_ACL | COPYFILE_XATTR))
        return failure(ErrorCode::UnsupportedMetadata, "cannot preserve ACL/extended attributes", {errno, std::generic_category()});
    return copyAttributes(source, output);
}

std::optional<int64_t> creationTime(const Path& path)
{
    struct stat info
    {
    };
    if (stat(path.c_str(), &info)) return {};
    return int64_t(info.st_birthtimespec.tv_sec) * 1000 + info.st_birthtimespec.tv_nsec / 1000000;
}
}  // namespace storage::detail
