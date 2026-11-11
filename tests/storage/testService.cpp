#include <condition_variable>
#include <mutex>
#include "common/files.h"

using namespace storage;

namespace
{
struct Wake
{
    std::mutex mutex;
    std::condition_variable condition;
    size_t count = 0;

    void signal()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++count;
        condition.notify_one();
    }

    bool wait(size_t expected)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, std::chrono::seconds(5), [&] { return count >= expected; });
    }
};
}  // namespace

TEST_SUITE_BEGIN("service");

TEST_CASE("callback_thread")
{
    test::Directory directory;
    test::write(directory.file(), "text");
    Wake wake;
    FileService service([&] { wake.signal(); });
    auto owner = std::this_thread::get_id();
    bool completed = false;

    service.open(directory.file(), {}, [&](Result<LoadedFile> result) {
        REQUIRE(result);
        CHECK(std::this_thread::get_id() == owner);
        CHECK_EQ(result.value.text, "text");
        completed = true;
    });
    REQUIRE(wake.wait(1));
    CHECK_FALSE(completed);
    CHECK_EQ(service.poll(), 1);
    CHECK(completed);
    CHECK_EQ(service.poll(), 0);
}

TEST_CASE("save_order")
{
    test::Directory directory;
    FileService service;
    std::vector<std::string> order;
    std::string snapshot = "snapshot";

    service.save(directory.file(), snapshot, {}, [&](SaveResult result) {
        REQUIRE(result);
        order.push_back("save");
    });
    snapshot = "edited after submission";
    service.open(directory.file(), {}, [&](Result<LoadedFile> result) {
        REQUIRE(result);
        CHECK_EQ(result.value.text, "snapshot");
        order.push_back("open");
    });
    REQUIRE(test::drain(service, [&] { return order.size() == 2; }));
    CHECK(order == std::vector<std::string>({"save", "open"}));
}

TEST_CASE("followup_save")
{
    test::Directory directory;
    FileService service;
    bool completed = false;

    service.save(directory.file(), "first", {}, [&](SaveResult first) {
        REQUIRE(first);
        CHECK_EQ(service.poll(), 0);
        service.save(directory.file(), "second", {{}, first.stamp}, [&](SaveResult second) {
            REQUIRE(second);
            completed = true;
        });
    });
    REQUIRE(test::drain(service, [&] { return completed; }));
    CHECK_EQ(test::read(directory.file()), "second");
}

TEST_CASE("concurrent_requests")
{
    test::Directory directory;
    test::write(directory.file(), "text");
    FileService service;
    std::vector<std::thread> callers;
    size_t completed = 0;

    for (unsigned i = 0; i < 8; ++i)
        callers.emplace_back([&] {
            service.open(directory.file(), {}, [&](Result<LoadedFile> result) {
                REQUIRE(result);
                ++completed;
            });
        });
    for (auto& thread : callers)
        thread.join();
    REQUIRE(test::drain(service, [&] { return completed == 8; }));
}

TEST_CASE("callback_destruction")
{
    test::Directory directory;
    test::write(directory.file(), "text");
    Wake wake;
    auto service = std::make_unique<FileService>([&] { wake.signal(); });
    bool stale = false;

    service->open(directory.file(), {}, [&](Result<LoadedFile> result) {
        REQUIRE(result);
        service.reset();
    });
    service->open(directory.file(), {}, [&](Result<LoadedFile>) { stale = true; });
    REQUIRE(wake.wait(2));
    CHECK_EQ(service->poll(), 1);
    CHECK_FALSE(service);
    CHECK_FALSE(stale);
}

TEST_SUITE_END();
