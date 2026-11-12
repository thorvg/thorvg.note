#pragma once

#include <thornote/documents/workspace.h>
#include <thornote/documents/autosave.h>
#include <thornote/preferences/layout.h>
#include <thornote/scripting/layout_script.h>
#include <thornote/preferences/preferences.h>
#include <thornote/preferences/theme_profiles.h>
#include <thornote/documents/note_browser.h>
#include <thornote/extensions/host.h>
#include <thornote/platform/system_fonts.h>
#include <thornote/platform/native_window.h>
#include <ui/ui.h>
#include <SDL3/SDL.h>
#include <array>
#include <set>

namespace tn
{
struct NativeTextEdit;
namespace test
{
class EditorTest;
}

class EditorApp
{
    friend class test::EditorTest;
    SystemFonts systemFonts;

public:
    using WindowAction = tn::WindowAction;
    explicit EditorApp(std::function<void()> wake = {}, storage::Path settingsDirectory = {});
    ~EditorApp();
    void openPath(const storage::Path&);
    void openSettings();
    void activateDocument(uint64_t);
    std::optional<ui::Rect> restoreSession();
    void setLayout(Layout);
    void setLayoutDirectory(storage::Path path);
    void render(float width, float height, double seconds = -1);
    void event(const SDL_Event&, float windowToLogicalX, float windowToLogicalY);
    void nativeTextInput(const NativeTextEdit&);
    bool nativeKeyDown(const SDL_KeyboardEvent&);
    bool poll();
    int nextPollDelay(uint64_t now) const;
    bool filesPending() const { return workspace.busy() || noteBrowser.loading(); }
    void requestQuit();
    void dialogResult(bool save, uint64_t target, const std::vector<std::string>& paths, const std::string& error);
    std::optional<ui::Rect> textInputArea();
    // The document whose editor owns the keyboard and accepts committed text.
    DocumentTab* textInputTarget() const;
    bool editorAt(float x, float y) const;
    std::string windowTitle() const;
    bool nativeTitleBarVisible() const { return !customTitleBarVisible(); }
    bool customTitleBarVisible() const;
    void setCustomTitleBarSupported(bool supported) { customChrome = supported; }
    void setWindowState(bool maximized, bool fullscreen)
    {
        windowMaximized = maximized;
        windowFullscreen = fullscreen;
    }
    bool windowDraggableAt(float x, float y) const;
    storage::Path suggestedSavePath(uint64_t id, const storage::Path& defaultFolder) const;
    bool done() const { return exiting; }

    Workspace workspace;
    ui::Context context;
    std::function<void(bool save, uint64_t target, const storage::Path& directory)> chooseFile;
    std::function<void(WindowAction)> changeWindow;
    std::function<bool(float x, float y, std::string_view selection, const ui::CommandRegistry&)> showTextMenu;

private:
    enum class Prompt
    {
        None,
        Close,
        Quit
    };
    void registerCommands();
    void focusEditor(bool focus);
    void openPalette();
    void closePalette();
    void openRecentFiles();
    void findFile(const storage::Path& directory = {});
    void openRecentDirectory(const storage::Path&);
    bool openLatest(const storage::Path& directory);
    void drawWelcome(ui::Rect);
    bool welcomeVisible() const { return workspace.documents().empty() && !settings.active && (composition != Layout::Note || !folderWorkspace); }
    void requestClose(uint64_t id);
    void save(uint64_t id, bool saveAs = false);
    void finishPrompt(bool discard);
    void cancelPrompt();
    void applyAppearance();
    void editTheme();
    void configure(DocumentTab&);
    void prepareFont(DocumentTab&, std::string_view added = {});
    void cycleTabs(int direction);
    void closeSettings();
    void drawSettings(ui::Rect);
    bool selectFont(const FontChoice&);
    Preferences preferences() const;
    void loadPreferences();
    bool flushPreferences(bool force = false);
    bool hasLayoutView(std::string_view) const;
    SessionState captureSession(bool content) const;
    bool checkpointSession(bool force = false);
    bool pollSession();
    LayoutState& layout() { return layouts[size_t(composition)]; }
    const LayoutState& layout() const { return layouts[size_t(composition)]; }
    void drawHeader(ui::Rect);
    void drawTitleBar(ui::Rect, bool headerOnly = false);
    void drawAppIcon(ui::Rect, bool divider = true);
    void drawWindowControls(ui::Rect, std::string_view instance, bool divider = true);
    void drawWindowTitle(ui::Rect, bool text = true, bool divider = true);
    void drawCenteredTitle(ui::Rect available, std::string_view title, float size, bool modified = false);
    void drawWindowButton(ui::Rect, std::string_view instance, const char* command);
    std::vector<ui::ViewType> windowViewTypes(bool divider = true);
    void drawTabs(ui::Context&, ui::Rect, bool titlebar = false);
    void drawSidebar(ui::Rect);
    void drawCollections(ui::Rect);
    void drawDocuments(ui::Rect, bool notes);
    void drawNoteHeading(ui::Rect);
    void drawStatus(ui::Rect);
    void drawEditor(ui::Rect);
    void drawFind(ui::Rect&);
    void openFind(bool replace = false);
    void closeFind();
    void updateFind();
    void findMatch(int direction);
    void replaceMatch(bool all = false);
    bool findFocused() const;
    void drawMenu();
    void openMenu(std::string_view);
    void openEditorMenu(float x, float y);
    void closeMenu(bool preserveFolder = false);
    void activateMenu(size_t);
    std::vector<DocumentTab*> filteredDocuments() const;
    void selectCollection(std::string);
    bool browsingNoteFiles() const;
    void refreshNoteFiles(bool force = false);
    void selectNoteFolder(const storage::Path&);
    void openFolderMenu(const storage::Path&, ui::Rect);
    void removeNoteFolder(const storage::Path&);
    void drawNoteFiles(ui::Rect);
    void drawPrompt(ui::Rect);
    DocumentTab* target() const;
    DocumentTab* visibleDocument() const;
    bool searchFocused() const;
    void editorCommand(ui::Key);
    bool key(const SDL_KeyboardEvent&);
    void commitText(uint64_t, std::string_view);
    bool syncInputExtensions();
    void editInputExtensions();
    ui::lua::Values layoutScriptState() const;
    void editLayoutScript(bool titlebar = false);
    void reloadLayoutScript();

