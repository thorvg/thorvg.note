#include "common/frame.h"
#include <fstream>

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

TEST_CASE_FIXTURE(Editor, "markdown_headings_scale_relative_to_font_and_preserve_navigation")
{
    bounds.h = 280;
    REQUIRE(document.text("# title\ntext\n## subtitle\nlast"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    draw();
    editor.select(0, 0);
    auto h1 = editor.caretRect();
    editor.select(document.lineStart(1), document.lineStart(1));
    auto body = editor.caretRect();
    editor.select(document.lineStart(2), document.lineStart(2));
    auto h2 = editor.caretRect();
    CHECK(h1.h == doctest::Approx(body.h * 2));
    CHECK(h2.h == doctest::Approx(body.h * 1.65f));
    CHECK(body.y == doctest::Approx(h1.y + h1.h));
    context.focus("editor");
    draw();
    context.key(Key::Up);
    CHECK(document.lineAt(editor.session().selection().caret) == 1);
    context.key(Key::Down);
    CHECK(document.lineAt(editor.session().selection().caret) == 2);
    options.fontSize *= 1.5f;
    editor.options(options);
    editor.select(0, 0);
    CHECK(editor.caretRect().h == doctest::Approx(h1.h * 1.5f));
    editor.select(document.lineStart(1), document.lineStart(1));
    CHECK(editor.caretRect().h == doctest::Approx(body.h * 1.5f));
    options.markdown = false;
    editor.options(options);
    editor.select(0, 0);
    CHECK(editor.caretRect().h == doctest::Approx(body.h * 1.5f));
}

TEST_CASE_FIXTURE(Editor, "markdown_uses_theme_colors_and_keeps_line_number_size")
{
    bounds.h = 280;
    REQUIRE(document.text("# aaaa\naaaa\n- aaaa\n`aaaa`\n[aaaa](target)\n![aaaa](image)"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    theme.markdownHeading = {221, 37, 58};
    theme.markdownCode = {32, 169, 108};
    theme.markdownLink = {65, 77, 205};
    theme.markdownImage = {181, 45, 165};
    editor.theme(theme);
    CHECK(pixels(theme.markdownHeading) > 0);
    CHECK(pixels(theme.markdownCode) > 0);
    CHECK(pixels(theme.markdownLink) > 0);
    CHECK(pixels(theme.markdownImage) > 0);
    int numberTop = 0;
    auto numberHeight = [&](size_t offset) {
        editor.select(offset, offset);
        auto row = editor.caretRect();
        const auto& image = raster();
        int top = 300, bottom = -1;
        auto gutter = editor.textViewport().x;
        for (int y = int(bounds.y + row.y); y < std::min(300, int(bounds.y + row.y + row.h)); ++y)
            for (int x = int(bounds.x); x < int(bounds.x + gutter); ++x) {
                auto pixel = image[size_t(y) * 400 + x];
                auto r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
                if (r == theme.muted.r && g == theme.muted.g && b == theme.muted.b) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        numberTop = top - int(bounds.y + row.y);
        return bottom >= top ? bottom - top + 1 : 0;
    };
    auto headingNumber = numberHeight(0);
    auto headingTop = numberTop;
    auto bodyNumber = numberHeight(document.lineStart(1));
    CHECK(headingNumber > 0);
    CHECK(std::abs(headingTop - numberTop) <= 1);
    CHECK(std::abs(headingNumber - bodyNumber) <= 1);
    options.markdown = false;
    editor.options(options);
    CHECK(std::abs(headingNumber - numberHeight(0)) <= 1);
    options.markdown = true;
    editor.options(options);
    auto previous = theme.markdownHeading;
    theme.markdownHeading = {12, 181, 201};
    editor.theme(theme);
    CHECK(pixels(previous) == 0);
    CHECK(pixels(theme.markdownHeading) > 0);
}

TEST_CASE_FIXTURE(Editor, "markdown_reparses_edits_and_preserves_composition_and_decorations")
{
    REQUIRE(document.text("# title\nplain\nlast"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    context.focus("editor");
    draw();
    editor.select(0, 0);
    float headingHeight = editor.caretRect().h;
    std::string error;
    REQUIRE(editor.applyEdits({.revision = document.revision(), .edits = {{0, 2, ""}}}, error));
    CHECK(editor.caretRect().h == doctest::Approx(headingHeight / 2));
    CHECK(editor.undo());
    CHECK(editor.caretRect().h == doctest::Approx(headingHeight));
    editor.select(document.lineStart(1), document.lineStart(1));
    editor.composition("composed\ntext", 8);
    draw();
    CHECK(editor.composing());
    CHECK(editor.caretRect().h == doctest::Approx(headingHeight / 2));
    context.key(Key::Escape);
    draw();
    CHECK(!editor.composing());
    ui::TextDecoration decoration{0, 7};
    REQUIRE(editor.decorations("test", document.revision(), std::span(&decoration, 1)));
    CHECK(pixels(theme.highlight) > 0);
    CHECK(document.text() == "# title\nplain\nlast");
}

TEST_CASE_FIXTURE(Editor, "markdown_operators_and_operands_have_independent_colors")
{
    bounds.h = 280;
    REQUIRE(document.text("# aaaa\n**aaaa** *aaaa* ~~aaaa~~\n- aaaa\n1. aaaa\n`aaaa`\n[aaaa](aaaa)\n![aaaa](aaaa)\n> aaaa"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    ui::Color ui::Theme::*tokens[] = {
        &ui::Theme::markdownHeading, &ui::Theme::markdownHeadingMarker,
        &ui::Theme::markdownStrong, &ui::Theme::markdownStrongMarker,
        &ui::Theme::markdownEmphasis, &ui::Theme::markdownEmphasisMarker,
        &ui::Theme::markdownStrike, &ui::Theme::markdownStrikeMarker,
        &ui::Theme::markdownList, &ui::Theme::markdownListMarker,
        &ui::Theme::markdownCode, &ui::Theme::markdownCodeMarker,
        &ui::Theme::markdownLink, &ui::Theme::markdownLinkMarker,
        &ui::Theme::markdownImage, &ui::Theme::markdownImageMarker,
        &ui::Theme::markdownNumber, &ui::Theme::markdownNumberMarker,
        &ui::Theme::markdownQuote, &ui::Theme::markdownQuoteMarker};
    for (size_t i = 0; i < std::size(tokens); ++i)
        theme.*tokens[i] = {uint8_t(30 + i * 10), 57, 179};
    editor.theme(theme);
    for (auto token : tokens)
        CHECK(pixels(theme.*token) > 0);
    auto operand = theme.markdownHeading;
    auto marker = theme.markdownHeadingMarker;
    theme.markdownHeadingMarker = {19, 211, 83};
    editor.theme(theme);
    CHECK(pixels(marker) == 0);
    CHECK(pixels(theme.markdownHeadingMarker) > 0);
    CHECK(pixels(operand) > 0);
}

TEST_CASE_FIXTURE(Editor, "markdown_bold_falls_back_when_the_bold_face_lacks_hangul")
{
    // Multilingual input: a Latin-only bold face belongs to the document face's family.
    std::ifstream input(ui::test::fontPath("M1Plain.ttf"), std::ios::binary);
    REQUIRE(input);
    std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto tableCount = (uint8_t(bytes[4]) << 8) | uint8_t(bytes[5]);
    bool marked = false;
    for (int i = 0; i < tableCount; ++i) {
        auto at = size_t(12 + i * 16);
        auto tag = std::string_view(bytes.data() + at, 4);
        if (tag != "head" && tag != "hmtx") continue;
        size_t table = 0;
        for (size_t j = 8; j < 12; ++j)
            table = (table << 8) | uint8_t(bytes[at + j]);
        REQUIRE(table + 45 < bytes.size());
        if (tag == "head") {
            bytes[table + 45] |= 1;
            marked = true;
        } else {
            // Distinguish a missing glyph's advance from the fallback glyph's advance.
            bytes[table] = 0;
            bytes[table + 1] = 100;
        }
    }
    REQUIRE(marked);
    auto bold = ui::loadFont("M1Fallback", bytes.data(), bytes.size());
    REQUIRE(bold);
    REQUIRE(fonts.manager->find("M1Fallback", 700) == bold);
    REQUIRE(fonts.manager->glyph(fonts.fallback, 0xac00));
    REQUIRE(!fonts.manager->glyph(bold, 0xac00));
    editor.font(fonts.fallback);
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    theme.markdownHeading = {205, 44, 71};
    theme.markdownStrong = {71, 205, 44};
    editor.theme(theme);
    REQUIRE(document.text("# 가가\n가가\n**가가**\n# A가A가"));
    CHECK(pixels(theme.text) > 0);
    CHECK(pixels(theme.markdownHeading) > 0);
    CHECK(pixels(theme.markdownStrong) > 0);
    auto body = editor.rangeRects(document.lineStart(1), document.lineEnd(1));
    auto heading = editor.rangeRects(2, document.lineEnd(0));
    auto strong = editor.rangeRects(document.lineStart(2) + 2, document.lineEnd(2) - 2);
    REQUIRE(body.size() == 1);
    REQUIRE(heading.size() == 1);
    REQUIRE(strong.size() == 1);
    CHECK(heading.front().w == doctest::Approx(body.front().w * 2));
    CHECK(strong.front().w == doctest::Approx(body.front().w));
    auto start = document.lineStart(3) + 3;
    auto rects = editor.rangeRects(start, start + 3);
    REQUIRE(rects.size() == 1);
    CHECK(rects.front().w > 0);
}

TEST_SUITE_END();
