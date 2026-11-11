#include "clipboard.h"
#include <SDL3/SDL.h>
#include <memory>
#include <stdexcept>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace tn
{
std::string readClipboardText()
{
    SDL_ClearError();
    std::unique_ptr<char, decltype(&SDL_free)> text(SDL_GetClipboardText(), SDL_free);
    if (!text || (!*text && *SDL_GetError())) throw std::runtime_error(SDL_GetError());
    return text.get();
}

void writeClipboardText(std::string_view text)
{
    if (text.find('\0') != std::string_view::npos) throw std::invalid_argument("clipboard text contains NUL");
    std::string value(text);
    if (!SDL_SetClipboardText(value.c_str())) throw std::runtime_error(SDL_GetError());
#ifdef __EMSCRIPTEN__
    // SDL keeps the text to itself in the browser. A copy made from a menu reaches the system clipboard this way, where the browser allows it.
    EM_ASM({ if (navigator.clipboard) navigator.clipboard.writeText(UTF8ToString($0)).catch(function() {}); }, value.c_str());
#endif
}
}  // namespace tn
