#include <common/doctest/doctest.h>
#include <common/engine.h>
#include <storage/common/files.h>
#include <thornote/app/editor_app.h>
#include <thornote/shell/input.h>

namespace tn::test
{
#ifdef __APPLE__
constexpr ui::platform::KeyModifiers primary{.meta = true};
#else
constexpr ui::platform::KeyModifiers primary{.control = true};
#endif

class EditorTest
{
    Engine engine;
    std::optional<storage::test::Directory> layouts;

public:
    std::unique_ptr<EditorApp> app = std::make_unique<EditorApp>();

    ~EditorTest()
    {
        app.reset();
        ui::unloadFonts();
    }

    DocumentTab& document(std::string_view text)
    {
        auto& tab = app->workspace.create();
        REQUIRE(tab.document.text(text));
        app->focusEditor(true);
        return tab;
    }

    auto& state() { return app->find; }
    auto& browser() { return app->noteBrowser; }
    const auto& collection() const { return app->collection; }
    bool run(std::string_view command) { return app->commands.execute(command); }
    bool enabled(std::string_view command) { return app->commands.find(command)->enabled(); }
    void draw(float width = 800) { app->render(width, 600, ui::frameTime()); }
    ui::Rect editorBounds() const { return app->editorBounds; }
    auto preview(std::string_view text, float width) { return app->previewLines(text, width); }

    void query(std::string_view value)
    {
        state().query = {value, value.size()};
        app->updateFind();
    }

    void key(Key key, ui::platform::KeyModifiers modifiers = {}) { app->keyDown({.key = key, .modifiers = modifiers}); }
    void input(const char* value) { app->text(value); }

    void click(ui::Rect rect)
    {
        PointerEvent pointer{.x = rect.x + rect.w / 2, .y = rect.y + rect.h / 2};
        app->pointerDown(pointer);
        app->pointerUp(pointer);
    }

    const ui::Theme& appearance() const { return app->theme; }
    bool transitioning() const { return app->themeTransition.has_value(); }
    void drawAt(double seconds) { app->render(800, 600, seconds); }
    void prepareFont(DocumentTab& tab, std::string_view added) { app->prepareFont(tab, added); }
    bool editorFocused() const { return app->editorFocus; }
    bool settingsOpen() const { return app->settings.open; }
    void focusEditor() { app->focusEditor(true); }
    const std::string& extensionError() const { return app->inputExtensions.error(); }
    void useExtensions(const storage::Path& path) { app->inputExtensions.setDirectory(path); }

    std::optional<ui::MenuDefinition> menuDefinition(std::string_view id) const { return app->layoutScript.menu(id); }
    void activate(uint64_t id) { app->activateDocument(id); }
    void openNoteMenu(uint64_t id) { app->openNoteMenu(id, {20, 20, 1, 1}); }
    // An installed copy of the bundled layouts can predate the source tree.
    bool useSourceLayouts()
    {
        auto source = utf8Path(THORNOTE_SOURCE_DATA) / "layouts";
        layouts.emplace();
        for (auto name : {"memo.lua", "note.lua"})
            std::filesystem::copy_file(source / "workspace.lua", layouts->path / name);
        std::filesystem::copy_file(source / "titlebar.lua", layouts->path / "titlebar.lua");
        app->setLayoutDirectory(layouts->path);
        draw();
        return app->layoutScript.error().empty();
    }
    // Replaces a layout script copied by useSourceLayouts().
    void writeLayout(const char* name, std::string_view source) { storage::test::write(layouts->path / name, source); }
    // Height of the row listing the file in the note browser, or zero when it is not listed.
    float noteRowHeight(const storage::Path& path) const
    {
        for (const auto& row : app->noteFiles.rows())
            if (row.file && app->noteBrowser.files()[*row.file].path == path) return row.height;
        return 0;
    }

    // Right-clicks down the note list, from below its search field, until a note opens its menu.
    bool rightClickNote()
    {
        draw();
        auto search = app->searchBounds;
        for (float y = search.y + search.h + 8; y < 600 && app->menu.empty(); y += 4) {
            PointerEvent pointer{.x = search.x + 20, .y = y, .button = Button::Right};
            app->pointerDown(pointer);
            app->pointerUp(pointer);
            draw();
        }
        return app->menu == "note-menu";
    }

    bool wait(const std::function<bool()>& complete)
    {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!complete() && std::chrono::steady_clock::now() < end) {
            app->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return complete();
    }
};
}  // namespace tn::test

using tn::Key;
using tn::test::EditorTest;
using tn::test::primary;

TEST_SUITE_BEGIN("search");

TEST_CASE_FIXTURE(EditorTest, "literal_search_uses_utf8_byte_ranges_and_wraps")
{
    auto& tab = document("café [a] café [A]");
    tab.editor.select(0, 5);
    REQUIRE(run("edit.find"));
    CHECK(state().query.value() == "café");
    REQUIRE(state().matches.size() == 2);
    CHECK(state().matches[1].start == 10);
    CHECK(state().current == 0);
    REQUIRE(run("edit.find.next"));
    CHECK(state().current == 1);
    REQUIRE(run("edit.find.next"));
    CHECK(state().current == 0);
    REQUIRE(run("edit.find.previous"));
    CHECK(state().current == 1);
    query("[a]");
    REQUIRE(state().matches.size() == 1);
    CHECK(state().matches[0].start == 6);
    CHECK(tab.editor.session().selectedText() == "[a]");
}

TEST_CASE_FIXTURE(EditorTest, "matches_are_non_overlapping_and_empty_queries_do_not_match")
{
    auto& tab = document("aaaaa");
    REQUIRE(run("edit.find"));
    query("aa");
    REQUIRE(state().matches.size() == 2);
    CHECK(state().matches[0].start == 0);
    CHECK(state().matches[1].start == 2);
    query("");
    CHECK(state().matches.empty());
    CHECK(!enabled("edit.find.next"));
    CHECK(!enabled("edit.replace.all"));
    query("missing");
    CHECK(state().matches.empty());
    CHECK(!run("edit.replace.current"));
    CHECK(tab.document.text() == "aaaaa");
}

