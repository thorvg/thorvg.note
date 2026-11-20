#include "markdown.h"
#include <md4c.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace tn
{
namespace
{
// Only closed strong spans and literal words are admitted. Other syntax retains semantic blocks.
bool independentText(std::string_view source)
{
    bool leading = true;
    for (auto ch : source) {
        auto byte = static_cast<unsigned char>(ch);
        if (ch == '\n') {
            leading = true;
            continue;
        }
        if (leading && (ch == ' ' || ch == '\t' || ch == '\r')) return false;
        leading = false;
        if (ch == '*') continue;
        if (byte < 0x80 && ch != ' ' && std::string_view(".,;:!?()'\"").find(ch) == std::string_view::npos && !(ch >= 'a' && ch <= 'z') && !(ch >= 'A' && ch <= 'Z') && !(ch >= '0' && ch <= '9')) return false;
    }
    return true;
}

bool independent(std::string_view source, const std::vector<MarkdownSpan>& spans)
{
    if (!independentText(source)) return false;
    size_t markers = 0;
    auto stars = size_t(std::count(source.begin(), source.end(), '*'));
    for (const auto& span : spans) {
        if (source.find('\n', span.start) < span.end) return false;
        if (span.kind == MarkdownKind::Marker && span.element == MarkdownKind::Strong && span.end - span.start == 2) markers += 2;
        else if (span.kind != MarkdownKind::Strong) return false;
    }
    return markers == stars;
}

struct Frame
{
    int type;
    unsigned level = 0;
    char fence = 0;
    bool ordered = false, checked = false;
    size_t first = std::string_view::npos, end = 0, hint = 0;
};

struct Parser
{
    std::string_view source;
    std::vector<MarkdownSpan> result;
    std::vector<Frame> blocks, spans;
    std::vector<MarkdownBlock> sections;
    bool split = false;
    size_t cursor = 0;

    void flush(bool final)
    {
        auto first = sections.empty() ? 0 : sections.back().end;
        auto end = cursor;
        for (const auto& span : result)
            end = std::max(end, size_t(span.end));
        end = final ? source.size() : std::min(lineEnd(end ? end - 1 : end) + 1, source.size());
        if (end <= first) return;
        std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        for (auto& span : result) {
            span.start -= first;
            span.end -= first;
        }
        result.shrink_to_fit();
        sections.push_back({first, end, std::move(result)});
        result.clear();
    }

    size_t lineStart(size_t offset) const
    {
        auto previous = offset ? source.rfind('\n', offset - 1) : std::string_view::npos;
        return previous == std::string_view::npos ? 0 : previous + 1;
    }
    size_t lineEnd(size_t offset) const { return std::min(source.find('\n', offset), source.size()); }
    void add(size_t first, size_t end, MarkdownKind kind, unsigned level = 0, uint8_t markerWidth = 0)
    {
        if (first < end && end <= source.size()) result.push_back({uint32_t(first), uint32_t(end), kind, uint8_t(level), MarkdownKind::Marker, markerWidth});
    }
    void addMarker(size_t first, size_t end, MarkdownKind element)
    {
        if (first < end && end <= source.size()) result.push_back({uint32_t(first), uint32_t(end), MarkdownKind::Marker, 0, element});
    }
    void text(const char* value, size_t size)
    {
        auto address = reinterpret_cast<uintptr_t>(value), start = reinterpret_cast<uintptr_t>(source.data());
        if (address < start || address - start > source.size() || size > source.size() - (address - start)) return;
        auto first = size_t(address - start), end = first + size;
        for (auto* stack : {&blocks, &spans})
            for (auto& frame : *stack) {
                frame.first = std::min(frame.first, first);
                frame.end = std::max(frame.end, end);
            }
        for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
            if (it->type == MD_BLOCK_CODE) break;
            if (it->type != MD_BLOCK_LI) continue;
            if (it->checked) add(first, end, MarkdownKind::Strike);
            break;
        }
        cursor = end;
    }
    void block()
    {
        auto frame = blocks.back();
        blocks.pop_back();
        if (frame.first == std::string_view::npos) {
            if (frame.type != MD_BLOCK_CODE || !frame.fence) return;
            frame.first = source.find(std::string(3, frame.fence), frame.hint);
            if (frame.first == std::string_view::npos) return;
            frame.end = std::min(lineEnd(frame.first) + 1, source.size());
        }
        auto first = lineStart(frame.first), end = lineEnd(frame.end ? frame.end - 1 : frame.end);
        if (frame.type == MD_BLOCK_H) {
            add(first, end, MarkdownKind::Heading, frame.level);
            auto marker = source.find_first_not_of(" \t>", first);
            auto stop = marker;
            while (stop < end && source[stop] == '#')
                ++stop;
            addMarker(marker, stop, MarkdownKind::Heading);
            auto hashEnd = end;
            while (hashEnd > first && (source[hashEnd - 1] == ' ' || source[hashEnd - 1] == '\t' || source[hashEnd - 1] == '\r'))
                --hashEnd;
            auto closing = hashEnd;
            while (closing > first && source[closing - 1] == '#')
                --closing;
            if (closing < hashEnd && closing > first && (source[closing - 1] == ' ' || source[closing - 1] == '\t'))
                addMarker(closing, hashEnd, MarkdownKind::Heading);
            auto next = end < source.size() ? end + 1 : end;
            if (next < source.size()) {
                auto underline = source.substr(next, lineEnd(next) - next);
                if (!underline.empty() && underline.find_first_not_of("=- \t\r") == std::string_view::npos) addMarker(next, lineEnd(next), MarkdownKind::Heading);
            }
        } else if (frame.type == MD_BLOCK_CODE) {
            if (frame.fence) {
                auto opener = source.find(std::string(3, frame.fence), frame.hint);
                if (opener != std::string_view::npos && opener <= frame.first) first = lineStart(opener);
                auto next = end < source.size() ? end + 1 : end;
                auto nonspace = source.find_first_not_of(" \t>", next);
                if (nonspace != std::string_view::npos && source[nonspace] == frame.fence) end = lineEnd(next);
            }
            add(first, end, MarkdownKind::Code);
            if (frame.fence) {
                for (auto line : {first, lineStart(end ? end - 1 : end)}) {
                    if (line != first && line < frame.end) continue;
                    auto marker = source.find_first_not_of(" \t>", line), stop = marker;
                    while (stop < end && source[stop] == frame.fence)
                        ++stop;
                    addMarker(marker, stop, MarkdownKind::Code);
                }
            }
            cursor = std::max(cursor, end);
        } else if (frame.type == MD_BLOCK_QUOTE) {
            add(first, end, MarkdownKind::Quote);
            for (auto line = first; line < end;) {
                auto stop = lineEnd(line);
                for (auto marker = line; marker < stop && (source[marker] == ' ' || source[marker] == '\t' || source[marker] == '>'); ++marker)
                    if (source[marker] == '>') addMarker(marker, marker + 1, MarkdownKind::Quote);
                line = stop + 1;
            }
        } else if (frame.type == MD_BLOCK_LI) {
            auto kind = frame.ordered ? MarkdownKind::NumberedList : MarkdownKind::List;
            add(first, end, kind);
            auto marker = source.find_first_not_of(" \t>", first);
            if (marker == std::string_view::npos || marker > frame.first) return;
            auto stop = marker;
            if (source[stop] == '-' || source[stop] == '+' || source[stop] == '*') ++stop;
            else {
                while (stop < frame.first && source[stop] >= '0' && source[stop] <= '9')
                    ++stop;
                if (stop < frame.first && (source[stop] == '.' || source[stop] == ')')) ++stop;
            }
            addMarker(marker, stop, kind);
        }
    }
    void complete(size_t first, size_t end)
    {
        for (auto& parent : spans) {
            parent.first = std::min(parent.first, first);
            parent.end = std::max(parent.end, end);
        }
        cursor = std::max(cursor, end);
    }
    void span()
    {
        auto frame = spans.back();
        spans.pop_back();
        auto first = frame.first, end = frame.end;
        auto kind = MarkdownKind::Strong;
        size_t delimiter = 0;
        switch (frame.type) {
            case MD_SPAN_STRONG: delimiter = 2; break;
            case MD_SPAN_EM:
                kind = MarkdownKind::Emphasis;
                delimiter = 1;
                break;
            case MD_SPAN_DEL:
                kind = MarkdownKind::Strike;
                delimiter = 2;
                break;
            case MD_SPAN_CODE:
                kind = MarkdownKind::Code;
                delimiter = 1;
                break;
            case MD_SPAN_A: kind = MarkdownKind::Link; break;
            case MD_SPAN_IMG: kind = MarkdownKind::Image; break;
            default: return;
        }
        if (kind == MarkdownKind::Link || kind == MarkdownKind::Image) {
            auto opener = kind == MarkdownKind::Image ? std::string_view("![") : std::string_view("[");
            auto open = source.find(opener, frame.hint);
            while (open != std::string_view::npos && open < first) {
                auto labelEnd = source.find(']', open + opener.size());
                if (labelEnd == std::string_view::npos || labelEnd + 1 >= source.size() || source[labelEnd + 1] != ':') break;
                open = source.find(opener, lineEnd(labelEnd));
            }
            if (open == std::string_view::npos || (first != std::string_view::npos && open > first)) {
                if (kind == MarkdownKind::Link && first != std::string_view::npos) {
                    if (first && source[first - 1] == '<' && end < source.size() && source[end] == '>') {
                        --first;
                        ++end;
                    }
                    add(first, end, kind);
                    if (source[first] == '<') {
                        addMarker(first, first + 1, kind);
                        addMarker(end - 1, end, kind);
                    }
                    complete(first, end);
                }
                return;
            }
            auto close = source.find(']', first == std::string_view::npos ? open + opener.size() : end);
            if (close == std::string_view::npos) return;
            end = close + 1;
            if (end < source.size() && source[end] == '(') {
                unsigned depth = 1;
                for (++end; end < source.size() && depth; ++end) {
                    if (source[end] == '\\' && end + 1 < source.size()) ++end;
                    else if (source[end] == '(') ++depth;
                    else if (source[end] == ')') --depth;
                }
            } else if (end < source.size() && source[end] == '[') {
                auto last = source.find(']', end + 1);
                if (last != std::string_view::npos) end = last + 1;
            }
            add(open, end, kind);
            addMarker(open, open + opener.size(), kind);
            addMarker(close, close + 1, kind);
            if (close + 1 < end && (source[close + 1] == '(' || source[close + 1] == '[')) {
                addMarker(close + 1, close + 2, kind);
                addMarker(end - 1, end, kind);
            }
            complete(open, end);
            return;
        }
        if (first == std::string_view::npos) return;
        auto contentStart = first, contentEnd = end;
        // MD4C excludes a single padding space on either side of nonblank code.
        if (kind == MarkdownKind::Code && first >= 2 && source.substr(first - 2, 2) == "` " && source.substr(end, 2) == " `") {
            --first;
            ++end;
        }
        if (kind == MarkdownKind::Code) {
            auto openEnd = first, closeStart = end;
            while (first && source[first - 1] == '`')
                --first;
            while (end < source.size() && end - closeStart < openEnd - first && source[end] == '`')
                ++end;
        } else if (first && end < source.size()) {
            auto marker = source[first - 1];
            if (kind == MarkdownKind::Strike && (first < 2 || source[first - 2] != '~')) delimiter = 1;
            if ((kind == MarkdownKind::Strike ? marker == '~' : marker == '*' || marker == '_') && first >= delimiter) {
                auto opening = source.substr(first - delimiter, delimiter);
                if (opening.find_first_not_of(marker) == source.npos && opening == source.substr(end, delimiter)) {
                    first -= delimiter;
                    end += delimiter;
                }
            }
        }
        auto width = contentStart - first;
        add(first, end, kind, 0, width == end - contentEnd && width <= UINT8_MAX ? uint8_t(width) : 0);
        addMarker(first, contentStart, kind);
        addMarker(contentEnd, end, kind);
        complete(first, end);
    }
};
}  // namespace

