#include "texteditor.h"
#include <ui/core/text/internal.h>

namespace ui
{
namespace
{
constexpr float fontPointsPerLogicalPixel = 72.0f / 96.0f;
uint32_t codepoint(std::string_view text)
{
    auto first = static_cast<unsigned char>(text.front());
    if (first < 128) return first;
    uint32_t value = first & (first < 0xe0 ? 31 : first < 0xf0 ? 15
                                                               : 7);
    for (size_t i = 1; i < text.size(); ++i)
        value = (value << 6) | (static_cast<unsigned char>(text[i]) & 63);
    return value;
}

bool glyphMetrics(tvg::Text* measurer, std::string_view text, tvg::GlyphMetrics& metrics, size_t* consumed = nullptr)
{
    // Bound the NUL-terminated input so metrics never scans the rest of the document.
    char value[5]{};
    std::copy_n(text.data(), std::min(text.size(), sizeof(value) - 1), value);
    const char* next = nullptr;
    auto result = measurer->metrics(value, metrics, &next);
    if (consumed) *consumed = next ? size_t(next - value) : 0;
    return result == tvg::Result::Success;
}
}  // namespace

float TextEditor::Impl::measure(std::string_view text, FontId font) const
{
    auto* fonts = documentFont ? &detail::fontManager() : nullptr;
    if (!font) font = documentFont;
    bool alternate = fonts && font != documentFont;
    if (alternate) measurer->font(fonts->textFont(font));
    float width = 0;
    uint32_t previous = 0;
    for (size_t offset = 0; offset < text.size();) {
        tvg::GlyphMetrics metrics{};
        size_t consumed = 0;
        if (!glyphMetrics(measurer.get(), text.substr(offset), metrics, &consumed) || !consumed) break;
        if (fonts) {
            auto glyph = fonts->glyph(font, codepoint(text.substr(offset, consumed)));
            width += fonts->textKerning(font, previous, glyph) * configuration.fontSize / fonts->unitsPerEm(font);
            previous = glyph;
        }
        width += metrics.advance;
        offset += consumed;
    }
    if (alternate) measurer->font(fonts->textFont(documentFont));
    return width;
}

std::vector<TextLine> TextEditor::Impl::layoutRows(std::string_view text, size_t base, std::vector<std::string>& display, std::vector<std::vector<Run>>& styles)
{
    auto* fonts = documentFont ? &detail::fontManager() : nullptr;
    std::vector<TextLine> lines;
    constexpr float headingScale[] = {1, 2, 1.65f, 1.4f, 1.2f, 1.1f, 1.05f};
    for (size_t begin = 0;;) {
        auto stop = std::min(text.find('\n', begin), text.size());
        auto rowFormat = formatAt(base + begin);
        float sizeScale = headingScale[std::min(rowFormat.heading, 6u)];
        float size = configuration.fontSize * sizeScale;
        float rowHeight = lineHeight * sizeScale;
        TextLine line;
        std::string shown;
        std::vector<Run> runs;
        Format current;
        FontId font = documentFont;
        bool leading = true;
        uint32_t previous = 0;
        float x = 0;
        size_t column = 0;
        line.offsets.push_back(base + begin);
        line.x.push_back(0);
        measurer->font(fonts ? fonts->textFont(font) : themeTokens.font.c_str());
        measurer->size(size * fontPointsPerLogicalPixel);
        tvg::GlyphMetrics space{};
        glyphMetrics(measurer.get(), " ", space);
        line.breakWidth = space.advance;
        auto append = [&](std::string_view value, uint32_t cp, bool caret) {
            tvg::GlyphMetrics metrics{};
            glyphMetrics(measurer.get(), value, metrics);
            if (fonts) {
                auto glyph = fonts->glyph(font, cp);
                x += fonts->textKerning(font, previous, glyph) * size / fonts->unitsPerEm(font);
                previous = glyph;
            }
            if (caret) line.x.back() = x;
            if (!runs.empty() && leading && cp != ' ') {
                runs.back().leading = x - runs.back().left;
                leading = false;
            }
            x += metrics.advance;
            shown.append(value);
            if (!runs.empty()) {
                runs.back().end = shown.size();
                runs.back().width = x - runs.back().left;
            }
        };
        for (size_t offset = begin; offset < stop;) {
            auto next = offset + 1;
            while (next < stop && (static_cast<unsigned char>(text[next]) & 0xc0) == 0x80)
                ++next;
            auto value = text.substr(offset, next - offset);
            auto cp = codepoint(value);
            auto format = formatAt(base + offset);
            auto face = format.bold ? boldDocumentFont : documentFont;
            if (fonts && face != documentFont && !fonts->glyph(face, cp)) face = documentFont;
            if (configuration.markdown && (runs.empty() || format.start != current.start || format.end != current.end || face != font)) {
                current = format;
                font = face;
                measurer->font(fonts ? fonts->textFont(font) : themeTokens.font.c_str());
                measurer->size(size * fontPointsPerLogicalPixel);
                tvg::TextMetrics metrics{};
                measurer->metrics(metrics);
                rowHeight = std::max(rowHeight, metrics.advance);
                runs.push_back({.first = shown.size(), .left = x, .top = metrics.ascent - metrics.descent, .scale = sizeScale, .font = font, .format = format});
                leading = true;
                previous = 0;
            }
            if (cp == '\t') {
                auto spaces = 4 - column % 4;
                for (size_t i = 0; i < spaces; ++i)
                    append(" ", ' ', i == 0);
                column += spaces;
            } else {
                append(value, cp, true);
                ++column;
            }
            line.offsets.push_back(base + next);
            line.x.push_back(x);
            offset = next;
        }
        for (auto& run : runs)
            run.top = (rowHeight - run.top) / 2;
        line.bounds = {0, 0, x, rowHeight};
        lines.push_back(std::move(line));
        display.push_back(std::move(shown));
        styles.push_back(std::move(runs));
        if (stop == text.size()) break;
        begin = stop + 1;
    }
    measurer->font(fonts ? fonts->textFont(documentFont) : themeTokens.font.c_str());
    measurer->size(configuration.fontSize * fontPointsPerLogicalPixel);
    return lines;
}

size_t TextEditor::Impl::replaceRows(size_t first, size_t count, std::string_view text, size_t base)
{
    std::vector<std::string> display;
    std::vector<std::vector<Run>> runs;
    auto lines = layoutRows(text, base, display, runs);
    const auto& old = view.layout();
    float top = first < old.size() ? old[first].bounds.y : contentHeight();
    float bottom = first + count < old.size() ? old[first + count].bounds.y : contentHeight();
    float y = top;
    for (auto& line : lines) {
        line.bounds.y = y;
        y += line.bounds.h;
    }
    auto shift = first + count < old.size() ? ptrdiff_t(base + text.size() + 1) - ptrdiff_t(old[first + count].offsets.front()) : 0;
    auto rows = lines.size();
    view.layout(first, count, std::move(lines), shift, y - bottom);
    displayLines.erase(displayLines.begin() + first, displayLines.begin() + first + count);
    displayLines.insert(displayLines.begin() + first, std::make_move_iterator(display.begin()), std::make_move_iterator(display.end()));
    rowRuns.erase(rowRuns.begin() + first, rowRuns.begin() + first + count);
    rowRuns.insert(rowRuns.begin() + first, std::make_move_iterator(runs.begin()), std::make_move_iterator(runs.end()));
    return rows;
}

void TextEditor::Impl::relayout()
{
    auto* fonts = &detail::fontManager();
    const auto& document = editing.document();
    std::string_view source = document.text();
    auto selection = editing.selection();
    auto generation = fonts ? fonts->generation() : 0;
    bool recompose = preedit != measuredPreedit || (!preedit.empty() && (preeditCursor != measuredCursor || selection.anchor != measuredSelection.anchor || selection.caret != measuredSelection.caret));
    if (!layoutDirty && !recompose && measuredRevision == document.revision() && measuredFontGeneration == generation) return;
    size_t start = 0, end = 0;
    auto change = document.changedSince(measuredRevision, start, end);
    auto font = preferredFont ? preferredFont : detail::fontManager().find(themeTokens.font);
    if (!font) fonts = nullptr;
    bool markdownChanged = configuration.markdown && markdownRevision != document.revision();
    if (markdownChanged) parseMarkdown();
    bool full = markdownChanged || layoutDirty || change == tn::ChangeStatus::Untracked || font != documentFont || generation != measuredFontGeneration || (composedRows && measuredRevision != document.revision());
    if (full) {
        if (font != documentFont || generation != measuredFontGeneration) ++textStyleRevision;
        documentFont = font;
        boldDocumentFont = documentFont;
        if (fonts) {
            for (const auto& info : fonts->fonts()) {
                if (info.id != documentFont) continue;
                auto bold = fonts->find(info.family, 700);
                if (bold) boldDocumentFont = bold;
                break;
            }
        }
        measurer->font(fonts ? fonts->textFont(documentFont) : themeTokens.font.c_str());
        measurer->size(configuration.fontSize * fontPointsPerLogicalPixel);
        tvg::TextMetrics metrics{};
        measurer->metrics(metrics);
        lineHeight = std::max(configuration.fontSize * 1.3f, metrics.advance);
        documentTop = (lineHeight - (metrics.ascent - metrics.descent)) / 2;
        digitWidth = 0;
        if (configuration.lineNumbers) {
            lineNumberFont = fonts ? preferredNumberFont ? preferredNumberFont : documentFont : 0;
            for (char ch = '0'; ch <= '9'; ++ch)
                digitWidth = std::max(digitWidth, measure(std::string_view(&ch, 1), lineNumberFont));
        }
        replaceRows(0, view.layout().size(), source, 0);
        composedRows = 0;
    } else {
        if (composedRows && recompose) {
            auto last = composedFirst + composedSpan - 1;
            replaceRows(composedFirst, composedRows, source.substr(document.lineStart(composedFirst), document.lineEnd(last) - document.lineStart(composedFirst)), document.lineStart(composedFirst));
            composedRows = 0;
        }
        if (change == tn::ChangeStatus::Changed) {
            auto first = document.lineAt(start), last = document.lineAt(end);
            auto count = last + 1 - first + view.layout().size() - document.lineCount();
            replaceRows(first, count, source.substr(document.lineStart(first), document.lineEnd(last) - document.lineStart(first)), document.lineStart(first));
        }
    }
    if (!preedit.empty() && (full || recompose)) {
        // Preedit replaces the selection in a window of rows; the document is unchanged.
        auto begin = selection.start(), finish = begin + preedit.size(), cursor = begin + preeditCursor;
        auto first = document.lineAt(begin), last = document.lineAt(selection.end());
        auto lineStart = document.lineStart(first);
        auto composed = std::string(source.substr(lineStart, begin - lineStart)) + preedit + std::string(source.substr(selection.end(), document.lineEnd(last) - selection.end()));
        composedFirst = first;
        composedSpan = last + 1 - first;
        composedRows = replaceRows(first, composedSpan, composed, lineStart);
        preeditRects.clear();
        for (size_t row = first; row < first + composedRows; ++row) {
            const auto& line = view.layout()[row];
            if (cursor >= line.offsets.front() && cursor <= line.offsets.back()) preeditCaret = {line.position(cursor), line.bounds.y, 1.5f, line.bounds.h};
            if (finish > line.offsets.front() && begin <= line.offsets.back()) {
                float left = line.position(std::max(begin, line.offsets.front()));
                float right = line.position(std::min(finish, line.offsets.back()));
                if (finish > line.offsets.back()) right += line.breakWidth;
                preeditRects.push_back({left, line.bounds.y, right - left, line.bounds.h});
            }
        }
    }
    if (preedit.empty()) preeditRects.clear();
    gutter = configuration.lineNumbers ? digitWidth * std::max(size_t(4), std::to_string(document.lineCount()).size()) + configuration.padding * 2 + 10 : 0;
    widest = 0;
    for (const auto& line : view.layout())
        widest = std::max(widest, line.bounds.w);
    measuredRevision = document.revision();
    measuredSelection = selection;
    measuredPreedit = preedit;
    measuredCursor = preeditCursor;
    measuredFontGeneration = generation;
    layoutDirty = false;
    dirty = true;
    clampScroll();
}

TextEditor::Impl::RenderLine& TextEditor::Impl::prepareLine(size_t row)
{
    auto* fonts = documentFont ? &detail::fontManager() : nullptr;
    auto& cached = linePaints[row];
    if (!cached) {
        cached = std::make_unique<RenderLine>();
        cached->number->text(std::to_string(row + 1).c_str());
    }
    bool restyled = cached->style != textStyleRevision;
    if (restyled) {
        auto style = [&](tvg::Text* text, FontId font, Color color) {
            text->font(fonts ? fonts->textFont(font) : themeTokens.font.c_str());
            text->size(configuration.fontSize * fontPointsPerLogicalPixel);
            text->layout(0, 0);
            text->align(0, 0);
            text->wrap(tvg::TextWrap::None);
            text->fill(color.r, color.g, color.b);
            text->opacity(color.a);
        };
        style(cached->body, documentFont, themeTokens.text);
        style(cached->number, lineNumberFont, themeTokens.muted);
        cached->numberWidth = measure(std::to_string(row + 1), lineNumberFont);
        cached->style = textStyleRevision;
    }
    if (configuration.markdown) {
        if (restyled || !cached->assigned || cached->value != displayLines[row]) prepareRuns(*cached, row);
        cached->value = displayLines[row];
        cached->assigned = true;
        return *cached;
    }
    cached->runs.clear();
    if (restyled || !cached->assigned || cached->value != displayLines[row]) {
        cached->value = displayLines[row];
        auto first = cached->value.find_first_not_of(' ');
        if (first == std::string::npos) first = cached->value.size();
        cached->bodyLeft = 0;
        if (first) {
            // Empty leading glyph outlines can give ThorVG an invalid anchor.
            // Keep their advances, including kerning with the first visible glyph.
            cached->bodyLeft = measure(std::string_view(cached->value).substr(0, first));
            tvg::GlyphMetrics metrics{};
            size_t consumed = 0;
            if (first < cached->value.size() && glyphMetrics(measurer.get(), std::string_view(cached->value).substr(first), metrics, &consumed) && consumed)
                cached->bodyLeft = measure(std::string_view(cached->value).substr(0, first + consumed)) - metrics.advance;
        }
        cached->body->text(cached->value.c_str() + first);
        cached->assigned = true;
    }
    return *cached;
}
}  // namespace ui
