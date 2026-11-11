# Storage

## Responsibility

- Load and save regular UTF-8 files, normalizing line endings to LF while retaining the external format and BOM.
- Detect conflicts using file identity and raw-byte digests; write through a sibling staging file before replacement.
- Discover files recursively with optional UTF-8 validation, and keep draft recovery records separate from original files.
- Expose all public types through [`storage.h`](storage.h) in namespace `storage`.
- Require C++20, native filesystem APIs, and the [`task`](../task/task.h) module; no UI or document-engine dependency.

## Ownership

- `load()` returns owned text and metadata. Blocking `save()` borrows its text only for the duration of the call.
- Retain the baseline from a load or committed save in `SaveOptions::expected`; a default stamp permits creation only. Copied stamps share immutable state.
- `FileService` owns queued arguments and callbacks. Construct, poll, and destroy it on the same host thread.
- With workers enabled, submission and cancellation are thread-safe. `wakeHost` only signals the host; `poll()` delivers completions on the host thread.
- Keep captured state alive until callbacks are delivered or discarded. Service destruction cancels outstanding work, waits for it, and discards pending completions.
- `RecoveryStore` owns record baselines for one directory; use it on one thread at a time. The host retains recovery IDs and chooses when to checkpoint or remove drafts.

## Current Limits

- Windows is unsupported; Meson stops configuration with an explicit diagnostic.
- UTF-8 only; invalid sequences, NUL bytes, and UTF-16/32 BOMs are rejected. File saves require LF-normalized text and a uniform output line ending.
- Whole-file text snapshots; byte limits are optional. Recovery metadata has a separate 64 KiB limit.
- No file watching or locking. A change between verification and replacement can be overwritten; changes after commit are detected on the next save.
- An error can follow a committed replacement. Inspect `SaveResult::state` and retain its installed-file stamp before deciding whether to retry.
- Hard-linked destinations are unsupported. Saving is refused when required ownership, permissions, or metadata cannot be preserved.
- Without workers, `FileService` runs work during submission; completions still require `poll()`. Recovery operations always block the caller.
- Recovery writes one record at a time. The host coordinates session indexes: write new recovery IDs, commit the index, then remove old records.
