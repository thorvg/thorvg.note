#include <thornote/documents/widgets.h>
#include <thornote/documents/emoji.h>
#include <common/doctest/doctest.h>
#include <common/engine.h>
#include <thornote/app/app_identity.h>

TEST_CASE("emoji_catalog_recognizes_every_unicode_sequence_as_one_widget")
{
    CHECK(std::size(tn::emojiCatalog()) == 881);
    for (size_t i = 0; i < std::size(tn::emojiCatalog()); ++i) {
        const auto& emoji = tn::emojiCatalog()[i];
        INFO(emoji.code);
        CHECK(tn::emojiIndex(emoji.text) == i);
        std::string source = "before " + std::string(emoji.text) + " after";
        std::vector<ui::TextWidget> ranges;
        tn::matchEmoji(source, 100, ranges);
        REQUIRE(ranges.size() == 1);
        CHECK(ranges.front().start == 107);
        CHECK(ranges.front().end == 107 + emoji.text.size());
    }
    std::vector<size_t> matches;
    CHECK(tn::emojiMatches(":", matches) == 881);
    CHECK(tn::emojiMatches(":sparkle", matches) > 0);
    CHECK(tn::emojiCatalog()[matches.front()].text == "\xe2\x9c\xa8");
    CHECK(tn::emojiIndex(":laugh:") == tn::emojiIndex("\xf0\x9f\x98\x86"));
}

TEST_CASE("task_progress_counts_source_and_excludes_fenced_and_indented_code")
{
    tn::Document document;
    REQUIRE(document.text("- [ ] Open\n+ [X] Done\n1. [x] Done\n\nOutside\n\n    - [ ] Code\n```md\n- [ ] Literal\n```\n"));
    tn::DocumentWidgets widgets;
    widgets.update(document);
    CHECK(widgets.total == 3);
    CHECK(widgets.completed == 2);
    tn::EditSession session(document);
    session.select(3, 4);
    REQUIRE(session.insert("x"));
    widgets.update(document);
    CHECK(widgets.completed == 3);
    REQUIRE(session.undo());
    widgets.update(document);
    CHECK(widgets.completed == 2);
    REQUIRE(session.redo());
    widgets.update(document);
    CHECK(widgets.completed == 3);
    REQUIRE(document.text(""));
    widgets.update(document);
    CHECK(widgets.total == 0);
    CHECK(widgets.completed == 0);
}

TEST_CASE("task_progress_includes_tab_and_space_indented_child_items")
{
    tn::Document document;
    REQUIRE(document.text("- [x] First\n- [x] Second\n- [x] Third\n\t- [ ] Child\n\t\t- [X] Grandchild\n    - [ ] Sibling\n\nOutside\n\n    - [ ] Code\n\t- [ ] Code\n"));
    tn::DocumentWidgets widgets;
    widgets.update(document);
    CHECK(widgets.total == 6);
    CHECK(widgets.completed == 4);
    tn::EditSession session(document);
    auto at = document.text().find("[ ] Child") + 1;
    session.select(at, at + 1);
    REQUIRE(session.insert("x"));
    widgets.update(document);
    CHECK(widgets.total == 6);
    CHECK(widgets.completed == 5);
    REQUIRE(session.undo());
    widgets.update(document);
    CHECK(widgets.completed == 4);
}

TEST_CASE("task_progress_excludes_code_inside_list_items")
{
    tn::Document document;
    REQUIRE(document.text("- [ ] Parent\n      - [ ] Indented code\n\n    ```md\n    - [ ] Fenced code\n    ```\n\t- [x] Child\n"));
    tn::DocumentWidgets widgets;
    widgets.update(document);
    CHECK(widgets.total == 2);
    CHECK(widgets.completed == 1);
}

TEST_CASE("task_progress_preserves_list_context_across_chunks_and_parent_edits")
{
    std::string source = "- [x] Parent\n";
    for (size_t i = 0; i < 6000; ++i)
        source += "  continuation\n";
    source += "\t- [ ] Child\n";
    tn::Document document;
    REQUIRE(document.text(source));
    tn::DocumentWidgets widgets;
    widgets.update(document);
    CHECK(widgets.total == 2);
    CHECK(widgets.completed == 1);
    tn::EditSession session(document);
    session.select(0, 2);
    REQUIRE(session.insert("Plain "));
    widgets.update(document);
    CHECK(widgets.total == 0);
    CHECK(widgets.completed == 0);
    REQUIRE(session.undo());
    widgets.update(document);
    CHECK(widgets.total == 2);
    CHECK(widgets.completed == 1);
}

TEST_CASE("incremental_task_progress_matches_full_scans_across_chunk_and_fence_edits")
{
    tn::Document document;
    std::string source;
    for (size_t row = 0; row < 18000; ++row)
        source += row % 7 == 0 ? "- [x] Completed task\n" : "- [ ] Pending task\n";
    REQUIRE(document.text(source));
    tn::EditSession session(document);
    tn::DocumentWidgets widgets;
    widgets.update(document);
    uint32_t state = 12345;
    auto next = [&] {
        state = state * 1664525 + 1013904223;
        return state;
    };
    for (size_t step = 0; step < 180; ++step) {
        auto at = next() % (document.text().size() + 1);
        auto end = std::min(at + next() % 19, document.text().size());
        std::string_view insertion;
        switch (step % 5) {
            case 0: insertion = "\n- [ ] New\n"; break;
            case 1: insertion = "```\n"; break;
            case 2: insertion = "text"; break;
            case 3: insertion = "\n~~~\n"; break;
            default: insertion = ""; break;
        }
        session.select(at, end);
        REQUIRE(session.insert(insertion));
        auto verify = [&] {
            widgets.update(document);
            tn::DocumentWidgets fresh;
            fresh.update(document);
            INFO(step);
            CHECK(widgets.total == fresh.total);
            CHECK(widgets.completed == fresh.completed);
        };
        verify();
        if (step % 13 == 0) {
            REQUIRE(session.undo());
            verify();
            REQUIRE(session.redo());
            verify();
        }
    }
}

