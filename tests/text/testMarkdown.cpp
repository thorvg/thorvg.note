#include <text/markdown.h>
#include <common/doctest/doctest.h>
#include <algorithm>
#include <string>

TEST_SUITE_BEGIN("document");

TEST_CASE("markdown_preserves_source_offsets_and_parses_context")
{
    using tn::MarkdownKind;
    std::string text = "# Heading\n\n**bold** and *italic* with `code` and [link](target) ![alt](picture.png)\n\n- item\n1. ordered\n\n```md\n# literal\n```\n";
    auto spans = tn::parseMarkdown(text);
    auto contains = [&](MarkdownKind kind, std::string_view value) {
        return std::any_of(spans.begin(), spans.end(), [&](const auto& span) {
            return span.kind == kind && std::string_view(text).substr(span.start, span.end - span.start) == value;
        });
    };
    CHECK(contains(MarkdownKind::Heading, "# Heading"));
    CHECK(contains(MarkdownKind::Strong, "**bold**"));
    CHECK(contains(MarkdownKind::Emphasis, "*italic*"));
    CHECK(contains(MarkdownKind::Code, "`code`"));
    CHECK(contains(MarkdownKind::Link, "[link](target)"));
    CHECK(contains(MarkdownKind::Image, "![alt](picture.png)"));
    CHECK(contains(MarkdownKind::Marker, "-"));
    CHECK(contains(MarkdownKind::Marker, "1."));
    CHECK(contains(MarkdownKind::Code, "```md\n# literal\n```"));
    CHECK(std::count_if(spans.begin(), spans.end(), [](const auto& span) { return span.kind == MarkdownKind::Heading; }) == 1);
    CHECK(std::all_of(spans.begin(), spans.end(), [&](const auto& span) { return span.start < span.end && span.end <= text.size(); }));
}

TEST_CASE("markdown_handles_setext_nested_spans_references_and_empty_images")
{
    using tn::MarkdownKind;
    std::string text = "Title\n=====\n\n***nested*** and [ref][key] and ![](a(b).png)\n\n[key]: target\n";
    auto spans = tn::parseMarkdown(text);
    auto heading = std::find_if(spans.begin(), spans.end(), [](const auto& span) { return span.kind == MarkdownKind::Heading; });
    REQUIRE(heading != spans.end());
    CHECK(heading->level == 1);
    CHECK(text.substr(heading->start, heading->end - heading->start) == "Title");
    CHECK(std::any_of(spans.begin(), spans.end(), [&](const auto& span) { return span.kind == MarkdownKind::Image && text.substr(span.start, span.end - span.start) == "![](a(b).png)"; }));
    CHECK(std::any_of(spans.begin(), spans.end(), [&](const auto& span) { return span.kind == MarkdownKind::Link && text.substr(span.start, span.end - span.start) == "[ref][key]"; }));
    CHECK(std::any_of(spans.begin(), spans.end(), [](const auto& span) { return span.kind == MarkdownKind::Strong; }));
    CHECK(std::any_of(spans.begin(), spans.end(), [](const auto& span) { return span.kind == MarkdownKind::Emphasis; }));
    CHECK(tn::parseMarkdown("plain text and \\*escaped\\*\n").empty());
}

TEST_CASE("markdown_tracks_empty_fences_autolinks_and_task_markers")
{
    using tn::MarkdownKind;
    std::string text = "```\n```\n\n```text\n`literal\n```\n\n- [x] done\n\n<https://example.test> and https://example.test\n";
    auto spans = tn::parseMarkdown(text);
    auto contains = [&](MarkdownKind kind, std::string_view value) {
        return std::any_of(spans.begin(), spans.end(), [&](const auto& span) { return span.kind == kind && std::string_view(text).substr(span.start, span.end - span.start) == value; });
    };
    CHECK(contains(MarkdownKind::Code, "```\n```"));
    CHECK(contains(MarkdownKind::Code, "```text\n`literal\n```"));
    CHECK(contains(MarkdownKind::Marker, "[x]"));
    CHECK(contains(MarkdownKind::Link, "<https://example.test>"));
    CHECK(contains(MarkdownKind::Link, "https://example.test"));
}

TEST_CASE("markdown_does_not_treat_a_following_bullet_as_a_setext_underline")
{
    std::string text = "### Header\n- plain item\n";
    auto spans = tn::parseMarkdown(text);
    auto item = text.find("plain");
    CHECK(std::none_of(spans.begin(), spans.end(), [&](const auto& span) { return span.kind == tn::MarkdownKind::Marker && span.start <= item && span.end > item; }));
}

TEST_CASE("markdown_keeps_nested_image_links_and_reference_definition_offsets")
{
    using tn::MarkdownKind;
    std::string text = "[key]: target\n\n[![**alt**](img)](url) and ***nested*** and [label][key]\n";
    auto spans = tn::parseMarkdown(text);
    auto contains = [&](MarkdownKind kind, std::string_view value) {
        return std::any_of(spans.begin(), spans.end(), [&](const auto& span) { return span.kind == kind && std::string_view(text).substr(span.start, span.end - span.start) == value; });
    };
    CHECK(contains(MarkdownKind::Link, "[![**alt**](img)](url)"));
    CHECK(contains(MarkdownKind::Image, "![**alt**](img)"));
    CHECK(contains(MarkdownKind::Link, "[label][key]"));
    CHECK(contains(MarkdownKind::Emphasis, "***nested***"));
    CHECK(contains(MarkdownKind::Strong, "**nested**"));
}

TEST_CASE("markdown_operators_retain_their_element_kind")
{
    using tn::MarkdownKind;
    std::string text = "# title #\n\n**bold** *italic* ~~gone~~ `code` [label](target) ![alt](image)\n\n- item\n1. item\n\n> quote\n\n```cpp\ncode\n```\n";
    auto spans = tn::parseMarkdown(text);
    auto marker = [&](MarkdownKind element, std::string_view value) {
        return std::any_of(spans.begin(), spans.end(), [&](const auto& span) {
            return span.kind == MarkdownKind::Marker && span.element == element && std::string_view(text).substr(span.start, span.end - span.start) == value;
        });
    };
    CHECK(marker(MarkdownKind::Heading, "#"));
    CHECK(marker(MarkdownKind::Strong, "**"));
    CHECK(marker(MarkdownKind::Emphasis, "*"));
    CHECK(marker(MarkdownKind::Strike, "~~"));
    CHECK(marker(MarkdownKind::Code, "`"));
    CHECK(marker(MarkdownKind::Code, "```"));
    CHECK(marker(MarkdownKind::Link, "["));
    CHECK(marker(MarkdownKind::Link, "]"));
    CHECK(marker(MarkdownKind::Link, "("));
    CHECK(marker(MarkdownKind::Link, ")"));
    CHECK(marker(MarkdownKind::Image, "!["));
    CHECK(marker(MarkdownKind::List, "-"));
    CHECK(marker(MarkdownKind::NumberedList, "1."));
    CHECK(marker(MarkdownKind::Quote, ">"));
    CHECK(std::any_of(spans.begin(), spans.end(), [](const auto& span) { return span.kind == MarkdownKind::List; }));
}

TEST_SUITE_END();
