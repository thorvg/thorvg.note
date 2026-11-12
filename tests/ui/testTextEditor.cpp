#include "common/frame.h"

namespace
{
using ui::Key;

struct Editor : ui::test::Frame
{
    tn::Document document;
    ui::TextEditor editor{document};
    ui::Rect bounds{20, 10, 300, 200};
    std::vector<uint32_t> output;
    std::unique_ptr<tvg::SwCanvas> canvas;

    Editor()
    {
        editor.font(fonts.plain);
    }

    void draw()
    {
        render([&] { editor.render(context, "editor", bounds); });
        if (canvas) {
            REQUIRE(canvas->update() == tvg::Result::Success);
            REQUIRE(canvas->draw(true) == tvg::Result::Success);
            REQUIRE(canvas->sync() == tvg::Result::Success);
        }
    }

    const std::vector<uint32_t>& raster()
    {
        if (!canvas) {
            output.resize(400 * 300);
            canvas.reset(tvg::SwCanvas::gen());
            REQUIRE(canvas->target(output.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
            REQUIRE(canvas->add(context.scene()) == tvg::Result::Success);
        }
        draw();
        return output;
    }

    size_t pixels(ui::Color color)
    {
        const auto& pixels = raster();
        uint32_t value = uint32_t(color.a) << 24 | uint32_t(color.r) << 16 | uint32_t(color.g) << 8 | color.b;
        return size_t(std::count(pixels.begin(), pixels.end(), value));
    }
};
}  // namespace

TEST_SUITE_BEGIN("editor");

TEST_CASE_FIXTURE(Editor, "input_reaches_the_editor_through_the_context")
{
    int changes = 0;
    editor.onChange = [&] {
        ++changes;
    };
    draw();
    CHECK(!editor.focused());
    context.textInput("ignored");
    CHECK(document.text().empty());

    context.focus("editor");
    draw();
    CHECK(editor.focused());
    CHECK(editor.wantsTextInput());
    context.textInput("hello");
    context.key(Key::Enter);
    context.key(Key::Tab);
    context.textInput("world");
    context.key(Key::Backspace);
    CHECK(document.text() == "hello\n\tworl");
    CHECK(changes == 5);
    CHECK(context.focused() == "editor");

    context.key(Key::A, {.primary = true});
    CHECK(editor.session().selection().start() == 0);
    CHECK(editor.session().selection().end() == document.text().size());

    context.focus({});
    draw();
    CHECK(!editor.focused());
}

TEST_CASE_FIXTURE(Editor, "pointer_selects_and_wheel_scrolls_in_host_coordinates")
{
    std::string text = "alpha beta gamma";
    for (int i = 0; i < 100; ++i)
        text += "\nline";
    REQUIRE(document.text(text));
    editor.select(7, 7);
    draw();
    auto caret = editor.caretRect();
    float x = bounds.x + caret.x + 2, y = bounds.y + caret.y + caret.h / 2;

    context.pointerMove(x, y);
    CHECK(context.cursor() == ui::Cursor::Text);
    context.pointerDown(x, y, {}, 2);
    context.pointerUp(x, y);
    CHECK(editor.focused());
    CHECK(editor.session().selectedText() == "beta");

    CHECK(context.wheel(-3));
    CHECK(editor.scrollY() == doctest::Approx(caret.h * 3));
    draw();
    CHECK(editor.caretRect().y == doctest::Approx(caret.y - caret.h * 3));
    context.pointerPosition(bounds.x + bounds.w + 10, y);
    CHECK(!context.wheel(-3));
}

TEST_CASE_FIXTURE(Editor, "wheel_is_released_at_editor_scroll_boundaries")
{
    std::string text;
    for (int i = 0; i < 100; ++i)
        text += "line\n";
    REQUIRE(document.text(text));
    draw();
    context.pointerMove(bounds.x + 20, bounds.y + 20);
    CHECK(!context.wheel(1));
    CHECK(context.wheel(-0.25f));
    CHECK(editor.scrollY() == 0);
    CHECK(context.wheel(-1000));
    CHECK(editor.scrollY() > 0);
    CHECK(!context.wheel(-1));
    CHECK(context.wheel(1));
}

TEST_CASE_FIXTURE(Editor, "wheel_is_released_when_content_fits")
{
    REQUIRE(document.text("Short note"));
    draw();
    context.pointerMove(bounds.x + 20, bounds.y + 20);
    CHECK(!context.wheel(-1));
    context.focus("editor");
    CHECK(!context.wheel(1));
    CHECK(editor.scrollY() == 0);
}

TEST_CASE_FIXTURE(Editor, "composition_and_read_only_leave_the_document_unchanged")
{
    REQUIRE(document.text("note"));
    context.focus("editor");
    draw();
    editor.composition("ka", 2);
    CHECK(editor.composing());
    std::string error;
    CHECK(!editor.insert("x", error));
    CHECK(error == "IME composition is active");
    context.key(Key::Escape);
    CHECK(!editor.composing());

    auto options = editor.options();
    options.readOnly = true;
    editor.options(options);
    context.textInput("x");
    context.key(Key::Backspace);
    CHECK(!editor.insert("x", error));
    CHECK(error == "editor is read-only");
    CHECK(!editor.wantsTextInput());
    CHECK(document.text() == "note");
}

TEST_CASE_FIXTURE(Editor, "multibyte_layout")
{
    editor.font(fonts.fallback);
    std::string text = "A가中";
    for (size_t i = 0; i < 500; ++i)
        text += "\nA";
    REQUIRE(document.text(text));
    context.focus("editor");
    draw();

    editor.select(0, 0);
    auto start = editor.caretRect().x;
    editor.select(1, 1);
    auto latin = editor.caretRect().x;
    editor.select(4, 4);
    auto korean = editor.caretRect().x;
    editor.select(7, 7);
    auto chinese = editor.caretRect().x;
    CHECK(latin > start);
    CHECK(korean > latin);
    CHECK(chinese > korean);

    context.textInput("가");
    CHECK(document.text().substr(0, 10) == "A가中가");
    CHECK(editor.caretRect().x == doctest::Approx(chinese + korean - latin));
    REQUIRE(editor.undo());
    CHECK(document.text() == text);
    CHECK(editor.caretRect().x == doctest::Approx(chinese));
}

TEST_CASE_FIXTURE(Editor, "decoration_owners_coexist_and_follow_theme_changes")
{
    REQUIRE(document.text("     "));
    editor.theme(theme);
    auto revision = document.revision();
    std::array<ui::TextDecoration, 1> search{{{0, 2}}};
    std::array<ui::TextDecoration, 1> remote{{{3, 5, {.token = &ui::Theme::success}}}};
    REQUIRE(editor.decorations("search", revision, search));
    REQUIRE(editor.decorations("session/other", revision, remote));
    CHECK(pixels(theme.highlight) > 0);
    CHECK(pixels(theme.success) > 0);
    CHECK(editor.session().selection().empty());
    CHECK(document.revision() == revision);
    CHECK(!document.canUndo());

    auto previous = theme.highlight;
    theme.highlight = {193, 57, 111};
    editor.theme(theme);
    CHECK(pixels(previous) == 0);
    CHECK(pixels(theme.highlight) > 0);
    editor.clearDecorations("search");
    CHECK(pixels(theme.highlight) == 0);
    CHECK(pixels(theme.success) > 0);
}

TEST_CASE_FIXTURE(Editor, "decorations_reject_invalid_updates_and_hide_stale_revisions")
{
    REQUIRE(document.text("é    "));
    editor.theme(theme);
    std::array<ui::TextDecoration, 1> ranges{{{2, 6}}};
    auto revision = document.revision();
    REQUIRE(editor.decorations("owner", revision, ranges));
    std::array<ui::TextDecoration, 1> split{{{1, 3}}};
    CHECK(!editor.decorations("owner", revision, split));
    CHECK(!editor.decorations("owner", revision + 1, ranges));
    CHECK(!editor.decorations("", revision, ranges));
    CHECK(pixels(theme.highlight) > 0);
    std::string error;
    REQUIRE(editor.insert("x", error));
    CHECK(pixels(theme.highlight) == 0);
    ranges[0] = {3, 7};
    REQUIRE(editor.decorations("owner", document.revision(), ranges));
    CHECK(pixels(theme.highlight) > 0);
    REQUIRE(editor.undo());
    CHECK(pixels(theme.highlight) == 0);
}

TEST_CASE_FIXTURE(Editor, "overlapping_ranges_cover_scrolled_lines_and_expose_overlay_anchors")
{
    std::string text;
    for (size_t row = 0; row < 80; ++row)
        text += "     \n";
    REQUIRE(document.text(text));
    editor.theme(theme);
    std::array<ui::TextDecoration, 2> ranges{{{0, text.size(), {.token = &ui::Theme::success}}, {1, 2}}};
    REQUIRE(editor.decorations("overlap", document.revision(), ranges));
    draw();
    editor.scroll(0, 500);
    CHECK(pixels(theme.success) > 0);
    CHECK(editor.rangeRects(0, 1).empty());
    auto visible = editor.rangeRects(0, text.size());
    REQUIRE(!visible.empty());
    auto viewport = editor.textViewport();
    for (const auto& rect : visible) {
        CHECK(rect.y >= viewport.y);
        CHECK(rect.y + rect.h <= viewport.y + viewport.h);
    }
    context.focus("editor");
    draw();
    editor.composition("x", 1);
    REQUIRE(editor.composing());
    CHECK(editor.rangeRects(0, text.size()).empty());
}

TEST_CASE_FIXTURE(Editor, "decoration_borders_remain_visible_over_selections_and_carets_are_passive")
{
    REQUIRE(document.text("     "));
    editor.theme(theme);
    std::array<ui::TextDecoration, 2> ranges{{{0, 2, {.token = &ui::Theme::highlightActive}, {.token = &ui::Theme::accent}}, {4, 4, {.token = &ui::Theme::success}}}};
    REQUIRE(editor.decorations("remote", document.revision(), ranges));
    editor.select(0, 2);
    auto bordered = raster();
    ranges[0].border = {};
    REQUIRE(editor.decorations("remote", document.revision(), ranges));
    CHECK(bordered != raster());
    CHECK(pixels(theme.success) > 0);
    auto caret = editor.rangeRects(4, 4);
    REQUIRE(caret.size() == 1);
    CHECK(caret[0].w == doctest::Approx(1.5f));
    CHECK(editor.session().selection().end() == 2);
}

TEST_CASE_FIXTURE(Editor, "decoration_borders_keep_the_same_footprint_as_selections")
{
    REQUIRE(document.text("    \n    "));
    editor.theme(theme);
    auto options = editor.options();
    options.lineNumbers = false;
    for (float size : {16.0f, 17.5f, 9.0f}) {
        CAPTURE(size);
        options.fontSize = size;
        editor.options(options);
        for (auto range : {tn::Selection{0, 2}, tn::Selection{1, 7}}) {
            CAPTURE(range.anchor);
            CAPTURE(range.caret);
            editor.clearDecorations("match");
            editor.select(0, 0);
            auto plain = raster();
            editor.select(range.anchor, range.caret);
            auto selected = raster();
            editor.select(0, 0);
            ui::TextDecoration match{range.start(), range.end(), {.token = &ui::Theme::selection}, {.token = &ui::Theme::selection}};
            REQUIRE(editor.decorations("match", document.revision(), std::span(&match, 1)));
            const auto& highlighted = raster();
            size_t differences = 0;
            for (size_t i = 0; i < plain.size(); ++i)
                differences += (selected[i] != plain[i]) != (highlighted[i] != plain[i]);
            CHECK(differences == 0);
        }
    }
}

TEST_SUITE_END();
