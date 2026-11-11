#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

/**
 * @brief Text-file storage, directory discovery, and draft recovery.
 *
 * Text is exchanged as UTF-8 with LF line endings; FileFormat retains the external file format.
 * load() and save() block the caller, FileService runs the same operations in the background,
 * and RecoveryStore keeps drafts apart from their original files.
 */
namespace storage
{

/**
 * @brief Native filesystem path; construct one from UTF-8 through char8_t text, such as Path(u8"notes/todo.md").
 */
using Path = std::filesystem::path;

/**
 * @brief Line endings detected on load or selected for output.
 */
enum class LineEnding
{
    Lf,    ///< LF.
    Crlf,  ///< CRLF.
    Cr,    ///< CR.
    Mixed  ///< Multiple conventions; select a uniform convention before save().
};

/**
 * @brief File-format metadata retained separately from normalized text.
 */
struct FileFormat
{
    /**
     * @brief Line-ending convention written in place of each LF.
     */
    LineEnding ending = LineEnding::Lf;

    /**
     * @brief Whether the file starts with a UTF-8 byte-order mark.
     */
    bool bom = false;
};

/**
 * @brief Portable failure categories.
 */
enum class ErrorCode
{
    None,                 ///< No error.
    Cancelled,            ///< Cancellation accepted before completion or commit.
    InvalidPath,          ///< Invalid path or recovery identifier, or not a regular file.
    NotFound,             ///< Required file or directory is missing.
    ReadOnly,             ///< Writing is not permitted.
    NotText,              ///< Input is not UTF-8 text: invalid sequence, NUL byte, or UTF-16/32 BOM.
    InvalidText,          ///< Output text or format is not savable, such as embedded CR or mixed line endings.
    TooLarge,             ///< Size limit exceeded or memory allocation failed.
    Conflict,             ///< File identity, content, or destination differs from the baseline.
    UnsupportedMetadata,  ///< Required filesystem metadata cannot be preserved.
    Io,                   ///< I/O or worker failure; inspect Error::system.
    InvalidRecovery       ///< Damaged, inconsistent, or unsupported recovery record.
};

/**
 * @brief Failure details for blocking operations and asynchronous completions.
 *
 * @note An error converts to true when an operation failed.
 *
 * @code
 *     auto loaded = storage::load("note.txt");
 *     if (loaded.error.code == storage::ErrorCode::NotFound) {
 *         // Branch on code; show message to the user.
 *     }
 * @endcode
 */
struct Error
{
    /**
     * @brief Failure category for program logic; ErrorCode::None on success.
     */
    ErrorCode code = ErrorCode::None;

    /**
     * @brief Underlying OS error, when available.
     */
    std::error_code system;

    /**
     * @brief Diagnostic text in English; not intended for program logic.
     */
    std::string message;

    /**
     * @brief Returns whether this value describes a failure.
     *
     * @return True if code differs from ErrorCode::None; otherwise false.
     */
    explicit operator bool() const { return code != ErrorCode::None; }
};

/**
 * @brief Owned operation output paired with its error.
 *
 * @code
 *     auto loaded = storage::load("note.txt");
 *     if (loaded) {
 *         // loaded.value holds the text, format, and baseline.
 *     } else {
 *         // loaded.error describes the failure; loaded.value is empty.
 *     }
 * @endcode
 */
template<typename T>
struct Result
{
    /**
     * @brief Operation output; default-constructed when the operation failed.
     */
    T value{};

    /**
     * @brief Failure details; empty on success.
     */
    Error error;

    /**
     * @brief Returns whether the operation succeeded.
     *
     * @return True if error is empty; otherwise false.
     */
    explicit operator bool() const { return !error; }
};

namespace detail
{
struct StampData;
struct Access;
}  // namespace detail

/**
 * @brief Copyable, opaque baseline of a file's disk state for conflict detection.
 *
 * Existing-file baselines retain file identity and a digest of raw bytes.
 * Missing-file baselines retain the resolved destination. Copies share immutable state.
 *
 * A file conflicts with its baseline when it was modified, replaced by another file
 * with the same content, removed, or created where none was expected. Changes limited
 * to metadata such as timestamps do not conflict.
 *
 * @note A default stamp requests create-only saving: the destination must not exist.
 * @note Files are not watched. External changes surface only on the next load() or save().
 *
 * @see load(), save(), SaveOptions::expected
 */
class DiskStamp
{
public:
    /**
     * @brief Returns whether the baseline represents an existing file.
     *
     * @return True for a loaded or saved file; false for a default stamp or a missing-file baseline.
     */
    bool exists() const;

