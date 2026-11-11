#include "internal.h"

namespace storage
{
struct FileService::Impl
{
    detail::TextFileCache textFiles;
    task::TaskQueue queue;
    explicit Impl(std::function<void()> wake) :
        queue(std::move(wake)) {}
};

namespace
{
// Runs work on the queue and delivers its result, or the cancellation, through poll().
template<typename ResultType, typename Work>
void post(task::TaskQueue& queue, std::function<void(ResultType)> done, Work work)
{
    queue.submit([work = std::move(work), done = std::move(done)](detail::Control& control) {
        ResultType result = work(control);
        if (!control.finish()) {
            result = {};
            result.error = detail::failure(ErrorCode::Cancelled, "cancelled");
        }
        return [done, result = std::move(result)]() mutable {
            if (done) done(std::move(result));
        };
    });
}
}  // namespace

FileService::FileService(std::function<void()> wakeHost) :
    impl(std::make_shared<Impl>(std::move(wakeHost))) {}

FileService::~FileService()
{
    impl->queue.shutdown();
}

void FileService::open(Path path, LoadOptions options, OpenCallback done)
{
    post<Result<LoadedFile>>(impl->queue, std::move(done), [path = std::move(path), options](detail::Control& control) {
        return detail::load(path, options, &control);
    });
}

void FileService::save(Path path, std::string text, SaveOptions options, SaveCallback done)
{
    post<SaveResult>(impl->queue, std::move(done), [path = std::move(path), text = std::move(text), options](detail::Control& control) {
        return detail::save(path, text, options, &control);
    });
}

void FileService::list(Path directory, DirectoryCallback done, ListOptions options)
{
    post<Result<std::vector<DirectoryEntry>>>(impl->queue, std::move(done), [directory = std::move(directory), options, cache = &impl->textFiles](detail::Control& control) {
        return detail::list(directory, options, &control, cache);
    });
}

void FileService::cancel()
{
    impl->queue.cancel();
}

size_t FileService::poll()
{
    auto state = impl;
    return state->queue.poll();
}
}  // namespace storage
