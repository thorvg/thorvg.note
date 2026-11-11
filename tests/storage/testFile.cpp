#include <storage/internal.h>
#include "common/files.h"

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(__APPLE__) || defined(__linux__)
#include <sys/xattr.h>
#endif

using namespace storage;
using detail::Stage;

namespace
{
// Installs fault injection for saves on the current thread.
struct Faults : detail::Hooks
{
    Faults() { detail::hooks = this; }
    ~Faults() { detail::hooks = nullptr; }
};

Error check(const Path& path, const DiskStamp& expected)
{
    return detail::check(path, expected, nullptr);
}

struct ExistingFile
{
    test::Directory directory;
    Path path = directory.file();
    SaveOptions options;

    ExistingFile()
    {
        test::write(path, "original");
        auto loaded = load(path);
        if (!loaded) throw std::runtime_error("cannot load file fixture");
        options = test::options(loaded.value);
    }
};

#if defined(__APPLE__) || defined(__linux__)
#if defined(__APPLE__)
constexpr char attributeName[] = "com.thornote.storage-test";
#else
constexpr char attributeName[] = "user.thornote.storage-test";
#endif

void setAttribute(const Path& path, std::string_view value)
{
#if defined(__APPLE__)
    REQUIRE_EQ(setxattr(path.c_str(), attributeName, value.data(), value.size(), 0, 0), 0);
#else
    REQUIRE_EQ(setxattr(path.c_str(), attributeName, value.data(), value.size(), 0), 0);
#endif
}

ssize_t getAttribute(const Path& path, void* value, size_t size)
{
#if defined(__APPLE__)
    return getxattr(path.c_str(), attributeName, value, size, 0, 0);
#else
    return getxattr(path.c_str(), attributeName, value, size);
#endif
}
#endif
}  // namespace

TEST_SUITE_BEGIN("file");

TEST_CASE("round_trip")
{
    test::Directory directory;
    const std::pair<const char*, const char*> cases[] = {
        {"", ""},
        {"\xef\xbb\xbf한글\r\nno-final-newline", "한글\nno-final-newline"},
    };
    for (const auto& entry : cases) {
        CAPTURE(entry.first);
        test::write(directory.file(), entry.first);
        auto loaded = load(directory.file());
        REQUIRE(loaded);
        CHECK_EQ(loaded.value.text, entry.second);
        CHECK(loaded.value.stamp.exists());
        CHECK(!check(directory.file(), loaded.value.stamp));

        auto saved = save(directory.file(), loaded.value.text, test::options(loaded.value));
        REQUIRE(saved);
        CHECK_EQ(test::read(directory.file()), entry.first);
        CHECK(!check(directory.file(), saved.stamp));
        CHECK_EQ(directory.entries(), 1);
    }
}

TEST_CASE("read_boundaries")
{
    test::Directory directory;
    const std::string prefix(1024 * 1024 - 1, 'x');
    const std::pair<const char*, const char*> cases[] = {
        {"한🙂\r\n끝", "한🙂\n끝"},
        {"\r\n끝", "\n끝"},
    };
    for (const auto& entry : cases) {
        CAPTURE(entry.first);
        test::write(directory.file(), prefix + entry.first);
        auto loaded = load(directory.file());
        REQUIRE(loaded);
        CHECK(loaded.value.format.ending == LineEnding::Crlf);
        CHECK_EQ(loaded.value.text, prefix + entry.second);
    }
}

TEST_CASE("missing_file")
{
    test::Directory directory;
    CHECK(load(directory.file()).error.code == ErrorCode::NotFound);
    auto loaded = load(directory.file(), {true});
    REQUIRE(loaded);
    CHECK_FALSE(loaded.value.stamp.exists());
    CHECK_FALSE(std::filesystem::exists(directory.file()));

    REQUIRE(save(directory.file(), "new", test::options(loaded.value)));
    CHECK(save(directory.file(), "must not overwrite").error.code == ErrorCode::Conflict);
    CHECK_EQ(test::read(directory.file()), "new");
}

