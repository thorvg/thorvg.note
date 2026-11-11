#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>

#define TN_TASK_DECLARE_IMPL \
private:                     \
    struct Impl;             \
    std::shared_ptr<Impl> impl

namespace task
{

/**
 * @brief Thread-safe cancellation state owned by one queued task.
 *
 * @note Cancellation is cooperative and never interrupts a thread. The context cannot be copied or moved.
 * @note TaskQueue owns a context for each submitted job and passes it to the worker callback.
 * TaskQueue::cancel() requests cancellation through those contexts. TaskScheduler executes the queue's worker.
 *
 * @code
 *     task::TaskScheduler scheduler(1);
 *     task::TaskQueue queue({}, scheduler);
 *     bool committed = false;
 *     bool cancelled = false;
 *
 *     queue.submit([&](task::TaskContext& context) {
 *         // The queue supplies this job's context when the scheduler runs it.
 *         bool accepted = false;
 *         if (!context.cancelled()) {
 *             // Prepare output here, then request permission to commit it.
 *             accepted = context.commit();
 *         }
 *         return [&, accepted] {
 *             committed = accepted;
 *             cancelled = !accepted;
 *         };
 *     });
 *
 *     queue.cancel();  // Requests cancellation of running and pending job contexts.
 *     queue.wait();
 *     queue.poll();  // Delivers the result on the creating thread.
 *     assert(committed || cancelled);
 *     // cancelled is false if the worker committed before the cancellation request.
 * @endcode
 *
 * @see TaskQueue::submit(), TaskQueue::cancel(), TaskQueue::poll(), commit()
 */
struct TaskContext
{
    /**
     * @brief Creates a context in the working phase.
     */
    TaskContext();
    TaskContext(const TaskContext&) = delete;
    TaskContext& operator=(const TaskContext&) = delete;

    /**
     * @brief Returns whether cancellation was accepted for this context.
     */
    bool cancelled() const;

    /**
     * @brief Requests cancellation while the task is in the working phase.
     *
     * @return True only for the call that changes the phase to cancelled.
     *
     * @note Returns false after cancellation, commit, or finish.
     */
    bool cancel();

    /**
     * @brief Enters an irreversible phase and rejects subsequent cancellation.
     *
     * @return True only when transitioning from working to committing.
     *
     * @note This method changes task state; it does not perform a file or database commit.
     */
    bool commit();

    /**
     * @brief Marks a non-cancelled task as finished.
     *
     * @return False if cancelled; true if finished by this call or already finished.
     *
     * @note This state does not indicate that a queued completion callback has run.
     */
    bool finish();

private:
    enum class Phase
    {
        Working,
        Cancelled,
        Committing,
        Finished
    };
    std::atomic<Phase> phase{Phase::Working};
};

/**
 * @brief Executes independent tasks on a worker thread pool.
 *
 * @note Task submission is thread-safe. Work may execute concurrently and completion order is unspecified.
 * @note Without thread support, work runs synchronously on the submitting thread.
 * @note WebAssembly enables workers only when compiled with pthread support.
 *
 * @code
 *     bool finished = false;
 *     {
 *         task::TaskScheduler scheduler(1);
 *         scheduler.submit([&] { finished = true; });
 *     }
 *     assert(finished);  // Destruction waits for accepted work.
 * @endcode
 */
struct TaskScheduler
{
    /**
     * @brief Independent worker callback.
     */
    using Work = std::function<void()>;

    /**
     * @brief Creates a scheduler and starts its workers.
     *
     * @param[in] workers Worker count, or zero to select the hardware concurrency clamped to 2 through 4. Ignored without thread support.
     *
     * @note On worker creation failure, started workers are stopped before the exception is propagated.
     */
    explicit TaskScheduler(size_t workers = 0);

    /**
     * @brief Waits for workers to drain accepted tasks.
     *
     * @note Destruction must not race with other API calls.
     * @note Destroy from outside the worker pool to join every worker; a worker destroying its own scheduler is detached.
     */
    ~TaskScheduler();
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    /**
     * @brief Schedules work without waiting for completion.
     *
     * @param[in] work Nonempty worker callback. Accepted work runs before the scheduler stops.
     *
     * @throws std::invalid_argument If the callback is empty.
     * @throws std::logic_error If the scheduler is stopping.
     *
     * @note Exceptions thrown by the callback are caught and discarded. Use TaskQueue to deliver failures through poll().
     */
    void submit(Work);

