#include "input.h"
#include <algorithm>
#include <cmath>
#include <cctype>

namespace tn
{
static_assert(uint32_t(Key::Backspace) == SDLK_BACKSPACE && uint32_t(Key::Tab) == SDLK_TAB && uint32_t(Key::Enter) == SDLK_RETURN && uint32_t(Key::Escape) == SDLK_ESCAPE && uint32_t(Key::Space) == SDLK_SPACE && uint32_t(Key::Delete) == SDLK_DELETE);
static_assert(uint32_t(Key::F1) == SDLK_F1 && uint32_t(Key::F3) == SDLK_F3 && uint32_t(Key::Home) == SDLK_HOME && uint32_t(Key::PageUp) == SDLK_PAGEUP && uint32_t(Key::End) == SDLK_END && uint32_t(Key::PageDown) == SDLK_PAGEDOWN);
static_assert(uint32_t(Key::Right) == SDLK_RIGHT && uint32_t(Key::Left) == SDLK_LEFT && uint32_t(Key::Down) == SDLK_DOWN && uint32_t(Key::Up) == SDLK_UP);
static_assert(uint32_t(Key::KeypadMinus) == SDLK_KP_MINUS && uint32_t(Key::KeypadPlus) == SDLK_KP_PLUS && uint32_t(Key::KeypadEnter) == SDLK_KP_ENTER);
static_assert(uint8_t(Button::Left) == SDL_BUTTON_LEFT && uint8_t(Button::Middle) == SDL_BUTTON_MIDDLE && uint8_t(Button::Right) == SDL_BUTTON_RIGHT);

namespace
{
ui::platform::KeyModifiers modifiers(SDL_Keymod mods)
{
    return {bool(mods & SDL_KMOD_SHIFT), bool(mods & SDL_KMOD_CTRL), bool(mods & SDL_KMOD_ALT), bool(mods & SDL_KMOD_GUI)};
}
}  // namespace

KeyEvent keyEvent(const SDL_KeyboardEvent& event)
{
    KeyEvent result{.key = Key(event.key), .scancode = uint32_t(event.scancode), .modifiers = modifiers(event.mod), .altGr = bool(event.mod & SDL_KMOD_MODE), .repeat = event.repeat, .time = event.timestamp ? SDL_NS_TO_MS(event.timestamp) : SDL_GetTicks()};
    auto key = event.scancode ? SDL_GetKeyFromScancode(event.scancode, event.mod, false) : event.key;
    if (!key) key = event.key;
    switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: result.name = "enter"; break;
        case SDLK_ESCAPE: result.name = "escape"; break;
        case SDLK_SPACE: result.name = " "; break;
        default:
            result.name = SDL_GetKeyName(key);
            std::transform(result.name.begin(), result.name.end(), result.name.begin(), [](unsigned char c) { return c < 128 ? char(std::tolower(c)) : char(c); });
            break;
    }
    return result;
}

WheelEvent wheelEvent(const SDL_MouseWheelEvent& event, float sx, float sy, float wheelPixels)
{
    float direction = event.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
    if (wheelPixels > 0) direction *= wheelPixels;
    return {event.x * direction, event.y * direction, event.mouse_x * sx, event.mouse_y * sy, modifiers(SDL_GetModState()), wheelPixels > 0};
}

WheelEvent pixelWheel(float x, float y, float sx, float sy)
{
    float pointerX = 0, pointerY = 0;
    SDL_GetMouseState(&pointerX, &pointerY);
    return {x, y, pointerX * sx, pointerY * sy, modifiers(SDL_GetModState()), true};
}

bool send(App& app, const SDL_Event& event, float sx, float sy)
{
    switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: app.close(); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: app.focus(false); break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: app.focus(true); break;
        case SDL_EVENT_KEY_DOWN: app.keyDown(keyEvent(event.key)); break;
        case SDL_EVENT_KEY_UP: return app.keyUp(keyEvent(event.key));
        case SDL_EVENT_TEXT_INPUT: app.text(event.text.text ? event.text.text : ""); break;
        case SDL_EVENT_TEXT_EDITING: {
            std::string_view text = event.edit.text ? event.edit.text : "";
            // SDL places the composition cursor in characters.
            size_t offset = 0;
            for (auto characters = event.edit.start; characters-- > 0 && offset < text.size();) {
                ++offset;
                while (offset < text.size() && (uint8_t(text[offset]) & 0xc0) == 0x80)
                    ++offset;
            }
            app.compose(text, offset);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: return app.pointerMove(event.motion.x * sx, event.motion.y * sy);
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            PointerEvent pointer{event.button.x * sx, event.button.y * sy, Button(event.button.button), event.button.clicks, modifiers(SDL_GetModState())};
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) app.pointerDown(pointer);
            else app.pointerUp(pointer);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
#ifdef __EMSCRIPTEN__
            app.wheel(wheelEvent(event.wheel, sx, sy, ui::wheelStepPixels));
#else
            app.wheel(wheelEvent(event.wheel, sx, sy, 0));
#endif
            break;
        default: break;
    }
    return true;
}
void WheelResampler::add(const WheelEvent& event, double seconds)
{
    auto gap = seconds - previous;
    auto duration = gap > 0.1 ? 1.0 / 120 : std::max(gap * 2, 1.0 / 240);
    previous = seconds;
    latest = event;
    pending.push_back({event.x, event.y, seconds, duration, 0, false});
}

void WheelResampler::ease(const WheelEvent& event, double seconds)
{
    latest = event;
    pending.push_back({event.x, event.y, seconds, 0.12, 0, true});
}

std::optional<WheelEvent> WheelResampler::take(double seconds)
{
    float x = 0, y = 0;
    for (auto& share : pending) {
        auto due = std::clamp((seconds - share.start) / share.duration, 0.0, 1.0);
        if (share.eased) due = 1 - std::pow(1 - due, 3);
        x += share.x * float(due - share.taken);
        y += share.y * float(due - share.taken);
        share.taken = due;
    }
    std::erase_if(pending, [](const auto& share) { return share.taken >= 1; });
    if (x == 0 && y == 0) return std::nullopt;
    auto result = latest;
    result.x = x;
    result.y = y;
    return result;
}
bool WheelNotches::notch(float delta, double seconds)
{
    auto current = seconds - previous;
    previous = seconds;
    bool result = std::abs(delta) >= 10 && current >= 0.018 && gap >= 0.018;
    gap = current;
    return result;
}
}  // namespace tn