    ThemeProfiles themeProfiles;
    ui::Theme theme;
    RecentItems recent;
    storage::Path findDirectory;  // Folder shown by the pending open dialog.
    std::unique_ptr<tvg::Picture> titleBarIcon;
    ui::TextEditorOptions options{.markdown = true};
    ui::TabOptions tabOptions{ui::TabStyle::Connected, 7, 6};
    ui::FontId editorFont = 0, lineNumberFont = 0;
    std::array<ui::FontId, 2> bundledFonts{};
    std::vector<uint32_t> labelCodepoints;
    FontChoice fontChoice{{}, 0, "System (Automatic)"};
    struct Settings
    {
        bool open = false, active = false, catalogLoaded = false;
        int category = 0;
        float scroll = 0;
        size_t fontOffset = 0;
        ui::TextInput search, fontSearch;
        ui::Rect searchBounds{}, fontSearchBounds{};
        std::string previousFontQuery, fontError, saveError;
        std::vector<FontChoice> catalog;
        tn::Document sample;
        ui::TextEditor preview{sample};
    } settings;
    struct Find
    {
        bool open = false, replace = false;
        ui::TextInput query, replacement;
        ui::Rect queryBounds{}, replacementBounds{};
        uint64_t document = 0, revision = uint64_t(-1);
        std::string searchedQuery;
        std::vector<ui::TextDecoration> matches;
        std::optional<size_t> current;
    } find;
    storage::Path preferencesPath;
    storage::DiskStamp preferencesStamp;
    Autosave preferenceSave{500, true, true};
    NoteBrowser noteBrowser;
    NoteFileList noteFiles;
    SessionStore sessionStore;
    // Checkpoints recur while typing instead of waiting for idle input.
    Autosave sessionSave{750, false, false};
    float restoredWidth = 1080, restoredHeight = 720;
    std::string submittedSession;
    ui::CommandRegistry commands;
    ui::CommandRegistry recentCommands;
    tn::InputExtensions inputExtensions{commands,
                                        [this](uint64_t id) -> ui::TextEditor* { auto tab = workspace.find(id); return tab && !tab->loading ? &tab->editor : nullptr; },
                                        [this] { auto tab = target(); return tab ? tab->id : 0; },
                                        [this](uint64_t id) {
                                            auto tab = visibleDocument();
                                            return editorFocus && tab && tab->id == id;
                                        }};
    bool recentPalette = false;
    struct
    {
        ui::Rect viewport{};
        float x = 0, y = 0, limitX = 0, limitY = 0;
        std::optional<float> grabX, grabY;
    } welcomeScroll;
    struct
    {
        float x = 0, width = 0, origin = 0, limit = 0;
        size_t count = 0;
        uint64_t selected = 0, pressed = 0;
        std::optional<float> grab;
        bool dragging = false;
    } tabScroll;
    LayoutScript layoutScript{commands};
    ui::Composition workbench;
    ui::CommandListState paletteState, listState;
    ui::ListState collectionList, folderList;
    std::vector<ui::ListItem> collectionItems, folderItems;  // Borrowed by the lists until input is dispatched.
    ui::Rect editorBounds{}, sidebarBounds{}, searchBounds{}, menuButton{};
    ui::Rect headerDragBounds{};
    std::vector<ui::Rect> windowDragBounds;
    ui::Rect promptSave{}, promptDiscard{}, promptCancel{}, hintBounds{};
    float width = 0, height = 0, pointerX = -1, pointerY = -1;
    Layout composition = Layout::Memo;
    std::array<LayoutState, layoutCount> layouts = defaultLayouts;
    std::string menu, menuRoot;
    storage::Path menuFolder;
    std::optional<ui::Rect> contextMenuAnchor;
    ui::MenuDefinition menuDefinition;
    size_t menuSelection = 0, documentOffset = 0, collectionOffset = 0;
    float noteFileScroll = 0;
    ui::TextInput noteSearch;
    mutable std::string searchedQuery;
    mutable std::unordered_map<uint64_t, std::pair<uint64_t, bool>> searchHits;  // tab id -> (revision + 1, match)
    std::string collection = "all", previousQuery, hint;
    bool folderWorkspace = false;
    std::set<uint64_t> favorites;
    bool revealDocument = true;
    bool dark = false, commandList = false, palette = false, dialog = false;
    bool customChrome = false, windowMaximized = false, windowFullscreen = false;
    WindowControls windowControls{nativeWindowControls()};
    // Whether the visible document editor owns keyboard input; mirrors the context focus on "editor".
    bool editorFocus = false;
    bool composing = false, windowFocused = true, exiting = false, waitingSave = false;
    std::string suppressedInputText;
    bool suppressKeyText = false, textInputShift = false;
    uint64_t paletteTarget = 0, lastActive = 0, promptTarget = 0;
    Prompt prompt = Prompt::None;
    std::set<uint64_t> discarded;
    uint64_t nextLayoutPoll = 0;
};

}  // namespace tn