TEST_CASE("unicode_path")
{
    test::Directory directory;
    auto path = directory.path / std::filesystem::path(u8"한글-🙂.txt");
    SaveOptions options{{LineEnding::Crlf, true}, {}, 6};
    CHECK(save(path, "x\ny", options).error.code == ErrorCode::TooLarge);
    CHECK_EQ(directory.entries(), 0);

    options.maxBytes = 7;
    REQUIRE(save(path, "x\ny", options));
    CHECK_EQ(test::read(path), "\xef\xbb\xbfx\r\ny");
    auto loaded = load(path);
    REQUIRE(loaded);
    CHECK_EQ(loaded.value.text, "x\ny");
}

TEST_CASE("invalid_input")
{
    test::Directory directory;
    SUBCASE("empty_path")
    {
        CHECK(load({}).error.code == ErrorCode::InvalidPath);
    }
    SUBCASE("nul_path")
    {
        CHECK(load(Path(std::string("bad\0path", 8))).error.code == ErrorCode::InvalidPath);
    }
    SUBCASE("directory")
    {
        CHECK(load(directory.path).error.code == ErrorCode::InvalidPath);
    }
    SUBCASE("size_limit")
    {
        test::write(directory.file(), "1234");
        CHECK(load(directory.file(), {false, 3}).error.code == ErrorCode::TooLarge);
    }
    SUBCASE("utf8")
    {
        test::write(directory.file(), "valid then \xe2\x82");
        auto invalid = load(directory.file());
        CHECK(invalid.error.code == ErrorCode::NotText);
        CHECK(invalid.value.text.empty());
    }
#if !defined(_WIN32)
    SUBCASE("fifo")
    {
        REQUIRE_EQ(mkfifo(directory.file("fifo").c_str(), 0600), 0);
        CHECK(load(directory.file("fifo")).error.code == ErrorCode::InvalidPath);
    }
#endif
}

TEST_CASE("invalid_output")
{
    test::Directory directory;
    const std::string bytes = "one\r\ntwo\n";
    test::write(directory.file(), bytes);
    auto loaded = load(directory.file());
    REQUIRE(loaded);
    auto options = test::options(loaded.value);

    SUBCASE("mixed_line_endings")
    {
        CHECK(save(directory.file(), loaded.value.text, options).error.code == ErrorCode::InvalidText);
    }
    SUBCASE("utf8")
    {
        options.format.ending = LineEnding::Lf;
        CHECK(save(directory.file(), "\xff", options).error.code == ErrorCode::InvalidText);
    }
    CHECK_EQ(test::read(directory.file()), bytes);
}

TEST_CASE_FIXTURE(ExistingFile, "content_conflict")
{
    SUBCASE("modified")
    {
        auto modified = std::filesystem::last_write_time(path);
        test::write(path, "external");
        std::filesystem::last_write_time(path, modified);
        CHECK(check(path, options.expected).code == ErrorCode::Conflict);
        CHECK(save(path, "new", options).error.code == ErrorCode::Conflict);
        CHECK_EQ(test::read(path), "external");
    }
    SUBCASE("deleted")
    {
        std::filesystem::remove(path);
        CHECK(check(path, options.expected).code == ErrorCode::Conflict);
        CHECK(save(path, "do not recreate", options).error.code == ErrorCode::Conflict);
        CHECK_FALSE(std::filesystem::exists(path));
    }
}

TEST_CASE_FIXTURE(ExistingFile, "precommit_conflict")
{
    Faults hooks;
    hooks.before = [&](Stage stage) {
        if (stage == Stage::Verify) test::write(path, "external");
        return std::error_code{};
    };
    auto saved = save(path, "new", options);
    CHECK(saved.error.code == ErrorCode::Conflict);
    CHECK_EQ(test::read(path), "external");
    CHECK_EQ(directory.entries(), 1);
}

TEST_CASE_FIXTURE(ExistingFile, "replacement_conflict")
{
    auto replacement = directory.file("replacement");
    test::write(replacement, "original");
    std::filesystem::remove(path);
    std::filesystem::rename(replacement, path);

    CHECK(save(path, "new", options).error.code == ErrorCode::Conflict);
    CHECK_EQ(test::read(path), "original");
}

