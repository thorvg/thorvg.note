#include "texteditor.h"
#include <ui/core/text/internal.h>
#include <algorithm>
#include <limits>

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

bool markdownSyntax(std::string_view text)
{
    unsigned indent = 0;
    bool leading = true;
    for (size_t i = 0; i < text.size(); ++i) {
        auto ch = text[i];
        if (ch == '\n' || ch == '\r') {
            leading = true;
            indent = 0;
            continue;
        }
        if (leading) {
            if (ch == ' ') {
                if (++indent == 4) return true;
                continue;
            }
            leading = false;
            if (ch >= '0' && ch <= '9') {
                auto end = text.find_first_not_of("0123456789", i);
                if (end != text.npos && (text[end] == '.' || text[end] == ')')) return true;
            }
        }
        switch (ch) {
            case '#':
            case '*':
            case '_':
            case '`':
            case '~':
            case '\\':
            case '[':
            case '<':
            case '>':
            case '=':
            case '-':
            case '+':
            case '@':
            case '&':
            case '\t': return true;
            case ':':
                if (text.substr(i, 3) == "://") return true;
                break;
            case '.':
                if (i >= 3 && (text[i - 1] | 32) == 'w' && (text[i - 2] | 32) == 'w' && (text[i - 3] | 32) == 'w') return true;
                break;
        }
    }
    return false;
}
}  // namespace
size_t TextEditor::Impl::markdownBlock(size_t offset) const
{
    auto at = std::upper_bound(markdownBlocks.begin(), markdownBlocks.end(), offset, [](size_t value, const auto& block) { return value < block.start; });
    return at == markdownBlocks.begin() ? 0 : size_t(at - markdownBlocks.begin() - 1);
}

