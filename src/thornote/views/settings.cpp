#include <thornote/app/editor_app.h>
#include <thorvg.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace tn
{
namespace
{
std::string lower(std::string value)
{
    for (auto& ch : value)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
}  // namespace

void EditorApp::activateDocument(uint64_t id)
{
    settings.active = false;
    workspace.select(id);
    focusEditor(true);
}

void EditorApp::openSettings()
{
    settings.open = settings.active = true;
    if (!settings.catalogLoaded) {
        settings.catalog = availableFonts();
        settings.catalogLoaded = true;
        auto current = std::find_if(settings.catalog.begin(), settings.catalog.end(), [this](const auto& font) { return font.path == fontChoice.path && font.index == fontChoice.index; });
        settings.fontOffset = size_t(current - settings.catalog.begin());
    }
    focusEditor(false);
    context.focus("settings/search");
}

void EditorApp::closeSettings()
{
    settings.open = settings.active = false;
    focusEditor(true);
}

void EditorApp::editTheme()
{
    std::string error;
    auto path = themeProfiles.edit(dark, error);
    if (path.empty()) {
        workspace.error = std::move(error);
        return;
    }
    settings.active = false;
    noteSearch = {};
    collection = "all";
    workspace.open(path);
    focusEditor(true);
}

void EditorApp::cycleTabs(int direction)
{
    auto& documents = workspace.documents();
    size_t total = documents.size() + settings.open;
    if (!total) return;
    size_t selected = documents.size();
    if (!settings.active)
        for (size_t i = 0; i < documents.size(); ++i)
            if (documents[i].get() == workspace.active()) selected = i;
    auto next = size_t((int(selected) + direction + int(total)) % int(total));
    if (next == documents.size()) openSettings();
    else activateDocument(documents[next]->id);
}

bool EditorApp::selectFont(const FontChoice& choice)
{
    bool automatic = choice.path.empty();
    auto id = systemFonts.load(choice, settings.fontError);
    if (automatic && !id) id = bundledFonts[0];
    if (!id || id > ui::fonts().size()) return false;
    const auto& found = ui::fonts()[id - 1];
    // The registry also accepts faces the renderer cannot draw, such as CFF outlines.
    if (!automatic) {
        if (tvg::Text::load(found.path.c_str()) != tvg::Result::Success) {
            settings.fontError = "Cannot render this font with ThorVG Text.";
            return false;
        }
        tvg::Text::unload(found.path.c_str());
    }
    editorFont = id;
    // Keep the native catalog identity even when its descriptor ordinal differs
    // from the default file face index used by the engine.
    fontChoice = automatic ? FontChoice{{}, 0, "System (Automatic)"}
                           : FontChoice{utf8Path(found.path), choice.index, choice.name.empty() ? found.name : choice.name};
    // Document editors pick the face up on their next prepareFont().
    std::vector<uint32_t> sample;
    collectCodepoints(settings.sample.text(), sample);
    settings.preview.font(systemFonts.fontFor(editorFont, sample), lineNumberFont);
    settings.fontError.clear();
    return true;
}

Preferences EditorApp::preferences() const
{
    return {fontChoice, options.fontSize, dark, int(tabOptions.style), layouts, composition, recent};
}

void EditorApp::loadPreferences()
{
    if (!preferencesPath.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(preferencesPath.parent_path(), ec);
        auto loaded = storage::load(preferencesPath, {true, 64 * 1024});
        if (!loaded) {
            settings.saveError = "Cannot read settings: " + loaded.error.message;
            preferenceSave.enabled = false;
        } else {
            preferencesStamp = loaded.value.stamp;
            Preferences value;
            if (loaded.value.stamp.exists() && !decodePreferences(loaded.value.text, value)) {
                settings.saveError = "Invalid settings file. Fix settings.conf and reopen the app.";
                preferenceSave.enabled = false;
            } else if (loaded.value.stamp.exists()) {
                selectFont(value.font);
                options.fontSize = value.fontSize;
                dark = value.dark;
                tabOptions.style = ui::TabStyle(value.tabStyle);
                layouts = value.layouts;
                composition = value.composition;
                recent = std::move(value.recent);
            }
        }
    }
    preferenceSave.saved = preferenceSave.observed = encodePreferences(preferences());
}

bool EditorApp::flushPreferences(bool force)
{
    if (preferencesPath.empty() || !preferenceSave.enabled) return false;
    auto text = encodePreferences(preferences());
    if (!preferenceSave.due(text, SDL_GetTicks(), force)) return false;
    std::error_code ec;
    std::filesystem::create_directories(preferencesPath.parent_path(), ec);
    storage::SaveOptions saveOptions;
    saveOptions.expected = preferencesStamp;
    saveOptions.maxBytes = 64 * 1024;
    auto result = ec ? storage::SaveResult{} : storage::save(preferencesPath, text, saveOptions);
    preferenceSave.at = 0;
    if (result) {
        preferencesStamp = result.stamp;
        preferenceSave.saved = text;
        settings.saveError.clear();
    } else {
        settings.saveError = "Cannot save settings: " + (ec ? ec.message() : result.error.message);
        if (result.state != storage::SaveState::NotWritten) preferenceSave.enabled = false;
    }
    return true;
}

void EditorApp::drawSettings(ui::Rect r)
{
    context.rect(r, theme.background);
    const bool categoryRail = r.w >= 560;
    const float rail = r.w >= 700 ? 180 : 148;
    const float gap = 28;
    context.text({r.x + 24, r.y + 14, categoryRail ? rail - 16 : r.w - 48, 36}, "Settings", theme.text, 22, true);
    if (categoryRail) context.text({r.x + 24, r.y + 52, rail - 16, 24}, "THORNOTE", theme.muted, 10, true);
    settings.searchBounds = categoryRail ? ui::Rect{r.x + rail + gap, r.y + 22, r.w - rail - gap * 2, 34}
                                         : ui::Rect{r.x + 24, r.y + 56, r.w - 48, 34};
    ui::textField(context, "settings/search", settings.searchBounds, settings.search, "Search settings", true, {}, "search");
    ui::button(context, "settings/search/clear", {settings.searchBounds.x + settings.searchBounds.w - 30, settings.searchBounds.y + 1, 29, 32}, "", [this] { settings.search = {}; settings.scroll = 0; }, {.style = ui::ButtonStyle::Quiet, .icon = "close", .enabled = !settings.search.value().empty()});
    const char* names[]{"Editor", "Appearance", "Layout"};
    for (int i = 0; i < 3; ++i) {
        float cell = (r.w - 64) / 3;
        ui::Rect row = categoryRail ? ui::Rect{r.x + 12, r.y + 94 + i * 38, rail - 12, 34}
                                    : ui::Rect{r.x + 24 + i * (cell + 8), r.y + 102, cell, 34};
        auto state = context.control("settings/category/" + std::to_string(i), row, [this, i](const ui::Event& event) {
            if (event.activated()) {
                settings.category = i;
                settings.scroll = 0;
                settings.search = {};
            }
        });
        if (state.hovered || (settings.search.value().empty() && settings.category == i)) context.rect(row, settings.category == i ? theme.selection : theme.surface, 6);
        float padding = categoryRail ? 12 : 6;
        context.text({row.x + padding, row.y, row.w - 2 * padding, row.h}, names[i], theme.text, 12, settings.category == i);
        if (state.focused) context.focusRing(row.inset(4));
    }
    if (categoryRail) context.line(r.x + rail, r.y + 90, r.x + rail, r.y + r.h - 24, theme.border);
    ui::Rect viewport = categoryRail ? ui::Rect{r.x + rail + gap, r.y + 90, r.w - rail - gap * 2, std::max(1.0f, r.h - 116)}
                                     : ui::Rect{r.x + 24, r.y + 152, r.w - 48, std::max(1.0f, r.h - 178)};
    // Borders and focus rings extend outside control bounds; keep them inside the scroll clip.
    auto body = viewport.inset(4);
    settings.fontSearchBounds = {};
    auto query = lower(settings.search.value());
    bool sections[]{query.empty() ? settings.category == 0 : std::string("editor font family size zoom preview line numbers").find(query) != std::string::npos,
                    query.empty() ? settings.category == 1 : std::string("appearance theme light dark tab style connected segmented underline").find(query) != std::string::npos,
                    query.empty() ? settings.category == 2 : std::string("layout script lua memo note editor").find(query) != std::string::npos};
    auto choiceColumns = [&](size_t count) {
        auto capacity = std::max(size_t(1), std::min(count, size_t(std::max(1.0f, (body.w + 8) / 118))));
        auto rows = (count + capacity - 1) / capacity;
        return (count + rows - 1) / rows;
    };
    auto choiceRows = [&](size_t count) {
        return (count + choiceColumns(count) - 1) / choiceColumns(count);
    };
    float editorHeight = 550 + (body.w < 390 ? 32 : 0);
    float appearanceHeight = 350 + (choiceRows(2) + choiceRows(3) - 2) * 48;
    float layoutHeight = 296 + (choiceRows(layoutCount) - 1) * 48;
    float total = (sections[0] ? editorHeight : 0) + (sections[1] ? appearanceHeight : 0) + (sections[2] ? layoutHeight : 0);
    float limit = std::max(0.0f, total - body.h);
    context.scroll("settings/scroll", viewport, settings.scroll, limit);
    context.clip(viewport, [&] {
        float y = body.y - settings.scroll;
        auto heading = [&](const char* title, const char* description) {
            context.text({body.x, y, body.w, 28}, title, theme.text, 16, true);
            context.text({body.x, y + 32, body.w, 24}, description, theme.muted, 11);
            y += 68;
        };
        auto choices = [&](const char* id, const std::vector<std::string>& labels, int selected, std::function<void(int)> choose) {
            auto columns = choiceColumns(labels.size());
            float cell = std::min(152.0f, (body.w - (columns - 1) * 8) / columns);
            for (size_t i = 0; i < labels.size(); ++i)
                ui::button(context, std::string(id) + "/" + std::to_string(i), {body.x + (i % columns) * (cell + 8), y + (i / columns) * 48, cell, 32}, labels[i], [choose, i] { choose(int(i)); }, {.style = int(i) == selected ? ui::ButtonStyle::Primary : ui::ButtonStyle::Default});
            y += choiceRows(labels.size()) * 48;
        };
        if (sections[0]) {
            auto top = y;
            heading("Editor", "Font settings apply to all open text editors.");
            context.text({body.x, y, body.w, 24}, "Font family", theme.text, 12, true);
            context.text({body.x, y + 26, body.w, 24}, fontChoice.name, theme.accent, 12);
            y += 60;
            settings.fontSearchBounds = {body.x, y, body.w, 32};
            ui::textField(context, "settings/fonts/search", settings.fontSearchBounds, settings.fontSearch, "Search installed fonts", true, {}, "search");
            y += 40;
            if (settings.previousFontQuery != settings.fontSearch.value()) {
                settings.previousFontQuery = settings.fontSearch.value();
                settings.fontOffset = 0;
            }
            std::vector<size_t> matches;
            auto fontQuery = lower(settings.fontSearch.value());
            for (size_t i = 0; i < settings.catalog.size(); ++i)
                if (lower(settings.catalog[i].name).find(fontQuery) != std::string::npos) matches.push_back(i);
            ui::Rect list{body.x, y, body.w, 140};
            context.rect(list, theme.surface, 6, theme.border);
            size_t fontLimit = matches.size() > 5 ? matches.size() - 5 : 0;
            context.scroll("settings/fonts/scroll", list, settings.fontOffset, fontLimit, {.step = 3});
            context.clip(list.inset(2), [&] {
                for (size_t row = 0; row < 5 && settings.fontOffset + row < matches.size(); ++row) {
                    size_t index = matches[settings.fontOffset + row];
                    const auto& choice = settings.catalog[index];
                    ui::Rect cell{list.x + 4, list.y + row * 28, list.w - 8, 28};
                    auto state = context.control("settings/fonts/" + std::to_string(index), cell, [this, index](const ui::Event& event) { if (event.activated()) selectFont(settings.catalog[index]); });
                    bool selected = choice.path == fontChoice.path && choice.index == fontChoice.index;
                    if (selected || state.hovered) context.rect(cell, selected ? theme.selection : theme.raised, 4);
                    context.text({cell.x + 10, cell.y, cell.w - 40, cell.h}, choice.name, theme.text, 12);
                    if (selected) context.icon("settings/fonts/" + std::to_string(index) + "/check", {cell.x + cell.w - 26, cell.y, 22, 28}, "check", theme.accent,
                                               {state.hovered, state.pressed, true, true, state.focused});
                    if (state.focused) context.focusRing(cell.inset(4));
                }
                if (matches.empty()) context.text(list, "No matching fonts", theme.muted, 12, false, ui::Align::Center);
            });
            ui::scrollbar(context, {list.x + list.w - 4, list.y, 2, list.h}, 5, float(matches.size()), float(settings.fontOffset), 16);
            y += 148;
            context.text({body.x, y, body.w, 20}, settings.fontError.empty() ? std::to_string(matches.size()) + " font choices · Preview" : settings.fontError,
                         settings.fontError.empty() ? theme.muted : theme.danger, 10);
            y += 26;
            ui::Rect preview{body.x, y, body.w, 80};
            context.rect(preview, theme.surface, 6, theme.border);
            context.clip(preview.inset(1), [&] { settings.preview.render(context, "settings/preview", preview.inset(1)); });
            y += 96;
            context.text({body.x, y, 120, 32}, "Font size", theme.text, 12, true);
            if (body.w < 390) y += 32;
            float x = body.x + (body.w < 390 ? 0 : std::min(150.0f, body.w - 260));
            ui::button(context, "settings/size/decrease", {x, y, 32, 32}, "", [this] { commands.execute("editor.font.decrease"); }, {.icon = "minus"});
            std::ostringstream sizeLabel;
            sizeLabel << options.fontSize << " px";
            context.text({x + 40, y, 64, 32}, sizeLabel.str(), theme.text, 12, false, ui::Align::Center);
            ui::button(context, "settings/size/increase", {x + 112, y, 32, 32}, "", [this] { commands.execute("editor.font.increase"); }, {.icon = "plus"});
            ui::button(context, "settings/size/reset", {x + 156, y, 82, 32}, "Reset", [this] { commands.execute("editor.font.reset"); }, {.style = ui::ButtonStyle::Quiet});
            y += 48;
            ui::checkbox(context, "settings/lineNumbers", {body.x, y, body.w, 30}, "Show line numbers in this layout", layout().lineNumbers, [this](bool value) { layout().lineNumbers = value; applyAppearance(); });
            y = top + editorHeight;
        }
        if (sections[1]) {
            auto top = y;
            heading("Appearance", "Choose the window appearance and document tab style.");
            context.text({body.x, y, body.w, 24}, "Color theme", theme.text, 12, true);
            y += 32;
            choices("settings/theme", {"Light", "Dark"}, dark ? 1 : 0, [this](int value) { dark = value; applyAppearance(); });
            y += 16;
            context.text({body.x, y, body.w, 24}, themeProfiles.profile(dark).name, theme.muted, 12);
            y += 30;
            ui::button(context, "settings/theme/edit", {body.x, y, std::min(230.0f, body.w), 34}, "Edit current theme…", [this] { editTheme(); });
            y += 50;
            context.text({body.x, y, body.w, 24}, "Tab style", theme.text, 12, true);
            y += 32;
            choices("settings/tabs", {"Connected", "Segmented", "Underline"}, tabOptions.style == ui::TabStyle::Connected ? 0 : tabOptions.style == ui::TabStyle::Segmented ? 1
                                                                                                                                                                            : 2,
                    [this](int value) { tabOptions.style = value == 0 ? ui::TabStyle::Connected : value == 1 ? ui::TabStyle::Segmented
                                                                                                             : ui::TabStyle::Underline; });
            y = top + appearanceHeight;
        }
        if (sections[2]) {
            heading("Layout", "Configure the workspace used by document tabs.");
            choices("settings/layout", {"Memo", "Note"}, int(composition), [this](int value) { setLayout(Layout(value)); });
            context.text({body.x, y, body.w, 24}, std::string("Options for ") + layoutName(composition), theme.muted, 11);
            y += 36;
            ui::checkbox(context, "settings/layout/lineNumbers", {body.x, y, body.w, 30}, "Show line numbers", layout().lineNumbers, [this](bool value) { layout().lineNumbers = value; applyAppearance(); });
            y += 54;
            ui::button(context, "settings/layout/edit", {body.x, y, std::min(230.0f, body.w), 34}, "Edit current layout script…", [this] { editLayoutScript(); });
        }
        if (!sections[0] && !sections[1] && !sections[2]) context.text({body.x, y, body.w, 48}, "No matching settings", theme.muted, 13);
    });
    ui::scrollbar(context, {r.x + r.w - 8, body.y, 3, body.h}, body.h, total, settings.scroll);
    context.text({body.x, r.y + r.h - 24, body.w, 22}, settings.saveError.empty() ? "Changes apply immediately and are saved automatically." : settings.saveError,
                 settings.saveError.empty() ? theme.muted : theme.danger, 10);
}
}  // namespace tn