    /**
     * @brief Returns the SHA-256 digest of the raw file bytes.
     *
     * @return A 32-byte digest, or zero-filled bytes when no file was read.
     *
     * @note An existing empty file has the digest of an empty stream.
     */
    std::array<uint8_t, 32> digest() const;

private:
    std::shared_ptr<const detail::StampData> data;
    friend struct detail::Access;
};

/**
 * @brief Normalized text and its external file format.
 */
struct Text
{
    /**
     * @brief LF-normalized UTF-8 without NUL bytes or a file BOM; preserves the final newline.
     */
    std::string text;

    /**
     * @brief Line endings and BOM of the external file.
     */
    FileFormat format;
};

/**
 * @brief Loaded text with its requested path and conflict baseline.
 */
struct LoadedFile : Text
{
    /**
     * @brief Absolute requested path, retaining a symbolic-link alias.
     */
    Path path;

    /**
     * @brief Baseline of the resolved target and its raw bytes.
     */
    DiskStamp stamp;
};

/**
 * @brief Missing-file policy and byte limit for load().
 */
struct LoadOptions
{
    /**
     * @brief Allow a missing file, returning empty text and a baseline without creating it.
     */
    bool createIfMissing = false;

    /**
     * @brief Maximum raw and normalized byte counts; absent means unlimited.
     */
    std::optional<size_t> maxBytes = {};
};

/**
 * @brief Output format, expected disk state, and byte limit for save().
 */
struct SaveOptions
{
    /**
     * @brief Line endings and BOM to write; LineEnding::Mixed is rejected.
     */
    FileFormat format;

    /**
     * @brief Baseline from load() or the previous save; default means create-only.
     */
    DiskStamp expected;

    /**
     * @brief Maximum encoded bytes, including the BOM and expanded CRLF; absent means unlimited.
     */
    std::optional<size_t> maxBytes = {};
};

/**
 * @brief Whether a save installed bytes at the destination.
 */
enum class SaveState
{
    NotWritten,                 ///< Nothing was installed; the destination is untouched.
    Committed,                  ///< Output was installed and synchronized.
    CommittedDurabilityUnknown  ///< Output was installed, but a later persistence step failed.
};

/**
 * @brief Save outcome. Commit state and error must be considered together.
 *
 * @code
 *     auto saved = storage::save("note.txt", "text\n", options);
 *     if (saved) {
 *         options.expected = saved.stamp;  // Baseline for the next save.
 *     } else if (saved.state == storage::SaveState::NotWritten) {
 *         // The file on disk is unchanged; saved.error explains why.
 *     } else {
 *         // The new bytes are in place but may not survive a crash; do not retry blindly.
 *     }
 * @endcode
 */
struct SaveResult
{
    /**
     * @brief Whether the destination now holds the saved bytes.
     */
    SaveState state = SaveState::NotWritten;

    /**
     * @brief Baseline of the installed bytes; empty only for SaveState::NotWritten.
     */
    DiskStamp stamp;

    /**
     * @brief Failure details; empty for SaveState::Committed.
     */
    Error error;

