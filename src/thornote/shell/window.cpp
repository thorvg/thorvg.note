#include "shell.h"
#include "input.h"
#include "cursors.h"
#include "renderer.h"
#include <thornote/app/app_identity.h>
#include <thornote/platform/native_text_input.h>
#include <thornote/platform/native_window.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/html5.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#include <thornote/platform/emscripten/browser_files.h>

extern "C" EMSCRIPTEN_KEEPALIVE void thornoteWindowExposed(SDL_WindowID window)
{
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_EXPOSED;
    event.window.windowID = window;
    SDL_PushEvent(&event);
}
#endif

namespace tn
{
namespace
{
using FileChosen = std::function<void(const std::vector<std::string>&, const std::string&)>;
struct DialogResult
{
    FileChosen done;
    std::vector<std::string> paths;
    std::string error;
};
struct Mailbox
{
    std::mutex mutex;
    std::deque<DialogResult> results;
    uint32_t event = 0;  // Registered while the shell runs.
    // Requires the lock.
    void post() const
    {
        if (!event) return;
        SDL_Event value{};
        value.type = event;
        SDL_PushEvent(&value);
    }
};
// Never released: file dialogs and background work can report after the shell stops.
Mailbox& mailbox()
{
    static auto value = new Mailbox;
    return *value;
}
struct DialogRequest
{
    FileChosen done;
    std::string initial;
};
void SDLCALL dialogCallback(void* data, const char* const* paths, int)
{
    std::unique_ptr<DialogRequest> request(static_cast<DialogRequest*>(data));
    DialogResult result{std::move(request->done), {}, {}};
    if (!paths) result.error = std::string("File dialog failed: ") + SDL_GetError();
    else
        for (auto current = paths; *current; ++current)
            result.paths.emplace_back(*current);
    auto& box = mailbox();
    std::lock_guard<std::mutex> lock(box.mutex);
    if (!box.event) return;
    box.results.push_back(std::move(result));
    box.post();
}

class Window
{
public:
    ~Window()
    {
        SDL_RemoveEventWatch(exposeWatch, this);
        titleBarInput.stop();
        nativeInput.stop();
        cursors.clear();
        {
            auto& box = mailbox();
            std::lock_guard<std::mutex> lock(box.mutex);
            box.event = 0;
            box.results.clear();
        }
        // Paints the app releases still need the engine, which the canvas keeps until the renderer stops.
        app.reset();
        renderer.stop();
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    bool initialize(std::unique_ptr<App> value, const RunOptions& options)
    {
        app = std::move(value);
        if (!initializeAppIdentity() || !SDL_Init(SDL_INIT_VIDEO)) return false;
        window = renderer.createWindow("ThorNote", 1080, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
        if (!window) return false;
        snapshotMode = !options.snapshot.empty();
#ifndef __EMSCRIPTEN__
        if (!applyAppIcon(window)) {
            std::cerr << "ThorNote: cannot set app icon: " << SDL_GetError() << '\n';
            SDL_ClearError();
        }
#endif
        nativeInputReady = nativeInput.initialize(window, [this](const auto& edit) { app->edit(edit); updateTextInput(); }, [this](const auto& key) { SDL_Event event{}; event.key = key; send(*app, event, inputX, inputY); updateTextInput(); }, [this](const auto& key) {
#ifdef __EMSCRIPTEN__
            // The browser hands over clipboard shortcuts at once; the input queued before them comes first.
            for (SDL_Event queued; SDL_PollEvent(&queued);)
                dispatch(queued);
#endif
            bool handled = app->keyDown(keyEvent(key));
            updateTextInput();
            return handled; });
        {
            auto& box = mailbox();
            std::lock_guard<std::mutex> lock(box.mutex);
            box.event = SDL_RegisterEvents(1);
            if (!box.event) return false;
        }
        auto display = SDL_GetWindowDisplayScale(window), density = SDL_GetWindowPixelDensity(window);
        auto scale = display > 0 && density > 0 ? display / density : 1;
        SDL_SetWindowSize(window, std::max(1, int(options.width * scale)), std::max(1, int(options.height * scale)));
        app->showTextMenu = [this](float x, float y, std::string_view selection, const ui::CommandRegistry& commands) {
            if (!nativeInputReady) return false;
            SDL_CaptureMouse(false);
            if (!draw()) return false;  // Present the word selected by right-click before modal menu tracking.
            return nativeInput.contextMenu(x / inputX, y / inputY, selection, commands);
        };
        app->changeWindow = [this](WindowAction action) {
            pendingWindowAction = action;
            wake();
        };
        app->chooseFile = [this](bool save, const storage::Path& initial, FileChosen done) {
            showFileDialog(save, initial, std::move(done));
        };
#ifndef __EMSCRIPTEN__
        app->chooseFolder = [this](const storage::Path& initial, FileChosen done) {
            auto request = new DialogRequest{std::move(done), utf8(initial)};
            SDL_ShowOpenFolderDialog(dialogCallback, request, window, request->initial.empty() ? nullptr : request->initial.c_str(), false);
        };
#endif
        custom = nativeWindowChromeSupported(window) && SDL_SetWindowHitTest(window, hitTest, this) && titleBarInput.initialize(window, [this](float x, float y) { return custom && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) && app->draggable(x * inputX, y * inputY); }, [this] { app->changeWindow(WindowAction::TitleBarDoubleClick); });
        if (custom) custom = updateNativeWindowChrome(window, false);
        if (!custom) {
            titleBarInput.stop();
            SDL_SetWindowHitTest(window, nullptr, nullptr);
            if (!updateNativeWindowChrome(window, true)) return false;
            SDL_ClearError();
        }
        // Apply the window size before creating the first frame.
        if (!SDL_SyncWindow(window)) return false;
        if (!SDL_AddEventWatch(exposeWatch, this)) return false;
#ifdef __EMSCRIPTEN__
        // A page resize clears the canvas even when the window keeps its size.
        EM_ASM({ window.addEventListener("resize", function() { _thornoteWindowExposed($0); }); }, SDL_GetWindowID(window));
#endif
        if (!draw()) return false;
        std::cout << "ThorNote: render engine " << renderer.engine() << "; native UI + TextEditor\n";
        if (options.snapshot.empty()) SDL_ShowWindow(window);
        return true;
    }

    bool drawable() const
    {
        if (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) return false;
        int width = 0, height = 0;
        return SDL_GetWindowSizeInPixels(window, &width, &height) && width > 0 && height > 0;
    }

    // One pass of the loop; returns the exit status once the app is done.
    std::optional<int> step(const RunOptions& options)
    {
        if (app->done()) return 0;

        // 1. Finished work: background loads, saves and file dialogs. These can replace controls,
        //    so rebuild them before the queued input.
        bool dirty = app->update();
        std::deque<DialogResult> results;
        {
            auto& box = mailbox();
            std::lock_guard<std::mutex> lock(box.mutex);
            results.swap(box.results);
        }
        for (const auto& result : results) {
            result.done(result.paths, result.error);
            dirty = true;
        }
        if (dirty && !draw()) return 1;

        // 2. Input. Rebuild without presenting between events so callbacks and input bounds reflect
        //    the latest state; wheel input can share a frame.
        dirty = false;
        bool stale = false;
        for (SDL_Event event; SDL_PollEvent(&event);) {
            if (stale && event.type != SDL_EVENT_MOUSE_WHEEL) {
                if (!draw(false)) return 1;
                stale = false;
            }
            if (dispatch(event)) dirty = stale = true;
        }

        // 3. Animation.
        if (auto next = app->nextFrame(); next && drawable()) dirty |= *next <= ui::frameTime();

        // 4. Present.
        if ((dirty || snapshotMode) && !draw()) return 1;
        if (snapshotMode && !app->busy() && ++passes >= 3) return renderer.snapshot(options.snapshot) ? 0 : 1;
        return std::nullopt;
    }

    // Milliseconds until the next step is due without input.
    int delay(const RunOptions& options) const
    {
        if (!options.snapshot.empty()) return 1;
        auto delay = app->delay();
        if (auto next = app->nextFrame(); next && drawable()) delay = std::min(delay, int(std::ceil(std::max(0.0, *next - ui::frameTime()) * 1000)));
        return delay;
    }

private:
    void showFileDialog(bool save, const storage::Path& initial, FileChosen done)
    {
        auto request = new DialogRequest{std::move(done), utf8(initial)};
        if (save && !initial.empty() && initial.is_relative()) {
            if (auto folder = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) request->initial = utf8(utf8Path(folder) / initial);
        }
#ifdef __EMSCRIPTEN__
        if (save) {
            auto path = browserSavePath(request->initial);
            const char* paths[] = {path.empty() ? nullptr : path.c_str(), nullptr};
            dialogCallback(request, paths, 0);
        } else {
            browserOpenFiles([request](const char* const* paths, const char* error) {
                if (error) SDL_SetError("%s", error);
                dialogCallback(request, error ? nullptr : paths, 0);
            });
        }
#else
        if (save) SDL_ShowSaveFileDialog(dialogCallback, request, window, nullptr, 0, request->initial.c_str());
        else SDL_ShowOpenFileDialog(dialogCallback, request, window, nullptr, 0, request->initial.empty() ? nullptr : request->initial.c_str(), true);
#endif
    }

    // Returns whether the event may change the frame. Input always may; a window notification can
    // describe geometry already presented by the expose watch or an earlier event.
    bool dispatch(SDL_Event& event)
    {
        if ((event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) SDL_CaptureMouse(false);
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) renderer.invalidate();
        bool handled = titleBarInput.event(event);
        if (!handled && nativeInputReady) handled = nativeInput.event(event);
        if (!handled) {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) SDL_CaptureMouse(true);
            if (!send(*app, event, inputX, inputY)) return false;
        }
        switch (event.type) {
            case SDL_EVENT_WINDOW_EXPOSED:
                return event.window.windowID != SDL_GetWindowID(window) || !event.common.timestamp || event.common.timestamp > exposedAt;
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
                return event.window.windowID != SDL_GetWindowID(window) || presented != geometry();
            default: return true;
        }
    }

