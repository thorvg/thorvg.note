#pragma once

#include <text/text.h>
#include <SDL3/SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ui
{
class CommandRegistry;
}

namespace tn
{

struct NativeTextEdit
{
    uint64_t document = 0;
    size_t begin = 0, end = 0, cursor = 0;
    std::string text;
    bool composition = false;
};

// Cocoa needs document-relative UTF-16 selection and replacement ranges.
// Keep that platform contract in the app; the editor receives UTF-8 byte ranges.
class NativeTextInput
{
public:
    NativeTextInput();
    ~NativeTextInput();
    // Callbacks run on the main thread during native input dispatch. Apply the
    // operation and update() before returning, so synchronous IME queries see it.
    // beforeKey runs before native text interpretation unless IME preedit is active.
    bool initialize(SDL_Window* window, std::function<void(const NativeTextEdit&)> apply, std::function<void(const SDL_KeyboardEvent&)> key,
                    std::function<bool(const SDL_KeyboardEvent&)> beforeKey = {});
    void stop();
    void update(uint64_t document, const tn::Document* text, size_t anchor, size_t caret, const SDL_Rect* area);
    // Consume SDL copies already handled by native text input or menu tracking.
    bool event(const SDL_Event& event);
    // Show the platform text menu; false lets the host use its portable menu.
    bool contextMenu(float x, float y, std::string_view selection, const ui::CommandRegistry&);

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

}  // namespace tn