    /**
     * @brief Returns whether the save completed.
     *
     * @return True only for SaveState::Committed with no error.
     *
     * @warning False does not mean the original file is unchanged. Inspect state.
     */
    explicit operator bool() const { return state == SaveState::Committed && !error; }
};

/**
 * @brief Loads a regular UTF-8 file and captures its disk baseline.
 *
 * Records the BOM and line-ending convention, normalizes CRLF and CR to LF, and preserves the final newline.
 *
 * @param[in] path File path; relative paths use the working directory.
 * @param[in] options Missing-file policy and optional byte limit.
 *
 * @return Owned text, format, absolute requested path, and baseline on success.
 *
 * @retval ErrorCode::NotFound If the file is missing and not permitted to be, or the path is a dangling symbolic link.
 * @retval ErrorCode::InvalidPath If the path is empty or does not name a regular file.
 * @retval ErrorCode::NotText If the content is invalid UTF-8, holds a NUL byte, or starts with a UTF-16/32 BOM.
 * @retval ErrorCode::TooLarge If the file exceeds LoadOptions::maxBytes.
 * @retval ErrorCode::Conflict If the file changed while it was being read; load again.
 * @retval ErrorCode::Io If the file cannot be opened or read.
 *
 * @note Symbolic links resolve to their targets. Mixed line endings are reported in FileFormat.
 *
 * @code
 *     // "a\r\nb" on disk.
 *     auto loaded = storage::load("note.txt");
 *     assert(loaded);
 *     assert(loaded.value.text == "a\nb");
 *     assert(loaded.value.format.ending == storage::LineEnding::Crlf);
 *     assert(loaded.value.stamp.exists());
 *
 *     // A missing file yields empty text and a baseline that permits creating it.
 *     auto created = storage::load("new.txt", {true});
 *     assert(created && !created.value.stamp.exists());
 * @endcode
 *
 * @see save(), FileService::open()
 */
Result<LoadedFile> load(const Path& path, LoadOptions options = {});

/**
 * @brief Saves normalized text through a staged, baseline-checked replacement.
 *
 * The text is written to a sibling staging file that replaces the destination only if
 * the destination still matches the baseline. Supported metadata is preserved.
 *
 * @param[in] path Destination whose parent directory exists.
 * @param[in] lfText LF-normalized UTF-8 without NUL bytes or CR; borrowed for this call.
 * @param[in] options Output format, expected baseline, and optional encoded byte limit.
 *
 * @return Commit state, the baseline for the next save, and any error.
 *
 * @retval ErrorCode::Conflict If the destination differs from the baseline; the external change is left untouched.
 * @retval ErrorCode::InvalidText If the text holds CR, NUL bytes, or invalid UTF-8, or the format is LineEnding::Mixed.
 * @retval ErrorCode::TooLarge If the encoded output exceeds SaveOptions::maxBytes.
 * @retval ErrorCode::ReadOnly If the destination is not writable.
 * @retval ErrorCode::UnsupportedMetadata If the file has hard links, or ownership or attributes that cannot be preserved.
 * @retval ErrorCode::NotFound If the parent directory is missing or the path is a dangling symbolic link.
 * @retval ErrorCode::Io If staging, writing, or replacing fails.
 *
 * @note Files are not locked. A change arriving between verification and replacement is overwritten.
 *
 * @warning Errors after replacement report SaveState::CommittedDurabilityUnknown. Inspect state before retrying.
 *
 * @code
 *     auto loaded = storage::load("note.txt");
 *     storage::SaveOptions options{loaded.value.format, loaded.value.stamp};
 *
 *     auto saved = storage::save("note.txt", "edited\n", options);
 *     if (saved.error.code == storage::ErrorCode::Conflict) {
 *         // Changed outside: load the current state, then save with its baseline to overwrite.
 *         auto current = storage::load("note.txt");
 *         options.expected = current.value.stamp;
 *         saved = storage::save("note.txt", "edited\n", options);
 *     }
 *     if (saved) options.expected = saved.stamp;
 *
 *     // A default baseline creates a file and never overwrites one.
 *     auto created = storage::save("new.txt", "text\n");
 * @endcode
 *
 * @see load(), SaveResult, FileService::save()
 */
SaveResult save(const Path& path, std::string_view lfText, SaveOptions options = {});

/**
 * @brief Discovered regular file and optional creation time.
 */
struct DirectoryEntry
{
    /**
     * @brief Absolute path, retaining any file symlink encountered during traversal.
     */
    Path path;

    /**
     * @brief UTC milliseconds since Unix epoch; absent when birth time is unavailable.
     */
    std::optional<int64_t> createdAt;
};

/**
 * @brief Content filtering for directory discovery.
 */
struct ListOptions
{
    /**
     * @brief Keep only files whose complete content is supported UTF-8; names and extensions are not filters.
     */
    bool textOnly = false;
};

/**
 * @brief Draft text and restoration metadata, stored independently of the original file.
 *
 * @note RecoveryStore locates records by id; originalPath is display metadata only.
 */
struct RecoverySnapshot : Text
{
    /**
     * @brief Stable draft ID of 32 lowercase hexadecimal characters; empty until the first save.
     */
    std::string id;