namespace
{
bool parse(Parser& parser)
{
    static_assert(sizeof(MD_SIZE) == sizeof(uint32_t));
    auto source = parser.source;
    if (source.size() > std::numeric_limits<MD_SIZE>::max()) return {};
    MD_PARSER callbacks{};
    callbacks.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEAUTOLINKS;
    callbacks.enter_block = [](MD_BLOCKTYPE type, void* detail, void* data) {
        auto& p = *static_cast<Parser*>(data);
        Frame frame{int(type)};
        frame.hint = p.cursor;
        if (type == MD_BLOCK_H) frame.level = static_cast<MD_BLOCK_H_DETAIL*>(detail)->level;
        if (type == MD_BLOCK_CODE) frame.fence = static_cast<MD_BLOCK_CODE_DETAIL*>(detail)->fence_char;
        if (type == MD_BLOCK_LI) {
            const auto& item = *static_cast<MD_BLOCK_LI_DETAIL*>(detail);
            frame.ordered = !p.blocks.empty() && p.blocks.back().type == MD_BLOCK_OL;
            frame.checked = item.is_task && (item.task_mark == 'x' || item.task_mark == 'X');
            if (item.is_task && item.task_mark_offset) p.addMarker(item.task_mark_offset - 1, item.task_mark_offset + 2, frame.ordered ? MarkdownKind::NumberedList : MarkdownKind::List);
        }
        p.blocks.push_back(frame);
        return 0;
    };
    callbacks.leave_block = [](MD_BLOCKTYPE, void*, void* data) {
        auto& p = *static_cast<Parser*>(data);
        p.block();
        if (p.split && p.blocks.size() == 1) p.flush(false);
        return 0;
    };
    callbacks.enter_span = [](MD_SPANTYPE type, void*, void* data) {
        auto& p = *static_cast<Parser*>(data);
        Frame frame{int(type)};
        frame.hint = p.cursor;
        p.spans.push_back(frame);
        return 0;
    };
    callbacks.leave_span = [](MD_SPANTYPE, void*, void* data) {
        static_cast<Parser*>(data)->span();
        return 0;
    };
    callbacks.text = [](MD_TEXTTYPE, const char* text, MD_SIZE size, void* data) {
        static_cast<Parser*>(data)->text(text, size);
        return 0;
    };
    return md_parse(source.data(), MD_SIZE(source.size()), &callbacks, &parser) == 0;
}
}  // namespace