TEST_CASE_FIXTURE(EditorTest, "replace_current_advances_past_inserted_matches")
{
    auto& tab = document("one one");
    REQUIRE(run("edit.replace"));
    query("one");
    state().replacement.value("oneone");
    REQUIRE(run("edit.replace.current"));
    CHECK(tab.document.text() == "oneone one");
    CHECK(tab.editor.session().selection().start() == 7);
    REQUIRE(tab.editor.undo());
    CHECK(tab.document.text() == "one one");
    CHECK(tab.editor.session().selectedText() == "one");
    REQUIRE(tab.editor.redo());
    CHECK(tab.document.text() == "oneone one");
}

TEST_CASE_FIXTURE(EditorTest, "replace_all_is_one_undo_step_separate_from_prior_edits")
{
    auto& tab = document("cat cat cat");
    std::string error;
    tab.editor.select(11, 11);
    tab.editor.beginUndoGroup();
    REQUIRE(tab.editor.insert("!", error));
    REQUIRE(run("edit.replace"));
    query("cat");
    state().replacement.value("é");
    REQUIRE(run("edit.replace.all"));
    CHECK(tab.document.text() == "é é é!");
    REQUIRE(tab.editor.undo());
    CHECK(tab.document.text() == "cat cat cat!");
    REQUIRE(tab.editor.undo());
    CHECK(tab.document.text() == "cat cat cat");
    REQUIRE(tab.editor.redo());
    REQUIRE(tab.editor.redo());
    CHECK(tab.document.text() == "é é é!");
}

TEST_CASE_FIXTURE(EditorTest, "deletion_normalized_replacements_and_noops")
{
    auto& tab = document("aa aa");
    REQUIRE(run("edit.replace"));
    query("aa");
    SUBCASE("deletion")
    {
        REQUIRE(run("edit.replace.all"));
        CHECK(tab.document.text() == " ");
    }
    SUBCASE("line_endings")
    {
        state().replacement.value("b\r\nc\r");
        REQUIRE(run("edit.replace.current"));
        CHECK(tab.document.text() == "b\nc\n aa");
        CHECK(tab.editor.session().selection().start() == 5);
    }
    SUBCASE("unchanged_text")
    {
        state().replacement.value("aa");
        auto revision = tab.document.revision();
        REQUIRE(run("edit.replace.all"));
        CHECK(tab.document.revision() == revision);
        CHECK(!tab.document.canUndo());
    }
}

TEST_CASE_FIXTURE(EditorTest, "replacements_follow_current_revisions_and_document_switches")
{
    auto& first = document("cat cat");
    auto id = first.id;
    REQUIRE(run("edit.replace"));
    query("cat");
    std::string error;
    REQUIRE(first.editor.applyEdits({first.document.revision(), {{0, 0, "prefix "}}, {}}, error));
    state().replacement.value("dog");
    REQUIRE(run("edit.replace.all"));
    CHECK(first.document.text() == "prefix dog dog");
    auto& second = document("cat");
    draw();
    CHECK(state().document == second.id);
    REQUIRE(state().matches.size() == 1);
    REQUIRE(run("edit.replace.all"));
    CHECK(second.document.text() == "dog");
    CHECK(first.document.text() == "prefix dog dog");
    app->activateDocument(id);
    draw();
    CHECK(state().matches.empty());
    REQUIRE(first.editor.undo());
    draw();
    CHECK(state().matches.size() == 2);
}

TEST_CASE_FIXTURE(EditorTest, "replacement_revalidates_selection_and_respects_edit_policy")
{
    auto& tab = document("cat dog");
    REQUIRE(run("edit.replace"));
    query("cat");
    state().replacement.value("new");
    tab.editor.select(4, 7);
    REQUIRE(run("edit.replace.current"));
    CHECK(tab.document.text() == "cat dog");
    CHECK(tab.editor.session().selectedText() == "cat");
    auto options = tab.editor.options();
    options.readOnly = true;
    tab.editor.options(options);
    CHECK(enabled("edit.find.next"));
    CHECK(!run("edit.replace.all"));
    options.readOnly = false;
    tab.editor.options(options);
    focusEditor();
    draw();
    tab.editor.composition("x", 1);
    REQUIRE(tab.editor.composing());
    CHECK(!run("edit.replace.all"));
    CHECK(tab.document.text() == "cat dog");
}

TEST_CASE_FIXTURE(EditorTest, "commands_detect_matches_added_after_an_empty_search")
{
    auto& tab = document("dog");
    REQUIRE(run("edit.replace"));
    query("cat");
    CHECK(!enabled("edit.replace.all"));
    std::string error;
    REQUIRE(tab.editor.applyEdits({tab.document.revision(), {{0, 3, "cat"}}, {}}, error));
    state().replacement.value("new");
    REQUIRE(run("edit.replace.all"));
    CHECK(tab.document.text() == "new");
}

TEST_CASE_FIXTURE(EditorTest, "tabbing_from_search_back_to_the_editor_restores_text_input")
{
    auto& tab = document("cat cat");
    REQUIRE(run("edit.find"));
    query("cat");
    draw();
    for (int i = 0; i < 10 && app->context.focused() != "editor"; ++i) {
        key(Key::Tab);
        draw();
    }
    REQUIRE(app->context.focused() == "editor");
    CHECK(editorFocused());
    CHECK(app->textInputTarget() == &tab);
    input("new");
    CHECK(tab.document.text() == "new cat");
}

TEST_CASE_FIXTURE(EditorTest, "field_input_navigation_and_close_preserve_document_focus")
{
    auto& tab = document("cat cat");
    draw();
    key(Key('f'), primary);
    draw(390);
    REQUIRE(state().open);
    CHECK(app->context.focused() == "find/query");
    CHECK(!editorFocused());
    input("cat");
    CHECK(state().matches.size() == 2);
    CHECK(tab.document.text() == "cat cat");
    auto area = app->textInputArea();
    REQUIRE(area);
    CHECK(area->y >= state().queryBounds.y);
    CHECK(area->y + area->h <= state().queryBounds.y + state().queryBounds.h);
    key(Key::Enter);
    CHECK(state().current == 1);
    key(Key::Enter, {.shift = true});
    CHECK(state().current == 0);
    REQUIRE(run("edit.replace.toggle"));
    draw(390);
    click(state().replacementBounds);
    CHECK(app->context.focused() == "find/replacement");
    input("dog");
    key(Key::F3);
    CHECK(tab.document.text() == "cat cat");
    CHECK(state().current == 1);
    key(Key::F3, {.shift = true});
    CHECK(state().current == 0);
    key(Key::Enter);
    CHECK(tab.document.text() == "dog cat");
    key(Key::Escape);
    CHECK(!state().open);
    CHECK(editorFocused());
    CHECK(app->context.focused() == "editor");
}

