#include <thornote/app/editor_app.h>
#include <thornote/platform/display_text.h>
#include <ui/ui.h>
#include <algorithm>
#include <cmath>
#include <cctype>

namespace tn
{
namespace
{
std::string excerpt(std::string_view value)
{
    auto start = value.find_first_not_of("\n\r \t");
    if (start == std::string_view::npos) return "";
    value.remove_prefix(start);
    value = value.substr(0, value.find('\n'));
    auto length = std::min(value.size(), size_t(160));
    while (length < value.size() && length && (uint8_t(value[length]) & 0xc0) == 0x80)
        --length;
    return std::string(value.substr(0, length));
}

std::string elide(ui::Context& context, std::string value, float width, float size)
{
    if (context.textWidth(value, size) <= width) return value;
    while (!value.empty() && context.textWidth(value + "…", size) > width) {
        size_t end = value.size() - 1;
        while (end && (uint8_t(value[end]) & 0xc0) == 0x80)
            --end;
        value.resize(end);
    }
    return value + "…";
}

std::string preview(const DocumentTab& tab)
{
    auto first = tab.document.text().find('\n');
    return first == std::string::npos ? "" : excerpt(std::string_view(tab.document.text()).substr(first + 1));
}

size_t scrollRows(size_t offset, size_t limit, float amount)
{
    if (amount == 0) return offset;
    int step = amount > 0 ? -std::max(1, int(amount * 3)) : std::max(1, int(-amount * 3));
    return size_t(std::clamp(int(offset) + step, 0, int(limit)));
}

bool matches(std::string_view value, std::string_view query)
{
    return std::search(value.begin(), value.end(), query.begin(), query.end(), [](unsigned char a, unsigned char b) {
               return std::tolower(a) == std::tolower(b);
           })
        != value.end();
}
}  // namespace

std::vector<DocumentTab*> EditorApp::filteredDocuments() const
{
    std::vector<DocumentTab*> result;
    if (searchedQuery != noteSearch.value) {
        searchedQuery = noteSearch.value;
        searchHits.clear();
    }
    // Frames ask for the visible document several times; search each text once per revision.
    auto contains = [&](const DocumentTab& tab) {
        auto& hit = searchHits[tab.id];
        if (hit.first != tab.document.revision() + 1) hit = {tab.document.revision() + 1, matches(tab.document.text(), noteSearch.value)};
        return hit.second;
    };
    for (const auto& tab : workspace.documents()) {
        if (composition == Layout::Note && !commandList && !browsingNoteFiles()) {
            if (collection == "favorites" && !favorites.count(tab->id)) continue;
            if (collection == "unsaved" && !tab->path.empty()) continue;
            if (!noteSearch.value.empty() && !matches(tab->title, noteSearch.value) && !contains(*tab)) continue;
        }
        result.push_back(tab.get());
    }
    return result;
}

DocumentTab* EditorApp::visibleDocument() const
{
    if (settings.active) return nullptr;
    auto active = workspace.active();
    if (composition != Layout::Note || commandList || browsingNoteFiles() || width < 660) return active;
    auto documents = filteredDocuments();
    return std::find(documents.begin(), documents.end(), active) != documents.end() ? active : nullptr;
}

void EditorApp::selectCollection(std::string value)
{
    settings.active = false;
    collection = std::move(value);
    documentOffset = 0;
    noteFileScroll = 0;
    refreshNoteFiles();
    auto documents = filteredDocuments();
    if (!documents.empty() && std::find(documents.begin(), documents.end(), workspace.active()) == documents.end()) workspace.select(documents.front()->id);
    focusEditor(true);
}

void EditorApp::drawCollections(ui::Rect r)
{
    context.rect(r, theme.raised);
    context.text({r.x + 16, r.y + 10, r.w - 32, 30}, "Library", theme.text, 13, true);
    size_t favoriteCount = 0, unsaved = 0;
    for (const auto& tab : workspace.documents()) {
        favoriteCount += favorites.count(tab->id);
        if (tab->path.empty()) ++unsaved;
    }
    collectionItems = {
        {"all", "All", {}, "grid"},
        {"favorites", "Favorites", std::to_string(favoriteCount), "star"},
        {"unsaved", "Unsaved", std::to_string(unsaved), "file"},
    };
    ui::list(context, "collections", {r.x + 6, r.y + 48, r.w - 12, 108}, collectionItems, collection, collectionList, [this](std::string id) { selectCollection(std::move(id)); }, 36);
    context.text({r.x + 16, r.y + 170, r.w - 32, 24}, "RECENT FOLDERS", theme.muted, 10, true);
    folderItems.clear();
    ui::Rect body{r.x + 6, r.y + 202, r.w - 12, std::max(0.0f, r.h - 214)};
    size_t count = std::max(size_t(1), size_t(body.h / 36));
    const auto& folders = recent.rootDirectories();
    size_t limit = folders.size() > count ? folders.size() - count : 0;
    collectionOffset = std::min(collectionOffset, limit);
    context.control("collections/scroll", body, [this, limit](const ui::Event& e) {
        if (e.type == ui::EventType::Scroll) collectionOffset = scrollRows(collectionOffset, limit, e.y); }, !dialog, ui::ControlFlag::Scrollable);
    if (!folders.empty()) {
        for (size_t i = collectionOffset; i < folders.size() && i < collectionOffset + count; ++i) {
            const auto& path = folders[i];
            auto name = displayName(path);
            folderItems.push_back({utf8(path), name.empty() ? displayPath(path) : name, {}, "folder", 0, !dialog});
        }
        auto selected = collection.compare(0, 7, "folder:") == 0 ? utf8(recent.rootDirectory(utf8Path(collection.substr(7)))) : std::string{};
        ui::list(context, "folders", body, folderItems, selected, folderList, [this](std::string path) { selectNoteFolder(utf8Path(path)); }, 36, [this](std::string path, ui::Rect anchor) { openFolderMenu(utf8Path(path), anchor); });
    } else context.text(body, "Open a file to add\nits folder here", theme.muted, 11);
    context.line(r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h, theme.border);
}

void EditorApp::drawDocuments(ui::Rect r, bool notes)
{
    if (notes && browsingNoteFiles()) {
        drawNoteFiles(r);
        return;
    }
    context.rect(r, theme.surface);
    auto documents = filteredDocuments();
    std::string title = !notes ? "Documents" : collection == "favorites" ? "Favorites"
        : collection == "unsaved"                                        ? "Unsaved"
                                                                         : "All Notes";
    context.text({r.x + 14, r.y + 9, r.w - 62, 30}, title, theme.text, 13, true);
    context.text({r.x + r.w - 44, r.y + 9, 28, 30}, std::to_string(documents.size()), theme.muted, 11, false, ui::Align::End);
    float top = 48;
    if (notes) {
        searchBounds = {r.x + 10, r.y + 48, r.w - 20, 30};
        ui::textField(context, "notes/search", searchBounds, noteSearch, "Search notes", true, {}, "search");
        top = 92;
        if (previousQuery != noteSearch.value) {
            previousQuery = noteSearch.value;
            documentOffset = 0;
        }
    }
    ui::Rect body{r.x + 6, r.y + top, r.w - 12, std::max(0.0f, r.h - top - 6)};
    float rowHeight = notes ? 72 : 48;
    size_t count = std::max(size_t(1), size_t(body.h / rowHeight));
    size_t limit = documents.size() > count ? documents.size() - count : 0;
    if (revealDocument) {
        auto found = std::find(documents.begin(), documents.end(), workspace.active());
        if (found != documents.end()) {
            size_t index = size_t(found - documents.begin());
            if (index < documentOffset) documentOffset = index;
            if (index >= documentOffset + count) documentOffset = index - count + 1;
        }
        revealDocument = false;
    }
    documentOffset = std::min(documentOffset, limit);
    context.control("documents/scroll", body, [this, limit](const ui::Event& e) {
        if (e.type == ui::EventType::Scroll) documentOffset = scrollRows(documentOffset, limit, e.y); }, true, ui::ControlFlag::Scrollable);
    context.clip(body, [&] {
        for (size_t i = documentOffset; i < documents.size() && i < documentOffset + count; ++i) {
            auto tab = documents[i];
            auto id = tab->id;
            ui::Rect row{body.x, body.y + (i - documentOffset) * rowHeight, body.w, rowHeight - 4};
            auto state = context.control("documents/" + std::to_string(id), row, [this, id](const ui::Event& e) {
                if (e.type == ui::EventType::Activate || (e.type == ui::EventType::Key && (e.key == ui::Key::Enter || e.key == ui::Key::Space))) {
                    activateDocument(id);
                }
            });
            bool selected = tab == workspace.active();
            if (selected || state.hovered) context.rect(row, selected ? theme.selection : theme.raised, 6);
            context.text({row.x + 10, row.y + 5, row.w - 38, 24}, elide(context, tab->displayTitle(notes), row.w - 44, 12), theme.text, 12, selected || notes);
            auto detail = tab->loading ? "Opening…" : tab->saving ? "Saving…"
                : tab->path.empty()                               ? "Not saved"
                : notes                                           ? preview(*tab)
                                                                  : displayName(tab->path.parent_path());
            context.text({row.x + 10, row.y + 29, row.w - 20, notes ? 24.0f : 14.0f}, elide(context, detail, row.w - 28, 10), theme.muted, 10);
            if (tab->dirty()) context.icon("documents/" + std::to_string(id) + "/dirty", {row.x + row.w - 26, row.y + 5, 24, 24}, "dirty", theme.muted,
                                           {state.hovered, state.pressed, true, true, state.focused});
            if (notes && favorites.count(id)) context.icon("documents/" + std::to_string(id) + "/favorite", {row.x + row.w - 26, row.y + 33, 18, 18}, "star", theme.accent,
                                                           {state.hovered, state.pressed, true, true, state.focused});
            // The ring extends outward by 3px; keep its stroke inside the list clip.
            if (state.focused) context.focusRing(row.inset(4));
        }
    });
    if (documents.empty()) context.text(body.inset(12), notes ? "No matching notes" : "No open documents", theme.muted, 12, false, ui::Align::Center);
    if (limit) {
        float track = body.h, thumb = std::max(20.0f, track * float(count) / documents.size());
        context.rect({r.x + r.w - 4, body.y + (track - thumb) * documentOffset / limit, 2, thumb}, theme.border, 1);
    }
    context.line(r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h, theme.border);
}

void EditorApp::drawSidebar(ui::Rect r)
{
    sidebarBounds = r;
    context.rect(r, theme.surface);
    context.text({r.x + 14, r.y + 8, r.w - 28, 28}, "Commands", theme.text, 13, true);
    ui::commandList(context, "sidebar/commands", {r.x + 8, r.y + 42, r.w - 16, r.h - 50}, commands, listState);
}

void EditorApp::drawNoteHeading(ui::Rect r)
{
    context.rect(r, theme.background);
    auto tab = visibleDocument();
    auto breadcrumb = settings.active ? "Settings" : !tab ? ""
        : tab->path.empty()                               ? "Unsaved / " + tab->displayTitle()
                                                          : displayName(tab->path.parent_path()) + " / " + tab->title;
    ui::Rect breadcrumbBounds{r.x + options.padding, r.y + 3, r.w - options.padding - 64, 32};
    context.text(breadcrumbBounds, elide(context, breadcrumb, breadcrumbBounds.w - 6, 11), theme.muted, 11);
    if (!tab) return;
    ui::Rect star{r.x + r.w - 48, r.y + 4, 28, 28};
    auto state = context.control("note/favorite", star, [this](const ui::Event& e) {
        if (e.type == ui::EventType::Activate || (e.type == ui::EventType::Key && e.key == ui::Key::Enter)) commands.execute("note.favorite.toggle"); }, !tab->loading);
    if (state.hovered) context.rect(star, theme.raised, 5);
    context.icon("note/favorite/icon", star, "star", favorites.count(tab->id) ? theme.accent : theme.muted,
                 {state.hovered, state.pressed, favorites.count(tab->id) != 0, !tab->loading, state.focused});
    if (state.focused) context.focusRing(star);
}

void EditorApp::drawStatus(ui::Rect r)
{
    auto tab = visibleDocument();
    context.rect(r, theme.surface);
    context.line(r.x, r.y, r.x + r.w, r.y, theme.border);
    std::string_view error = workspace.error;
    if (error.empty()) error = themeProfiles.error(dark);
    if (error.empty()) error = inputExtensions.error();
    if (error.empty()) error = layoutScript.error();
    if (!error.empty()) {
        context.text({r.x + 12, r.y, r.w - 24, r.h}, context.elideText(error, std::max(0.0f, r.w - 24), 10), theme.danger, 10);
        return;
    }
    if (settings.active) {
        context.text({r.x + 12, r.y, r.w - 24, r.h}, "Settings", theme.muted, 10);
        return;
    }
    if (!tab) return;
    auto caret = tab->editor.session().selection().caret;
    auto line = tab->document.lineAt(caret);
    size_t column = 1;
    for (auto pos = tab->document.lineStart(line); pos < caret; pos = tab->document.next(pos))
        ++column;
    const char* eol = tab->format.ending == storage::LineEnding::Lf ? "LF" : tab->format.ending == storage::LineEnding::Crlf ? "CRLF"
        : tab->format.ending == storage::LineEnding::Cr                                                                      ? "CR"
                                                                                                                             : "Mixed EOL";
    std::string state = tab->loading ? "Opening…" : tab->saving ? "Saving…"
        : tab->dirty()                                          ? "Modified"
        : tab->stamp.exists()                                   ? "Saved"
                                                                : "Not saved";
    if (!tab->editor.inputState().status.empty()) state = tab->editor.inputState().status + " · " + state;
    context.text({r.x + 12, r.y, r.w / 2, r.h}, state, theme.muted, 10);
    context.text({r.x + r.w / 2, r.y, r.w / 2 - 12, r.h}, "Ln " + std::to_string(line + 1) + ", Col " + std::to_string(column) + "     UTF-8     " + eol, theme.muted, 10, false, ui::Align::End);
}

void EditorApp::drawEditor(ui::Rect r)
{
    if (settings.active) {
        drawSettings(r);
        return;
    }
    auto tab = visibleDocument();
    editorBounds = r;
    if (!tab || tab->loading) {
        context.text(r, tab ? "Opening file…" : composition == Layout::Note && !workspace.documents().empty() ? "No matching notes"
                                                                                                              : "Create a document or open a file",
                     theme.muted, 16, false, ui::Align::Center);
        return;
    }
    if (tab->restoreView) {
        auto saved = *tab->restoreView;
        tab->editor.select(saved.anchor, saved.caret);
        tab->editor.scroll(saved.x, saved.y);
        tab->restoreView.reset();
    }
    tab->editor.render(context, "editor", editorBounds);
}

void EditorApp::render(float w, float h, double seconds)
{
    width = w;
    height = h;
    for (const auto& tab : workspace.documents()) {
        configure(*tab);
    }
    if (!settings.active && composition == Layout::Note && !commandList && !browsingNoteFiles() && w >= 660) {
        auto documents = filteredDocuments();
        if (!documents.empty() && std::find(documents.begin(), documents.end(), workspace.active()) == documents.end()) workspace.select(documents.front()->id);
    }
    syncInputExtensions();
    auto active = visibleDocument();
    auto id = active ? active->id : 0;
    if (lastActive != id) {
        lastActive = id;
        revealDocument = true;
        focusEditor(!searchFocused() && !palette && menu.empty() && prompt == Prompt::None && !dialog);
    }
    syncInputExtensions();
    hint.clear();
    if (!std::isfinite(seconds) || seconds < 0) seconds = ui::frameTime();
    context.begin(theme, {0, 0, w, h}, seconds);
    context.pointerPosition(pointerX, pointerY);
    context.rect({0, 0, w, h}, theme.background);
    auto& state = layout();
    bool notes = composition == Layout::Note && !commandList;
    if (notes) refreshNoteFiles();
    bool navigation = (composition == Layout::Note || commandList) && w >= 660;
    editorBounds = sidebarBounds = searchBounds = headerDragBounds = {};
    if (!menu.empty()) menuButton = contextMenuAnchor.value_or(ui::Rect{});
    windowDragBounds.clear();
    layoutScript.refresh(composition, layoutScriptState());
    std::vector<ui::ViewType> types{
        {"titlebar", [this](ui::Context&, ui::Rect r, std::string_view) {
             drawTitleBar(r);
         }},
        {"header", [this](ui::Context&, ui::Rect r, std::string_view) {
             drawHeader(r);
         }},
        {"tabs", [this](ui::Context& ctx, ui::Rect r, std::string_view) {
             drawTabs(ctx, r);
         }},
        {"collections", [this](ui::Context&, ui::Rect r, std::string_view) {
             drawCollections(r);
         }},
        {"documents", [this, notes](ui::Context&, ui::Rect r, std::string_view) {
             if (commandList) drawSidebar(r);
             else drawDocuments(r, notes);
         }},
        {"note-heading", [this](ui::Context&, ui::Rect r, std::string_view) {
             drawNoteHeading(r);
         }},
        {"editor", [this](ui::Context&, ui::Rect r, std::string_view) {
             drawEditor(r);
         }},
        {"content", [this](ui::Context&, ui::Rect r, std::string_view) {
             drawEditor(r);
         }},
        {"status", [this](ui::Context&, ui::Rect r, std::string_view) {
             drawStatus(r);
         }},
    };
    if (welcomeVisible()) drawWelcome({0, 0, w, h});
    else if (layoutScript.enabled()) {
        auto window = windowViewTypes();
        types.insert(types.end(), std::make_move_iterator(window.begin()), std::make_move_iterator(window.end()));
        layoutScript.render(context, {0, 0, w, h}, types, commands);
    } else {
        // Built-in arrangement for a layout script that cannot load.
        auto pane = [](const char* source, bool visible, ui::Length width, ui::Length height = {}, bool resizable = false) {
            auto element = ui::view(source, width, height);
            element.visible = visible;
            element.resizable = resizable;
            return element;
        };
        auto tree = ui::column({
            pane("titlebar", true, {}, ui::fixed(40)),
            ui::row({
                pane("collections", notes && navigation && w >= 940, ui::preferred(state.sidebarWidth, 150, 260), {}, true),
                pane("documents", navigation, ui::preferred(notes ? state.notesWidth : commandList ? 260
                                                                                                   : state.sidebarWidth,
                                                            180, 340),
                     {}, true),
                ui::column({
                    pane("tabs", composition != Layout::Memo && (settings.open || (!notes && workspace.documents().size() > 1)), {}, ui::fixed(34)),
                    pane("note-heading", notes, {}, ui::fixed(38)),
                    pane("content", true, {}),
                }),
            }),
            pane("status", true, {}, ui::fixed(22)),
        });
        std::string error;
        if (workbench.apply(std::move(tree), error)) workbench.render(context, "workspace", {0, 0, w, h}, types, commands);
    }
    if (!menu.empty() && (menuButton.w <= 0 || menuButton.h <= 0)) closeMenu();
    if (!menu.empty()) drawMenu();
    if (palette) {
        auto options = recentPalette ? ui::CommandListOptions{"Recent Files", "Find a recent file...",
                                                              recent.files.empty() ? "No recent files yet" : "No matching recent files"}
                                     : ui::CommandListOptions{};
        ui::commandPalette(context, {0, 0, w, h}, recentPalette ? recentCommands : commands, paletteState, [this, recent = recentPalette] { if (recent == recentPalette) closePalette(); }, options);
    }
    if (prompt != Prompt::None) drawPrompt({0, 0, w, h});
    if (!hint.empty() && menu.empty() && !palette && prompt == Prompt::None && !dialog) {
        float size = context.textWidth(hint, 11) + 18;
        ui::Rect r{std::clamp(hintBounds.x, 8.0f, std::max(8.0f, w - size - 8)), hintBounds.y + hintBounds.h + 8, size, 28};
        context.rect(r, theme.raised, 5, theme.border);
        context.text(r.inset(5), hint, theme.text, 11);
    }
    if (customTitleBarVisible() && !windowMaximized && !windowFullscreen) {
        auto border = theme.border;
        if (!windowFocused) border.a = 128;
        context.rect({0.5f, 0.5f, w - 1, h - 1}, {0, 0, 0, 0}, 0, border);
    }
    context.end();
    // Keyboard focus may have reached or left the editor inside the context; a closing modal layer restores its own focus.
    if (context.focused() == "editor") focusEditor(menu.empty() && !palette && prompt == Prompt::None);
    else if (!context.focused().empty()) focusEditor(false);
    else if (editorFocus) context.focus("editor");
}

}  // namespace tn
