#include "editor_app.h"
#include "app_identity.h"
#include <thornote/platform/native_window.h>
#include <thornote/platform/clipboard.h>
#include <thornote/platform/display_text.h>
#include <ui/ui.h>
#include <algorithm>
#include <exception>
#include <limits>

namespace tn
{

EditorApp::EditorApp(std::function<void()> wake, storage::Path settingsDirectory) :
    workspace(wake), noteBrowser(wake), sessionStore(std::move(wake))
{
    customChrome = nativeWindowChromeAvailable();
    bundledFonts = {ui::loadFont(utf8(appData("assets/Pretendard-Regular.ttf")).c_str()), ui::loadFont(utf8(appData("assets/Pretendard-Bold.ttf")).c_str())};
    lineNumberFont = ui::loadFont(utf8(appData("assets/DejaVuSansMono.ttf")).c_str());
    editorFont = bundledFonts[0];
    layoutScript.iconAssets(settingsDirectory.empty() ? storage::Path{} : settingsDirectory / "icons",
                            [this](std::vector<ui::IconAsset> assets, std::string& error) { return context.icons(std::move(assets), error); });
    settings.sample.text("The quick brown fox jumps over the lazy dog.\n한글 가나다 · 中文 · 日本語 · 0123456789  () {} []");
    selectFont(fontChoice);
    context.fontFor = [this](std::string_view text, bool bold) {
        auto id = systemFonts.defaultFont(bold);
        // Labels are mostly ASCII, which every UI face draws.
        if (id && std::any_of(text.begin(), text.end(), [](unsigned char ch) { return ch >= 0x80; })) {
            labelCodepoints.clear();
            collectCodepoints(text, labelCodepoints);
            id = systemFonts.fontFor(id, labelCodepoints);
        }
        return id ? id : bundledFonts[bold];
    };
    workbench.resized = [this](std::string_view id, float value) {
        if (id == "collections") layout().sidebarWidth = value;
        else if (id == "documents") (composition == Layout::Note && !commandList ? layout().notesWidth : layout().sidebarWidth) = value;
    };
    if (!settingsDirectory.empty()) preferencesPath = std::move(settingsDirectory) / "settings.conf";
    loadPreferences();
    themeProfiles.setDirectory(preferencesPath.empty() ? storage::Path{} : preferencesPath.parent_path() / "themes");
    applyAppearance();
    registerCommands();
    inputExtensions.setDirectory(preferencesPath.empty() ? storage::Path{} : preferencesPath.parent_path() / "extensions");
    auto& bar = layoutScript.titlebar();
    bar.tooltip = [this](std::string_view text, ui::Rect anchor) {
        hint = text;
        hintBounds = anchor;
    };
    bar.menuAnchor = [this](std::string_view id, ui::Rect anchor) {
        if (menuRoot.empty() || menuRoot == id) menuButton = anchor;
    };
    bar.openMenu = [this](std::string_view id, ui::Rect anchor) {
        menuRoot = id;
        menuButton = anchor;
        openMenu(id);
    };
    workspace.fileAccessed = [this](const storage::Path& path) {
        auto root = composition == Layout::Note ? recent.rootDirectory(path.parent_path()) : storage::Path{};
        recent.rememberFile(path, root.empty());
        if (!root.empty()) recent.rememberDirectory(root);
        const auto& files = noteBrowser.files();
        bool known = std::any_of(files.begin(), files.end(), [&](const auto& file) { return file.path == path; });
        noteBrowser.invalidatePreview(path);
        refreshNoteFiles(!known);
    };
}

EditorApp::~EditorApp()
{
    if (sessionSave.enabled && !workspace.busy()) checkpointSession(true);
    flushPreferences(true);
}

void EditorApp::openPath(const storage::Path& path)
{
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        activateDocument(workspace.open(path));
        return;
    }
    auto directory = std::filesystem::absolute(path, error);
    if (error) {
        workspace.error = "Cannot resolve directory: " + error.message();
        return;
    }
    recent.rememberDirectory(directory);
    folderWorkspace = true;
    layouts[size_t(Layout::Note)].sidebar = true;
    setLayout(Layout::Note);
    selectNoteFolder(recent.directories.front());
}