TEST_CASE("theme_highlight_tokens_round_trip_and_follow_defaults")
{
    tn::ThemeProfile profile;
    std::string error;
    REQUIRE(tn::parseTheme("return {schema=1, colors={highlight='#12345678', highlightActive='#ABCDEF'}}", "theme.lua", false, profile, error));
    CHECK(profile.colors.highlight.a == 0x78);
    ui::StyleColor style;
    REQUIRE(ui::styleColor("highlight", style));
    CHECK(style.resolve(profile.colors).r == 0x12);
    REQUIRE(ui::styleColor("highlightActive", style));
    CHECK(style.resolve(profile.colors).g == 0xcd);
    tn::ThemeProfile restored;
    REQUIRE(tn::parseTheme(tn::encodeTheme(profile), "theme.lua", false, restored, error));
    CHECK(restored.colors.highlight.a == 0x78);
    REQUIRE(tn::parseTheme("return {schema=1, colors={text='#222222'}}", "theme.lua", true, restored, error));
    CHECK(restored.colors.highlight.r == tn::defaultTheme(true).colors.highlight.r);
}

TEST_CASE_FIXTURE(EditorTest, "opening_a_listed_note_preserves_sidebar_geometry")
{
    storage::test::Directory directory;
    storage::test::write(directory.file(), "# Title\n\nBody preview\n");
    app->setLayout(tn::Layout::Note);
    browser().show({directory.path});
    auto wait = [&](const auto& complete) {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!complete() && std::chrono::steady_clock::now() < end) {
            app->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return complete();
    };
    REQUIRE(wait([&] { return !browser().loading(); }));
    auto revision = browser().revision();
    auto id = app->workspace.open(directory.file());
    REQUIRE(wait([&] { return !app->workspace.find(id)->loading; }));
    CHECK_FALSE(browser().loading());
    CHECK(browser().revision() == revision);
}

TEST_CASE_FIXTURE(EditorTest, "new_drafts_remain_in_all_notes")
{
    app->setLayout(tn::Layout::Note);
    REQUIRE(run("file.new"));
    CHECK(collection() == "all");
    REQUIRE(app->workspace.active());
    CHECK(app->workspace.active()->path.empty());
    REQUIRE(run("file.new"));
    CHECK(collection() == "all");
    CHECK(app->workspace.documents().size() == 2);
}

TEST_CASE_FIXTURE(EditorTest, "notes_open_one_at_a_time_and_only_the_focused_one_follows_to_memo")
{
    storage::test::Directory directory;
    for (auto name : {"a.md", "b.md", "c.md"})
        storage::test::write(directory.file(name), name);
    auto& memo = document("memo");
    app->openPath(directory.path);
    CHECK(app->workspace.active() == &memo);
    auto open = [&](const char* name) {
        auto id = app->workspace.open(directory.file(name), false);
        REQUIRE(wait([&] { return !app->workspace.busy(); }));
        draw();
        return id;
    };
    auto a = open("a.md");
    // Memo keeps the unsaved draft, so Note lets it go.
    CHECK_FALSE(memo.in(tn::Layout::Note));
    REQUIRE(app->workspace.find(a)->document.text("edited"));
    auto b = open("b.md");
    REQUIRE(run("note.favorite.toggle"));
    auto c = open("c.md");
    CHECK(app->workspace.active()->id == c);
    // The note left behind closes, unless it has changes or is a favorite.
    CHECK(open("a.md") == a);
    CHECK_FALSE(app->workspace.find(c));
    CHECK(app->workspace.find(b));
    app->setLayout(tn::Layout::Memo);
    CHECK(app->workspace.active()->id == a);
    CHECK_FALSE(app->workspace.find(b)->in(tn::Layout::Memo));
    CHECK(memo.in(tn::Layout::Memo));
    activate(memo.id);
    app->setLayout(tn::Layout::Note);
    CHECK(app->workspace.active() == &memo);
}

TEST_CASE_FIXTURE(EditorTest, "a_note_left_behind_keeps_its_preview_in_the_list")
{
    storage::test::Directory directory;
    auto a = directory.file("a.md"), b = directory.file("b.md");
    for (const auto& path : {a, b})
        storage::test::write(path, "# Title\n\nBody preview\n");
    app->openPath(directory.path);
    REQUIRE(wait([&] { return !browser().loading(); }));
    REQUIRE(wait([&] {
        draw();
        return noteRowHeight(a) > 40 && noteRowHeight(b) > 40;
    }));
    auto height = noteRowHeight(a);
    auto opened = app->workspace.open(a, false);
    activate(opened);
    REQUIRE(wait([&] { return !app->workspace.busy(); }));
    draw();
    activate(app->workspace.open(b, false));
    REQUIRE(wait([&] { return !app->workspace.busy(); }));
    draw();
    // The note closes behind, while its row keeps the preview so the rows below stay in place.
    CHECK_FALSE(app->workspace.find(opened));
    CHECK(noteRowHeight(a) == height);
}

TEST_CASE_FIXTURE(EditorTest, "note_rows_keep_the_saved_preview_until_the_note_is_saved")
{
    storage::test::Directory directory;
    // The first line runs past the bytes read for a preview.
    auto path = directory.file("long.md");
    storage::test::write(path, std::string(1200, 'a') + "\n");
    app->openPath(directory.path);
    REQUIRE(wait([&] { return !browser().loading(); }));
    REQUIRE(wait([&] {
        draw();
        return noteRowHeight(path) > 40;
    }));
    auto height = noteRowHeight(path);
    auto id = app->workspace.open(path, false);
    activate(id);
    REQUIRE(wait([&] { return !app->workspace.busy(); }));
    draw();
    CHECK(noteRowHeight(path) == height);
    app->edit({id, 0, 0, 0, "# Title\n\nShort\n", false});
    draw();
    CHECK(noteRowHeight(path) == height);
    key(Key('s'), primary);
    REQUIRE(wait([&] { return !app->workspace.busy(); }));
    CHECK(wait([&] {
        draw();
        return noteRowHeight(path) == 56;
    }));
}

