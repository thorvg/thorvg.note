#include "editor_app.h"
#include <algorithm>

namespace tn
{

bool EditorApp::findFocused() const
{
    return find.open && !settings.active && (context.focused() == "find/query" || (find.replace && context.focused() == "find/replacement"));
}

void EditorApp::openFind(bool replace)
{
    auto tab = target();
    if (!tab || tab->loading || tab->editor.composing()) return;
    configure(*tab);
    auto selected = tab->editor.session().selectedText();
    if (!selected.empty() && selected.find('\n') == std::string::npos) {
        find.query.value(std::move(selected));
    }
    find.open = true;
    find.replace = replace;
    focusEditor(false);
    context.focus("find/query");
    findMatch(0);
}

void EditorApp::closeFind()
{
    find.open = false;
    updateFind();
    focusEditor(true);
}

void EditorApp::updateFind()
{
    auto tab = find.open ? visibleDocument() : nullptr;
    if (tab && tab->loading) tab = nullptr;
    auto id = tab ? tab->id : 0;
    if (find.document != id) {
        if (auto previous = workspace.find(find.document)) {
            previous->editor.clearDecorations("find/matches");
            previous->editor.clearDecorations("find/current");
        }
        find.document = id;
        find.revision = uint64_t(-1);
        find.matches.clear();
        find.current.reset();
    }
    if (!tab) return;
    auto revision = tab->document.revision();
    bool queryChanged = find.searchedQuery != find.query.value();
    bool rebuild = queryChanged || find.revision != revision;
    if (rebuild) {
        find.searchedQuery = find.query.value();
        find.revision = revision;
        find.matches.clear();
        if (!find.query.value().empty()) {
            const auto& text = tab->document.text();
            for (size_t offset = 0; (offset = text.find(find.query.value(), offset)) != std::string::npos;) {
                auto end = offset + find.query.value().size();
                if (tab->document.clamp(offset) == offset && tab->document.clamp(end) == end) find.matches.push_back({offset, end});
                offset = end;
            }
        }
        tab->editor.decorations("find/matches", revision, find.matches);
        if (queryChanged && !find.matches.empty() && !tab->editor.composing()) {
            auto offset = tab->editor.session().selection().start();
            auto match = std::lower_bound(find.matches.begin(), find.matches.end(), offset, [](const auto& range, size_t value) { return range.start < value; });
            if (match == find.matches.end()) match = find.matches.begin();
            tab->editor.select(match->start, match->end);
        }
    }
    auto selected = tab->editor.session().selection();
    auto match = std::lower_bound(find.matches.begin(), find.matches.end(), selected.start(), [](const auto& range, size_t value) { return range.start < value; });
    std::optional<size_t> current;
    if (match != find.matches.end() && match->start == selected.start() && match->end == selected.end()) current = size_t(match - find.matches.begin());
    if (rebuild || current != find.current) {
        find.current = current;
        if (current) {
            ui::TextDecoration active{match->start, match->end, {.token = &ui::Theme::highlightActive}, {.token = &ui::Theme::highlightActive}};
            tab->editor.decorations("find/current", revision, std::span(&active, 1));
        } else tab->editor.clearDecorations("find/current");
    }
}

void EditorApp::findMatch(int direction)
{
    updateFind();
    auto tab = visibleDocument();
    if (!find.open || !tab || tab->loading || tab->editor.composing() || find.matches.empty()) return;
    auto selected = tab->editor.session().selection();
    auto offset = direction > 0 ? selected.end() : selected.start();
    auto match = std::lower_bound(find.matches.begin(), find.matches.end(), offset, [](const auto& range, size_t value) { return range.start < value; });
    if (direction < 0) {
        if (match == find.matches.begin()) match = find.matches.end();
        --match;
    } else if (match == find.matches.end()) match = find.matches.begin();
    tab->editor.select(match->start, match->end);
    updateFind();
}

void EditorApp::replaceMatch(bool all)
{
    updateFind();
    auto tab = visibleDocument();
    if (!find.open || !tab || tab->loading || tab->editor.options().readOnly || tab->editor.composing() || find.matches.empty()) return;
    if (!all && !find.current) {
        findMatch(0);
        return;
    }
    tn::EditBatch batch;
    batch.revision = find.revision;
    if (all) {
        batch.edits.reserve(find.matches.size());
        for (const auto& range : find.matches)
            batch.edits.push_back({range.start, range.end, find.replacement.value()});
    } else {
        const auto& range = find.matches[*find.current];
        batch.edits.push_back({range.start, range.end, find.replacement.value()});
    }
    std::string error;
    // A search action must not join a preceding typing or extension edit group.
    tab->editor.endUndoGroup();
    if (!tab->editor.applyEdits(batch, error)) {
        workspace.error = std::move(error);
        updateFind();
        return;
    }
    auto end = batch.edits.front().start;
    // The edit path normalizes CRLF/CR; compute the caret from the resulting document.
    for (size_t i = 0; i < find.replacement.value().size(); ++i) {
        if (find.replacement.value()[i] == '\r' && i + 1 < find.replacement.value().size() && find.replacement.value()[i + 1] == '\n') ++i;
        ++end;
    }
    tab->editor.select(end, end);
    updateFind();
    if (!all) findMatch(0);
}

void EditorApp::drawFind(ui::Rect& bounds)
{
    find.queryBounds = find.replacementBounds = {};
    if (!find.open) return;
    updateFind();
    float height = std::min(bounds.h, find.replace ? 84.0f : 44.0f);
    ui::Rect panel{bounds.x, bounds.y, bounds.w, height};
    context.rect(panel, theme.surface);
    context.line(panel.x, panel.y + panel.h, panel.x + panel.w, panel.y + panel.h, theme.border);
    bounds.y += height;
    bounds.h = std::max(1.0f, bounds.h - height);
    float x = panel.x + 8, y = panel.y + 6;
    auto action = [&](const char* command, const char* label, const char* icon, ui::Rect rect, bool active = false) {
        auto entry = commands.find(command);
        auto state = ui::button(context, command, rect, label, [this, command] { commands.execute(command); },
                                {.style = ui::ButtonStyle::Quiet, .icon = icon, .enabled = entry && entry->enabled(), .active = active});
        if (state.hovered && entry) {
            hint = entry->title;
            hintBounds = rect;
        }
    };
    action("edit.replace.toggle", "", find.replace ? "minus" : "plus", {x, y, 28, 30}, find.replace);
    x += 32;
    const char* navigation[]{"edit.find.previous", "edit.find.next", "edit.find.close"};
    const char* icons[]{"chevron-left", "chevron-right", "close"};
    float right = panel.x + panel.w - 8;
    float actionsWidth = std::size(navigation) * 30.0f;
    float countWidth = 60;
    find.queryBounds = {x, y, std::max(1.0f, right - actionsWidth - countWidth - x - 8), 30};
    ui::textField(context, "find/query", find.queryBounds, find.query, "Find", true, {}, "search");
    auto count = std::to_string(find.current ? *find.current + 1 : 0) + " / " + std::to_string(find.matches.size());
    context.text({right - actionsWidth - countWidth, y, countWidth, 30}, count, !find.query.value().empty() && find.matches.empty() ? theme.danger : theme.muted, 11, false, ui::Align::Center);
    for (size_t i = 0; i < std::size(navigation); ++i)
        action(navigation[i], "", icons[i], {right - actionsWidth + i * 30, y, 28, 30});
    if (!find.replace) return;
    y += 40;
    struct Action
    {
        const char* command;
        const char* label;
        float width;
    };
    const Action replacements[]{{"edit.replace.current", "Replace", 70}, {"edit.replace.all", "Replace All", 90}};
    float replacementsWidth = 0;
    for (const auto& entry : replacements)
        replacementsWidth += entry.width + 4;
    find.replacementBounds = {x, y, std::max(1.0f, right - replacementsWidth - x - 8), 30};
    ui::textField(context, "find/replacement", find.replacementBounds, find.replacement, "Replace with");
    x = right - replacementsWidth;
    for (const auto& entry : replacements) {
        action(entry.command, entry.label, "", {x, y, entry.width, 30});
        x += entry.width + 4;
    }
}

}  // namespace tn
