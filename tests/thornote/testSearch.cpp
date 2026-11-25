#include <thornote/platform/native_text_input.h>
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
    ui::Rect editorBounds() const { return app->editorBounds; }
    auto preview(std::string_view text, float width) { return app->previewLines(text, width); }

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

    const ui::Theme& appearance() const { return app->theme; }
    bool transitioning() const { return app->themeTransition.has_value(); }
    void drawAt(double seconds) { app->render(800, 600, seconds); }
    void prepareFont(DocumentTab& tab, std::string_view added) { app->prepareFont(tab, added); }
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
    CHECK(area->y >= state().queryBounds.y);
    CHECK(area->y + area->h <= state().queryBounds.y + state().queryBounds.h);
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
#ifdef __APPLE__
    constexpr auto primary = SDL_KMOD_GUI;
#else
    constexpr auto primary = SDL_KMOD_CTRL;
#endif
    auto press = [&](SDL_Keycode key, uint64_t ms, bool repeat = false) {
        SDL_KeyboardEvent event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key = key;
        event.mod = primary;
        event.timestamp = SDL_MS_TO_NS(ms);
        event.repeat = repeat;
        REQUIRE(app->nativeKeyDown(event));
        return tab.editor.options().fontSize;
    };
    CHECK(press(SDLK_EQUALS, 100) == 17);
    CHECK(press(SDLK_EQUALS, 400, true) == 19);
    CHECK(press(SDLK_EQUALS, 1000, true) == 23);
    CHECK(press(SDLK_EQUALS, 1800, true) == 29);
    REQUIRE(run("editor.font.decrease"));
    CHECK(tab.editor.options().fontSize == 28);

    SDL_Event release{};
    release.type = SDL_EVENT_KEY_UP;
    release.key.key = SDLK_EQUALS;
    release.key.mod = primary;
    app->event(release, 1, 1);
    CHECK(press(SDLK_EQUALS, 2000, true) == 29);
    CHECK(press(SDLK_MINUS, 2100) == 28);
    CHECK(press(SDLK_MINUS, 3000, true) == 24);
    CHECK(press(SDLK_MINUS, 4000, true) == 18);
    CHECK(press(SDLK_MINUS, 4100, true) == 12);
    CHECK(press(SDLK_MINUS, 4200, true) == 6);
    CHECK(enabled("editor.font.decrease"));
    CHECK(run("editor.font.decrease"));
    CHECK(press(SDLK_PLUS, 4300) == 6);
    CHECK(press(SDLK_0, 4400) == 16);
    CHECK(press(SDLK_KP_PLUS, 4500) == 17);
    CHECK(press(SDLK_KP_MINUS, 4600) == 16);
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
    app->event(motion, 1, 1);

    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.mouse_x = x;
    event.wheel.mouse_y = y;
    auto wheel = [&](float delta, SDL_Keymod modifiers, SDL_MouseWheelDirection direction = SDL_MOUSEWHEEL_NORMAL) {
        event.wheel.y = delta;
        event.wheel.direction = direction;
        auto previous = SDL_GetModState();
        SDL_SetModState(modifiers);
        app->event(event, 1, 1);
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
#ifdef __APPLE__
    constexpr auto primary = SDL_KMOD_GUI;
#else
    constexpr auto primary = SDL_KMOD_CTRL;
#endif
    key(SDLK_Z, primary);
    CHECK(state().query.value() == "한글");
    key(SDLK_Z, SDL_Keymod(primary | SDL_KMOD_SHIFT));
    CHECK(state().query.value() == "한글 input");
    key(SDLK_A, primary);
    input("replacement");
    CHECK(state().query.value() == "replacement");
    key(SDLK_Z, primary);
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
    app->nativeTextInput({id, 0, 0, 3, "ㅎ", true});
    CHECK(field.value().empty());
    CHECK(field.preedit == "ㅎ");
    app->nativeTextInput({id, 0, 0, 3, "한", true});
    CHECK(field.value().empty());
    app->nativeTextInput({id, 0, 0, 0, "한", false});
    CHECK(field.value() == "한");
    CHECK(field.preedit.empty());
    app->nativeTextInput({id, 0, 3, 3, "하", true});
    CHECK(field.value() == "한");
    app->nativeTextInput({id, 0, 3, 0, "하", false});
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
    for (auto key : {SDLK_A, SDLK_E, SDLK_SPACE}) {
        SDL_KeyboardEvent event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key = key;
        CHECK_FALSE(app->nativeKeyDown(event));
    }
    SDL_KeyboardEvent option{};
    option.type = SDL_EVENT_KEY_DOWN;
    option.key = SDLK_E;
    option.mod = SDL_KMOD_ALT;
    CHECK_FALSE(app->nativeKeyDown(option));
    auto id = reinterpret_cast<uintptr_t>(&field->document);
    app->nativeTextInput({id, 0, 0, 0, "find", false});
    CHECK(field->value() == "find");
    SDL_KeyboardEvent backspace{};
    backspace.type = SDL_EVENT_KEY_DOWN;
    backspace.key = SDLK_BACKSPACE;
    CHECK(app->nativeKeyDown(backspace));
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
            app->poll();
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
        key(SDLK_F1);
        draw();
        input(query);
        draw();
        key(SDLK_RETURN);
        draw();
    };
    fromPalette("rename file");
    REQUIRE(app->context.focused() == "prompt/name");
    input("renamed");
    key(SDLK_RETURN);
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
    fromPalette("delete file");
    REQUIRE(app->context.focused() == "prompt/cancel");
    key(SDLK_ESCAPE);
    CHECK(std::filesystem::exists(destination));
    CHECK(tab->dirty());
    fromPalette("delete file");
    key(SDLK_TAB);
    REQUIRE(app->context.focused() == "prompt/confirm");
    key(SDLK_RETURN);
    CHECK_FALSE(std::filesystem::exists(destination));
    CHECK_FALSE(app->workspace.find(id));
}

