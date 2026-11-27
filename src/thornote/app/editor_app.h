#pragma once

#include <thornote/documents/workspace.h>
#include <thornote/documents/emoji.h>
#include <thornote/preferences/layout.h>
#include <thornote/scripting/layout_script.h>
#include <thornote/preferences/preferences.h>
#include <thornote/preferences/theme_profiles.h>
#include <thornote/documents/note_browser.h>
#include <thornote/extensions/host.h>
#include <thornote/platform/system_fonts.h>
#include <thornote/shell/shell.h>
#include <thornote/widgets/find_bar.h>
#include <thornote/widgets/status_bar.h>
#include <thornote/widgets/tab_strip.h>
#include <thornote/widgets/welcome.h>
#include <thornote/widgets/window_chrome.h>
#include <ui/ui.h>
#include <array>
#include <set>

namespace tn
{
namespace test
{
class EditorTest;
}

class EditorApp : public App
{
    friend class test::EditorTest;
    SystemFonts systemFonts;
    EmojiAssets emojiAssets;

public:
    explicit EditorApp(const std::function<void()>& wake = {}, const storage::Path& settingsDirectory = {});
    ~EditorApp() override;
    void openPath(const storage::Path&);
    void openSettings();
    void activateDocument(uint64_t);
    // Returns the saved window size.
    std::optional<ui::Rect> restoreSession();
    void setLayout(Layout);
    void setLayoutDirectory(storage::Path path);
    std::optional<ui::Rect> textInputArea();
    // The document whose editor owns the keyboard and accepts committed text.
    DocumentTab* textInputTarget() const;
    bool editorAt(float x, float y) const;

    bool update() override;
    int delay() const override;
    void render(float width, float height, double seconds) override;
    tvg::Scene* scene() const override { return context.scene(); }
    std::optional<double> nextFrame() const override { return context.nextFrame(); }
    bool done() const override { return exiting; }
    bool busy() const override { return workspace.busy() || noteBrowser.loading(); }
    void window(const WindowState& state) override { chrome.window = state; }
    std::string title() const override;
    ui::Cursor cursor() const override { return context.cursor(); }
    bool draggable(float x, float y) const override;
    std::optional<TextInputState> textInput() override;
    void error(std::string message) override { workspace.error = std::move(message); }
    bool keyDown(const KeyEvent&) override;
    bool keyUp(const KeyEvent&) override;
    void text(std::string_view committed) override;
    void compose(std::string_view text, size_t cursor) override;
    void edit(const NativeTextEdit&) override;
    bool pointerMove(float x, float y) override;
    void pointerDown(const PointerEvent&) override;
    void pointerUp(const PointerEvent&) override;
    void wheel(const WheelEvent&) override;
    void focus(bool focused) override;
    void close() override;

    Workspace workspace;
    ui::Context context;

private:
    enum class Prompt
    {
        None,
        Close,
        Quit,
        Rename,
        Delete
    };
    void registerCommands();
    void focusEditor(bool focus);
    void openPalette();
    bool welcomeVisible() const { return workspace.documents().empty() && !settings.active && (composition != Layout::Note || !folderWorkspace); }
    void requestClose(uint64_t id);
    void requestQuit();
    // A relative path when the document has no file.
    storage::Path suggestedSavePath(uint64_t id) const;
    void save(uint64_t id, bool saveAs = false);
    void dialogResult(bool save, uint64_t target, const std::vector<std::string>& paths, const std::string& error);
    void finishPrompt(bool discard);
    void requestFilePrompt(Prompt);
    void finishFilePrompt();
    void cancelPrompt();
    void applyAppearance();
    void applyEditorOptions();
    void applyEditorOptions(DocumentTab&);
    void setFontSize(float size);
    void zoom(int direction, const KeyEvent&);
    void editTheme();
    void configure(DocumentTab&);
    void prepareFont(DocumentTab&, std::string_view added = {});
    void cycleTabs(int direction);
    void closeSettings();
    void drawSettings(ui::Rect);
    bool selectFont(const FontChoice&);
    Preferences preferences() const;
    bool flushPreferences(bool force = false);
    SessionState captureSession(bool content) const;
    bool checkpointSession(bool force = false);
    LayoutState& layout() { return layouts[size_t(composition)]; }
    const LayoutState& layout() const { return layouts[size_t(composition)]; }
    bool customTitleBarVisible() const { return chrome.window.customTitleBar; }
    bool nativeTitleBarVisible() const { return !customTitleBarVisible(); }
    void drawTitleBar(ui::Rect, bool headerOnly = false);
    std::vector<ui::ViewType> windowViewTypes(bool divider = true);
    void drawTabs(ui::Context&, ui::Rect, bool titlebar = false);
    void drawCollections(ui::Rect);
    void drawDocuments(ui::Rect, bool notes);
    void drawNoteHeading(ui::Rect);
    void drawStatus(ui::Rect);
    void drawEditor(ui::Rect);
    void openFind(bool replace = false);
    void updateFind() { find.update(visibleDocument()); }
    void openMenu(std::string_view);
    void closeMenu(bool preserveFolder = false);
    void openNoteMenu(uint64_t id, ui::Rect anchor);
    std::vector<DocumentTab*> filteredDocuments() const;
    bool browsingNoteFiles() const;
    void refreshNoteFiles(bool force = false);
    void selectNoteFolder(const storage::Path&);
    void drawNoteFiles(ui::Rect);
    void drawNoteSearch(ui::Rect, std::string_view placeholder);
    std::array<std::string, 3> previewLines(std::string_view, float width);
    void drawPrompt(ui::Rect);
    DocumentTab* target() const;
    DocumentTab* visibleDocument() const;
    bool searchFocused() const;
    void editorCommand(ui::Key);
    bool key(const KeyEvent&);
    void commitText(uint64_t, std::string_view);
    // Extensions and find results follow every input; returns whether the frame may change.
    bool sync(bool changed = true);
    bool syncInputExtensions();
    ui::lua::Values layoutScriptState() const;
    bool markdownShortcut(const KeyEvent&);
    void editLayoutScript(bool titlebar = false);
    // Milliseconds on the frame clock.
    static uint64_t now() { return uint64_t(ui::frameTime() * 1000); }

