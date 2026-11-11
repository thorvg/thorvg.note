#include <ui/ui.h>
#include <algorithm>
#include <cmath>

namespace ui
{

void list(Context& context, std::string_view id, Rect bounds, const std::vector<ListItem>& items, std::string_view selected, ListState& state, std::function<void(std::string)> select, float rowHeight, std::function<void(std::string, Rect)> contextMenu)
{
    const auto& t = context.theme();
    if (rowHeight <= 0) rowHeight = t.rowHeight;
    auto limit = std::max(0.0f, items.size() * rowHeight - bounds.h);
    auto prefix = std::string(id);
    context.scroll(prefix + "/scroll", bounds, state.scroll, limit, {.step = rowHeight});
    auto first = size_t(state.scroll / rowHeight);
    auto end = std::min(items.size(), size_t(std::ceil((state.scroll + bounds.h) / rowHeight)));
    context.clip(bounds, [&] {
        for (size_t i = first; i < end; ++i) {
            const auto& item = items[i];
            Rect row{bounds.x, bounds.y + i * rowHeight - state.scroll, bounds.w, rowHeight};
            // Neighbors are resolved when a key arrives, so rows outside the viewport need no control.
            auto interaction = context.control(prefix + "/" + item.id, row, [&context, &items, &state, i, prefix, select, contextMenu, rowHeight, height = bounds.h](const Event& event) {
                if (i >= items.size()) return;
                bool key = event.type == EventType::Key;
                if (event.type == EventType::ContextMenu && contextMenu) contextMenu(items[i].id, {event.x, event.y, 1, 1});
                else if (event.type == EventType::Activate || (key && (event.key == Key::Enter || event.key == Key::Space))) select(items[i].id);
                else if (key && (event.key == Key::Up || event.key == Key::Down)) {
                    auto step = event.key == Key::Up ? -1 : 1;
                    for (auto next = ptrdiff_t(i) + step; next >= 0 && next < ptrdiff_t(items.size()); next += step) {
                        if (!items[next].enabled) continue;
                        state.scroll = std::max(std::min(state.scroll, next * rowHeight), (next + 1) * rowHeight - height);
                        auto target = items[next].id;
                        context.focus(prefix + "/" + target);
                        select(std::move(target));
                        break;
                    }
                } }, item.enabled);
            if (item.id == selected || interaction.hovered) context.rect(row.inset(2), item.id == selected ? t.selection : t.raised, 4);
            float left = row.x + 6 + item.depth * 14;
            context.icon(prefix + "/" + item.id + "/icon", {left, row.y, 24, row.h}, item.icon, item.enabled ? t.muted : t.border,
                         {interaction.hovered, interaction.pressed, item.id == selected, item.enabled, interaction.focused});
            auto detailWidth = item.detail.empty() ? 0.0f : std::min(row.w / 3, context.textWidth(item.detail, 10) + 12);
            context.text({left + 28, row.y, std::max(0.0f, row.x + row.w - left - 36 - detailWidth), row.h}, item.title, item.enabled ? t.text : t.muted, 12);
            if (detailWidth > 0) context.text({row.x + row.w - detailWidth - 8, row.y, detailWidth, row.h}, item.detail, t.muted, 10, false, Align::End);
            if (interaction.focused) context.focusRing(row.inset(4));
        }
    });
}

}  // namespace ui
