#include <storage/common/files.h>
#include <thornote/documents/note_browser.h>
#include <thornote/documents/recent.h>

TEST_CASE("note_previews_skip_titles_and_preserve_utf8_boundaries")
{
    CHECK(tn::documentPreview("# Title\r\n\r\nBody text\r\nMore text") == "Body text");
    CHECK(tn::documentPreview("Single line") == "Single line");
    CHECK(tn::documentPreview("# Title\n\n").empty());
    CHECK(tn::documentPreview("# Title\n" + std::string(159, 'a') + "é") == std::string(159, 'a'));
}

TEST_CASE("file_previews_load_without_opening_documents_and_refresh_after_edits")
{
    storage::test::Directory directory;
    storage::test::write(directory.file(), "# Title\n\nFirst preview\n");
    auto newer = directory.file("newer.md");
    storage::test::write(newer, "# Newer\n");
    std::filesystem::last_write_time(directory.file(), std::filesystem::last_write_time(newer) - std::chrono::hours(1));
    tn::NoteBrowser browser;
    auto wait = [&](const auto& complete) {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!complete() && std::chrono::steady_clock::now() < end) {
            browser.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return complete();
    };
    browser.show({directory.path});
    REQUIRE(wait([&] { return !browser.loading(); }));
    REQUIRE(browser.files().size() == 2);
    REQUIRE(browser.files()[0].path == newer);
    REQUIRE(wait([&] { return browser.preview(1) == "First preview"; }));

    storage::test::write(directory.file(), "# Title\n\nUpdated preview\n");
    std::filesystem::last_write_time(directory.file(), std::filesystem::last_write_time(newer) + std::chrono::hours(1));
    SUBCASE("explicit refresh retains the previous preview during the scan")
    {
        browser.show({directory.path}, true);
        CHECK(browser.preview(1) == "First preview");
    }
    SUBCASE("a completed save refreshes the order when the library is shown")
    {
        browser.fileAccessed(directory.file(), false);
        browser.show({directory.path});
        CHECK_FALSE(browser.loading());
        browser.fileAccessed(directory.file(), true);
        browser.show({directory.path});
    }
    CHECK(browser.files()[0].path == newer);
    REQUIRE(wait([&] { return !browser.loading(); }));
    REQUIRE(browser.files()[0].path == directory.file());
    REQUIRE(wait([&] { return browser.preview(0) == "Updated preview"; }));
}

TEST_CASE("file_previews_reload_only_after_their_files_are_modified")
{
    storage::test::Directory directory;
    auto first = directory.file("first.md"), second = directory.file("second.md");
    storage::test::write(first, "# First\n\nFirst preview\n");
    storage::test::write(second, "# Second\n\nSecond preview\n");
    tn::NoteBrowser browser;
    auto wait = [&](const auto& complete) {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!complete() && std::chrono::steady_clock::now() < end) {
            browser.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return complete();
    };
    auto preview = [&](const storage::Path& path) {
        auto found = std::find_if(browser.files().begin(), browser.files().end(), [&](const auto& file) { return file.path == path; });
        return found == browser.files().end() ? std::string{} : browser.preview(size_t(found - browser.files().begin()));
    };
    browser.show({directory.path});
    REQUIRE(wait([&] { return !browser.loading(); }));
    REQUIRE(wait([&] { return preview(first) == "First preview"; }));

    // Opening the note and refreshing the list keep the preview while the modification time stays.
    auto stamp = std::filesystem::last_write_time(first);
    storage::test::write(first, "# First\n\nChanged preview\n");
    std::filesystem::last_write_time(first, stamp);
    browser.fileAccessed(first, false);
    browser.show({directory.path}, true);
    REQUIRE(wait([&] { return !browser.loading(); }));
    // Previews load in order, so any reload of the first lands before the second.
    REQUIRE(wait([&] { return preview(second) == "Second preview"; }));
    CHECK(preview(first) == "First preview");

    std::filesystem::last_write_time(first, stamp + std::chrono::seconds(10));
    browser.fileAccessed(first, true);
    browser.show({directory.path});
    REQUIRE(wait([&] { return !browser.loading(); }));
    REQUIRE(wait([&] { return preview(first) == "Changed preview"; }));
}

TEST_CASE("preview_row_heights_update_scroll_geometry")
{
    std::vector<storage::DirectoryEntry> files{{"first.md", {}}, {"second.md", {}}, {"third.md", {}}};
    tn::NoteFileList list;
    REQUIRE(list.update(files, 1, {}));
    REQUIRE(list.rows().size() == 4);
    list.rowHeight(1, 88);
    list.rowHeight(2, 56);
    CHECK(list.rows()[2].top == 116);
    CHECK(list.rows()[3].top == 172);
    CHECK(list.height() == 232);
    auto visible = list.visibleRows(116, 56);
    CHECK(visible.first == 2);
    CHECK(visible.second == 3);
}

TEST_CASE("note_dates_use_modification_time_regardless_of_creation_time")
{
    constexpr int64_t created = 1704110400000;
    constexpr int64_t modified = created + 2 * 24 * 60 * 60 * 1000;
    std::vector<storage::DirectoryEntry> files{
        {"created.md", created, modified},
        {"modified.md", {}, modified},
        {"unknown.md", created, {}},
    };
    tn::NoteFileList list;
    REQUIRE(list.update(files, 1, {}));
    REQUIRE(list.rows().size() == 5);
    CHECK(list.rows()[0].date == tn::modificationDate(modified));
    CHECK(list.rows()[1].file == 0);
    CHECK(list.rows()[2].file == 1);
    CHECK(list.rows()[3].date == "Date unavailable");
    CHECK(list.rows()[4].file == 2);
}

TEST_CASE("note_roots_resolve_directory_aliases_before_opening_files")
{
    storage::test::Directory directory;
    auto alias = directory.path / "alias";
    auto notes = directory.path / "notes";
    std::filesystem::create_directory(notes);
    std::filesystem::create_directory_symlink(notes, alias);
    tn::RecentItems recent;
    recent.directories = {alias, notes};
    REQUIRE(recent.rootDirectories().size() == 1);
    CHECK(recent.rootDirectories().front() == notes);
    CHECK(recent.rootDirectory(notes) == notes);
    CHECK(recent.rootDirectory(alias) == notes);
}

TEST_CASE("imported_folders_stay_among_directories_until_removed")
{
    storage::test::Directory directory;
    auto opened = directory.path / "opened";
    auto imported = directory.path / "imported";
    std::filesystem::create_directory(opened);
    std::filesystem::create_directory(imported);
    tn::RecentItems recent;
    recent.rememberDirectory(opened);
    recent.importDirectory(imported);
    CHECK(recent.imported == std::vector<storage::Path>{imported});
    CHECK(recent.directories == std::vector<storage::Path>{imported, opened});
    recent.removeDirectory(imported);
    CHECK(recent.imported.empty());
    CHECK(recent.directories == std::vector<storage::Path>{opened});
}
