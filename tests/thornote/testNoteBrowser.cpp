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
    REQUIRE(browser.files().size() == 1);
    REQUIRE(wait([&] { return browser.preview(0) == "First preview"; }));

    storage::test::write(directory.file(), "# Title\n\nUpdated preview\n");
    browser.show({directory.path}, true);
    CHECK(browser.preview(0) == "First preview");
    REQUIRE(wait([&] { return !browser.loading(); }));
    REQUIRE(wait([&] { return browser.preview(0) == "Updated preview"; }));
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