TEST_CASE("markdown_image_references_preserve_source_and_normal_row_heights")
{
    tn::test::Engine engine;
    struct Fonts
    {
        ~Fonts() { ui::unloadFonts(); }
    } fonts;
    ui::Context context;
    auto theme = ui::Theme::light();
    tn::Document document;
    std::string source = "![image](https://example.test/picture.png)\n![vector](local.svg)\n![animation](https://example.test/animation.gif)\n![lottie](animation.json)\n![]()\nNext line";
    REQUIRE(document.text(source));
    ui::TextEditor editor(document);
    auto font = ui::loadFont(tn::utf8(tn::appData("assets/Pretendard-Regular.ttf")).c_str());
    REQUIRE(font);
    editor.font(font);
    auto options = editor.options();
    options.markdown = true;
    editor.options(options);
    auto end = document.lineStart(5);
    editor.select(end, end);
    auto draw = [&] {
        context.begin(theme, {0, 0, 600, 400});
        editor.render(context, "editor", {0, 0, 600, 400});
        context.end();
        return editor.caretRect().y;
    };
    auto baseline = draw();
    tn::DocumentWidgets widgets;
    widgets.attach(editor);
    CHECK(draw() == doctest::Approx(baseline));
    CHECK(document.text() == source);
}

TEST_CASE("document_emojis_keep_playing_during_text_edits_and_composition")
{
    tn::test::Engine engine;
    struct Fonts
    {
        ~Fonts() { ui::unloadFonts(); }
    } fonts;
    auto asset = std::make_shared<ui::IconAsset>();
    asset->name = "emoji/test";
    asset->mime = "lot";
    asset->loop = true;
    asset->tint = false;
    asset->data = R"({"v":"5.7.4","w":24,"h":24,"fr":60,"ip":0,"op":60,"assets":[],"layers":[{"ty":4,"ind":1,"ip":0,"op":60,"st":0,"sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"s":{"a":0,"k":[100,100,100]},"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]}},"shapes":[{"ty":"el","p":{"a":1,"k":[{"t":0,"s":[6,12],"e":[18,12],"o":{"x":0.33,"y":0.33},"i":{"x":0.67,"y":0.67}},{"t":59,"s":[18,12]}]},"s":{"a":0,"k":[6,6]}},{"ty":"fl","c":{"a":0,"k":[1,0,0,1]},"o":{"a":0,"k":100},"r":1}]}]})";
    ui::Context context;
    context.iconLoader([&](std::string_view) -> std::shared_ptr<const ui::IconAsset> { return asset; });
    auto theme = ui::Theme::light();
    tn::Document document;
    REQUIRE(document.text("😗 tail"));
    ui::TextEditor editor(document);
    auto font = ui::loadFont(tn::utf8(tn::appData("assets/Pretendard-Regular.ttf")).c_str());
    REQUIRE(font);
    editor.font(font);
    auto options = editor.options();
    options.fontSize = 64;
    options.lineNumbers = false;
    editor.options(options);
    tn::DocumentWidgets widgets;
    widgets.attach(editor);
    std::vector<uint32_t> pixels(400 * 200);
    std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen(tvg::EngineOption::SmartRender));
    REQUIRE(canvas->target(pixels.data(), 400, 400, 200, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);
    REQUIRE(canvas->add(context.scene()) == tvg::Result::Success);
    auto draw = [&](double time) {
        context.begin(theme, {0, 0, 400, 200}, time);
        editor.render(context, "editor", {0, 0, 400, 200});
        context.end();
        REQUIRE(canvas->update() == tvg::Result::Success);
        REQUIRE(canvas->draw(false) == tvg::Result::Success);
        REQUIRE(canvas->sync() == tvg::Result::Success);
        auto partial = pixels;
        REQUIRE(canvas->update() == tvg::Result::Success);
        REQUIRE(canvas->draw(true) == tvg::Result::Success);
        REQUIRE(canvas->sync() == tvg::Result::Success);
        CHECK((pixels == partial));
    };
    draw(0);
    auto rect = editor.rangeRects(0, 4).front();
    auto crop = [&] {
        std::vector<uint32_t> result;
        for (auto y = static_cast<int>(rect.y); y < int(rect.y + rect.h); ++y)
            for (auto x = static_cast<int>(rect.x); x < int(rect.x + rect.w); ++x)
                result.push_back(pixels[y * 400 + x] == 0xffff0000u);
        return result;
    };
    draw(.25);
    auto before = crop();
    std::string error;
    REQUIRE(editor.applyEdits({.revision = document.revision(), .edits = {{document.text().size(), document.text().size(), "x"}}}, error));
    draw(.3);
    CHECK((crop() != before));
    REQUIRE(context.nextFrame());
    CHECK(*context.nextFrame() > .3);
    CHECK(*context.nextFrame() < .4);
    before = crop();
    draw(.6);
    CHECK((crop() != before));
    context.focus("editor");
    editor.select(document.text().size(), document.text().size());
    draw(.65);
    before = crop();
    editor.composition("x", 1);
    REQUIRE(editor.composing());
    draw(.75);
    CHECK((crop() != before));
    before = crop();
    draw(.9);
    CHECK((crop() != before));
    before = crop();
    editor.composition("", 0);
    draw(1.1);
    CHECK((crop() != before));
}
