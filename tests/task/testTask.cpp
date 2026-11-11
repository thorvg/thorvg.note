#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <common/doctest/doctest.h>
#include <task/task.h>

#if !defined(TN_TASK_SINGLE_THREAD) && (!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__))
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
struct Signal
{
    std::mutex mutex;
    std::condition_variable condition;
    size_t count = 0;

    void send()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++count;
        condition.notify_all();
    }

    bool wait(size_t expected = 1)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, std::chrono::seconds(5), [&] { return count >= expected; });
    }
};
}  // namespace
#endif

static_assert(!std::is_copy_constructible_v<task::TaskContext>);
static_assert(!std::is_move_constructible_v<task::TaskContext>);

TEST_SUITE_BEGIN("task");

TEST_CASE("context")
{
    task::TaskContext context;
    const auto& state = context;

    SUBCASE("cancel")
    {
        CHECK(context.cancel());
        CHECK(state.cancelled());
        CHECK(!context.cancel());
        CHECK(!context.commit());
        CHECK(!context.finish());
    }

    SUBCASE("commit")
    {
        CHECK(context.commit());
        CHECK(!context.cancel());
        CHECK(!context.commit());
        CHECK(context.finish());
        CHECK(context.finish());
    }

    SUBCASE("finish")
    {
        CHECK(context.finish());
        CHECK(!context.cancel());
        CHECK(!context.commit());
        CHECK(context.finish());
    }
}

// A single queue preserves work and completion order in either execution mode.
TEST_CASE("queue_order")
{
    task::TaskScheduler scheduler(2);
    task::TaskQueue queue({}, scheduler);
    std::vector<int> work, completed;

    for (int i = 0; i < 4; ++i)
        queue.submit([&, i](task::TaskContext&) {
            work.push_back(i);
            return [&, i] {
                completed.push_back(i);
            };
        });
    queue.wait();

    CHECK(work == std::vector<int>({0, 1, 2, 3}));
    CHECK(completed.empty());
    CHECK(queue.poll() == 4);
    CHECK(completed == work);
    CHECK(queue.poll() == 0);
}

TEST_CASE("exceptions")
{
    task::TaskScheduler scheduler(1);
    task::TaskQueue queue({}, scheduler);
    bool completed = false;

    CHECK_THROWS_AS(scheduler.submit({}), std::invalid_argument);
    CHECK_THROWS_AS(queue.submit({}), std::invalid_argument);
    scheduler.submit([] { throw std::runtime_error("discarded"); });
    queue.submit([](task::TaskContext&) -> task::TaskQueue::Completion {
        throw std::runtime_error("work failed");
    });
    queue.submit([&](task::TaskContext&) { return [&] {
                                               completed = true;
                                           }; });
    queue.wait();

    // A worker failure is delivered by poll; later completions remain available.
    CHECK_THROWS_AS(queue.poll(), std::runtime_error);
    CHECK(!completed);
    CHECK(queue.poll() == 1);
    CHECK(completed);
}

// A std::bad_alloc thrown by work is rethrown from poll() without losing the following job's completion.
TEST_CASE("exception_isolation")
{
    task::TaskScheduler scheduler(1);
    task::TaskQueue queue({}, scheduler);
    bool completed = false;

    queue.submit([](task::TaskContext&) -> task::TaskQueue::Completion {
        throw std::bad_alloc{};
    });
    queue.submit([&](task::TaskContext&) {
        return [&] {
            completed = true;
        };
    });
    queue.wait();

    CHECK_THROWS_AS(queue.poll(), std::bad_alloc);
    CHECK(!completed);
    CHECK(queue.poll() == 1);
    CHECK(completed);
}

// Nested submission must append work, never recursively execute another job.
TEST_CASE("queue_drain")
{
    task::TaskScheduler scheduler(1);
    task::TaskQueue queue({}, scheduler);
    int depth = 0, maximum = 0, completed = 0;
    std::function<void(int)> submit = [&](int remaining) {
        queue.submit([&, remaining](task::TaskContext&) {
            ++depth;
            if (depth > maximum) maximum = depth;
            if (remaining > 1) submit(remaining - 1);
            --depth;
            return [&] {
                ++completed;
            };
        });
    };
    submit(256);
    queue.wait();

    CHECK(maximum == 1);
    CHECK(completed == 0);
    CHECK(queue.poll() == 256);
    CHECK(completed == 256);
}

