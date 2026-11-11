#include <thornote/platform/native_text_input.h>
#include <ui/ui.h>
#include <emscripten.h>
#include <deque>

extern "C" {
// Implemented in text_input.js. Attaching fails unless the selector names a text field.
bool thornoteTextInputAttach(const char* selector);
void thornoteTextInputPlace(int x, int y, int height);
}

namespace tn
{
struct NativeTextInput::Impl
{
    SDL_Window* window = nullptr;
    SDL_Rect area{};
    std::function<bool(const SDL_KeyboardEvent&)> keyDown;
    // The browser reports text between frames; queued events borrow these strings until the app has handled them.
    std::deque<std::string> texts;
    size_t handled = 0;
    std::string copied;
    uint32_t wake = 0;

    // Runs a clipboard shortcut at once: the browser only exposes its clipboard while its own event is dispatched.
    void press(SDL_Scancode scancode, SDL_Keycode key)
    {
        SDL_KeyboardEvent event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.scancode = scancode;
        event.key = key;
        event.mod = SDL_GetModState() | (ui::platform::modifiers({false, true, false, false}).primary ? SDL_KMOD_LCTRL : SDL_KMOD_LGUI);
        event.down = true;
        keyDown(event);
        // The app draws on its next pass over the event queue.
        SDL_Event redraw{};
        redraw.type = wake;
        SDL_PushEvent(&redraw);
    }
};

namespace
{
NativeTextInput::Impl* input = nullptr;
}  // namespace

extern "C" {
EMSCRIPTEN_KEEPALIVE void thornoteTextEdit(const char* text, int cursor)
{
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_EDITING;
    event.edit.text = input->texts.emplace_back(text).c_str();
    event.edit.start = cursor;
    SDL_PushEvent(&event);
}

EMSCRIPTEN_KEEPALIVE void thornoteTextCommit(const char* text, int)
{
    SDL_Event event{};
    if (text[0] == '\n' && !text[1]) {
        // A line break that ended a composition is the Enter key the input method consumed.
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.scancode = SDL_SCANCODE_RETURN;
        event.key.key = SDLK_RETURN;
        event.key.down = true;
    } else {
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = input->texts.emplace_back(text).c_str();
    }
    SDL_PushEvent(&event);
}

// Returns what the app copied or cut, or null when it had nothing to offer.
EMSCRIPTEN_KEEPALIVE const char* thornoteClipboardCopy(int cut)
{
    SDL_ClearClipboardData();
    input->press(cut ? SDL_SCANCODE_X : SDL_SCANCODE_C, cut ? SDLK_X : SDLK_C);
    auto text = SDL_GetClipboardText();
    input->copied = text ? text : "";
    SDL_free(text);
    return input->copied.empty() ? nullptr : input->copied.c_str();
}

EMSCRIPTEN_KEEPALIVE void thornoteClipboardPaste(const char* text, int)
{
    SDL_SetClipboardText(text);
    input->press(SDL_SCANCODE_V, SDLK_V);
}
}

NativeTextInput::NativeTextInput() :
    impl(std::make_unique<Impl>()) {}
NativeTextInput::~NativeTextInput() = default;

bool NativeTextInput::initialize(SDL_Window* window, std::function<void(const NativeTextEdit&)>, std::function<void(const SDL_KeyboardEvent&)>, std::function<bool(const SDL_KeyboardEvent&)> beforeKey)
{
    impl->window = window;
    impl->keyDown = std::move(beforeKey);
    impl->wake = SDL_RegisterEvents(1);
    input = impl.get();
    // Composition and the clipboard need a text field: the page names one as the keyboard element, otherwise keys arrive as plain presses.
    return thornoteTextInputAttach(SDL_GetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT));
}

void NativeTextInput::stop() {}

void NativeTextInput::update(uint64_t, const tn::Document* text, size_t, size_t, const SDL_Rect* area)
{
    if (!text || !area) return;
    // The field only receives printable keys while SDL expects text.
    SDL_StartTextInput(impl->window);
    if (area->x == impl->area.x && area->y == impl->area.y && area->h == impl->area.h) return;
    impl->area = *area;
    thornoteTextInputPlace(area->x, area->y, area->h);
}

bool NativeTextInput::event(const SDL_Event& event)
{
    for (; impl->handled; --impl->handled)
        impl->texts.pop_front();
    if (impl->texts.empty()) return false;
    auto text = impl->texts.front().c_str();
    if ((event.type == SDL_EVENT_TEXT_EDITING && event.edit.text == text) || (event.type == SDL_EVENT_TEXT_INPUT && event.text.text == text)) impl->handled = 1;
    return false;
}

bool NativeTextInput::contextMenu(float, float, std::string_view, const ui::CommandRegistry&)
{
    return false;
}
}  // namespace tn
