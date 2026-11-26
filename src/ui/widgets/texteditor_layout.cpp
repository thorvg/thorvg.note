#include "texteditor.h"
#include <ui/core/text/internal.h>
#include <bitset>

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

float TextEditor::Impl::measure(std::string_view text, FontId font, float scale) const
{
    auto* fonts = documentFont ? &detail::fontManager() : nullptr;
    if (!font) font = documentFont;
    bool alternate = fonts && font != documentFont;
    if (alternate) measurer->font(fonts->textFont(font));
    if (scale != 1) measurer->size(configuration.fontSize * scale * fontPointsPerLogicalPixel);
    float width = 0;
    uint32_t previous = 0;
    for (size_t offset = 0; offset < text.size();) {
        tvg::GlyphMetrics metrics{};
        size_t consumed = 0;
        if (!glyphMetrics(measurer.get(), text.substr(offset), metrics, &consumed) || !consumed) break;
        if (fonts) {
            auto glyph = fonts->glyph(font, codepoint(text.substr(offset, consumed)));
            width += fonts->textKerning(font, previous, glyph) * configuration.fontSize * scale / fonts->unitsPerEm(font);
            previous = glyph;
        }
        width += metrics.advance;
        offset += consumed;
    }
    if (alternate) measurer->font(fonts->textFont(documentFont));
    if (scale != 1) measurer->size(configuration.fontSize * fontPointsPerLogicalPixel);
    return width;
}