    static SDL_HitTestResult SDLCALL hitTest(SDL_Window* window, const SDL_Point* point, void* data)
    {
        auto& self = *static_cast<Window*>(data);
        if (!self.custom || (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) return SDL_HITTEST_NORMAL;
        return self.titleBarInput.hitTest(nativeWindowHitTest(window, *point, self.app->draggable(point->x * self.inputX, point->y * self.inputY)));
    }

    // Native move and resize loops hold the main loop and deliver expose events before SDL_PollEvent
    // returns, so draw from here, on the main thread.
    static bool SDLCALL exposeWatch(void* data, SDL_Event* event)
    {
        if (SDL_IsMainThread() && event->type == SDL_EVENT_WINDOW_EXPOSED) {
            auto& self = *static_cast<Window*>(data);
            if (event->window.windowID != SDL_GetWindowID(self.window) || self.drawing) return true;
            self.renderer.invalidate();
            if (self.draw()) self.exposedAt = event->common.timestamp;
        }
        return true;
    }

    // What a presented frame depends on besides the app.
    struct Geometry
    {
        int windowWidth = 0, windowHeight = 0, pixelWidth = 0, pixelHeight = 0;
        float scale = 0;
        SDL_WindowFlags flags = 0;
        SDL_Rect safeArea{};
        bool operator==(const Geometry& other) const
        {
            return windowWidth == other.windowWidth && windowHeight == other.windowHeight && pixelWidth == other.pixelWidth && pixelHeight == other.pixelHeight && scale == other.scale && flags == other.flags && safeArea.x == other.safeArea.x && safeArea.y == other.safeArea.y && safeArea.w == other.safeArea.w && safeArea.h == other.safeArea.h;
        }
    };

    Geometry geometry() const
    {
        Geometry value;
        SDL_GetWindowSize(window, &value.windowWidth, &value.windowHeight);
        SDL_GetWindowSizeInPixels(window, &value.pixelWidth, &value.pixelHeight);
        value.scale = SDL_GetWindowDisplayScale(window);
        value.flags = SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_HIDDEN);
        SDL_GetWindowSafeArea(window, &value.safeArea);
        return value;
    }

