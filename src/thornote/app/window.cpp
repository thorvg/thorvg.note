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
        SDL_DestroySurface(frame);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    bool initialize(const RunOptions& options)
    {
        if (!initializeAppIdentity() || !SDL_Init(SDL_INIT_VIDEO)) return false;
        window = SDL_CreateWindow("ThorNote", 1080, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
        if (!window) return false;
        if (!applyAppIcon(window)) {
            std::cerr << "ThorNote: cannot set app icon: " << SDL_GetError() << '\n';
            SDL_ClearError();
        }
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
        bool headless = std::string_view(SDL_GetCurrentVideoDriver()) == "dummy";
        bool custom = nativeWindowChromeSupported(window) && SDL_SetWindowHitTest(window, hitTest, this) && titleBarInput.initialize(window, [this](float x, float y) { return app->customTitleBarVisible() && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) && app->windowDraggableAt(x * inputX, y * inputY); }, [this] { app->changeWindow(EditorApp::WindowAction::TitleBarDoubleClick); });
        if (custom) custom = updateNativeWindowChrome(window, false);
        if (!custom) {
            titleBarInput.stop();
            SDL_SetWindowHitTest(window, nullptr, nullptr);
            if (!updateNativeWindowChrome(window, true)) return false;
            SDL_ClearError();
        }
        app->setCustomTitleBarSupported((nativeWindowChromeAvailable() && headless) || custom);
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
            // The WebAssembly port of SDL has no file dialogs.
            SDL_Unsupported();
            dialogCallback(request, nullptr, 0);
#else
            if (save) SDL_ShowSaveFileDialog(dialogCallback, request, window, nullptr, 0, request->initial.c_str());
            else SDL_ShowOpenFileDialog(dialogCallback, request, window, nullptr, 0, request->initial.empty() ? nullptr : request->initial.c_str(), true);
#endif
        };
        for (const auto& path : options.paths)
            app->openPath(utf8Path(path));
        if (options.settings) app->openSettings();
        // Apply restored geometry before creating the first software surface.
        if (!SDL_SyncWindow(window)) return false;
        if (!liveResize.start(window, [this] { return drawFrame(); })) return false;
#ifdef __EMSCRIPTEN__
        // A page resize clears the canvas even when the window keeps its size.
        EM_ASM({ window.addEventListener("resize", function() { _thornoteWindowExposed($0); }); }, SDL_GetWindowID(window));
#endif
        if (!draw()) return false;
        std::cout << "ThorNote: render engine cpu; native UI + TextEditor\n";
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
            do {
                if (!dispatch(event)) return 1;
            } while (SDL_PollEvent(&event));
        }
        if (auto next = app->context.nextFrame(); next && drawable()) dirty |= *next <= ui::frameTime();
        if (dirty || !options.snapshot.empty())
            if (!draw()) return 1;
        if (!options.snapshot.empty() && !app->filesPending() && ++passes >= 3) return snapshot(options.snapshot) ? 0 : 1;
        return std::nullopt;
    }

private:
    // Hands one event to the app; false once the frame it changed cannot be drawn.
    bool dispatch(SDL_Event& event)
    {
        if ((event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) SDL_CaptureMouse(false);
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) presentWhole = true;
        bool handled = titleBarInput.event(event);
        if (!handled && nativeInputReady) handled = nativeInput.event(event);
        if (!handled) {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) SDL_CaptureMouse(true);
            app->event(event, inputX, inputY);
        }
        // Rebuild between events so callbacks and input bounds reflect the latest state.
        return !liveResize.needsDraw(event) || draw();
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

    bool drawFrame()
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
        auto target = SDL_GetWindowSurface(window);
        if (!target) return false;
        titleBarInput.update(app->customTitleBarVisible(), flags & SDL_WINDOW_INPUT_FOCUS);
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window, &ww, &wh);
        if (ww <= 0 || wh <= 0 || target->w <= 0 || target->h <= 0) return true;
        auto scale = SDL_GetWindowDisplayScale(window);
        if (scale <= 0) scale = 1;
        inputX = float(target->w) / ww / scale;
        inputY = float(target->h) / wh / scale;
        canvas->sync();
        bool retarget = !frame || frame->w != target->w || frame->h != target->h;
        if (retarget) {
            SDL_DestroySurface(frame);
            pixels.resize(size_t(target->w) * target->h);
            if (canvas->target(pixels.data(), target->w, target->w, target->h, tvg::ColorSpace::ARGB8888) != tvg::Result::Success) return false;
            frame = SDL_CreateSurfaceFrom(target->w, target->h, SDL_PIXELFORMAT_ARGB8888, pixels.data(), target->w * 4);
            if (!frame) return false;
        }
        app->render(target->w / scale, target->h / scale);
        ui::transform(app->context.scene(), {scale, 0, 0, 0, scale, 0, 0, 0, 1});
        if (!attached) {
            if (canvas->add(app->context.scene()) != tvg::Result::Success) return false;
            attached = true;
        }
        // Unchanged paints are retained, so ThorVG repaints and reports only the regions that changed.
        if (canvas->update() != tvg::Result::Success || canvas->draw(retarget) != tvg::Result::Success) return false;
        regions.resize(canvas->dirtyRegions());
        if (!regions.empty() && canvas->dirtyRegions(regions.data(), uint32_t(regions.size())) != tvg::Result::Success) return false;
        if (canvas->sync() != tvg::Result::Success) return false;
        SDL_SetWindowTitle(window, app->windowTitle().c_str());
        updateTextInput();
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (SDL_GetMouseFocus() == window) {
            auto hit = app->customTitleBarVisible() ? nativeWindowHitTest(window, {int(mx), int(my)}, false) : SDL_HITTEST_NORMAL;
            if (hit < SDL_HITTEST_RESIZE_TOPLEFT) cursors.set(app->context.cursor());
        }
        bool whole = presentWhole || retarget || target != presented || liveResize.exposing();
        presentWhole = false;
        presented = target;
        if (whole) return SDL_BlitSurface(frame, nullptr, target, nullptr) && SDL_UpdateWindowSurface(window);
        rects.clear();
        SDL_Rect bounds{0, 0, target->w, target->h};
        for (const auto& region : regions) {
            SDL_Rect rect{region.xMin, region.yMin, region.xMax - region.xMin, region.yMax - region.yMin};
            if (!SDL_GetRectIntersection(&rect, &bounds, &rect)) continue;
            if (!SDL_BlitSurface(frame, &rect, target, &rect)) return false;
            rects.push_back(rect);
        }
        return rects.empty() || SDL_UpdateWindowSurfaceRects(window, rects.data(), int(rects.size()));
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
        std::ofstream output(path, std::ios::binary);
        output << "P6\n"
               << frame->w << ' ' << frame->h << "\n255\n";
        for (auto pixel : pixels) {
            char rgb[] = {char(pixel >> 16), char(pixel >> 8), char(pixel)};
            output.write(rgb, 3);
        }
        return bool(output);
    }

    SDL_Window* window = nullptr;
    SDL_Surface* frame = nullptr;
    SDL_Surface* presented = nullptr;
    std::vector<tvg::Region> regions;
    std::vector<SDL_Rect> rects;
    bool presentWhole = true;
    tn::Cursors cursors;
    NativeTextInput nativeInput;
    NativeTitleBarInput titleBarInput;
    bool nativeInputReady = false;
    std::vector<uint32_t> pixels;
    std::unique_ptr<tvg::SwCanvas> canvas{tvg::SwCanvas::gen(tvg::EngineOption::SmartRender)};
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
