#include <thornote/app/window.h>
#include <thornote/app/app_identity.h>
#include <thorvg.h>
#include <ui/ui.h>
#include <iostream>
#include <string_view>

namespace
{
struct Runtime
{
    bool ready = false;
    std::string regular = tn::utf8(tn::appData("assets/Pretendard-Regular.ttf")), bold = tn::utf8(tn::appData("assets/Pretendard-Bold.ttf"));
    Runtime()
    {
        if (tvg::Initializer::init(0) != tvg::Result::Success) return;
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
                 "  Mod+N/O/S/W: new/open/save/close; Mod+Shift+P or F1: commands; Mod+B: sidebar; Mod+,: settings\n";
    return status;
}
}  // namespace

int main(int argc, char** argv)
{
    tn::RunOptions options;
    bool files = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (files) options.paths.emplace_back(arg);
        else if (arg == "--") files = true;
        else if (arg == "--layout" && i + 1 < argc) {
            options.layoutSpecified = true;
            std::string_view layout(argv[++i]);
            if (layout == "memo") options.layout = tn::Layout::Memo;
            else if (layout == "note") options.layout = tn::Layout::Note;
            else return usage(2);
        } else if (arg == "--layout-dir" && i + 1 < argc) options.layoutDirectory = argv[++i];
        else if (arg == "--settings-dir" && i + 1 < argc) options.settingsDirectory = argv[++i];
        else if (arg == "--settings") options.settings = true;
        else if (arg == "--fresh") options.fresh = true;
        else if (arg == "--snapshot" && i + 1 < argc) options.snapshot = argv[++i];
        else if (arg == "--help" || arg == "-h") return usage(0);
        else if (!arg.empty() && arg.front() != '-') options.paths.emplace_back(arg);
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
    return tn::run(options);
}