TEST_CASE_FIXTURE(EditorTest, "closing_a_memo_tab_leaves_its_changes_with_note")
{
    auto& shared = document("shared");
    auto& other = document("other");
    app->setLayout(tn::Layout::Note);
    activate(shared.id);
    app->setLayout(tn::Layout::Memo);
    CHECK(app->workspace.active() == &shared);
    REQUIRE(shared.document.text("edited"));
    // Note holds the document as well, so the tab closes without asking.
    REQUIRE(run("file.close"));
    CHECK_FALSE(shared.in(tn::Layout::Memo));
    CHECK(app->workspace.active() == &other);
    app->setLayout(tn::Layout::Note);
    draw();
    CHECK(app->workspace.active() == &other);
    CHECK(shared.in(tn::Layout::Note));
    CHECK(shared.document.text() == "edited");
}

TEST_CASE_FIXTURE(EditorTest, "layout_scripts_turn_commands_off_per_layout")
{
    app->chooseFolder = [](const storage::Path&, const auto&) {
    };
    REQUIRE(useSourceLayouts());
    // Memo opens files, so folders belong to Note.
    CHECK_FALSE(enabled("folder.open"));
    CHECK(enabled("file.open"));
    app->setLayout(tn::Layout::Note);
    draw();
    CHECK(enabled("folder.open"));
    // A script declaring its own list replaces the default.
    app->setLayout(tn::Layout::Memo);
    writeLayout("memo.lua", "return {schema = 1, disabledCommands = function(state) return {'file.new'} end, build = function(state) return ui.view {source = 'content'} end}");
    REQUIRE(run("layout.script.reload"));
    draw();
    CHECK(enabled("folder.open"));
    CHECK_FALSE(enabled("file.new"));
}

TEST_CASE_FIXTURE(EditorTest, "right_clicking_a_draft_selects_it_and_opens_the_note_menu")
{
    app->setLayout(tn::Layout::Note);
    REQUIRE(useSourceLayouts());
    auto& first = document("first note");
    document("second note");
    REQUIRE(rightClickNote());
    CHECK(app->workspace.active() == &first);
    auto definition = menuDefinition("note-menu");
    REQUIRE(definition);
    std::vector<std::string> commands;
    for (const auto& item : definition->items)
        commands.push_back(item.command);
    // The empty commands are separators and the Move Note submenu.
    CHECK(commands == std::vector<std::string>{"file.export", "", "file.new", "", "note.favorite.toggle", "note.duplicate", "file.rename", "", "", "file.delete"});
    CHECK(definition->items[7].title == "Move Note");
    CHECK(definition->items[4].title == "Favorite Note");
    REQUIRE(run("note.favorite.toggle"));
    draw();
    CHECK(menuDefinition("note-menu")->items[4].title == "Unfavorite Note");
    // A draft has no file to rename, move or delete.
    CHECK_FALSE(enabled("file.rename"));
    CHECK_FALSE(enabled("note.move.choose"));
    CHECK_FALSE(enabled("file.delete"));
    REQUIRE(run("note.duplicate"));
    REQUIRE(app->workspace.documents().size() == 3);
    auto copy = app->workspace.active();
    CHECK(copy != &first);
    CHECK(copy->document.text() == "first note");
    CHECK(copy->path.empty());
}

TEST_CASE_FIXTURE(EditorTest, "note_menu_duplicates_a_saved_note_beside_it")
{
    storage::test::Directory directory;
    auto source = directory.file("note.md");
    storage::test::write(source, "body\r\n");
    app->openPath(directory.path);
    REQUIRE(useSourceLayouts());
    REQUIRE(wait([&] { return !browser().loading(); }));
    REQUIRE(rightClickNote());
    auto original = app->workspace.active();
    REQUIRE(original);
    REQUIRE(wait([&] { return !app->workspace.busy(); }));
    CHECK(original->document.text() == "body\n");
    CHECK(enabled("file.rename"));
    CHECK(enabled("file.delete"));
    // The name of a copy that already exists is not reused.
    for (auto name : {"note copy.md", "note copy 2.md"}) {
        activate(original->id);
        REQUIRE(run("note.duplicate"));
        REQUIRE(wait([&] { return !app->workspace.busy(); }));
        CHECK(app->workspace.active() != original);
        CHECK(storage::test::read(directory.file(name)) == "body\r\n");
    }
    CHECK(storage::test::read(source) == "body\r\n");
}

TEST_CASE_FIXTURE(EditorTest, "note_menu_moves_a_saved_note_to_a_listed_or_chosen_folder")
{
    storage::test::Directory directory;
    auto inbox = directory.path / "inbox", done = directory.path / "done", archive = directory.path / "archive";
    for (const auto& folder : {inbox, done, archive})
        std::filesystem::create_directory(folder);
    storage::test::write(inbox / "note.md", "body");
    app->openPath(done);
    app->openPath(inbox);
    REQUIRE(useSourceLayouts());
    REQUIRE(wait([&] { return !browser().loading(); }));
    REQUIRE(rightClickNote());
    REQUIRE(wait([&] { return !app->workspace.busy(); }));
    draw();
    REQUIRE(menuDefinition("note-menu")->items[7].id == "note-menu/move");
    auto move = menuDefinition("note-menu/move");
    REQUIRE(move);
    std::vector<std::string> titles;
    for (const auto& item : move->items)
        titles.push_back(item.title);
    // Imported folders follow the folder dialog entry, most recent first.
    CHECK(titles == std::vector<std::string>{"Choose Folder…", "", "inbox", "done"});
    // A note does not move into its own folder.
    CHECK_FALSE(enabled(move->items[2].command));
    REQUIRE(run(move->items[3].command));
    auto tab = app->workspace.active();
    CHECK(tab->path == done / "note.md");
    CHECK(storage::test::read(done / "note.md") == "body");
    CHECK_FALSE(std::filesystem::exists(inbox / "note.md"));
    app->chooseFolder = [&](const storage::Path& initial, const auto& chosen) {
        CHECK(initial == done);
        chosen({tn::utf8(archive)}, {});
    };
    REQUIRE(run("note.move.choose"));
    CHECK(tab->path == archive / "note.md");
    CHECK(storage::test::read(archive / "note.md") == "body");
    // The chosen folder joins the recent ones when the menu opens again.
    openNoteMenu(tab->id);
    draw();
    CHECK(menuDefinition("note-menu/move")->items.back().title == "archive");
}

