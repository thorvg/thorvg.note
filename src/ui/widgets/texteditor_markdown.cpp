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

std::pair<uint32_t, size_t> wordCharacter(std::string_view text)
{
    if (text.empty()) return {};
    auto first = static_cast<unsigned char>(text[0]);
    if (first < 0x80) return {first, 1};
    if (text.size() < 3 || first < 0xe0 || first > 0xef) return {};
    auto second = static_cast<unsigned char>(text[1]), third = static_cast<unsigned char>(text[2]);
    if ((second & 0xc0) != 0x80 || (third & 0xc0) != 0x80 || (first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)) return {};
    return {uint32_t(first & 15) << 12 | uint32_t(second & 63) << 6 | uint32_t(third & 63), 3};
}

bool wordLetter(uint32_t cp)
{
    // These Hangul ranges are neither whitespace nor punctuation in the vendored MD4C tables.
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= 0x1100 && cp <= 0x11ff) || (cp >= 0x3131 && cp <= 0x318e) || (cp >= 0xac00 && cp <= 0xd7a3);
}

bool wordLetters(std::string_view text)
{
    while (!text.empty()) {
        auto [cp, length] = wordCharacter(text);
        if (!length || !wordLetter(cp)) return false;
        text.remove_prefix(length);
    }
    return true;
}

bool wordBefore(std::string_view text, size_t end)
{
    auto first = end - 1;
    while (first && end - first < 4 && (static_cast<unsigned char>(text[first]) & 0xc0) == 0x80)
        --first;
    auto [cp, length] = wordCharacter(text.substr(first, end - first));
    return length == end - first && wordLetter(cp);
}

