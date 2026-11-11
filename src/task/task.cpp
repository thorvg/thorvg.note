#include <algorithm>
#include <deque>
#include <exception>
#include <list>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>
#include "task.h"

#if !defined(TN_TASK_SINGLE_THREAD) && (!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__))
#define TN_TASK_WORKERS
#include <condition_variable>
#include <thread>
#endif

namespace task
{
TaskContext::TaskContext() = default;

bool TaskContext::cancelled() const
{
    return phase.load() == Phase::Cancelled;
}

bool TaskContext::cancel()
{
    auto expected = Phase::Working;
    return phase.compare_exchange_strong(expected, Phase::Cancelled);
}

bool TaskContext::commit()
{
    auto expected = Phase::Working;
    return phase.compare_exchange_strong(expected, Phase::Committing);
}

bool TaskContext::finish()
{
    auto current = phase.load();
    for (;;) {
        if (current == Phase::Cancelled) return false;
        if (current == Phase::Finished) return true;
        if (phase.compare_exchange_weak(current, Phase::Finished)) return true;
    }
}

struct TaskScheduler::Impl
{
#ifdef TN_TASK_WORKERS
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<Work> jobs;
    std::vector<std::thread> workers;
    bool stopping = false;

    void run()
    {
        for (;;) {
            Work work;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [&] { return stopping || !jobs.empty(); });
                if (jobs.empty()) return;
                work = std::move(jobs.front());
                jobs.pop_front();
            }
            try {
                work();
            } catch (...) {
            }
        }
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        condition.notify_all();
        for (auto& worker : workers)
            if (worker.joinable()) {
                if (worker.get_id() == std::this_thread::get_id()) worker.detach();
                else worker.join();
            }
    }
#endif
};

TaskScheduler::TaskScheduler(size_t count) :
    impl(std::make_shared<Impl>())
{
#ifdef TN_TASK_WORKERS
    if (!count) count = std::max(2u, std::min(4u, std::thread::hardware_concurrency()));
    try {
        for (size_t i = 0; i < count; ++i)
            impl->workers.emplace_back([state = impl] { state->run(); });
    } catch (...) {
        impl->stop();
        throw;
    }
#else
    (void)count;
#endif
}

TaskScheduler::~TaskScheduler()
{
#ifdef TN_TASK_WORKERS
    impl->stop();
#endif
}

TaskScheduler& TaskScheduler::shared()
{
    static TaskScheduler scheduler;
    return scheduler;
}

void TaskScheduler::submit(Work work)
{
    if (!work) throw std::invalid_argument("empty task");
#ifdef TN_TASK_WORKERS
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (impl->stopping) throw std::logic_error("task scheduler is stopping");
        impl->jobs.push_back(std::move(work));
    }
    impl->condition.notify_one();
#else
    try {
        work();
    } catch (...) {
    }
#endif
}

namespace
{
#ifdef TN_TASK_WORKERS
using QueueMutex = std::mutex;
#else
struct QueueMutex
{
    void lock() {}
    void unlock() {}
};
#endif
}  // namespace

struct TaskQueue::Impl : std::enable_shared_from_this<Impl>
{
    struct Job
    {
        TaskContext context;
        Work work;
        Completion completion;
        std::exception_ptr error;

        explicit Job(Work work) :
            work(std::move(work)) {}
    };
    TaskScheduler& scheduler;
    std::function<void()> wake;
    QueueMutex mutex;
    std::list<Job> jobs, ready;
    bool running = false, closed = false, polling = false;
#ifdef TN_TASK_WORKERS
    std::condition_variable condition;
    std::thread::id owner = std::this_thread::get_id();
#endif

    Impl(TaskScheduler& scheduler, std::function<void()> wake) :
        scheduler(scheduler), wake(std::move(wake)) {}

    void run()
    {
        for (;;) {
            Job* job;
            {
                std::lock_guard<QueueMutex> lock(mutex);
                if (jobs.empty()) {
                    running = false;
#ifdef TN_TASK_WORKERS
                    condition.notify_all();
#endif
                    return;
                }
                job = &jobs.front();
            }
            try {
                job->completion = job->work(job->context);
            } catch (...) {
                job->error = std::current_exception();
            }
            job->context.finish();
            // Release worker captures outside the lock before publishing completion.
            job->work = {};
            {
                std::lock_guard<QueueMutex> lock(mutex);
                // The node already owns result storage; publishing cannot allocate.
                ready.splice(ready.end(), jobs, jobs.begin());
            }
            if (wake) try {
                    wake();
                } catch (...) {
                }
        }
    }

    void cancel()
    {
        // The running job remains in this list until its result is published.
        for (auto& job : jobs)
            job.context.cancel();
    }

    void wait()
    {
#ifdef TN_TASK_WORKERS
        std::unique_lock<QueueMutex> lock(mutex);
        condition.wait(lock, [&] { return !running; });
#endif
    }
};

TaskQueue::TaskQueue(std::function<void()> wake, TaskScheduler& scheduler) :
    impl(std::make_shared<Impl>(scheduler, std::move(wake))) {}

TaskQueue::~TaskQueue()
{
    shutdown();
}

void TaskQueue::submit(Work work)
{
    if (!work) throw std::invalid_argument("empty queued task");
    auto current = impl;
    std::unique_lock<QueueMutex> lock(current->mutex);
    if (current->closed) throw std::logic_error("task queue is closed");
    current->jobs.emplace_back(std::move(work));
    if (current->running) return;
    current->running = true;
#ifndef TN_TASK_WORKERS
    // Inline execution re-enters this queue; threaded admission stays atomic.
    lock.unlock();
#endif
    try {
        current->scheduler.submit([current] { current->run(); });
    } catch (...) {
#ifndef TN_TASK_WORKERS
        lock.lock();
#endif
        std::list<Impl::Job> rejected;
        rejected.splice(rejected.end(), current->jobs);
        current->running = false;
        lock.unlock();
        throw;
    }
}

size_t TaskQueue::poll()
{
    auto current = impl;
#ifdef TN_TASK_WORKERS
    if (std::this_thread::get_id() != current->owner) throw std::logic_error("TaskQueue::poll must run on its creating thread");
#endif
    if (current->polling) return 0;
    struct Guard
    {
        bool& value;
        ~Guard() { value = false; }
    } guard{current->polling};
    current->polling = true;
    size_t count;
    {
        std::lock_guard<QueueMutex> lock(current->mutex);
        count = current->ready.size();
    }
    size_t delivered = 0;
    for (; delivered < count; ++delivered) {
        std::list<Impl::Job> result;
        {
            std::lock_guard<QueueMutex> lock(current->mutex);
            if (current->closed) break;
            result.splice(result.end(), current->ready, current->ready.begin());
        }
        auto& job = result.front();
        if (job.error) std::rethrow_exception(job.error);
        if (job.completion) job.completion();
    }
    return delivered;
}

void TaskQueue::cancel()
{
    std::lock_guard<QueueMutex> lock(impl->mutex);
    impl->cancel();
}

void TaskQueue::wait()
{
    impl->wait();
}

void TaskQueue::shutdown()
{
    auto current = impl;
    {
        std::lock_guard<QueueMutex> lock(current->mutex);
        current->closed = true;
        current->cancel();
    }
    current->wait();
    std::list<Impl::Job> discarded;
    std::function<void()> wake;
    {
        std::lock_guard<QueueMutex> lock(current->mutex);
        discarded.splice(discarded.end(), current->ready);
        // Swapping ensures captures are released outside the lock.
        wake.swap(current->wake);
    }
}
}  // namespace task

#undef TN_TASK_WORKERS