    // Every frame goes through here: a draw started inside another one is skipped, and after a failed
    // draw none runs. A draw that does not present keeps the presented geometry.
    bool draw(bool present = true)
    {
        if (failed) return false;
        if (drawing) return true;
        drawing = true;
        auto current = geometry();
        bool result = drawFrame(present);
        drawing = false;
        failed |= !result;
        if (result && present) presented = current;
        return result;
    }

    // Without presenting, only updates the controls and the input state.
    bool drawFrame(bool present)
    {
        if (pendingWindowAction) {
            auto action = *pendingWindowAction;
            pendingWindowAction.reset();
            if (!performWindowAction(window, action)) app->error(std::string("Cannot change window state: ") + SDL_GetError());
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) return true;
        auto flags = SDL_GetWindowFlags(window);
        auto scale = SDL_GetWindowDisplayScale(window);
        if (scale <= 0) scale = 1;
        app->window({custom, bool(flags & SDL_WINDOW_MAXIMIZED), bool(flags & SDL_WINDOW_FULLSCREEN), scale});
        if (!updateNativeWindowChrome(window, !custom)) return false;
        titleBarInput.update(custom, flags & SDL_WINDOW_INPUT_FOCUS);
        int ww, wh, width, height;
        if (!SDL_GetWindowSize(window, &ww, &wh) || !SDL_GetWindowSizeInPixels(window, &width, &height)) return false;
        if (ww <= 0 || wh <= 0 || width <= 0 || height <= 0) return true;
        inputX = float(width) / ww / scale;
        inputY = float(height) / wh / scale;
        app->render(width / scale, height / scale, ui::frameTime());
        if (present) {
            ui::transform(app->scene(), {scale, 0, 0, 0, scale, 0, 0, 0, 1});
            if (!renderer.draw(app->scene(), width, height)) return false;
        }
        SDL_SetWindowTitle(window, app->title().c_str());
        updateTextInput();
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (SDL_GetMouseFocus() == window) {
            auto hit = custom ? nativeWindowHitTest(window, {int(mx), int(my)}, false) : SDL_HITTEST_NORMAL;
            if (hit < SDL_HITTEST_RESIZE_TOPLEFT) cursors.set(app->cursor());
        }
        return !present || snapshotMode || renderer.present();
    }

