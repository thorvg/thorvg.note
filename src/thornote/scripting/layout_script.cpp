#include "layout_script.h"
#include <thornote/app/app_identity.h>
#include <thornote/app/editor_app.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <map>
#include <set>

namespace tn
{
namespace
{
std::filesystem::path bundledLayout()
{
    return appData("layouts/workspace.lua");
}

std::filesystem::path layoutOverride(const std::filesystem::path& directory, Layout profile)
{
    auto path = directory / (std::string(layoutId(profile)) + ".lua");
    std::error_code error;
    if (profile == Layout::Memo && !std::filesystem::exists(path, error)) {
        auto legacy = directory / "text-editor.lua";
        if (std::filesystem::exists(legacy, error)) return legacy;
    }
    return path;
}

const std::vector<std::string> views{
    "header",
    "collections",
    "documents",
    "tabs",
    "note-heading",
    "content",
    "editor",
    "status",
    "titlebar",
    "window-icon",
    "window-title",
    "window-tabs",
    "window-drag",
    "window-close",
    "window-minimize",
    "window-maximize",
    "window-fullscreen",
    "window-zoom",
    "window-controls",
};

bool checkViews(const ui::Element& root, std::string& error)
{
    std::map<std::string, size_t> counts;
    bool editorVisible = false;
    std::function<bool(const ui::Element&, bool)> visit = [&](const ui::Element& node, bool visible) {
        visible &= node.visible;
        if (node.kind == ui::ElementKind::View) {
            if (++counts[node.source] > 1 && node.source.compare(0, 7, "window-") != 0) {
                error = "app views can occur only once: " + node.source;
                return false;
            }
            if (node.source == "editor" || node.source == "content") editorVisible |= visible;
        }
        for (const auto& child : node.children)
            if (!visit(child, visible)) return false;
        return true;
    };
    if (!visit(root, true)) return false;
    if (counts["content"] && counts["editor"]) {
        error = "content already owns the editor view";
        return false;
    }
    if (!editorVisible) {
        error = "layout requires one visible editor view";
        return false;
    }
    return true;
}

bool ensureWindowControls(ui::Composition& composition, bool custom, bool leading, float controlsWidth, std::string& error)
{
    auto root = composition.definition();
    bool changed = false;
    std::function<void(ui::Element&)> migrate = [&](ui::Element& node) {
        for (auto& child : node.children)
            migrate(child);
        if (node.kind != ui::ElementKind::Row || node.children.size() != 2) return;
        for (size_t i = 0; i < 2; ++i) {
            auto& group = node.children[i];
            const auto& header = node.children[1 - i];
            if (group.id != "window-controls" || group.kind != ui::ElementKind::Row || group.width.basis != 78 || group.children.size() != 5 || header.kind != ui::ElementKind::View || header.source != "header") continue;
            const auto& children = group.children;
            if (children[0].source != "window-drag" || children[1].source != "window-close" || children[2].source != "window-minimize" || (children[3].source != "window-fullscreen" && children[3].source != "window-zoom") || children[4].source != "window-drag") continue;
            // Recognize the old bundled group without rearranging arbitrary user layouts.
            group.kind = ui::ElementKind::View;
            group.source = "window-controls";
            group.width = ui::fixed(controlsWidth);
            group.children.clear();
            if (i != (leading ? 0u : 1u)) std::swap(node.children[0], node.children[1]);
            changed = true;
            break;
        }
    };
    migrate(root);
    std::set<std::string> ids;
    ui::Element* bar = nullptr;
    std::vector<ui::Element*> headers;
    auto action = [](const ui::Element& node) {
        return node.kind == ui::ElementKind::View ? node.source : node.kind == ui::ElementKind::Button && node.enabled ? node.command
                                                                                                                       : std::string{};
    };
    auto control = [&](const ui::Element& node) {
        auto id = action(node);
        if (id == "window-controls") return 7u;
        return id == "window-close" || id == "window.close" ? 1u : id == "window-minimize" || id == "window.minimize"                             ? 2u
            : id == "window-fullscreen" || id == "window-maximize" || id == "window-zoom" || id == "window.fullscreen" || id == "window.maximize" ? 4u
                                                                                                                                                  : 0u;
    };
    std::function<void(ui::Element&, bool)> find = [&](ui::Element& node, bool visible) {
        ids.insert(node.id);
        visible &= node.visible && node.width.maximum > 0 && node.height.maximum > 0;
        if (visible) {
            if (node.kind == ui::ElementKind::View && node.source == "header") headers.push_back(&node);
            bool row = node.kind == ui::ElementKind::Row && (node.id == "titlebar" || std::any_of(node.children.begin(), node.children.end(), [&](const auto& child) { return control(child) || child.id == "window-controls"; }));
            if (!bar && (row || (node.kind == ui::ElementKind::View && node.source == "titlebar"))) bar = &node;
        }
        for (auto& child : node.children)
            find(child, visible);
    };
    find(root, true);
    std::string prefix = "required-window";
    while (std::any_of(ids.begin(), ids.end(), [&](const std::string& id) { return id == prefix || id.compare(0, prefix.size() + 1, prefix + "/") == 0; }))
        prefix += '_';
    auto view = [&](const char* source) {
        ui::Element node;
        node.id = prefix + "/" + source;
        node.source = source;
        return node;
    };
    if (!bar && !headers.empty()) {
        bar = headers.front();
        bar->source = "titlebar";
        changed = true;
    }
    if (!bar) {
        if (!custom) return false;
        auto titlebar = view("titlebar");
        titlebar.height = ui::fixed(40);
        ui::Element frame;
        frame.kind = ui::ElementKind::Column;
        frame.id = prefix;
        frame.children.push_back(std::move(titlebar));
        frame.children.push_back(std::move(root));
        return composition.apply(std::move(frame), error);
    }
    if (bar->height.basis < 40) {
        bar->height = ui::fixed(40);
        changed = true;
    }
    bool toolbar = bar->kind == ui::ElementKind::View;
    bool iconPresent = false;
    ui::Element* caption = nullptr;
    std::set<const ui::Element*> inside;
    unsigned present = toolbar ? 7 : 0;
    std::function<void(ui::Element&, bool)> inspect = [&](ui::Element& node, bool visible) {
        inside.insert(&node);
        visible &= node.visible && node.width.maximum > 0 && node.height.maximum > 0;
        if (visible) {
            present |= control(node);
            iconPresent |= node.kind == ui::ElementKind::View && node.source == "window-icon";
            if (node.kind == ui::ElementKind::View && node.source == "header") toolbar = true;
            if (!caption && node.kind == ui::ElementKind::View && node.source == "window-title") caption = &node;
        }
        for (auto& child : node.children)
            inspect(child, visible);
    };
    inspect(*bar, true);
    // Older scripts kept a title-only row and a separate application header.
    for (auto header : headers) {
        if (header == bar || inside.count(header)) continue;
        header->kind = ui::ElementKind::Label;
        header->source.clear();
        header->visible = false;
        changed = true;
    }
    if (!toolbar) {
        if (caption) caption->source = "header";
        else bar->children.push_back(view("header"));
        for (auto& child : bar->children)
            if (child.id == "window-trailing" && child.source == "window-drag") child.visible = false;
        changed = true;
    }
    if (custom && present != 7) {
        auto position = leading ? std::find_if(bar->children.begin(), bar->children.end(), [](const auto& node) { return node.source == "header"; }) : bar->children.end();
        const std::vector<const char*> sources = leading ? std::vector<const char*>{"window-close", "window-minimize", "window-zoom"}
                                                         : std::vector<const char*>{"window-minimize", "window-zoom", "window-close"};
        for (auto source : sources) {
            auto button = view(source);
            if (!(present & control(button))) {
                button.width = ui::fixed(leading ? 22 : controlsWidth / 3);
                position = bar->children.insert(position, std::move(button)) + 1;
            }
        }
        changed = true;
    }
    if (custom && !leading && !iconPresent && bar->kind == ui::ElementKind::Row) {
        auto icon = view("window-icon");
        icon.width = ui::fixed(36);
        bar->children.insert(bar->children.begin(), std::move(icon));
        changed = true;
    }
    return changed && composition.apply(std::move(root), error);
}
}  // namespace

LayoutScript::LayoutScript(ui::CommandRegistry& commands) :
    configuration(commands), titlebarConfiguration(commands)
{
    configuration.validateRoot = checkViews;
}

void LayoutScript::setDirectory(std::filesystem::path path)
{
    directory = std::move(path);
    watched.clear();
    titlebarWatched.clear();
}

void LayoutScript::iconAssets(std::filesystem::path root, std::function<bool(std::vector<ui::IconAsset>, std::string&)> apply)
{
    applyIconAssets = std::move(apply);
    configuration.iconAssets(root, [this](std::vector<ui::IconAsset> assets, std::string& error) {
        return updateIcons(0, std::move(assets), error);
    });
    titlebarConfiguration.iconAssets(std::move(root), [this](std::vector<ui::IconAsset> assets, std::string& error) {
        return updateIcons(1, std::move(assets), error);
    });
}

bool LayoutScript::updateIcons(size_t source, std::vector<ui::IconAsset> assets, std::string& error)
{
    auto combined = source == 0 ? assets : registeredIcons[0];
    const auto& titlebar = source == 1 ? assets : registeredIcons[1];
    for (const auto& asset : titlebar) {
        auto found = std::find_if(combined.begin(), combined.end(), [&](const auto& entry) { return entry.name == asset.name; });
        if (found == combined.end()) combined.push_back(asset);
        else *found = asset;
    }
    if (applyIconAssets && !applyIconAssets(std::move(combined), error)) return false;
    registeredIcons[source] = std::move(assets);
    return true;
}

bool LayoutScript::refresh(Layout profile, const ui::lua::Values& state, bool checkFile, bool force)
{
    bool changed = false;
    auto previousError = message;
    if (!initialized || current != profile) {
        if (initialized) compositions[size_t(current)] = std::move(configuration.view());
        configuration.clear();
        configuration.view() = std::move(compositions[size_t(profile)]);
        current = profile;
        initialized = true;
        watched.clear();
        checkFile = true;
    }
    if (directory.empty()) {
        auto path = SDL_GetPrefPath(preferencesOrganization, preferencesAppId);
        if (path) {
            directory = utf8Path(path) / "layouts";
            SDL_free(path);
        }
    }
    if (checkFile || force || watched.empty()) {
        std::error_code ec;
        auto path = layoutOverride(directory, profile);
        if (directory.empty() || !std::filesystem::exists(path, ec)) path = bundledLayout();
        auto time = std::filesystem::last_write_time(path, ec);
        if (ec) time = std::filesystem::file_time_type::min();
        if (force || path != watched || time != modified) {
            watched = path;
            modified = time;
            loadFailed = !configuration.load(utf8(path), state, views);
            message = loadFailed ? configuration.error() : "";
            changed = true;
        }
    }
    if (checkFile && configuration.poll(state, views)) {
        changed = true;
        loadFailed = !configuration.error().empty();
        message = configuration.error();
    }
    if (configuration.enabled()) {
        bool rebuilt = configuration.refresh(state, views);
        if (!rebuilt || !loadFailed) message = configuration.error();
        {
            std::string error;
            changed |= ensureWindowControls(configuration.view(), std::get<bool>(state.at("customTitleBar")),
                                            std::get<bool>(state.at("windowControlsLeading")), float(std::get<double>(state.at("windowControlsWidth"))), error);
            if (!error.empty()) message = std::move(error);
        }
    }
    changed |= refreshTitlebar(state, checkFile, force);
    return changed || message != previousError;
}

std::filesystem::path LayoutScript::edit(Layout profile, std::string& error)
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
    auto path = directory / (std::string(layoutId(profile)) + ".lua");
    if (!std::filesystem::exists(path, ec)) {
        auto source = layoutOverride(directory, profile);
        std::filesystem::copy_file(source == path ? bundledLayout() : source, path, std::filesystem::copy_options::none, ec);
        if (ec) {
            error = "Cannot create layout script: " + ec.message();
            return {};
        }
    }
    return path;
}