void EditorApp::setLayoutDirectory(storage::Path path)
{
    layoutScript.iconAssets(path / "icons",
                            [this](std::vector<ui::IconAsset> assets, std::string& error) { return context.icons(std::move(assets), error); });
    layoutScript.setDirectory(std::move(path));
}

DocumentTab* EditorApp::target() const
{
    return palette ? workspace.find(paletteTarget) : visibleDocument();
}

void EditorApp::configure(DocumentTab& tab)
{
    prepareFont(tab);
    if (tab.configured) return;
    tab.editor.theme(theme);
    tab.editor.options(options);
    tab.editor.readClipboard = readClipboardText;
    tab.editor.writeClipboard = writeClipboardText;
    tab.configured = true;
}

void EditorApp::prepareFont(DocumentTab& tab, std::string_view added)
{
    bool changed = !added.empty() && collectCodepoints(added, tab.codepoints);
    if (tab.preparedFont != editorFont) {
        tab.codepoints.clear();
        collectCodepoints(tab.document.text(), tab.codepoints);
        changed = true;
    } else if (tab.fontRevision != tab.document.revision()) {
        // Only changed text can add codepoints.
        std::string_view text = tab.document.text();
        size_t start = 0, end = 0;
        auto status = tab.document.changedSince(tab.fontRevision, start, end);
        if (status == tn::ChangeStatus::Untracked) start = 0, end = text.size();
        if (status != tn::ChangeStatus::Unchanged) changed |= collectCodepoints(text.substr(start, end - start), tab.codepoints);
    }
    tab.fontRevision = tab.document.revision();
    tab.preparedFont = editorFont;
    // One face draws the document, so a text the chosen face cannot cover moves to the closest substitute.
    if (changed) tab.editor.font(systemFonts.fontFor(editorFont, tab.codepoints), lineNumberFont);
}

void EditorApp::focusEditor(bool focus)
{
    auto active = visibleDocument();
    bool selected = focus && windowFocused && active && !active->loading;
    if (!selected || !editorFocus) composing = false;
    editorFocus = selected;
    if (selected) context.focus("editor");
    else if (context.focused() == "editor") context.focus("");
}

void EditorApp::setLayout(Layout value)
{
    composition = value;
    commandList = false;
    documentScroll = 0;
    collectionOffset = 0;
    revealDocument = true;
    applyAppearance();
    focusEditor(!palette && prompt == Prompt::None && !dialog && menu.empty());
}

void EditorApp::applyAppearance()
{
    themeProfiles.refresh(dark);
    theme = themeProfiles.profile(dark).colors;
    options.lineNumbers = layout().lineNumbers;
    options.padding = 16;
    settings.preview.theme(theme);
    settings.preview.options({options.fontSize, 8, false, true});
    // Publish the engine-accepted size to app controls and every document view.
    options.fontSize = settings.preview.options().fontSize;
    for (const auto& tab : workspace.documents()) {
        auto& editor = tab->editor;
        bool zoom = editor.options().fontSize != options.fontSize;
        auto anchor = zoom ? editor.caretRect() : ui::Rect{};
        editor.theme(theme);
        editor.options(options);
        if (zoom) {
            auto caret = editor.caretRect();
            auto area = editor.textViewport();
            float horizontal = caret.x + editor.scrollX() + 3 <= area.x + area.w ? 0 : editor.scrollX() + caret.x - anchor.x;
            editor.scroll(horizontal, editor.scrollY() + (caret.y + caret.h / 2) - (anchor.y + anchor.h / 2));
        }
    }
}