    void updateTextInput()
    {
        auto input = app->textInput();
        if (!input) {
            nativeInput.update(0, nullptr, 0, 0, nullptr);
            SDL_StopTextInput(window);
            return;
        }
        const auto& area = input->area;
        SDL_Rect rect{int(area.x / inputX), int(area.y / inputY), 1, std::max(1, int(std::ceil(area.h / inputY)))};
        if (nativeInputReady && input->document) nativeInput.update(input->id, input->document, input->anchor, input->caret, &rect);
        else {
            nativeInput.update(0, nullptr, 0, 0, nullptr);
            SDL_SetTextInputArea(window, &rect, 0);
            SDL_StartTextInput(window);
        }
    }

    SDL_Window* window = nullptr;
    Renderer renderer;
    bool snapshotMode = false, custom = false;
    tn::Cursors cursors;
    NativeTextInput nativeInput;
    NativeTitleBarInput titleBarInput;
    bool nativeInputReady = false;
    std::unique_ptr<App> app;
    std::optional<WindowAction> pendingWindowAction;
    float inputX = 1, inputY = 1;
    size_t passes = 0;
    std::optional<Geometry> presented;
    Uint64 exposedAt = 0;
    bool drawing = false, failed = false;
};

#ifdef __EMSCRIPTEN__
// The browser runs each pass after main returns, so the window and its options are never released.
struct Session
{
    RunOptions options;
    Window window;
    int frame = 0, timer = 0;
};

void step(void* data);

// A step due within a frame waits for the frame, which also takes the input queued until then. A hidden page draws no frames.
void schedule(Session& session, int delay)
{
    // Context::requestAnimationFrame() asks for 1/60 s, which rounds up to 17 ms.
    if (delay <= 17 && session.window.drawable()) {
        if (!session.frame) session.frame = emscripten_request_animation_frame([](double, void* data) { step(data); return false; }, &session);
    } else if (!session.timer) session.timer = emscripten_set_timeout(step, delay, &session);
}

bool SDLCALL watch(void* data, SDL_Event*)
{
    schedule(*static_cast<Session*>(data), 0);
    return true;
}

// Like the native loop, waits for input or until the next step is due.
void step(void* data)
{
    auto& session = *static_cast<Session*>(data);
    if (session.frame) emscripten_cancel_animation_frame(session.frame);
    if (session.timer) emscripten_clear_timeout(session.timer);
    session.frame = session.timer = 0;
    if (session.window.step(session.options)) SDL_RemoveEventWatch(watch, data);
    else schedule(session, session.window.delay(session.options));
}
#endif
}  // namespace

void wake()
{
#ifdef __EMSCRIPTEN_PTHREADS__
    // Only the page thread runs SDL and schedules the steps, so a worker passes the wake to it.
    if (!emscripten_is_main_runtime_thread()) {
        emscripten_proxy_async(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), [](void*) { wake(); }, nullptr);
        return;
    }
#endif
    auto& box = mailbox();
    std::lock_guard<std::mutex> lock(box.mutex);
    box.post();
}

int run(std::unique_ptr<App> app, const RunOptions& options)
{
#ifdef __EMSCRIPTEN__
    auto session = new Session{options, {}};
    if (!session->window.initialize(std::move(app), session->options)) {
        std::cerr << "ThorNote: initialization failed: " << SDL_GetError() << '\n';
        EM_ASM({ if (Module['onAbort']) Module['onAbort'](UTF8ToString($0)); }, SDL_GetError());
        delete session;
        return 1;
    }
    SDL_AddEventWatch(watch, session);
    schedule(*session, 0);
    return 0;
#else
    Window window;
    if (!window.initialize(std::move(app), options)) {
        std::cerr << "ThorNote: initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }
    // Step, then sleep until input, wake() or the next deadline.
    for (;;) {
        if (auto status = window.step(options)) return *status;
        SDL_WaitEventTimeout(nullptr, window.delay(options));
    }
#endif
}

}  // namespace tn
