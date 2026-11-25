#include "window.h"
#include "app_identity.h"
#include "editor_app.h"
#include <thornote/platform/native_text_input.h>
#include <thornote/platform/native_window.h>
#include "live_resize.h"
#include "cursors.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
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
struct DialogResult
{
    bool save;
    uint64_t target;
    std::vector<std::string> paths;
    std::string error;
};
struct Mailbox
{
    std::mutex mutex;
    std::deque<DialogResult> results;
    uint32_t event = 0;
    bool alive = true;
    void wake()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (alive) {
            SDL_Event value{};
            value.type = event;
            SDL_PushEvent(&value);
        }
    }
};
struct DialogRequest
{
    std::shared_ptr<Mailbox> mailbox;
    bool save;
    uint64_t target;
    std::string initial;
};
void SDLCALL dialogCallback(void* data, const char* const* paths, int)
{
    std::unique_ptr<DialogRequest> request(static_cast<DialogRequest*>(data));
    DialogResult result{request->save, request->target, {}, {}};
    if (!paths) result.error = std::string("File dialog failed: ") + SDL_GetError();
    else
        for (auto current = paths; *current; ++current)
            result.paths.emplace_back(*current);
    auto mailbox = request->mailbox;
    std::lock_guard<std::mutex> lock(mailbox->mutex);
    if (!mailbox->alive) return;
    mailbox->results.push_back(std::move(result));
    SDL_Event event{};
    event.type = mailbox->event;
    SDL_PushEvent(&event);
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
            std::lock_guard<std::mutex> lock(mailbox->mutex);
            mailbox->alive = false;
        }
        canvas.reset();
        app.reset();
        if (gl) SDL_GL_DestroyContext(gl);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    bool initialize(const RunOptions& options)
    {
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
        nativeInputReady = nativeInput.initialize(window, [this](const auto& edit) { app->nativeTextInput(edit); updateTextInput(); }, [this](const auto& key) { SDL_Event event{}; event.key = key; app->event(event, inputX, inputY); updateTextInput(); }, [this](const auto& key) {
#ifdef __EMSCRIPTEN__
            // The browser hands over clipboard shortcuts at once; the input queued before them comes first.
            for (SDL_Event queued; SDL_PollEvent(&queued);)
                dispatch(queued);
#endif
            bool handled = app->nativeKeyDown(key);
            updateTextInput();
            return handled; });
        mailbox->event = SDL_RegisterEvents(1);
        if (mailbox->event == uint32_t(-1)) return false;
        auto display = SDL_GetWindowDisplayScale(window), density = SDL_GetWindowPixelDensity(window);
        auto scale = display > 0 && density > 0 ? display / density : 1;
        SDL_SetWindowSize(window, int(1080 * scale), int(720 * scale));
        storage::Path settingsDirectory;
        if (!options.settingsDirectory.empty()) settingsDirectory = utf8Path(options.settingsDirectory);
        else if (auto path = SDL_GetPrefPath(preferencesOrganization, preferencesAppId)) {
            settingsDirectory = utf8Path(path);
            SDL_free(path);
        }
        app = std::make_unique<EditorApp>([mailbox = mailbox] { mailbox->wake(); }, settingsDirectory);
#ifdef __EMSCRIPTEN__
        app->workspace.persist = [this](auto complete) {
            browserSync([this, complete = std::move(complete)](std::string error) {
                complete(std::move(error));
                mailbox->wake();
            });
        };
        app->exportFile = [this](uint64_t id) {
            if (auto tab = app->workspace.find(id)) browserExport(utf8(app->suggestedSavePath(id, "/notes").filename()), tab->document.text(), tab->format);
        };
#endif
        app->showTextMenu = [this](float x, float y, std::string_view selection, const ui::CommandRegistry& commands) {
            if (!nativeInputReady) return false;
            SDL_CaptureMouse(false);
            if (!draw()) return false;  // Present the word selected by right-click before modal menu tracking.
            return nativeInput.contextMenu(x / inputX, y / inputY, selection, commands);
        };
        app->changeWindow = [this](EditorApp::WindowAction action) {
            pendingWindowAction = action;
            mailbox->wake();
        };
        bool custom = nativeWindowChromeSupported(window) && SDL_SetWindowHitTest(window, hitTest, this) && titleBarInput.initialize(window, [this](float x, float y) { return app->customTitleBarVisible() && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) && app->windowDraggableAt(x * inputX, y * inputY); }, [this] { app->changeWindow(EditorApp::WindowAction::TitleBarDoubleClick); });
        if (custom) custom = updateNativeWindowChrome(window, false);
        if (!custom) {
            titleBarInput.stop();
            SDL_SetWindowHitTest(window, nullptr, nullptr);
            if (!updateNativeWindowChrome(window, true)) return false;
            SDL_ClearError();
        }
        app->setCustomTitleBarSupported(custom);
        if (!options.fresh) {
            if (auto restored = app->restoreSession()) SDL_SetWindowSize(window, std::max(1, int(restored->w * scale)), std::max(1, int(restored->h * scale)));
        }
        if (!options.layoutDirectory.empty()) app->setLayoutDirectory(utf8Path(options.layoutDirectory));
        if (options.layoutSpecified) app->setLayout(options.layout);
        app->chooseFile = [this](bool save, uint64_t target, const storage::Path& directory) {
            auto request = new DialogRequest{mailbox, save, target, {}};
            if (save) {
                auto folder = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
                request->initial = utf8(app->suggestedSavePath(target, folder ? utf8Path(folder) : storage::Path{}));
            } else request->initial = utf8(directory);
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
        };
        for (const auto& path : options.paths)
            app->openPath(utf8Path(path));
        if (options.settings) app->openSettings();
        // Apply restored geometry before creating the first GL frame.
        if (!SDL_SyncWindow(window)) return false;
        if (!liveResize.start(window, [this] { return drawFrame(); })) return false;
