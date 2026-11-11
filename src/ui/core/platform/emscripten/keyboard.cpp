#include <ui/ui.h>
#include <emscripten.h>

namespace ui::platform
{
namespace
{
// The browser reports its host: Command stays the primary modifier on Apple systems.
bool apple()
{
    static bool value = EM_ASM_INT({ return navigator.platform.startsWith("Mac") || navigator.platform.startsWith("iP"); });
    return value;
}
}  // namespace

Modifiers modifiers(KeyModifiers modifiers)
{
    if (apple()) return {modifiers.shift, modifiers.meta, modifiers.alt};
    return {modifiers.shift, modifiers.control, modifiers.control};
}

Modifiers navigation(Key& key, KeyModifiers modifiers)
{
    auto result = platform::modifiers(modifiers);
    if (!apple() || !result.primary) return result;
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
    if (value.compare(0, 4, "Mod+") != 0) return value;
    if (!apple()) return value.replace(0, 4, "Ctrl+");
    value.replace(0, 4, "⌘");
    auto shift = value.find("Shift+");
    if (shift != std::string::npos) value.replace(shift, 6, "⇧");
    return value;
}
}  // namespace ui::platform
