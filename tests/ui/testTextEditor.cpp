#include "common/frame.h"
#include <fstream>

namespace
{
using ui::Key;

struct TextPose
{
    tvg::Text* text;
    tvg::Matrix transform;
    tvg::Scene* parent;
    tvg::Matrix parentTransform;
};

void textPoses(tvg::Scene* scene, std::vector<TextPose>& result, float x = 0, float y = 0)
{
    auto parent = scene->transform();
    parent.e13 += x;
    parent.e23 += y;
    for (auto paint : scene->paints()) {
        if (paint->type() == tvg::Type::Scene) textPoses(static_cast<tvg::Scene*>(paint), result, parent.e13, parent.e23);
        else if (paint->type() == tvg::Type::Text) result.push_back({static_cast<tvg::Text*>(paint), paint->transform(), scene, parent});
    }
}

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

    void draw(bool clear = false)
    {
        render([&] { editor.render(context, "editor", bounds); });
        if (canvas) {
            REQUIRE(canvas->update() == tvg::Result::Success);
            REQUIRE(canvas->draw(clear) == tvg::Result::Success);
            REQUIRE(canvas->sync() == tvg::Result::Success);
        }
    }

    const std::vector<uint32_t>& raster()
    {
        if (!canvas) {
            output.resize(400 * 300);
            canvas.reset(tvg::SwCanvas::gen(tvg::EngineOption::SmartRender));
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

TEST_CASE_FIXTURE(Editor, "markdown_shortcuts_edit_source_and_restore_selection")
{
    using Kind = ui::MarkdownFormat;
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    SUBCASE("empty markers, continued typing and exit")
    {
        for (auto [kind, marker] : {std::pair{Kind::Bold, "**"}, {Kind::Italic, "*"}, {Kind::Code, "`"}, {Kind::Strike, "~~"}}) {
            REQUIRE(document.text(""));
            editor.select(0, 0);
            REQUIRE(editor.formatMarkdown(kind));
            CHECK(document.text() == std::string(marker) + marker);
            CHECK(editor.session().selection().caret == std::string_view(marker).size());
            REQUIRE(editor.formatMarkdown(kind));
            CHECK(document.text().empty());
            REQUIRE(editor.formatMarkdown(kind));
            std::string error;
            REQUIRE(editor.insert("word", error));
            REQUIRE(editor.formatMarkdown(kind));
            CHECK(editor.session().selection().caret == document.text().size());
            REQUIRE(editor.insert(".", error));
            CHECK(document.text() == std::string(marker) + "word" + marker + ".");
        }
    }
    SUBCASE("existing delimiters and undo")
    {
        REQUIRE(document.text("__bold__ and _italic_"));
        editor.select(6, 2);
        REQUIRE(editor.formatMarkdown(Kind::Bold));
        CHECK(document.text() == "bold and _italic_");
        REQUIRE(editor.undo());
        CHECK(document.text() == "__bold__ and _italic_");
        CHECK(editor.session().selection().anchor == 6);
        CHECK(editor.session().selection().caret == 2);
        REQUIRE(editor.redo());
        editor.select(9, 17);
        REQUIRE(editor.formatMarkdown(Kind::Italic));
        CHECK(document.text() == "bold and italic");
    }
    SUBCASE("inline code containing backticks")
    {
        REQUIRE(document.text("Use ` or `` here"));
        editor.select(4, 11);
        REQUIRE(editor.formatMarkdown(Kind::Code));
        CHECK(document.text() == "Use ``` ` or `` ``` here");
        CHECK(editor.session().selectedText() == "` or ``");
        REQUIRE(editor.formatMarkdown(Kind::Code));
        CHECK(document.text() == "Use ` or `` here");
    }
    SUBCASE("unformat a word without changing surrounding bold text")
    {
        REQUIRE(document.text("**one two three**"));
        editor.select(9, 6);
        REQUIRE(editor.formatMarkdown(Kind::Bold));
        CHECK(document.text() == "**one** two **three**");
        CHECK(editor.session().selectedText() == "two");
        REQUIRE(document.text("**word**"));
        editor.select(2, 6);
        REQUIRE(editor.formatMarkdown(Kind::Italic));
        CHECK(document.text() == "***word***");
        REQUIRE(editor.formatMarkdown(Kind::Italic));
        CHECK(document.text() == "**word**");
        REQUIRE(document.text("a ~word~ here"));
        editor.select(3, 7);
        REQUIRE(editor.formatMarkdown(Kind::Strike));
        CHECK(document.text() == "a word here");
    }
    SUBCASE("literal code and an existing thematic break")
    {
        REQUIRE(document.text("```\ncode\n```\n\n****"));
        editor.select(4, 8);
        CHECK_FALSE(editor.formatMarkdown(Kind::Bold));
        CHECK_FALSE(editor.formatMarkdown(Kind::Code));
        editor.select(15, 15);
        REQUIRE(editor.formatMarkdown(Kind::Bold));
        CHECK(document.text().ends_with("********"));
    }
    SUBCASE("links select the destination for editing")
    {
        REQUIRE(document.text("See [example](url)"));
        editor.select(7, 7);
        REQUIRE(editor.formatMarkdown(Kind::Link));
        CHECK(editor.session().selectedText() == "url");
        editor.select(0, 0);
        REQUIRE(editor.formatMarkdown(Kind::Link));
        CHECK(document.text().starts_with("[]()See"));
        CHECK(editor.session().selection().caret == 1);
    }
    SUBCASE("line formats preserve indentation, checked items and the next line")
    {
        REQUIRE(document.text("> - [x] done\n>   next\nlast"));
        auto end = document.text().find("last");
        editor.select(end, 0);
        REQUIRE(editor.formatMarkdown(Kind::TaskList));
        CHECK(document.text() == "> - [x] done\n>   - [ ] next\nlast");
        REQUIRE(editor.formatMarkdown(Kind::OrderedList));
        CHECK(document.text() == "> 1. done\n>   2. next\nlast");
        REQUIRE(editor.formatMarkdown(Kind::OrderedList));
        CHECK(document.text() == "> done\n>   next\nlast");
        REQUIRE(editor.undo());
        CHECK(document.text() == "> 1. done\n>   2. next\nlast");
    }
    SUBCASE("disabled outside editable Markdown or during composition")
    {
        REQUIRE(document.text("original"));
        editor.select(0, 8);
        options.markdown = false;
        editor.options(options);
        CHECK_FALSE(editor.formatMarkdown(Kind::Bold));
        options.markdown = options.readOnly = true;
        editor.options(options);
        CHECK_FALSE(editor.formatMarkdown(Kind::Bold));
        options.readOnly = false;
        editor.options(options);
        context.focus("editor");
        draw();
        editor.composition("한", 3);
        REQUIRE(editor.composing());
        CHECK_FALSE(editor.formatMarkdown(Kind::Bold));
        CHECK(document.text() == "original");
    }
}

TEST_CASE_FIXTURE(Editor, "checked_task_styles_follow_edits_and_undo")
{
    REQUIRE(document.text("- [ ] Finish **the note**\n- [ ] Keep editing\n"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    theme.markdownStrike = {245, 20, 190};
    editor.theme(theme);
    CHECK(pixels(theme.markdownStrike) == 0);
    editor.select(3, 4);
    std::string error;
    REQUIRE(editor.insert("x", error));
    CHECK(pixels(theme.markdownStrike) > 0);
    REQUIRE(editor.undo());
    CHECK(pixels(theme.markdownStrike) == 0);
    REQUIRE(editor.redo());
    CHECK(pixels(theme.markdownStrike) > 0);
}

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
    CHECK(editor.caretRect().y == doctest::Approx(caret.y - std::round(caret.h * 3)));
    context.pointerPosition(bounds.x + bounds.w + 10, y);
    CHECK(!context.wheel(-3));
}

TEST_CASE_FIXTURE(Editor, "double_click_selects_words_inside_markup_and_code")
{
    const std::string source = "![A vector illustration](illustration.svg)\n`canvas->draw()`.\n한글_name42  끝";
    REQUIRE(document.text(source));
    auto options = editor.options();
    SUBCASE("plain text")
    {
        options.markdown = false;
    }
    SUBCASE("Markdown")
    {
        options.markdown = true;
    }
    editor.options(options);
    auto check = [&](size_t start, size_t inside, std::string_view expected) {
        editor.select(start + inside, start + inside);
        draw();
        auto caret = editor.caretRect();
        float x = bounds.x + caret.x + 1, y = bounds.y + caret.y + caret.h / 2;
        context.pointerDown(x, y, {}, 2);
        context.pointerUp(x, y);
        CHECK(editor.session().selection().start() == start);
        CHECK(editor.session().selectedText() == expected);
    };
    check(source.find("illustration.svg"), 3, "illustration");
    check(source.find("svg"), 2, "svg");
    check(source.find("canvas"), 5, "canvas");
    check(source.find("->"), 1, "->");
    check(source.find("draw"), 0, "draw");
    check(source.find("한글"), std::string_view("한").size(), "한글_name42");
    check(source.find("  "), 1, "  ");
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
    CHECK(editor.scrollY() == doctest::Approx(editor.caretRect().h * 0.25f));
    CHECK(context.wheel(-1000));
    CHECK(editor.scrollY() > 0);
    CHECK(!context.wheel(-1));
    CHECK(context.wheel(1));
}

TEST_CASE_FIXTURE(Editor, "pixel_wheel_scrolls_by_the_reported_distance")
{
    std::string text;
    for (int i = 0; i < 100; ++i)
        text += "line " + std::string(100, 'x') + "\n";
    REQUIRE(document.text(text));
    draw();
    context.pointerMove(bounds.x + 20, bounds.y + 20);
    CHECK(context.wheel(-13.5f, 0, true));
    CHECK(editor.scrollY() == doctest::Approx(13.5f));
    CHECK(context.wheel(0, 9, true));
    CHECK(editor.scrollX() == doctest::Approx(9));
}

TEST_CASE_FIXTURE(Editor, "fractional_scroll_is_drawn_on_device_pixels")
{
    std::string text = "body\n# heading\n";
    for (int i = 0; i < 100; ++i)
        text += "body\n";
    REQUIRE(document.text(text));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    context.scale(2);
    draw();
    auto top = editor.caretRect().y;
    context.pointerMove(bounds.x + 20, bounds.y + 20);
    CHECK(context.wheel(-0.2f, 0, true));
    draw();
    CHECK(editor.scrollY() == doctest::Approx(0.2f));
    CHECK(editor.caretRect().y == doctest::Approx(top));
    CHECK(context.wheel(-0.2f, 0, true));
    draw();
    CHECK(editor.scrollY() == doctest::Approx(0.4f));
    CHECK(editor.caretRect().y == doctest::Approx(top - 0.5f));
    CHECK(context.wheel(-0.35f, 0, true));
    draw();
    CHECK(editor.caretRect().y == doctest::Approx(top - 1));
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

TEST_CASE_FIXTURE(Editor, "markdown_wheel_preserves_fractional_input_across_heading_sizes")
{
    std::string text = "body\n# heading\n## subheading\n";
    for (int i = 0; i < 50; ++i)
        text += "body\n";
    REQUIRE(document.text(text));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    draw();
    float step = editor.caretRect().h;
    context.pointerMove(bounds.x + 20, bounds.y + 20);
    CHECK(context.wheel(-0.25f));
    CHECK(editor.scrollY() == doctest::Approx(step * 0.25f));
    CHECK(context.wheel(-0.25f));
    CHECK(editor.scrollY() == doctest::Approx(step * 0.5f));
    CHECK(context.wheel(0.125f));
    CHECK(editor.scrollY() == doctest::Approx(step * 0.375f));
    CHECK(context.wheel(-2));
    CHECK(editor.scrollY() == doctest::Approx(step * 2.375f));
    CHECK(context.wheel(-1000));
    auto limit = editor.scrollY();
    CHECK(!context.wheel(-1));
    CHECK(context.wheel(0.25f));
    CHECK(editor.scrollY() == doctest::Approx(limit - step * 0.25f));
}

TEST_CASE_FIXTURE(Editor, "scrollbar_drag_preserves_selection_and_reaches_both_ends")
{
    std::string text = std::string(160, 'W');
    for (size_t i = 0; i < 150; ++i)
        text += "\nshort";
    REQUIRE(document.text(text));
    editor.select(1, 4);
    draw();
    editor.scroll(1e6f, 1e6);
    auto limitX = editor.scrollX();
    auto limitY = editor.scrollY();
    REQUIRE(limitX > bounds.w);
    REQUIRE(limitY > bounds.h);
    editor.scroll(0, 0);
    draw();
    auto x = bounds.x + bounds.w - 5, y = bounds.y + bounds.h - 5;
    SUBCASE("vertical")
    {
        context.pointerDown(x, bounds.y + 3);
        context.pointerMove(x - 40, bounds.y + bounds.h / 2);
        CHECK(editor.scrollY() > 0);
        CHECK(editor.scrollY() < limitY);
        draw();
        context.pointerMove(x - 40, y + 50);
        CHECK(editor.scrollY() == limitY);
        context.pointerMove(x, bounds.y - 10);
        CHECK(editor.scrollY() == 0);
        context.pointerUp(x, bounds.y - 10);
        context.pointerMove(x, y);
        CHECK(editor.scrollY() == 0);
        click(x, bounds.y + bounds.h / 2);
        CHECK(editor.scrollY() > 0);
        CHECK(editor.scrollY() < limitY);
        CHECK(editor.scrollX() == 0);
    }
    SUBCASE("horizontal")
    {
        context.pointerDown(bounds.x + editor.textViewport().x + 3, y);
        context.pointerMove(x + 50, y - 40);
        CHECK(editor.scrollX() == limitX);
        context.pointerUp(x + 50, y - 40);
        CHECK(editor.scrollY() == 0);
    }
    CHECK(editor.session().selection().anchor == 1);
    CHECK(editor.session().selection().caret == 4);
}

TEST_CASE_FIXTURE(Editor, "scrolling_moves_parent_scenes_and_retains_text_in_the_viewport_cache")
{
    std::string text;
    for (size_t i = 0; i < 10000; ++i)
        text += "line " + std::to_string(i) + " with enough text to scroll horizontally **bold** and [link](target)\n";
    REQUIRE(document.text(text));
    auto options = editor.options();
    options.lineNumbers = true;
    SUBCASE("plain")
    {
        options.markdown = false;
    }
    SUBCASE("markdown")
    {
        options.markdown = true;
    }
    editor.options(options);
    draw();
    std::vector<TextPose> before;
    textPoses(context.scene(), before);
    REQUIRE_FALSE(before.empty());
    CHECK(before.size() < 300);
    auto distance = editor.caretRect().h / 2;
    editor.scroll(40, distance);
    draw();
    std::vector<TextPose> after;
    textPoses(context.scene(), after);
    size_t reused = 0;
    for (const auto& pose : before) {
        auto found = std::find_if(after.begin(), after.end(), [&](const auto& other) { return other.text == pose.text; });
        if (found == after.end()) continue;
        ++reused;
        CHECK(found->transform.e13 == pose.transform.e13);
        CHECK(found->transform.e23 == pose.transform.e23);
        CHECK(found->parent == pose.parent);
        CHECK(found->parentTransform.e23 == doctest::Approx(pose.parentTransform.e23 - std::round(distance)));
    }
    CHECK(reused == before.size());
    auto viewport = editor.textViewport();
    for (const auto& pose : after) {
        auto value = std::string_view(pose.text->text());
        if (value.empty() || !std::all_of(value.begin(), value.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) continue;
        CHECK(pose.transform.e23 + distance * 2 > editor.scrollY());
        CHECK(pose.transform.e23 < editor.scrollY() + viewport.h);
    }
    editor.scroll(0, 0);
    draw();
    after.clear();
    textPoses(context.scene(), after);
    REQUIRE(after.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i)
        CHECK(after[i].text == before[i].text);
    editor.scroll(0, 1000000);
    draw();
    after.clear();
    textPoses(context.scene(), after);
    CHECK_FALSE(after.empty());
    CHECK(after.size() < 300);
}

TEST_CASE_FIXTURE(Editor, "viewport_scenes_match_full_redraws_and_fresh_layouts_after_scrolling")
{
    std::string text;
    for (size_t i = 0; i < 500; ++i)
        text += (i % 3 == 0 ? "# Heading " : "Body ") + std::to_string(i) + " `code` ~~strike~~ [link](target) with long trailing text\n";
    REQUIRE(document.text(text));
    auto options = editor.options();
    options.lineNumbers = true;
    SUBCASE("plain")
    {
        options.markdown = false;
    }
    SUBCASE("markdown")
    {
        options.markdown = true;
    }
    editor.options(options);
    editor.theme(theme);
    output.resize(400 * 300);
    canvas.reset(tvg::SwCanvas::gen(tvg::EngineOption::SmartRender));
    REQUIRE(canvas->target(output.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
    REQUIRE(canvas->add(context.scene()) == tvg::Result::Success);
    draw();
    for (auto position : {ui::Rect{0, 13}, ui::Rect{43, 87}, ui::Rect{0, 370}, ui::Rect{23, 100000}, ui::Rect{0, 0}}) {
        editor.scroll(position.x, position.y);
        draw(false);
        auto incremental = output;
        draw(true);
        CHECK(bool(incremental == output));

        ui::TextEditor reference(document);
        reference.font(fonts.plain);
        reference.options(options);
        reference.theme(theme);
        reference.scroll(editor.scrollX(), editor.scrollY());
        ui::Context referenceContext;
        referenceContext.begin(theme, viewport);
        reference.render(referenceContext, "editor", bounds);
        referenceContext.end();
        std::vector<uint32_t> expected(output.size());
        std::unique_ptr<tvg::SwCanvas> full(tvg::SwCanvas::gen());
        REQUIRE(full->target(expected.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
        REQUIRE(full->add(referenceContext.scene()) == tvg::Result::Success);
        REQUIRE(full->draw(true) == tvg::Result::Success);
        REQUIRE(full->sync() == tvg::Result::Success);
        // A fresh view has measured fewer rows, so its horizontal thumb can be wider.
        // The viewport must still match exactly; the full redraw above also checks the thumb.
        auto textArea = editor.textViewport();
        auto bottom = size_t(bounds.y + textArea.y + textArea.h);
        for (size_t y = 0; y < 300; ++y) {
            if (y >= bottom && y < size_t(bounds.y + bounds.h)) continue;
            CHECK(std::equal(output.begin() + y * 400, output.begin() + (y + 1) * 400, expected.begin() + y * 400));
        }
    }
}

TEST_CASE_FIXTURE(Editor, "smart_render_draws_opaque_frames_without_clearing_after_retargeting")
{
    REQUIRE(document.text("# Heading\nBody `code` and [link](target)\nMore text"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    canvas.reset(tvg::SwCanvas::gen(tvg::EngineOption::SmartRender));
    REQUIRE(canvas->add(context.scene()) == tvg::Result::Success);
    for (auto size : {ui::Rect{0, 0, 400, 300}, ui::Rect{0, 0, 250, 160}, ui::Rect{0, 0, 500, 340}}) {
        auto w = uint32_t(size.w), h = uint32_t(size.h);
        output.assign(size_t(w) * h, 0x5a5a5a5a);
        REQUIRE(canvas->target(output.data(), w, w, h, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
        viewport = size;
        bounds = {20, 10, size.w - 40, size.h - 20};
        render([&] {
            context.rect(viewport, theme.background);
            editor.render(context, "editor", bounds);
        });
        REQUIRE(canvas->update() == tvg::Result::Success);
        REQUIRE(canvas->draw(false) == tvg::Result::Success);
        REQUIRE(canvas->sync() == tvg::Result::Success);
        auto incremental = output;
        REQUIRE(canvas->update() == tvg::Result::Success);
        REQUIRE(canvas->draw(true) == tvg::Result::Success);
        REQUIRE(canvas->sync() == tvg::Result::Success);
        CHECK(bool(incremental == output));
    }
}

TEST_CASE_FIXTURE(Editor, "cached_markdown_rows_match_full_redraws_after_edits_and_style_changes")
{
    REQUIRE(document.text("# aaaa\n[bbbb](target)\n~~aaaa~~\n`bbbb`\nMore text\nLast line"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    raster();
    auto verify = [&](const char* change) {
        INFO(std::string(change));
        draw(false);
        auto incremental = output;
        draw(true);
        CHECK(bool(incremental == output));
    };
    REQUIRE(document.text("aaaa\n`bbbb`\n[aaaa](target)\n~~bbbb~~\nLast line"));
    verify("document edit");
    theme.markdownLink = {23, 171, 87};
    theme.markdownCode = {196, 71, 102};
    theme.markdownCodeBackground = {77, 108, 231, 90};
    editor.theme(theme);
    verify("theme change");
    options.markdown = false;
    editor.options(options);
    verify("plain text");
    options.markdown = true;
    options.fontSize = 23;
    editor.options(options);
    verify("markdown and font size");
    editor.select(1, 15);
    verify("selection");
    editor.scroll(0, 35);
    verify("scroll");
    editor.composition("aaaa", 2);
    verify("composition");
    editor.composition("", 0);
    verify("composition cleared");
}

TEST_CASE_FIXTURE(Editor, "smart_render_limits_caret_blink_damage_to_a_small_region")
{
    REQUIRE(document.text("First line\nSecond line\nThird line"));
    output.resize(400 * 300);
    canvas.reset(tvg::SwCanvas::gen(tvg::EngineOption::SmartRender));
    REQUIRE(canvas->target(output.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
    REQUIRE(canvas->add(context.scene()) == tvg::Result::Success);
    context.focus("editor");
    auto frame = [&](double time) {
        context.begin(theme, viewport, time);
        editor.render(context, "editor", bounds);
        context.end();
        REQUIRE(canvas->update() == tvg::Result::Success);
        REQUIRE(canvas->draw(false) == tvg::Result::Success);
    };
    frame(1);
    REQUIRE(canvas->sync() == tvg::Result::Success);
    frame(1.6);
    std::vector<tvg::Region> regions(canvas->dirtyRegions());
    REQUIRE_FALSE(regions.empty());
    REQUIRE(canvas->dirtyRegions(regions.data(), uint32_t(regions.size())) == tvg::Result::Success);
    size_t damaged = 0;
    for (const auto& region : regions)
        damaged += size_t(region.xMax - region.xMin) * size_t(region.yMax - region.yMin);
    CHECK(damaged < 400 * 300 / 4);
    REQUIRE(canvas->sync() == tvg::Result::Success);
    auto incremental = output;
    REQUIRE(canvas->update() == tvg::Result::Success);
    REQUIRE(canvas->draw(true) == tvg::Result::Success);
    REQUIRE(canvas->sync() == tvg::Result::Success);
    CHECK(bool(incremental == output));
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
        for (auto y = static_cast<int>(bounds.y + row.y); y < std::min(300, int(bounds.y + row.y + row.h)); ++y)
            for (auto x = static_cast<int>(bounds.x); x < int(bounds.x + gutter); ++x) {
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

TEST_CASE_FIXTURE(Editor, "line_numbers_match_body_digit_height_and_position_after_font_changes")
{
    auto digits = ui::loadFont(ui::test::fontPath("M1Digits.ttf").c_str());
    REQUIRE(digits);
    REQUIRE(document.text("1\n2\n3"));
    auto options = editor.options();
    auto ink = [&](ui::Color color, bool gutter) {
        const auto& image = raster();
        auto area = editor.textViewport();
        int top = 300, bottom = -1;
        uint32_t value = uint32_t(color.a) << 24 | uint32_t(color.r) << 16 | uint32_t(color.g) << 8 | color.b;
        auto first = int(bounds.x + (gutter ? 0 : area.x));
        auto end = int(bounds.x + (gutter ? area.x : bounds.w));
        auto row = editor.caretRect();
        for (int y = std::max(0, int(bounds.y + row.y)); y < std::min(300, int(bounds.y + row.y + row.h)); ++y)
            for (int x = first; x < end; ++x)
                if (image[size_t(y) * 400 + x] == value) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
        REQUIRE(bottom >= top);
        return std::pair{top, bottom};
    };
    for (auto font : {fonts.plain, digits, fonts.kern, digits}) {
        for (auto markdown : {false, true}) {
            for (auto size : {16.0f, 32.0f}) {
                CAPTURE(font);
                CAPTURE(markdown);
                CAPTURE(size);
                editor.font(font, digits);
                options.markdown = markdown;
                options.fontSize = size;
                editor.options(options);
                editor.select(0, 0);
                auto number = ink(theme.muted, true);
                auto text = ink(theme.text, false);
                CHECK(std::abs(number.first - text.first) <= 1);
                CHECK(std::abs(number.second - text.second) <= 1);
            }
        }
    }
    REQUIRE(document.text("1\n# 2\n## 3\n### 4\n#### 5\n##### 6\n###### 7"));
    options.fontSize = 16;
    options.markdown = true;
    editor.options(options);
    editor.select(0, 0);
    auto number = ink(theme.muted, true);
    auto top = number.first - bounds.y - editor.caretRect().y;
    auto height = number.second - number.first + 1;
    auto bodyHeight = editor.caretRect().h;
    for (size_t row = 1; row < document.lineCount(); ++row) {
        editor.select(document.lineStart(row), document.lineStart(row));
        auto headingNumber = ink(theme.muted, true);
        CHECK(std::abs(headingNumber.first - bounds.y - editor.caretRect().y - top) <= 1);
        CHECK(std::abs(headingNumber.second - headingNumber.first + 1 - height) <= 1);
        CHECK(editor.caretRect().h > bodyHeight);
    }
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

TEST_CASE_FIXTURE(Editor, "markdown_incremental_edits_match_fresh_layouts")
{
    std::string source, inserted;
    size_t start = 0, removed = 0, secondWordEdit = 0;
    bool compositionCommit = false, fallbackFont = false;
    SUBCASE("independent_paragraph_edits")
    {
        for (unsigned row = 0; row < 600; ++row)
            source += "**bold** plainword\n";
        source += "\n# Tail\n";
        inserted = "\xed\x95\x9c";
        fallbackFont = true;
        SUBCASE("first_character") {}
        SUBCASE("word_start")
        {
            start = 9;
        }
        SUBCASE("sentence_punctuation")
        {
            start = 12;
            inserted = ", end.";
        }
        SUBCASE("unicode_punctuation")
        {
            start = 12;
            inserted = "\xef\xbc\x81";
        }
        SUBCASE("newline")
        {
            start = 12;
            inserted = "\n";
        }
        SUBCASE("multiline_paste")
        {
            start = 12;
            inserted = "first\nsecond\nthird";
        }
        SUBCASE("chunk_boundary_newline")
        {
            start = 128 * 19;
            inserted = "\n";
        }
        SUBCASE("delete_chunk_boundary")
        {
            start = 128 * 19 - 1;
            removed = 1;
            inserted.clear();
        }
        SUBCASE("open_delimiter")
        {
            start = 12;
            inserted = "**open ";
        }
        SUBCASE("heading")
        {
            start = 128 * 19;
            inserted = "# ";
        }
        SUBCASE("fence")
        {
            start = 128 * 19;
            inserted = "```\n";
        }
        SUBCASE("setext")
        {
            start = 128 * 19;
            inserted = "===\n";
        }
        SUBCASE("link")
        {
            start = 12;
            inserted = "[label](target)";
        }
        SUBCASE("reference_definition")
        {
            start = 128 * 19;
            inserted = "[key]: target\n";
        }
        SUBCASE("multiline_emphasis")
        {
            start = 12;
            inserted = "**open\ncontinued**";
        }
    }
    SUBCASE("independent_boundary_merge")
    {
        source = "<div>\n\n**bold** plainword\n";
        start = source.find("\n\n") + 1;
        removed = 1;
    }
    SUBCASE("independent_boundary_split")
    {
        source = "**bold** plainword\n\nclose**";
        start = source.find("\n\n") + 1;
        removed = 1;
    }
    SUBCASE("removing_a_reference_definition_between_independent_blocks")
    {
        source = "[label][key]\n\nfirst plainword\n\n[key]: target\n\nlast plainword";
        start = source.find("plainword");
        removed = source.rfind("plainword") - start;
    }
    SUBCASE("independent_text_in_a_reference_document")
    {
        source = "**bold** plainword\n\n[label][key]\n\n[key]: target";
        start = 12;
        inserted = "\xe4\xb8\xad";
        fallbackFont = true;
    }
    SUBCASE("multiline_unresolved_reference_label_changes")
    {
        source = "[laX\nbel]\n\n[la bel]: target";
        start = 3;
        removed = 1;
    }
    SUBCASE("hangul_word_edits")
    {
        fallbackFont = true;
        source = "**bold** 가나다라마 tailword\nunchanged **content**\nlast 가나다라마";
        start = source.find("가나다라마") + 6;
        inserted = "한";
        SUBCASE("insertion") {}
        SUBCASE("deletion")
        {
            removed = 3;
            inserted.clear();
        }
        SUBCASE("replacement")
        {
            removed = 3;
            inserted = "글";
        }
        SUBCASE("space_boundary")
        {
            start = source.find(" tailword");
        }
        SUBCASE("eof")
        {
            start = source.size();
        }
        SUBCASE("compatibility_jamo")
        {
            inserted = "ㅎㄱ";
        }
        SUBCASE("canonical_jamo")
        {
            inserted = "\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab";
        }
        SUBCASE("mixed_latin_and_jamo")
        {
            start = source.find("tailword") + 3;
            inserted = "한\xe1\x84\x80";
        }
        SUBCASE("composition_commit")
        {
            compositionCommit = true;
        }
        SUBCASE("unsupported_cjk")
        {
            inserted = "中";
        }
        SUBCASE("unsupported_two_byte_letter")
        {
            inserted = "\xc3\xa9";
        }
        SUBCASE("unsupported_symbol")
        {
            inserted = "\xf0\x9f\x98\x80";
        }
        SUBCASE("unicode_punctuation")
        {
            inserted = "！";
        }
    }
    SUBCASE("composition_commit_in_a_styled_paragraph")
    {
        source = "first **bold**\n\n# Heading\n\nLast line";
        start = 2;
        inserted = "\xed\x95\x9c";
        compositionCommit = true;
    }
    SUBCASE("composition_commit_inserts_a_newline")
    {
        source = "first **bold**\n\n# Heading\n\nLast line";
        start = source.find("bold") + 2;
        inserted = "composed\ntext";
        compositionCommit = true;
    }
    SUBCASE("plain_edit_before_styled_rows")
    {
        source = "Plain text\n\n# Heading\n**bold** and `code`\nLast line";
        start = 5;
        inserted = "more\ntext ";
    }
    SUBCASE("word_edit_inside_multiline_emphasis")
    {
        source = "**bold plainword\nsecond plainword**\nfollowing *emphasis*";
        start = source.find("plainword") + 4;
        inserted = "More";
    }
    SUBCASE("word_edit_inside_parenthesized_number_marker_reparses")
    {
        source = "11) plainword\n\nnext";
        start = 1;
        inserted = "x";
    }
    SUBCASE("word_deletion_creates_parenthesized_number_marker")
    {
        source = "**bold**\n\n1x1) plainword\n\nnext";
        start = source.find('x');
        removed = 1;
    }
    SUBCASE("hangul_edit_inside_parenthesized_number_marker_reparses")
    {
        fallbackFont = true;
        source = "11) plainword\n\nnext";
        start = 1;
        inserted = "한";
    }
    SUBCASE("hangul_deletion_creates_parenthesized_number_marker")
    {
        fallbackFont = true;
        source = "**bold**\n\n1한1) plainword\n\nnext";
        start = source.find("한");
        removed = 3;
    }
    SUBCASE("word_edit_inside_nested_quote_number_marker_reparses")
    {
        source = "> 11) plainword\n> next";
        start = source.find("11)") + 1;
        inserted = "x";
    }
    SUBCASE("word_deletion_creates_nested_number_marker")
    {
        source = "- 1x1) plainword\n\nnext **bold**";
        start = source.find('x');
        removed = 1;
    }
    SUBCASE("two_word_edits_inside_multiline_emphasis")
    {
        source = "**bold plainwordlongword\nsecond plainword**\nfollowing *emphasis*";
        auto word = source.find("plainwordlongword");
        start = word + 2;
        inserted = "More";
        secondWordEdit = word + 10;
    }
    SUBCASE("word_end_insertion_before_a_newline")
    {
        source = "**bold** plainword\nsecond *emphasis*\nlast";
        start = source.find('\n');
        inserted = "More";
    }
    SUBCASE("word_end_deletion_before_a_newline")
    {
        source = "**bold** plainword\nsecond *emphasis*\nlast";
        start = source.find('\n') - 3;
        removed = 3;
    }
    SUBCASE("word_end_insertion_at_eof")
    {
        source = "**bold**\nplainword";
        start = source.size();
        inserted = "More";
    }
    SUBCASE("word_end_deletion_at_eof")
    {
        source = "**bold**\nplainword";
        start = source.size() - 3;
        removed = 3;
    }
    SUBCASE("word_end_insertion_before_a_space")
    {
        source = "**bold** plainword nextword\nlast *emphasis*";
        start = source.find(" nextword");
        inserted = "More";
    }
    SUBCASE("word_end_deletion_before_a_space")
    {
        source = "**bold** plainword nextword\nlast *emphasis*";
        start = source.find(" nextword") - 3;
        removed = 3;
    }
    SUBCASE("word_end_before_punctuation_keeps_the_parser_path")
    {
        source = "**bold** plainword!\nlast *emphasis*";
        start = source.find('!');
        inserted = "More";
    }
    SUBCASE("word_end_before_a_delimiter_keeps_the_parser_path")
    {
        source = "**bold** *plainword*\nlast *emphasis*";
        start = source.find("word") + 4;
        inserted = "More";
    }
    SUBCASE("word_deletion_inside_a_list")
    {
        source = "- first plainword\n- second **plainword**\n- third item";
        start = source.find("plainword") + 3;
        removed = 3;
    }
    SUBCASE("word_replacement_inside_code")
    {
        source = "```text\nfirst plainword\nsecond plainword\n```\n\n# Tail";
        start = source.find("plainword") + 3;
        removed = 3;
        inserted = "More";
    }
    SUBCASE("word_edit_in_a_heading")
    {
        source = "# First plainword\n\n## Second heading\n\nLast plainword";
        start = source.find("plainword") + 3;
        inserted = "More";
    }
    SUBCASE("letter_deletion_reveals_an_entity")
    {
        source = "**prefix** &amXp; plainword\n\nLast line";
        start = source.find('X');
        removed = 1;
    }
    SUBCASE("letter_deletion_reveals_an_autolink")
    {
        source = "**prefix** httpXs://example.com\n\nLast line";
        start = source.find('X');
        removed = 1;
    }
    SUBCASE("letter_deletion_changes_a_reference_label")
    {
        source = "[laXbel]\n\n[label]: target\n\nLast line";
        start = source.find('X');
        removed = 1;
    }
    SUBCASE("letter_edit_in_a_html_tag")
    {
        source = "<diXv>\nplainword\n</div>\n\n**Tail**";
        start = source.find('X');
        removed = 1;
    }
    SUBCASE("word_edit_after_a_tab")
    {
        source = "**prefix**\tplainword\nsecond plainword";
        start = source.find("plainword") + 3;
        inserted = "More";
    }
    SUBCASE("edit_within_multiline_emphasis")
    {
        source = "**First line\nsecond line\nthird line**\n\nLast line";
        start = source.find("second") + 3;
        inserted = "more ";
    }
    SUBCASE("plain_edit_without_syntax")
    {
        source = "Plain text: one, two.\nSecond line\nLast line";
        start = 6;
        inserted = "more\ntext ";
    }
    SUBCASE("plain_text_becomes_a_heading")
    {
        source = "First line\nTitle\nLast line";
        start = source.find("Title");
        inserted = "# ";
    }
    SUBCASE("deletion_introduces_indented_code")
    {
        source = "prefix    code\nLast line";
        removed = 6;
    }
    SUBCASE("deletion_introduces_a_numbered_list")
    {
        source = "prefix1. item\nLast line";
        removed = 6;
    }
    SUBCASE("plain_text_becomes_a_url")
    {
        source = "http//example.com\nLast line";
        start = 4;
        inserted = ":";
    }
    SUBCASE("deletion_introduces_a_www_link")
    {
        source = "wwXw.example.com\nLast line";
        start = 2;
        removed = 1;
    }
    SUBCASE("plain_text_becomes_an_email_link")
    {
        source = "personexample.com\nLast line";
        start = 6;
        inserted = "@";
    }
    SUBCASE("opening_fence_propagates_beyond_neighboring_blocks")
    {
        source = "# First\n\nfirst\n\n# Second\n\n**second**\n\n# Third\n\nLast line";
        start = source.find("first");
        inserted = "```text\n";
    }
    SUBCASE("quote_and_nested_list_block_boundaries_change")
    {
        source = "> # Title\n> text\n\n- first\n  - **nested**\n\nLast line";
        removed = 2;
    }
    SUBCASE("removing_a_heading_merges_inline_context")
    {
        source = "before **open\n\n# Boundary\n\nclose**\n\nLast line";
        start = source.find("\n\n#");
        removed = std::string("\n\n# Boundary\n\n").size();
        inserted = "\n";
    }
    SUBCASE("same_size_edit_within_styled_text")
    {
        source = "# Title\n\n**bold** and `code`\nLast line";
        start = source.find("bold");
        removed = 4;
        inserted = "body";
    }
    SUBCASE("multibyte_edit_before_styled_rows")
    {
        source = "A가中\n\n# Heading\nLast line";
        start = 1;
        removed = 3;
        inserted = "한글\n";
    }
    SUBCASE("closing_fence_changes_following_rows")
    {
        source = "```\nfirst\n```\n# Heading\n**bold**\nLast line";
        start = source.find("```", 3);
        removed = 3;
    }
    SUBCASE("setext_marker_changes_the_previous_row")
    {
        source = "Title\nplain\n\nLast line";
        start = source.find("plain");
        removed = 5;
        inserted = "=====";
    }
    SUBCASE("reference_definition_changes_an_earlier_link")
    {
        source = "[label][ref]\n\nPlain text\n\n[ref]: destination\nLast line";
        start = source.find("[ref]:");
        removed = 1;
    }
    SUBCASE("new_reference_definition_resolves_an_earlier_link")
    {
        source = "[label][ref]\n\nPlain text\nLast line";
        start = source.size();
        inserted = "\n\n[ref]: destination";
    }
    SUBCASE("delete_all_styled_text")
    {
        source = "**bold**\n\n# Heading\nLast line";
        removed = source.size();
    }
    SUBCASE("multiple_edits_and_deleted_newlines")
    {
        source = "# Heading\n\n**bold**\n\nLast line";
        start = source.find("\n\n");
        removed = 2;
        inserted = " ";
    }
    REQUIRE(document.text(source));
    auto font = fallbackFont ? fonts.fallback : fonts.plain;
    editor.font(font);
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    editor.theme(theme);
    raster();
    std::string error;
    auto verify = [&] {
        ui::TextEditor reference(document);
        reference.font(font);
        reference.options(options);
        reference.theme(theme);
        ui::Context referenceContext;
        referenceContext.begin(theme, viewport);
        reference.render(referenceContext, "editor", bounds);
        referenceContext.end();
        std::vector<uint32_t> expected(output.size());
        std::unique_ptr<tvg::SwCanvas> full(tvg::SwCanvas::gen());
        REQUIRE(full->target(expected.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
        REQUIRE(full->add(referenceContext.scene()) == tvg::Result::Success);
        for (size_t row = 0; row < document.lineCount(); ++row) {
            INFO(row);
            auto offset = document.lineStart(row);
            editor.select(offset, offset);
            reference.select(offset, offset);
            draw(true);
            reference.scroll(editor.scrollX(), editor.scrollY());
            referenceContext.begin(theme, viewport);
            reference.render(referenceContext, "editor", bounds);
            referenceContext.end();
            auto caret = editor.caretRect(), fresh = reference.caretRect();
            CHECK(caret.x == doctest::Approx(fresh.x));
            CHECK(caret.y == doctest::Approx(fresh.y));
            CHECK(caret.h == doctest::Approx(fresh.h));
            REQUIRE(full->update() == tvg::Result::Success);
            REQUIRE(full->draw(true) == tvg::Result::Success);
            REQUIRE(full->sync() == tvg::Result::Success);
            CHECK(bool(output == expected));
        }
    };
    tn::EditBatch batch{.revision = document.revision(), .edits = {{start, start + removed, inserted}}};
    if (secondWordEdit) batch.edits.push_back({secondWordEdit, secondWordEdit + 2, "x"});
    if (source.starts_with("# Heading")) batch.edits.push_back({source.find("Last"), source.size(), "**Tail**"});
    if (compositionCommit) {
        editor.select(start, start + removed);
        editor.composition(inserted, inserted.size());
        draw();
        editor.composition("", 0);
        REQUIRE(editor.insert(inserted, error));
    } else REQUIRE(editor.applyEdits(batch, error));
    verify();
    REQUIRE(editor.undo());
    verify();
    REQUIRE(editor.redo());
    verify();
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

TEST_CASE_FIXTURE(Editor, "large_document_coordinates_preserve_fractional_scroll_and_height")
{
    std::string text;
    for (size_t i = 0; i < 4000; ++i) {
        if (i) text += '\n';
        text += "# A";
    }
    REQUIRE(document.text(text));
    auto options = editor.options();
    options.markdown = true;
    options.fontSize = ui::TextEditorOptions::maxFontSize;
    editor.options(options);
    draw();
    auto rowHeight = editor.caretRect().h;
    editor.select(text.size(), text.size());
    draw();
    auto maximum = editor.scrollY();
    auto end = editor.caretRect();
    CHECK(maximum > 33554432.0);
    CHECK(std::abs(std::round(maximum) + end.y + end.h - double(rowHeight) * 4000) < 0.01);
    auto origin = maximum - 1000;
    editor.scroll(0, origin);
    auto before = editor.caretRect().y;
    for (size_t i = 1; i <= 8; ++i) {
        editor.scroll(0, origin + i * 0.25);
        CHECK(editor.scrollY() == origin + i * 0.25);
        CHECK(editor.caretRect().y == doctest::Approx(before - (std::round(origin + i * 0.25) - std::round(origin))));
    }
}

TEST_CASE_FIXTURE(Editor, "horizontal_extent_expands_when_an_unseen_wide_line_is_measured")
{
    std::string text = "short";
    for (size_t i = 0; i < 1000; ++i)
        text += "\nshort";
    text += "\n" + std::string(200, 'A');
    REQUIRE(document.text(text));
    draw();
    editor.scroll(100000, 0);
    auto initial = editor.scrollX();
    editor.select(text.size(), text.size());
    draw();
    editor.scroll(100000, 0);
    CHECK(editor.scrollX() > initial + 100);
}

TEST_CASE_FIXTURE(Editor, "horizontal_extent_tracks_duplicate_widths_and_discarded_geometry")
{
    auto wide = std::string(200, 'A');
    std::string text = wide;
    for (size_t i = 0; i < 1000; ++i)
        text += "\nshort";
    text += "\n" + wide;
    REQUIRE(document.text(text));
    draw();
    editor.select(text.size(), text.size());
    draw();
    editor.scroll(100000, 0);
    auto maximum = editor.scrollX();
    CHECK(maximum > 100);
    editor.select(0, 0);
    draw();
    auto last = document.lineCount() - 1;
    editor.select(document.lineStart(last), document.lineEnd(last));
    std::string error;
    REQUIRE(editor.insert("short", error));
    draw();
    editor.scroll(100000, 0);
    CHECK(editor.scrollX() == doctest::Approx(maximum));
    editor.select(0, wide.size());
    REQUIRE(editor.insert("short", error));
    draw();
    editor.scroll(100000, 0);
    CHECK(editor.scrollX() == 0);
    REQUIRE(editor.undo());
    draw();
    editor.scroll(100000, 0);
    CHECK(editor.scrollX() == doctest::Approx(maximum));
}

TEST_CASE_FIXTURE(Editor, "opened_long_row_lays_out_across_frames_before_showing_its_scrollbar")
{
    std::string text = "short\n";
    for (size_t i = 0; text.size() < 700'000; ++i)
        text += "word" + std::to_string(i % 97) + (i % 9 ? " " : "\t");
    // A caret at the end needs the whole row at once, which makes the reference.
    tn::Document whole;
    ui::TextEditor reference{whole};
    reference.font(fonts.plain);
    REQUIRE(whole.text(text));
    reference.select(text.size(), text.size());
    render([&] { reference.render(context, "reference", bounds); });
    auto end = reference.caretRect().x + reference.scrollX();
    reference.scroll(1e9f, 0);
    auto limit = reference.scrollX();
    REQUIRE(limit > 100000);
    REQUIRE(document.text(text));
    draw();
    CHECK(context.nextFrame().has_value());
    SUBCASE("frames")
    {
        auto x = bounds.x + bounds.w / 2, y = bounds.y + bounds.h - 2;
        size_t frames = 1;
        for (; context.nextFrame() && frames < 100; ++frames) {
            draw();
            context.pointerMove(x, y);
            CHECK(context.cursor() == ui::Cursor::Text);
        }
        CHECK(frames > 2);
        draw();
        context.pointerMove(x, y);
        CHECK(context.cursor() == ui::Cursor::Default);
    }
    SUBCASE("scroll")
    {
        editor.scroll(1e9f, 0);
        CHECK(editor.scrollX() == limit);
    }
    SUBCASE("edit")
    {
        editor.select(0, 0);
        std::string error;
        REQUIRE(editor.insert("a", error));
        draw();
        CHECK_FALSE(context.nextFrame().has_value());
    }
    auto size = document.text().size();
    editor.select(size, size);
    CHECK(editor.caretRect().x + editor.scrollX() == end);
    draw();
    CHECK_FALSE(context.nextFrame().has_value());
}

TEST_CASE_FIXTURE(Editor, "composition_commit_retains_unaffected_markdown_paints")
{
    bounds.h = 280;
    REQUIRE(document.text("first\n\n# Second\n\nthird\n\n# Fourth\n\nlast"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    context.focus("editor");
    raster();
    std::vector<TextPose> before;
    textPoses(context.scene(), before);
    auto found = std::find_if(before.begin(), before.end(), [](const auto& pose) { return std::string_view(pose.text->text()).find("Fourth") != std::string_view::npos; });
    REQUIRE(found != before.end());
    found->text->ref();
    std::unique_ptr<tvg::Text, void (*)(tvg::Text*)> retained(found->text, [](tvg::Text* text) { text->unref(); });
    editor.select(0, 0);
    editor.composition("\xed\x95\x9c", 3);
    draw();
    SUBCASE("commit_after_clearing_preedit")
    {
        editor.composition("", 0);
    }
    SUBCASE("commit_active_preedit") {}
    context.textInput("\xed\x95\x9c");
    draw();
    CHECK(!editor.composing());
    CHECK(document.text().starts_with("\xed\x95\x9c"
                                      "first"));
    std::vector<TextPose> after;
    textPoses(context.scene(), after);
    CHECK(std::any_of(after.begin(), after.end(), [&](const auto& pose) { return pose.text == retained.get(); }));
}

TEST_CASE_FIXTURE(Editor, "font_assignment_preserves_layout_until_fonts_change")
{
    REQUIRE(document.text("AVAV\n\n# Heading"));
    editor.font(fonts.plain, fonts.plain);
    auto options = editor.options();
    options.markdown = true;
    options.lineNumbers = true;
    editor.options(options);
    auto beforePixels = raster();
    auto width = editor.rangeRects(0, 4).front().w;
    std::vector<TextPose> before;
    textPoses(context.scene(), before);
    auto found = std::find_if(before.begin(), before.end(), [](const auto& pose) { return std::string_view(pose.text->text()) == "AVAV"; });
    REQUIRE(found != before.end());
    found->text->ref();
    std::unique_ptr<tvg::Text, void (*)(tvg::Text*)> retained(found->text, [](tvg::Text* text) { text->unref(); });
    bool changed = false;
    SUBCASE("same_fonts")
    {
        editor.font(fonts.plain, fonts.plain);
    }
    SUBCASE("document_font_changes")
    {
        editor.font(fonts.kern, fonts.plain);
        CHECK(editor.rangeRects(0, 4).front().w < width);
        changed = true;
    }
    SUBCASE("line_number_font_changes")
    {
        editor.font(fonts.plain, fonts.kern);
        changed = true;
    }
    SUBCASE("font_registry_changes")
    {
        std::ifstream input(ui::test::fontPath("M1Plain.ttf"), std::ios::binary);
        REQUIRE(input);
        std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        REQUIRE(ui::loadFont("Additional", bytes.data(), bytes.size()));
        editor.font(fonts.plain, fonts.plain);
    }
    auto afterPixels = raster();
    std::vector<TextPose> after;
    textPoses(context.scene(), after);
    CHECK(std::any_of(after.begin(), after.end(), [&](const auto& pose) { return pose.text == retained.get(); }) == !changed);
    if (!changed) {
        CHECK(afterPixels == beforePixels);
        CHECK(editor.rangeRects(0, 4).front().w == doctest::Approx(width));
    }
}

TEST_CASE_FIXTURE(Editor, "word_edits_preserve_other_rows_in_the_same_markdown_block")
{
    REQUIRE(document.text("**bold** plainword tailword\nunchanged **content**\nlast plainword"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    raster();
    std::vector<TextPose> before;
    textPoses(context.scene(), before);
    auto found = std::find_if(before.begin(), before.end(), [](const auto& pose) { return std::string_view(pose.text->text()).find("unchanged") != std::string_view::npos; });
    REQUIRE(found != before.end());
    found->text->ref();
    std::unique_ptr<tvg::Text, void (*)(tvg::Text*)> retained(found->text, [](tvg::Text* text) { text->unref(); });
    auto start = document.text().find("plainword") + 3;
    auto end = start;
    std::string inserted = "More";
    SUBCASE("interior_word_insertion") {}
    SUBCASE("line_end_insertion")
    {
        start = end = document.lineEnd(0);
    }
    SUBCASE("line_end_deletion")
    {
        end = document.lineEnd(0);
        start = end - 3;
        inserted.clear();
    }
    SUBCASE("eof_insertion")
    {
        start = end = document.text().size();
    }
    SUBCASE("eof_deletion")
    {
        end = document.text().size();
        start = end - 3;
        inserted.clear();
    }
    SUBCASE("space_boundary_insertion")
    {
        start = end = document.text().find(" tailword");
    }
    editor.select(start, end);
    std::string error;
    REQUIRE(editor.insert(inserted, error));
    draw();
    std::vector<TextPose> after;
    textPoses(context.scene(), after);
    CHECK(std::any_of(after.begin(), after.end(), [&](const auto& pose) { return pose.text == retained.get(); }));
}

TEST_CASE_FIXTURE(Editor, "composition_commit_word_edits_retain_other_paragraph_rows")
{
    std::string source = "**bold** 가가가가가\nunchanged **content**\nlast 가가가가가";
    std::string inserted = "가";
    auto start = source.find("가가가가가") + 6;
    size_t removed = 0;
    bool retainedExpected = true;
    SUBCASE("hangul_word") {}
    SUBCASE("hangul_word_end")
    {
        start = source.find('\n');
    }
    SUBCASE("hangul_eof")
    {
        start = source.size();
    }
    SUBCASE("canonical_jamo")
    {
        inserted = "\xe1\x84\x92\xe1\x85\xa1";
    }
    SUBCASE("compatibility_jamo")
    {
        inserted = "ㅎ";
    }
    SUBCASE("latin_word")
    {
        source = "**bold** plainword\nunchanged **content**\nlast plainword";
        start = source.find("plainword") + 3;
        inserted = "More";
    }
    SUBCASE("unsupported_unicode_reparses")
    {
        inserted = "中";
        retainedExpected = false;
    }
    SUBCASE("replacement_without_original_row_cache_reparses")
    {
        removed = 3;
        inserted = "가가";
        retainedExpected = false;
    }
    REQUIRE(document.text(source));
    editor.font(fonts.fallback);
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    context.focus("editor");
    raster();
    std::vector<TextPose> before;
    textPoses(context.scene(), before);
    auto found = std::find_if(before.begin(), before.end(), [](const auto& pose) { return std::string_view(pose.text->text()).find('u') != std::string_view::npos; });
    REQUIRE(found != before.end());
    found->text->ref();
    std::unique_ptr<tvg::Text, void (*)(tvg::Text*)> retained(found->text, [](tvg::Text* text) { text->unref(); });
    editor.select(start, start + removed);
    editor.composition(inserted, inserted.size());
    draw();
    editor.composition("", 0);
    auto revision = document.revision();
    context.textInput(inserted);
    draw();
    auto expected = source;
    expected.replace(start, removed, inserted);
    CHECK(document.text() == expected);
    CHECK(document.revision() > revision);
    CHECK(!editor.composing());
    CHECK(editor.session().selection().caret == start + inserted.size());
    std::vector<TextPose> after;
    textPoses(context.scene(), after);
    CHECK(std::any_of(after.begin(), after.end(), [&](const auto& pose) { return pose.text == retained.get(); }) == retainedExpected);
}

TEST_CASE_FIXTURE(Editor, "composition_updates_match_fresh_layouts")
{
    const std::string source = "# Heading\n\n**bold** plainword\nnext content\n\n## Tail\nlast";
    REQUIRE(document.text(source));
    auto options = editor.options();
    options.lineNumbers = true;
    SUBCASE("plain")
    {
        options.markdown = false;
    }
    SUBCASE("markdown")
    {
        options.markdown = true;
    }
    editor.options(options);
    context.focus("editor");
    raster();
    auto first = source.find("plainword") + 2;
    editor.select(first, first + 3);
    struct Step
    {
        const char* text;
        size_t cursor;
        bool move = false;
    };
    for (auto step : {Step{"ab", 1}, Step{"cd", 1}, Step{"expanded", 6}, Step{"x", 1}, Step{"first\nsecond", 7}, Step{"ab", 1}, Step{"ab", 0}, Step{"ab", 2, true}, Step{"", 0}}) {
        INFO(step.text);
        INFO(step.cursor);
        if (step.move) {
            auto next = source.find("content") + 2;
            editor.select(next, next + 2);
        }
        editor.composition(step.text, step.cursor);
        draw();
        auto selection = editor.session().selection();
        ui::TextEditor reference(document);
        reference.font(fonts.plain);
        reference.options(options);
        reference.theme(theme);
        reference.select(selection.anchor, selection.caret);
        ui::Context referenceContext;
        referenceContext.focus("editor");
        auto renderReference = [&] {
            referenceContext.begin(theme, viewport);
            reference.render(referenceContext, "editor", bounds);
            referenceContext.end();
        };
        renderReference();
        reference.composition(step.text, step.cursor);
        reference.scroll(editor.scrollX(), editor.scrollY());
        renderReference();
        std::vector<uint32_t> expected(output.size());
        std::unique_ptr<tvg::SwCanvas> full(tvg::SwCanvas::gen());
        REQUIRE(full->target(expected.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
        REQUIRE(full->add(referenceContext.scene()) == tvg::Result::Success);
        REQUIRE(full->draw(true) == tvg::Result::Success);
        REQUIRE(full->sync() == tvg::Result::Success);
        CHECK(bool(output == expected));
        auto caret = editor.caretRect(), fresh = reference.caretRect();
        CHECK(caret.x == doctest::Approx(fresh.x));
        CHECK(caret.y == doctest::Approx(fresh.y));
        CHECK(caret.h == doctest::Approx(fresh.h));
        CHECK(editor.session().selection().anchor == selection.anchor);
        CHECK(editor.session().selection().caret == selection.caret);
        CHECK(document.text() == source);
    }
}

TEST_CASE_FIXTURE(Editor, "repeated_word_edits_match_fresh_markdown_layouts")
{
    std::string source;
    SUBCASE("paragraph")
    {
        source = "**bold** plainword\nsecond *emphasis* plainword\nlast ~~strike~~ plainword";
    }
    SUBCASE("list")
    {
        source = "- first plainword\n- **second** plainword\n  - nested plainword";
    }
    SUBCASE("code")
    {
        source = "```text\nfirst plainword\nsecond plainword\nlast plainword\n```";
    }
    SUBCASE("quote")
    {
        source = "> first plainword\n> **second** plainword\n> last plainword";
    }
    REQUIRE(document.text(source));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    editor.theme(theme);
    raster();
    uint32_t random = 7;
    auto next = [&] {
        return random = random * 1664525u + 1013904223u;
    };
    for (size_t iteration = 0; iteration < 40; ++iteration) {
        INFO(iteration);
        std::vector<tn::Selection> words;
        const auto& text = document.text();
        auto letter = [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        };
        for (size_t first = 0; first < text.size();) {
            if (!letter(text[first])) {
                ++first;
                continue;
            }
            auto end = first;
            while (end < text.size() && letter(text[end]))
                ++end;
            if (end - first >= 4) words.push_back({first, end});
            first = end;
        }
        REQUIRE_FALSE(words.empty());
        auto word = words[next() % words.size()];
        auto start = word.start() + 1 + next() % (word.end() - word.start() - 2);
        auto removed = next() % std::min(size_t(3), word.end() - start);
        auto inserted = next() % 3 == 0 ? std::string{} : next() % 2 ? std::string("More")
                                                                     : std::string("q");
        if (inserted.empty() && !removed) removed = 1;
        editor.select(start, start + removed);
        std::string error;
        REQUIRE(editor.insert(inserted, error));
        draw();
        ui::TextEditor reference(document);
        reference.font(fonts.plain);
        reference.options(options);
        reference.theme(theme);
        auto selection = editor.session().selection();
        reference.select(selection.anchor, selection.caret);
        reference.scroll(editor.scrollX(), editor.scrollY());
        ui::Context referenceContext;
        referenceContext.begin(theme, viewport);
        reference.render(referenceContext, "editor", bounds);
        referenceContext.end();
        std::vector<uint32_t> expected(output.size());
        std::unique_ptr<tvg::SwCanvas> full(tvg::SwCanvas::gen());
        REQUIRE(full->target(expected.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
        REQUIRE(full->add(referenceContext.scene()) == tvg::Result::Success);
        REQUIRE(full->draw(true) == tvg::Result::Success);
        REQUIRE(full->sync() == tvg::Result::Success);
        CHECK(bool(output == expected));
        auto caret = editor.caretRect(), fresh = reference.caretRect();
        CHECK(caret.x == doctest::Approx(fresh.x));
        CHECK(caret.y == doctest::Approx(fresh.y));
        CHECK(caret.h == doctest::Approx(fresh.h));
    }
}

TEST_CASE_FIXTURE(Editor, "horizontal_viewport_limits_text_without_changing_document_geometry")
{
    std::string source;
    for (size_t i = 0; i < 500; ++i)
        source += "AV\t가나 To 0123 ";
    auto options = editor.options();
    SUBCASE("plain")
    {
        options.markdown = false;
    }
    SUBCASE("markdown")
    {
        options.markdown = true;
        source = "# **" + source + "** `" + source + "`";
    }
    editor.options(options);
    editor.font(fonts.fallback);
    REQUIRE(document.text(source));
    draw();
    for (auto x : {0.0f, 1000.25f, 5000.5f, 0.0f}) {
        editor.scroll(x, 0);
        draw();
        std::vector<TextPose> poses;
        textPoses(context.scene(), poses);
        size_t bytes = 0;
        for (auto pose : poses) {
            auto value = std::string_view(pose.text->text());
            CHECK(value.size() < 512);
            if (!value.empty()) CHECK((static_cast<unsigned char>(value.front()) & 0xc0) != 0x80);
            bytes += value.size();
        }
        CHECK(bytes > 0);
        CHECK(bytes < 1024);
        CHECK(document.text() == source);
    }
    editor.select(source.size(), source.size());
    draw();
    CHECK(editor.scrollX() > 5000);
    auto end = editor.caretRect();
    std::string error;
    REQUIRE(editor.insert("!", error));
    draw();
    REQUIRE(editor.undo());
    draw();
    CHECK(document.text() == source);
    CHECK(editor.caretRect().x == doctest::Approx(end.x));
}

TEST_CASE_FIXTURE(Editor, "horizontal_markdown_scroll_matches_fresh_views")
{
    std::string body;
    for (size_t i = 0; i < 150; ++i)
        body += "AV\tWj To ";
    REQUIRE(document.text("*" + body + "* **" + body + "** `" + body + "` ~~" + body + "~~ [" + body + "](target)"));
    auto options = editor.options();
    options.markdown = true;
    options.lineNumbers = true;
    editor.options(options);
    editor.theme(theme);
    editor.font(fonts.kern);
    raster();
    for (auto x : {0.0f, 100.25f, 1000.5f, 5000.75f, 10000.0f, 20000.0f, 0.0f}) {
        editor.scroll(x, 0);
        draw();
        ui::TextEditor reference(document);
        reference.font(fonts.kern);
        reference.options(options);
        reference.theme(theme);
        ui::Context referenceContext;
        auto refresh = [&] {
            referenceContext.begin(theme, viewport);
            reference.render(referenceContext, "editor", bounds);
            referenceContext.end();
        };
        refresh();
        reference.scroll(editor.scrollX(), editor.scrollY());
        refresh();
        std::vector<uint32_t> expected(output.size());
        std::unique_ptr<tvg::SwCanvas> full(tvg::SwCanvas::gen());
        REQUIRE(full->target(expected.data(), 400, 400, 300, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
        REQUIRE(full->add(referenceContext.scene()) == tvg::Result::Success);
        REQUIRE(full->draw(true) == tvg::Result::Success);
        REQUIRE(full->sync() == tvg::Result::Success);
        CHECK(bool(output == expected));
    }
}

TEST_CASE_FIXTURE(Editor, "fallback_glyphs_keep_the_primary_face_for_surrounding_text")
{
    REQUIRE(document.text("A가V\nAVAV"));
    auto options = editor.options();
    SUBCASE("plain") {}
    SUBCASE("markdown")
    {
        options.markdown = true;
    }
    SUBCASE("heading")
    {
        options.markdown = true;
        REQUIRE(document.text("# A가V\nAVAV"));
    }
    editor.options(options);
    raster();
    std::vector<TextPose> poses;
    textPoses(context.scene(), poses);
    auto advanceOf = [&](std::string_view value) {
        auto at = std::find_if(poses.begin(), poses.end(), [&](const auto& pose) { return std::string_view(pose.text->text()) == value; });
        REQUIRE(at != poses.end());
        tvg::GlyphMetrics metrics{};
        REQUIRE(at->text->metrics(std::string(value).c_str(), metrics) == tvg::Result::Success);
        return metrics.advance;
    };
    auto expectedAdvance = [&](ui::FontId font, const char* value, float scale = 1) {
        std::unique_ptr<tvg::Text> text(tvg::Text::gen());
        REQUIRE(text->font(fonts.manager->textFont(font)) == tvg::Result::Success);
        REQUIRE(text->size(options.fontSize * scale * 72.0f / 96.0f) == tvg::Result::Success);
        tvg::GlyphMetrics metrics{};
        REQUIRE(text->metrics(value, metrics) == tvg::Result::Success);
        return metrics.advance;
    };
    auto scale = document.text().starts_with("#") ? 2 : 1;
    CHECK(advanceOf("가") == doctest::Approx(expectedAdvance(fonts.fallback, "가", scale)));
    CHECK(advanceOf("V") == doctest::Approx(expectedAdvance(fonts.plain, "V", scale)));
    auto width = 2 * (expectedAdvance(fonts.plain, "A") + expectedAdvance(fonts.plain, "V"));
    CHECK(editor.rangeRects(document.lineStart(1), document.lineEnd(1)).front().w == doctest::Approx(width));
}

TEST_CASE_FIXTURE(Editor, "loading_a_fallback_font_remeasures_only_missing_glyph_rows")
{
    ui::unloadFonts();
    auto primary = ui::loadFont(ui::test::fontPath("M1Plain.ttf").c_str());
    REQUIRE(primary);
    editor.font(primary);
    REQUIRE(document.text("A가V\nAVAV"));
    raster();
    std::vector<TextPose> before;
    textPoses(context.scene(), before);
    auto found = std::find_if(before.begin(), before.end(), [](const auto& pose) { return pose.text->text() && std::string_view(pose.text->text()) == "AVAV"; });
    REQUIRE(found != before.end());
    found->text->ref();
    std::unique_ptr<tvg::Text, void (*)(tvg::Text*)> retained(found->text, [](tvg::Text* text) { text->unref(); });
    auto fallback = ui::loadFont(ui::test::fontPath("M1Fallback.otf").c_str());
    REQUIRE(fallback);
    REQUIRE(fonts.manager->fallback(primary, 0xac00) == fallback);
    draw();
    std::vector<TextPose> after;
    textPoses(context.scene(), after);
    CHECK(std::any_of(after.begin(), after.end(), [&](const auto& pose) { return pose.text == retained.get(); }));
    CHECK(std::any_of(after.begin(), after.end(), [](const auto& pose) { return pose.text->text() && std::string_view(pose.text->text()) == "가"; }));
    CHECK(document.text() == "A가V\nAVAV");
}

TEST_CASE_FIXTURE(Editor, "platform_fallback_preserves_existing_selections")
{
    REQUIRE(!fonts.manager->glyph(fonts.plain, 0xac00));
    REQUIRE(fonts.manager->fallback(fonts.plain, 0xac00) == fonts.fallback);
    std::ifstream input(ui::test::fontPath("M1Fallback.otf"), std::ios::binary);
    REQUIRE(input);
    std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto platform = ui::loadFont("Platform fallback", bytes.data(), bytes.size());
    REQUIRE(platform);
    fonts.manager->preferFallback(fonts.plain, 0xac00, platform);
    CHECK(fonts.manager->fallback(fonts.plain, 0xac00) == fonts.fallback);
    REQUIRE(!fonts.manager->cachedFallback(fonts.kern, 0xac00));
    fonts.manager->preferFallback(fonts.kern, 0xac00, platform);
    CHECK(fonts.manager->fallback(fonts.kern, 0xac00) == platform);
    fonts.manager->preferFallback(fonts.plain, 'A', platform);
    CHECK(fonts.manager->fallback(fonts.plain, 'A') == fonts.plain);
    fonts.manager->preferFallback(fonts.plain, 0xac00, fonts.kern);
    CHECK(fonts.manager->fallback(fonts.plain, 0xac00) == fonts.fallback);
}

TEST_CASE_FIXTURE(Editor, "platform_lookup_keeps_cached_substitute_rows")
{
    std::ifstream input(ui::test::fontPath("M1Fallback.otf"), std::ios::binary);
    REQUIRE(input);
    std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto count = (uint8_t(bytes[4]) << 8) | uint8_t(bytes[5]);
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        auto at = size_t(12 + i * 16);
        if (std::string_view(bytes.data() + at, 4) != "hmtx") continue;
        size_t first = 0, length = 0;
        for (size_t j = 0; j < 4; ++j) {
            first = (first << 8) | uint8_t(bytes[at + 8 + j]);
            length = (length << 8) | uint8_t(bytes[at + 12 + j]);
        }
        REQUIRE(first + length <= bytes.size());
        for (size_t j = 0; j + 4 <= length; j += 4) {
            bytes[first + j] = 6;
            bytes[first + j + 1] = 64;
        }
        changed = true;
    }
    REQUIRE(changed);
    auto platform = ui::loadFont("Platform metrics", bytes.data(), bytes.size());
    REQUIRE(platform);
    REQUIRE(document.text("A가V\nAVAV"));
    raster();
    auto width = editor.rangeRects(1, 4).front().w;
    std::vector<TextPose> poses;
    textPoses(context.scene(), poses);
    auto found = std::find_if(poses.begin(), poses.end(), [](const auto& pose) { return pose.text->text() && std::string_view(pose.text->text()) == "AVAV"; });
    REQUIRE(found != poses.end());
    found->text->ref();
    std::unique_ptr<tvg::Text, void (*)(tvg::Text*)> retained(found->text, [](tvg::Text* text) { text->unref(); });
    auto generation = fonts.manager->generation();
    fonts.manager->preferFallback(fonts.plain, 0xac00, platform);
    CHECK(fonts.manager->generation() == generation);
    draw();
    CHECK(editor.rangeRects(1, 4).front().w == doctest::Approx(width));
    poses.clear();
    textPoses(context.scene(), poses);
    CHECK(std::any_of(poses.begin(), poses.end(), [&](const auto& pose) { return pose.text == retained.get(); }));
}

TEST_CASE_FIXTURE(Editor, "color_changes_preserve_measured_text_and_apply_to_cached_paints")
{
    REQUIRE(document.text("AVAV\n# Heading"));
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    raster();
    std::vector<TextPose> before;
    textPoses(context.scene(), before);
    auto found = std::find_if(before.begin(), before.end(), [](const auto& pose) { return pose.text->text() && std::string_view(pose.text->text()) == "AVAV"; });
    REQUIRE(found != before.end());
    found->text->ref();
    std::unique_ptr<tvg::Text, void (*)(tvg::Text*)> retained(found->text, [](tvg::Text* text) { text->unref(); });
    auto width = editor.rangeRects(0, 4).front().w;
    theme.text = {12, 172, 92};
    editor.theme(theme);
    editor.options(options);  // Unchanged options must not discard layout during a theme update.
    CHECK(pixels(theme.text) > 0);
    CHECK(editor.rangeRects(0, 4).front().w == doctest::Approx(width));
    std::vector<TextPose> after;
    textPoses(context.scene(), after);
    CHECK(std::any_of(after.begin(), after.end(), [&](const auto& pose) { return pose.text == retained.get(); }));
}