#ifdef __EMSCRIPTEN__
        // A page resize clears the canvas even when the window keeps its size.
        EM_ASM({ window.addEventListener("resize", function() { _thornoteWindowExposed($0); }); }, SDL_GetWindowID(window));
#endif
        if (!draw()) return false;
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

    // One pass over finished work and queued events; returns the exit status once the app is done.
    std::optional<int> step(const RunOptions& options, bool wait)
    {
        if (app->done()) return 0;
        bool dirty = app->poll();
        std::deque<DialogResult> results;
        {
            std::lock_guard<std::mutex> lock(mailbox->mutex);
            results.swap(mailbox->results);
        }
        for (const auto& result : results) {
            app->dialogResult(result.save, result.target, result.paths, result.error);
            dirty = true;
        }
        // Reloads and file completions can replace controls; rebuild before queued input.
        if (dirty) {
            if (!draw()) return 1;
            dirty = false;
        }
        SDL_Event event;
        int delay = 0;
        if (!options.snapshot.empty()) delay = 1;
        else if (wait) {
            delay = app->nextPollDelay(SDL_GetTicks());
            if (auto next = app->context.nextFrame(); next && drawable()) delay = std::min(delay, int(std::ceil(std::max(0.0, *next - ui::frameTime()) * 1000)));
        }
        if (SDL_WaitEventTimeout(&event, delay)) {
            // Rebuild between events so callbacks and input bounds reflect the latest state, then draw once.
            bool stale = false;
            do {
#ifdef __EMSCRIPTEN__
                // Wheel input can share a frame.
                if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    dispatch(event);
                    dirty = stale = true;
                    continue;
                }
#endif
                if (stale && !build()) return 1;
                dispatch(event);
                stale = liveResize.needsDraw(event);
                dirty |= stale;
            } while (SDL_PollEvent(&event));
        }
        if (auto next = app->context.nextFrame(); next && drawable()) dirty |= *next <= ui::frameTime();
        if (dirty || !options.snapshot.empty())
            if (!draw()) return 1;
        if (!options.snapshot.empty() && !app->filesPending() && ++passes >= 3) return snapshot(options.snapshot) ? 0 : 1;
        return std::nullopt;
    }

