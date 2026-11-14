#include "texteditor.h"
#include <algorithm>
#include <cmath>

namespace ui
{

bool TextEditor::widgets(std::string_view owner, TextWidgetRule rule)
{
    if (owner.empty() || !rule.match || !rule.draw || !std::isfinite(rule.popupWidth) || !std::isfinite(rule.popupHeight) || !std::isfinite(rule.popupPadding) || rule.popupWidth < 32 || rule.popupHeight < 32 || rule.popupPadding < 0 || rule.popupPadding * 2 >= std::min(rule.popupWidth, rule.popupHeight) || (rule.suggest && !rule.popup)) return false;
    auto& rules = impl->widgetRules;
    auto found = std::find_if(rules.begin(), rules.end(), [&](const auto& entry) { return entry.owner == owner; });
    if (found == rules.end()) rules.push_back({std::string(owner), std::move(rule)});
    else found->rule = std::move(rule);
    ++impl->widgetGeneration;
    impl->widgetIdentities.clear();
    impl->widgetPopup.reset();
    impl->suggestionRevision = uint64_t(-1);
    impl->restyle();
    return true;
}

void TextEditor::clearWidgets(std::string_view owner)
{
    auto& rules = impl->widgetRules;
    auto found = std::find_if(rules.begin(), rules.end(), [&](const auto& entry) { return entry.owner == owner; });
    if (found == rules.end()) return;
    rules.erase(found);
    ++impl->widgetGeneration;
    impl->widgetIdentities.clear();
    impl->widgetPopup.reset();
    impl->restyle();
}

bool TextEditor::replaceWidget(const TextWidgetEvent& event, std::string_view value, std::string& error)
{
    const auto& document = impl->editing.document();
    if (event.revision != document.revision() || event.start >= event.end || event.end > document.text().size() || document.text().compare(event.start, event.end - event.start, event.value) != 0) {
        error = "widget source changed";
        return false;
    }
    auto x = impl->horizontal;
    auto y = impl->vertical;
    if (!applyEdits({.revision = event.revision, .edits = {{event.start, event.end, std::string(value)}}}, error)) return false;
    scroll(x, y);
    closeWidget();
    return true;
}

void TextEditor::closeWidget()
{
    impl->widgetPopup.reset();
    if (impl->widgetHost) impl->widgetHost->focus(impl->widgetEditorId);
    impl->invalidate();
}

void TextEditor::Impl::matchWidgets(std::vector<Widget>& result, std::string_view text, size_t base, bool blocks)
{
    result.clear();
    if (widgetRules.empty()) return;
    const auto& document = editing.document();
    std::vector<TextWidget> matches;
    for (size_t rule = 0; rule < widgetRules.size(); ++rule) {
        if (blocks && !widgetRules[rule].rule.block) continue;
        matches.clear();
        widgetRules[rule].rule.match(text, base, matches);
        for (auto range : matches) {
            if (result.size() >= 128) break;
            if (range.start < base || range.start >= range.end || range.end - base > text.size() || range.end - range.start > 4096 || document.clamp(range.start) != range.start || document.clamp(range.end) != range.end || !std::isfinite(range.width) || range.width <= 0 || range.width > 32 || (range.display != TextWidget::Replace && range.display != TextWidget::After && range.display != TextWidget::Below)) continue;
            if (range.display == TextWidget::Below && (!widgetRules[rule].rule.block || !std::isfinite(range.height) || range.height <= 0 || range.height > 64)) continue;
            if (blocks && range.display != TextWidget::Below) continue;
            if (!widgetRules[rule].rule.code && configuration.markdown && !markdownBlocks.empty()) {
                auto block = markdownBlock(range.start);
                const auto& formats = blockFormats(block);
                auto first = range.start - markdownBlocks[block].start, end = range.end - markdownBlocks[block].start;
                if (std::any_of(formats.begin(), formats.end(), [&](const auto& format) { return format.code && format.start < end && format.end > first; })) continue;
            }
            range.start -= base;
            range.end -= base;
            if (std::any_of(result.begin(), result.end(), [&](const auto& item) { return item.range.start < range.end && item.range.end > range.start; })) continue;
            result.push_back({range, rule, {}});
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.range.start < b.range.start; });
}

size_t TextEditor::Impl::widgetBoundary(size_t offset, bool forward)
{
    if (widgetRules.empty() || !preedit.empty()) return offset;
    auto row = editing.document().lineAt(offset);
    measureLine(row);
    const auto& line = view.line(row);
    auto at = std::lower_bound(line.offsets.begin(), line.offsets.end(), offset - line.start());
    if (at == line.offsets.end() || at == line.offsets.begin() || *at == offset - line.start()) return offset;
    return line.start() + (forward ? *at : *std::prev(at));
}

void TextEditor::Impl::snapWidgets(bool forward, bool expand)
{
    if (widgetRules.empty() || !preedit.empty()) return;
    const auto& selections = editing.selections();
    auto primary = editing.selection();
    size_t primaryIndex = 0;
    std::vector<tn::Selection> corrected;
    for (size_t i = 0; i < selections.size(); ++i) {
        auto range = selections[i];
        if (range.anchor == primary.anchor && range.caret == primary.caret) primaryIndex = i;
        if (expand && !range.empty()) {
            bool reverse = range.anchor > range.caret;
            range.anchor = widgetBoundary(range.anchor, reverse);
            range.caret = widgetBoundary(range.caret, !reverse);
        } else {
            range.caret = widgetBoundary(range.caret, forward);
            if (selections[i].empty()) range.anchor = range.caret;
        }
        if (corrected.empty() && (range.anchor != selections[i].anchor || range.caret != selections[i].caret)) corrected = selections;
        if (!corrected.empty()) corrected[i] = range;
    }
    if (!corrected.empty()) editing.select(corrected, primaryIndex);
}

bool TextEditor::copy()
{
    if (!writeClipboard) return false;
    const auto& selections = impl->editing.selections();
    if (std::none_of(selections.begin(), selections.end(), [](const auto& range) { return !range.empty(); })) return false;
    if (std::none_of(impl->widgetRules.begin(), impl->widgetRules.end(), [](const auto& entry) { return bool(entry.rule.clipboard); })) {
        writeClipboard(impl->editing.selectedText());
        return true;
    }
    impl->relayout();
    const auto& document = impl->editing.document();
    std::string_view source = document.text();
    std::string output;
    std::vector<Impl::Widget> matches;
    for (size_t i = 0; i < selections.size(); ++i) {
        if (i) output += '\n';
        auto first = selections[i].start(), end = selections[i].end();
        for (auto row = document.lineAt(first); first < end; ++row) {
            auto base = document.lineStart(row), stop = document.lineEnd(row);
            impl->matchWidgets(matches, source.substr(base, stop - base), base);
            for (const auto& item : matches) {
                const auto& range = item.range;
                const auto& callback = impl->widgetRules[item.rule].rule.clipboard;
                if (!callback || base + range.start < first || base + range.end > end) continue;
                output.append(source.substr(first, base + range.start - first));
                output += callback(source.substr(base + range.start, range.end - range.start));
                first = base + range.end;
            }
            auto next = std::min(end, stop < source.size() ? stop + 1 : stop);
            output.append(source.substr(first, next - first));
            first = next;
        }
    }
    writeClipboard(output);
    return true;
}

void TextEditor::Impl::paintWidgets(Context& host, std::string_view id, Rect bounds)
{
    if (widgetRules.empty()) return;
    widgetHost = &host;
    widgetEditorId = id;
    const auto& document = editing.document();
    auto revision = document.revision(), generation = widgetGeneration;
    if (widgetRevision != revision) {
        size_t start = 0, end = 0;
        auto change = document.changedSince(widgetRevision, start, end);
        auto shift = ptrdiff_t(document.text().size()) - ptrdiff_t(widgetSourceSize);
        if (change == tn::ChangeStatus::Untracked) widgetIdentities.clear();
        else {
            auto oldEnd = size_t(ptrdiff_t(end) - shift);
            std::erase_if(widgetIdentities, [&](auto& item) {
                if (item.end <= start) return false;
                if (item.start < oldEnd) return true;
                item.start = size_t(ptrdiff_t(item.start) + shift);
                item.end = size_t(ptrdiff_t(item.end) + shift);
                return false;
            });
        }
        widgetRevision = revision;
        widgetSourceSize = document.text().size();
    }
    for (auto& item : widgetIdentities)
        item.seen = false;
    if (widgetPopup && (widgetPopup->event.revision != revision || !preedit.empty())) widgetPopup.reset();
    auto area = textArea();
    area.x += bounds.x;
    area.y += bounds.y;
    auto selection = editing.selection();
    bool changed = suggestionRevision != revision || suggestionCaret != selection.caret || suggestionAnchor != selection.anchor;
    if (widgetPopup && widgetPopup->suggestion && (!hasFocus || configuration.readOnly || !input.text || !preedit.empty() || selection.start() != selection.end() || changed)) widgetPopup.reset();
    if (changed && !widgetPopup && hasFocus && !configuration.readOnly && input.text && preedit.empty()) {
        suggestionRevision = revision;
        suggestionCaret = selection.caret;
        suggestionAnchor = selection.anchor;
        if (selection.start() == selection.end()) {
            auto start = document.lineStart(document.lineAt(selection.caret));
            start = document.clamp(std::max(start, selection.caret > 4096 ? selection.caret - 4096 : 0));
            auto prefix = std::string_view(document.text()).substr(start, selection.caret - start);
            for (size_t index = 0; index < widgetRules.size(); ++index) {
                const auto& rule = widgetRules[index].rule;
                if (!rule.suggest) continue;
                auto at = rule.suggest(prefix);
                if (at >= prefix.size() || document.clamp(start + at) != start + at) continue;
                if (!rule.code && configuration.markdown && !markdownBlocks.empty()) {
                    auto block = markdownBlock(start + at);
                    const auto& formats = blockFormats(block);
                    auto first = start + at - markdownBlocks[block].start, end = selection.caret - markdownBlocks[block].start;
                    if (std::any_of(formats.begin(), formats.end(), [&](const auto& format) { return format.code && format.start < end && format.end > first; })) continue;
                }
                widgetPopup = WidgetPopup{index, {start + at, selection.caret, revision, {}, std::string(prefix.substr(at))}, false, true};
                break;
            }
        }
    }
    auto visible = view.visibleLines(vertical, vertical + area.h);
    bool anchored = false;
    if (widgetPopup && widgetPopup->suggestion) {
        auto caret = owner.caretRect();
        caret.x += bounds.x;
        caret.y += bounds.y;
        widgetPopup->event.bounds = caret;
        auto hit = caret.intersect(area).intersect(host.clip());
        anchored = hit.w > 0 && hit.h > 0;
    }
    host.clip(area, [&] {
        for (size_t row = visible.first; row < visible.second; ++row) {
            auto found = linePaints.find(row);
            if (found == linePaints.end() || !found->second->measured) continue;
            const auto& line = view.line(row);
            auto sourceOffset = [&](size_t offset) {
                if (!composedRows || offset < selection.start() + preedit.size()) return offset;
                return size_t(ptrdiff_t(offset) - ptrdiff_t(preedit.size()) + ptrdiff_t(selection.end() - selection.start()));
            };
            for (const auto& item : found->second->widgets) {
                auto rect = item.bounds;
                rect.x += area.x - horizontal;
                rect.y += area.y + float(line.y() - vertical);
                auto hit = rect.intersect(area).intersect(host.clip());
                if (hit.w <= 0 || hit.h <= 0) continue;
                TextWidgetEvent event{sourceOffset(line.start() + item.range.start), sourceOffset(line.start() + item.range.end), revision, rect, {}};
                event.value = document.text().substr(event.start, event.end - event.start);
                auto position = std::pair{item.rule, event.start};
                auto identity = std::lower_bound(widgetIdentities.begin(), widgetIdentities.end(), position, [](const auto& item, auto key) { return std::pair{item.rule, item.start} < key; });
                if (identity == widgetIdentities.end() || identity->rule != item.rule || identity->start != event.start)
                    identity = widgetIdentities.insert(identity, {item.rule, event.start, event.end, ++nextWidgetId});
                else if (identity->end != event.end) *identity = {item.rule, event.start, event.end, ++nextWidgetId};
                identity->seen = true;
                auto key = std::string(id) + "/widget/" + widgetRules[item.rule].owner + '/' + std::to_string(identity->id);
                auto& rule = widgetRules[item.rule].rule;
                auto state = host.control(key, hit, [this, index = item.rule, generation, event, bounds](const Event& input) {
                    if (generation != widgetGeneration || event.revision != editing.document().revision()) return;
                    if (input.type == EventType::Press) {
                        widgetHost->focus(widgetEditorId);
                        focus(true);
                        pointerDown(input.x - bounds.x, input.y - bounds.y, input.modifiers.shift, input.clicks);
                        return;
                    }
                    if (input.type == EventType::Drag) {
                        pointerMove(input.x - bounds.x, input.y - bounds.y);
                        return;
                    }
                    if (input.type == EventType::Release || input.type == EventType::Cancel) {
                        pointerUp();
                        return;
                    }
                    if (!input.activated() || !preedit.empty() || !editing.selection().empty()) return;
                    auto activate = widgetRules[index].rule.activate;
                    if (activate) {
                        activate(owner, event);
                        if (widgetHost) widgetHost->focus(widgetEditorId);
                    } else if (widgetRules[index].rule.popup) widgetPopup = WidgetPopup{index, event};
                    invalidate(); }, !configuration.readOnly || bool(rule.popup), ControlFlag::Capture);
                rule.draw(host, key, rect, state, event.value);
                if (widgetPopup && !widgetPopup->suggestion && widgetPopup->rule == item.rule && widgetPopup->event.start == event.start) {
                    widgetPopup->event.bounds = rect;
                    anchored = true;
                }
            }
        }
    });
    std::erase_if(widgetIdentities, [](const auto& item) { return !item.seen; });
    if (!widgetPopup) return;
    if (!anchored) {
        widgetPopup.reset();
        return;
    }
    if (!widgetPopup->opened) {
        host.focus(id);
        widgetPopup->opened = true;
    }
    auto popup = *widgetPopup;
    auto& rule = widgetRules[popup.rule].rule;
    auto viewport = host.clip();
    Rect panel{popup.event.bounds.x, popup.event.bounds.y + popup.event.bounds.h + 6, std::min(rule.popupWidth, viewport.w), std::min(rule.popupHeight, viewport.h)};
    panel.x = std::clamp(panel.x, viewport.x, viewport.x + viewport.w - panel.w);
    if (panel.y + panel.h > viewport.y + viewport.h) panel.y = popup.event.bounds.y - panel.h - 6;
    panel.y = std::clamp(panel.y, viewport.y, viewport.y + viewport.h - panel.h);
    auto prefix = std::string(id) + "/widget/popup";
    auto close = [this, &host, editor = std::string(id)] {
        owner.closeWidget();
        host.focus(editor);
    };
    if (!popup.suggestion) {
        host.modal(prefix + "/panel", close);
        host.control(prefix + "/outside", viewport, [close](const Event& event) { if (event.activated()) close(); }, true, ControlFlag::None);
    }
    host.rect({panel.x, panel.y + 3, panel.w, panel.h}, {0, 0, 0, 35}, 8);
    host.rect(panel, themeTokens.surface, 8, themeTokens.border);
    host.control(prefix + "/panel", panel, [](const Event&) {}, true, ControlFlag::None);
    host.clip(panel, [&] { rule.popup(host, prefix, panel.inset(rule.popupPadding), owner, popup.event); });
}

}  // namespace ui
