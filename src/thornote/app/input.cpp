#include "editor_app.h"
#include <thornote/platform/clipboard.h>
#include <ui/ui.h>
#include <algorithm>
#include <cmath>
#include <exception>

namespace tn
{
namespace
{
ui::Key uiKey(Key key)
{
    switch (key) {
        case Key::Tab: return ui::Key::Tab;
        case Key::Enter:
        case Key::KeypadEnter: return ui::Key::Enter;
        case Key::Space: return ui::Key::Space;
        case Key::Escape: return ui::Key::Escape;
        case Key::Left: return ui::Key::Left;
        case Key::Right: return ui::Key::Right;
        case Key::Up: return ui::Key::Up;
        case Key::Down: return ui::Key::Down;
        case Key::Home: return ui::Key::Home;
        case Key::End: return ui::Key::End;
        case Key::PageUp: return ui::Key::PageUp;
        case Key::PageDown: return ui::Key::PageDown;
        case Key::Backspace: return ui::Key::Backspace;
        case Key::Delete: return ui::Key::Delete;
        default: break;
    }
    switch (uint32_t(key)) {
        case 'a': return ui::Key::A;
        case 'c': return ui::Key::C;
        case 'x': return ui::Key::X;
        case 'v': return ui::Key::V;
        case 'z': return ui::Key::Z;
        case 'y': return ui::Key::Y;
        default: return ui::Key::Unknown;
    }
}
ui::KeyStroke stroke(const KeyEvent& event, bool down)
{
    const auto& keys = event.modifiers;
    return {event.name, keys.shift, keys.control, keys.alt, keys.meta, ui::platform::modifiers(keys).primary, event.repeat, down};
}
bool printable(const KeyEvent& event)
{
    auto key = uint32_t(event.key);
    return key >= 32 && key < 0x110000 && event.key != Key::Delete;
}
// Fields ignore clipboard failures.
std::string clipboardText()
{
    try {
        return readClipboardText();
    } catch (const std::exception&) {
        return {};
    }
}
}  // namespace

bool EditorApp::searchFocused() const
{
    if (context.input()) return true;
    auto active = workspace.active();
    return palette || (!settings.active && find.focused()) || (settings.active && (context.focused() == "settings/search" || context.focused() == "settings/fonts/search")) || ((composition == Layout::Note || commandList) && (!active || !editorFocus) && ((commandList && context.focused() == "sidebar/commands/search") || (composition == Layout::Note && !commandList && context.focused() == "notes/search")));
}

void EditorApp::zoom(int direction, const KeyEvent& event)
{
    if (!event.repeat || zoomKey != event.key || event.time < zoomStarted) {
        zoomStarted = event.time;
        zoomKey = event.key;
    }
    auto step = 1 + std::min<uint64_t>((event.time - zoomStarted) / 300, 5);
    setFontSize(std::max(options.fontSize / 2, options.fontSize + direction * static_cast<float>(step)));
}

bool EditorApp::key(const KeyEvent& event)
{
    suppressedInputText.clear();
    suppressKeyText = false;
    const auto& keys = event.modifiers;
    auto mods = ui::platform::modifiers(keys);
    textInputShift = keys.shift;
    if (dialog) return true;
    if (prompt != Prompt::None && prompt != Prompt::Rename) {
        context.key(uiKey(event.key), {.shift = mods.shift});
        return true;
    }
    if (!menu.empty()) {
        if (event.key == Key::F1 || (mods.primary && mods.shift && event.key == Key('p'))) openPalette();
        else context.key(uiKey(event.key), mods);
        return true;
    }
    if (composing) {
        if (auto input = context.input(); input && event.key == Key::Escape) {
            input->composition("", 0);
            composing = false;
            return true;
        }
        if (editorFocus && uiKey(event.key) != ui::Key::Unknown) context.key(uiKey(event.key), mods);
        if (event.key != Key::Escape) return false;
        composing = false;
    }
    if (event.key == Key::F1 || (mods.primary && mods.shift && event.key == Key('p'))) {
        openPalette();
        return true;
    }
    if (auto input = context.input(); input && mods.primary) {
        switch (uint32_t(event.key)) {
            case 'c':
            case 'x': {
                if (event.key == Key('x') && mods.shift) return true;
                auto text = input->session.selectedText();
                if (!text.empty()) {
                    try {
                        writeClipboardText(text);
                    } catch (const std::exception&) {
                    }
                    if (event.key == Key('x')) input->insert("");
                }
                return true;
            }
            case 'v': input->insert(clipboardText()); return true;
            case 'a':
            case 'z':
            case 'y': context.key(uiKey(event.key), mods); return true;
        }
    }
    if (searchFocused() && printable(event) && !mods.primary && !keys.control) return false;
    if (context.input()) {
        auto key = uiKey(event.key);
        if (key == ui::Key::Left || key == ui::Key::Right || key == ui::Key::Home || key == ui::Key::End || key == ui::Key::Backspace || key == ui::Key::Delete) {
            auto navigation = ui::platform::navigation(key, keys);
            context.key(key, navigation);
            return true;
        }
    }
    if (prompt == Prompt::Rename) {
        context.key(uiKey(event.key), mods);
        return true;
    }
    if (palette) {
        if (mods.primary && event.key == Key('v')) context.textInput(clipboardText());
        else context.key(uiKey(event.key), mods);
        return true;
    }
    if (auto tab = visibleDocument(); tab && editorFocus && !settings.active && !searchFocused()) {
        bool nativeText = tab->editor.inputState().text;
        if (!nativeText || !printable(event) || keys.control || keys.alt || keys.meta) {
            auto input = stroke(event, true);
            auto result = inputExtensions.key(tab->id, input);
            if (result != ui::InputResult::Pass) {
                suppressKeyText = printable(event) && (keys.control || keys.alt || keys.meta);
                if (!nativeText && printable(event) && !keys.control && !keys.alt && !keys.meta) {
                    suppressedInputText = input.key;
                    if (input.shift && suppressedInputText.size() == 1 && suppressedInputText[0] >= 'a' && suppressedInputText[0] <= 'z') suppressedInputText[0] += 'A' - 'a';
                }
                return true;
            }
        }
    }
    if (mods.primary) {
        if (!searchFocused() && markdownShortcut(event)) return true;
        const char* command = nullptr;
        switch (uint32_t(event.key)) {
            case 'n': command = "file.new"; break;
            case 'o': command = "file.open"; break;
            case 'p': command = "file.recent"; break;
            case 's': command = mods.shift ? "file.saveAs" : "file.save"; break;
            case 'w': command = "file.close"; break;
            case 'q': command = "file.quit"; break;
            case ',': command = "settings.open"; break;
            case 'f': command = keys.alt ? "edit.replace" : "edit.find"; break;
#ifndef __APPLE__
            case 'h': command = "edit.replace"; break;
#endif
            case 'g': command = mods.shift ? "edit.find.previous" : "edit.find.next"; break;
            case uint32_t(Key::Tab): command = mods.shift ? "tab.previous" : "tab.next"; break;
            case '=':
            case uint32_t(Key::KeypadPlus):
            case '+': zoom(1, event); return true;
            case uint32_t(Key::KeypadMinus):
            case '-': zoom(-1, event); return true;
            case '0': command = "editor.font.reset"; break;
        }
        if (command) {
            commands.execute(command);
            return true;
        }
        if (searchFocused()) {
            if (event.key == Key('v')) context.textInput(clipboardText());
            return true;
        }
        switch (uint32_t(event.key)) {
            case 'z': command = mods.shift ? "edit.redo" : "edit.undo"; break;
            case 'y': command = "edit.redo"; break;
            case 'a': command = "edit.selectAll"; break;
            case 'c': command = "edit.copy"; break;
            case 'x':
                if (!mods.shift) command = "edit.cut";
                break;
            case 'v': command = "edit.paste"; break;
        }
        if (command) {
            commands.execute(command);
            return true;
        }
    }
    if (auto command = settings.active ? nullptr : find.command(event.key, mods.shift)) {
        commands.execute(command);
        return true;
    }
    if (settings.active) {
        context.key(uiKey(event.key), mods);
        return true;
    }
    if (welcomeVisible() && welcome.scrollKey(context, uiKey(event.key))) return true;
    if (visibleDocument() && editorFocus) {
        if (printable(event) && !mods.primary && !keys.control) return false;
        auto key = uiKey(event.key);
        // The typed character of Space arrives as text input.
        if (key == ui::Key::Unknown || key == ui::Key::Space) return false;
        auto navigation = ui::platform::navigation(key, keys);
        context.key(key, navigation);
        return true;
    }
    if (event.key == Key::Escape) focusEditor(true);
    else context.key(uiKey(event.key), mods);
    return false;
}

bool EditorApp::sync(bool changed)
{
    changed |= syncInputExtensions();
    updateFind();
    return changed;
}

bool EditorApp::keyDown(const KeyEvent& event)
{
    bool handled = key(event);
    sync();
    return handled;
}

bool EditorApp::keyUp(const KeyEvent& event)
{
    const auto& keys = event.modifiers;
    if (event.key == zoomKey || !ui::platform::modifiers(keys).primary) zoomKey = Key::Unknown;
    suppressedInputText.clear();
    suppressKeyText = false;
    textInputShift = false;
    // A release reaches the frame only through an input extension.
    bool changed = false;
    if (!dialog && !palette && prompt == Prompt::None && menu.empty() && !composing && !settings.active) {
        if (auto tab = visibleDocument(); tab && editorFocus && (!tab->editor.inputState().text || !printable(event) || keys.control || keys.alt || keys.meta)) {
            inputExtensions.key(tab->id, stroke(event, false));
            changed = true;
        }
    }
    return sync(changed);
}

void EditorApp::edit(const NativeTextEdit& edit)
{
    if (auto input = context.input()) {
        if (!textInputArea() || edit.document != reinterpret_cast<uintptr_t>(&input->document)) return;
        input->session.select(edit.begin, edit.end);
        composing = edit.composition && !edit.text.empty();
        if (edit.composition) input->composition(edit.text, edit.cursor);
        else input->insert(edit.text);
        updateFind();
        return;
    }
    auto tab = textInputTarget();
    if (!textInputArea() || !tab || tab->id != edit.document) return;
    auto selection = tab->editor.session().selection();
    if (selection.start() != edit.begin || selection.end() != edit.end) tab->editor.select(edit.begin, edit.end);
    composing = edit.composition && !edit.text.empty();
    if (edit.composition) {
        textInputShift = false;
        prepareFont(*tab, edit.text);
        tab->editor.composition(edit.text, edit.cursor);
    } else commitText(tab->id, edit.text);
    syncInputExtensions();
    updateFind();
}

void EditorApp::commitText(uint64_t id, std::string_view text)
{
    auto tab = textInputTarget();
    if (!tab || tab->id != id) return;
    tab->editor.composition("", 0);
    if (suppressKeyText) {
        suppressKeyText = false;
        return;
    }
    if (!suppressedInputText.empty()) {
        bool consumed = text == suppressedInputText;
        suppressedInputText.clear();
        if (consumed) return;
    }
    auto result = inputExtensions.text(id, text, textInputShift);
    textInputShift = false;
    if (result == ui::InputResult::Pass && textInputTarget() == tab) context.textInput(text);
}

void EditorApp::text(std::string_view committed)
{
    composing = false;
    if ((prompt == Prompt::None || prompt == Prompt::Rename) && !dialog && menu.empty()) {
        if (searchFocused()) context.textInput(committed);
        else if (!settings.active) {
            if (auto tab = visibleDocument()) commitText(tab->id, committed);
        }
    }
    sync();
}

void EditorApp::compose(std::string_view text, size_t cursor)
{
    if (auto input = context.input()) {
        composing = !text.empty();
        input->composition(text, cursor);
    } else if (prompt == Prompt::None && !palette && !dialog && menu.empty()) {
        if (auto tab = visibleDocument(); tab && editorFocus) {
            composing = !text.empty();
            textInputShift = false;
            prepareFont(*tab, text);
            tab->editor.composition(text, cursor);
        }
    }
    sync();
}

bool EditorApp::pointerMove(float x, float y)
{
    pointerX = x;
    pointerY = y;
    return sync(context.pointerMove(pointerX, pointerY));
}

void EditorApp::pointerDown(const PointerEvent& event)
{
    if (event.button == Button::Right) {
        if (!dialog && !palette && prompt == Prompt::None && menu.empty()) {
            pointerX = event.x;
            pointerY = event.y;
            if (!editorAt(pointerX, pointerY)) context.contextMenu(pointerX, pointerY);
            else if (auto tab = visibleDocument(); tab && !tab->loading) {
                context.cancelPointer();
                // A secondary click on plain text selects the word under it, as a double click does.
                if (tab->editor.session().selection().empty()) {
                    context.pointerDown(pointerX, pointerY, {}, 2);
                    context.cancelPointer();
                }
                focusEditor(true);
                if (!showTextMenu || !showTextMenu(pointerX, pointerY, tab->editor.session().selectedText(), commands)) {
                    contextMenuAnchor = menuButton = ui::Rect{pointerX, pointerY, 1, 1};
                    openMenu("editor-menu");
                }
            }
        }
    } else if (event.button == Button::Left && !dialog) {
        pointerX = event.x;
        pointerY = event.y;
        if (event.clicks == 2 && customTitleBarVisible() && !chrome.window.fullscreen && draggable(pointerX, pointerY)) {
            if (changeWindow) changeWindow(WindowAction::TitleBarDoubleClick);
        } else {
            context.pointerDown(pointerX, pointerY, ui::platform::modifiers(event.modifiers), event.clicks);
            // A press focuses the control under it, or nothing.
            focusEditor(context.focused() == "editor");
        }
    }
    sync();
}

void EditorApp::pointerUp(const PointerEvent& event)
{
    if (event.button == Button::Left) {
        pointerX = event.x;
        pointerY = event.y;
        context.pointerMove(pointerX, pointerY);
        context.pointerUp(pointerX, pointerY);
    }
    sync();
}

void EditorApp::wheel(const WheelEvent& event)
{
    if (event.y != 0 && std::isfinite(event.y) && event.modifiers.control && editorAt(event.pointerX, event.pointerY))
        setFontSize(std::max(options.fontSize / 2, options.fontSize + event.y));
    else context.wheel(event.y, event.x);
    sync();
}

void EditorApp::focus(bool focused)
{
    if (focused) {
        chrome.focused = true;
        refreshNoteFiles(true);
        if (!searchFocused()) focusEditor(!palette && menu.empty() && prompt == Prompt::None && !dialog);
    } else {
        zoomKey = Key::Unknown;
        if (auto input = context.input()) input->composition("", 0);
        chrome.focused = false;
        suppressedInputText.clear();
        composing = false;
        suppressKeyText = false;
        textInputShift = false;
        if (!menu.empty()) closeMenu();
        focusEditor(false);
        context.cancelPointer();
    }
    sync();
}

void EditorApp::close()
{
    requestQuit();
    sync();
}

DocumentTab* EditorApp::textInputTarget() const
{
    auto tab = visibleDocument();
    return editorFocus && tab && !tab->loading && !tab->editor.options().readOnly && tab->editor.inputState().text ? tab : nullptr;
}

std::optional<ui::Rect> EditorApp::textInputArea()
{
    if (!chrome.focused || dialog || (prompt != Prompt::None && prompt != Prompt::Rename) || !menu.empty()) return {};
    if (auto input = context.input()) return input->caretBounds;
    if (prompt == Prompt::Rename) return {};
    if (palette) return ui::Rect{(width - std::min(550.0f, width - 32)) / 2 + 12, std::min(90.0f, height / 8) + (recentPalette ? 43 : 12), 1, 34};
    if (searchFocused()) {
        if (!settings.active && find.focused()) return find.caret();
        if (settings.active && (context.focused() == "settings/search" || context.focused() == "settings/fonts/search")) {
            auto r = context.focused() == "settings/fonts/search" ? settings.fontSearchBounds : settings.searchBounds;
            return ui::Rect{r.x + 38, r.y, 1, r.h};
        }
        if (context.focused() == "notes/search") return ui::Rect{searchBounds.x + 38, searchBounds.y, 1, searchBounds.h};
        return ui::Rect{sidebarBounds.x + 46, sidebarBounds.y + 42, 1, 34};
    }
    if (auto tab = textInputTarget()) {
        auto caret = tab->editor.caretRect();
        caret.x += editorBounds.x;
        caret.y += editorBounds.y;
        return caret;
    }
    return {};
}

std::optional<TextInputState> EditorApp::textInput()
{
    auto area = textInputArea();
    if (!area) return {};
    if (auto input = context.input()) {
        auto selection = input->session.selection();
        return TextInputState{*area, reinterpret_cast<uintptr_t>(&input->document), &input->document, selection.anchor, selection.caret};
    }
    if (auto tab = textInputTarget()) {
        auto selection = tab->editor.session().selection();
        return TextInputState{*area, tab->id, &tab->document, selection.anchor, selection.caret};
    }
    return TextInputState{*area};
}

}  // namespace tn