    std::function<void(uint64_t)> exportFile;
    ThemeProfiles themeProfiles;
    ui::Theme theme;
    struct ThemeTransition
    {
        ui::Theme from, to;
        std::optional<double> started;
    };
    std::optional<ThemeTransition> themeTransition;
    RecentItems recent;
    storage::Path findDirectory;  // Folder shown by the pending open dialog.
    ui::TextEditorOptions options{.markdown = true};
    Key zoomKey = Key::Unknown;
    uint64_t zoomStarted = 0;
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
        std::string previousFontQuery, fontError;
        std::vector<FontChoice> catalog;
        tn::Document sample;
        ui::TextEditor preview{sample};
        ui::FontId previewFont = 0;
    } settings;
    storage::Path settingsDirectory;
    PreferenceFile preferenceFile;
    NoteBrowser noteBrowser;
    NoteFileList noteFiles;
    struct Preview
    {
        std::string text;
        std::array<std::string, 3> lines;
        ui::FontId font;
        float width;
    };
    std::vector<Preview> previews;
    size_t nextPreview = 0;
    SessionStore session;
    float restoredWidth = 1080, restoredHeight = 720;
    ui::CommandRegistry commands;
    ui::CommandRegistry recentCommands;
    FindBar find{context, workspace, commands};
    TabStrip tabs{workspace, commands};
    WelcomePage welcome{commands};
    tn::InputExtensions inputExtensions{commands,
                                        [this](uint64_t id) -> ui::TextEditor* { auto tab = workspace.find(id); return tab && !tab->loading ? &tab->editor : nullptr; },
                                        [this] { auto tab = target(); return tab ? tab->id : 0; },
                                        [this](uint64_t id) {
                                            auto tab = visibleDocument();
                                            return editorFocus && tab && tab->id == id;
                                        }};
    bool recentPalette = false;
    LayoutScript layoutScript{commands};
    ui::Composition workbench;
    ui::CommandListState paletteState, listState;
    ui::ListState collectionList, importedList, folderList;
    std::vector<ui::ListItem> collectionItems, importedItems, folderItems;  // Borrowed by the lists until input is dispatched.
    ui::Rect editorBounds{}, sidebarBounds{}, searchBounds{}, menuButton{};
    ui::Rect promptSave{}, promptDiscard{}, promptCancel{};
    ui::TextInput renameInput;
    float width = 0, height = 0, pointerX = -1, pointerY = -1;
    Layout composition = Layout::Memo;
    std::array<LayoutState, layoutCount> layouts = defaultLayouts;
    std::string menu, menuRoot;
    storage::Path menuFolder;
    std::optional<ui::Rect> contextMenuAnchor;
    ui::MenuDefinition menuDefinition;
    size_t menuSelection = 0, collectionOffset = 0, importedOffset = 0;
    float noteFileScroll = 0, documentScroll = 0;
    ui::TextInput noteSearch;
    mutable std::string searchedQuery;
    mutable std::unordered_map<uint64_t, std::pair<uint64_t, bool>> searchHits;  // tab id -> (revision + 1, match)
    std::string collection = "all", previousQuery;
    Hint hint;
    WindowChrome chrome{commands};
    bool folderWorkspace = false;
    std::set<uint64_t> favorites;
    bool revealDocument = true;
    bool dark = false, commandList = false, palette = false, dialog = false;
    // Whether the visible document editor owns keyboard input; mirrors the context focus on "editor".
    bool editorFocus = false;
    bool composing = false, exiting = false, waitingSave = false;
    std::string suppressedInputText;
    bool suppressKeyText = false, textInputShift = false;
    uint64_t paletteTarget = 0, lastActive = 0, promptTarget = 0;
    Prompt prompt = Prompt::None;
    std::set<uint64_t> discarded;
    uint64_t nextLayoutPoll = 0;
};

}  // namespace tn
