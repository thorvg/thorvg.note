#include "layout_script.h"
#include <thornote/app/app_identity.h>

namespace tn
{
namespace
{
const std::vector<std::string> types{"window-icon", "window-title", "window-drag", "window-close", "window-minimize",
                                     "window-maximize", "window-fullscreen", "window-zoom", "window-controls"};
std::filesystem::path bundledTitlebar()
{
    return appData("layouts/titlebar.lua");
}
}  // namespace

bool LayoutScript::refreshTitlebar(const ui::lua::Values& state, bool checkFile, bool force)
{
    auto previous = titlebarMessage;
    bool changed = false;
    if (!titlebarConfiguration.enabled()) {
        if (!titlebarConfiguration.load(utf8(bundledTitlebar()), state, types)) {
            titlebarMessage = titlebarConfiguration.error();
            return previous != titlebarMessage;
        }
        fallbackFolderMenu = titlebarConfiguration.view().menu("folder-menu");
        changed = true;
    }
    if (checkFile || force || titlebarWatched.empty()) {
        std::error_code error;
        auto path = directory / "titlebar.lua";
        if (directory.empty() || !std::filesystem::exists(path, error)) path = bundledTitlebar();
        auto time = std::filesystem::last_write_time(path, error);
        if (error) time = std::filesystem::file_time_type::min();
        if (force || path != titlebarWatched || time != titlebarModified) {
            titlebarWatched = path;
            titlebarModified = time;
            titlebarFailed = !titlebarConfiguration.load(utf8(path), state, types);
            titlebarMessage = titlebarFailed ? titlebarConfiguration.error() : "";
            changed = true;
        }
    }
    if (checkFile && titlebarConfiguration.poll(state, types)) {
        changed = true;
        titlebarFailed = !titlebarConfiguration.error().empty();
        titlebarMessage = titlebarConfiguration.error();
    }
    bool rebuilt = titlebarConfiguration.refresh(state, types);
    if (!rebuilt || !titlebarFailed) titlebarMessage = titlebarConfiguration.error();
    return changed || titlebarMessage != previous;
}

std::filesystem::path LayoutScript::editTitlebar(std::string& error)
{
    if (directory.empty()) {
        error = "Cannot locate the layout settings folder";
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = "Cannot create layout folder: " + ec.message();
        return {};
    }
    auto path = directory / "titlebar.lua";
    if (!std::filesystem::exists(path, ec)) {
        std::filesystem::copy_file(bundledTitlebar(), path, std::filesystem::copy_options::none, ec);
        if (ec) {
            error = "Cannot create title bar script: " + ec.message();
            return {};
        }
    }
    return path;
}
}  // namespace tn
