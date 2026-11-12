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
        if (composition == Layout::Note && !commandList) {
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
    documentScroll = 0;
    noteFileScroll = 0;
    refreshNoteFiles();
    auto documents = filteredDocuments();
    if (!documents.empty() && std::find(documents.begin(), documents.end(), workspace.active()) == documents.end()) workspace.select(documents.front()->id);
    focusEditor(true);
}

void EditorApp::drawCollections(ui::Rect r)
{
    context.rect(r, theme.surface);
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
    ui::list(context, "collections", {r.x + 6, r.y + 8, r.w - 12, 108}, collectionItems, collection, collectionList, [this](std::string id) { selectCollection(std::move(id)); }, 36);
    context.text({r.x + 16, r.y + 130, r.w - 32, 24}, "RECENT FOLDERS", theme.muted, 10, true);
    folderItems.clear();
    ui::Rect body{r.x + 6, r.y + 162, r.w - 12, std::max(0.0f, r.h - 174)};
    size_t count = std::max(size_t(1), size_t(body.h / 36));
    const auto& folders = recent.rootDirectories();
    size_t limit = folders.size() > count ? folders.size() - count : 0;
    context.scroll("collections/scroll", body, collectionOffset, limit, {.step = 3, .enabled = !dialog});
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
    if (!notes) {
        context.text({r.x + 14, r.y + 9, r.w - 62, 30}, "Documents", theme.text, 13, true);
        context.text({r.x + r.w - 44, r.y + 9, 28, 30}, std::to_string(documents.size()), theme.muted, 11, false, ui::Align::End);
    }
    float top = 48;
    if (notes) {
        drawNoteSearch(r, "Search notes");
        top = 54;
        if (previousQuery != noteSearch.value) {
            previousQuery = noteSearch.value;
            documentScroll = 0;
        }
    }
    ui::Rect body{r.x + 6, r.y + top, r.w - 12, std::max(0.0f, r.h - top - 6)};
    struct Row
    {
        std::array<std::string, 3> lines;
        float top, height;
    };
    std::vector<Row> rows;
    float total = 0;
    for (const auto* tab : documents) {
        auto detail = tab->loading ? "Opening…" : tab->saving ? "Saving…"
            : notes                                           ? documentPreview(tab->document.text())
            : tab->path.empty()                               ? "Not saved"
                                                              : displayName(tab->path.parent_path());
        auto lines = notes ? previewLines(detail, body.w - 20)
                           : std::array<std::string, 3>{context.elideText(detail, body.w - 28, 10)};
        auto count = std::count_if(lines.begin(), lines.end(), [](const auto& line) { return !line.empty(); });
        float height = notes ? 40 + count * 16 : 48;
        rows.push_back({std::move(lines), total, height});
        if (revealDocument && tab == workspace.active()) {
            if (total < documentScroll) documentScroll = total;
            else if (total + height > documentScroll + body.h) documentScroll = total + height - body.h;
        }
        total += height;
    }
    revealDocument = false;
    context.scroll("documents/scroll", body, documentScroll, std::max(0.0f, total - body.h), {.step = 60});
    context.clip(body, [&] {
        for (size_t i = 0; i < documents.size(); ++i) {
            if (rows[i].top + rows[i].height <= documentScroll) continue;
            if (rows[i].top >= documentScroll + body.h) break;
            auto tab = documents[i];
            auto id = tab->id;
            ui::Rect row{body.x, body.y + rows[i].top - documentScroll, body.w, rows[i].height - 4};
            auto state = context.control("documents/" + std::to_string(id), row, [this, id](const ui::Event& e) {
                if (e.activated()) {
                    activateDocument(id);
                }
            });
            bool selected = tab == workspace.active();
            if (selected || state.hovered) context.rect(row, selected ? theme.selection : theme.raised, 6);
            auto title = context.elideText(tab->displayTitle(notes), row.w - (tab->dirty() ? 44 : 20), 12, selected || notes);
            context.text({row.x + 10, row.y + 5, row.w - 20, 24}, title, theme.text, 12, selected || notes);
            for (size_t line = 0; line < rows[i].lines.size(); ++line)
                if (!rows[i].lines[line].empty()) context.text({row.x + 10, row.y + 29 + line * 16, row.w - 20, 16}, rows[i].lines[line], theme.muted, 10);
            if (tab->dirty()) context.icon("documents/" + std::to_string(id) + "/dirty", {row.x + 14 + context.textWidth(title, 12, selected || notes), row.y + 5, 20, 24}, "dirty", theme.muted,
                                           {.hovered = state.hovered});
            if (notes && favorites.count(id)) context.icon("documents/" + std::to_string(id) + "/favorite", {row.x + row.w - 26, row.y + 33, 18, 18}, "star", theme.accent,
                                                           {state.hovered, state.pressed, true, true, state.focused});
            // The ring extends outward by 3px; keep its stroke inside the list clip.
            if (state.focused) context.focusRing(row.inset(4));
        }
    });
    if (documents.empty()) context.text(body.inset(12), notes ? "No matching notes" : "No open documents", theme.muted, 12, false, ui::Align::Center);
    ui::scrollbar(context, {r.x + r.w - 4, body.y, 2, body.h}, body.h, total, documentScroll, 20);
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
    context.rect(r, theme.surface);
    auto tab = visibleDocument();
    auto breadcrumb = settings.active ? "Settings" : !tab ? ""
        : tab->path.empty()                               ? "Unsaved / " + tab->displayTitle()
                                                          : displayName(tab->path.parent_path()) + " / " + tab->title;
    ui::Rect breadcrumbBounds{r.x + options.padding, r.y, r.w - options.padding - 92, r.h};
    context.text(breadcrumbBounds, context.elideText(breadcrumb, breadcrumbBounds.w - 6, 11), theme.muted, 11);
    ui::Rect toggle{r.x + r.w - 78, r.y + (r.h - 28) / 2, 28, 28};
    bool enabled = tab && !tab->loading;
    auto state = context.control("note/lineNumbers", toggle, [this](const ui::Event& event) {
        if (event.activated(false)) {
            commands.execute("view.lineNumbers.toggle");
            focusEditor(true);
        } }, enabled);
    if (state.hovered) context.rect(toggle, theme.raised, 5);
    context.icon("note/lineNumbers/icon", toggle, "line-numbers", theme.muted,
                 {state.hovered, state.pressed, options.lineNumbers, enabled, state.focused});
    if (state.focused) context.focusRing(toggle);
    if (state.hovered) {
        hint = options.lineNumbers ? "Hide line numbers" : "Show line numbers";
        hintBounds = toggle;
    }
    drawNoteFavorite({r.x + r.w - 42, r.y + (r.h - 28) / 2, 28, 28});
}