private:
    void dispatch(SDL_Event& event)
    {
        if ((event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) SDL_CaptureMouse(false);
        bool handled = titleBarInput.event(event);
        if (!handled && nativeInputReady) handled = nativeInput.event(event);
        if (!handled) {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) SDL_CaptureMouse(true);
            app->event(event, inputX, inputY);
        }
    }

    static SDL_HitTestResult SDLCALL hitTest(SDL_Window* window, const SDL_Point* point, void* data)
    {
        auto& self = *static_cast<Window*>(data);
        auto flags = SDL_GetWindowFlags(window);
        if (!self.app || self.app->nativeTitleBarVisible() || (flags & SDL_WINDOW_FULLSCREEN)) return SDL_HITTEST_NORMAL;
        return self.titleBarInput.hitTest(nativeWindowHitTest(window, *point, self.app->windowDraggableAt(point->x * self.inputX, point->y * self.inputY)));
    }

    bool draw()
    {
        return liveResize.draw();
    }

    // Updates the controls and the input state without drawing the frame.
    bool build()
    {
        return liveResize.draw([this] { return drawFrame(false); }, false);
    }

    bool drawFrame(bool present = true)
    {
        if (pendingWindowAction) {
            auto action = *pendingWindowAction;
            pendingWindowAction.reset();
            if (!performWindowAction(window, action)) app->workspace.error = std::string("Cannot change window state: ") + SDL_GetError();
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) return true;
        auto flags = SDL_GetWindowFlags(window);
        app->setWindowState(flags & SDL_WINDOW_MAXIMIZED, flags & SDL_WINDOW_FULLSCREEN);
        if (!updateNativeWindowChrome(window, app->nativeTitleBarVisible())) return false;
        titleBarInput.update(app->customTitleBarVisible(), flags & SDL_WINDOW_INPUT_FOCUS);
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
        app->render(width / scale, height / scale);
        if (present) {
            ui::transform(app->context.scene(), {scale, 0, 0, 0, scale, 0, 0, 0, 1});
            if (!attached) {
                if (canvas->add(app->context.scene()) != tvg::Result::Success) return false;
                attached = true;
            }
            if (canvas->update() != tvg::Result::Success || canvas->draw(true) != tvg::Result::Success || canvas->sync() != tvg::Result::Success) return SDL_SetError("Cannot draw the GL frame");
        }
        SDL_SetWindowTitle(window, app->windowTitle().c_str());
        updateTextInput();
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (SDL_GetMouseFocus() == window) {
            auto hit = app->customTitleBarVisible() ? nativeWindowHitTest(window, {int(mx), int(my)}, false) : SDL_HITTEST_NORMAL;
            if (hit < SDL_HITTEST_RESIZE_TOPLEFT) cursors.set(app->context.cursor());
        }
        return !present || snapshotMode || SDL_GL_SwapWindow(window);
    }

    void updateTextInput()
    {
        if (auto area = app->textInputArea()) {
            SDL_Rect rect{int(area->x / inputX), int(area->y / inputY), 1, std::max(1, int(std::ceil(area->h / inputY)))};
            auto tab = app->textInputTarget();
            if (auto input = app->context.input(); nativeInputReady && input) {
                auto selection = input->session.selection();
                nativeInput.update(reinterpret_cast<uintptr_t>(&input->document), &input->document, selection.anchor, selection.caret, &rect);
            } else if (nativeInputReady && tab) {
                auto selection = tab->editor.session().selection();
                nativeInput.update(tab->id, &tab->document, selection.anchor, selection.caret, &rect);
            } else {
                nativeInput.update(0, nullptr, 0, 0, nullptr);
                SDL_SetTextInputArea(window, &rect, 0);
                SDL_StartTextInput(window);
            }
        } else {
            nativeInput.update(0, nullptr, 0, 0, nullptr);
            SDL_StopTextInput(window);
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
    bool snapshotMode = false;
    tn::Cursors cursors;
    NativeTextInput nativeInput;
    NativeTitleBarInput titleBarInput;
    bool nativeInputReady = false;
    std::unique_ptr<tvg::GlCanvas> canvas;
    std::shared_ptr<Mailbox> mailbox = std::make_shared<Mailbox>();
    std::unique_ptr<EditorApp> app;
    std::optional<EditorApp::WindowAction> pendingWindowAction;
    float inputX = 1, inputY = 1;
    bool attached = false;
    size_t passes = 0;
    tn::LiveResize liveResize;
};
}  // namespace

int run(const RunOptions& options)
{
#ifdef __EMSCRIPTEN__
    // The browser runs each pass after main returns, so the window and its options are never released.
    struct Session
    {
        RunOptions options;
        Window window;
    };
    auto session = new Session{options, {}};
    if (!session->window.initialize(session->options)) {
        std::cerr << "ThorNote: initialization failed: " << SDL_GetError() << '\n';
        EM_ASM({ if (Module['onAbort']) Module['onAbort'](UTF8ToString($0)); }, SDL_GetError());
        delete session;
        return 1;
    }
    emscripten_set_main_loop_arg(
        [](void* data) {
            auto& session = *static_cast<Session*>(data);
            if (session.window.step(session.options, false)) emscripten_cancel_main_loop();
        },
        session, 0, false);
    return 0;
#else
    Window window;
    if (!window.initialize(options)) {
        std::cerr << "ThorNote: initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }
    for (;;)
        if (auto status = window.step(options, true)) return *status;
#endif
}

}  // namespace tn
