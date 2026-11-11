#include <thornote/platform/system_fonts.h>
#include <thornote/preferences/font_catalog.h>

namespace tn
{

struct SystemFonts::Impl
{
};

SystemFonts::SystemFonts() = default;
SystemFonts::~SystemFonts() = default;
ui::FontId SystemFonts::defaultFont(bool)
{
    return 0;
}

ui::FontId SystemFonts::load(const FontChoice& choice, std::string& error)
{
    auto id = choice.path.empty() ? defaultFont() : ui::loadFont(utf8(choice.path).c_str(), choice.index);
    if (!id) {
        error = "Cannot load this font: " + ui::fontError();
        return 0;
    }
    error.clear();
    return id;
}

ui::FontId SystemFonts::fontFor(ui::FontId preferred, const std::vector<uint32_t>&)
{
    return preferred;
}

}  // namespace tn