TEST_CASE_FIXTURE(EditorTest, "settings_search_leaves_printable_native_keys_for_text_interpretation")
{
    REQUIRE(run("settings.open"));
    draw();
    app->context.focus("settings/search");
    draw();
    auto field = app->context.input();
    REQUIRE(field);
    SDL_KeyboardEvent event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key = SDLK_A;
    CHECK_FALSE(app->nativeKeyDown(event));
    auto id = reinterpret_cast<uintptr_t>(&field->document);
    app->nativeTextInput({id, 0, 0, 0, "font", false});
    CHECK(field->value() == "font");
}

TEST_CASE_FIXTURE(EditorTest, "editor_leaves_printable_native_keys_for_text_interpretation")
{
    auto& tab = document("");
    draw();
    REQUIRE(editorFocused());
    for (auto key : {SDLK_A, SDLK_C, SDLK_X, SDLK_V, SDLK_Z, SDLK_Y, SDLK_B, SDLK_SPACE}) {
        SDL_KeyboardEvent event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key = key;
        CHECK_FALSE(app->nativeKeyDown(event));
        event.mod = SDL_KMOD_SHIFT;
        CHECK_FALSE(app->nativeKeyDown(event));
    }
    SDL_KeyboardEvent option{};
    option.type = SDL_EVENT_KEY_DOWN;
    option.key = SDLK_A;
    option.mod = SDL_KMOD_ALT;
    CHECK_FALSE(app->nativeKeyDown(option));
    CHECK(tab.document.text().empty());
    app->nativeTextInput({tab.id, 0, 0, 0, "a", false});
    CHECK(tab.document.text() == "a");
    app->nativeTextInput({tab.id, 1, 1, 3, "한", true});
    CHECK(tab.document.text() == "a");
    app->nativeTextInput({tab.id, 1, 1, 0, "한", false});
    CHECK(tab.document.text() == "a한");
#ifdef __APPLE__
    constexpr auto primary = SDL_KMOD_GUI;
#else
    constexpr auto primary = SDL_KMOD_CTRL;
#endif
    SDL_KeyboardEvent shortcut{};
    shortcut.type = SDL_EVENT_KEY_DOWN;
    shortcut.key = SDLK_A;
    shortcut.mod = primary;
    CHECK(app->nativeKeyDown(shortcut));
    CHECK(tab.editor.session().selectedText() == "a한");
    app->nativeTextInput({tab.id, 0, 4, 0, "A", false});
    CHECK(tab.document.text() == "A");
    shortcut.key = SDLK_Z;
    CHECK(app->nativeKeyDown(shortcut));
    CHECK(tab.document.text() == "a한");
}