// Results produced by a completion belong to the next poll, including without workers.
TEST_CASE("poll_batch")
{
    task::TaskScheduler scheduler(1);
    task::TaskQueue queue({}, scheduler);
    int completed = 0;

    queue.submit([&](task::TaskContext&) {
        return [&] {
            ++completed;
            queue.submit([&](task::TaskContext&) { return [&] {
                                                       ++completed;
                                                   }; });
            queue.wait();
            CHECK(queue.poll() == 0);
        };
    });
    queue.wait();

    CHECK(queue.poll() == 1);
    CHECK(completed == 1);
    CHECK(queue.poll() == 1);
    CHECK(completed == 2);
}

TEST_CASE("closed_queue")
{
    task::TaskScheduler scheduler(1);
    task::TaskQueue queue({}, scheduler);
    bool completed = false;

    queue.submit([&](task::TaskContext&) { return [&] {
                                               completed = true;
                                           }; });
    queue.shutdown();

    CHECK(queue.poll() == 0);
    CHECK(!completed);
    CHECK_THROWS_AS(queue.submit([](task::TaskContext&) { return task::TaskQueue::Completion{}; }), std::logic_error);
}

#if !defined(TN_TASK_SINGLE_THREAD) && (!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__))

// Both workers must enter before either is released, proving actual overlap.
TEST_CASE("parallel_work")
{
    Signal started, release, finished;
    std::atomic<bool> background{true}, released{true};
    auto owner = std::this_thread::get_id();
    task::TaskScheduler scheduler(2);

    for (int i = 0; i < 2; ++i)
        scheduler.submit([&] {
            if (std::this_thread::get_id() == owner) background = false;
            started.send();
            if (!release.wait()) released = false;
            finished.send();
        });

    auto parallel = started.wait(2);
    release.send();  // Release workers even if the overlap check failed.
    CHECK(parallel);
    CHECK(finished.wait(2));
    CHECK(background.load());
    CHECK(released.load());
}

// Separate queues may overlap even though work within each queue remains serial.
TEST_CASE("parallel_queues")
{
    Signal started, release;
    bool firstReleased = false, secondReleased = false;
    task::TaskScheduler scheduler(2);
    task::TaskQueue first({}, scheduler), second({}, scheduler);

    first.submit([&](task::TaskContext&) {
        started.send();
        firstReleased = release.wait();
        return task::TaskQueue::Completion{};
    });
    second.submit([&](task::TaskContext&) {
        started.send();
        secondReleased = release.wait();
        return task::TaskQueue::Completion{};
    });
    auto parallel = started.wait(2);
    release.send();
    first.wait();
    second.wait();

    CHECK(parallel);
    CHECK(firstReleased);
    CHECK(secondReleased);
}

TEST_CASE("concurrent_submit")
{
    task::TaskScheduler scheduler(2);
    task::TaskQueue queue({}, scheduler);
    std::atomic<int> ran{0};
    int completed = 0;
    auto owner = std::this_thread::get_id();
    std::vector<std::thread> callers;

    for (int i = 0; i < 4; ++i)
        callers.emplace_back([&] {
            for (int j = 0; j < 20; ++j)
                queue.submit([&](task::TaskContext&) {
                    ++ran;
                    return [&] {
                        CHECK(std::this_thread::get_id() == owner);
                        ++completed;
                    };
                });
        });
    for (auto& caller : callers)
        caller.join();
    queue.wait();

    CHECK(ran.load() == 80);
    CHECK(completed == 0);
    CHECK(queue.poll() == 80);
    CHECK(completed == 80);
}

