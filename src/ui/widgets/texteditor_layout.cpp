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
void clearScene(tvg::Scene* scene)
{
    while (!scene->paints().empty()) {
        auto paint = scene->paints().front();
        // Keep removed shapes alive while ThorVG records their damaged regions.
        paint->ref();
        scene->remove(paint);
        paint->unref();
    }
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
        auto block = markdownBlock(base + begin);
        const auto& formats = blockFormats(block);
        auto blockBase = markdownBlocks.empty() ? 0 : markdownBlocks[block].start;
        auto cursor = std::upper_bound(formats.begin(), formats.end(), base + begin - blockBase, [](size_t offset, const auto& format) { return offset < format.start; });
        if (cursor != formats.begin()) --cursor;
        auto formatAt = [&](size_t offset) {
            if (!configuration.markdown) return Format{};
            auto relative = offset - blockBase;
            while (cursor != formats.end() && cursor->end <= relative)
                ++cursor;
            if (cursor == formats.end() || cursor->start > relative) return Format{};
            auto value = *cursor;
            value.start += blockBase;
            value.end += blockBase;
            return value;
        };
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
        line.base = base + begin;
        line.length = stop - begin;
        line.offsets.reserve(stop - begin + 1);
        line.x.reserve(stop - begin + 1);
        shown.reserve(stop - begin);
        line.offsets.push_back(0);
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
            line.offsets.push_back(next - begin);
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

std::vector<TextLine> TextEditor::Impl::rowMetadata(std::string_view text, size_t base)
{
    std::vector<TextLine> result;
    const auto& document = editing.document();
    result.reserve(document.lineAt(base + text.size()) - document.lineAt(base) + 1);
    auto block = markdownBlock(base);
    size_t span = 0, headingEnd = 0, boldEnd = 0;
    unsigned heading = 0;
    constexpr float scale[] = {1, 2, 1.65f, 1.4f, 1.2f, 1.1f, 1.05f};
    for (size_t begin = 0;;) {
        auto end = std::min(text.find('\n', begin), text.size());
        auto offset = base + begin;
        while (block + 1 < markdownBlocks.size() && offset >= markdownBlocks[block].end) {
            ++block;
            span = 0;
        }
        if (!markdownBlocks.empty() && configuration.markdown) {
            const auto& current = markdownBlocks[block];
            auto cached = markdownFormats.find(block);
            if (cached != markdownFormats.end()) {
                headingEnd = boldEnd = 0;
                const auto& formats = cached->second;
                auto at = std::upper_bound(formats.begin(), formats.end(), offset - current.start, [](size_t value, const auto& format) { return value < format.end; });
                for (; at != formats.end() && current.start + at->start < base + end; ++at) {
                    if (at->heading) {
                        heading = at->heading;
                        headingEnd = current.start + at->end;
                    }
                    if (at->bold) boldEnd = current.start + at->end;
                }
            } else {
                while (span < current.spans.size() && current.start + current.spans[span].start < base + end) {
                    const auto& value = current.spans[span++];
                    if (value.kind == tn::MarkdownKind::Heading) {
                        heading = value.level;
                        headingEnd = current.start + value.end;
                        boldEnd = std::max(boldEnd, headingEnd);
                    } else if (value.kind == tn::MarkdownKind::Strong) boldEnd = std::max(boldEnd, current.start + value.end);
                }
            }
        }
        auto multiplier = scale[std::min(headingEnd > offset ? heading : 0u, 6u)];
        TextLine line;
        line.base = offset;
        line.length = end - begin;
        line.bounds.h = std::max(lineHeight, boldEnd > offset ? boldAdvance : 0.0f) * multiplier;
        result.push_back(std::move(line));
        if (end == text.size()) break;
        begin = end + 1;
    }
    return result;
}

void TextEditor::Impl::updateWidth(float before, float after)
{
    if (before > 0) {
        auto found = lineWidths.find(before);
        if (found != lineWidths.end() && --found->second == 0) lineWidths.erase(found);
    }
    if (after > 0) ++lineWidths[after];
    widest = lineWidths.empty() ? 0 : lineWidths.rbegin()->first;
}

void TextEditor::Impl::measureLine(size_t row)
{
    const auto& old = view.layout()[row];
    auto& cached = linePaints[row];
    if (!cached) cached = std::make_unique<RenderLine>();
    if (!old.offsets.empty() && cached->measured) return;
    auto sourceBase = old.start();
    if (composedRows && row >= composedFirst + composedRows) {
        auto selection = editing.selection();
        sourceBase = size_t(ptrdiff_t(sourceBase) - ptrdiff_t(preedit.size()) + ptrdiff_t(selection.end() - selection.start()));
    }
    auto source = std::string_view(editing.document().text()).substr(sourceBase, old.length);
    std::vector<std::string> display;
    std::vector<std::vector<Run>> styles;
    auto lines = layoutRows(source, sourceBase, display, styles);
    lines[0].base = old.base;
    lines[0].top = old.y();
    lines[0].bounds.y = old.bounds.y;
    lines[0].bounds.h = old.bounds.h;
    updateWidth(old.bounds.w, lines[0].bounds.w);
    cached->display = std::move(display[0]);
    cached->styles = std::move(styles[0]);
    cached->measured = true;
    view.layout(row, 1, std::move(lines), 0, 0);
}

size_t TextEditor::Impl::replaceRows(size_t first, size_t count, std::string_view text, size_t base, bool composed)
{
    std::vector<std::string> display;
    std::vector<std::vector<Run>> runs;
    auto lines = composed ? layoutRows(text, base, display, runs) : rowMetadata(text, base);
    const auto& old = view.layout();
    if (!lineWidths.empty()) {
        for (size_t row = first; row < first + count; ++row)
            updateWidth(old[row].bounds.w, 0);
    }
    double top = first < old.size() ? old[first].y() : contentHeight();
    double bottom = first + count < old.size() ? old[first + count].y() : contentHeight();
    double y = top;
    for (auto& line : lines) {
        line.top = y;
        line.bounds.y = float(y);
        y += line.bounds.h;
        if (line.bounds.w > 0) updateWidth(0, line.bounds.w);
    }
    auto shift = first + count < old.size() ? ptrdiff_t(base + text.size() + 1) - ptrdiff_t(old[first + count].start()) : 0;
    auto rows = lines.size();
    auto eraseFirst = linePaints.lower_bound(first);
    auto eraseEnd = rows == count ? linePaints.lower_bound(first + count) : linePaints.end();
    for (auto it = eraseFirst; it != eraseEnd; ++it)
        view.discard(it->first);
    linePaints.erase(eraseFirst, eraseEnd);
    view.layout(first, count, std::move(lines), shift, y - bottom);
    if (composed) {
        for (size_t i = 0; i < rows; ++i) {
            auto& cached = linePaints[first + i];
            cached = std::make_unique<RenderLine>();
            cached->display = std::move(display[i]);
            cached->styles = std::move(runs[i]);
            cached->measured = true;
        }
    }
    return rows;
}

void TextEditor::Impl::relayout()
{
    auto* fonts = &detail::fontManager();
    const auto& document = editing.document();
    std::string_view source = document.text();
    auto selection = editing.selection();
    auto generation = fonts ? fonts->generation() : 0;
    bool compositionChanged = preedit != measuredPreedit || (!preedit.empty() && (selection.anchor != measuredSelection.anchor || selection.caret != measuredSelection.caret));
    bool recompose = compositionChanged || (!preedit.empty() && preeditCursor != measuredCursor);
    if (!layoutDirty && !recompose && measuredRevision == document.revision() && measuredFontGeneration == generation) return;
    size_t start = 0, end = 0;
    auto change = document.changedSince(measuredRevision, start, end);
    auto font = preferredFont ? preferredFont : detail::fontManager().find(themeTokens.font);
    if (!font) fonts = nullptr;
    if (!configuration.markdown) {
        markdownBlocks.clear();
        markdownFormats.clear();
        markdownRevision = uint64_t(-1);
    }
    bool markdownChanged = configuration.markdown && markdownRevision != document.revision();
    bool full = layoutDirty || change == tn::ChangeStatus::Untracked || font != documentFont || generation != measuredFontGeneration || (composedRows && measuredRevision != document.revision());
    bool replaceComposition = composedRows && !full && change == tn::ChangeStatus::Unchanged && !preedit.empty() && selection.anchor == measuredSelection.anchor && selection.caret == measuredSelection.caret;
    if (markdownChanged) parseMarkdown(start, end, !full && markdownRevision == measuredRevision);
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
        boldAdvance = metrics.advance;
        if (fonts && boldDocumentFont != documentFont) {
            measurer->font(fonts->textFont(boldDocumentFont));
            measurer->metrics(metrics);
            boldAdvance = metrics.advance;
            measurer->font(fonts->textFont(documentFont));
        }
        digitWidth = 0;
        if (configuration.lineNumbers) {
            lineNumberFont = fonts ? preferredNumberFont ? preferredNumberFont : documentFont : 0;
            for (char ch = '0'; ch <= '9'; ++ch)
                digitWidth = std::max(digitWidth, measure(std::string_view(&ch, 1), lineNumberFont));
        }
        linePaints.clear();
        lineWidths.clear();
        widest = 0;
        replaceRows(0, view.layout().size(), source, 0);
        composedRows = 0;
    } else {
        if (composedRows && recompose && !replaceComposition) {
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
        composedFirst = first;
        composedSpan = last + 1 - first;
        if (full || compositionChanged) {
            auto composed = std::string(source.substr(lineStart, begin - lineStart)) + preedit + std::string(source.substr(selection.end(), document.lineEnd(last) - selection.end()));
            composedRows = replaceRows(first, replaceComposition ? composedRows : composedSpan, composed, lineStart, true);
        }
        preeditRects.clear();
        for (size_t row = first; row < first + composedRows; ++row) {
            const auto& line = view.layout()[row];
            if (cursor >= line.start() && cursor <= line.end()) preeditCaret = {line.position(cursor), float(line.y() - vertical), 1.5f, line.bounds.h};
            if (finish > line.start() && begin <= line.end()) {
                float left = line.position(std::max(begin, line.start()));
                float right = line.position(std::min(finish, line.end()));
                if (finish > line.end()) right += line.breakWidth;
                preeditRects.push_back({left, float(line.y() - vertical), right - left, line.bounds.h});
            }
        }
    }
    if (preedit.empty()) preeditRects.clear();
    preeditOrigin = vertical;
    gutter = configuration.lineNumbers ? digitWidth * std::max(size_t(4), std::to_string(document.lineCount()).size()) + configuration.padding * 2 + 10 : 0;
    measuredRevision = document.revision();
    measuredSelection = selection;
    measuredPreedit = preedit;
    measuredCursor = preeditCursor;
    measuredFontGeneration = generation;
    layoutDirty = false;
    textContentDirty = true;
    dirty = true;
    clampScroll();
}

TextEditor::Impl::RenderLine& TextEditor::Impl::prepareLine(size_t row)
{
    auto* fonts = documentFont ? &detail::fontManager() : nullptr;
    measureLine(row);
    auto& cached = linePaints[row];
    if (!cached->assigned) cached->number->text(std::to_string(row + 1).c_str());
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
        if (restyled || !cached->assigned || cached->value != cached->display) {
            prepareRuns(*cached);
            if (!cached->group) {
                cached->group = tvg::Scene::gen();
                cached->group->ref();
            }
            clearScene(cached->group);
            if (cached->background) clearScene(cached->background);
            auto height = view.layout()[row].bounds.h;
            for (size_t i = 0; i < cached->styles.size(); ++i) {
                const auto& run = cached->styles[i];
                auto text = i ? cached->runs[i - 1]->text : cached->body;
                transform(text, {1, 0, run.left + run.leading, 0, 1, run.top, 0, 0, 1});
                cached->group->add(text);
                auto color = markdownColor(run.format.color);
                auto stroke = [&](float position) {
                    auto path = tvg::Shape::gen();
                    auto top = height * position;
                    path->moveTo(run.left, top);
                    path->lineTo(run.left + run.width, top);
                    path->strokeWidth(1);
                    path->strokeCap(tvg::StrokeCap::Round);
                    path->strokeFill(color.r, color.g, color.b, color.a);
                    cached->group->add(path);
                };
                if (run.format.strike) stroke(0.55f);
                if (run.format.link) stroke(0.85f);
                if (run.format.code) {
                    if (!cached->background) {
                        cached->background = tvg::Scene::gen();
                        cached->background->ref();
                    }
                    auto color = markdownColor(&Theme::markdownCodeBackground);
                    auto shape = tvg::Shape::gen();
                    shape->appendRect(run.left, 0, run.width, height, 2, 2);
                    shape->fill(color.r, color.g, color.b, color.a);
                    cached->background->add(shape);
                }
            }
        }
        cached->value = cached->display;
        cached->assigned = true;
        return *cached;
    }
    if (cached->group) clearScene(cached->group);
    if (cached->background) clearScene(cached->background);
    cached->runs.clear();
    if (restyled || !cached->assigned || cached->value != cached->display) {
        cached->value = cached->display;
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

void TextEditor::Impl::prepareViewport(size_t first, size_t end)
{
    bool rebuild = textContentDirty || first < cachedFirst || end > cachedEnd;
    if (!rebuild && first == drawnFirst && end == drawnEnd) return;
    const auto& geometry = view.layout();
    if (rebuild) {
        auto area = textArea();
        auto begin = std::lower_bound(geometry.begin(), geometry.end(), vertical - area.h, [](const auto& row, double y) { return row.y() + row.bounds.h <= y; });
        auto finish = std::lower_bound(begin, geometry.end(), vertical + area.h * 2, [](const auto& row, double y) { return row.y() < y; });
        cachedFirst = size_t(begin - geometry.begin());
        cachedEnd = size_t(finish - geometry.begin());
        auto caretRow = editing.document().lineAt(editing.selection().caret);
        for (auto it = linePaints.begin(); it != linePaints.end();) {
            bool composed = it->first >= composedFirst && it->first < composedFirst + composedRows;
            if ((it->first < cachedFirst || it->first >= cachedEnd) && it->first != caretRow && !composed) {
                view.discard(it->first);
                it = linePaints.erase(it);
            } else ++it;
        }
        renderOrigin = cachedFirst < geometry.size() ? geometry[cachedFirst].y() : 0;
        for (size_t row = cachedFirst; row < cachedEnd; ++row) {
            auto& line = prepareLine(row);
            auto y = float(geometry[row].y() - renderOrigin);
            if (configuration.lineNumbers) transform(line.number, {1, 0, gutter - configuration.padding - line.numberWidth, 0, 1, y, 0, 0, 1});
            if (configuration.markdown) {
                transform(line.group, {1, 0, 0, 0, 1, y, 0, 0, 1});
                if (line.background) transform(line.background, {1, 0, 0, 0, 1, y, 0, 0, 1});
            } else transform(line.body, {1, 0, line.bodyLeft, 0, 1, y + documentTop, 0, 0, 1});
        }
    }
    textContent.begin(themeTokens, {0, 0, widest, float(height * 3)}, seconds);
    codeContent.begin(themeTokens, {0, 0, widest, float(height * 3)}, seconds);
    numberContent.begin(themeTokens, {0, 0, gutter, float(height * 3)}, seconds);
    for (size_t row = first; row < end; ++row) {
        auto& line = *linePaints.at(row);
        if (configuration.lineNumbers) numberContent.add(line.number);
        if (configuration.markdown && line.background && !line.background->paints().empty()) codeContent.add(line.background);
        textContent.add(configuration.markdown ? static_cast<tvg::Paint*>(line.group) : line.body);
    }
    textContent.end();
    codeContent.end();
    numberContent.end();
    drawnFirst = first;
    drawnEnd = end;
    textContentDirty = false;
}

}  // namespace ui