TEST_CASE_FIXTURE(EditorTest, "exporting_a_note_writes_a_copy_to_the_chosen_file")
{
    storage::test::Directory directory;
    auto destination = directory.file("export.txt");
    auto& draft = document("draft note");
    app->chooseFile = [&](bool save, const storage::Path&, const auto& done) {
        CHECK(save);
        done({tn::utf8(destination)}, {});
    };
    app->workspace.message.clear();
    REQUIRE(run("file.export"));
    REQUIRE(wait([&] { return !app->workspace.message.empty() || !app->workspace.error.empty(); }));
    CHECK(app->workspace.error.empty());
    CHECK(storage::test::read(destination) == "draft note");
    CHECK(draft.path.empty());
}

TEST_CASE_FIXTURE(EditorTest, "note_previews_reuse_wrapping_and_follow_content_width_and_font_changes")
{
    draw();
    auto fontFor = app->context.fontFor;
    auto font = fontFor("preview", false);
    size_t measured = 0;
    app->context.fontFor = [&](std::string_view, bool) {
        ++measured;
        return font;
    };
    const auto text = "Alpha beta gamma delta epsilon zeta eta theta iota kappa lambda";
    auto first = preview(text, 70);
    REQUIRE(!first[1].empty());
    auto before = measured;
    CHECK(preview(text, 70) == first);
    CHECK(measured == before + 1);  // Only resolve the face; no prefix measurements on a cache hit.
    CHECK(preview("Edited preview", 70) != first);
    auto wider = preview(text, 200);
    CHECK(wider[0].size() > first[0].size());
    font = fontFor("preview", true);
    before = measured;
    auto bold = preview(text, 70);
    CHECK(measured > before + 1);
    before = measured;
    CHECK(preview(text, 70) == bold);
    CHECK(measured == before + 1);
    REQUIRE(run("theme.toggle"));
    before = measured;
    CHECK(preview(text, 70) == bold);
    CHECK(measured > before + 1);
}

TEST_CASE_FIXTURE(EditorTest, "note_preview_wrapping_preserves_utf8_and_word_boundaries")
{
    draw();
    auto width = app->context.textWidth("Alpha beta", 10) + 0.01f;
    auto lines = preview("  Alpha beta gamma delta", width);
    CHECK(lines[0] == "Alpha beta");
    CHECK(lines[1] == "gamma");
    CHECK(lines[2] == "delta");
    auto narrow = preview("한글中文", 0);
    CHECK(narrow[0] == "한");
    CHECK(narrow[1] == "글");
    CHECK(narrow[2].empty());
    CHECK(preview(" \t ", 50) == std::array<std::string, 3>{});
    // Cache replacement must not change wrapping when a previously seen preview returns.
    for (size_t i = 0; i < 100; ++i)
        preview("Preview " + std::to_string(i), 70);
    CHECK(preview("  Alpha beta gamma delta", width) == lines);
}

TEST_SUITE_END();

TEST_CASE_FIXTURE(EditorTest, "editor_zoom_accelerates_only_while_the_shortcut_is_held")
{
    auto& tab = document("sample");
    draw();
    auto press = [&](Key key, uint64_t ms, bool repeat = false) {
        REQUIRE(app->keyDown({.key = key, .modifiers = primary, .repeat = repeat, .time = ms}));
        return tab.editor.options().fontSize;
    };
    CHECK(press(Key('='), 100) == 17);
    CHECK(press(Key('='), 400, true) == 19);
    CHECK(press(Key('='), 1000, true) == 23);
    CHECK(press(Key('='), 1800, true) == 29);
    REQUIRE(run("editor.font.decrease"));
    CHECK(tab.editor.options().fontSize == 28);

    app->keyUp({.key = Key('='), .modifiers = primary});
    CHECK(press(Key('='), 2000, true) == 29);
    CHECK(press(Key('-'), 2100) == 28);
    CHECK(press(Key('-'), 3000, true) == 24);
    CHECK(press(Key('-'), 4000, true) == 18);
    CHECK(press(Key('-'), 4100, true) == 12);
    CHECK(press(Key('-'), 4200, true) == 6);
    CHECK(enabled("editor.font.decrease"));
    CHECK(run("editor.font.decrease"));
    CHECK(press(Key('+'), 4300) == 6);
    CHECK(press(Key('0'), 4400) == 16);
    CHECK(press(Key::KeypadPlus, 4500) == 17);
    CHECK(press(Key::KeypadMinus, 4600) == 16);
}

TEST_CASE_FIXTURE(EditorTest, "control_wheel_changes_editor_font_size")
{
    auto& tab = document(std::string(120, '\n'));
    draw();
    auto area = editorBounds();
    auto x = area.x + area.w / 2;
    auto y = area.y + area.h / 2;
    REQUIRE(app->editorAt(x, y));

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    tn::send(*app, motion, 1, 1);

    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.mouse_x = x;
    event.wheel.mouse_y = y;
    auto wheel = [&](float delta, SDL_Keymod modifiers, SDL_MouseWheelDirection direction = SDL_MOUSEWHEEL_NORMAL) {
        event.wheel.y = delta;
        event.wheel.direction = direction;
        auto previous = SDL_GetModState();
        SDL_SetModState(modifiers);
        tn::send(*app, event, 1, 1);
        SDL_SetModState(previous);
        return tab.editor.options().fontSize;
    };

    auto size = tab.editor.options().fontSize;
    CHECK(wheel(1, SDL_KMOD_LCTRL) == size + 1);
    CHECK(wheel(-0.5f, SDL_KMOD_RCTRL) == size + 0.5f);
    CHECK(wheel(0.5f, SDL_KMOD_LCTRL, SDL_MOUSEWHEEL_FLIPPED) == size);

    auto scroll = tab.editor.scrollY();
    CHECK(wheel(-1, SDL_KMOD_NONE) == size);
    CHECK(tab.editor.scrollY() > scroll);

    event.wheel.mouse_x = -1;
    CHECK(wheel(1, SDL_KMOD_LCTRL) == size);
}

