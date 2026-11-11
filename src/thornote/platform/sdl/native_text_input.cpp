#include <thornote/platform/native_text_input.h>

namespace tn
{
struct NativeTextInput::Impl
{
};

NativeTextInput::NativeTextInput() = default;
NativeTextInput::~NativeTextInput() = default;
bool NativeTextInput::initialize(SDL_Window*, std::function<void(const NativeTextEdit&)>, std::function<void(const SDL_KeyboardEvent&)>, std::function<bool(const SDL_KeyboardEvent&)>)
{
    return false;
}
void NativeTextInput::stop() {}
void NativeTextInput::update(uint64_t, const tn::Document*, size_t, size_t, const SDL_Rect*) {}
bool NativeTextInput::event(const SDL_Event&)
{
    return false;
}
bool NativeTextInput::contextMenu(float, float, std::string_view, const ui::CommandRegistry&)
{
    return false;
}
}  // namespace tn