TEST_CASE_FIXTURE(EditorTest, "markdown_shortcuts_use_the_editor_selection")
{
    auto& tab = document("한글 note");
    draw();
#ifdef __APPLE__
    constexpr auto primary = SDL_KMOD_GUI;
#else
    constexpr auto primary = SDL_KMOD_CTRL;
#endif
    for (auto [shortcut, marker] : {std::pair{SDLK_B, "**"}, {SDLK_I, "*"}, {SDLK_E, "`"}}) {
        tab.editor.select(6, 0);
        key(shortcut, primary);
        CHECK(tab.document.text() == std::string(marker) + "한글" + marker + " note");
        CHECK(tab.editor.session().selection().anchor > tab.editor.session().selection().caret);
        CHECK(tab.editor.session().selectedText() == "한글");
        key(shortcut, primary);
        CHECK(tab.document.text() == "한글 note");
    }
    auto shift = SDL_Keymod(primary | SDL_KMOD_SHIFT);
    key(SDLK_X, shift);
    CHECK(tab.document.text() == "~~한글~~ note");
    key(SDLK_X, shift);
    key(SDLK_2, SDL_Keymod(primary | SDL_KMOD_ALT));
    CHECK(tab.document.text() == "## 한글 note");
    key(SDLK_1, SDL_Keymod(primary | SDL_KMOD_ALT));
    CHECK(tab.document.text() == "# 한글 note");
    key(SDLK_1, SDL_Keymod(primary | SDL_KMOD_ALT));
    key(SDLK_8, shift);
    CHECK(tab.document.text() == "- 한글 note");
    key(SDLK_7, shift);
    CHECK(tab.document.text() == "1. 한글 note");
    key(SDLK_9, shift);
    CHECK(tab.document.text() == "- [ ] 한글 note");
    key(SDLK_9, shift);
    key(SDLK_PERIOD, shift);
    CHECK(tab.document.text() == "> 한글 note");
    key(SDLK_PERIOD, shift);
    key(SDLK_K, primary);
    CHECK(tab.document.text() == "[한글]() note");
    CHECK(tab.editor.session().selection().caret == 9);
    tab.editor.undo();
    REQUIRE(run("edit.find"));
    key(SDLK_B, primary);
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

TEST_CASE("line_number_corrections_round_trip_and_keep_legacy_defaults")
{
    tn::Preferences value;
    value.lineNumberScale = 1.25f;
    value.lineNumberOffset = -2;
    tn::Preferences decoded;
    REQUIRE(decodePreferences(encodePreferences(value), decoded));
    CHECK(decoded.lineNumberScale == value.lineNumberScale);
    CHECK(decoded.lineNumberOffset == value.lineNumberOffset);
    REQUIRE(decodePreferences("thornote-settings 1\nfont-size 20\n", decoded));
    CHECK(decoded.lineNumberScale == 1);
    CHECK(decoded.lineNumberOffset == 0);
    for (auto invalid : {"line-number-scale 0", "line-number-scale 3", "line-number-scale nan", "line-number-offset 17", "line-number-offset -17", "line-number-offset inf"})
        CHECK_FALSE(decodePreferences(std::string("thornote-settings 1\n") + invalid + '\n', decoded));
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