    /**
     * @brief Returns the shared scheduler, initialized on first use.
     *
     * @note The shared instance is destroyed during normal program shutdown; callers must not delete it.
     */
    static TaskScheduler& shared();

    TN_TASK_DECLARE_IMPL;
};

/**
 * @brief Executes work serially on a scheduler and delivers completions on its creating thread.
 *
 * @note With thread support, different queues may execute concurrently and submit()/cancel() are thread-safe.
 * @note Without thread support, use on one thread; work runs during submit(), while completions still require poll().
 * @note poll(), shutdown(), and destruction belong to the creating thread. The scheduler must outlive the queue.
 * @note A scheduled queue drains its pending work before releasing the worker; fairness between queues is not guaranteed.
 *
 * @code
 *     task::TaskScheduler scheduler(1);
 *     task::TaskQueue queue({}, scheduler);
 *     bool completed = false;
 *
 *     queue.submit([&](task::TaskContext&) {
 *         return [&] { completed = true; };
 *     });
 *     queue.wait();
 *     assert(!completed);
 *     queue.poll();
 *     assert(completed);
 * @endcode
 */
struct TaskQueue
{
    /**
     * @brief Callback invoked by poll() on the creating thread; an empty callback is allowed.
     */
    using Completion = std::function<void()>;

    /**
     * @brief Worker callback returning a completion for delivery on the creating thread.
     *
     * @note A thrown exception is captured and rethrown during poll(). Cancelled work still runs and must check its context.
     */
    using Work = std::function<Completion(TaskContext&)>;

    /**
     * @brief Creates a serial queue using the shared or supplied scheduler.
     *
     * @param[in] wake Optional notification when a result is queued; it must only signal the host. Runs on the submitting thread without workers.
     * @param[in] scheduler Scheduler used to execute work; ownership is not transferred.
     *
     * @note The wake callback must not call poll() or shutdown() directly. Exceptions from wake are discarded.
     */
    explicit TaskQueue(std::function<void()> wake = {}, TaskScheduler& scheduler = TaskScheduler::shared());

    /**
     * @brief Shuts down the queue and discards undelivered completions.
     *
     * @see shutdown()
     */
    ~TaskQueue();
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    /**
     * @brief Appends work to this queue in submission order.
     *
     * @param[in] work Nonempty worker callback producing an optional completion.
     *
     * @throws std::invalid_argument If the callback is empty.
     * @throws std::logic_error If the queue is closed.
     *
     * @note Submission may also propagate allocation or scheduler errors.
     */
    void submit(Work);

    /**
     * @brief Delivers results available when this call begins, without waiting for new work.
     *
     * @return Number of result entries consumed, including entries without a callback. A recursive call returns zero.
     *
     * @throws std::logic_error If called from another thread.
     *
     * @note Completion exceptions propagate to the caller; remaining entries stay queued.
     * @note New results queued during this call are left for a later poll().
     */
    size_t poll();

    /**
     * @brief Requests cancellation of currently running and queued work.
     *
     * @note Work is not removed, completions are still delivered, and new submissions remain accepted.
     * @note A task that has committed or finished cannot be cancelled.
     */
    void cancel();

    /**
     * @brief Blocks until this queue has no running or pending worker work.
     *
     * @note Does not execute completion callbacks or close the queue. Without workers, this method does nothing. Do not call from work running on this queue.
     * @note In a browser, blocking the main thread can prevent worker startup; use a worker host or poll asynchronously.
     */
    void wait();

    /**
     * @brief Closes the queue, requests cancellation, waits for work, and discards undelivered completions.
     *
     * @note Further submissions are rejected. The scheduler remains available to other queues.
     * @note Call on the creating thread, never from this queue's work or wake callback.
     * @note This method blocks and has the same browser main-thread constraints as wait().
     */
    void shutdown();

    TN_TASK_DECLARE_IMPL;
};
}  // namespace task

#undef TN_TASK_DECLARE_IMPL
