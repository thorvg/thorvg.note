#include <storage/internal.h>
#include <storage/platform/posix/metadata.h>

namespace storage::detail
{
void readMetadata(FileInfo& info, const struct stat& st)
{
    info.modified = st.st_mtim.tv_sec;
    info.modifiedNanos = st.st_mtim.tv_nsec;
    info.changed = st.st_ctim.tv_sec;
    info.changedNanos = st.st_ctim.tv_nsec;
}

Error copyExtendedMetadata(int, int)
{
    return failure(ErrorCode::UnsupportedMetadata, "metadata preservation is unavailable on this platform");
}

std::optional<int64_t> creationTime(const Path&)
{
    return {};
}
}  // namespace storage::detail
