#include <common/doctest/doctest.h>
#include <optional>
#include <ui/ui.h>

namespace
{
const char* workspace = R"lua(
return {
  schema = 1,
  icons = {{name = "pin", file = "icons/pin.json", tint = false, poses = {hover = "over"}}},
  images = {{name = "logo", file = "logo.png"}},
  commands = {
    {id = "note.pin", title = "Pin Note", category = "Notes", run = function() app.commands.execute("view.toggle") end},
    {id = "note.fail", title = "Fail", run = function() error("broken") end},
  },
  build = function(state)
    assert(io == nil and os == nil and require == nil and load == nil and dofile == nil)
    return ui.column {
      ui.view {source = "titlebar", height = 40},
      ui.row {
        gap = 4,
        children = {
          ui.view {source = "documents", width = ui.preferred(state.sidebarWidth, 180, 340), resizable = true, resizeEdge = "trailing"},
          ui.view {source = "content", id = "main", visible = state.notes},
        },
      },
      ui.button {command = "note.pin", icon = ui.icons.star, style = "quiet", tooltip = state.tabs[2].title},
    }
  end,
}
)lua";

// Wraps a build function body in a minimal module.
std::string module(const char* body)
{
    return std::string("return {schema = 1, build = function(state) ") + body + " end}";
}
}  // namespace

TEST_SUITE_BEGIN("lua");

TEST_CASE("build_reads_the_declared_tree_and_host_state")
{
    ui::CommandRegistry host;
    ui::lua::Script script(host);
    std::string error;
    REQUIRE_MESSAGE(script.load(workspace, "workspace.lua", error), error);
    CHECK(!script.load(workspace, "workspace.lua", error));

    REQUIRE(script.icons().size() == 1);
    CHECK(script.icons()[0].file == "icons/pin.json");
    CHECK(!script.icons()[0].asset.tint);
    CHECK(script.icons()[0].asset.markers[1] == "over");
    CHECK(script.icons()[0].asset.markers[2] == "pressed");
    REQUIRE(script.images().size() == 1);
    CHECK(script.images()[0].name == "logo");

    ui::lua::Values state{{"notes", false}, {"sidebarWidth", 240.0}, {"tabs", std::vector<ui::lua::Record>{{{"title", std::string("One")}}, {{"title", std::string("Two")}}}}};
    ui::Element root;
    REQUIRE_MESSAGE(script.build(state, root, error), error);
    CHECK(root.kind == ui::ElementKind::Column);
    REQUIRE(root.children.size() == 3);
    CHECK(root.children[0].height.basis == 40);
    const auto& row = root.children[1];
    CHECK(row.gap == 4);
    REQUIRE(row.children.size() == 2);
    CHECK(row.children[0].width.basis == 240);
    CHECK(row.children[0].width.minimum == 180);
    CHECK(row.children[0].resizable);
    CHECK(row.children[1].id == "main");
    CHECK(!row.children[1].visible);
    const auto& button = root.children[2];
    CHECK(button.icon == "star");
    CHECK(button.style == ui::ButtonStyle::Quiet);
    CHECK(button.tooltip == "Two");

    ui::Composition composition;
    REQUIRE_MESSAGE(composition.apply(root, error), error);
    CHECK(composition.definition().children[1].children[0].id == "documents");
}

TEST_CASE("commands_queue_host_commands_and_outlive_the_script_safely")
{
    int toggled = 0;
    ui::CommandRegistry host;
    host.add({.id = "view.toggle", .title = "Toggle", .run = [&] {
                  ++toggled;
              }});
    std::optional<ui::lua::Script> script(std::in_place, host);
    std::string error;
    REQUIRE_MESSAGE(script->load(workspace, "workspace.lua", error), error);
    for (auto& command : script->commands()) {
        CHECK(command.owner == "workspace.lua");
        REQUIRE(host.add(std::move(command)));
    }

    CHECK(host.execute("note.pin"));
    CHECK(toggled == 1);
    CHECK(script->revision() == 1);
    CHECK(script->error().empty());
    CHECK(host.execute("note.fail"));
    CHECK(script->error().find("broken") != std::string::npos);

    script.reset();
    CHECK(host.execute("note.pin"));
    CHECK(toggled == 1);
    CHECK(host.release("workspace.lua") == 2);
}

TEST_CASE("invalid_modules_and_exhausted_limits_report_errors")
{
    struct Case
    {
        std::string source;
        const char* message;
        ui::lua::Limits limits;
    };
    const Case cases[] = {
        {"return {schema = 2, build = function() end}", "unsupported script schema", {}},
        {"return {schema = 1}", "requires build", {}},
        {"return string.dump(function() end)", "module table", {}},
        {module("return ui.view {source = 'a', colour = 'red'}"), "unknown field: colour", {}},
        {module("return ui.row {ui.view {source = 'a'}, children = {}}"), "children cannot be both", {}},
        {module("return ui.row {align = 'middle'}"), "align must be one of: start center end", {}},
        {module("return ui.row {ui.view {source = 'a'}, ui.view {source = 'a'}}"), "IDs must be nonempty and unique", {}},
        {module("app.commands.execute('x')"), "only available inside command handlers", {}},
        {module("while true do end"), "Lua instruction budget exceeded", {}},
        {module("local ok = pcall(function() while true do end end) return ui.view {source = 'a'}"), "Lua instruction budget exceeded", {.instructions = 5000}},
        {module("return ui.label {text = string.rep('x', 8 * 1024 * 1024)}"), "not enough memory", {.memory = 1024 * 1024}},
    };
    for (const auto& entry : cases) {
        CAPTURE(entry.source);
        ui::CommandRegistry host;
        ui::lua::Script script(host, entry.limits);
        ui::Element root;
        std::string error;
        CHECK(!(script.load(entry.source, "case.lua", error) && script.build({}, root, error)));
        CHECK_MESSAGE(error.find(entry.message) != std::string::npos, error);
        CHECK(root.children.empty());
    }
}

TEST_SUITE_END();