void TextEditor::Impl::parseMarkdown(size_t& start, size_t& end, bool incremental)
{
    const auto& document = editing.document();
    std::string_view source = document.text();
    auto firstRow = document.lineStart(document.lineAt(start));
    auto lastRow = document.lineEnd(document.lineAt(end));
    auto changed = source.substr(firstRow, lastRow - firstRow);
    auto wordEdit = [&] {
        if (!incremental || plainMarkdown || referenceMarkdown || markdownBlocks.empty() || composedRows || source.size() > std::numeric_limits<uint32_t>::max() || document.lineCount() != view.layout().size() || firstRow != document.lineStart(document.lineAt(end))) return false;
        auto row = document.lineAt(start);
        auto found = linePaints.find(row);
        if (found == linePaints.end() || !found->second->measured) return false;
        const auto& previous = view.layout()[row];
        const auto& text = found->second->display;
        if (previous.start() != firstRow || text.size() != previous.length) return false;
        auto shift = ptrdiff_t(source.size()) - ptrdiff_t(view.layout().back().end());
        auto oldEnd = ptrdiff_t(end) - shift;
        if (oldEnd < ptrdiff_t(start) || start <= firstRow || oldEnd > ptrdiff_t(previous.end()) || end > lastRow) return false;
        auto letter = [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        };
        auto alphanumeric = [&](unsigned char ch) {
            return letter(ch) || (ch >= '0' && ch <= '9');
        };
        auto wordEnd = [&](unsigned char ch) {
            return alphanumeric(ch) || ch == ' ' || ch == '\n';
        };
        auto first = start - firstRow, stop = size_t(oldEnd) - firstRow;
        auto oldRight = stop == text.size() ? '\n' : text[stop];
        auto newRight = end == lastRow ? '\n' : source[end];
        if (!alphanumeric(text[first - 1]) || !wordEnd(oldRight) || !alphanumeric(source[start - 1]) || !wordEnd(newRight)) return false;
        if (!std::all_of(text.begin() + first, text.begin() + stop, letter) || !std::all_of(source.begin() + start, source.begin() + end, letter)) return false;
        // Keep escaping, references, entities, HTML and permissive autolinks on the parser path.
        constexpr auto sensitive = "\\[]<&@:./\t";
        if (text.find_first_of(sensitive) != text.npos || changed.find_first_of(sensitive) != changed.npos) return false;
        auto index = markdownBlock(start);
        auto& block = markdownBlocks[index];
        auto begin = start - block.start, finish = size_t(oldEnd) - block.start;
        if (size_t(oldEnd) > block.end) return false;
        for (const auto& span : block.spans)
            if ((span.start >= begin && span.start <= finish) || (span.end >= begin && span.end <= finish)) return false;
        auto relocate = [&](auto& offset) {
            if (offset > finish) offset = size_t(ptrdiff_t(offset) + shift);
        };
        if (shift) {
            for (auto& span : block.spans) {
                relocate(span.start);
                relocate(span.end);
            }
            auto formats = markdownFormats.find(index);
            if (formats != markdownFormats.end()) {
                for (auto& format : formats->second) {
                    relocate(format.start);
                    relocate(format.end);
                }
            }
            block.end += shift;
            for (size_t i = index + 1; i < markdownBlocks.size(); ++i) {
                markdownBlocks[i].start += shift;
                markdownBlocks[i].end += shift;
            }
        }
        markdownRevision = document.revision();
        return true;
    };
    if (wordEdit()) return;
    if (!incremental) plainMarkdown = !markdownSyntax(source);
    else if (plainMarkdown) plainMarkdown = !markdownSyntax(changed);
    markdownFormats.clear();
    if (plainMarkdown) {
        markdownBlocks.clear();
        referenceMarkdown = false;
        markdownRevision = document.revision();
        return;
    } else if (!incremental || markdownBlocks.empty() || referenceMarkdown || changed.find("]:") != source.npos) {
        markdownBlocks = tn::parseMarkdownBlocks(source);
        referenceMarkdown = source.find("]:") != source.npos;
        start = 0;
        end = source.size();
    } else {
        auto shift = ptrdiff_t(source.size()) - ptrdiff_t(view.layout().back().end());
        auto oldEnd = size_t(ptrdiff_t(end) - shift);
        size_t first = markdownBlock(start), last = markdownBlock(oldEnd);
        if (first) --first;
        if (last + 1 < markdownBlocks.size()) ++last;
        auto begin = markdownBlocks[first].start;
        std::vector<tn::MarkdownBlock> replacement;
        auto same = [](const auto& a, const auto& b) {
            if (a.spans.size() != b.spans.size()) return false;
            for (size_t i = 0; i < a.spans.size(); ++i) {
                const auto& x = a.spans[i];
                const auto& y = b.spans[i];
                if (x.start != y.start || x.end != y.end || x.kind != y.kind || x.level != y.level || x.element != y.element) return false;
            }
            return true;
        };
        size_t stop = 0;
        for (;;) {
            stop = size_t(ptrdiff_t(markdownBlocks[last].end) + shift);
            replacement = tn::parseMarkdownBlocks(source.substr(begin, stop - begin));
            if (last + 1 == markdownBlocks.size()) break;
            if (!replacement.empty()) {
                const auto& anchor = replacement.back();
                const auto& old = markdownBlocks[last];
                if (anchor.start + begin == size_t(ptrdiff_t(old.start) + shift) && anchor.end + begin == stop && same(anchor, old)) break;
            }
            last = std::min(markdownBlocks.size() - 1, last + (last - first + 1));
        }
        for (auto& block : replacement) {
            block.start += begin;
            block.end += begin;
        }
        if (shift) {
            for (size_t i = last + 1; i < markdownBlocks.size(); ++i) {
                markdownBlocks[i].start += shift;
                markdownBlocks[i].end += shift;
            }
        }
        if (replacement.size() == last - first + 1) {
            std::move(replacement.begin(), replacement.end(), markdownBlocks.begin() + first);
        } else {
            auto at = markdownBlocks.erase(markdownBlocks.begin() + first, markdownBlocks.begin() + last + 1);
            markdownBlocks.insert(at, std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
        }
        start = begin;
        end = stop;
    }
    markdownRevision = document.revision();
}

const std::vector<TextEditor::Impl::Format>& TextEditor::Impl::blockFormats(size_t block)
{
    static const std::vector<Format> empty;
    if (!configuration.markdown || markdownBlocks.empty() || markdownBlocks[block].spans.empty()) return empty;
    auto found = markdownFormats.find(block);
    if (found != markdownFormats.end()) return found->second;
    if (markdownFormats.size() >= 16) markdownFormats.clear();
    auto& formats = markdownFormats[block];
    const auto& spans = markdownBlocks[block].spans;
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
            formats.push_back(format);
        }
        while (edge < edges.size() && edges[edge].offset == offset) {
            auto& event = edges[edge++];
            if (event.enter) active.push_back(event.span);
            else active.erase(std::remove(active.begin(), active.end(), event.span), active.end());
        }
        previous = offset;
    }
    return formats;
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

void TextEditor::Impl::prepareRuns(RenderLine& line)
{
    const auto& runs = line.styles;
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
        auto value = line.display.substr(run.first, run.end - run.first);
        auto first = value.find_first_not_of(' ');
        if (first == std::string::npos) first = value.size();
        text->text(value.c_str() + first);
    }
}
}  // namespace ui
