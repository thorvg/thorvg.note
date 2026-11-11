#include "common/frame.h"

using ui::Key;

TEST_SUITE_BEGIN("widgets");

TEST_CASE("registry_runs_available_commands_and_releases_by_owner")
{
    ui::CommandRegistry commands;
    int runs = 0;
    CHECK(commands.add({"file.save", "Save", "File", "Mod+S", [&] {
                            ++runs;
                        }}));
    CHECK(!commands.add({"file.save", "Again"}));
    CHECK(!commands.add({"", "Unnamed"}));
    CHECK(commands.add({.id = "note.pin", .title = "Pin Note", .category = "Notes", .run = [&] { ++runs; }, .available = [] { return false; }, .owner = "notes.lua"}));

    CHECK(commands.execute("file.save"));
    CHECK(!commands.execute("note.pin"));
    CHECK(!commands.execute("missing"));
    CHECK(runs == 1);
    CHECK(commands.matching("NOTE") == std::vector<size_t>{1});
    CHECK(commands.matching("").size() == 2);

    CHECK(commands.release("") == 0);
    CHECK(commands.release("notes.lua") == 1);
    CHECK(!commands.find("note.pin"));
    CHECK(commands.remove("file.save"));
    CHECK(commands.commands().empty());
}

TEST_CASE_FIXTURE(ui::test::Frame, "text_field_edits_at_the_cursor")
{
    ui::TextInput input;
    bool submitted = false;
    auto draw = [&] {
        render([&] {
            ui::textField(context, "name", {0, 0, 200, 30}, input, "Name", true, [&](Key key) {
                if (key != Key::Enter) return false;
                submitted = true;
                return true;
            });
        });
    };
    draw();
    click(10, 10);
    context.textInput("a\xc3\xa9"
                      "b");
    context.key(Key::Left);
    context.key(Key::Backspace);
    context.textInput("c\n");
    context.key(Key::Enter);
    CHECK(input.value == "acb");
    CHECK(input.cursor == 2);
    CHECK(submitted);
}

TEST_CASE_FIXTURE(ui::test::Frame, "list_draws_visible_rows_and_reveals_the_selection")
{
    std::vector<ui::ListItem> items(100);
    for (size_t i = 0; i < items.size(); ++i)
        items[i].id = items[i].title = "item" + std::to_string(i);
    items[9].enabled = false;
    ui::ListState state;
    std::string selected;
    auto draw = [&] {
        render([&] {
            ui::list(context, "list", {0, 0, 200, 100}, items, selected, state, [&](std::string id) { selected = std::move(id); }, 20);
        });
    };
    draw();
    context.pointerPosition(10, 10);
    CHECK(context.wheel(-3));
    CHECK(state.scroll == 60);

    draw();
    click(10, 90);
    CHECK(selected == "item7");

    // Selection moves past the viewport and over the disabled row.
    context.key(Key::Down);
    CHECK(selected == "item8");
    CHECK(state.scroll == 80);
    draw();
    context.key(Key::Down);
    CHECK(selected == "item10");
    CHECK(context.focused() == "list/item10");
    CHECK(state.scroll == 120);

    state.scroll = 1e6f;
    draw();
    CHECK(state.scroll == 1900);
}

TEST_CASE_FIXTURE(ui::test::Frame, "palette_filters_and_executes_the_selection")
{
    std::string ran;
    bool closed = false;
    ui::CommandRegistry commands;
    commands.add({.id = "file.open", .title = "Open File", .category = "File", .run = [&] {
                      ran = "open";
                  }});
    commands.add({.id = "file.save", .title = "Save File", .category = "File", .run = [&] {
                      ran = "save";
                  }});
    commands.add({.id = "view.zoom", .title = "Zoom In", .category = "View", .run = [&] {
                      ran = "zoom";
                  }});
    ui::CommandListState state;
    auto draw = [&] {
        render([&] { ui::commandPalette(context, viewport, commands, state, [&] { closed = true; }); });
    };
    draw();
    CHECK(context.focused() == "palette/search");
    context.textInput("file");
    context.key(Key::Down);
    context.key(Key::Enter);
    CHECK(ran == "save");
    CHECK(closed);
}