void EditorApp::editorCommand(ui::Key key)
{
    auto tab = target();
    if (!tab || tab->loading) return;
    configure(*tab);
    auto& editor = tab->editor;
    std::string error;
    try {
        if (key == ui::Key::Z) editor.undo();
        else if (key == ui::Key::Y) editor.redo();
        else if (key == ui::Key::A) editor.selectAll();
        else if (key == ui::Key::V && editor.readClipboard) editor.insert(editor.readClipboard(), error);
        else if (!editor.session().selection().empty() && editor.writeClipboard) {
            editor.writeClipboard(editor.session().selectedText());
            if (key == ui::Key::X) editor.insert("", error);
        }
    } catch (const std::exception& failure) {
        error = failure.what();
    }
    if (!error.empty()) workspace.error = std::move(error);
}

void EditorApp::registerCommands()
{
    auto add = [&](const char* id, const char* title, const char* category, const char* shortcut, std::function<void()> run,
                   std::function<bool()> available = {}, const char* reason = "") {
        commands.add({id, title, category, shortcut, std::move(run), std::move(available), reason});
    };
    auto ready = [this] {
        return target() && !target()->loading;
    };
    auto editable = [this, ready] {
        return ready() && !target()->editor.options().readOnly;
    };
    auto writable = [this] {
        return target() && !target()->busy() && !dialog;
    };
    add("file.new", "New document", "File", "Mod+N", [this] {
        auto& tab = workspace.create();
        settings.active = false;
        if (composition == Layout::Note) {
            noteSearch = {};
            if (collection == "favorites") favorites.insert(tab.id);
            else if (collection != "all") collection = "unsaved";
            if (collection == "all") noteFileScroll = 0;
        }
        focusEditor(true);
    });
    add("file.open", "Open files...", "File", "Mod+O", [this] { findFile(); }, [this] { return !dialog; });
    add("file.recent", "Open recent file", "File", "Mod+P", [this] { openRecentFiles(); }, [this] { return !dialog; });
    add("file.save", "Save document", "File", "Mod+S", [this] { if (auto tab = target()) save(tab->id); }, writable, "No document is ready, or a file operation is pending");
    add("file.saveAs", "Save document as...", "File", "Mod+Shift+S", [this] { if (auto tab = target()) save(tab->id, true); }, writable, "No document is ready, or a file operation is pending");
    add("file.close", "Close tab", "File", "Mod+W", [this] { if (settings.active) closeSettings(); else if (auto tab = target()) requestClose(tab->id); }, [this, writable] { return settings.active || writable(); }, "No tab is ready to close");
    add("file.quit", "Quit editor", "File", "Mod+Q", [this] { requestQuit(); });
    add("edit.undo", "Undo", "Edit", "Mod+Z", [this] { editorCommand(ui::Key::Z); }, [this, editable] { return editable() && target()->document.canUndo(); }, "Nothing to undo");
    add("edit.redo", "Redo", "Edit", "Mod+Shift+Z", [this] { editorCommand(ui::Key::Y); }, [this, editable] { return editable() && target()->document.canRedo(); }, "Nothing to redo");
    for (const auto& entry : std::vector<std::pair<const char*, ui::Key>>{{"copy", ui::Key::C}, {"cut", ui::Key::X}, {"paste", ui::Key::V}, {"selectAll", ui::Key::A}}) {
        auto key = entry.second;
        const char* title = key == ui::Key::C ? "Copy selection" : key == ui::Key::X ? "Cut selection"
            : key == ui::Key::V                                                      ? "Paste"
                                                                                     : "Select all";
        const char* shortcut = key == ui::Key::C ? "Mod+C" : key == ui::Key::X ? "Mod+X"
            : key == ui::Key::V                                                ? "Mod+V"
                                                                               : "Mod+A";
        auto available = [this, key, ready] {
            if (!ready()) return false;
            if ((key == ui::Key::X || key == ui::Key::V) && target()->editor.options().readOnly) return false;
            return (key != ui::Key::C && key != ui::Key::X) || !target()->editor.session().selection().empty();
        };
        commands.add({std::string("edit.") + entry.first, title, "Edit", shortcut, [this, key] { editorCommand(key); }, available, "No document or text selection is available"});
    }
    auto searchable = [this, ready] {
        return ready() && !target()->editor.composing();
    };
    auto matches = [this, searchable] {
        if (!find.open || !searchable() || find.query.value().empty()) return false;
        auto tab = target();
        if (find.document == tab->id && find.revision == tab->document.revision() && find.searchedQuery == find.query.value()) return !find.matches.empty();
        return tab->document.text().find(find.query.value()) != std::string::npos;
    };
    auto replaceable = [matches, editable] {
        return matches() && editable();
    };
    add("edit.find", "Find", "Edit", "Mod+F", [this] { openFind(); }, searchable);
#ifdef __APPLE__
    const char* replaceShortcut = "Mod+Alt+F";
#else
    const char* replaceShortcut = "Mod+H";
#endif
    add("edit.replace", "Find and replace", "Edit", replaceShortcut, [this] { openFind(true); }, searchable);
    add("edit.find.next", "Find next", "Edit", "Mod+G", [this] { findMatch(1); }, matches);
    add("edit.find.previous", "Find previous", "Edit", "Mod+Shift+G", [this] { findMatch(-1); }, matches);
    add("edit.find.close", "Close find", "Edit", "Escape", [this] { closeFind(); }, [this] { return find.open; });
    add("edit.replace.toggle", "Toggle replacement field", "Edit", "", [this] {
        if (!find.open) openFind(true);
        else {
            find.replace = !find.replace;
            if (!find.replace && context.focused() == "find/replacement") context.focus("find/query");
        } }, searchable);
    add("edit.replace.current", "Replace current match", "Edit", "", [this] { replaceMatch(); }, replaceable);
    add("edit.replace.all", "Replace all matches", "Edit", "", [this] { replaceMatch(true); }, replaceable);
    add("ui.commands.open", "Open Command Palette", "Commands", "Mod+Shift+P", [this] { openPalette(); });
    add("settings.open", "Open Settings", "Settings", "Mod+,", [this] { openSettings(); });
    add("settings.editor.open", "Open editor settings", "Settings", "", [this] {
        settings.category = 0;
        settings.search = {};
        settings.scroll = 0;
        openSettings();
    });
    add("settings.font.open", "Choose editor font", "Settings", "", [this] {
        commands.execute("settings.editor.open");
        context.focus("settings/fonts/search");
    });
    add("view.commands.toggle", "Toggle Command List", "Commands", "", [this] {
        layout().sidebar = true; commandList = !commandList;
        if (commandList) { focusEditor(false); context.focus("sidebar/commands/search"); }
        else focusEditor(true); }, [this] { return hasLayoutView("documents"); }, "This layout has no navigation view; use the Command Palette or edit the layout script");
    add("folder.remove", "Remove folder from Note", "Folders", "", [this] { removeNoteFolder(menuFolder); }, [this] { return !menuFolder.empty(); }, "Right-click a folder in Note to choose an item");
    add("view.lineNumbers.toggle", "Toggle line numbers", "View", "", [this] { layout().lineNumbers = !layout().lineNumbers; applyAppearance(); });
    add("layout.memo", "Use Memo layout", "Layout", "", [this] { setLayout(Layout::Memo); });
    add("layout.note", "Use Note layout", "Layout", "", [this] { setLayout(Layout::Note); });
    add("layout.script.edit", "Edit current layout script", "Layout", "", [this] { editLayoutScript(); });
    add("layout.titlebar.edit", "Edit title bar script", "Layout", "", [this] { editLayoutScript(true); });
    add("layout.script.reload", "Reload current layout script", "Layout", "", [this] { reloadLayoutScript(); });
    add("extensions.edit", "Edit input extensions", "Input extensions", "", [this] { editInputExtensions(); });
    add("extensions.reload", "Reload input extensions", "Input extensions", "", [this] { inputExtensions.reload(); });
    add("extensions.disable", "Disable input extensions", "Input extensions", "", [this] { inputExtensions.setEnabled(false); });
    add("extensions.enable", "Enable input extensions", "Input extensions", "", [this] { inputExtensions.setEnabled(true); });
    add("note.favorite.toggle", "Add / remove favorite", "Notes", "", [this] {
        if (auto tab = target()) { if (!favorites.erase(tab->id)) favorites.insert(tab->id); }
        focusEditor(true); }, ready, "No document is ready");
    add("theme.light", "Use light appearance", "Appearance", "", [this] { dark = false; applyAppearance(); });
    add("theme.dark", "Use dark appearance", "Appearance", "", [this] { dark = true; applyAppearance(); });
    add("theme.toggle", "Switch light / dark theme", "Appearance", "", [this] { dark = !dark; applyAppearance(); });
    add("theme.edit", "Edit current theme", "Appearance", "", [this] { editTheme(); });
    add("theme.reload", "Reload theme", "Appearance", "", [this] { themeProfiles.refresh(dark, true); applyAppearance(); });
    add("window.close", "Close window", "Window", "", [this] { requestQuit(); });
    add("window.minimize", "Minimize window", "Window", "", [this] { changeWindow(WindowAction::Minimize); }, [this] { return bool(changeWindow) && !windowFullscreen; });
    add("window.maximize", "Maximize / restore window", "Window", "", [this] { changeWindow(WindowAction::Maximize); }, [this] { return bool(changeWindow) && !windowFullscreen; });
    add("window.fullscreen", "Enter / leave full screen", "Window", "", [this] { changeWindow(WindowAction::Fullscreen); }, [this] { return bool(changeWindow); });
    add("tabs.connected", "Use connected tabs", "Appearance", "", [this] { tabOptions.style = ui::TabStyle::Connected; });
    add("tabs.segmented", "Use segmented tabs", "Appearance", "", [this] { tabOptions.style = ui::TabStyle::Segmented; });
    add("tabs.underline", "Use underlined tabs", "Appearance", "", [this] { tabOptions.style = ui::TabStyle::Underline; });
    add("editor.font.increase", "Increase editor font size", "Appearance", "Mod++", [this] { options.fontSize += std::min(1.0f, options.fontSize); applyAppearance(); });
    add("editor.font.decrease", "Decrease editor font size", "Appearance", "Mod+-", [this] { options.fontSize -= std::min(1.0f, options.fontSize / 2); applyAppearance(); });
    add("editor.font.reset", "Reset editor font size", "Appearance", "Mod+0", [this] { options.fontSize = 16; applyAppearance(); });
    add("tab.next", "Next tab", "Tabs", "Mod+Tab", [this] { cycleTabs(1); }, [this] { return workspace.documents().size() + settings.open > 1; }, "Only one tab is open");
    add("tab.previous", "Previous tab", "Tabs", "Mod+Shift+Tab", [this] { cycleTabs(-1); }, [this] { return workspace.documents().size() + settings.open > 1; }, "Only one tab is open");
    add("file.lineEndings.lf", "Convert output line endings to LF", "File", "", [this] { if (auto tab = target()) tab->format.ending = storage::LineEnding::Lf; }, ready, "No document is ready");
}