bool LayoutScript::enabled() const
{
    return !configuration.view().definition().id.empty();
}

bool LayoutScript::hasView(Layout profile, std::string_view source) const
{
    if (!enabled() || current != profile) return false;
    std::function<bool(const ui::Element&)> find = [&](const ui::Element& node) {
        if (node.kind == ui::ElementKind::View && node.source == source) return true;
        for (const auto& child : node.children)
            if (find(child)) return true;
        return false;
    };
    // Hidden views can become visible through app commands or a subsequent Lua rebuild.
    return find(configuration.view().definition());
}

bool EditorApp::hasLayoutView(std::string_view source) const
{
    if (layoutScript.enabled()) return layoutScript.hasView(composition, source);
    return true;
}

void LayoutScript::render(ui::Context& context, ui::Rect bounds, const std::vector<ui::ViewType>& types, const ui::CommandRegistry& commands)
{
    auto& view = configuration.view();
    const auto& bar = titlebarConfiguration.view();
    view.openMenu = bar.openMenu;
    view.menuAnchor = bar.menuAnchor;
    view.tooltip = bar.tooltip;
    view.render(context, "workspace", bounds, types, commands);
}

std::optional<ui::MenuDefinition> LayoutScript::menu(std::string_view id) const
{
    if (auto found = configuration.view().menu(id)) return found;
    if (auto found = titlebarConfiguration.view().menu(id)) return found;
    if (id == "editor-menu") {
        ui::MenuDefinition menu;
        menu.id = id;
        menu.compact = true;
        for (const auto& entry : std::vector<std::pair<const char*, const char*>>{
                 {"Undo", "edit.undo"}, {"Redo", "edit.redo"}, {"", ""}, {"Cut", "edit.cut"}, {"Copy", "edit.copy"}, {"Paste", "edit.paste"}, {"Select All", "edit.selectAll"}, {"", ""}, {"Find…", "edit.find"}, {"Replace…", "edit.replace"}}) {
            ui::MenuItem item;
            item.title = entry.first;
            item.command = entry.second;
            menu.items.push_back(std::move(item));
        }
        return menu;
    }
    return id == "folder-menu" ? fallbackFolderMenu : std::nullopt;
}

