#include "texteditor.h"
#include <algorithm>

namespace ui
{
namespace
{
struct LineMarker
{
    size_t start, end;
    bool matches;
};

LineMarker lineMarker(std::string_view line, MarkdownFormat format, unsigned level)
{
    size_t at = 0;
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
        ++at;
    auto spaceAfter = [&](size_t end) {
        return end == line.size() || line[end] == ' ' || line[end] == '\t';
    };
    auto withSpace = [&](size_t end) {
        return end < line.size() && spaceAfter(end) ? end + 1 : end;
    };
    while (at < line.size() && line[at] == '>') {
        auto end = withSpace(at + 1);
        if (format == MarkdownFormat::Quote) return {at, end, true};
        at = line.find_first_not_of(" \t", end);
        if (at == line.npos) at = line.size();
    }
    auto end = at;
    if (format == MarkdownFormat::Heading) {
        while (end < line.size() && line[end] == '#')
            ++end;
        if (end > at && end - at <= 6 && spaceAfter(end)) return {at, withSpace(end), end - at == level};
    } else if (format != MarkdownFormat::Quote && at < line.size()) {
        MarkdownFormat found = MarkdownFormat::BulletList;
        if (line[at] == '-' || line[at] == '+' || line[at] == '*') ++end;
        else {
            while (end < line.size() && line[end] >= '0' && line[end] <= '9')
                ++end;
            if (end == at || end - at > 9 || end == line.size() || (line[end] != '.' && line[end] != ')')) return {at, at, false};
            ++end;
            found = MarkdownFormat::OrderedList;
        }
        if (spaceAfter(end)) {
            end = withSpace(end);
            if (end + 3 <= line.size() && line[end] == '[' && line[end + 2] == ']' && (line[end + 1] == ' ' || line[end + 1] == 'x' || line[end + 1] == 'X') && spaceAfter(end + 3)) {
                found = MarkdownFormat::TaskList;
                end = withSpace(end + 3);
            }
            return {at, end, found == format};
        }
    }
    return {at, at, false};
}
}  // namespace

bool TextEditor::Impl::formatLines(MarkdownFormat format, unsigned level)
{
    std::string prefix;
    switch (format) {
        case MarkdownFormat::Heading:
            if (level < 1 || level > 6) return false;
            prefix.assign(level, '#');
            prefix += ' ';
            break;
        case MarkdownFormat::Quote: prefix = "> "; break;
        case MarkdownFormat::BulletList: prefix = "- "; break;
        case MarkdownFormat::TaskList: prefix = "- [ ] "; break;
        case MarkdownFormat::OrderedList: break;
        default: return false;
    }
    const auto& document = editing.document();
    const std::string_view source = document.text();
    auto selection = editing.selection();
    auto first = document.lineAt(selection.start());
    auto last = document.lineAt(selection.empty() ? selection.end() : selection.end() - 1);
    relayout();
    auto begin = document.lineStart(first), end = document.lineEnd(last);
    for (auto i = markdownBlock(begin); i < markdownBlocks.size() && markdownBlocks[i].start <= end; ++i) {
        const auto& block = markdownBlocks[i];
        for (const auto& span : block.spans) {
            if (span.kind == tn::MarkdownKind::Code && !span.markerWidth && block.start + span.end > begin && block.start + span.start <= end) return false;
        }
    }
    tn::EditBatch batch;
    batch.revision = document.revision();
    bool remove = true;
    for (auto row = first; row <= last; ++row) {
        auto base = document.lineStart(row);
        auto line = source.substr(base, document.lineEnd(row) - base);
        if (first != last && line.find_first_not_of(" \t") == line.npos) continue;
        auto marker = lineMarker(line, format, level);
        remove &= marker.matches;
        if (format == MarkdownFormat::OrderedList) prefix = std::to_string(batch.edits.size() + 1) + ". ";
        auto replacement = marker.matches && format != MarkdownFormat::OrderedList ? std::string(line.substr(marker.start, marker.end - marker.start)) : prefix;
        batch.edits.push_back({base + marker.start, base + marker.end, std::move(replacement)});
    }
    if (batch.edits.empty()) return false;
    if (remove)
        for (auto& edit : batch.edits)
            edit.text.clear();
    owner.endUndoGroup();
    std::string error;
    return owner.applyEdits(batch, error);
}

bool TextEditor::formatMarkdown(MarkdownFormat format, unsigned level)
{
    using Kind = tn::MarkdownKind;
    if (!options().markdown || options().readOnly || composing()) return false;
    const auto& document = session().document();
    const std::string_view source = document.text();
    auto selection = session().selection();
    auto start = selection.start(), end = selection.end();
    auto kind = Kind::Strong;
    std::string opener;
    switch (format) {
        case MarkdownFormat::Bold: opener = "**"; break;
        case MarkdownFormat::Italic:
            kind = Kind::Emphasis;
            opener = "*";
            break;
        case MarkdownFormat::Code:
            kind = Kind::Code;
            opener = "`";
            break;
        case MarkdownFormat::Strike:
            kind = Kind::Strike;
            opener = "~~";
            break;
        case MarkdownFormat::Link:
            kind = Kind::Link;
            opener = "[";
            break;
        default: return impl->formatLines(format, level);
    }
    tn::EditBatch batch;
    batch.revision = document.revision();
    auto& empty = impl->emptyMarkup;
    bool insertPair = false;
    if (selection.empty() && empty && empty->revision == batch.revision && empty->format == format && empty->caret == start) {
        batch.edits.push_back({start - opener.size(), end + opener.size(), ""});
        start = end = start - opener.size();
    } else {
        impl->relayout();
        if (!selection.empty() && kind != Kind::Code && kind != Kind::Link) {
            auto text = source.substr(start, end - start);
            auto first = text.find_first_not_of(" \t\n");
            if (first == text.npos) return false;
            end = start + text.find_last_not_of(" \t\n") + 1;
            start += first;
        }
        // An inline command must stay within one Markdown block.
        auto block = impl->markdownBlock(start);
        if (start != end && (impl->markdownBlock(end - 1) != block || source.substr(start, end - start).find("\n\n") != source.npos)) return false;
        const tn::MarkdownSpan* match = nullptr;
        size_t base = 0;
        if (!impl->markdownBlocks.empty()) {
            const auto& parsed = impl->markdownBlocks[block];
            base = parsed.start;
            for (const auto& span : parsed.spans) {
                size_t first = base + span.start, last = base + span.end;
                if (first > start) break;
                if (last < end) continue;
                bool inside = start >= first + span.markerWidth && end <= last - span.markerWidth;
                bool entire = start == first && end == last;
                if (kind == Kind::Link && span.kind == Kind::Link && (inside || entire)) {
                    for (const auto& marker : parsed.spans) {
                        if (marker.start <= span.start || marker.end >= span.end || marker.kind != Kind::Marker || marker.element != Kind::Link) continue;
                        auto open = base + marker.start;
                        if (source[open] == '(' || source[open] == '[') {
                            select(open + 1, last - 1);
                            return true;
                        }
                    }
                    return false;
                }
                if (span.kind == Kind::Code && (inside || entire) && (!span.markerWidth || kind != Kind::Code)) return false;
                if (span.kind == kind && span.markerWidth && (inside || entire) && (!match || span.end - span.start < match->end - match->start)) match = &span;
            }
        }
        if (match) {
            size_t first = base + match->start, last = base + match->end, width = match->markerWidth;
            if (selection.empty() && end == last - width) {
                select(last, last);
                return true;
            }
            start = std::max(start, first + width);
            end = std::min(end, last - width);
            auto left = source.substr(first + width, start - (first + width));
            auto right = source.substr(end, last - width - end);
            // Wrapping another inline style can leave just its markers on either side.
            bool part = !selection.empty() && kind != Kind::Code && (left.find_first_not_of("*_~`") != left.npos || right.find_first_not_of("*_~`") != right.npos);
            auto leftEnd = left.find_last_not_of(" \t\n"), rightStart = right.find_first_not_of(" \t\n");
            auto marker = std::string(source.substr(first, width));
            if (part && rightStart != right.npos) batch.edits.push_back({end + rightStart, end + rightStart, marker});
            else batch.edits.push_back({last - width, last, ""});
            if (part && leftEnd != left.npos) {
                auto close = first + width + leftEnd + 1;
                batch.edits.push_back({close, close, marker});
                start += width;
                end += width;
            } else {
                batch.edits.push_back({first, first + width, ""});
                start -= width;
                end -= width;
            }
        } else if (kind == Kind::Link) {
            if (start == end) {
                batch.edits.push_back({start, start, "[]()"});
                start = end = start + 1;
            } else {
                batch.edits = {{start, start, "["}, {end, end, "]()"}};
                start = end = end + 3;
            }
        } else {
            std::string closer = opener;
            if (kind == Kind::Code && start != end) {
                auto text = source.substr(start, end - start);
                size_t longest = 0, run = 0;
                for (char ch : text) {
                    run = ch == '`' ? run + 1 : 0;
                    longest = std::max(longest, run);
                }
                if (longest >= 32) return false;  // MD4C's maximum code span delimiter is 32.
                opener.assign(longest + 1, '`');
                closer = opener;
                if (text.front() == '`' || text.back() == '`' || (text.front() == ' ' && text.back() == ' ' && text.find_first_not_of(' ') != text.npos)) {
                    opener += ' ';
                    closer.insert(0, 1, ' ');
                }
            }
            insertPair = start == end;
            if (insertPair) batch.edits.push_back({start, start, opener + closer});
            else batch.edits = {{start, start, opener}, {end, end, closer}};
            start += opener.size();
            end += opener.size();
        }
    }
    batch.selection = selection.anchor <= selection.caret ? tn::Selection{start, end} : tn::Selection{end, start};
    endUndoGroup();
    std::string error;
    if (!applyEdits(batch, error)) return false;
    if (insertPair) empty = Impl::EmptyMarkup{document.revision(), start, format};
    else empty.reset();
    return true;
}

}  // namespace ui
