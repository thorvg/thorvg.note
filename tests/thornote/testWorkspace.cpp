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

TEST_CASE_FIXTURE(PersistedWorkspace, "a_saved_copy_keeps_the_document_on_its_own_file")
{
    storage::test::write(directory.file("note.md"), "note\r\n");
    auto id = workspace.open(directory.file("note.md"));
    REQUIRE(wait([&] { return !workspace.find(id)->loading; }));
    auto& tab = *workspace.find(id);
    REQUIRE(tab.document.text("edited\n"));
    workspace.message.clear();
    REQUIRE(workspace.saveCopy(id, directory.file("copy.md")));
    REQUIRE(wait([&] { return !workspace.message.empty() || !workspace.error.empty(); }));
    CHECK(workspace.error.empty());
    CHECK(storage::test::read(directory.file("copy.md")) == "edited\r\n");
    CHECK(storage::test::read(directory.file("note.md")) == "note\r\n");
    CHECK(tab.path.filename() == "note.md");
    CHECK(tab.dirty());
    // A file open in a tab is never overwritten by a copy.
    CHECK_FALSE(workspace.saveCopy(id, directory.file("note.md")));
}

TEST_CASE_FIXTURE(PersistedWorkspace, "a_moved_note_keeps_its_tab_and_saves_in_the_new_folder")
{
    auto folder = directory.path / "folder";
    std::filesystem::create_directory(folder);
    storage::test::write(directory.file("note.md"), "note");
    auto id = workspace.open(directory.file("note.md"));
    REQUIRE(wait([&] { return !workspace.find(id)->loading; }));
    auto& tab = *workspace.find(id);
    REQUIRE(workspace.move(id, folder));
    REQUIRE(complete);
    complete({});
    CHECK(workspace.message == "Moved to folder");
    CHECK(tab.path == folder / "note.md");
    CHECK_FALSE(std::filesystem::exists(directory.file("note.md")));
    REQUIRE(tab.document.text("edited"));
    complete = {};
    REQUIRE(workspace.save(id));
    REQUIRE(wait([&] { return bool(complete); }));
    complete({});
    CHECK(storage::test::read(folder / "note.md") == "edited");
    // A move never replaces a file at the destination.
    storage::test::write(directory.file("note.md"), "taken");
    CHECK_FALSE(workspace.move(id, directory.path));
    CHECK(workspace.error.find("Move failed") == 0);
    CHECK(tab.path == folder / "note.md");
    CHECK(storage::test::read(directory.file("note.md")) == "taken");
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

TEST_CASE("session_round_trip_preserves_fractional_positions_in_large_documents")
{
    tn::SessionState state;
    tn::SessionDocument document;
    document.id = 1;
    state.selections.fill(1);
    document.content.id = std::string(32, 'a');
    document.content.title = "Large.md";
    document.scrollY = 64427259.779846191;
    state.documents.push_back(document);
    tn::SessionState restored;
    REQUIRE(tn::decodeSession(tn::encodeSession(state), restored));
    REQUIRE(restored.documents.size() == 1);
    CHECK(restored.documents[0].scrollY == document.scrollY);
}

TEST_CASE("session_keeps_the_layouts_of_documents_and_reads_one_older_selection")
{
    tn::SessionState state;
    for (uint8_t id : {1, 2}) {
        tn::SessionDocument document;
        document.id = id;
        document.content.id = std::string(32, char('a' + id));
        document.layouts = id;
        state.documents.push_back(document);
    }
    state.selections = {1, 2};
    auto text = tn::encodeSession(state);
    tn::SessionState restored;
    REQUIRE(tn::decodeSession(text, restored));
    CHECK(restored.selections == state.selections);
    CHECK(restored.documents[1].layouts == 2);
    // Older sessions selected one document for every layout.
    text.replace(text.find("selections 1 2"), 14, "active 2");
    REQUIRE(tn::decodeSession(text, restored));
    CHECK(restored.selections == std::array<uint64_t, tn::layoutCount>{2, 2});
    state.documents[0].layouts = 1 << tn::layoutCount;
    CHECK_FALSE(tn::decodeSession(tn::encodeSession(state), restored));
}

TEST_CASE_FIXTURE(PersistedWorkspace, "layouts_select_and_close_their_own_documents")
{
    auto first = workspace.create().id, second = workspace.create().id;
    workspace.show(tn::Layout::Note);
    CHECK_FALSE(workspace.active());
    auto note = workspace.create().id;
    // A document both layouts hold leaves one without closing.
    workspace.select(first);
    REQUIRE(workspace.find(first)->document.text("changed"));
    workspace.show(tn::Layout::Memo);
    CHECK(workspace.active()->id == second);
    CHECK_FALSE(workspace.find(note)->in(tn::Layout::Memo));
    workspace.select(first);
    REQUIRE(workspace.close(first));
    CHECK(workspace.active()->id == second);
    workspace.show(tn::Layout::Note);
    CHECK(workspace.active()->id == first);
    CHECK_FALSE(workspace.close(first));
    CHECK(workspace.close(note));
    CHECK_FALSE(workspace.find(note));
    // The last layout holding changes closes them only when discarded.
    CHECK(workspace.close(first, true));
    CHECK_FALSE(workspace.find(first));
    CHECK_FALSE(workspace.active());
}
