#include <chrono>
#include <storage/internal.h>
#include "common/files.h"

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

using namespace storage;

namespace
{
RecoverySnapshot draft(std::string text = "한글\nunsaved")
{
    RecoverySnapshot snapshot;
    snapshot.title = "Untitled document";
    snapshot.text = std::move(text);
    snapshot.revision = 17;
    return snapshot;
}

Path record(const Path& directory, const RecoverySnapshot& snapshot)
{
    return directory / (snapshot.id + ".recovery");
}
}  // namespace

TEST_SUITE_BEGIN("recovery");

TEST_CASE("private_directories")
{
    test::Directory directory;
    auto root = directory.file("app-state/recovery");
    RecoveryStore store(root);
    auto snapshot = draft();
    snapshot.title = "../title is never a filename";
    REQUIRE(store.save(snapshot));
    CHECK_EQ(snapshot.id.size(), 32);
    CHECK(std::filesystem::exists(record(root, snapshot)));
    CHECK_EQ(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}), 1);
#if !defined(_WIN32)
    struct stat info = {};
    REQUIRE_EQ(stat(root.c_str(), &info), 0);
    CHECK_EQ(info.st_mode & 0777, 0700);
    REQUIRE_EQ(stat(record(root, snapshot).c_str(), &info), 0);
    CHECK_EQ(info.st_mode & 0777, 0600);
#endif
}

TEST_CASE("untitled_restore")
{
    test::Directory directory;
    RecoveryStore store(directory.path);
    auto snapshot = draft("\xef\xbb\xbf"
                          "First line\nLast line");
    snapshot.title = "Untitled 🙂\nmetadata";
    snapshot.format = {LineEnding::Mixed, true};
    REQUIRE(store.save(snapshot));

    auto restored = RecoveryStore(directory.path).load(snapshot.id);
    REQUIRE(restored);
    CHECK_EQ(restored.value.id, snapshot.id);
    CHECK_EQ(restored.value.title, snapshot.title);
    CHECK_EQ(restored.value.text, snapshot.text);
    CHECK_EQ(restored.value.revision, snapshot.revision);
    CHECK(restored.value.originalPath.empty());
    CHECK(restored.value.format.bom);
    CHECK(restored.value.format.ending == LineEnding::Mixed);
    CHECK(restored.value.savedAtUnixMilliseconds > 0);
}

TEST_CASE("empty_text")
{
    test::Directory directory;
    RecoveryStore store(directory.path, 0);
    const FileFormat formats[] = {{LineEnding::Lf, false}, {LineEnding::Crlf, true}, {LineEnding::Cr, false}, {LineEnding::Mixed, true}};
    for (const auto& format : formats) {
        CAPTURE(format.ending);
        auto snapshot = draft("");
        snapshot.format = format;
        snapshot.originalPath = directory.path / std::filesystem::path(u8"Original 🙂.txt");
        REQUIRE(store.save(snapshot));

        auto restored = store.load(snapshot.id);
        REQUIRE(restored);
        CHECK(restored.value.text.empty());
        CHECK(restored.value.originalPath == snapshot.originalPath);
        CHECK(restored.value.format.ending == format.ending);
        CHECK_EQ(restored.value.format.bom, format.bom);
        CHECK_FALSE(std::filesystem::exists(snapshot.originalPath));
    }
}

TEST_CASE("checkpoints")
{
    test::Directory directory;
    RecoveryStore store(directory.path);
    auto snapshot = draft("one");
    REQUIRE(store.save(snapshot));
    auto id = snapshot.id;

    snapshot.text = "two and more";
    ++snapshot.revision;
    REQUIRE(store.save(snapshot));
    CHECK_EQ(snapshot.id, id);
    CHECK_EQ(directory.entries(), 1);
    auto restored = store.load(id);
    REQUIRE(restored);
    CHECK_EQ(restored.value.text, "two and more");

    REQUIRE(!store.remove(id));
    CHECK_FALSE(std::filesystem::exists(record(directory.path, snapshot)));
    CHECK(store.remove(id).code == ErrorCode::Conflict);
    CHECK(store.load(id).error.code == ErrorCode::NotFound);
}