TEST_CASE("create_conflict")
{
    test::Directory directory;
    Faults hooks;
    hooks.before = [&](Stage stage) {
        if (stage == Stage::Commit) test::write(directory.file(), "external");
        return std::error_code{};
    };
    auto saved = save(directory.file(), "new");
    CHECK(saved.error.code == ErrorCode::Conflict);
    CHECK_EQ(test::read(directory.file()), "external");
    CHECK_EQ(directory.entries(), 1);
}

TEST_CASE_FIXTURE(ExistingFile, "write_failures")
{
    for (auto stage : {Stage::Create, Stage::Write, Stage::Metadata, Stage::FileSync, Stage::Close, Stage::Commit}) {
        CAPTURE(stage);
        Faults hooks;
        hooks.before = [stage](Stage current) {
            return stage == current ? std::make_error_code(std::errc::no_space_on_device) : std::error_code{};
        };
        auto failed = save(path, "new", options);
        CHECK(failed.state == SaveState::NotWritten);
        CHECK(failed.error.system == std::errc::no_space_on_device);
        CHECK_EQ(test::read(path), "original");
        CHECK_EQ(directory.entries(), 1);
    }
}

TEST_CASE_FIXTURE(ExistingFile, "short_writes")
{
    Faults hooks;
    hooks.maxWrite = 2;
    SUBCASE("partial_failure")
    {
        size_t writes = 0;
        hooks.before = [&](Stage stage) {
            return stage == Stage::Write && ++writes == 3 ? std::make_error_code(std::errc::no_space_on_device) : std::error_code{};
        };
        auto failed = save(path, "many bytes", options);
        CHECK(failed.error.system == std::errc::no_space_on_device);
        CHECK(failed.state == SaveState::NotWritten);
        CHECK_EQ(test::read(path), "original");
    }
    SUBCASE("complete")
    {
        REQUIRE(save(path, "한🙂\ntext", options));
        CHECK_EQ(test::read(path), "한🙂\ntext");
    }
    CHECK_EQ(directory.entries(), 1);
}

TEST_CASE_FIXTURE(ExistingFile, "postcommit_failure")
{
    Faults hooks;
    SUBCASE("directory_sync")
    {
        hooks.before = [](Stage stage) {
            return stage == Stage::DirectorySync ? std::make_error_code(std::errc::io_error) : std::error_code{};
        };
        auto saved = save(path, "new", options);
        CHECK(saved.state == SaveState::CommittedDurabilityUnknown);
        CHECK(saved.error.system == std::errc::io_error);
        CHECK(!check(path, saved.stamp));
    }
    SUBCASE("exception")
    {
        hooks.before = [](Stage stage) -> std::error_code {
            if (stage == Stage::DirectorySync) throw std::runtime_error("completion failure");
            return {};
        };
        auto saved = save(path, "new", options);
        CHECK(saved.state == SaveState::CommittedDurabilityUnknown);
        CHECK(saved.error.code == ErrorCode::Io);
        CHECK(!check(path, saved.stamp));
    }
    CHECK_EQ(test::read(path), "new");
    CHECK_EQ(directory.entries(), 1);
}

TEST_CASE_FIXTURE(ExistingFile, "postcommit_change")
{
    Faults hooks;
    hooks.before = [&](Stage stage) {
        if (stage == Stage::DirectorySync) test::write(path, "external");
        return std::error_code{};
    };
    auto saved = save(path, "new", options);
    REQUIRE(saved);
    CHECK(save(path, "newer", {{}, saved.stamp}).error.code == ErrorCode::Conflict);
    CHECK_EQ(test::read(path), "external");
}

TEST_CASE_FIXTURE(ExistingFile, "cancellation")
{
    detail::Control control;
    Faults hooks;
    SUBCASE("before_commit")
    {
        hooks.before = [&](Stage stage) {
            if (stage == Stage::Commit) CHECK(control.cancel());
            return std::error_code{};
        };
        auto failed = detail::save(path, "new", options, &control);
        CHECK(failed.error.code == ErrorCode::Cancelled);
        CHECK_EQ(test::read(path), "original");
    }
    SUBCASE("after_commit")
    {
        hooks.before = [&](Stage stage) {
            if (stage == Stage::DirectorySync) CHECK_FALSE(control.cancel());
            return std::error_code{};
        };
        REQUIRE(detail::save(path, "new", options, &control));
        CHECK_EQ(test::read(path), "new");
    }
}

