#include <common/doctest/doctest.h>
#include <common/engine.h>
#include <storage/common/files.h>
#include <thornote/app/editor_app.h>

namespace tn::test
{
class EditorTest
{
    Engine engine;

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
    void draw(float width = 800) { app->render(width, 600); }

    void query(std::string value)
    {
        state().query = {value, value.size()};
        app->updateFind();
    }

    void key(SDL_Keycode key, SDL_Keymod modifiers = SDL_KMOD_NONE)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = key;
        event.key.mod = modifiers;
        app->event(event, 1, 1);
    }

    void input(const char* value)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = value;
        app->event(event, 1, 1);
    }

    void click(ui::Rect rect)
    {
        for (auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
            SDL_Event event{};
            event.type = type;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.x = rect.x + rect.w / 2;
            event.button.y = rect.y + rect.h / 2;
            app->event(event, 1, 1);
        }
    }

    bool editorFocused() const { return app->editorFocus; }
    void focusEditor() { app->focusEditor(true); }
};
}  // namespace tn::test

using tn::test::EditorTest;

TEST_SUITE_BEGIN("search");

TEST_CASE_FIXTURE(EditorTest, "literal_search_uses_utf8_byte_ranges_and_wraps")
{
    auto& tab = document("café [a] café [A]");
    tab.editor.select(0, 5);
    REQUIRE(run("edit.find"));
    CHECK(state().query.value == "café");
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
    state().replacement.value = "oneone";
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
    state().replacement.value = "é";
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
        state().replacement.value = "b\r\nc\r";
        REQUIRE(run("edit.replace.current"));
        CHECK(tab.document.text() == "b\nc\n aa");
        CHECK(tab.editor.session().selection().start() == 5);
    }
    SUBCASE("unchanged_text")
    {
        state().replacement.value = "aa";
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
    state().replacement.value = "dog";
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
    state().replacement.value = "new";
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
    state().replacement.value = "new";
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
        key(SDLK_TAB);
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
#ifdef __APPLE__
    key(SDLK_F, SDL_KMOD_GUI);
#else
    key(SDLK_F, SDL_KMOD_CTRL);
#endif
    draw(390);
    REQUIRE(state().open);
    CHECK(app->context.focused() == "find/query");
    CHECK(!editorFocused());
    input("cat");
    CHECK(state().matches.size() == 2);
    CHECK(tab.document.text() == "cat cat");
    auto area = app->textInputArea();
    REQUIRE(area);
    CHECK(area->y == state().queryBounds.y);
    key(SDLK_RETURN);
    CHECK(state().current == 1);
    key(SDLK_RETURN, SDL_KMOD_SHIFT);
    CHECK(state().current == 0);
    REQUIRE(run("edit.replace.toggle"));
    draw(390);
    click(state().replacementBounds);
    CHECK(app->context.focused() == "find/replacement");
    input("dog");
    key(SDLK_F3);
    CHECK(tab.document.text() == "cat cat");
    CHECK(state().current == 1);
    key(SDLK_F3, SDL_KMOD_SHIFT);
    CHECK(state().current == 0);
    key(SDLK_RETURN);
    CHECK(tab.document.text() == "dog cat");
    key(SDLK_ESCAPE);
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
            app->poll();
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

TEST_SUITE_END();