TEST_CASE("stale_baseline")
{
    test::Directory directory;
    RecoveryStore store(directory.path), other(directory.path);
    auto snapshot = draft("one");
    REQUIRE(store.save(snapshot));

    SUBCASE("unknown_record")
    {
        CHECK(other.save(snapshot).error.code == ErrorCode::Conflict);
        CHECK(other.remove(snapshot.id).code == ErrorCode::Conflict);
    }
    SUBCASE("replaced_record")
    {
        REQUIRE(other.load(snapshot.id));
        snapshot.text = "two and more";
        REQUIRE(store.save(snapshot));
        CHECK(other.save(snapshot).error.code == ErrorCode::Conflict);
        CHECK(other.remove(snapshot.id).code == ErrorCode::Conflict);
    }
    SUBCASE("removed_record")
    {
        REQUIRE(other.load(snapshot.id));
        REQUIRE(!store.remove(snapshot.id));
        CHECK(!other.remove(snapshot.id));
        return;
    }
    auto restored = store.load(snapshot.id);
    REQUIRE(restored);
    CHECK_EQ(restored.value.text, snapshot.text);
}

TEST_CASE("postcommit_failure")
{
    test::Directory directory;
    RecoveryStore store(directory.path);
    auto snapshot = draft("one");
    detail::Hooks hooks;
    hooks.before = [](detail::Stage stage) -> std::error_code {
        if (stage == detail::Stage::DirectorySync) throw std::runtime_error("completion failure");
        return {};
    };
    detail::hooks = &hooks;
    auto failed = store.save(snapshot);
    detail::hooks = nullptr;
    CHECK(failed.state == SaveState::CommittedDurabilityUnknown);

    // The store must keep tracking the record it installed.
    snapshot.text = "two";
    REQUIRE(store.save(snapshot));
    CHECK(!store.remove(snapshot.id));
}

TEST_CASE("corrupt_record")
{
    test::Directory directory;
    RecoveryStore store(directory.path);
    auto snapshot = draft("recover me"), other = draft("other");
    REQUIRE(store.save(snapshot));
    REQUIRE(store.save(other));
    auto bytes = test::read(record(directory.path, snapshot));
    auto id = snapshot.id;
    SUBCASE("truncated")
    {
        bytes.pop_back();
    }
    SUBCASE("modified")
    {
        bytes.back() = 'X';
    }
    SUBCASE("unknown_format")
    {
        bytes.insert(0, "unknown-recovery 2\n");
    }
    SUBCASE("wrong_id")
    {
        id = other.id;
    }

    test::write(directory.path / (id + ".recovery"), bytes);
    auto loaded = store.load(id);
    CHECK(loaded.error.code == ErrorCode::InvalidRecovery);
    CHECK(loaded.value.text.empty());
    if (id == snapshot.id) {
        auto intact = store.load(other.id);
        REQUIRE(intact);
        CHECK_EQ(intact.value.text, "other");
    }
}

TEST_CASE("invalid_output")
{
    test::Directory directory;
    auto root = directory.file("recovery");
    auto snapshot = draft("1234");
    SUBCASE("invalid_text")
    {
        RecoveryStore store(root);
        for (const auto& text : {std::string("bad\rtext"), std::string("\xff"), std::string("bad\0text", 8)}) {
            CAPTURE(text);
            snapshot.text = text;
            CHECK(store.save(snapshot).error.code == ErrorCode::InvalidText);
        }
    }
    SUBCASE("text_limit")
    {
        CHECK(RecoveryStore(root, 3).save(snapshot).error.code == ErrorCode::TooLarge);
    }
    SUBCASE("metadata_limit")
    {
        snapshot.title.assign(64 * 1024, 'x');
        CHECK(RecoveryStore(root).save(snapshot).error.code == ErrorCode::TooLarge);
    }
    CHECK_FALSE(std::filesystem::exists(root));
}

