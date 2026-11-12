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
struct Frame
{
    int type;
    unsigned level = 0;
    char fence = 0;
    bool ordered = false;
    size_t first = std::string_view::npos, end = 0, hint = 0;
};

struct Parser
{
    std::string_view source;
    std::vector<MarkdownSpan> result;
    std::vector<Frame> blocks, spans;
    size_t cursor = 0;

    size_t lineStart(size_t offset) const
    {
        auto previous = offset ? source.rfind('\n', offset - 1) : std::string_view::npos;
        return previous == std::string_view::npos ? 0 : previous + 1;
    }
    size_t lineEnd(size_t offset) const { return std::min(source.find('\n', offset), source.size()); }
    void add(size_t first, size_t end, MarkdownKind kind, unsigned level = 0)
    {
        if (first < end && end <= source.size()) result.push_back({first, end, kind, level});
    }
    void addMarker(size_t first, size_t end, MarkdownKind element)
    {
        if (first < end && end <= source.size()) result.push_back({first, end, MarkdownKind::Marker, 0, element});
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
        if (delimiter && first >= delimiter) {
            auto marker = source[first - 1];
            if (marker == '*' || marker == '_' || marker == '`' || marker == '~') {
                if (kind == MarkdownKind::Code) {
                    while (first && source[first - 1] == '`')
                        --first;
                    auto count = contentStart - first;
                    while (end < source.size() && end - contentEnd < count && source[end] == '`')
                        ++end;
                } else {
                    first -= delimiter;
                    if (source.size() - end >= delimiter) end += delimiter;
                }
            }
        }
        add(first, end, kind);
        addMarker(first, contentStart, kind);
        addMarker(contentEnd, end, kind);
        complete(first, end);
    }
};
}  // namespace

std::vector<MarkdownSpan> parseMarkdown(std::string_view source)
{
    if (source.empty()) return {};
    Parser parser{};
    parser.source = source;
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
            if (item.is_task && item.task_mark_offset) p.addMarker(item.task_mark_offset - 1, item.task_mark_offset + 2, frame.ordered ? MarkdownKind::NumberedList : MarkdownKind::List);
        }
        p.blocks.push_back(frame);
        return 0;
    };
    callbacks.leave_block = [](MD_BLOCKTYPE, void*, void* data) {
        static_cast<Parser*>(data)->block();
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
    if (md_parse(source.data(), MD_SIZE(source.size()), &callbacks, &parser) != 0) return {};
    std::stable_sort(parser.result.begin(), parser.result.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return std::move(parser.result);
}
}  // namespace tn