std::vector<MarkdownSpan> parseMarkdown(std::string_view source)
{
    if (source.empty()) return {};
    Parser parser{};
    parser.source = source;
    if (!parse(parser)) return {};
    std::stable_sort(parser.result.begin(), parser.result.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return std::move(parser.result);
}

std::vector<MarkdownBlock> parseMarkdownBlocks(std::string_view source)
{
    if (source.empty()) return {};
    // Certify bounded rows before giving a giant paragraph to the inline parser.
    if (independentText(source)) {
        std::vector<MarkdownBlock> sections;
        bool complete = true;
        for (size_t first = 0; first < source.size();) {
            auto end = first;
            for (unsigned row = 0; row < 128 && end < source.size(); ++row) {
                auto newline = source.find('\n', end);
                end = newline == source.npos ? source.size() : newline + 1;
            }
            Parser part{};
            part.source = source.substr(first, end - first);
            part.split = true;
            if (!parse(part)) return {};
            part.flush(true);
            if (std::any_of(part.sections.begin(), part.sections.end(), [&](const auto& block) { return !independent(part.source.substr(block.start, block.end - block.start), block.spans); })) {
                complete = false;
                break;
            }
            for (auto& block : part.sections) {
                block.start += first;
                block.end += first;
                block.independent = true;
                sections.push_back(std::move(block));
            }
            first = end;
        }
        if (complete) return sections;
    }
    Parser parser{};
    parser.source = source;
    parser.split = true;
    if (!parse(parser)) return {};
    parser.flush(true);
    std::vector<MarkdownBlock> sections;
    for (auto& block : parser.sections) {
        auto text = source.substr(block.start, block.end - block.start);
        if (!independent(text, block.spans)) {
            sections.push_back(std::move(block));
            continue;
        }
        size_t first = 0, span = 0;
        while (first < text.size()) {
            auto end = first;
            for (unsigned row = 0; row < 128 && end < text.size(); ++row) {
                auto newline = text.find('\n', end);
                end = newline == text.npos ? text.size() : newline + 1;
            }
            MarkdownBlock part{block.start + first, block.start + end, {}, true};
            auto begin = span;
            while (span < block.spans.size() && block.spans[span].start < end)
                ++span;
            part.spans.reserve(span - begin);
            for (; begin < span; ++begin) {
                auto value = block.spans[begin];
                value.start -= first;
                value.end -= first;
                part.spans.push_back(value);
            }
            sections.push_back(std::move(part));
            first = end;
        }
    }
    return sections;
}
}  // namespace tn
