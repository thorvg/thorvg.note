#include <ui/ui.h>
#include <algorithm>

namespace ui
{

namespace
{
bool activated(const Event& event)
{
    return event.type == EventType::Activate || (event.type == EventType::Key && (event.key == Key::Enter || event.key == Key::Space));
}

size_t previous(const std::string& value, size_t cursor)
{
    if (!cursor) return 0;
    --cursor;
    while (cursor && (uint8_t(value[cursor]) & 0xc0) == 0x80)
        --cursor;
    return cursor;
}

size_t next(const std::string& value, size_t cursor)
{
    if (cursor == value.size()) return cursor;
    ++cursor;
    while (cursor < value.size() && (uint8_t(value[cursor]) & 0xc0) == 0x80)
        ++cursor;
    return cursor;
}
}  // namespace

State button(Context& context, std::string_view id, Rect bounds, std::string_view label, std::function<void()> activate, const ButtonOptions& options)
{
    const auto& [style, icon, enabled, active, appearance] = options;
    auto state = context.control(id, bounds, [activate = std::move(activate)](const Event& event) { if (activated(event) && activate) activate(); }, enabled);
    const auto& t = context.theme();
    auto fill = style == ButtonStyle::Primary ? t.accent : style == ButtonStyle::Quiet ? t.surface
                                                                                       : t.raised;
    auto ink = style == ButtonStyle::Primary ? t.accentText : t.text;
    fill = resolve(appearance.fill, t, fill);
    ink = resolve(appearance.text, t, ink);
    if (active && style != ButtonStyle::Primary) fill = resolve(appearance.activeFill, t, t.selection);
    if (state.hovered || state.pressed) fill = resolve(appearance.hoverFill, t, style == ButtonStyle::Primary ? t.success : t.selection);
    if (state.pressed) fill = resolve(appearance.pressedFill, t, fill);
    if (!enabled) ink = t.muted;
    if (!enabled) fill = resolve(appearance.disabledFill, t, t.surface);
    context.rect(bounds, fill, appearance.radius >= 0 ? appearance.radius : t.radius,
                 resolve(appearance.border, t, {0, 0, 0, 0}), appearance.borderWidth);
    if (state.focused && appearance.focusRing) context.focusRing(bounds.inset(4));
    if (!icon.empty()) {
        context.icon(std::string(id) + "/icon", {bounds.x + (label.empty() ? (bounds.w - 24) / 2 : 8), bounds.y, 24, bounds.h}, icon, ink,
                     {.hovered = state.hovered, .pressed = state.pressed, .active = active, .enabled = enabled, .focused = state.focused});
        if (!label.empty()) context.text({bounds.x + 36, bounds.y, bounds.w - 44, bounds.h}, label, ink, appearance.fontSize, appearance.bold, appearance.textAlign.value_or(Align::Start));
    } else context.text(bounds.inset(6), label, ink, appearance.fontSize, appearance.bold, appearance.textAlign.value_or(Align::Center));
    return state;
}

void checkbox(Context& context, std::string_view id, Rect bounds, std::string_view label, bool value, std::function<void(bool)> change, bool enabled)
{
    auto state = context.control(id, bounds, [value, change = std::move(change)](const Event& event) { if (activated(event)) change(!value); }, enabled);
    const auto& t = context.theme();
    Rect box{bounds.x, bounds.y + (bounds.h - 18) / 2, 18, 18};
    context.rect(box, value && enabled ? t.accent : t.surface, 4, state.hovered ? t.accent : t.border);
    if (value) context.icon(std::string(id) + "/check", box, "check", enabled ? t.accentText : t.muted,
                            {.hovered = state.hovered, .pressed = state.pressed, .active = value, .enabled = enabled, .focused = state.focused});
    context.text({bounds.x + 28, bounds.y, bounds.w - 28, bounds.h}, label, enabled ? t.text : t.muted);
    if (state.focused) context.focusRing(box);
}

void TextInput::insert(std::string_view text)
{
    cursor = std::min(cursor, value.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (uint8_t(text[i]) < 32 || uint8_t(text[i]) == 127) continue;
        value.insert(cursor++, 1, text[i]);
    }
}

void TextInput::edit(Key key)
{
    cursor = std::min(cursor, value.size());
    switch (key) {
        case Key::Left: cursor = previous(value, cursor); break;
        case Key::Right: cursor = next(value, cursor); break;
        case Key::Home: cursor = 0; break;
        case Key::End: cursor = value.size(); break;
        case Key::Backspace: {
            auto start = previous(value, cursor);
            value.erase(start, cursor - start);
            cursor = start;
            break;
        }
        case Key::Delete: value.erase(cursor, next(value, cursor) - cursor); break;
        default: break;
    }
}

void textField(Context& context, std::string_view id, Rect bounds, TextInput& input, std::string_view placeholder, bool enabled, std::function<bool(Key)> keyHandler, std::string_view icon, const Appearance& appearance)
{
    auto state = context.control(id, bounds, [&input, keyHandler = std::move(keyHandler)](const Event& event) {
        if (event.type == EventType::Text) input.insert(event.text);
        else if (event.type == EventType::Press) input.cursor = input.value.size();
        else if (event.type == EventType::Key && (!keyHandler || !keyHandler(event.key))) input.edit(event.key); }, enabled);
    const auto& t = context.theme();
    context.rect(bounds, resolve(appearance.fill, t, t.background), appearance.radius >= 0 ? appearance.radius : t.radius,
                 resolve(appearance.border, t, state.focused ? t.accent : t.border), appearance.borderWidth);
    if (!icon.empty()) context.icon(std::string(id) + "/leading", {bounds.x + 8, bounds.y, 22, bounds.h}, icon, t.muted,
                                    {.hovered = state.hovered, .pressed = state.pressed, .enabled = enabled, .focused = state.focused});
    float inset = icon.empty() ? 8 : 38;
    Rect content{bounds.x + inset, bounds.y + 2, bounds.w - inset - 12, bounds.h - 4};
    float cursorWidth = context.textWidth(std::string_view(input.value).substr(0, std::min(input.cursor, input.value.size())), appearance.fontSize, appearance.bold, input.value);
    float offset = state.focused ? std::max(0.0f, cursorWidth - content.w + 4) : 0;
    context.text(content, input.value.empty() ? placeholder : std::string_view(input.value), enabled && !input.value.empty() ? resolve(appearance.text, t, t.text) : t.muted,
                 appearance.fontSize, appearance.bold, Align::Start, offset);
    if (state.focused) context.rect({content.x + cursorWidth - offset, bounds.y + 9, 1.5f, bounds.h - 18}, t.accent);
}

}  // namespace ui