ui::lua::Values EditorApp::layoutScriptState() const
{
    const auto& state = layout();
    auto tab = visibleDocument();
    bool notes = composition == Layout::Note && !commandList;
    bool navigation = (composition == Layout::Note || commandList) && width >= 660;
    std::vector<ui::lua::Record> documents;
    for (const auto& document : workspace.documents()) {
        documents.push_back({{"id", double(document->id)}, {"title", document->displayTitle()}, {"path", utf8(document->path)}, {"dirty", document->dirty()}, {"loading", document->loading}, {"active", document.get() == tab}});
    }
    // macOS hides its traffic lights in full screen, so their reserved area collapses.
    float controlsWidth = windowControls.systemDrawn() && windowFullscreen ? 0 : windowControls.width();
    return {{"mode", std::string(layoutId(composition))}, {"dark", dark}, {"themeName", themeProfiles.profile(dark).name}, {"width", double(width)}, {"height", double(height)}, {"customTitleBar", customTitleBarVisible()}, {"nativeTitleBar", nativeTitleBarVisible()}, {"windowTitle", windowTitle()}, {"headerOnly", false}, {"dialog", dialog}, {"sidebar", state.sidebar}, {"menu", menu}, {"menuRoot", menuRoot}, {"tabStyle", double(int(tabOptions.style))}, {"windowControlsLeading", windowControls.leading()}, {"windowControlsWidth", double(controlsWidth)}, {"windowMaximized", windowMaximized}, {"windowFullscreen", windowFullscreen}, {"windowFocused", windowFocused}, {"notes", notes}, {"navigation", navigation}, {"collections", notes && navigation}, {"settings", settings.active}, {"tabs", composition != Layout::Memo && (settings.open || (!notes && workspace.documents().size() > 1))}, {"tabCount", double(workspace.documents().size() + settings.open)}, {"documentPath", tab ? utf8(tab->path) : std::string{}}, {"documents", std::move(documents)}, {"commands", commandList}, {"dirty", tab && tab->dirty()}, {"favorite", tab && favorites.count(tab->id) != 0}, {"documentReady", tab && !tab->loading}, {"sidebarWidth", double(state.sidebarWidth)}, {"documentWidth", double(notes ? state.notesWidth : commandList ? 260
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 : state.sidebarWidth)},
            {"lineNumbers", options.lineNumbers},
            {"fontSize", double(options.fontSize)}};
}

void EditorApp::editLayoutScript(bool titlebar)
{
    layoutScript.refresh(composition, layoutScriptState(), true);
    std::string error;
    auto path = titlebar ? layoutScript.editTitlebar(error) : layoutScript.edit(composition, error);
    if (path.empty()) {
        workspace.error = error;
        return;
    }
    noteSearch = {};
    collection = "all";
    settings.active = false;
    workspace.open(path);
    focusEditor(true);
}

void EditorApp::reloadLayoutScript()
{
    layoutScript.refresh(composition, layoutScriptState(), true, true);
}

}  // namespace tn
