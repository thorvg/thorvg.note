#import <Foundation/Foundation.h>
#include <sys/stat.h>
#include <cerrno>
#include <storage/internal.h>

namespace storage::detail
{
Result<Path> moveToTrash(const Path& source, const FileInfo& expected)
{
    // Another file could have taken the name while the contents were verified.
    struct stat current;
    if (lstat(source.c_str(), &current)) {
        auto code = errno;
        return {{}, failure(code == ENOENT ? ErrorCode::Conflict : ErrorCode::Io, "cannot inspect source file", {code, std::generic_category()})};
    }
    if (!S_ISREG(current.st_mode) || uint64_t(current.st_dev) != expected.device || uint64_t(current.st_ino) != expected.id || uint64_t(current.st_size) != expected.size)
        return {{}, failure(ErrorCode::Conflict, "source file changed")};
    @autoreleasepool {
        NSURL* trashed = nil;
        NSError* error = nil;
        auto url = [NSURL fileURLWithFileSystemRepresentation:source.c_str() isDirectory:NO relativeToURL:nil];
        if (![NSFileManager.defaultManager trashItemAtURL:url resultingItemURL:&trashed error:&error])
            return {{}, failure(ErrorCode::Io, error ? error.localizedDescription.UTF8String : "cannot move file to the trash")};
        return {trashed ? Path(trashed.fileSystemRepresentation) : Path(), {}};
    }
}
}  // namespace storage::detail
