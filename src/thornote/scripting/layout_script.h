#pragma once

#include <thornote/preferences/layout.h>
#include "configuration.h"
#include <array>

namespace tn
{

// The app owns its files, Lua sessions and available views. UI receives native elements.
class LayoutScript
{
public:
    explicit LayoutScript(ui::CommandRegistry&);
    void setDirectory(std::filesystem::path);
    void iconAssets(std::filesystem::path root, std::function<bool(std::vector<ui::IconAsset>, std::string&)> apply);
    bool refresh(Layout, const ui::lua::Values&, bool checkFile = false, bool force = false);
    std::filesystem::path edit(Layout, std::string& error);
    bool enabled() const;
    bool hasView(Layout, std::string_view source) const;
    void render(ui::Context&, ui::Rect, const std::vector<ui::ViewType>&, const ui::CommandRegistry&);
    bool refreshTitlebar(const ui::lua::Values&, bool checkFile = false, bool force = false);
    std::filesystem::path editTitlebar(std::string& error);
    ui::Composition& titlebar() { return titlebarConfiguration.view(); }
    std::optional<ui::MenuDefinition> menu(std::string_view id) const;
    const std::string& error() const { return message.empty() ? titlebarMessage : message; }
    bool titlebarError() const { return message.empty() && !titlebarMessage.empty(); }

private:
    bool updateIcons(size_t source, std::vector<ui::IconAsset>, std::string& error);
    std::array<std::vector<ui::IconAsset>, 2> registeredIcons;
    std::function<bool(std::vector<ui::IconAsset>, std::string&)> applyIconAssets;
    std::string message;
    tn::Configuration configuration;
    tn::Configuration titlebarConfiguration;
    std::optional<ui::MenuDefinition> fallbackFolderMenu;
    std::string titlebarMessage;
    std::filesystem::path titlebarWatched;
    std::filesystem::file_time_type titlebarModified{};
    bool titlebarFailed = false;
    std::array<ui::Composition, layoutCount> compositions;
    std::filesystem::path directory, watched;
    std::filesystem::file_time_type modified{};
    Layout current = Layout::Memo;
    bool initialized = false, loadFailed = false;
};

}  // namespace tn
