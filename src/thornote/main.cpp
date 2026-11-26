#include <thornote/app/app_identity.h>
#include <thornote/app/editor_app.h>
#include <thorvg.h>
#include <ui/ui.h>
#include <algorithm>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>

namespace
{
struct Runtime
{
    bool ready = false;
    std::string regular = tn::utf8(tn::appData("assets/Pretendard-Regular.ttf")), bold = tn::utf8(tn::appData("assets/Pretendard-Bold.ttf"));
    Runtime()
    {
#ifdef THORNOTE_THREADS
        // One core stays with the event loop.
        auto threads = std::max(std::thread::hardware_concurrency(), 1u) - 1;
#else
        auto threads = 0u;
#endif
        if (tvg::Initializer::init(threads) != tvg::Result::Success) return;
        ready = tvg::Text::load(regular.c_str()) == tvg::Result::Success && tvg::Text::load(bold.c_str()) == tvg::Result::Success;
    }
    ~Runtime()
    {
        ui::unloadFonts();
        tvg::Text::unload(bold.c_str());
        tvg::Text::unload(regular.c_str());
        tvg::Initializer::term();
    }
};
int usage(int status)
{
    std::cout << "usage: tnote [--layout memo|note] [--layout-dir path] [--settings] [--settings-dir path] [--fresh] [--snapshot image.ppm] [--] [paths...]\n"
                 "  Directories open in Note; files open as documents. Use . for the current directory.\n"
                 "  Reopens the last session, including drafts; --fresh skips session restore and checkpointing.\n"
                 "  Mod+N/O/S/W: new/open/save/close; Mod+Shift+P or F1: commands; Mod+,: settings\n";
    return status;
}
}  // namespace

int main(int argc, char** argv)
{
    tn::RunOptions options;
    std::optional<tn::Layout> layout;
    std::string layoutDirectory, settingsDirectory;
    std::vector<std::string> paths;
    bool settings = false, fresh = false, files = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (files) paths.emplace_back(arg);
        else if (arg == "--") files = true;
        else if (arg == "--layout" && i + 1 < argc) {
            std::string_view value(argv[++i]);
            if (value == "memo") layout = tn::Layout::Memo;
            else if (value == "note") layout = tn::Layout::Note;
            else return usage(2);
        } else if (arg == "--layout-dir" && i + 1 < argc) layoutDirectory = argv[++i];
        else if (arg == "--settings-dir" && i + 1 < argc) settingsDirectory = argv[++i];
        else if (arg == "--settings") settings = true;
        else if (arg == "--fresh") fresh = true;
        else if (arg == "--snapshot" && i + 1 < argc) options.snapshot = argv[++i];
        else if (arg == "--help" || arg == "-h") return usage(0);
        else if (!arg.empty() && arg.front() != '-') paths.emplace_back(arg);
        else return usage(2);
    }
#ifdef __EMSCRIPTEN__
    // main returns to the browser while the app keeps running, so the engine is never released.
    static Runtime runtime;
#else
    Runtime runtime;
#endif
    if (!runtime.ready) {
        std::cerr << "ThorNote: cannot initialize ThorVG and fonts\n";
        return 1;
    }
    auto app = std::make_unique<tn::EditorApp>(tn::wake, settingsDirectory.empty() ? tn::preferencesDirectory() : tn::utf8Path(settingsDirectory));
    if (!fresh) {
        if (auto restored = app->restoreSession()) {
            options.width = restored->w;
            options.height = restored->h;
        }
    }
    if (!layoutDirectory.empty()) app->setLayoutDirectory(tn::utf8Path(layoutDirectory));
    if (layout) app->setLayout(*layout);
    for (const auto& path : paths)
        app->openPath(tn::utf8Path(path));
    if (settings) app->openSettings();
    return tn::run(std::move(app), options);
}