TEST_CASE_FIXTURE(ExistingFile, "hardlinks")
{
    auto alias = directory.file("alias");
    std::filesystem::create_hard_link(path, alias);
    CHECK(save(path, "new", options).error.code == ErrorCode::UnsupportedMetadata);
    CHECK_EQ(test::read(path), "original");
    CHECK_EQ(test::read(alias), "original");
    CHECK_EQ(std::filesystem::hard_link_count(path), 2);
}

#if !defined(_WIN32)
TEST_CASE_FIXTURE(ExistingFile, "symlinks")
{
    auto alias = directory.file("link");
    std::filesystem::create_symlink(path.filename(), alias);
    auto loaded = load(alias);
    REQUIRE(loaded);

    SUBCASE("referent")
    {
        REQUIRE(save(alias, "new", test::options(loaded.value)));
        CHECK(std::filesystem::is_symlink(alias));
        CHECK_EQ(test::read(path), "new");
    }
    SUBCASE("retargeted")
    {
        test::write(directory.file("other"), "other");
        std::filesystem::remove(alias);
        std::filesystem::create_symlink("other", alias);
        CHECK(save(alias, "do not overwrite", test::options(loaded.value)).error.code == ErrorCode::Conflict);
        CHECK_EQ(test::read(directory.file("other")), "other");
    }
    SUBCASE("dangling")
    {
        std::filesystem::remove(alias);
        std::filesystem::create_symlink("missing", alias);
        CHECK_FALSE(load(alias, {true}));
        CHECK_FALSE(save(alias, "do not replace link"));
        CHECK(std::filesystem::is_symlink(alias));
    }
}

TEST_CASE_FIXTURE(ExistingFile, "permissions")
{
    REQUIRE_EQ(chmod(path.c_str(), 0640), 0);
    auto saved = save(path, "second", options);
    REQUIRE(saved);
    struct stat info = {};
    REQUIRE_EQ(stat(path.c_str(), &info), 0);
    CHECK_EQ(info.st_mode & 0777, 0640);

    REQUIRE_EQ(chmod(path.c_str(), 0444), 0);
    CHECK(save(path, "third", {{}, saved.stamp}).error.code == ErrorCode::ReadOnly);
    CHECK_EQ(test::read(path), "second");
}
#endif

#if defined(__APPLE__) || defined(__linux__)
TEST_CASE_FIXTURE(ExistingFile, "extended_attributes")
{
    setAttribute(path, "metadata");
    CHECK(!check(path, options.expected));
    REQUIRE(save(path, "second", options));
    char value[8]{};
    REQUIRE_EQ(getAttribute(path, value, sizeof(value)), ssize_t(sizeof(value)));
    CHECK_EQ(std::string(value, sizeof(value)), "metadata");
}

TEST_CASE_FIXTURE(ExistingFile, "metadata_conflict")
{
    Faults hooks;
    hooks.before = [&](Stage stage) {
        if (stage == Stage::Verify) setAttribute(path, "new metadata");
        return std::error_code{};
    };
    auto saved = save(path, "replacement", options);
    CHECK(saved.error.code == ErrorCode::Conflict);
    CHECK(saved.state == SaveState::NotWritten);
    CHECK_EQ(test::read(path), "original");
}

TEST_CASE_FIXTURE(ExistingFile, "staging_attributes")
{
    Faults hooks;
    hooks.before = [&](Stage stage) {
        if (stage == Stage::Metadata)
            for (const auto& entry : std::filesystem::directory_iterator(directory.path))
                if (entry.path() != path) setAttribute(entry.path(), "staging");
        return std::error_code{};
    };
    REQUIRE(save(path, "new", options));
    CHECK_EQ(getAttribute(path, nullptr, 0), -1);
    CHECK_EQ(test::read(path), "new");
}
#endif

TEST_SUITE_END();