void EditorApp::drawNoteFavorite(ui::Rect star)
{
    auto tab = visibleDocument();
    if (!tab) return;
    auto state = context.control("note/favorite", star, [this](const ui::Event& e) {
        if (e.activated(false)) commands.execute("note.favorite.toggle"); }, !tab->loading);
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
    drawFind(r);
    editorBounds = r;
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
    updateFind();
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
    find.queryBounds = find.replacementBounds = {};
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
        bool sidebarTitle = composition == Layout::Note && navigation;
        bool categories = notes && navigation;
        auto categoryList = pane("collections", true, {});
        categoryList.id = "collection-list";
        std::vector<ui::Element> categoryChildren;
        if (sidebarTitle && categories) categoryChildren.push_back(pane("titlebar", true, {}, ui::fixed(40)));
        categoryChildren.push_back(std::move(categoryList));
        auto categoryPane = ui::column(std::move(categoryChildren));
        categoryPane.id = "collections";
        categoryPane.width = ui::preferred(state.sidebarWidth, customTitleBarVisible() && !windowControls.leading() ? 200 : 150, 260);
        categoryPane.visible = categories;
        categoryPane.resizable = true;
        auto documentList = pane("documents", true, {});
        documentList.id = "document-list";
        std::vector<ui::Element> documentChildren;
        if (sidebarTitle && !categories) documentChildren.push_back(pane("titlebar", true, {}, ui::fixed(40)));
        documentChildren.push_back(std::move(documentList));
        auto documentPane = ui::column(std::move(documentChildren));
        documentPane.id = "documents";
        documentPane.width = ui::preferred(notes ? state.notesWidth : commandList ? 260
                                                                                  : state.sidebarWidth,
                                           180, 340);
        documentPane.visible = navigation;
        documentPane.resizable = true;
        std::vector<ui::Element> children;
        if (!sidebarTitle) children.push_back(pane("titlebar", true, {}, ui::fixed(40)));
        std::vector<ui::Element> content{
            pane("tabs", composition != Layout::Memo && (settings.open || (!notes && workspace.documents().size() > 1)), {}, ui::fixed(34)),
            pane("note-heading", notes && !settings.active, {}, ui::fixed(40)),
            pane("content", true, {}),
        };
        if (composition == Layout::Note) content.push_back(pane("status", true, {}, ui::fixed(22)));
        children.push_back(ui::row({std::move(categoryPane), std::move(documentPane), ui::column(std::move(content))}));
        if (composition != Layout::Note) children.push_back(pane("status", true, {}, ui::fixed(22)));
        auto tree = ui::column(std::move(children));
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