    /**
     * @brief UTF-8 display title; never used as a filename.
     */
    std::string title;

    /**
     * @brief Original absolute path, or empty for an untitled draft.
     */
    Path originalPath;

    /**
     * @brief Caller-supplied document revision retained in the record.
     */
    uint64_t revision = 0;

    /**
     * @brief UTC checkpoint time in milliseconds since Unix epoch; assigned during save and returned on load.
     */
    int64_t savedAtUnixMilliseconds = 0;
};

/**
 * @brief Draft checkpoints kept in one private directory.
 *
 * The store remembers the baseline of each record it loaded or saved, and only
 * replaces or removes records that still match it.
 *
 * @note Operations block the caller. Use one store on one thread at a time.
 *
 * @code
 *     storage::RecoveryStore store(stateDirectory / "recovery");  // Absolute path.
 *
 *     storage::RecoverySnapshot draft;
 *     draft.title = "Untitled";
 *     draft.text = "unsaved\n";
 *     store.save(draft);  // Assigns draft.id on the first checkpoint.
 *
 *     draft.text = "unsaved, edited\n";
 *     store.save(draft);  // Replaces the same record.
 *
 *     // After a restart, the id retained by the application restores the draft.
 *     auto restored = store.load(draft.id);
 *     assert(restored.value.text == "unsaved, edited\n");
 *
 *     store.remove(draft.id);  // Once the document is saved or discarded.
 * @endcode
 */
class RecoveryStore
{
public:
    /**
     * @brief Binds a store to a recovery directory without touching the disk.
     *
     * @param[in] directory Absolute recovery directory; must not be a symbolic link.
     * @param[in] maxBytes Optional draft-text byte limit; serialized metadata has a separate 64 KiB limit.
     */
    explicit RecoveryStore(Path directory, std::optional<size_t> maxBytes = {});

    /**
     * @brief Writes a draft checkpoint without touching its original file.
     *
     * @param[in,out] snapshot Draft and metadata; an empty id receives a new one to retain across checkpoints.
     *
     * @return Commit state and any error, as for storage::save().
     *
     * @retval ErrorCode::Conflict If the record was created or changed outside this store.
     * @retval ErrorCode::InvalidPath If the directory, id, or original path is invalid.
     * @retval ErrorCode::InvalidText If the text or title holds CR, NUL bytes, or invalid UTF-8.
     * @retval ErrorCode::TooLarge If the text or metadata exceeds its limit.
     *
     * @note Creates a missing directory with private permissions.
     * @note Mixed line-ending metadata is preserved; draft text must still be LF-normalized UTF-8.
     */
    SaveResult save(RecoverySnapshot& snapshot);

    /**
     * @brief Restores a draft after verifying its ID, format, and integrity digest.
     *
     * @param[in] id Draft ID assigned by save().
     *
     * @return Draft text, metadata, and checkpoint time on success.
     *
     * @retval ErrorCode::NotFound If the directory or record is missing.
     * @retval ErrorCode::InvalidRecovery If the record is damaged, inconsistent, or of an unsupported version.
     * @retval ErrorCode::InvalidPath If the directory or id is invalid.
     * @retval ErrorCode::TooLarge If the text exceeds the store's limit.
     */
    Result<RecoverySnapshot> load(std::string_view id);