void EditorApp::openPalette()
{
    if ((palette && !recentPalette) || prompt != Prompt::None || dialog) return;
    if (!menu.empty()) closeMenu();
    paletteTarget = visibleDocument() ? visibleDocument()->id : 0;
    paletteState = {};
    palette = true;
    recentPalette = false;
    focusEditor(false);
}

void EditorApp::closePalette()
{
    palette = false;
    recentPalette = false;
    paletteTarget = 0;
    focusEditor(prompt == Prompt::None && !dialog);
}

storage::Path EditorApp::suggestedSavePath(uint64_t id, const storage::Path& defaultFolder) const
{
    auto tab = workspace.find(id);
    if (!tab) return {};
    auto name = tab->displayTitle(composition == Layout::Note);
    if (!tab->path.empty() && name == displayName(tab->path)) return tab->path;

    // A content title suggests a filename, never a directory or path.
    for (auto& ch : name) {
        if (uint8_t(ch) < 32 || uint8_t(ch) == 127) ch = ' ';
        else if (std::string_view("/\\:*?\"<>|").find(ch) != std::string_view::npos) ch = '-';
    }
    auto last = name.find_last_not_of(" .");
    name.resize(last == std::string::npos ? 0 : last + 1);
    if (name.empty()) name = "Untitled " + std::to_string(id);
    auto extension = tab->path.empty() ? std::string(".txt") : utf8(tab->path.extension());
    if (name.size() < extension.size() || name.compare(name.size() - extension.size(), extension.size(), extension) != 0) name += extension;
    return (tab->path.empty() ? defaultFolder : tab->path.parent_path()) / utf8Path(name);
}