bool wordAfter(std::string_view text, size_t first)
{
    if (first == text.size()) return true;  // A row end has the newline's whitespace class.
    auto [cp, length] = wordCharacter(text.substr(first));
    return length && (wordLetter(cp) || (cp >= '0' && cp <= '9') || cp == ' ');
}

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
        if (!incremental || plainMarkdown || markdownBlocks.empty() || composedRows || source.size() > std::numeric_limits<uint32_t>::max() || document.lineCount() != view.lineCount() || firstRow != document.lineStart(document.lineAt(end))) return false;
        auto row = document.lineAt(start);
        const auto& previous = view.line(row);
        if (previous.start() != firstRow) return false;
        auto shift = ptrdiff_t(source.size()) - ptrdiff_t(view.line(view.lineCount() - 1).end());
        auto oldEnd = ptrdiff_t(end) - shift;
        if (oldEnd < ptrdiff_t(start) || start <= firstRow || oldEnd > ptrdiff_t(previous.end()) || end > lastRow) return false;
        auto first = start - firstRow, stop = size_t(oldEnd) - firstRow;
        if (stop > previous.length || previous.length - (stop - first) + (end - start) != changed.size()) return false;
        if (!wordBefore(changed, first) || !wordAfter(changed, end - firstRow) || !wordLetters(source.substr(start, end - start))) return false;
        // Keep escaping, references, entities, HTML and permissive autolinks on the parser path.
        constexpr auto sensitive = "\\[]<&@:./\t";
        if (changed.find_first_of(sensitive) != changed.npos) return false;
        // Pure insertions keep old neighbors intact when clearing preedit discarded its row paint.
        auto found = linePaints.find(row);
        if (found != linePaints.end() && found->second->measured && found->second->display.size() == previous.length) {
            const auto& text = found->second->display;
            if (!wordBefore(text, first) || !wordAfter(text, stop) || !wordLetters(std::string_view(text).substr(first, stop - first)) || text.find_first_of(sensitive) != text.npos) return false;
        } else if (oldEnd != ptrdiff_t(start)) return false;
        auto index = markdownBlock(start);
        auto& block = markdownBlocks[index];
        if (referenceMarkdown && !block.independent) return false;
        auto begin = start - block.start, finish = size_t(oldEnd) - block.start;
        if (size_t(oldEnd) > block.end) return false;
        for (const auto& span : block.spans)
            if ((span.start >= begin && span.start <= finish) || (span.end >= begin && span.end <= finish) || (span.kind == tn::MarkdownKind::Marker && span.start < begin && span.end > finish)) return false;
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
    if (incremental && !plainMarkdown && !markdownBlocks.empty() && !composedRows && changed.find("]:") == source.npos) {
        auto shift = ptrdiff_t(source.size()) - ptrdiff_t(view.line(view.lineCount() - 1).end());
        auto oldEnd = size_t(ptrdiff_t(end) - shift);
        auto first = markdownBlock(start), last = markdownBlock(oldEnd);
        if (std::all_of(markdownBlocks.begin() + first, markdownBlocks.begin() + last + 1, [](const auto& block) { return block.independent; })) {
            if (first && markdownBlocks[first - 1].independent) --first;
            if (last + 1 < markdownBlocks.size() && markdownBlocks[last + 1].independent) ++last;
            auto begin = markdownBlocks[first].start;
            auto stop = size_t(ptrdiff_t(markdownBlocks[last].end) + shift);
            // Preserve the boundary rows that isolate an adjacent semantic block, including HTML.
            bool isolated = (!first || start > document.lineEnd(document.lineAt(begin))) && (last + 1 == markdownBlocks.size() || end < document.lineStart(document.lineAt(stop ? stop - 1 : 0)));
            auto replacement = isolated ? tn::parseMarkdownBlocks(source.substr(begin, stop - begin)) : std::vector<tn::MarkdownBlock>{};
            if (!replacement.empty() && std::all_of(replacement.begin(), replacement.end(), [](const auto& block) { return block.independent; })) {
                for (auto& block : replacement) {
                    block.start += begin;
                    block.end += begin;
                }
                for (size_t i = last + 1; i < markdownBlocks.size(); ++i) {
                    markdownBlocks[i].start += shift;
                    markdownBlocks[i].end += shift;
                }
                auto at = markdownBlocks.erase(markdownBlocks.begin() + first, markdownBlocks.begin() + last + 1);
                markdownBlocks.insert(at, std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
                markdownFormats.clear();
                markdownRevision = document.revision();
                start = begin;
                end = stop;
                return;
            }
        }
    }
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
        auto shift = ptrdiff_t(source.size()) - ptrdiff_t(view.line(view.lineCount() - 1).end());
        auto oldEnd = size_t(ptrdiff_t(end) - shift);
        size_t first = markdownBlock(start), last = markdownBlock(oldEnd);
        // A newly open delimiter can reach every formerly independent row in its paragraph.
        while (first && markdownBlocks[first].independent && markdownBlocks[first - 1].independent)
            --first;
        while (last + 1 < markdownBlocks.size() && markdownBlocks[last].independent && markdownBlocks[last + 1].independent)
            ++last;
        if (first) --first;
        if (last + 1 < markdownBlocks.size()) ++last;
        auto begin = markdownBlocks[first].start;
        std::vector<tn::MarkdownBlock> replacement;
        auto same = [](const auto& a, const auto& b) {
            if (a.spans.size() != b.spans.size()) return false;
            for (size_t i = 0; i < a.spans.size(); ++i) {
                const auto& x = a.spans[i];
                const auto& y = b.spans[i];
                if (x.start != y.start || x.end != y.end || x.kind != y.kind || x.level != y.level || x.element != y.element || x.markerWidth != y.markerWidth) return false;
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

void TextEditor::Impl::prepareRuns(RenderLine& line)
{
    const auto& runs = line.styles;
    line.runs.resize(runs.empty() ? 0 : runs.size() - 1);
    auto& fonts = detail::fontManager();
    for (size_t i = 0; i < runs.size(); ++i) {
        const auto& run = runs[i];
        if (run.end <= line.clipFirst || run.first >= line.clipEnd) {
            if (i == 0) line.body->text("");
            else if (line.runs[i - 1]) line.runs[i - 1]->text->text("");
            continue;
        }
        if (i && !line.runs[i - 1]) line.runs[i - 1] = std::make_unique<RenderRun>();
        auto text = i ? line.runs[i - 1]->text : line.body;
        auto color = run.foreground.value_or(themeTokens.markdownColor(run.format.color));
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
        auto left = prepareText(text, line, &run);
        transform(text, {1, 0, left, 0, 1, run.top, 0, 0, 1});
    }
}
}  // namespace ui
