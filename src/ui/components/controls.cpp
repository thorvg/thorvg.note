#include <ui/ui.h>
#include <algorithm>

namespace ui
{

State button(Context& context, std::string_view id, Rect bounds, std::string_view label, std::function<void()> activate, const ButtonOptions& options)
{
    const auto& [style, icon, enabled, active, appearance] = options;
    auto state = context.control(id, bounds, [activate = std::move(activate)](const Event& event) { if (event.activated() && activate) activate(); }, enabled);
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
    auto state = context.control(id, bounds, [value, change = std::move(change)](const Event& event) { if (event.activated()) change(!value); }, enabled);
    const auto& t = context.theme();
    Rect box{bounds.x, bounds.y + (bounds.h - 18) / 2, 18, 18};
    context.rect(box, value && enabled ? t.accent : t.surface, 4, state.hovered ? t.accent : t.border);
    if (value) context.icon(std::string(id) + "/check", box, "check", enabled ? t.accentText : t.muted,
                            {.hovered = state.hovered, .pressed = state.pressed, .active = value, .enabled = enabled, .focused = state.focused});
    context.text({bounds.x + 28, bounds.y, bounds.w - 28, bounds.h}, label, enabled ? t.text : t.muted);
    if (state.focused) context.focusRing(box);
}

TextInput::TextInput(std::string_view text, size_t cursor)
{
    value(text);
    session.select(std::min(cursor, text.size()), std::min(cursor, text.size()));
}

TextInput::TextInput(const TextInput& other) :
    document(other.document)
{
    auto selection = other.session.selection();
    session.select(selection.anchor, selection.caret);
}

TextInput& TextInput::operator=(const TextInput& other)
{
    if (this == &other) return *this;
    document = other.document;
    auto selection = other.session.selection();
    session.select(selection.anchor, selection.caret);
    preedit.clear();
    return *this;
}

void TextInput::value(std::string_view text)
{
    if (!document.text(text)) return;
    session.select(document.text().size(), document.text().size());
    preedit.clear();
}

void TextInput::insert(std::string_view text)
{
    preedit.clear();
    if (std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
        std::string filtered;
        for (auto c : text)
            if (static_cast<unsigned char>(c) >= 32 && c != 127) filtered += c;
        session.insert(filtered);
    } else session.insert(text);
}

void TextInput::composition(std::string_view text, size_t cursor)
{
    preedit = text;
    preeditCursor = std::min(cursor, preedit.size());
    while (preeditCursor && preeditCursor < preedit.size() && (static_cast<unsigned char>(preedit[preeditCursor]) & 0xc0) == 0x80)
        --preeditCursor;
}

void TextInput::edit(Key key, Modifiers mods)
{
    if (!preedit.empty()) {
        if (key == Key::Escape) preedit.clear();
        return;
    }
    if (mods.primary && key == Key::A) {
        session.selectAll();
        return;
    }
    if (mods.primary && key == Key::Z) {
        if (mods.shift) session.redo();
        else session.undo();
        return;
    }
    if (mods.primary && key == Key::Y) {
        session.redo();
        return;
    }
    switch (key) {
        case Key::Left: session.move(mods.word ? tn::Move::WordLeft : tn::Move::Left, mods.shift); break;
        case Key::Right: session.move(mods.word ? tn::Move::WordRight : tn::Move::Right, mods.shift); break;
        case Key::Home: session.move(tn::Move::Start, mods.shift); break;
        case Key::End: session.move(tn::Move::End, mods.shift); break;
        case Key::Backspace: session.backspace(mods.word); break;
        case Key::Delete: session.deleteForward(mods.word); break;
        default: break;
    }
}

void textField(Context& context, std::string_view id, Rect bounds, TextInput& input, std::string_view placeholder, bool enabled, std::function<bool(Key)> keyHandler, std::string_view icon, const Appearance& appearance)
{
    float inset = icon.empty() ? 8 : 38;
    Rect content{bounds.x + inset, bounds.y + 2, std::max(0.0f, bounds.w - inset - 12), bounds.h - 4};
    auto selection = input.session.selection();
    auto width = [&](size_t offset) {
        return context.textWidth(std::string_view(input.value()).substr(0, offset), appearance.fontSize, appearance.bold, input.value());
    };
    float offset = context.focused() == id ? std::max(0.0f, width(selection.caret) - content.w + 4) : 0;
    auto state = context.control(id, bounds, [&input, &context, content, offset, appearance, keyHandler = std::move(keyHandler)](const Event& event) {
        if (event.type == EventType::Text) input.insert(event.text);
        else if (event.type == EventType::Press || event.type == EventType::Drag) {
            input.preedit.clear();
            auto& text = input.value();
            auto x = event.x - content.x + offset;
            size_t caret = 0;
            float previous = 0;
            while (caret < text.size()) {
                auto next = input.document.next(caret);
                float advance = context.textWidth(std::string_view(text).substr(0, next), appearance.fontSize, appearance.bold, text);
                if (x < (previous + advance) / 2) break;
                previous = advance;
                caret = next;
            }
            if (event.type == EventType::Press && event.clicks >= 2) input.session.selectAll();
            else input.session.select(event.type == EventType::Drag || event.modifiers.shift ? input.session.selection().anchor : caret, caret);
        } else if (event.type == EventType::Key && (event.modifiers.primary || input.preedit.empty()) &&
                   (!keyHandler || event.modifiers.primary || !keyHandler(event.key))) input.edit(event.key, event.modifiers); }, enabled, ControlFlag::Focusable | ControlFlag::Capture, Cursor::Text, &input);
    const auto& t = context.theme();
    context.rect(bounds, resolve(appearance.fill, t, t.background), appearance.radius >= 0 ? appearance.radius : t.radius,
                 resolve(appearance.border, t, state.focused ? t.accent : t.border), appearance.borderWidth);
    if (!icon.empty()) context.icon(std::string(id) + "/leading", {bounds.x + 8, bounds.y, 22, bounds.h}, icon, t.muted,
                                    {.hovered = state.hovered, .pressed = state.pressed, .enabled = enabled, .focused = state.focused});
    std::string composed;
    std::string_view display = input.value();
    size_t caret = selection.caret;
    if (!input.preedit.empty()) {
        composed = input.value();
        composed.replace(selection.start(), selection.end() - selection.start(), input.preedit);
        display = composed;
        caret = selection.start() + input.preeditCursor;
    }
    auto measure = [&](size_t position) {
        return context.textWidth(display.substr(0, position), appearance.fontSize, appearance.bold, display);
    };
    float cursorWidth = measure(caret);
    offset = state.focused ? std::max(0.0f, cursorWidth - content.w + 4) : 0;
    input.caretBounds = {content.x + cursorWidth - offset, bounds.y + 9, 1.5f, std::max(1.0f, bounds.h - 18)};
    context.clip(content, [&] {
        if (state.focused && !selection.empty() && input.preedit.empty()) {
            float left = measure(selection.start()), right = measure(selection.end());
            context.rect({content.x + left - offset, content.y, right - left, content.h}, t.selection);
        }
        context.text(content, display.empty() ? placeholder : display, enabled && !display.empty() ? resolve(appearance.text, t, t.text) : t.muted,
                     appearance.fontSize, appearance.bold, Align::Start, offset);
        if (state.focused && !input.preedit.empty()) {
            float left = measure(selection.start()), right = measure(selection.start() + input.preedit.size());
            context.rect({content.x + left - offset, bounds.y + bounds.h - 6, right - left, 1}, t.accent);
        }
        if (state.focused) context.rect(input.caretBounds, t.accent);
    });
}

}  // namespace ui