void EditorApp::save(uint64_t id, bool saveAs)
{
    auto tab = workspace.find(id);
    if (!tab || tab->busy() || dialog) return;
    if (saveAs || tab->path.empty()) {
        if (chooseFile) {
            dialog = true;
            focusEditor(false);
            chooseFile(true, id, {});
        }
    } else if (workspace.save(id) && prompt != Prompt::None) waitingSave = true;
}

void EditorApp::dialogResult(bool saving, uint64_t id, const std::vector<std::string>& paths, const std::string& error)
{
    dialog = false;
    if (!error.empty()) {
        // A failed open dialog falls back to the most recent document of its folder.
        if (saving || !openLatest(findDirectory)) workspace.error = error;
    } else if (!paths.empty()) {
        if (saving) {
            if (workspace.saveAs(id, utf8Path(paths.front())) && prompt != Prompt::None) waitingSave = true;
        } else {
            settings.active = false;
            for (const auto& path : paths)
                workspace.open(utf8Path(path), false);
        }
    }
    focusEditor(prompt == Prompt::None && !palette);
}

void EditorApp::requestClose(uint64_t id)
{
    auto tab = workspace.find(id);
    if (!tab || tab->busy()) return;
    if (!tab->dirty()) {
        workspace.close(id);
        focusEditor(true);
        return;
    }
    prompt = Prompt::Close;
    promptTarget = id;
    focusEditor(false);
}

