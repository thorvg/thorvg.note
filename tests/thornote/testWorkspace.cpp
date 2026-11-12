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
