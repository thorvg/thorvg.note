#include "common/frame.h"

namespace
{
ui::Element element(ui::ElementKind kind, std::string id = {})
{
    ui::Element node;
    node.kind = kind;
    node.id = std::move(id);
    return node;
}

ui::Element workspace(float sidebarWidth)
{
    auto sidebar = ui::view("sidebar", ui::preferred(sidebarWidth, 100, 300));
    sidebar.resizable = true;
    auto title = element(ui::ElementKind::Label);
    title.text = "Title";
    return ui::row({sidebar, ui::column({title, element(ui::ElementKind::Input, "query")})});
}
}  // namespace

TEST_SUITE_BEGIN("composition");

TEST_CASE("validation_names_the_offending_element")
{
    std::string error;
    CHECK(ui::validate(workspace(200), error));
    CHECK(error.empty());

    CHECK(!ui::validate(ui::row({ui::view("a"), ui::view("a")}), error));
    CHECK(error == "a: IDs must be nonempty and unique");
    CHECK(!ui::validate(ui::row({element(ui::ElementKind::Input)}), error));
    CHECK(error == "root/0: inputs, menus and resizable elements require an ID");
    CHECK(!ui::validate(element(ui::ElementKind::Button, "pin"), error));
    CHECK(error == "pin: button requires a command ID");
}

TEST_CASE_FIXTURE(ui::test::Frame, "apply_assigns_ids_and_keeps_state_by_id")
{
    ui::Composition composition;
    std::string error, resized;
    composition.resized = [&](std::string_view id, float size) {
        resized = std::string(id) + "=" + std::to_string(int(size));
    };
    REQUIRE(composition.apply(workspace(200), error));
    const auto& root = composition.definition();
    CHECK(root.id == "root");
    CHECK(root.children[0].id == "sidebar");
    CHECK(root.children[1].children[0].id == "root/1/0");
    CHECK(!composition.input("sidebar"));

    ui::CommandRegistry commands;
    auto draw = [&] {
        render([&] { composition.render(context, "ws", {0, 0, 400, 100}, {}, commands); });
    };
    draw();
    context.pointerDown(300, 75);
    context.textInput("abc");
    context.pointerUp(300, 75);
    context.pointerDown(200, 50);
    context.pointerMove(240, 50);
    context.pointerUp(240, 50);
    CHECK(resized == "sidebar=240");
    CHECK(composition.definition().children[0].width.basis == 240);

    // The same request keeps the user's size; a new one replaces it. Input text follows the ID.
    REQUIRE(composition.apply(workspace(200), error));
    CHECK(composition.definition().children[0].width.basis == 240);
    auto staged = composition;
    REQUIRE(staged.apply(workspace(220), error));
    CHECK(staged.definition().children[0].width.basis == 220);
    CHECK(staged.input("query")->value() == "abc");
    CHECK(composition.definition().children[0].width.basis == 240);

    CHECK(!composition.apply(ui::row({ui::view("a"), ui::view("a")}), error));
    CHECK(composition.input("query")->value() == "abc");
}

TEST_CASE_FIXTURE(ui::test::Frame, "elements_run_commands_and_host_views")
{
    int runs = 0;
    bool available = true;
    ui::CommandRegistry commands;
    commands.add({.id = "note.pin", .title = "Pin", .run = [&] { ++runs; }, .available = [&] {
                      return available;
                  }});
    auto pin = element(ui::ElementKind::Button);
    pin.command = "note.pin";
    pin.width = ui::fixed(100);

    ui::Composition composition;
    std::string error, instance;
    ui::Rect bounds;
    REQUIRE(composition.apply(ui::row({pin, ui::view("content")}), error));
    std::vector<ui::ViewType> types{{"content", [&](ui::Context&, ui::Rect rect, std::string_view id) {
                                         bounds = rect;
                                         instance = id;
                                     }}};
    auto draw = [&] {
        render([&] { composition.render(context, "ws", {0, 0, 400, 100}, types, commands); });
    };
    draw();
    CHECK(instance == "ws/content/content");
    CHECK(bounds.x == 100);
    CHECK(bounds.w == 300);
    click(10, 10);
    CHECK(runs == 1);

    available = false;
    draw();
    click(10, 10);
    CHECK(runs == 1);
}

TEST_SUITE_END();