void EditorApp::requestQuit()
{
    if (!menu.empty()) closeMenu();
    if (dialog || workspace.busy()) {
        workspace.error = "Finish the current file operation before closing.";
        return;
    }
    if (sessionSave.enabled) {
        if (checkpointSession(true)) exiting = true;
        return;
    }
    for (const auto& tab : workspace.documents()) {
        if (tab->dirty() && !discarded.count(tab->id)) {
            prompt = Prompt::Quit;
            promptTarget = tab->id;
            palette = false;
            focusEditor(false);
            return;
        }
    }
    exiting = true;
}

void EditorApp::cancelPrompt()
{
    prompt = Prompt::None;
    waitingSave = false;
    discarded.clear();
    focusEditor(true);
}

void EditorApp::finishPrompt(bool discard)
{
    auto tab = workspace.find(promptTarget);
    if (!tab || tab->busy() || (!discard && tab->dirty())) return;
    auto action = prompt;
    prompt = Prompt::None;
    waitingSave = false;
    if (action == Prompt::Close) {
        workspace.close(tab->id, discard);
        focusEditor(true);
    } else if (action == Prompt::Quit) {
        if (discard) discarded.insert(tab->id);
        requestQuit();
    }
}

bool EditorApp::poll()
{
    bool changed = workspace.poll() != 0;
    changed |= noteBrowser.poll();
    changed |= flushPreferences();
    changed |= pollSession();
    checkpointSession();
    auto now = SDL_GetTicks();
    if (now >= nextLayoutPoll) {
        if (themeProfiles.refresh(dark)) {
            applyAppearance();
            changed = true;
        }
        changed |= layoutScript.refresh(composition, layoutScriptState(), true);
        changed |= inputExtensions.poll();
        nextLayoutPoll = now + 200;
    }
    if (waitingSave) {
        auto tab = workspace.find(promptTarget);
        if (tab && !tab->busy()) {
            waitingSave = false;
            if (!tab->dirty()) finishPrompt(false);
            changed = true;
        }
    }
    auto tab = workspace.active();
    if (changed && tab && !tab->loading && prompt == Prompt::None && !palette && !dialog && menu.empty() && context.focused().empty()) focusEditor(true);
    changed |= syncInputExtensions();
    return changed;
}