TEST_CASE_FIXTURE(ui::test::Frame, "scrollbar_drag_track_click_and_empty_content")
{
    float offset = 0;
    std::optional<float> grab;
    auto draw = [&](float content, bool enabled = true) {
        render([&] { ui::scrollbar(context, "scroll", {0, 0, 12, 100}, content, offset, grab, ui::Axis::Vertical, enabled); });
    };
    draw(400);
    context.pointerDown(6, 10);
    REQUIRE(grab.has_value());
    context.pointerMove(6, 100);
    CHECK(offset == 300);
    context.pointerUp(6, 100);
    CHECK(!grab.has_value());
    draw(400);
    click(6, 20);
    CHECK(offset == doctest::Approx(30));
    draw(400, false);
    click(6, 80);
    CHECK(offset == doctest::Approx(30));
    grab = 5;
    draw(100);
    CHECK(!grab.has_value());
    auto thumb = ui::scrollbarThumb({0, 0, 100, 12}, 100, 400, 150, ui::Axis::Horizontal);
    CHECK(thumb.x == doctest::Approx(37.5f));
    CHECK(thumb.w == 25);
    CHECK(ui::scrollbarThumb({0, 0, 12, 100}, 100, 100, 0).h == 0);
    offset = 0;
    render([&] { ui::scrollbar(context, "horizontal", {0, 0, 100, 12}, 400, offset, grab, ui::Axis::Horizontal); });
    context.pointerDown(10, 6);
    context.pointerMove(100, 6);
    CHECK(offset == 300);
    context.pointerUp(100, 6);
    CHECK(!grab.has_value());
}

TEST_CASE_FIXTURE(ui::test::Frame, "menu_unifies_pointer_keyboard_and_submenu_navigation")
{
    ui::CommandRegistry commands;
    commands.add({.id = "run", .title = "Run", .run = [] {
                  }});
    commands.add({.id = "off", .title = "Disabled", .run = [] {}, .available = [] {
                      return false;
                  }});
    ui::MenuDefinition definition{.items = {
                                      {.id = "off", .title = "Disabled", .command = "off"},
                                      {.id = "separator"},
                                      {.id = "run", .title = "Run", .command = "run"},
                                      {.id = "sub", .title = "Submenu", .items = {{.id = "child", .title = "Child", .command = "run"}}},
                                  }};
    size_t selection = 0, activated = 99;
    int backs = 0, closes = 0;
    auto draw = [&] {
        render([&] {
            ui::menu(context, "menu", viewport, {8, 0, 1, 1}, definition, selection, commands, [&](size_t index) { activated = index; }, [&] { ++backs; }, [&] { ++closes; });
        });
    };
    draw();
    context.key(Key::Down);
    CHECK(selection == 2);
    context.key(Key::Tab);
    CHECK(selection == 3);
    context.key(Key::Tab, {.shift = true});
    CHECK(selection == 2);
    context.key(Key::Enter);
    CHECK(activated == 2);
    context.pointerMove(30, 110);
    CHECK(selection == 3);
    draw();
    CHECK(selection == 3);
    context.key(Key::Up);
    CHECK(selection == 2);
    draw();
    CHECK(selection == 2);
    activated = 99;
    context.key(Key::Right);
    CHECK(activated == 99);
    context.key(Key::Down);
    context.key(Key::Right);
    CHECK(activated == 3);
    context.key(Key::Left);
    CHECK(backs == 1);
    context.key(Key::Escape);
    CHECK(backs == 2);
    click(30, 30);
    CHECK(activated == 3);
    click(350, 250);
    CHECK(closes == 1);
    draw();
    click(30, 75);
    CHECK(activated == 2);
    context.key(Key::Down);
    context.key(Key::Space);
    CHECK(activated == 3);
}

TEST_SUITE_END();
