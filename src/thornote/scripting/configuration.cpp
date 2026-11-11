#include "configuration.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace tn
{

Configuration::~Configuration()
{
    clear();
}

void Configuration::clear()
{
    for (const auto& id : commandIds)
        commands.remove(id);
    commandIds.clear();
    script.reset();
    filename.clear();
    watchedFilename.clear();
    message.clear();
    assetFiles.clear();
    iconRegistry.reset();
    iconSettingsChanged = false;
}

void Configuration::iconAssets(std::filesystem::path root, std::function<bool(std::vector<ui::IconAsset>, std::string&)> apply)
{
    iconsRoot = std::move(root);
    applyIcons = std::move(apply);
    iconSettingsChanged = true;
}

Configuration::AssetFile Configuration::stamp(const std::filesystem::path& path)
{
    AssetFile result;
    result.requested = path;
    std::error_code ec;
    result.resolved = std::filesystem::canonical(path, ec);
    if (ec || !std::filesystem::is_regular_file(result.resolved, ec) || ec) return result;
    result.size = std::filesystem::file_size(result.resolved, ec);
    if (ec) return result;
    result.modified = std::filesystem::last_write_time(result.resolved, ec);
    result.exists = !ec;
    return result;
}

bool Configuration::readAssets(const std::filesystem::path& root, const std::vector<FileRequest>& requests, size_t fileLimit, size_t totalLimit,
                               std::vector<std::string>& data, std::vector<AssetFile>& files)
{
    if (requests.empty()) return true;
    std::error_code ec;
    auto offset = files.size();
    for (const auto& request : requests) {
        std::filesystem::path relative;
        try {
            relative = utf8Path(request.file);
        } catch (const std::filesystem::filesystem_error&) {
            message = request.name + ": invalid asset file path";
            return false;
        }
        if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()
            || std::any_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; })) {
            message = request.name + ": asset file must stay inside its directory";
            return false;
        }
        files.push_back(stamp(root / relative));
    }
    auto resolvedRoot = std::filesystem::canonical(root, ec);
    if (ec || !std::filesystem::is_directory(resolvedRoot, ec) || ec) {
        message = "cannot open asset directory: " + utf8(root);
        return false;
    }
    size_t total = 0;
    for (size_t i = 0; i < requests.size(); ++i) {
        const auto& file = files[offset + i];
        const auto& request = requests[i];
        auto fail = [&](const std::string& reason) {
            message = request.name + ": " + reason;
            return false;
        };
        if (!file.exists) return fail("cannot open asset file: " + request.file);
        auto prefix = std::mismatch(resolvedRoot.begin(), resolvedRoot.end(), file.resolved.begin(), file.resolved.end());
        if (prefix.first != resolvedRoot.end() || prefix.second == file.resolved.end()) return fail("asset file escapes its directory");
        if (file.size > fileLimit || total + file.size > totalLimit)
            return fail("assets exceed the " + std::to_string(fileLimit / (1024 * 1024)) + " MiB file or " + std::to_string(totalLimit / (1024 * 1024)) + " MiB total limit");
        std::ifstream input(file.resolved, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < 0 || uintmax_t(input.tellg()) != file.size) return fail("asset changed while reading");
        std::string bytes(size_t(file.size), '\0');
        input.seekg(0);
        if (!bytes.empty() && !input.read(bytes.data(), std::streamsize(bytes.size()))) return fail("cannot read asset file");
        auto after = stamp(file.requested);
        if (!after.exists || after.resolved != file.resolved || after.modified != file.modified || after.size != file.size) return fail("asset changed while reading");
        total += bytes.size();
        data.push_back(std::move(bytes));
    }
    return true;
}