TEST_CASE("wheel_resampler_gives_display_frames_even_shares_of_slower_input")
{
    tn::WheelResampler resampler;
    double frame = 1.0 / 120;
    size_t event = 0;
    float total = 0;
    std::vector<float> shares;
    for (double now = 0; now < 0.5; now += frame) {
        for (; event * 0.01 <= now && event < 40; ++event)
            resampler.add({.y = 10, .pixels = true}, event * 0.01);
        if (auto scrolled = resampler.take(now)) {
            CHECK(scrolled->pixels);
            total += scrolled->y;
            shares.push_back(scrolled->y);
        } else shares.push_back(0);
    }
    CHECK(!resampler.active());
    CHECK(total == doctest::Approx(400));
    for (size_t i = 4; i < 40; ++i)
        CHECK(shares[i] == doctest::Approx(10 * frame / 0.01).epsilon(0.02));
}

TEST_CASE("wheel_resampler_keeps_sparse_momentum_moving_every_frame")
{
    tn::WheelResampler resampler;
    resampler.add({.y = 20, .pixels = true}, 0);
    resampler.add({.y = 20, .pixels = true}, 0.04);
    resampler.add({.y = 20, .pixels = true}, 0.08);
    for (double now = 0.045; now < 0.12; now += 1.0 / 120) {
        auto scrolled = resampler.take(now);
        REQUIRE(scrolled);
        CHECK(scrolled->y > 0);
    }
}

TEST_CASE_FIXTURE(EditorTest, "font_size_reaches_a_hidden_document_when_it_is_shown")
{
    std::string text;
    for (int i = 0; i < 200; ++i)
        text += "line " + std::to_string(i) + '\n';
    auto& hidden = document(text);
    auto offset = text.find("line 100");
    hidden.editor.select(offset, offset);
    draw();
    auto anchor = hidden.editor.caretRect();
    auto& shown = document("shown");
    draw();
    REQUIRE(run("editor.font.increase"));
    // Only the visible document lays out again.
    CHECK(shown.editor.options().fontSize == 17);
    CHECK(hidden.editor.options().fontSize == 16);
    activate(hidden.id);
    draw();
    CHECK(hidden.editor.options().fontSize == 17);
    // It keeps the caret where it was on screen.
    auto caret = hidden.editor.caretRect();
    CHECK(std::abs((caret.y + caret.h / 2) - (anchor.y + anchor.h / 2)) <= 1);
}

TEST_CASE_FIXTURE(EditorTest, "extension_recursion_counts_only_work_queued_in_turn")
{
    // Many editors report at once without recursing.
    for (int i = 0; i < 200; ++i)
        document("note");
    draw();
    CHECK(extensionError().empty());
    // A command that queues itself again still stops.
    storage::test::Directory extensions;
    storage::test::write(extensions.path / "loop.lua", "return {schema = 1, id = \"loop\", commands = {again = function(editor) editor:command(\"loop.again\") end}}");
    useExtensions(extensions.path);
    REQUIRE(extensionError().empty());
    REQUIRE(run("loop.again"));
    CHECK(extensionError().find("recursion") != std::string::npos);
}

TEST_CASE("fractional_font_sizes_survive_loading_preferences")
{
    tn::Preferences decoded;
    REQUIRE(decodePreferences("thornote-settings 1\nfont-size 0.125\ndark 1\n", decoded));
    CHECK(decoded.fontSize == 0.125f);
    CHECK(decoded.dark);
}

TEST_CASE_FIXTURE(EditorTest, "find_field_undo_and_redo_do_not_change_the_document")
{
    auto& tab = document("Original document");
    draw();
    REQUIRE(run("edit.find"));
    draw();
    input("한글");
    input(" input");
    auto shift = primary;
    shift.shift = true;
    key(Key('z'), primary);
    CHECK(state().query.value() == "한글");
    key(Key('z'), shift);
    CHECK(state().query.value() == "한글 input");
    key(Key('a'), primary);
    input("replacement");
    CHECK(state().query.value() == "replacement");
    key(Key('z'), primary);
    CHECK(state().query.value() == "한글 input");
    CHECK(tab.document.text() == "Original document");
}

TEST_CASE_FIXTURE(EditorTest, "find_field_native_composition_preserves_replacement_ranges")
{
    document("Original document");
    draw();
    REQUIRE(run("edit.find"));
    draw();
    auto& field = state().query;
    auto id = reinterpret_cast<uintptr_t>(&field.document);
    app->edit({id, 0, 0, 3, "ㅎ", true});
    CHECK(field.value().empty());
    CHECK(field.preedit == "ㅎ");
    app->edit({id, 0, 0, 3, "한", true});
    CHECK(field.value().empty());
    app->edit({id, 0, 0, 0, "한", false});
    CHECK(field.value() == "한");
    CHECK(field.preedit.empty());
    app->edit({id, 0, 3, 3, "하", true});
    CHECK(field.value() == "한");
    app->edit({id, 0, 3, 0, "하", false});
    CHECK(field.value() == "하");
    REQUIRE(field.session.undo());
    CHECK(field.value() == "한");
    REQUIRE(field.session.undo());
    CHECK(field.value().empty());
}
TEST_CASE_FIXTURE(EditorTest, "palette_leaves_printable_native_keys_for_text_interpretation")
{
    document("Original document");
    draw();
    REQUIRE(run("ui.commands.open"));
    draw();
    auto field = app->context.input();
    REQUIRE(field);
    REQUIRE(app->context.focused() == "palette/search");
    for (auto key : {Key('a'), Key('e'), Key::Space})
        CHECK_FALSE(app->keyDown({.key = key}));
    CHECK_FALSE(app->keyDown({.key = Key('e'), .modifiers = {.alt = true}}));
    auto id = reinterpret_cast<uintptr_t>(&field->document);
    app->edit({id, 0, 0, 0, "find", false});
    CHECK(field->value() == "find");
    CHECK(app->keyDown({.key = Key::Backspace}));
    CHECK(field->value() == "fin");
}