TEST_CASE("size_limit")
{
    test::Directory directory;
    auto snapshot = draft("1234");
    RecoveryStore store(directory.path, 4);
    REQUIRE(store.save(snapshot));
    auto loaded = store.load(snapshot.id);
    REQUIRE(loaded);
    CHECK_EQ(loaded.value.text, "1234");
    CHECK(RecoveryStore(directory.path, 3).load(snapshot.id).error.code == ErrorCode::TooLarge);
}

TEST_CASE("invalid_path")
{
    test::Directory directory;
    RecoveryStore store(directory.path);
    auto snapshot = draft();
    SUBCASE("root")
    {
        for (const auto& root : {Path{}, Path("relative/recovery")}) {
            CAPTURE(root);
            RecoveryStore invalid(root);
            CHECK(invalid.save(snapshot).error.code == ErrorCode::InvalidPath);
            CHECK(invalid.load(snapshot.id).error.code == ErrorCode::InvalidPath);
        }
    }
    SUBCASE("id")
    {
        RecoveryStore absent(directory.file("absent"));
        const std::string cases[] = {"../escape", std::string(32, 'A'), std::string(32, 'a') + "/../escape", std::string(31, 'a') + '\0'};
        for (size_t i = 0; i < std::size(cases); ++i) {
            CAPTURE(i);
            snapshot.id = cases[i];
            CHECK(absent.save(snapshot).error.code == ErrorCode::InvalidPath);
            CHECK(store.load(snapshot.id).error.code == ErrorCode::InvalidPath);
            CHECK(store.remove(snapshot.id).code == ErrorCode::InvalidPath);
        }
        CHECK(store.load("").error.code == ErrorCode::InvalidPath);
        CHECK_EQ(directory.entries(), 0);
    }
    SUBCASE("original_path")
    {
        snapshot.originalPath = "relative.txt";
        CHECK(store.save(snapshot).error.code == ErrorCode::InvalidPath);
    }
    SUBCASE("file_root")
    {
        test::write(directory.file(), "not a directory");
        CHECK_FALSE(RecoveryStore(directory.file()).save(snapshot));
    }
    SUBCASE("directory_record")
    {
        snapshot.id = std::string(32, 'a');
        std::filesystem::create_directory(record(directory.path, snapshot));
        CHECK(store.save(snapshot).error.code == ErrorCode::InvalidPath);
        CHECK(store.load(snapshot.id).error.code == ErrorCode::InvalidPath);
    }
}

#if !defined(_WIN32)
TEST_CASE("symlinks")
{
    test::Directory directory;
    auto root = directory.file("recovery");
    std::filesystem::create_directory(root);
    auto snapshot = draft();
    SUBCASE("root")
    {
        auto alias = directory.file("alias");
        std::filesystem::create_directory_symlink(root, alias);
        CHECK_FALSE(RecoveryStore(alias).save(snapshot));
    }
    SUBCASE("record")
    {
        RecoveryStore store(root);
        snapshot.id = std::string(32, 'a');
        auto outside = directory.file("outside");
        test::write(outside, "must survive");
        std::filesystem::create_symlink(outside, record(root, snapshot));
        CHECK_FALSE(store.save(snapshot));
        CHECK_FALSE(store.load(snapshot.id));
        CHECK_EQ(test::read(outside), "must survive");
    }
}
#endif

TEST_CASE("metadata_changes")
{
    test::Directory directory;
    RecoveryStore store(directory.path);
    auto snapshot = draft("indexed");
    REQUIRE(store.save(snapshot));
    auto path = record(directory.path, snapshot);
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) - std::chrono::hours(1));

    CHECK(!store.remove(snapshot.id));
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_SUITE_END();
