#pragma once

#include <ui/ui.h>
#include <thornote/path.h>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace tn
{

// The app owns file IO, the script session, available view types and command registration.
class Configuration
{
public:
    explicit Configuration(ui::CommandRegistry& commands) :
        commands(commands) {}
    ~Configuration();
    Configuration(const Configuration&) = delete;
    Configuration& operator=(const Configuration&) = delete;
    bool load(const std::string& path, const ui::lua::Values&, const std::vector<std::string>& viewTypes);
    bool reload(const ui::lua::Values&, const std::vector<std::string>& viewTypes);
    bool refresh(const ui::lua::Values&, const std::vector<std::string>& viewTypes);
    bool poll(const ui::lua::Values&, const std::vector<std::string>& viewTypes);
    // The host callback must preserve its previous assets when returning false.
    void iconAssets(std::filesystem::path root, std::function<bool(std::vector<ui::IconAsset>, std::string&)> apply);
    void clear();
    bool enabled() const { return !filename.empty(); }
    const std::string& path() const { return filename; }
    const std::string& error() const;
    ui::Composition& view() { return composition; }
    const ui::Composition& view() const { return composition; }
    // Optional host constraints, checked before replacing the active tree or commands.
    std::function<bool(const ui::Element&, std::string& error)> validateRoot;

private:
    struct AssetFile
    {
        std::filesystem::path requested, resolved;
        std::filesystem::file_time_type modified = std::filesystem::file_time_type::min();
        uintmax_t size = 0;
        bool exists = false;
    };
    static AssetFile stamp(const std::filesystem::path&);
    struct FileRequest
    {
        std::string name, file;
    };
    bool readAssets(const std::filesystem::path& root, const std::vector<FileRequest>&, size_t fileLimit, size_t totalLimit,
                    std::vector<std::string>& data, std::vector<AssetFile>& files);
    bool readIcons(const std::string& path, const std::vector<ui::lua::IconFile>&, std::vector<ui::IconAsset>&, std::vector<AssetFile>&);
    bool readImages(const std::string& path, const std::vector<ui::lua::ImageFile>&, std::vector<ui::ImageAsset>&, std::vector<AssetFile>&);
    bool check(const ui::Element&, const std::vector<std::string>& viewTypes, const std::vector<ui::Command>& pending, const ui::Icons*);
    ui::CommandRegistry& commands;
    ui::Composition composition;
    std::shared_ptr<ui::lua::Script> script;
    std::vector<std::string> commandIds;
    std::string filename, watchedFilename, message;
    ui::lua::Values previous;
    uint64_t previousRevision = 0;
    std::filesystem::file_time_type modified{};
    std::filesystem::path iconsRoot;
    std::function<bool(std::vector<ui::IconAsset>, std::string&)> applyIcons;
    std::unique_ptr<ui::Icons> iconRegistry;
    std::vector<AssetFile> assetFiles;
    bool iconSettingsChanged = false;
};

}  // namespace tn
