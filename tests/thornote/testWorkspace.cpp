#include <common/engine.h>
#include <storage/common/files.h>
#include <thornote/documents/workspace.h>

namespace
{
struct PersistedWorkspace
{
    tn::test::Engine engine;
    storage::test::Directory directory;
    tn::Workspace workspace;
    std::function<void(std::string)> complete;

    PersistedWorkspace()
    {
        workspace.persist = [this](auto done) {
            complete = std::move(done);
        };
    }

    bool wait(const std::function<bool()>& ready)
    {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!ready() && std::chrono::steady_clock::now() < end) {
            workspace.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return ready();
    }
};
}  // namespace

TEST_CASE_FIXTURE(PersistedWorkspace, "saving_waits_for_durable_storage_and_preserves_newer_edits")
{
    storage::test::write(directory.file(), "original");
    auto id = workspace.open(directory.file());
    REQUIRE(wait([&] { return !workspace.find(id)->loading; }));
    auto& tab = *workspace.find(id);
    REQUIRE(tab.document.text("saved snapshot"));
    REQUIRE(workspace.save(id));
    REQUIRE(wait([&] { return bool(complete); }));
    CHECK(tab.saving);
    CHECK(tab.dirty());
    CHECK(tab.savedText == "original");
    REQUIRE(tab.document.text("newer edit"));
    complete({});
    CHECK_FALSE(tab.saving);
    CHECK(tab.dirty());
    CHECK(tab.savedText == "saved snapshot");
}

TEST_CASE_FIXTURE(PersistedWorkspace, "failed_browser_persistence_remains_modified_and_can_be_retried")
{
    auto& tab = workspace.create();
    REQUIRE(tab.document.text("new note"));
    REQUIRE(workspace.saveAs(tab.id, directory.file()));
    REQUIRE(wait([&] { return bool(complete); }));
    complete("storage full");
    CHECK_FALSE(tab.saving);
    CHECK(tab.dirty());
    CHECK(workspace.error.find("storage full") != std::string::npos);
    complete = {};
    REQUIRE(workspace.save(tab.id));
    REQUIRE(wait([&] { return bool(complete); }));
    complete({});
    CHECK_FALSE(tab.saving);
    CHECK_FALSE(tab.dirty());
    CHECK(workspace.error.empty());
}

TEST_CASE_FIXTURE(PersistedWorkspace, "opening_documents_keeps_the_current_document_until_ready")
{
    auto previous = workspace.create().id;
    REQUIRE(workspace.active()->document.text("Current document"));
    storage::test::write(directory.file(), "Next document");
    auto next = workspace.open(directory.file(), false);
    CHECK(workspace.active()->id == previous);
    REQUIRE(wait([&] { return !workspace.find(next)->loading; }));
    CHECK(workspace.active()->id == next);
    CHECK(workspace.active()->document.text() == "Next document");
}

TEST_CASE_FIXTURE(PersistedWorkspace, "pending_documents_do_not_replace_a_later_selection")
{
    auto previous = workspace.create().id;
    storage::test::write(directory.file(), "Next document");
    auto next = workspace.open(directory.file(), false);
    workspace.select(previous);
    REQUIRE(wait([&] { return !workspace.find(next)->loading; }));
    CHECK(workspace.active()->id == previous);
}

TEST_CASE_FIXTURE(PersistedWorkspace, "failed_opens_keep_the_current_document_visible")
{
    auto previous = workspace.create().id;
    auto next = workspace.open(directory.path / "missing.md", false);
    CHECK(workspace.active()->id == previous);
    REQUIRE(wait([&] { return !workspace.find(next); }));
    CHECK(workspace.active()->id == previous);
    CHECK_FALSE(workspace.error.empty());
}

TEST_CASE_FIXTURE(PersistedWorkspace, "opened_notes_use_the_same_path_as_their_listed_files")
{
    storage::test::write(directory.file(), "# Title\n\nBody preview");
    auto alias = directory.path / "alias";
    std::filesystem::create_directory_symlink(directory.path, alias);
    uint64_t id = 0;
    SUBCASE("open")
    {
        id = workspace.open(alias / directory.file().filename(), false);
    }
    SUBCASE("restore")
    {
        tn::SessionDocument state;
        id = state.id = 1;
        state.content.originalPath = alias / directory.file().filename();
        state.content.title = "sample.txt";
        workspace.restore(std::move(state));
    }
    REQUIRE(wait([&] { return !workspace.find(id)->loading; }));
    CHECK(workspace.find(id)->path == std::filesystem::canonical(directory.file()));
    CHECK(workspace.open(directory.file(), false) == id);
}
