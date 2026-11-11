#include <ui/ui.h>

namespace ui::platform
{
Modifiers modifiers(KeyModifiers modifiers)
{
    return {modifiers.shift, modifiers.control, modifiers.control};
}

Modifiers navigation(Key&, KeyModifiers modifiers)
{
    return platform::modifiers(modifiers);
}

std::string shortcutLabel(std::string value)
{
    if (value.compare(0, 4, "Mod+") == 0) value.replace(0, 4, "Ctrl+");
    return value;
}
}  // namespace ui::platform