bool Configuration::readIcons(const std::string& path, const std::vector<ui::lua::IconFile>& definitions, std::vector<ui::IconAsset>& assets, std::vector<AssetFile>& files)
{
    std::error_code ec;
    auto root = std::filesystem::absolute(iconsRoot.empty() ? utf8Path(path).parent_path() / "icons" : iconsRoot, ec);
    if (ec) {
        message = "cannot resolve icons directory";
        return false;
    }
    std::vector<FileRequest> requests;
    for (const auto& definition : definitions)
        requests.push_back({definition.asset.name, definition.file});
    std::vector<std::string> data;
    auto offset = files.size();
    if (!readAssets(root, requests, 4 * 1024 * 1024, 16 * 1024 * 1024, data, files)) return false;
    for (size_t i = 0; i < definitions.size(); ++i) {
        auto extension = utf8(files[offset + i].requested.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        auto asset = definitions[i].asset;
        if (extension == ".json") asset.mime = "lottie+json";
        else if (extension == ".svg") asset.mime = "svg+xml";
        else {
            message = asset.name + ": icon file must be .json or .svg";
            return false;
        }
        asset.data = std::move(data[i]);
        assets.push_back(std::move(asset));
    }
    return true;
}

bool Configuration::readImages(const std::string& path, const std::vector<ui::lua::ImageFile>& definitions, std::vector<ui::ImageAsset>& assets, std::vector<AssetFile>& files)
{
    std::vector<FileRequest> requests;
    for (const auto& definition : definitions)
        requests.push_back({definition.name, definition.file});
    std::vector<std::string> data;
    auto offset = files.size();
    auto root = utf8Path(path).parent_path();
    if (root.empty()) root = ".";
    if (!readAssets(root, requests, 16 * 1024 * 1024, 64 * 1024 * 1024, data, files)) return false;
    for (size_t i = 0; i < definitions.size(); ++i) {
        auto extension = utf8(files[offset + i].requested.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        ui::ImageAsset asset{definitions[i].name, {}, {}};
        if (extension == ".svg") asset.mime = "svg+xml";
        else if (extension == ".png") asset.mime = "png";
        else if (extension == ".jpg" || extension == ".jpeg") asset.mime = "jpeg";
        else if (extension == ".webp") asset.mime = "webp";
        else {
            message = asset.name + ": image file must be .svg, .png, .jpg, .jpeg or .webp";
            return false;
        }
        asset.data = std::move(data[i]);
        assets.push_back(std::move(asset));
    }
    return true;
}

const std::string& Configuration::error() const
{
    return message.empty() && script ? script->error() : message;
}

bool Configuration::check(const ui::Element& root, const std::vector<std::string>& types, const std::vector<ui::Command>& pending, const ui::Icons* icons)
{
    if (!ui::validate(root, message)) return false;
    std::set<std::string> ids;
    for (const auto& entry : commands.commands()) {
        if (std::find(commandIds.begin(), commandIds.end(), entry.id) == commandIds.end()) ids.insert(entry.id);
    }
    for (const auto& command : pending) {
        if (command.id.empty() || command.title.empty() || !ids.insert(command.id).second) {
            message = "duplicate or invalid command: " + command.id;
            return false;
        }
    }
    std::function<bool(const std::vector<ui::MenuItem>&)> menus = [&](const auto& items) {
        for (const auto& item : items) {
            if (!item.command.empty() && !ids.count(item.command)) {
                message = item.id + ": unknown command: " + item.command;
                return false;
            }
            if (!menus(item.items)) return false;
        }
        return true;
    };
    std::function<bool(const ui::Element&)> visit = [&](const ui::Element& node) {
        if (!menus(node.items)) return false;
        if (node.kind == ui::ElementKind::View && std::find(types.begin(), types.end(), node.source) == types.end()) {
            message = node.id + ": unknown app view: " + node.source;
            return false;
        }
        if (!node.command.empty() && !ids.count(node.command)) {
            message = node.id + ": unknown command: " + node.command;
            return false;
        }
        const auto& builtin = ui::builtinIcons();
        if (!node.icon.empty() && std::find(builtin.begin(), builtin.end(), node.icon) == builtin.end() && (!icons || !icons->contains(node.icon))) {
            message = node.id + ": unknown icon: " + node.icon;
            return false;
        }
        for (const auto& child : node.children)
            if (!visit(child)) return false;
        return true;
    };
    return visit(root) && (!validateRoot || validateRoot(root, message));
}

bool Configuration::load(const std::string& path, const ui::lua::Values& state, const std::vector<std::string>& types)
{
    message.clear();
    if (std::filesystem::path(path).extension() != ".lua") {
        message = "expected a .lua configuration";
        return false;
    }
    watchedFilename = path;
    iconSettingsChanged = false;
    assetFiles.clear();
    std::error_code ec;
    modified = std::filesystem::last_write_time(path, ec);
    if (ec) modified = std::filesystem::file_time_type::min();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        message = "cannot open " + path;
        return false;
    }
    auto size = input.tellg();
    if (size < 0 || size > 1024 * 1024) {
        message = "configuration exceeds 1 MiB";
        return false;
    }
    std::string source(size_t(size), '\0');
    input.seekg(0);
    if (!source.empty() && !input.read(source.data(), std::streamsize(source.size()))) {
        message = "cannot read " + path;
        return false;
    }
    ui::Element root;
    auto next = std::make_shared<ui::lua::Script>(commands);
    if (!next->load(source, path, message) || !next->build(state, root, message)) return false;
    auto pending = next->commands();
    std::vector<ui::IconAsset> assets;
    std::vector<AssetFile> files;
    bool iconsRead = readIcons(path, next->icons(), assets, files);
    std::vector<ui::ImageAsset> images;
    bool imagesRead = iconsRead && readImages(path, next->images(), images, files);
    assetFiles = files;
    if (!imagesRead) return false;
    auto candidate = std::make_unique<ui::Icons>();
    if (!candidate->replace(assets, message) || !check(root, types, pending, candidate.get())) return false;
    auto staged = composition;
    if (!staged.images(std::move(images), message) || !staged.apply(std::move(root), message)) return false;
    if (applyIcons && !applyIcons(std::move(assets), message)) return false;
    composition = std::move(staged);
    iconRegistry = std::move(candidate);
    assetFiles = std::move(files);
    iconSettingsChanged = false;
    for (const auto& id : commandIds)
        commands.remove(id);
    commandIds.clear();
    script = std::move(next);
    for (auto& command : pending) {
        commandIds.push_back(command.id);
        commands.add(std::move(command));
    }
    filename = path;
    previous = state;
    previousRevision = script->revision();
    return true;
}

bool Configuration::reload(const ui::lua::Values& state, const std::vector<std::string>& types)
{
    // A failed first load remains watchable without becoming the active configuration.
    auto path = watchedFilename.empty() ? filename : watchedFilename;
    return !path.empty() && load(path, state, types);
}

bool Configuration::refresh(const ui::lua::Values& state, const std::vector<std::string>& types)
{
    if (!script || (state == previous && previousRevision == script->revision())) return true;
    previous = state;
    previousRevision = script->revision();
    ui::Element root;
    if (!script->build(state, root, message)) return false;
    std::vector<ui::Command> existing;
    for (const auto& command : commands.commands()) {
        if (std::find(commandIds.begin(), commandIds.end(), command.id) != commandIds.end()) existing.push_back(command);
    }
    return check(root, types, existing, iconRegistry.get()) && composition.apply(std::move(root), message);
}

bool Configuration::poll(const ui::lua::Values& state, const std::vector<std::string>& types)
{
    if (watchedFilename.empty()) return false;
    std::error_code ec;
    auto next = std::filesystem::last_write_time(watchedFilename, ec);
    bool changed = iconSettingsChanged || (ec ? modified != std::filesystem::file_time_type::min() : next != modified);
    for (auto& file : assetFiles) {
        auto current = stamp(file.requested);
        changed |= current.exists != file.exists || current.resolved != file.resolved || current.modified != file.modified || current.size != file.size;
        file = std::move(current);
    }
    if (!changed) return false;
    iconSettingsChanged = false;
    modified = ec ? std::filesystem::file_time_type::min() : next;
    reload(state, types);
    return true;
}

}  // namespace tn
