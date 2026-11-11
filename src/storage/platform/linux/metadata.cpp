#include <fcntl.h>
#include <sys/xattr.h>
#include <storage/platform/posix/metadata.h>
#include <storage/internal.h>

namespace storage::detail
{
void readMetadata(FileInfo& info, const struct stat& st)
{
    info.modified = st.st_mtim.tv_sec;
    info.modifiedNanos = st.st_mtim.tv_nsec;
    info.changed = st.st_ctim.tv_sec;
    info.changedNanos = st.st_ctim.tv_nsec;
}

ssize_t listAttributes(int file, char* names, size_t size)
{
    return flistxattr(file, names, size);
}

ssize_t getAttribute(int file, const char* name, void* value, size_t size)
{
    return fgetxattr(file, name, value, size);
}

int removeAttribute(int file, const char* name)
{
    return fremovexattr(file, name);
}

int setAttribute(int file, const char* name, const void* value, size_t size)
{
    return fsetxattr(file, name, value, size, 0);
}

Error copyExtendedMetadata(int source, int output)
{
    return copyAttributes(source, output);
}

std::optional<int64_t> creationTime(const Path& path)
{
    struct statx info
    {
    };
    if (statx(AT_FDCWD, path.c_str(), 0, STATX_BTIME, &info) || !(info.stx_mask & STATX_BTIME)) return {};
    return info.stx_btime.tv_sec * 1000 + info.stx_btime.tv_nsec / 1000000;
}
}  // namespace storage::detail
