#include "shell.h"
#include "input.h"
#include "live_resize.h"
#include "cursors.h"
#include <thornote/app/app_identity.h>
#include <thornote/platform/native_text_input.h>
#include <thornote/platform/native_window.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <thornote/platform/emscripten/browser_files.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#ifdef __EMSCRIPTEN__
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
        liveResize.stop();
        titleBarInput.stop();
        nativeInput.stop();
        cursors.clear();
        {
            auto& box = mailbox();
            std::lock_guard<std::mutex> lock(box.mutex);
            box.event = 0;
            box.results.clear();
        }
        canvas.reset();
        app.reset();
        if (gl) SDL_GL_DestroyContext(gl);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    bool initialize(std::unique_ptr<App> value, const RunOptions& options)
    {
        app = std::move(value);
        if (!initializeAppIdentity() || !SDL_Init(SDL_INIT_VIDEO)) return false;
#ifdef __EMSCRIPTEN__
        constexpr int profile = SDL_GL_CONTEXT_PROFILE_ES, major = 3, minor = 0;
#elif defined(__APPLE__)
        constexpr int profile = SDL_GL_CONTEXT_PROFILE_CORE, major = 4, minor = 1;
#else
        constexpr int profile = SDL_GL_CONTEXT_PROFILE_CORE, major = 3, minor = 3;
#endif
        if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile) || !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major) || !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor)) return false;
        window = SDL_CreateWindow("ThorNote", 1080, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
        if (!window) return false;
        gl = SDL_GL_CreateContext(window);
        if (!gl) return false;
        canvas.reset(tvg::GlCanvas::gen());
        if (!canvas) return SDL_SetError("ThorVG GL renderer is unavailable");
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
            if (!liveResize.draw()) return false;  // Present the word selected by right-click before modal menu tracking.
            return nativeInput.contextMenu(x / inputX, y / inputY, selection, commands);
        };
        app->changeWindow = [this](WindowAction action) {
            pendingWindowAction = action;
            wake();
        };
        app->chooseFile = [this](bool save, const storage::Path& initial, FileChosen done) {
            showFileDialog(save, initial, std::move(done));
        };
        custom = nativeWindowChromeSupported(window) && SDL_SetWindowHitTest(window, hitTest, this) && titleBarInput.initialize(window, [this](float x, float y) { return custom && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) && app->draggable(x * inputX, y * inputY); }, [this] { app->changeWindow(WindowAction::TitleBarDoubleClick); });
        if (custom) custom = updateNativeWindowChrome(window, false);
        if (!custom) {
            titleBarInput.stop();
            SDL_SetWindowHitTest(window, nullptr, nullptr);
            if (!updateNativeWindowChrome(window, true)) return false;
            SDL_ClearError();
        }
        // Apply the window size before creating the first GL frame.
        if (!SDL_SyncWindow(window)) return false;
        if (!liveResize.start(window, [this](bool present) { return drawFrame(present); })) return false;
#ifdef __EMSCRIPTEN__
        // A page resize clears the canvas even when the window keeps its size.
        EM_ASM({ window.addEventListener("resize", function() { _thornoteWindowExposed($0); }); }, SDL_GetWindowID(window));