bool EditorApp::syncInputExtensions()
{
    std::vector<uint64_t> ids;
    for (auto& tab : workspace.documents())
        ids.push_back(tab->id);
    return inputExtensions.observe(ids);
}

void EditorApp::editInputExtensions()
{
    std::string error;
    auto path = inputExtensions.edit(error);
    if (!error.empty()) workspace.error = std::move(error);
    else openPath(path);
}

int EditorApp::nextPollDelay(uint64_t now) const
{
    auto deadline = nextLayoutPoll;
    if (!preferencesPath.empty()) deadline = std::min(deadline, preferenceSave.deadline());
    if (!sessionStore.busy() && !workspace.busy()) deadline = std::min(deadline, sessionSave.deadline());
    if (deadline <= now) return 0;
    return int(std::min<uint64_t>(deadline - now, std::numeric_limits<int>::max()));
}

void EditorApp::drawPrompt(ui::Rect viewport)
{
    auto tab = workspace.find(promptTarget);
    if (!tab) {
        cancelPrompt();
        return;
    }
    context.modal("prompt/save", [this] { if (!waitingSave && !dialog) cancelPrompt(); });
    context.rect(viewport, {0, 0, 0, 150});
    context.control("prompt/backdrop", viewport, {}, true, ui::ControlFlag::None);
    ui::Rect panel{(viewport.w - 540) / 2, (viewport.h - 200) / 2, 540, 200};
    context.rect(panel, theme.surface, theme.radius, theme.border);
    context.text({panel.x + 22, panel.y + 16, 496, 34}, "Save changes to " + tab->displayTitle(composition == Layout::Note) + "?", theme.text, 16, true);
    context.text({panel.x + 22, panel.y + 57, 496, 44}, waitingSave ? "Saving..." : "Keep your changes, discard them, or continue editing.", theme.muted, 12);
    if (!workspace.error.empty()) context.text({panel.x + 22, panel.y + 101, 496, 26}, workspace.error, theme.danger, 10);
    promptSave = {panel.x + 22, panel.y + 145, 150, 34};
    promptDiscard = {panel.x + 194, panel.y + 145, 150, 34};
    promptCancel = {panel.x + 366, panel.y + 145, 150, 34};
    bool enabled = !waitingSave && !dialog && !tab->busy();
    ui::button(context, "prompt/save", promptSave, "Save", [this] { save(promptTarget); }, {.style = ui::ButtonStyle::Primary, .enabled = enabled});
    ui::button(context, "prompt/discard", promptDiscard, "Discard", [this] { finishPrompt(true); }, {.enabled = enabled});
    ui::button(context, "prompt/cancel", promptCancel, "Cancel", [this] { cancelPrompt(); }, {.enabled = enabled});
}

std::string EditorApp::windowTitle() const
{
    if (settings.active) return "Settings — ThorNote";
    auto tab = visibleDocument();
    return tab ? tab->label(composition == Layout::Note) + " — ThorNote" : "ThorNote";
}

bool EditorApp::editorAt(float x, float y) const
{
    auto tab = visibleDocument();
    return menu.empty() && !palette && prompt == Prompt::None && !dialog && tab && !tab->loading && editorBounds.contains(x, y);
}

}  // namespace tn