TEST_CASE_FIXTURE(EditorTest, "file_commands_keep_the_buffer_on_rename_and_confirm_deletion")
{
    storage::test::Directory directory;
    auto source = directory.file("original.md");
    auto destination = directory.file("renamed.md");
    storage::test::write(source, "original\r\n");
    auto id = app->workspace.open(source, false);
    auto wait = [&] {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (app->workspace.busy() && std::chrono::steady_clock::now() < end) {
            app->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE_FALSE(app->workspace.busy());
    };
    wait();
    auto tab = app->workspace.find(id);
    REQUIRE(tab);
    std::string error;
    REQUIRE(tab->editor.insert("unsaved ", error));

    auto fromPalette = [&](const char* query) {
        draw();
        key(Key::F1);
        draw();
        input(query);
        draw();
        key(Key::Enter);
        draw();
    };
    fromPalette("rename file");
    REQUIRE(app->context.focused() == "prompt/name");
    input("renamed");
    key(Key::Enter);
    CHECK_FALSE(std::filesystem::exists(source));
    REQUIRE(std::filesystem::exists(destination));
    REQUIRE(app->workspace.find(id) == tab);
    CHECK(tab->path == destination);
    CHECK(tab->document.text() == "unsaved original\n");
    CHECK(tab->document.canUndo());
    CHECK(tab->dirty());
    CHECK(storage::test::read(destination) == "original\r\n");
    REQUIRE(app->workspace.save(id));
    wait();
    CHECK(storage::test::read(destination) == "unsaved original\r\n");
    CHECK_FALSE(std::filesystem::exists(source));

    REQUIRE(tab->editor.insert("more ", error));
    // The system trash lies outside the test directory.
    CHECK(app->workspace.useTrash == storage::trashAvailable());
    app->workspace.useTrash = false;
    fromPalette("delete file");
    REQUIRE(app->context.focused() == "prompt/cancel");
    key(Key::Escape);
    CHECK(std::filesystem::exists(destination));
    CHECK(tab->dirty());
    fromPalette("delete file");
    key(Key::Tab);
    REQUIRE(app->context.focused() == "prompt/confirm");
    key(Key::Enter);
    CHECK_FALSE(std::filesystem::exists(destination));
    CHECK_FALSE(app->workspace.find(id));
}

TEST_CASE_FIXTURE(EditorTest, "memo_title_bar_drags_above_its_row")
{
    app->setLayout(tn::Layout::Memo);
    REQUIRE(useSourceLayouts());
    document("first");
    document("second");
    draw();
    REQUIRE(editorBounds().y >= 46);
    // The tabs and buttons keep their row at the bottom of the bar, under a band that moves the window.
    float control = -1;
    for (float x = 4; x < 800 && control < 0; x += 4)
        if (!app->draggable(x, 26) && !app->draggable(x, 8)) control = x;
    REQUIRE(control >= 0);
    CHECK(app->draggable(control, 3));
    CHECK_FALSE(app->draggable(control, 44));
}

TEST_CASE_FIXTURE(EditorTest, "note_heading_moves_the_window_and_closes_settings")
{
    app->setLayout(tn::Layout::Note);
    REQUIRE(useSourceLayouts());
    document("note");
    draw();
    auto content = editorBounds();
    REQUIRE(content.w > 0);
    float y = content.y - 20, button = content.x + content.w - 28;
    CHECK(app->draggable(content.x + 40, y));
    CHECK_FALSE(app->draggable(button, y));
    REQUIRE(run("settings.open"));
    draw();
    CHECK(app->draggable(content.x + 40, y));
    click({button - 1, y - 1, 2, 2});
    CHECK_FALSE(settingsOpen());
}

TEST_CASE_FIXTURE(EditorTest, "settings_search_leaves_printable_native_keys_for_text_interpretation")
{
    REQUIRE(run("settings.open"));
    draw();
    app->context.focus("settings/search");
    draw();
    auto field = app->context.input();
    REQUIRE(field);
    CHECK_FALSE(app->keyDown({.key = Key('a')}));
    auto id = reinterpret_cast<uintptr_t>(&field->document);
    app->edit({id, 0, 0, 0, "font", false});
    CHECK(field->value() == "font");
}

TEST_CASE_FIXTURE(EditorTest, "editor_leaves_printable_native_keys_for_text_interpretation")
{
    auto& tab = document("");
    draw();
    REQUIRE(editorFocused());
    for (auto key : {Key('a'), Key('c'), Key('x'), Key('v'), Key('z'), Key('y'), Key('b'), Key::Space}) {
        CHECK_FALSE(app->keyDown({.key = key}));
        CHECK_FALSE(app->keyDown({.key = key, .modifiers = {.shift = true}}));
    }
    CHECK_FALSE(app->keyDown({.key = Key('a'), .modifiers = {.alt = true}}));
    CHECK(tab.document.text().empty());
    app->edit({tab.id, 0, 0, 0, "a", false});
    CHECK(tab.document.text() == "a");
    app->edit({tab.id, 1, 1, 3, "한", true});
    CHECK(tab.document.text() == "a");
    app->edit({tab.id, 1, 1, 0, "한", false});
    CHECK(tab.document.text() == "a한");
    CHECK(app->keyDown({.key = Key('a'), .modifiers = primary}));
    CHECK(tab.editor.session().selectedText() == "a한");
    app->edit({tab.id, 0, 4, 0, "A", false});
    CHECK(tab.document.text() == "A");
    CHECK(app->keyDown({.key = Key('z'), .modifiers = primary}));
    CHECK(tab.document.text() == "a한");
}

TEST_CASE_FIXTURE(EditorTest, "shortcuts_work_after_a_click_ends_a_composition")
{
    storage::test::Directory directory;
    auto path = directory.file("note.md");
    storage::test::write(path, "Body");
    auto id = app->workspace.open(path, false);
    activate(id);
    REQUIRE(wait([&] { return !app->workspace.busy(); }));
    draw();
    auto& tab = *app->workspace.find(id);
    app->edit({id, 4, 4, 0, "!", false});
    app->edit({id, 5, 5, 3, "요", true});
    REQUIRE(tab.editor.composing());
    // The input method does not report the end of a composition that a click drops.
    auto bounds = editorBounds();
    click({bounds.x, bounds.y, 8, 8});
    draw();
    REQUIRE_FALSE(tab.editor.composing());
    key(Key('s'), primary);
    CHECK(tab.saving);
    REQUIRE(wait([&] { return !app->workspace.busy(); }));
    CHECK(storage::test::read(path) == "Body!");
}

TEST_CASE_FIXTURE(EditorTest, "markdown_shortcuts_use_the_editor_selection")
{
    auto& tab = document("한글 note");
    draw();
    for (auto [shortcut, marker] : {std::pair{Key('b'), "**"}, {Key('i'), "*"}, {Key('e'), "`"}}) {
        tab.editor.select(6, 0);
        key(shortcut, primary);
        CHECK(tab.document.text() == std::string(marker) + "한글" + marker + " note");
        CHECK(tab.editor.session().selection().anchor > tab.editor.session().selection().caret);
        CHECK(tab.editor.session().selectedText() == "한글");
        key(shortcut, primary);
        CHECK(tab.document.text() == "한글 note");
    }
    auto shift = primary, alt = primary;
    shift.shift = true;
    alt.alt = true;
    key(Key('x'), shift);
    CHECK(tab.document.text() == "~~한글~~ note");
    key(Key('x'), shift);
    key(Key('2'), alt);
    CHECK(tab.document.text() == "## 한글 note");
    key(Key('1'), alt);
    CHECK(tab.document.text() == "# 한글 note");
    key(Key('1'), alt);
    key(Key('8'), shift);
    CHECK(tab.document.text() == "- 한글 note");
    key(Key('7'), shift);
    CHECK(tab.document.text() == "1. 한글 note");
    key(Key('9'), shift);
    CHECK(tab.document.text() == "- [ ] 한글 note");
    key(Key('9'), shift);
    key(Key('.'), shift);
    CHECK(tab.document.text() == "> 한글 note");
    key(Key('.'), shift);
    key(Key('k'), primary);
    CHECK(tab.document.text() == "[한글]() note");
    CHECK(tab.editor.session().selection().caret == 9);
    tab.editor.undo();
    REQUIRE(run("edit.find"));
    key(Key('b'), primary);
    CHECK(tab.document.text() == "한글 note");
}

TEST_CASE_FIXTURE(EditorTest, "first_added_codepoints_survive_initial_font_preparation")
{
    auto& tab = document("ABC");
    REQUIRE(tab.preparedFont == 0);
    prepareFont(tab, "\xed\x95\x9c");
    CHECK(std::find(tab.codepoints.begin(), tab.codepoints.end(), uint32_t(0xd55c)) != tab.codepoints.end());
    auto points = tab.codepoints;
    prepareFont(tab, "\xed\x95\x9c");
    CHECK(tab.codepoints == points);
    CHECK(tab.document.text() == "ABC");
}

TEST_CASE_FIXTURE(EditorTest, "theme_transitions_preserve_the_current_frame_when_reversed")
{
    CHECK(!transitioning());
    drawAt(10);
    auto initial = appearance();
    REQUIRE(run("theme.toggle"));
    CHECK(transitioning());
    CHECK(tn::sameThemeColors(appearance(), initial));
    drawAt(20);  // An idle interval must not consume the animation before its first frame.
    CHECK(tn::sameThemeColors(appearance(), tn::blendTheme(initial, appearance(), 0)));
    REQUIRE(app->context.nextFrame());
    CHECK(*app->context.nextFrame() <= 20 + 1.0 / 60 + 0.00001);
    drawAt(20.5);
    auto middle = appearance();
    CHECK(middle.background.r != initial.background.r);
    CHECK(middle.text.r != initial.text.r);
    REQUIRE(run("theme.toggle"));
    CHECK(tn::sameThemeColors(appearance(), middle));
    drawAt(20.51);
    CHECK(tn::sameThemeColors(appearance(), middle));
    drawAt(21.6);
    CHECK(!transitioning());
    CHECK(tn::sameThemeColors(appearance(), initial));
}

TEST_CASE_FIXTURE(EditorTest, "repeating_a_theme_selection_does_not_restart_its_transition")
{
    REQUIRE(run("theme.dark"));
    CHECK(!transitioning());  // Startup appearance is immediate.
    drawAt(1);
    REQUIRE(run("theme.light"));
    drawAt(2);
    drawAt(2.5);
    REQUIRE(run("theme.light"));
    drawAt(3.01);
    CHECK(!transitioning());
    CHECK(appearance().background.r == tn::defaultTheme(false).colors.background.r);
}

TEST_CASE("imported_folders_survive_loading_preferences")
{
    tn::Preferences decoded;
    REQUIRE(decodePreferences("thornote-settings 1\nrecent-directory \"/notes/a\"\nrecent-directory \"/notes/b\"\nimported-directory \"/notes/b\"\n", decoded));
    CHECK(decoded.recent.directories.size() == 2);
    CHECK(decoded.recent.imported == std::vector<storage::Path>{"/notes/b"});
    CHECK(encodePreferences(decoded).find("imported-directory \"/notes/b\"\n") != std::string::npos);
}

TEST_CASE("retired_line_number_corrections_still_load_and_are_dropped")
{
    tn::Preferences decoded;
    REQUIRE(decodePreferences("thornote-settings 1\nline-number-scale 1.25\nline-number-offset -2\nfont-size 20\n", decoded));
    CHECK(decoded.fontSize == 20);
    CHECK(encodePreferences(decoded).find("line-number") == std::string::npos);
}

TEST_CASE("theme_blending_resolves_inherited_markdown_colors")
{
    auto from = tn::defaultTheme(false).colors, to = tn::defaultTheme(true).colors;
    auto halfway = tn::blendTheme(from, to, 0.5f);
    CHECK(halfway.background.r > to.background.r);
    CHECK(halfway.background.r < from.background.r);
    CHECK(halfway.markdownHeading.a == 255);
    CHECK(halfway.markdownHeading.r == halfway.accent.r);
    CHECK(halfway.markdownStrong.r == halfway.text.r);
    CHECK(halfway.markdownQuote.r == halfway.muted.r);
    CHECK(tn::sameThemeColors(tn::blendTheme(from, to, 1), to));
}