#endif
        if (!liveResize.draw()) return false;
        std::cout << "ThorNote: render engine gl; native UI + TextEditor\n";
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
        if (dirty && !liveResize.draw()) return 1;

        // 2. Input. Rebuild without presenting between events so callbacks and input bounds reflect
        //    the latest state; wheel input can share a frame.
        dirty = false;
        bool stale = false;
        for (SDL_Event event; SDL_PollEvent(&event);) {
            if (stale && event.type != SDL_EVENT_MOUSE_WHEEL) {
                if (!liveResize.draw(false)) return 1;
                stale = false;
            }
            if (dispatch(event) && liveResize.needsDraw(event)) dirty = stale = true;
        }

        // 3. Animation.
        if (auto next = app->nextFrame(); next && drawable()) dirty |= *next <= ui::frameTime();

        // 4. Present.
        if ((dirty || snapshotMode) && !liveResize.draw()) return 1;
        if (snapshotMode && !app->busy() && ++passes >= 3) return snapshot(options.snapshot) ? 0 : 1;
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

    // Returns whether the event may change the frame.
    bool dispatch(SDL_Event& event)
    {
        if ((event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) SDL_CaptureMouse(false);
        bool handled = titleBarInput.event(event);
        if (!handled && nativeInputReady) handled = nativeInput.event(event);
        if (!handled) {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) SDL_CaptureMouse(true);
            return send(*app, event, inputX, inputY);
        }
        return true;
    }

    static SDL_HitTestResult SDLCALL hitTest(SDL_Window* window, const SDL_Point* point, void* data)
    {
        auto& self = *static_cast<Window*>(data);
        if (!self.custom || (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) return SDL_HITTEST_NORMAL;
        return self.titleBarInput.hitTest(nativeWindowHitTest(window, *point, self.app->draggable(point->x * self.inputX, point->y * self.inputY)));
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
        app->window({custom, bool(flags & SDL_WINDOW_MAXIMIZED), bool(flags & SDL_WINDOW_FULLSCREEN)});
        if (!updateNativeWindowChrome(window, !custom)) return false;
        titleBarInput.update(custom, flags & SDL_WINDOW_INPUT_FOCUS);
        int ww, wh, width, height;
        if (!SDL_GetWindowSize(window, &ww, &wh) || !SDL_GetWindowSizeInPixels(window, &width, &height)) return false;
        if (ww <= 0 || wh <= 0 || width <= 0 || height <= 0) return true;
        auto scale = SDL_GetWindowDisplayScale(window);
        if (scale <= 0) scale = 1;
        inputX = float(width) / ww / scale;
        inputY = float(height) / wh / scale;
        if (present && (width != targetWidth || height != targetHeight)) {
            if (canvas->target(nullptr, nullptr, gl, 0, width, height, tvg::ColorSpace::ABGR8888S) != tvg::Result::Success) return SDL_SetError("Cannot set the GL render target");
            targetWidth = width;
            targetHeight = height;
        }
        app->render(width / scale, height / scale, ui::frameTime());
        if (present) {
            ui::transform(app->scene(), {scale, 0, 0, 0, scale, 0, 0, 0, 1});
            if (!attached) {
                if (canvas->add(app->scene()) != tvg::Result::Success) return false;
                attached = true;
            }
            if (canvas->update() != tvg::Result::Success || canvas->draw(true) != tvg::Result::Success || canvas->sync() != tvg::Result::Success) return SDL_SetError("Cannot draw the GL frame");
        }
        SDL_SetWindowTitle(window, app->title().c_str());
        updateTextInput();
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (SDL_GetMouseFocus() == window) {
            auto hit = custom ? nativeWindowHitTest(window, {int(mx), int(my)}, false) : SDL_HITTEST_NORMAL;
            if (hit < SDL_HITTEST_RESIZE_TOPLEFT) cursors.set(app->cursor());
        }
        return !present || snapshotMode || SDL_GL_SwapWindow(window);
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

    bool snapshot(const std::string& path)
    {
        if (targetWidth <= 0 || targetHeight <= 0) return false;
#ifdef __EMSCRIPTEN__
        auto readPixels = glReadPixels;
#else
        auto readPixels = reinterpret_cast<decltype(&glReadPixels)>(SDL_GL_GetProcAddress("glReadPixels"));
        if (!readPixels) return false;
#endif
        std::vector<uint8_t> pixels(size_t(targetWidth) * targetHeight * 4);
        readPixels(0, 0, targetWidth, targetHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        std::ofstream output(path, std::ios::binary);
        output << "P6\n"
               << targetWidth << ' ' << targetHeight << "\n255\n";
        for (int y = targetHeight - 1; y >= 0; --y) {
            auto row = pixels.data() + size_t(y) * targetWidth * 4;
            for (int x = 1; x < targetWidth; ++x) {
                row[x * 3] = row[x * 4];
                row[x * 3 + 1] = row[x * 4 + 1];
                row[x * 3 + 2] = row[x * 4 + 2];
            }
            output.write(reinterpret_cast<const char*>(row), size_t(targetWidth) * 3);
        }
        return bool(output);
    }

    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;
    int targetWidth = 0, targetHeight = 0;
    bool snapshotMode = false, custom = false;
    tn::Cursors cursors;
    NativeTextInput nativeInput;
    NativeTitleBarInput titleBarInput;
    bool nativeInputReady = false;
    std::unique_ptr<tvg::GlCanvas> canvas;
    std::unique_ptr<App> app;
    std::optional<WindowAction> pendingWindowAction;
    float inputX = 1, inputY = 1;
    bool attached = false;
    size_t passes = 0;
    tn::LiveResize liveResize;
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
