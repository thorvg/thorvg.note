#include "common/frame.h"

namespace
{
struct Widgets : ui::test::Frame
{
    tn::Document document;
    ui::TextEditor editor{document};
    ui::Rect bounds{20, 10, 320, 200}, anchor, panel;
    size_t scans = 0, bytes = 0;

    Widgets() { editor.font(fonts.plain); }

    void draw()
    {
        render([&] { editor.render(context, "editor", bounds); });
    }

    ui::TextWidgetRule rule(ui::TextWidget::Display display = ui::TextWidget::Replace)
    {
        ui::TextWidgetRule result;
        result.match = [this, display](std::string_view text, size_t base, std::vector<ui::TextWidget>& ranges) {
            ++scans;
            bytes += text.size();
            for (size_t at = text.find(":ok:"); at != text.npos; at = text.find(":ok:", at + 4))
                ranges.push_back({base + at, base + at + 4, display, 1.5f});
        };
        result.draw = [this](ui::Context& host, std::string_view, ui::Rect rect, ui::State, std::string_view value) {
            CHECK(value == ":ok:");
            anchor = rect;
            host.rect(rect, theme.success, 3);
        };
        return result;
    }
};

std::shared_ptr<ui::IconAsset> animatedIcon()
{
    auto asset = std::make_shared<ui::IconAsset>();
    asset->name = "animated";
    asset->data = R"({"v":"5.7.4","w":24,"h":24,"fr":60,"ip":0,"op":60,"assets":[],"layers":[{"ty":4,"ind":1,"ip":0,"op":60,"st":0,"sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"s":{"a":0,"k":[100,100,100]},"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]}},"shapes":[{"ty":"el","p":{"a":1,"k":[{"t":0,"s":[6,12],"e":[18,12],"o":{"x":0.33,"y":0.33},"i":{"x":0.67,"y":0.67}},{"t":59,"s":[18,12]}]},"s":{"a":0,"k":[6,6]}},{"ty":"fl","c":{"a":0,"k":[1,0,0,1]},"o":{"a":0,"k":100},"r":1}]}]})";
    asset->tint = false;
    asset->loop = true;
    return asset;
}
}  // namespace

TEST_SUITE_BEGIN("text-widgets");

TEST_CASE_FIXTURE(Widgets, "looping_animations_accept_optional_idle_markers_and_ignore_pose_metadata")
{
    auto asset = animatedIcon();
    asset->markers.fill("rest");
    std::string markers;
    SUBCASE("absent")
    {
        markers = "[]";
    }
    SUBCASE("capitalized")
    {
        markers = R"([{"tm":30,"cm":"Rest","dr":0}])";
    }
    SUBCASE("unrelated_out_of_range")
    {
        markers = R"([{"tm":0,"cm":"rest","dr":0},{"tm":90,"cm":"","dr":0}])";
    }
    SUBCASE("duplicate")
    {
        markers = R"([{"tm":0,"cm":"rest","dr":0},{"tm":30,"cm":"rest","dr":0}])";
    }
    SUBCASE("invalid_idle_falls_back_to_frame_zero")
    {
        markers = R"([{"tm":90,"cm":"rest","dr":0}])";
    }
    asset->data.insert(asset->data.size() - 1, ",\"markers\":" + markers);
    std::string error;
    CHECK(ui::validateIcon(*asset, error));
    asset->loop = false;
    CHECK_FALSE(ui::validateIcon(*asset, error));
}

