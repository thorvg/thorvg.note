#include "find_bar.h"
#include <algorithm>

namespace tn
{

bool FindBar::focused() const
{
    return open && (context.focused() == "find/query" || (replace && context.focused() == "find/replacement"));
}

bool FindBar::found(const DocumentTab& tab) const
{
    if (!open || query.value().empty()) return false;
    if (document == tab.id && revision == tab.document.revision() && searchedQuery == query.value()) return !matches.empty();
    return tab.document.text().find(query.value()) != std::string::npos;
}

void FindBar::show(DocumentTab& tab, bool replacing)
{
    auto selected = tab.editor.session().selectedText();
    if (!selected.empty() && selected.find('\n') == std::string::npos) {
        query.value(std::move(selected));
    }
    open = true;
    replace = replacing;
    context.focus("find/query");
}

void FindBar::toggleReplace()
{
    replace = !replace;
    if (!replace && context.focused() == "find/replacement") context.focus("find/query");
}

void FindBar::update(DocumentTab* visible)
{
    auto tab = open ? visible : nullptr;
    if (tab && tab->loading) tab = nullptr;
    auto id = tab ? tab->id : 0;
    if (document != id) {
        if (auto previous = workspace.find(document)) {
            previous->editor.clearDecorations("find/matches");
            previous->editor.clearDecorations("find/current");
        }
        document = id;
        revision = uint64_t(-1);
        matches.clear();
        current.reset();
    }
    if (!tab) return;
    auto latest = tab->document.revision();
    bool queryChanged = searchedQuery != query.value();
    bool rebuild = queryChanged || revision != latest;
    if (rebuild) {
        searchedQuery = query.value();
        revision = latest;
        matches.clear();
        if (!query.value().empty()) {
            const auto& text = tab->document.text();
            for (size_t offset = 0; (offset = text.find(query.value(), offset)) != std::string::npos;) {
                auto end = offset + query.value().size();
                if (tab->document.clamp(offset) == offset && tab->document.clamp(end) == end) matches.push_back({offset, end});
                offset = end;
            }
        }
        tab->editor.decorations("find/matches", latest, matches);
        if (queryChanged && !matches.empty() && !tab->editor.composing()) {
            auto offset = tab->editor.session().selection().start();
            auto match = std::lower_bound(matches.begin(), matches.end(), offset, [](const auto& range, size_t value) { return range.start < value; });
            if (match == matches.end()) match = matches.begin();
            tab->editor.select(match->start, match->end);
        }
    }
    auto selected = tab->editor.session().selection();
    auto match = std::lower_bound(matches.begin(), matches.end(), selected.start(), [](const auto& range, size_t value) { return range.start < value; });
    std::optional<size_t> selectedMatch;
    if (match != matches.end() && match->start == selected.start() && match->end == selected.end()) selectedMatch = size_t(match - matches.begin());
    if (rebuild || selectedMatch != current) {
        current = selectedMatch;
        if (current) {
            ui::TextDecoration active{match->start, match->end, {.token = &ui::Theme::highlightActive}, {.token = &ui::Theme::highlightActive}};
            tab->editor.decorations("find/current", latest, std::span(&active, 1));
        } else tab->editor.clearDecorations("find/current");
    }
}

void FindBar::step(DocumentTab* tab, int direction)
{
    update(tab);
    if (!open || !tab || tab->loading || tab->editor.composing() || matches.empty()) return;
    auto selected = tab->editor.session().selection();
    auto offset = direction > 0 ? selected.end() : selected.start();
    auto match = std::lower_bound(matches.begin(), matches.end(), offset, [](const auto& range, size_t value) { return range.start < value; });
    if (direction < 0) {
        if (match == matches.begin()) match = matches.end();
        --match;
    } else if (match == matches.end()) match = matches.begin();
    tab->editor.select(match->start, match->end);
    update(tab);
}

void FindBar::substitute(DocumentTab* tab, bool all)
{
    update(tab);
    if (!open || !tab || tab->loading || tab->editor.options().readOnly || tab->editor.composing() || matches.empty()) return;
    if (!all && !current) {
        step(tab, 0);
        return;
    }
    tn::EditBatch batch;
    batch.revision = revision;
    if (all) {
        batch.edits.reserve(matches.size());
        for (const auto& range : matches)
            batch.edits.push_back({range.start, range.end, replacement.value()});
    } else {
        const auto& range = matches[*current];
        batch.edits.push_back({range.start, range.end, replacement.value()});
    }
    std::string error;
    // A search action must not join a preceding typing or extension edit group.
    tab->editor.endUndoGroup();
    if (!tab->editor.applyEdits(batch, error)) {
        workspace.error = std::move(error);
        update(tab);
        return;
    }
    auto end = batch.edits.front().start;
    // The edit path normalizes CRLF/CR; compute the caret from the resulting document.
    for (size_t i = 0; i < replacement.value().size(); ++i) {
        if (replacement.value()[i] == '\r' && i + 1 < replacement.value().size() && replacement.value()[i + 1] == '\n') ++i;
        ++end;
    }
    tab->editor.select(end, end);
    update(tab);
    if (!all) step(tab, 0);
}

void FindBar::draw(ui::Rect& bounds, DocumentTab& tab, Hint& hint)
{
    queryBounds = replacementBounds = {};
    if (!open) return;
    update(&tab);
    const auto& theme = context.theme();
    float height = std::min(bounds.h, replace ? 84.0f : 44.0f);
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
        if (state.hovered && entry) hint = {entry->title, rect};
    };
    action("edit.replace.toggle", "", replace ? "minus" : "plus", {x, y, 28, 30}, replace);
    x += 32;
    const char* navigation[]{"edit.find.previous", "edit.find.next", "edit.find.close"};
    const char* icons[]{"chevron-left", "chevron-right", "close"};
    float right = panel.x + panel.w - 8;
    float actionsWidth = std::size(navigation) * 30.0f;
    float countWidth = 60;
    queryBounds = {x, y, std::max(1.0f, right - actionsWidth - countWidth - x - 8), 30};
    ui::textField(context, "find/query", queryBounds, query, "Find", true, {}, "search");
    auto count = std::to_string(current ? *current + 1 : 0) + " / " + std::to_string(matches.size());
    context.text({right - actionsWidth - countWidth, y, countWidth, 30}, count, !query.value().empty() && matches.empty() ? theme.danger : theme.muted, 11, false, ui::Align::Center);
    for (size_t i = 0; i < std::size(navigation); ++i)
        action(navigation[i], "", icons[i], {right - actionsWidth + i * 30, y, 28, 30});
    if (!replace) return;
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
    replacementBounds = {x, y, std::max(1.0f, right - replacementsWidth - x - 8), 30};
    ui::textField(context, "find/replacement", replacementBounds, replacement, "Replace with");
    x = right - replacementsWidth;
    for (const auto& entry : replacements) {
        action(entry.command, entry.label, "", {x, y, entry.width, 30});
        x += entry.width + 4;
    }
}

const char* FindBar::command(Key key, bool shift) const
{
    if (!open) return nullptr;
    if (key == Key::Escape) return "edit.find.close";
    bool enter = key == Key::Enter || key == Key::KeypadEnter;
    if (key != Key::F3 && !(enter && focused())) return nullptr;
    if (enter && context.focused() == "find/replacement") return "edit.replace.current";
    return shift ? "edit.find.previous" : "edit.find.next";
}

ui::Rect FindBar::caret() const
{
    auto r = context.focused() == "find/query" ? queryBounds : replacementBounds;
    return {r.x + 8, r.y, 1, r.h};
}

}  // namespace tn
