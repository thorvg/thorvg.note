#include <set>
#include <storage/internal.h>
#include "common/files.h"

using namespace storage;

namespace
{
Result<std::vector<DirectoryEntry>> listDirectory(const Path& directory, ListOptions options = {})
{
    return detail::list(directory, options, nullptr, nullptr);
}
}  // namespace

TEST_SUITE_BEGIN("directory");

TEST_CASE("recursive_listing")
{
    test::Directory directory;
    test::write(directory.file("note.txt"), "Text");
    test::write(directory.file(".hidden"), "Hidden");
    test::write(directory.file("binary.dat"), std::string_view("\0\xff", 2));
    std::filesystem::create_directories(directory.file("nested/more"));
    test::write(directory.file("nested/child.txt"), "Nested");
    test::write(directory.file("nested/more/deep.txt"), "Deep");
    std::error_code error;
    std::filesystem::create_directory_symlink(directory.path, directory.file("nested/back"), error);
#if !defined(_WIN32)
    REQUIRE_FALSE(error);
#endif

    auto result = listDirectory(directory.path);
    REQUIRE(result);
    CHECK_EQ(result.value.size(), 5);
    std::set<Path> paths;
    for (const auto& file : result.value)
        paths.insert(file.path.lexically_relative(directory.path));
    CHECK(paths == std::set<Path>({"note.txt", ".hidden", "binary.dat", "nested/child.txt", "nested/more/deep.txt"}));

    CHECK(listDirectory({}).error.code == ErrorCode::InvalidPath);
    CHECK(listDirectory(directory.file("missing")).error.code == ErrorCode::NotFound);
    CHECK_FALSE(listDirectory(directory.file("note.txt")));
}

TEST_CASE("listing_preserves_modification_time")
{
    test::Directory directory;
    auto path = directory.file("note.txt");
    test::write(path, "Text");
    auto expected = std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{1704110400123}};
    std::filesystem::last_write_time(path, std::chrono::file_clock::from_sys(expected));

    auto result = listDirectory(directory.path);
    REQUIRE(result);
    REQUIRE(result.value.size() == 1);
    REQUIRE(result.value.front().modifiedAt);
    CHECK(*result.value.front().modifiedAt == expected.time_since_epoch().count());
}

TEST_CASE("text_filter")
{
    test::Directory directory;
    test::write(directory.file("note.data"), "한글 🙂\r\nText");
    test::write(directory.file("empty"), "");
    test::write(directory.file("binary.txt"), std::string_view("x\0y", 3));
    test::write(directory.file("late.txt"), std::string(128 * 1024, 'a') + "\xff");
    test::write(directory.file("utf16.txt"), std::string_view("\xff\xfeX\0", 4));
    std::set<Path> expected{"note.data", "empty"};
    std::error_code error;
    std::filesystem::create_symlink(directory.file("note.data"), directory.file("alias.txt"), error);
    if (!error) expected.insert("alias.txt");
#if !defined(_WIN32)
    REQUIRE_FALSE(error);
#endif

    auto result = listDirectory(directory.path, {true});
    REQUIRE(result);
    std::set<Path> actual;
    for (const auto& file : result.value)
        actual.insert(file.path.filename());
    CHECK(actual == expected);
}

TEST_CASE("cache_invalidation")
{
    test::Directory directory;
    auto path = directory.file("note.txt");
    test::write(path, "abc");
    auto modified = std::filesystem::last_write_time(path);
    FileService service;
    auto scan = [&](size_t expected) {
        bool done = false;
        service.list(directory.path, [&](auto result) {
            REQUIRE(result);
            CHECK_EQ(result.value.size(), expected);
            done = true;
        },
                     {true});
        REQUIRE(test::drain(service, [&] { return done; }));
    };

    scan(1);
    scan(1);
    auto replacement = directory.file("replacement");
    test::write(replacement, "\xffXY");
    std::filesystem::last_write_time(replacement, modified);
    std::filesystem::remove(path);
    std::filesystem::rename(replacement, path);
    scan(0);

    test::write(path, "Fixed UTF-8 text 한글");
    scan(1);
}

TEST_SUITE_END();
