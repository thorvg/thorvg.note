#pragma once

#include <thornote/platform/window_controls.h>
#include <storage/storage.h>
#include <text/text.h>
#include <ui/ui.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tn
{
// A printable key carries its character, such as Key('a'); other keys carry a named code.
enum class Key : uint32_t
{
    Unknown = 0,
    Backspace = '\b',
    Tab = '\t',
    Enter = '\r',
    Escape = 27,
    Space = ' ',
    Delete = 127,
    F1 = (1u << 30) | 58,
    F3 = (1u << 30) | 60,
    Home = (1u << 30) | 74,
    PageUp = (1u << 30) | 75,
    End = (1u << 30) | 77,
    PageDown = (1u << 30) | 78,
    Right = (1u << 30) | 79,
    Left = (1u << 30) | 80,
    Down = (1u << 30) | 81,
    Up = (1u << 30) | 82,
    KeypadMinus = (1u << 30) | 86,
    KeypadPlus = (1u << 30) | 87,
    KeypadEnter = (1u << 30) | 88
};

struct KeyEvent
{
    Key key = Key::Unknown;
    // USB HID usage of the physical key, for shortcuts that follow key positions rather than characters.
    uint32_t scancode = 0;
    ui::platform::KeyModifiers modifiers{};
    bool altGr = false, repeat = false;
    uint64_t time = 0;  // Milliseconds.
    // Lowercase name of the key under the current modifiers, such as "a", "enter" or " ".
    std::string name{};
};

enum class Button : uint8_t
{
    Left = 1,
    Middle,
    Right
};

struct PointerEvent
{
    float x = 0, y = 0;
    Button button = Button::Left;
    int clicks = 1;
    ui::platform::KeyModifiers modifiers{};
};

struct WheelEvent
{
    // Steps to the right and away from the user; flipped scrolling is already reversed.
    float x = 0, y = 0;
    float pointerX = 0, pointerY = 0;
    ui::platform::KeyModifiers modifiers{};
};

// An edit from the platform input method, in UTF-8 byte offsets of the target document.
struct NativeTextEdit
{
    uint64_t document = 0;
    size_t begin = 0, end = 0, cursor = 0;
    std::string text;
    bool composition = false;
};

// Where typed text goes: the caret area, and the document a platform input method may edit.
struct TextInputState
{
    ui::Rect area;
    uint64_t id = 0;
    const Document* document = nullptr;
    size_t anchor = 0, caret = 0;
};

struct WindowState
{
    bool customTitleBar = false, maximized = false, fullscreen = false;
};

// The application side of the shell. Positions are logical pixels.
class App
{
public:
    virtual ~App() = default;

    // Applies finished background work; returns whether the frame may change.
    virtual bool update() = 0;
    // Milliseconds until update() is due without input.
    virtual int delay() const = 0;
    virtual void render(float width, float height, double seconds) = 0;
    virtual tvg::Scene* scene() const = 0;
    // The ui::frameTime() moment the content asked to be drawn again.
    virtual std::optional<double> nextFrame() const = 0;
    virtual bool done() const = 0;
    // Whether documents are still loading or saving.
    virtual bool busy() const = 0;

    virtual void window(const WindowState&) = 0;
    virtual std::string title() const = 0;
    virtual ui::Cursor cursor() const = 0;
    // Whether a press at the point moves the window instead of reaching the content.
    virtual bool draggable(float x, float y) const = 0;
    virtual std::optional<TextInputState> textInput() = 0;
    virtual void error(std::string message) = 0;

    // Any input may change the frame, except where keyUp() and pointerMove() return false.
    // keyDown() returns whether the key is consumed before the platform interprets it as text.
    virtual bool keyDown(const KeyEvent&) = 0;
    virtual bool keyUp(const KeyEvent&) = 0;
    virtual void text(std::string_view committed) = 0;
    // Shows uncommitted input method text; the cursor is a byte offset into it.
    virtual void compose(std::string_view text, size_t cursor) = 0;
    virtual void edit(const NativeTextEdit&) = 0;
    virtual bool pointerMove(float x, float y) = 0;
    virtual void pointerDown(const PointerEvent&) = 0;
    virtual void pointerUp(const PointerEvent&) = 0;
    virtual void wheel(const WheelEvent&) = 0;
    virtual void focus(bool focused) = 0;
    // The user asked to close the window.
    virtual void close() = 0;

    // Services the shell provides while it runs.
    // Shows a file dialog and later reports the chosen paths, or an error, on the main thread.
    // A relative save path starts in the documents folder.
    std::function<void(bool save, const storage::Path& initial, std::function<void(const std::vector<std::string>& paths, const std::string& error)> done)> chooseFile;
    std::function<void(const storage::Path& initial, std::function<void(const std::vector<std::string>& paths, const std::string& error)> done)> chooseFolder;
    std::function<void(WindowAction)> changeWindow;
    // Shows the platform text menu; false leaves the menu to the app.
    std::function<bool(float x, float y, std::string_view selection, const ui::CommandRegistry&)> showTextMenu;
};

struct RunOptions
{
    float width = 1080, height = 720;
    // Renders a few frames, writes the last one as a PPM image, and exits.
    std::string snapshot;
};

// Opens the window and runs the app until it is done, returning the exit status.
// The browser keeps running the app after this returns.
int run(std::unique_ptr<App>, const RunOptions& = {});

// Asks the running shell to call App::update(); callable from any thread at any time.
void wake();
}  // namespace tn
