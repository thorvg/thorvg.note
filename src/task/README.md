# Task

## Responsibility

- Run independent work on a worker pool through `TaskScheduler`; the shared instance selects two to four workers from the hardware concurrency.
- Run work serially through `TaskQueue` and deliver each completion on the thread that created the queue.
- Track cooperative cancellation and the commit point of one job through `TaskContext`.
- Capture exceptions thrown by queued work and rethrow them from `poll()`; exceptions from direct scheduler work are discarded.
- Expose all public types through [`task.h`](task.h) in namespace `task`.
- Require C++14 and the standard library, plus native threads when workers are enabled; no UI, storage, or document-engine dependency.

## Ownership

- `TaskQueue` owns each job's context, worker callback, and completion until the completion is delivered or discarded.
- The scheduler is borrowed and must outlive every queue that uses it. The shared scheduler is destroyed during normal program shutdown.
- Construct, poll, shut down, and destroy a queue on the same host thread. With workers enabled, `submit()` and `cancel()` are thread-safe.
- `wake` only signals the host; it must not call `poll()` or `shutdown()`. `poll()` delivers the completions that are ready when it begins.
- Keep captured state alive until completions are delivered or discarded. Queue shutdown and destruction cancel outstanding work, wait for it, and discard pending completions.
- Scheduler destruction waits for accepted work; destroy it from outside its own workers.

## Current Limits

- Cancellation is cooperative and never interrupts a thread. Cancelled work still runs and must check its context; its completion is still delivered.
- The queue is the cancellation scope: `cancel()` targets every running and pending job, and there is no per-job handle. Use a separate queue for work that must be cancelled independently.
- A job that has committed or finished cannot be cancelled. `commit()` only changes task state; the work performs the actual irreversible step.
- No priorities, delays, or result types. A scheduled queue drains its pending work before releasing the worker, so fairness between queues is not guaranteed.
- A completion exception propagates from `poll()` and leaves the remaining entries queued for a later call.
- Without workers (`task_threads` disabled), work runs during `submit()` and `wait()` does nothing; completions still require `poll()`.
- WebAssembly enables workers only with compiler and linker pthread flags. Blocking the browser main thread in `wait()` or `shutdown()` can prevent worker startup.
- `wait()` and `shutdown()` must not be called from work running on the same queue.