std::vector<TextLine> TextEditor::Impl::layoutRows(std::string_view text, size_t base, std::vector<std::string>& display, std::vector<std::vector<Run>>& styles, std::vector<Widget>* widgets, bool composed)
{
    auto* fonts = documentFont ? &detail::fontManager() : nullptr;
    std::vector<TextLine> lines;
    auto selection = editing.selection();
    auto sourceOffset = [&](size_t offset) {
        if (!composed || offset <= selection.start()) return offset;
        if (offset < selection.start() + preedit.size()) return selection.start();
        return size_t(ptrdiff_t(offset) - ptrdiff_t(preedit.size()) + ptrdiff_t(selection.end() - selection.start()));
    };
    size_t widget = 0;
    constexpr float headingScale[] = {1, 2, 1.65f, 1.4f, 1.2f, 1.1f, 1.05f};
    // ThorVG measures a glyph per call, so ASCII ones are measured once per font and size.
    std::array<tvg::GlyphMetrics, 128> ascii;
    std::bitset<128> asciiMeasured;
    auto asciiFont = documentFont;
    float asciiSize = 0;
    for (size_t begin = 0;;) {
        auto stop = std::min(text.find('\n', begin), text.size());
        auto block = markdownBlock(sourceOffset(base + begin));
        const auto& formats = blockFormats(block);
        auto blockBase = markdownBlocks.empty() ? 0 : markdownBlocks[block].start;
        auto cursor = std::upper_bound(formats.begin(), formats.end(), sourceOffset(base + begin) - blockBase, [](size_t offset, const auto& format) { return offset < format.start; });
        if (cursor != formats.begin()) --cursor;
        auto formatAt = [&](size_t offset) {
            if (!configuration.markdown) return Format{};
            auto relative = sourceOffset(offset) - blockBase;
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
        // Accumulate precisely because these positions also anchor visible text slices.
        double x = 0;
        size_t column = 0;
        auto firstWidget = widget;
        bool widgetBreak = false;
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
            auto byte = static_cast<unsigned char>(value.front());
            if (value.size() == 1 && byte < ascii.size()) {
                if (font != asciiFont || size != asciiSize) {
                    asciiMeasured.reset();
                    asciiFont = font;
                    asciiSize = size;
                }
                if (!asciiMeasured[byte]) {
                    ascii[byte] = {};
                    glyphMetrics(measurer.get(), value, ascii[byte]);
                    asciiMeasured.set(byte);
                }
                metrics = ascii[byte];
            } else glyphMetrics(measurer.get(), value, metrics);
            if (fonts) {
                auto glyph = fonts->glyph(font, cp);
                x += fonts->textKerning(font, previous, glyph) * size / fonts->unitsPerEm(font);
                previous = glyph;
            }
            if (caret) line.x.back() = x;
            if (!runs.empty() && leading && cp != ' ') {
                runs.back().firstWidth = metrics.max.x - metrics.min.x;
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
            auto item = widgets && widget < widgets->size() ? &(*widgets)[widget] : nullptr;
            if (item && offset == item->range.start && item->range.display == TextWidget::Replace) {
                auto width = item->range.width * size;
                item->bounds = {float(x), 0, width, 0};
                // Hidden source does not need glyph lookup, font fallback, or outline metrics.
                while (offset < item->range.end) {
                    auto next = offset + 1;
                    while (next < item->range.end && (static_cast<unsigned char>(text[next]) & 0xc0) == 0x80)
                        ++next;
                    if (text[offset] == '\t') {
                        auto spaces = 4 - column % 4;
                        shown.append(spaces, ' ');
                        column += spaces;
                    } else {
                        shown.append(text.substr(offset, next - offset));
                        ++column;
                    }
                    offset = next;
                }
                x += width;
                line.offsets.push_back(offset - begin);
                line.x.push_back(x);
                widgetBreak = true;
                ++widget;
                continue;
            }
            if (item && offset == item->range.start) widgetBreak = true;
            auto next = offset + 1;
            while (next < stop && (static_cast<unsigned char>(text[next]) & 0xc0) == 0x80)
                ++next;
            auto value = text.substr(offset, next - offset);
            auto cp = codepoint(value);
            auto format = formatAt(base + offset);
            auto face = format.bold ? boldDocumentFont : documentFont;
            if (fonts) {
                auto glyph = cp == '\t' ? uint32_t(' ') : cp;
                auto resolved = fonts->fallback(face, glyph);
                if (face != documentFont && !fonts->glyph(resolved, glyph)) resolved = fonts->fallback(documentFont, glyph);
                face = resolved;
            }
            if (runs.empty() || format.start != current.start || format.end != current.end || face != font || widgetBreak) {
                current = format;
                font = face;
                measurer->font(fonts ? fonts->textFont(font) : themeTokens.font.c_str());
                measurer->size(size * fontPointsPerLogicalPixel);
                tvg::TextMetrics metrics{};
                measurer->metrics(metrics);
                rowHeight = std::max(rowHeight, metrics.advance);
                bool styled = item && offset >= item->range.start && offset < item->range.end;
                runs.push_back({.first = shown.size(), .left = float(x), .top = metrics.ascent - metrics.descent, .scale = sizeScale, .font = font, .format = format, .foreground = styled ? item->range.foreground : std::nullopt, .background = styled ? item->range.background : std::nullopt});
                leading = true;
                previous = 0;
                widgetBreak = false;
            }
            if (fonts && !fonts->glyph(font, cp == '\t' ? uint32_t(' ') : cp)) runs.back().missing = true;
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
            if (item && offset == item->range.end) {
                auto width = item->range.width * size;
                item->bounds = {float(x), 0, width, 0};
                if (item->range.display != TextWidget::Below) {
                    x += width;
                    line.x.back() = x;
                }
                widgetBreak = true;
                ++widget;
            }
        }
        for (auto& run : runs)
            run.top = (rowHeight - run.top) / 2;
        float below = 0;
        if (widgets)
            for (size_t i = firstWidget; i < widget; ++i) {
                auto& item = (*widgets)[i];
                if (item.range.display == TextWidget::Below) {
                    float h = item.range.height * configuration.fontSize;
                    item.bounds = {0, rowHeight + below + configuration.fontSize * .5f, std::min(textArea().w, item.range.width * configuration.fontSize), h};
                    below += h + configuration.fontSize;
                    continue;
                }
                item.bounds.h = std::min(rowHeight, size * 1.25f);
                item.bounds.y = (rowHeight - item.bounds.h) / 2;
            }
        line.textHeight = rowHeight;
        line.bounds = {0, 0, float(x), rowHeight + below};
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
    std::vector<Widget> widgets;
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
        line.textHeight = line.bounds.h;
        matchWidgets(widgets, text.substr(begin, end - begin), offset, true);
        for (const auto& item : widgets)
            line.bounds.h += (item.range.height + 1) * configuration.fontSize;
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
    const auto& old = view.line(row);
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
    matchWidgets(cached->widgets, source, sourceBase);
    auto lines = layoutRows(source, sourceBase, display, styles, &cached->widgets);
    lines[0].base = old.base;
    lines[0].top = old.y();
    lines[0].bounds.y = old.bounds.y;
    auto displacement = double(lines[0].bounds.h) - old.bounds.h;
    updateWidth(old.bounds.w, lines[0].bounds.w);
    cached->assigned = false;
    cacheTabs(*cached, source);
    cached->display = std::move(display[0]);
    cached->styles = std::move(styles[0]);
    cached->measured = true;
    view.layout(row, 1, std::move(lines), 0, displacement);
}

size_t TextEditor::Impl::replaceRows(size_t first, size_t count, std::string_view text, size_t base, bool composed)
{
    std::vector<std::string> display;
    std::vector<std::vector<Run>> runs;
    std::vector<Widget> widgets;
    if (composed && !widgetRules.empty()) {
        const auto& document = editing.document();
        auto selection = editing.selection();
        auto firstRow = document.lineAt(selection.start()), lastRow = document.lineAt(selection.end());
        std::vector<Widget> matches;
        auto collect = [&](size_t row) {
            auto start = document.lineStart(row);
            matchWidgets(matches, std::string_view(document.text()).substr(start, document.lineEnd(row) - start), start);
            for (auto item : matches) {
                auto begin = start + item.range.start, end = start + item.range.end;
                if (end <= selection.start()) {
                    item.range.start = begin - base;
                    item.range.end = end - base;
                } else if (begin >= selection.end()) {
                    auto shift = ptrdiff_t(preedit.size()) - ptrdiff_t(selection.end() - selection.start());
                    item.range.start = size_t(ptrdiff_t(begin - base) + shift);
                    item.range.end = size_t(ptrdiff_t(end - base) + shift);
                } else continue;
                widgets.push_back(item);
            }
        };
        collect(firstRow);
        if (lastRow != firstRow) collect(lastRow);
    }
    auto lines = composed ? layoutRows(text, base, display, runs, &widgets, true) : rowMetadata(text, base);
    auto oldSize = view.lineCount();
    if (!lineWidths.empty()) {
        for (size_t row = first; row < first + count; ++row)
            updateWidth(view.line(row).bounds.w, 0);
    }
    double top = first < oldSize ? view.line(first).y() : contentHeight();
    double bottom = first + count < oldSize ? view.line(first + count).y() : contentHeight();
    double y = top;
    for (auto& line : lines) {
        line.top = y;
        line.bounds.y = float(y);
        y += line.bounds.h;
        if (line.bounds.w > 0) updateWidth(0, line.bounds.w);
    }
    auto shift = first + count < oldSize ? ptrdiff_t(base + text.size() + 1) - ptrdiff_t(view.line(first + count).start()) : 0;
    auto rows = lines.size();
    auto eraseFirst = linePaints.lower_bound(first);
    auto eraseEnd = rows == count ? linePaints.lower_bound(first + count) : linePaints.end();
    for (auto it = eraseFirst; it != eraseEnd; ++it)
        view.discard(it->first);
    linePaints.erase(eraseFirst, eraseEnd);
    view.layout(first, count, std::move(lines), shift, y - bottom);
    if (composed) {
        auto widget = widgets.begin();
        for (size_t i = 0; i < rows; ++i) {
            auto& cached = linePaints[first + i];
            cached = std::make_unique<RenderLine>();
            const auto& row = view.line(first + i);
            cacheTabs(*cached, text.substr(row.start() - base, row.length));
            cached->display = std::move(display[i]);
            cached->styles = std::move(runs[i]);
            while (widget != widgets.end() && base + widget->range.start < row.end()) {
                auto item = *widget++;
                item.range.start -= row.start() - base;
                item.range.end -= row.start() - base;
                cached->widgets.push_back(item);
            }
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
    auto reset = fonts ? fonts->resetGeneration() : 0;
    auto bold = font;
    if (fonts && font && font <= fonts->fonts().size()) {
        auto match = fonts->find(fonts->fonts()[font - 1].family, 700);
        if (match) bold = match;
    }
    bool markdownChanged = configuration.markdown && markdownRevision != document.revision();
    bool full = layoutDirty || change == tn::ChangeStatus::Untracked || font != documentFont || reset != measuredFontReset || bold != boldDocumentFont || (composedRows && measuredRevision != document.revision());
    if (!full && generation != measuredFontGeneration) {
        // Appending a font affects only rows that previously had an unsupported glyph.
        for (auto& [row, paint] : linePaints) {
            if (std::none_of(paint->styles.begin(), paint->styles.end(), [](const auto& run) { return run.missing; })) continue;
            view.discard(row);
            paint->measured = paint->assigned = false;
        }
    }
    bool replaceComposition = composedRows && !full && change == tn::ChangeStatus::Unchanged && !preedit.empty() && selection.anchor == measuredSelection.anchor && selection.caret == measuredSelection.caret;
    if (markdownChanged) parseMarkdown(start, end, !full && markdownRevision == measuredRevision);
    if (full) {
        if (font != documentFont || generation != measuredFontGeneration) ++textStyleRevision;
        documentFont = font;
        boldDocumentFont = bold;
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
            auto digitFont = fonts ? fonts->fallback(documentFont, '0') : documentFont;
            auto digits = [&](FontId face, float& ascent) {
                measurer->font(fonts ? fonts->textFont(face) : themeTokens.font.c_str());
                tvg::TextMetrics metrics{};
                measurer->metrics(metrics);
                ascent = metrics.ascent;
                float low = std::numeric_limits<float>::max(), high = std::numeric_limits<float>::lowest();
                for (char ch = '0'; ch <= '9'; ++ch) {
                    tvg::GlyphMetrics glyph{};
                    if (!glyphMetrics(measurer.get(), std::string_view(&ch, 1), glyph) || glyph.max.y <= glyph.min.y) continue;
                    low = std::min(low, glyph.min.y);
                    high = std::max(high, glyph.max.y);
                }
                return high > low ? std::pair{high, high - low} : std::pair{0.0f, 0.0f};
            };
            float bodyAscent = 0, numberAscent = 0;
            auto body = digits(digitFont, bodyAscent);
            tvg::TextMetrics bodyMetrics{};
            measurer->metrics(bodyMetrics);
            auto number = digits(lineNumberFont, numberAscent);
            numberScale = (body.second > 0 && number.second > 0 ? body.second / number.second : 1) * configuration.lineNumberScale;
            auto top = (std::max(lineHeight, bodyMetrics.advance) - (bodyMetrics.ascent - bodyMetrics.descent)) / 2;
            numberTop = top + bodyAscent - body.first - (numberAscent - number.first) * numberScale + configuration.lineNumberOffset;
            measurer->font(fonts ? fonts->textFont(documentFont) : themeTokens.font.c_str());
            for (char ch = '0'; ch <= '9'; ++ch)
                digitWidth = std::max(digitWidth, measure(std::string_view(&ch, 1), lineNumberFont, numberScale));
        }
        linePaints.clear();
        lineWidths.clear();
        widest = 0;
        replaceRows(0, view.lineCount(), source, 0);
        composedRows = 0;
    } else {
        if (composedRows && recompose && !replaceComposition) {
            auto last = composedFirst + composedSpan - 1;
            replaceRows(composedFirst, composedRows, source.substr(document.lineStart(composedFirst), document.lineEnd(last) - document.lineStart(composedFirst)), document.lineStart(composedFirst));
            composedRows = 0;
        }
        if (change == tn::ChangeStatus::Changed) {
            auto first = document.lineAt(start), last = document.lineAt(end);
            auto count = last + 1 - first + view.lineCount() - document.lineCount();
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
            const auto& line = view.line(row);
            if (cursor >= line.start() && cursor <= line.end()) preeditCaret = {line.position(cursor), float(line.y() - vertical), 1.5f, line.textHeight};
            if (finish > line.start() && begin <= line.end()) {
                float left = line.position(std::max(begin, line.start()));
                float right = line.position(std::min(finish, line.end()));
                if (finish > line.end()) right += line.breakWidth;
                preeditRects.push_back({left, float(line.y() - vertical), right - left, line.textHeight});
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
    measuredFontReset = reset;
    layoutDirty = false;
    textContentDirty = true;
    dirty = true;
    clampScroll();
}

void TextEditor::Impl::cacheTabs(RenderLine& line, std::string_view source)
{
    line.tabOffsets.clear();
    if (source.find('\t') == std::string_view::npos) return;
    size_t column = 0, extra = 0;
    for (size_t i = 0; i < source.size(); ++i) {
        if ((static_cast<unsigned char>(source[i]) & 0xc0) == 0x80) continue;
        if (source[i] == '\t') {
            auto spaces = 4 - column % 4;
            extra += spaces - 1;
            column += spaces;
            line.tabOffsets.emplace_back(i + 1, extra);
        } else ++column;
    }
}

float TextEditor::Impl::prepareText(tvg::Text* text, const RenderLine& line, const Run* run)
{
    auto first = run ? run->first : 0;
    auto end = run ? run->end : line.display.size();
    auto left = run ? run->left : 0.0f;
    auto font = run ? run->font : documentFont;
    auto scale = run ? run->scale : 1.0f;
    if (first < line.clipFirst) {
        first = line.clipFirst;
        left = line.clipX;
    }
    end = std::min(end, line.clipEnd);
    if (first >= end) {
        text->text("");
        return left;
    }
    auto value = line.display.substr(first, end - first);
    auto leading = value.find_first_not_of(' ');
    if (leading == std::string::npos) leading = value.size();
    if (leading && leading < value.size()) {
        // Preserve the advance and kerning of spaces omitted before the first outline.
        tvg::GlyphMetrics metrics{};
        size_t consumed = 0;
        if (glyphMetrics(text, std::string_view(value).substr(leading), metrics, &consumed) && consumed)
            left += measure(std::string_view(value).substr(0, leading + consumed), font, scale) - metrics.advance;
        else left += measure(std::string_view(value).substr(0, leading), font, scale);
    }
    if (run && run->format.italic && leading < value.size()) {
        // ThorVG's italic origin depends on the first glyph's outline width.
        tvg::GlyphMetrics metrics{};
        if (glyphMetrics(text, std::string_view(value).substr(leading), metrics))
            left += 0.18f * (run->firstWidth - (metrics.max.x - metrics.min.x));
    }
    text->text(value.c_str() + leading);
    return left;
}

TextEditor::Impl::RenderLine& TextEditor::Impl::prepareLine(size_t row)
{
    auto* fonts = documentFont ? &detail::fontManager() : nullptr;
    measureLine(row);
    auto& cached = linePaints[row];
    if (!cached->assigned) cached->number->text(std::to_string(row + 1).c_str());
    const auto& geometry = view.line(row);
    // Keep boundary kerning and glyph overhang outside the visible clip.
    auto padding = geometry.textHeight * 2;
    auto begin = std::lower_bound(geometry.x.begin(), geometry.x.end(), horizontal - padding);
    if (begin != geometry.x.begin()) --begin;
    auto end = std::upper_bound(begin, geometry.x.end(), horizontal + textArea().w + padding);
    auto offset = [&](size_t index) {
        auto value = geometry.offsets[std::min(index, geometry.offsets.size() - 1)];
        auto tab = std::upper_bound(cached->tabOffsets.begin(), cached->tabOffsets.end(), value, [](size_t value, const auto& tab) { return value < tab.first; });
        return value + (tab == cached->tabOffsets.begin() ? 0 : std::prev(tab)->second);
    };
    auto firstByte = offset(size_t(begin - geometry.x.begin()));
    auto endByte = offset(size_t(end - geometry.x.begin()));
    bool clipped = firstByte != cached->clipFirst || endByte != cached->clipEnd;
    cached->clipFirst = firstByte;
    cached->clipEnd = endByte;
    cached->clipX = begin != geometry.x.end() ? *begin : geometry.bounds.w;
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
        cached->number->size(configuration.fontSize * numberScale * fontPointsPerLogicalPixel);
        cached->numberWidth = measure(std::to_string(row + 1), lineNumberFont, numberScale);
        cached->style = textStyleRevision;
    }
    if (configuration.markdown || !cached->widgets.empty() || cached->styles.size() > 1 || (!cached->styles.empty() && cached->styles.front().font != documentFont)) {
        if (restyled || !cached->assigned || clipped) {
            prepareRuns(*cached);
            if (!cached->group) {
                cached->group = tvg::Scene::gen();
                cached->group->ref();
            }
            clearScene(cached->group);
            if (cached->background) clearScene(cached->background);
            auto height = view.line(row).textHeight;
            for (size_t i = 0; i < cached->styles.size(); ++i) {
                const auto& run = cached->styles[i];
                if (run.end <= cached->clipFirst || run.first >= cached->clipEnd) continue;
                auto text = i ? cached->runs[i - 1]->text : cached->body;
                if (!text->text()) continue;
                cached->group->add(text);
                auto color = run.foreground.value_or(themeTokens.markdownColor(run.format.color));
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
                if (run.format.code || run.background) {
                    if (!cached->background) {
                        cached->background = tvg::Scene::gen();
                        cached->background->ref();
                    }
                    auto color = run.background.value_or(themeTokens.markdownColor(&Theme::markdownCodeBackground));
                    auto shape = tvg::Shape::gen();
                    shape->appendRect(run.left, 0, run.width, height, 2, 2);
                    shape->fill(color.r, color.g, color.b, color.a);
                    cached->background->add(shape);
                }
            }
        }
        cached->assigned = true;
        return *cached;
    }
    if (cached->group) clearScene(cached->group);
    if (cached->background) clearScene(cached->background);
    cached->runs.clear();
    if (restyled || !cached->assigned || clipped) {
        cached->bodyLeft = prepareText(cached->body, *cached);
        cached->assigned = true;
    }
    return *cached;
}

void TextEditor::Impl::prepareViewport(size_t first, size_t end)
{
    bool rebuild = textContentDirty || first < cachedFirst || end > cachedEnd;
    auto area = textArea();
    if (!rebuild && first == drawnFirst && end == drawnEnd && horizontal == drawnHorizontal && area.w == drawnWidth) return;
    if (rebuild) {
        auto cached = view.visibleLines(vertical - area.h, vertical + area.h * 2);
        cachedFirst = cached.first;
        cachedEnd = cached.second;
        auto caretRow = editing.document().lineAt(editing.selection().caret);
        for (auto it = linePaints.begin(); it != linePaints.end();) {
            bool composed = it->first >= composedFirst && it->first < composedFirst + composedRows;
            if ((it->first < cachedFirst || it->first >= cachedEnd) && it->first != caretRow && !composed) {
                view.discard(it->first);
                it = linePaints.erase(it);
            } else ++it;
        }
        renderOrigin = cachedFirst < view.lineCount() ? view.line(cachedFirst).y() : 0;
        for (size_t row = cachedFirst; row < cachedEnd; ++row) {
            auto& line = prepareLine(row);
            auto y = float(view.line(row).y() - renderOrigin);
            if (configuration.lineNumbers) transform(line.number, {1, 0, gutter - configuration.padding - line.numberWidth, 0, 1, y + numberTop, 0, 0, 1});
            if (configuration.markdown || (line.group && !line.group->paints().empty())) {
                transform(line.group, {1, 0, 0, 0, 1, y, 0, 0, 1});
                if (line.background) transform(line.background, {1, 0, 0, 0, 1, y, 0, 0, 1});
            } else transform(line.body, {1, 0, line.bodyLeft, 0, 1, y + documentTop, 0, 0, 1});
        }
    }
    textContent.begin(themeTokens, {0, 0, widest, float(height * 3)}, seconds);
    codeContent.begin(themeTokens, {0, 0, widest, float(height * 3)}, seconds);
    numberContent.begin(themeTokens, {0, 0, gutter, float(height * 3)}, seconds);
    for (size_t row = first; row < end; ++row) {
        auto& line = prepareLine(row);
        bool grouped = configuration.markdown || (line.group && !line.group->paints().empty());
        if (!grouped) transform(line.body, {1, 0, line.bodyLeft, 0, 1, float(view.line(row).y() - renderOrigin) + documentTop, 0, 0, 1});
        if (configuration.lineNumbers) numberContent.add(line.number);
        if (configuration.markdown && line.background && !line.background->paints().empty()) codeContent.add(line.background);
        textContent.add(grouped ? static_cast<tvg::Paint*>(line.group) : line.body);
    }
    textContent.end();
    codeContent.end();
    numberContent.end();
    drawnFirst = first;
    drawnEnd = end;
    drawnHorizontal = horizontal;
    drawnWidth = area.w;
    textContentDirty = false;
}

}  // namespace ui