// Cancellation reaches active and pending work, but cannot revoke a committed task.
TEST_CASE("cancellation")
{
    bool commit = false;
    SUBCASE("working task") {}
    SUBCASE("committed task")
    {
        commit = true;
    }

    Signal started, release;
    bool activeCancelled = false, pendingCancelled = false;
    bool committed = false, released = false;
    int completed = 0;
    task::TaskScheduler scheduler(1);
    task::TaskQueue queue({}, scheduler);

    queue.submit([&](task::TaskContext& context) {
        if (commit) committed = context.commit();
        started.send();
        released = release.wait();
        activeCancelled = context.cancelled();
        return [&] {
            ++completed;
        };
    });
    auto running = started.wait();
    queue.submit([&](task::TaskContext& context) {
        pendingCancelled = context.cancelled();
        return [&] {
            ++completed;
        };
    });
    queue.cancel();
    release.send();
    queue.wait();

    CHECK(running);
    CHECK(released);
    CHECK(committed == commit);
    CHECK(activeCancelled == !commit);
    CHECK(pendingCancelled);
    CHECK(queue.poll() == 2);  // Cancellation does not discard completions.
    CHECK(completed == 2);
}

TEST_CASE("poll_thread")
{
    task::TaskScheduler scheduler(1);
    task::TaskQueue queue({}, scheduler);
    bool rejected = false;

    std::thread caller([&] {
        try {
            queue.poll();
        } catch (const std::logic_error&) {
            rejected = true;
        }
    });
    caller.join();

    CHECK(rejected);
}

TEST_CASE("shutdown")
{
    Signal started;
    bool cancelled = false, pendingCancelled = false, completed = false;
    task::TaskScheduler scheduler(1);
    task::TaskQueue queue({}, scheduler);

    queue.submit([&](task::TaskContext& context) {
        started.send();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        // Cooperative work must observe shutdown before it can finish.
        while (!context.cancelled() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        cancelled = context.cancelled();
        return [&] {
            completed = true;
        };
    });
    auto running = started.wait();
    queue.submit([&](task::TaskContext& context) {
        pendingCancelled = context.cancelled();
        return task::TaskQueue::Completion{};
    });
    queue.shutdown();

    CHECK(running);
    CHECK(cancelled);
    CHECK(pendingCancelled);
    CHECK(!completed);
    CHECK(queue.poll() == 0);
}

TEST_CASE("context_race")
{
    Signal ready, release;
    task::TaskContext context;
    bool cancelled = false, committed = false;

    std::thread first([&] {
        ready.send();
        if (release.wait()) cancelled = context.cancel();
    });
    std::thread second([&] {
        ready.send();
        if (release.wait()) committed = context.commit();
    });
    auto waiting = ready.wait(2);
    release.send();
    first.join();
    second.join();

    CHECK(waiting);
    CHECK(cancelled != committed);
    CHECK(context.cancelled() == cancelled);
    CHECK(context.finish() == committed);
}

#else

TEST_CASE("inline_work")
{
    task::TaskScheduler scheduler;
    bool ran = false;
    scheduler.submit([&] { ran = true; });
    CHECK(ran);

    task::TaskQueue queue({}, scheduler);
    ran = false;
    bool completed = false;
    queue.submit([&](task::TaskContext&) {
        ran = true;
        return [&] {
            completed = true;
        };
    });

    CHECK(ran);
    CHECK(!completed);
    CHECK(queue.poll() == 1);
    CHECK(completed);
}

// Nested submissions wait for the active work and retain cancellation state.
TEST_CASE("reentrant_submit")
{
    task::TaskScheduler scheduler;
    task::TaskQueue queue({}, scheduler);
    std::vector<int> order;
    bool cancelled = false;

    queue.submit([&](task::TaskContext&) {
        order.push_back(1);
        queue.submit([&](task::TaskContext& pending) {
            cancelled = pending.cancelled();
            order.push_back(3);
            return [&] {
                order.push_back(5);
            };
        });
        queue.cancel();
        order.push_back(2);
        return [&] {
            order.push_back(4);
        };
    });
    queue.wait();

    CHECK(cancelled);
    CHECK(order == std::vector<int>({1, 2, 3}));
    CHECK(queue.poll() == 2);
    CHECK(order == std::vector<int>({1, 2, 3, 4, 5}));
}

#endif

TEST_SUITE_END();
