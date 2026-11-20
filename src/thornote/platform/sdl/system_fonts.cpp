#include <thornote/platform/system_fonts.h>
#include <thornote/preferences/font_catalog.h>
#ifdef __EMSCRIPTEN__
#include <SDL3/SDL.h>
#include <emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE bool thornoteLoadHanFonts()
{
    auto regular = ui::loadFont("/assets/NotoSansSC-Regular.ttf");
    auto bold = ui::loadFont("/assets/NotoSansSC-Bold.ttf");
    if (!regular || !bold) return false;
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_EXPOSED;
    SDL_PushEvent(&event);
    return true;
}
#endif

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

ui::FontId SystemFonts::fontFor(ui::FontId preferred, const std::vector<uint32_t>& codepoints)
{
#ifdef __EMSCRIPTEN__
    if (std::any_of(codepoints.begin(), codepoints.end(), [](auto cp) { return cp >= 0x4e00 && cp <= 0x9fff; })) {
        EM_ASM({ if (Module['onMissingHan']) Module['onMissingHan'](); });
    }
#else
    (void)codepoints;
#endif
    return preferred;
}

}  // namespace tn
