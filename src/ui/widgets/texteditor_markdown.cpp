#include "texteditor.h"
#include <ui/core/text/internal.h>
#include <algorithm>

namespace ui
{
namespace
{
// Indexed by MarkdownKind; syntax ownership survives parsing so colors do not depend on overlap order.
constexpr Color Theme::*operandColors[] = {
    &Theme::markdownHeading, &Theme::markdownStrong, &Theme::markdownEmphasis, &Theme::markdownCode,
    &Theme::markdownLink, &Theme::markdownImage, &Theme::markdownQuote, &Theme::markdownMarker,
    &Theme::markdownStrike, &Theme::markdownList, &Theme::markdownNumber};
constexpr Color Theme::*operatorColors[] = {
    &Theme::markdownHeadingMarker, &Theme::markdownStrongMarker, &Theme::markdownEmphasisMarker, &Theme::markdownCodeMarker,
    &Theme::markdownLinkMarker, &Theme::markdownImageMarker, &Theme::markdownQuoteMarker, &Theme::markdownMarker,
    &Theme::markdownStrikeMarker, &Theme::markdownListMarker, &Theme::markdownNumberMarker};
static_assert(std::size(operandColors) == size_t(tn::MarkdownKind::NumberedList) + 1);
static_assert(std::size(operatorColors) == std::size(operandColors));
}  // namespace
void TextEditor::Impl::parseMarkdown()
{
    markdownFormats.clear();
    auto spans = tn::parseMarkdown(editing.document().text());
    struct Edge
    {
        size_t offset, span;
        bool enter;
    };
    std::vector<Edge> edges;
    edges.reserve(spans.size() * 2);
    for (size_t i = 0; i < spans.size(); ++i) {
        edges.push_back({spans[i].start, i, true});
        edges.push_back({spans[i].end, i, false});
    }
    std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) { return a.offset < b.offset; });
    std::vector<size_t> active;
    size_t previous = 0;
    for (size_t edge = 0; edge < edges.size();) {
        auto offset = edges[edge].offset;
        if (previous < offset) {
            Format format{.start = previous, .end = offset};
            int priority = 0;
            for (auto index : active) {
                const auto& span = spans[index];
                auto color = span.kind == tn::MarkdownKind::Marker ? operatorColors[size_t(span.element)] : operandColors[size_t(span.kind)];
                int rank = 0;
                switch (span.kind) {
                    case tn::MarkdownKind::Heading:
                        format.heading = span.level;
                        format.bold = true;
                        rank = 3;
                        break;
                    case tn::MarkdownKind::Strong:
                        format.bold = true;
                        rank = 4;
                        break;
                    case tn::MarkdownKind::Emphasis:
                        format.italic = true;
                        rank = 4;
                        break;
                    case tn::MarkdownKind::Strike:
                        format.strike = true;
                        rank = 4;
                        break;
                    case tn::MarkdownKind::Code:
                        format.code = true;
                        rank = 7;
                        break;
                    case tn::MarkdownKind::Image: rank = 6; break;
                    case tn::MarkdownKind::Link:
                        format.link = true;
                        rank = 5;
                        break;
                    case tn::MarkdownKind::Marker: rank = 8; break;
                    case tn::MarkdownKind::Quote: rank = 1; break;
                    case tn::MarkdownKind::List:
                    case tn::MarkdownKind::NumberedList: rank = 2; break;
                }
                if (rank > priority) {
                    priority = rank;
                    format.color = color;
                }
            }
            markdownFormats.push_back(format);
        }
        while (edge < edges.size() && edges[edge].offset == offset) {
            auto& event = edges[edge++];
            if (event.enter) active.push_back(event.span);
            else active.erase(std::remove(active.begin(), active.end(), event.span), active.end());
        }
        previous = offset;
    }
    markdownRevision = editing.document().revision();
    ++textStyleRevision;
}

TextEditor::Impl::Format TextEditor::Impl::formatAt(size_t offset) const
{
    if (!configuration.markdown) return {};
    auto found = std::upper_bound(markdownFormats.begin(), markdownFormats.end(), offset, [](size_t offset, const auto& format) { return offset < format.start; });
    if (found != markdownFormats.begin()) {
        --found;
        if (offset < found->end) return *found;
    }
    return {};
}

Color TextEditor::Impl::markdownColor(Color Theme::*token) const
{
    auto color = themeTokens.*token;
    if (color.a || token == &Theme::text) return color;
    if (token == &Theme::markdownNumber) return markdownColor(&Theme::markdownList);
    if (token == &Theme::markdownNumberMarker) return markdownColor(&Theme::markdownListMarker);
    if (token == &Theme::markdownQuote) return themeTokens.muted;
    if (token == &Theme::markdownCodeBackground) return themeTokens.raised;
    if (token == &Theme::markdownStrong || token == &Theme::markdownEmphasis || token == &Theme::markdownStrike || token == &Theme::markdownList) return themeTokens.text;
    if (std::find(std::begin(operatorColors), std::end(operatorColors), token) != std::end(operatorColors) && themeTokens.markdownMarker.a) return themeTokens.markdownMarker;
    return themeTokens.accent;
}

void TextEditor::Impl::prepareRuns(RenderLine& line, size_t row)
{
    const auto& runs = rowRuns[row];
    line.runs.resize(runs.empty() ? 0 : runs.size() - 1);
    auto& fonts = detail::fontManager();
    for (size_t i = 0; i < runs.size(); ++i) {
        const auto& run = runs[i];
        if (i && !line.runs[i - 1]) line.runs[i - 1] = std::make_unique<RenderRun>();
        auto text = i ? line.runs[i - 1]->text : line.body;
        auto color = markdownColor(run.format.color);
        auto name = run.font ? fonts.textFont(run.font) : themeTokens.font.c_str();
        text->font(name);
        text->size(configuration.fontSize * run.scale * 72.0f / 96.0f);
        text->italic(run.format.italic ? 0.18f : 0);
        text->layout(0, 0);
        text->align(0, 0);
        text->wrap(tvg::TextWrap::None);
        text->fill(color.r, color.g, color.b);
        text->opacity(color.a);
        // A closest-family match may still be regular, even when it is a different face.
        bool nativeBold = run.font && run.font <= fonts.fonts().size() && fonts.fonts()[run.font - 1].weight >= 700;
        float outline = 0;
        if (run.format.heading && !nativeBold) outline = configuration.fontSize * run.scale * 0.035f;
        else if (!run.format.heading && run.format.bold && run.font == documentFont) outline = configuration.fontSize * run.scale * 0.025f;
        text->outline(outline, color.r, color.g, color.b);
        auto value = displayLines[row].substr(run.first, run.end - run.first);
        auto first = value.find_first_not_of(' ');
        if (first == std::string::npos) first = value.size();
        text->text(value.c_str() + first);
    }
}
}  // namespace ui