TEST_CASE_FIXTURE(Widgets, "lazy_animated_icons_preserve_color_and_stop_when_offscreen")
{
    auto asset = animatedIcon();
    size_t requests = 0;
    context.iconLoader([&](std::string_view name) -> std::shared_ptr<const ui::IconAsset> {
        ++requests;
        CHECK(name == "animated");
        return asset;
    });
    std::string error;
    REQUIRE(ui::validateIcon(*asset, error));
    std::vector<uint32_t> pixels(400 * 300);
    std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen());
    REQUIRE(canvas->target(pixels.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
    context.begin(theme, viewport, 0);
    context.icon("emoji", {20, 20, 96, 96}, "animated", theme.text, {}, 96);
    context.end();
    CHECK(requests == 1);
    CHECK(context.nextFrame().has_value());
    REQUIRE(canvas->add(context.scene()) == tvg::Result::Success);
    REQUIRE(canvas->update() == tvg::Result::Success);
    REQUIRE(canvas->draw(true) == tvg::Result::Success);
    REQUIRE(canvas->sync() == tvg::Result::Success);
    CHECK(std::count(pixels.begin(), pixels.end(), 0xffff0000u) > 20);
    auto before = pixels;
    context.begin(theme, viewport, .5);
    context.icon("emoji", {20, 20, 96, 96}, "animated", theme.text, {}, 96);
    context.end();
    REQUIRE(canvas->update() == tvg::Result::Success);
    REQUIRE(canvas->draw(true) == tvg::Result::Success);
    REQUIRE(canvas->sync() == tvg::Result::Success);
    CHECK(pixels != before);
    context.begin(theme, viewport, 1);
    context.end();
    CHECK_FALSE(context.nextFrame().has_value());
}

TEST_CASE_FIXTURE(Widgets, "replacement_and_after_widgets_preserve_source_geometry_and_undo")
{
    REQUIRE(document.text("A :ok: V\nnext"));
    auto definition = rule();
    SUBCASE("replacement") {}
    SUBCASE("after")
    {
        definition = rule(ui::TextWidget::After);
    }
    definition.activate = [](ui::TextEditor& editor, const ui::TextWidgetEvent& event) {
        std::string error;
        CHECK(editor.replaceWidget(event, "done", error));
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    draw();
    CHECK(anchor.w == doctest::Approx(editor.options().fontSize * 1.5f));
    CHECK(document.text() == "A :ok: V\nnext");
    float previous = -1;
    for (size_t offset = 0; offset <= document.lineEnd(0); ++offset) {
        editor.select(offset, offset);
        CHECK(editor.caretRect().x >= previous);
        previous = editor.caretRect().x;
    }
    draw();
    click(anchor.x + anchor.w / 2, anchor.y + anchor.h / 2);
    CHECK(document.text() == "A done V\nnext");
    REQUIRE(editor.undo());
    CHECK(document.text() == "A :ok: V\nnext");
    REQUIRE(editor.redo());
    CHECK(document.text() == "A done V\nnext");
}

TEST_CASE_FIXTURE(Widgets, "replacement_widgets_have_atomic_caret_stops_selection_and_deletion")
{
    REQUIRE(document.text("A :ok: V"));
    REQUIRE(editor.widgets("test", rule()));
    draw();
    context.focus("editor");
    draw();
    editor.select(2, 2);
    REQUIRE(editor.navigate("right"));
    CHECK(editor.session().selection().caret == 6);
    REQUIRE(editor.navigate("left"));
    CHECK(editor.session().selection().caret == 2);
    context.key(ui::Key::Right, {.shift = true});
    CHECK(editor.session().selectedText() == ":ok:");
    context.key(ui::Key::Backspace);
    CHECK(document.text() == "A  V");
    REQUIRE(editor.undo());
    editor.select(6, 6);
    context.key(ui::Key::Backspace);
    CHECK(document.text() == "A  V");
    REQUIRE(editor.undo());
    CHECK(editor.session().selection().empty());
    CHECK(editor.session().selection().caret == 6);
    editor.select(2, 2);
    context.key(ui::Key::Delete);
    CHECK(document.text() == "A  V");
    REQUIRE(editor.undo());
    editor.select(6, 6);
    context.key(ui::Key::Backspace, {.word = true});
    CHECK(document.text() == "A  V");
}

TEST_CASE_FIXTURE(Widgets, "widget_clipboard_representation_preserves_code_and_partial_source_ranges")
{
    REQUIRE(document.text(":ok:\n`:ok:`\n```\n:ok:\n```\n:ok:"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    auto definition = rule();
    definition.clipboard = [](std::string_view) {
        return std::string("\xe2\x9c\xa8");
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    std::string clipboard;
    editor.writeClipboard = [&](std::string_view text) {
        clipboard = text;
    };
    editor.selectAll();
    REQUIRE(editor.copy());
    CHECK(clipboard == "\xe2\x9c\xa8\n`:ok:`\n```\n:ok:\n```\n\xe2\x9c\xa8");
    editor.select(1, 3);
    REQUIRE(editor.copy());
    CHECK(clipboard == "ok");
    auto readonly = editor.options();
    readonly.readOnly = true;
    editor.options(readonly);
    editor.select(0, 4);
    REQUIRE(editor.copy());
    CHECK(clipboard == "\xe2\x9c\xa8");
}

TEST_CASE_FIXTURE(Widgets, "replacement_widgets_can_be_drag_selected_without_opening_a_popup")
{
    REQUIRE(document.text("A :ok: V"));
    bool opened = false;
    auto definition = rule();
    definition.popup = [&](ui::Context&, std::string_view, ui::Rect, ui::TextEditor&, const ui::TextWidgetEvent&) {
        opened = true;
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    draw();
    auto y = anchor.y + anchor.h / 2;
    context.pointerDown(anchor.x + anchor.w * .9f, y);
    context.pointerMove(anchor.x + anchor.w * .1f, y);
    context.pointerUp(anchor.x + anchor.w * .1f, y);
    draw();
    CHECK(editor.session().selectedText() == ":ok:");
    CHECK_FALSE(opened);
}

TEST_CASE_FIXTURE(Widgets, "multi_codepoint_widgets_keep_one_visual_editing_unit")
{
    std::string emoji = "\xe2\x9d\xa4\xef\xb8\x8f";
    REQUIRE(document.text("A " + emoji + " B"));
    auto definition = rule();
    definition.match = [&](std::string_view, size_t, std::vector<ui::TextWidget>& ranges) {
        ranges.push_back({2, 2 + emoji.size()});
    };
    definition.draw = [](ui::Context&, std::string_view, ui::Rect, ui::State, std::string_view) {
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    draw();
    context.focus("editor");
    draw();
    editor.select(2, 2);
    context.key(ui::Key::Right);
    CHECK(editor.session().selection().caret == 2 + emoji.size());
    context.key(ui::Key::Left);
    CHECK(editor.session().selection().caret == 2);
    context.key(ui::Key::Delete);
    CHECK(document.text() == "A  B");
    REQUIRE(editor.undo());
    CHECK(document.text() == "A " + emoji + " B");
    CHECK(editor.session().selection().caret == 2);
}

TEST_CASE_FIXTURE(Widgets, "widget_actions_reject_stale_readonly_and_composing_edits")
{
    REQUIRE(document.text(":ok:"));
    ui::TextWidgetEvent event{0, 4, document.revision(), {}, ":ok:"};
    std::string error;
    auto options = editor.options();
    options.readOnly = true;
    editor.options(options);
    CHECK_FALSE(editor.replaceWidget(event, "done", error));
    options.readOnly = false;
    editor.options(options);
    draw();
    context.focus("editor");
    draw();
    editor.composition("x", 1);
    REQUIRE(editor.composing());
    CHECK_FALSE(editor.replaceWidget(event, "done", error));
    editor.composition("", 0);
    REQUIRE(editor.insert("new ", error));
    CHECK_FALSE(editor.replaceWidget(event, "done", error));
    CHECK(error == "widget source changed");
}

TEST_CASE_FIXTURE(Widgets, "widgets_scan_only_measured_rows_and_reuse_matches_while_idle")
{
    std::string source;
    for (size_t i = 0; i < 100000; ++i)
        source += "row :ok: text\n";
    REQUIRE(document.text(source));
    REQUIRE(editor.widgets("test", rule()));
    draw();
    CHECK(scans < 100);
    CHECK(bytes < 4096);
    auto before = scans;
    draw();
    draw();
    CHECK(scans == before);
    editor.scroll(0, 100000);
    draw();
    CHECK(scans > before);
    CHECK(scans < 200);
    editor.clearWidgets("test");
    before = scans;
    draw();
    CHECK(scans == before);
}

TEST_CASE_FIXTURE(Widgets, "popup_edits_are_anchored_and_dismiss_when_the_source_changes")
{
    REQUIRE(document.text("A :ok: V"));
    auto definition = rule();
    definition.popup = [this](ui::Context& host, std::string_view id, ui::Rect rect, ui::TextEditor& editor, const ui::TextWidgetEvent& event) {
        panel = rect;
        ui::button(host, std::string(id) + "/apply", {rect.x, rect.y, 60, 28}, "Apply", [&editor, event] {
            std::string error;
            CHECK(editor.replaceWidget(event, "done", error));
        });
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    draw();
    click(anchor.x + 2, anchor.y + 2);
    draw();
    CHECK(panel.w > 0);
    CHECK(panel.x >= viewport.x);
    CHECK(panel.y >= viewport.y);
    click(panel.x + 20, panel.y + 12);
    CHECK(document.text() == "A done V");
    draw();
    CHECK(context.focused() == "editor");
    REQUIRE(editor.undo());
    draw();
    click(anchor.x + 2, anchor.y + 2);
    draw();
    context.key(ui::Key::Escape);
    panel = {};
    draw();
    CHECK(panel.w == 0);
    CHECK(context.focused() == "editor");
}

TEST_CASE_FIXTURE(Widgets, "invalid_widget_ranges_are_ignored_and_rule_updates_are_transactional")
{
    REQUIRE(document.text("A가:ok:"));
    auto definition = rule();
    CHECK_FALSE(editor.widgets("", definition));
    auto invalid = definition;
    invalid.draw = {};
    CHECK_FALSE(editor.widgets("test", invalid));
    invalid = definition;
    invalid.popupWidth = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(editor.widgets("test", invalid));
    definition.match = [](std::string_view, size_t, std::vector<ui::TextWidget>& result) {
        result = {{1, 2}, {0, 500}, {0, 0}, {4, 8, ui::TextWidget::Replace, -1}};
    };
    REQUIRE(editor.widgets("test", definition));
    draw();
    CHECK(anchor.w == 0);
}

TEST_CASE_FIXTURE(Widgets, "cached_widget_ranges_follow_prefix_edits_and_respect_readonly_controls")
{
    REQUIRE(document.text("first\nrow :ok: end"));
    auto definition = rule();
    size_t activations = 0;
    definition.activate = [&](ui::TextEditor& editor, const ui::TextWidgetEvent& event) {
        ++activations;
        std::string error;
        CHECK(editor.replaceWidget(event, "done", error));
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    draw();
    std::string error;
    REQUIRE(editor.applyEdits({.revision = document.revision(), .edits = {{0, 0, "prefix "}}}, error));
    draw();
    click(anchor.x + 2, anchor.y + 2);
    CHECK(document.text() == "prefix first\nrow done end");
    CHECK(activations == 1);
    REQUIRE(editor.undo());
    auto options = editor.options();
    options.readOnly = true;
    editor.options(options);
    draw();
    click(anchor.x + 2, anchor.y + 2);
    CHECK(activations == 1);
    CHECK(document.text() == "prefix first\nrow :ok: end");
}

TEST_CASE_FIXTURE(Widgets, "multiline_composition_keeps_following_widget_source_offsets_valid")
{
    REQUIRE(document.text("first :ok:\nsecond :ok:"));
    REQUIRE(editor.widgets("test", rule()));
    draw();
    context.focus("editor");
    draw();
    editor.select(0, 0);
    editor.composition("long composition\nnew row", 24);
    REQUIRE(editor.composing());
    draw();
    CHECK(document.text() == "first :ok:\nsecond :ok:");
    editor.composition("", 0);
    draw();
}

TEST_CASE_FIXTURE(Widgets, "composition_preserves_widgets_outside_the_replaced_source_range")
{
    std::string source = ":ok: A :ok: B :ok:\nlast :ok:";
    REQUIRE(document.text(source));
    size_t painted = 0;
    auto definition = rule();
    definition.draw = [&](ui::Context&, std::string_view, ui::Rect, ui::State, std::string_view value) {
        CHECK(value == ":ok:");
        ++painted;
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    draw();
    context.focus("editor");
    draw();
    editor.select(5, 6);
    for (const auto* text : {"x", "longer", "x\nnew row", ":ok:\ntext"}) {
        painted = 0;
        editor.composition(text, std::strlen(text));
        draw();
        CHECK(painted == 4);
        CHECK(document.text() == source);
    }
    editor.composition("", 0);
    editor.select(7, 11);
    editor.composition("replaced", 8);
    painted = 0;
    draw();
    CHECK(painted == 3);
    editor.composition("", 0);
    painted = 0;
    draw();
    CHECK(painted == 4);
    CHECK(document.text() == source);
}

TEST_CASE_FIXTURE(Widgets, "suggestions_filter_while_typing_keep_focus_and_commit_as_one_edit")
{
    size_t suggestions = 0, paints = 0;
    std::string query;
    auto definition = rule();
    definition.suggest = [&](std::string_view prefix) {
        ++suggestions;
        return prefix == ":" || prefix == ":o" ? 0 : prefix.npos;
    };
    definition.popupHeight = 48;
    definition.popupPadding = 6;
    definition.popup = [&](ui::Context&, std::string_view, ui::Rect rect, ui::TextEditor&, const ui::TextWidgetEvent& event) {
        ++paints;
        panel = rect;
        query = event.value;
    };
    definition.popupKey = [](ui::Key key, ui::TextEditor& editor, const ui::TextWidgetEvent& event) {
        if (key != ui::Key::Enter) return false;
        std::string error;
        CHECK(editor.replaceWidget(event, ":ok:", error));
        return true;
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    draw();
    context.focus("editor");
    draw();
    context.textInput(":");
    draw();
    CHECK(query == ":");
    CHECK(panel.h == 36);
    CHECK(context.focused() == "editor");
    auto scans = suggestions;
    draw();
    CHECK(suggestions == scans);
    context.textInput("o");
    draw();
    CHECK(query == ":o");
    context.key(ui::Key::Enter);
    draw();
    CHECK(document.text() == ":ok:");
    CHECK(context.focused() == "editor");
    REQUIRE(editor.undo());
    CHECK(document.text() == ":o");
    draw();
    context.key(ui::Key::Escape);
    auto previous = paints;
    draw();
    draw();
    CHECK(paints == previous);
    context.textInput("z");
    draw();
    CHECK(document.text() == ":oz");
    CHECK(paints == previous);
}

TEST_CASE_FIXTURE(Widgets, "suggestions_respect_code_readonly_and_composition")
{
    bool painted = false;
    auto definition = rule();
    definition.suggest = [](std::string_view text) {
        return text.rfind(':');
    };
    definition.popup = [&](ui::Context&, std::string_view, ui::Rect, ui::TextEditor&, const ui::TextWidgetEvent&) {
        painted = true;
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    REQUIRE(document.text("```md\n:\n```"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    editor.select(7, 7);
    draw();
    context.focus("editor");
    draw();
    CHECK_FALSE(painted);
    REQUIRE(document.text(":"));
    editor.select(1, 1);
    options.readOnly = true;
    editor.options(options);
    draw();
    CHECK_FALSE(painted);
    options.readOnly = false;
    editor.options(options);
    editor.composition("x", 1);
    draw();
    CHECK_FALSE(painted);
    editor.composition("", 0);
    draw();
    CHECK(painted);
}

TEST_CASE_FIXTURE(Widgets, "literal_widget_colors_are_applied_to_retained_text")
{
    REQUIRE(document.text("A :ok: V"));
    auto definition = rule(ui::TextWidget::After);
    definition.match = [](std::string_view, size_t, std::vector<ui::TextWidget>& result) {
        result.push_back({2, 6, ui::TextWidget::After, 1.5f, ui::Color{80, 123, 201}, ui::Color{175, 132, 54, 28}});
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    draw();
    std::vector<uint32_t> pixels(400 * 300);
    std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen());
    REQUIRE(canvas->target(pixels.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
    REQUIRE(canvas->add(context.scene()) == tvg::Result::Success);
    REQUIRE(canvas->update() == tvg::Result::Success);
    REQUIRE(canvas->draw(true) == tvg::Result::Success);
    REQUIRE(canvas->sync() == tvg::Result::Success);
    CHECK(std::count(pixels.begin(), pixels.end(), 0xff507bc9u) > 0);
    CHECK(document.text() == "A :ok: V");
}

TEST_SUITE_END();

TEST_CASE_FIXTURE(Widgets, "below_widgets_reserve_space_without_enlarging_text_or_carets")
{
    bounds.h = 280;
    REQUIRE(document.text("A :ok: V\nnext"));
    auto options = editor.options();
    options.lineNumbers = true;
    options.markdown = true;
    editor.options(options);
    auto definition = rule(ui::TextWidget::Below);
    definition.block = true;
    REQUIRE(editor.widgets("preview", std::move(definition)));
    draw();
    auto first = editor.rangeRects(0, 1).front();
    auto second = editor.rangeRects(document.lineStart(1), document.lineEnd(1)).front();
    CHECK(anchor.y >= bounds.y + first.y + first.h);
    CHECK(anchor.h == doctest::Approx(options.fontSize * 12));
    CHECK(bounds.y + second.y >= anchor.y + anchor.h);
    editor.select(0, 0);
    CHECK(editor.caretRect().h == doctest::Approx(first.h));
    for (size_t offset = 0; offset <= document.lineEnd(0); ++offset) {
        editor.select(offset, offset);
        CHECK(editor.caretRect().h == doctest::Approx(first.h));
    }
    auto original = document.text();
    REQUIRE(editor.applyEdits({.revision = document.revision(), .edits = {{2, 6, "plain"}}}, original));
    draw();
    auto collapsed = editor.rangeRects(document.lineStart(1), document.lineEnd(1)).front();
    CHECK(collapsed.y == doctest::Approx(first.y + first.h));
    REQUIRE(editor.undo());
    draw();
    CHECK(editor.rangeRects(document.lineStart(1), document.lineEnd(1)).front().y == doctest::Approx(second.y));
    options.fontSize *= 1.5f;
    editor.options(options);
    draw();
    CHECK(anchor.h == doctest::Approx(options.fontSize * 12));
}

TEST_CASE_FIXTURE(Widgets, "below_widgets_skip_code_and_keep_offscreen_row_positions_stable")
{
    std::string source = "`:ok:`\n```\n:ok:\n```\n:ok:\n";
    for (size_t i = 0; i < 200; ++i)
        source += "plain\n";
    source += ":ok:\nlast";
    REQUIRE(document.text(source));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    auto definition = rule(ui::TextWidget::Below);
    definition.block = true;
    REQUIRE(editor.widgets("preview", std::move(definition)));
    draw();
    CHECK(editor.rangeRects(document.lineStart(1), document.lineEnd(1)).front().y < anchor.y);
    editor.select(document.text().size(), document.text().size());
    draw();
    auto before = editor.caretRect();
    draw();
    CHECK(editor.caretRect().y == doctest::Approx(before.y));
    CHECK(editor.caretRect().h == doctest::Approx(before.h));
    CHECK(anchor.y + anchor.h <= bounds.y + before.y);
}

TEST_CASE_FIXTURE(Widgets, "below_widgets_activate_from_the_preview_without_selecting_source")
{
    REQUIRE(document.text(":ok:\nnext"));
    auto definition = rule(ui::TextWidget::Below);
    definition.block = true;
    bool activated = false;
    definition.activate = [&](ui::TextEditor&, const ui::TextWidgetEvent& event) {
        activated = true;
        CHECK(event.value == ":ok:");
    };
    REQUIRE(editor.widgets("preview", std::move(definition)));
    draw();
    click(anchor.x + 10, anchor.y + 10);
    CHECK(activated);
    CHECK(editor.session().selection().empty());
    CHECK(document.text() == ":ok:\nnext");
}

TEST_CASE_FIXTURE(Widgets, "gif_image_instances_animate_and_stop_when_removed")
{
    const unsigned char bytes[]{
        'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0, 255, 0, 0, 0, 0, 255,
        0x21, 0xff, 11, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 3, 1, 0, 0, 0,
        0x21, 0xf9, 4, 0, 10, 0, 0, 0, 0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x44, 1, 0,
        0x21, 0xf9, 4, 0, 10, 0, 0, 0, 0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x4c, 1, 0, 0x3b};
    ui::Images images;
    std::string error;
    REQUIRE(images.replace({{"test", std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes)), "gif"}}, error));
    std::vector<uint32_t> pixels(400 * 300);
    std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen());
    REQUIRE(canvas->target(pixels.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
    auto drawImage = [&](double time, std::string_view id = "instance") {
        context.begin(theme, viewport, time);
        images.begin();
        images.render(context, id, "test", {20, 20, 40, 40}, ui::ImageFit::Contain);
        context.end();
        images.end();
        CHECK(context.nextFrame().has_value());
        REQUIRE(canvas->update() == tvg::Result::Success);
        REQUIRE(canvas->draw(true) == tvg::Result::Success);
        REQUIRE(canvas->sync() == tvg::Result::Success);
    };
    REQUIRE(canvas->add(context.scene()) == tvg::Result::Success);
    drawImage(0);
    auto first = context.scene()->paints().front();
    first->ref();
    CHECK(std::count(pixels.begin(), pixels.end(), 0xffff0000u) > 100);
    drawImage(.15);
    CHECK(std::count(pixels.begin(), pixels.end(), 0xff0000ffu) > 100);
    context.begin(theme, viewport, 1);
    images.begin();
    context.end();
    images.end();
    CHECK_FALSE(context.nextFrame().has_value());
    drawImage(2, "reentered");
    CHECK(first == context.scene()->paints().front());
    CHECK(std::count(pixels.begin(), pixels.end(), 0xffff0000u) > 100);
    first->unref();
}

TEST_CASE_FIXTURE(Widgets, "looping_icons_sample_authored_frames_across_fractional_layer_boundaries")
{
    auto asset = animatedIcon();
    auto first = asset->data.find("\"layers\":[") + 10;
    auto count = asset->data.size() - first - 2;
    auto before = asset->data.substr(first, count), after = before;
    before.replace(before.find("\"op\":60"), 7, "\"op\":29.5");
    after.replace(after.find("\"ip\":0"), 6, "\"ip\":30");
    asset->data.replace(first, count, before + ',' + after);
    context.iconLoader([&](std::string_view) -> std::shared_ptr<const ui::IconAsset> { return asset; });
    std::vector<uint32_t> pixels(400 * 300);
    std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen(tvg::EngineOption::SmartRender));
    REQUIRE(canvas->target(pixels.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
    REQUIRE(canvas->add(context.scene()) == tvg::Result::Success);
    for (double now : {0.0, .499, .4999, .5, 1.499}) {
        context.begin(theme, viewport, now);
        context.icon("emoji", {20, 20, 96, 96}, "animated", theme.text, {}, 96);
        context.end();
        REQUIRE(canvas->update() == tvg::Result::Success);
        REQUIRE(canvas->draw(false) == tvg::Result::Success);
        REQUIRE(canvas->sync() == tvg::Result::Success);
        CHECK(std::count(pixels.begin(), pixels.end(), 0xffff0000u) > 20);
    }
}

TEST_CASE_FIXTURE(Widgets, "visible_widget_identity_survives_prefix_edits_undo_and_composition")
{
    REQUIRE(document.text("prefix :ok: suffix"));
    auto definition = rule();
    std::string identity;
    auto paint = definition.draw;
    definition.draw = [&](ui::Context& host, std::string_view id, ui::Rect rect, ui::State state, std::string_view value) {
        identity = id;
        paint(host, id, rect, state, value);
    };
    REQUIRE(editor.widgets("test", std::move(definition)));
    draw();
    auto original = identity;
    std::string error;
    REQUIRE(editor.applyEdits({.revision = document.revision(), .edits = {{0, 0, "before "}}}, error));
    draw();
    CHECK(identity == original);
    REQUIRE(editor.undo());
    draw();
    CHECK(identity == original);
    REQUIRE(editor.redo());
    draw();
    CHECK(identity == original);
    context.focus("editor");
    editor.select(0, 0);
    draw();
    editor.composition("x", 1);
    draw();
    CHECK(identity == original);
    editor.composition("", 0);
    draw();
    CHECK(identity == original);
}

TEST_CASE_FIXTURE(Widgets, "below_previews_keep_fractional_wheel_distance_and_visible_anchors_stable")
{
    std::string source;
    for (size_t i = 0; i < 35; ++i)
        source += "body\n# heading\n:ok:\nbody\nbody\n";
    REQUIRE(document.text(source));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    auto definition = rule(ui::TextWidget::Below);
    definition.block = true;
    std::map<std::string, ui::Rect> previews;
    auto paint = definition.draw;
    definition.draw = [&](ui::Context& host, std::string_view id, ui::Rect rect, ui::State state, std::string_view value) {
        previews[std::string(id)] = rect;
        paint(host, id, rect, state, value);
    };
    REQUIRE(editor.widgets("preview", std::move(definition)));
    draw();
    auto step = editor.caretRect().h;
    context.pointerMove(bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
    for (int i = 0; i < 900; ++i) {
        float delta = i < 600 ? -.25f : .25f;
        auto previous = editor.scrollY();
        auto before = previews;
        REQUIRE(context.wheel(delta));
        auto requested = previous - delta * step;
        CHECK(editor.scrollY() == doctest::Approx(requested));
        previews.clear();
        draw();
        CHECK(editor.scrollY() == doctest::Approx(requested));
        for (const auto& [id, rect] : previews) {
            auto found = before.find(id);
            if (found != before.end()) CHECK(rect.y == doctest::Approx(found->second.y + delta * step).epsilon(.0001));
        }
    }
}

TEST_CASE_FIXTURE(Widgets, "image_previews_use_static_pictures_and_reuse_decoded_lottie_timelines")
{
    auto asset = animatedIcon();
    bool animated = true;
    std::string mime = "json", data = asset->data;
    SUBCASE("lottie") {}
    SUBCASE("svg")
    {
        animated = false;
        mime = "svg+xml";
        data = R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><circle cx="12" cy="12" r="4" fill="#ff0000"/></svg>)";
    }
    ui::Images images;
    std::string error;
    REQUIRE(images.replace({{"preview", data, mime}}, error));
    std::vector<uint32_t> pixels(400 * 300);
    std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen(tvg::EngineOption::SmartRender));
    REQUIRE(canvas->target(pixels.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
    REQUIRE(canvas->add(context.scene()) == tvg::Result::Success);
    auto show = [&](double time, std::string_view id) {
        context.begin(theme, viewport, time);
        images.begin();
        images.render(context, id, "preview", {20, 20, 96, 96}, ui::ImageFit::Contain);
        context.end();
        images.end();
        CHECK(context.nextFrame().has_value() == animated);
        REQUIRE(canvas->update() == tvg::Result::Success);
        REQUIRE(canvas->draw(false) == tvg::Result::Success);
        REQUIRE(canvas->sync() == tvg::Result::Success);
    };
    show(0, "first");
    CHECK(std::count(pixels.begin(), pixels.end(), 0xffff0000u) > 100);
    auto initial = pixels;
    show(.5, "first");
    CHECK((pixels != initial) == animated);
    auto first = context.scene()->paints().front();
    first->ref();
    context.begin(theme, viewport, 1);
    images.begin();
    context.end();
    images.end();
    CHECK_FALSE(context.nextFrame().has_value());
    show(2, "reentered");
    CHECK((first == context.scene()->paints().front()) == animated);
    CHECK((pixels == initial));
    first->unref();
}
