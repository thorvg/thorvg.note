#include <ui/ui.h>

namespace ui::platform
{
Modifiers modifiers(KeyModifiers modifiers)
{
    return {modifiers.shift, modifiers.meta, modifiers.alt};
}

Modifiers navigation(Key& key, KeyModifiers modifiers)
{
    auto result = platform::modifiers(modifiers);
    if (!result.primary) return result;
    if (key == Key::Left || key == Key::Right) {
        key = key == Key::Left ? Key::Home : Key::End;
        result.primary = false;
    } else if (key == Key::Up || key == Key::Down) {
        key = key == Key::Up ? Key::Home : Key::End;
    }
    return result;
}

std::string shortcutLabel(std::string value)
{
    if (value.compare(0, 4, "Mod+") == 0) {
        value.replace(0, 4, "⌘");
        auto shift = value.find("Shift+");
        if (shift != std::string::npos) value.replace(shift, 6, "⇧");
    }
    return value;
}
}  // namespace ui::platform
