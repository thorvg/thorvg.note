#pragma once

#include <tuple>
#include <utility>
#include <storage/storage.h>

namespace storage::detail
{

struct FileInfo
{
    uint64_t device = 0, id = 0, size = 0, links = 0;
    uint64_t owner = 0, group = 0, mode = 0, flags = 0;
    int64_t modified = 0, modifiedNanos = 0, changed = 0, changedNanos = 0;

    bool sameFile(const FileInfo& other) const
    {
        return device == other.device && id == other.id;
    }

    bool operator==(const FileInfo& b) const
    {
        return std::tie(device, id, size, links, owner, group, mode, flags, modified, modifiedNanos, changed, changedNanos)
            == std::tie(b.device, b.id, b.size, b.links, b.owner, b.group, b.mode, b.flags, b.modified, b.modifiedNanos, b.changed, b.changedNanos);
    }
};

Error closeHandle(intptr_t handle);
void discardHandle(intptr_t handle) noexcept;

struct NativeFile
{
    intptr_t handle = -1;

    NativeFile() = default;
    explicit NativeFile(intptr_t handle) :
        handle(handle) {}
    ~NativeFile()
    {
        if (handle != -1) discardHandle(handle);
    }
    NativeFile(const NativeFile&) = delete;
    NativeFile& operator=(const NativeFile&) = delete;
    NativeFile& operator=(NativeFile&& other) noexcept
    {
        if (this != &other) {
            if (handle != -1) discardHandle(handle);
            handle = std::exchange(other.handle, -1);
        }
        return *this;
    }

    Error close()
    {
        return handle == -1 ? Error{} : closeHandle(std::exchange(handle, -1));
    }
};

struct StagingFile
{
    NativeFile file, directory;
    Path path;
    bool committed = false;

    ~StagingFile();
    StagingFile() = default;
    StagingFile(const StagingFile&) = delete;
    StagingFile& operator=(const StagingFile&) = delete;
};

Result<NativeFile> openFile(const Path& path, bool writable = false);
Result<FileInfo> fileInfo(const NativeFile& file);
Result<size_t> readBytes(const NativeFile& file, char* buffer, size_t size);
Result<size_t> writeBytes(const NativeFile& file, std::string_view bytes);
Error createStaging(StagingFile& staging, const Path& target, bool existing);
Error copyMetadata(const NativeFile& source, const NativeFile& output, const FileInfo& info);
Error syncFile(const NativeFile& file);
Error commit(StagingFile& staging, const Path& target, bool existing);
Error syncDirectory(const StagingFile& staging);
Error writable(const Path& target, const FileInfo& info);
Error createPrivateDirectory(const Path& path);
std::optional<int64_t> creationTime(const Path& path);

}  // namespace storage::detail