    /**
     * @brief Removes a record that this store loaded or saved and that still matches.
     *
     * @param[in] id Draft ID identifying the record.
     *
     * @return Empty error if the record was removed or is already missing.
     *
     * @retval ErrorCode::Conflict If the record is unknown to this store or changed outside it.
     * @retval ErrorCode::InvalidPath If the id is invalid.
     */
    Error remove(std::string_view id);

private:
    Path directory;
    std::optional<size_t> maxBytes;
    std::unordered_map<std::string, DiskStamp> stamps;
};

/**
 * @brief Receives an owned open result during FileService::poll().
 */
using OpenCallback = std::function<void(Result<LoadedFile>)>;

/**
 * @brief Receives an owned save result during FileService::poll().
 */
using SaveCallback = std::function<void(SaveResult)>;

/**
 * @brief Receives an owned directory listing during FileService::poll().
 */
using DirectoryCallback = std::function<void(Result<std::vector<DirectoryEntry>>)>;

/**
 * @brief Serial file operations with host-controlled completion delivery.
 *
 * Requests own their arguments and run in submission order. Completions are delivered
 * only by poll(), on the thread that created the service.
 *
 * @note With task workers enabled, submission and cancel() are thread-safe and work runs in the background.
 * Without workers, use one thread; work runs during submission.
 * @note Construct, poll, and destroy on the same host thread.
 *
 * @code
 *     // The wake callback runs on a worker; it only asks the host loop to poll.
 *     storage::FileService files([&] { host.requestPoll(); });
 *
 *     files.open("note.txt", {}, [&](storage::Result<storage::LoadedFile> loaded) {
 *         if (!loaded) return;
 *         // Runs inside poll(); a callback may submit further requests.
 *         files.save(loaded.value.path, "edited\n", {loaded.value.format, loaded.value.stamp}, {});
 *     });
 *
 *     // Host event loop, when woken:
 *     files.poll();
 * @endcode
 */
class FileService
{
public:
    /**
     * @brief Creates a service owned by the calling thread.
     *
     * @param[in] wakeHost Optional completion signal, called from a worker when enabled.
     *
     * @note The signal must only notify the host; never poll, destroy, or wait on this service inside it.
     */
    explicit FileService(std::function<void()> wakeHost = {});

    /**
     * @brief Cancels outstanding work, waits for it, and discards undelivered completions.
     *
     * @note Destruction inside a completion callback is supported; later callbacks are discarded.
     */
    ~FileService();
    FileService(const FileService&) = delete;
    FileService& operator=(const FileService&) = delete;

    /**
     * @brief Queues a load and takes ownership of its arguments.
     *
     * @param[in] path File path, preferably absolute to avoid working-directory changes.
     * @param[in] options Missing-file policy and optional byte limit.
     * @param[in] done Completion callback receiving the result of load(); empty discards it.
     *
     * @see load()
     */
    void open(Path path, LoadOptions options, OpenCallback done);

    /**
     * @brief Queues a save using an owned text snapshot and disk baseline.
     *
     * @param[in] path Destination, preferably absolute.
     * @param[in] lfText Owned LF-normalized UTF-8 snapshot, independent of later caller edits.
     * @param[in] options Output format, baseline, and optional encoded byte limit.
     * @param[in] done Completion callback receiving the result of save(); empty discards it.
     *
     * @note Cancellation is rejected once commit starts.
     *
     * @see save()
     */
    void save(Path path, std::string lfText, SaveOptions options, SaveCallback done);

    /**
     * @brief Queues a recursive listing of regular files without following directory symlinks.
     *
     * @param[in] directory Root directory, preferably absolute.
     * @param[in] done Completion callback receiving entries in unspecified traversal order.
     * @param[in] options Optional full-content text validation, reusing results between scans.
     *
     * @note Includes hidden files and regular-file symlinks; unreadable descendants are skipped.
     * @note The callback receives ErrorCode::NotFound for a missing root and ErrorCode::InvalidPath for an empty path.
     */
    void list(Path directory, DirectoryCallback done, ListOptions options = {});

    /**
     * @brief Requests cooperative cancellation of running and queued work.
     *
     * @note Completed work and saves that entered commit are not undone.
     * Cancelled requests deliver ErrorCode::Cancelled through poll(). New submissions remain accepted.
     */
    void cancel();

    /**
     * @brief Delivers available completions on the creating thread.
     *
     * @return Number of completions delivered; zero for a recursive call.
     *
     * @throws std::logic_error If called from another thread.
     *
     * @note Callbacks may submit requests. Results arriving during this call wait for a later poll().
     * @note Callback exceptions propagate; remaining completions stay queued.
     */
    size_t poll();

private:
    struct Impl;
    std::shared_ptr<Impl> impl;
};

}  // namespace storage
