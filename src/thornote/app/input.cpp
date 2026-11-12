#include "editor_app.h"
#include <thornote/platform/native_text_input.h>
#include <ui/ui.h>
#include <algorithm>
#include <cctype>

namespace tn
{
namespace
{
ui::Key uiKey(SDL_Keycode key)
{
    switch (key) {
        case SDLK_TAB: return ui::Key::Tab;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return ui::Key::Enter;
        case SDLK_SPACE: return ui::Key::Space;
        case SDLK_ESCAPE: return ui::Key::Escape;
        case SDLK_LEFT: return ui::Key::Left;
        case SDLK_RIGHT: return ui::Key::Right;
        case SDLK_UP: return ui::Key::Up;
        case SDLK_DOWN: return ui::Key::Down;
        case SDLK_HOME: return ui::Key::Home;
        case SDLK_END: return ui::Key::End;
        case SDLK_PAGEUP: return ui::Key::PageUp;
        case SDLK_PAGEDOWN: return ui::Key::PageDown;
        case SDLK_BACKSPACE: return ui::Key::Backspace;
        case SDLK_DELETE: return ui::Key::Delete;
        case SDLK_A: return ui::Key::A;
        case SDLK_C: return ui::Key::C;
        case SDLK_X: return ui::Key::X;
        case SDLK_V: return ui::Key::V;
        case SDLK_Z: return ui::Key::Z;
        case SDLK_Y: return ui::Key::Y;
        default: return ui::Key::Unknown;
    }
}
ui::platform::KeyModifiers modifiers(SDL_Keymod mods)
{
    return {bool(mods & SDL_KMOD_SHIFT), bool(mods & SDL_KMOD_CTRL), bool(mods & SDL_KMOD_ALT), bool(mods & SDL_KMOD_GUI)};
}
ui::KeyStroke stroke(const SDL_KeyboardEvent& event)
{
    auto mods = modifiers(event.mod);
    ui::KeyStroke result;
    result.shift = mods.shift;
    result.control = mods.control;
    result.alt = mods.alt;
    result.meta = mods.meta;
    result.primary = ui::platform::modifiers(mods).primary;
    result.repeated = event.repeat;
    result.down = event.type != SDL_EVENT_KEY_UP;
    auto key = event.scancode ? SDL_GetKeyFromScancode(event.scancode, event.mod, false) : event.key;
    if (!key) key = event.key;
    switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: result.key = "enter"; break;
        case SDLK_ESCAPE: result.key = "escape"; break;
        default: result.key = SDL_GetKeyName(key); break;
    }
    std::transform(result.key.begin(), result.key.end(), result.key.begin(), [](unsigned char c) { return c < 128 ? char(std::tolower(c)) : char(c); });
    if (key == SDLK_SPACE) result.key = " ";
    return result;
}
bool printable(const SDL_KeyboardEvent& event)
{
    return event.key >= 32 && event.key < 0x110000 && event.key != SDLK_DELETE;
}
size_t byteAt(std::string_view text, int characters)
{
    size_t offset = 0;
    while (characters-- > 0 && offset < text.size()) {
        ++offset;
        while (offset < text.size() && (uint8_t(text[offset]) & 0xc0) == 0x80)
            ++offset;
    }
    return offset;
}
}  // namespace

bool EditorApp::searchFocused() const
{
    if (context.input()) return true;
    auto active = workspace.active();
    return palette || findFocused() || (settings.active && (context.focused() == "settings/search" || context.focused() == "settings/fonts/search")) || ((composition == Layout::Note || commandList) && (!active || !editorFocus) && ((commandList && context.focused() == "sidebar/commands/search") || (composition == Layout::Note && !commandList && context.focused() == "notes/search")));
}

bool EditorApp::key(const SDL_KeyboardEvent& event)
{
    suppressedInputText.clear();
    suppressKeyText = false;
    auto keys = modifiers(event.mod);
    auto mods = ui::platform::modifiers(keys);
    textInputShift = keys.shift;
    if (dialog) return true;
    if (prompt != Prompt::None) {
        context.key(uiKey(event.key), {.shift = mods.shift});
        return true;
    }
    if (!menu.empty()) {
        if (event.key == SDLK_F1 || (mods.primary && mods.shift && event.key == SDLK_P)) openPalette();
        else context.key(uiKey(event.key), mods);
        return true;
    }
    if (composing) {
        if (auto input = context.input(); input && event.key == SDLK_ESCAPE) {
            input->composition("", 0);
            composing = false;
            return true;
        }
        if (editorFocus && uiKey(event.key) != ui::Key::Unknown) context.key(uiKey(event.key), mods);
        if (event.key != SDLK_ESCAPE) return false;
        composing = false;
    }
    if (event.key == SDLK_F1 || (mods.primary && mods.shift && event.key == SDLK_P)) {
        openPalette();
        return true;
    }
    if (auto input = context.input(); input && mods.primary) {
        switch (event.key) {
            case SDLK_C:
            case SDLK_X: {
                auto text = input->session.selectedText();
                if (!text.empty()) {
                    SDL_SetClipboardText(text.c_str());
                    if (event.key == SDLK_X) input->insert("");
                }
                return true;
            }
            case SDLK_V: {
                auto text = SDL_GetClipboardText();
                if (text) input->insert(text);
                SDL_free(text);
                return true;
            }
            case SDLK_A:
            case SDLK_Z:
            case SDLK_Y: context.key(uiKey(event.key), mods); return true;
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
    if (palette) {
        if (mods.primary && event.key == SDLK_V) {
            auto value = SDL_GetClipboardText();
            if (value) context.textInput(value);
            SDL_free(value);
        } else context.key(uiKey(event.key), mods);
        return true;
    }
    if (auto tab = visibleDocument(); tab && editorFocus && !settings.active && !searchFocused()) {
        bool nativeText = tab->editor.inputState().text;
        if (!nativeText || !printable(event) || keys.control || keys.alt || keys.meta) {
            auto input = stroke(event);
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
        const char* command = nullptr;
        switch (event.key) {
            case SDLK_N: command = "file.new"; break;
            case SDLK_O: command = "file.open"; break;
            case SDLK_P: command = "file.recent"; break;
            case SDLK_S: command = mods.shift ? "file.saveAs" : "file.save"; break;
            case SDLK_W: command = "file.close"; break;
            case SDLK_Q: command = "file.quit"; break;
            case SDLK_COMMA: command = "settings.open"; break;
            case SDLK_F: command = keys.alt ? "edit.replace" : "edit.find"; break;
#ifndef __APPLE__
            case SDLK_H: command = "edit.replace"; break;
#endif
            case SDLK_G: command = mods.shift ? "edit.find.previous" : "edit.find.next"; break;
            case SDLK_TAB: command = mods.shift ? "tab.previous" : "tab.next"; break;
            case SDLK_EQUALS:
            case SDLK_PLUS: command = "editor.font.increase"; break;
            case SDLK_MINUS: command = "editor.font.decrease"; break;
            case SDLK_0: command = "editor.font.reset"; break;
        }
        if (command) {
            commands.execute(command);
            return true;
        }
        if (searchFocused()) {
            if (event.key == SDLK_V) {
                auto value = SDL_GetClipboardText();
                if (value) context.textInput(value);
                SDL_free(value);
            }
            return true;
        }
        switch (event.key) {
            case SDLK_Z: command = mods.shift ? "edit.redo" : "edit.undo"; break;
            case SDLK_Y: command = "edit.redo"; break;
            case SDLK_A: command = "edit.selectAll"; break;
            case SDLK_C: command = "edit.copy"; break;
            case SDLK_X: command = "edit.cut"; break;
            case SDLK_V: command = "edit.paste"; break;
        }
        if (command) {
            commands.execute(command);
            return true;
        }
    }
    if (find.open && !settings.active) {
        if (event.key == SDLK_ESCAPE) {
            commands.execute("edit.find.close");
            return true;
        }
        if (event.key == SDLK_F3 || (findFocused() && (event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER))) {
            auto command = event.key != SDLK_F3 && findFocused() && context.focused() == "find/replacement" ? "edit.replace.current" : mods.shift ? "edit.find.previous"
                                                                                                                                                  : "edit.find.next";
            commands.execute(command);
            return true;
        }
    }
    if (settings.active) {
        context.key(uiKey(event.key), mods);
        return true;
    }
    if (welcomeVisible() && context.scrollKey("welcome/scroll", uiKey(event.key))) return true;
    if (visibleDocument() && editorFocus) {
        auto key = uiKey(event.key);
        // The typed character of Space arrives as text input.
        if (key == ui::Key::Unknown || key == ui::Key::Space) return false;
        auto navigation = ui::platform::navigation(key, keys);
        context.key(key, navigation);
        return true;
    }
    if (event.key == SDLK_ESCAPE) focusEditor(true);
    else context.key(uiKey(event.key), mods);
    return false;
}

bool EditorApp::nativeKeyDown(const SDL_KeyboardEvent& event)
{
    bool handled = key(event);
    syncInputExtensions();
    updateFind();
    return handled;
}

void EditorApp::nativeTextInput(const NativeTextEdit& edit)
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

void EditorApp::event(const SDL_Event& event, float sx, float sy)
{
    switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: requestQuit(); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (auto input = context.input()) input->composition("", 0);
            windowFocused = false;
            suppressedInputText.clear();
            composing = false;
            suppressKeyText = false;
            textInputShift = false;
            if (!menu.empty()) closeMenu();
            focusEditor(false);
            context.cancelPointer();
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            windowFocused = true;
            refreshNoteFiles(true);
            if (!searchFocused()) {
                bool focus = !palette && menu.empty() && prompt == Prompt::None && !dialog;
                focusEditor(focus);
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            key(event.key);
            break;
        case SDL_EVENT_KEY_UP:
            suppressedInputText.clear();
            suppressKeyText = false;
            textInputShift = false;
            if (!dialog && !palette && prompt == Prompt::None && menu.empty() && !composing && !settings.active) {
                if (auto tab = visibleDocument(); tab && editorFocus && (!tab->editor.inputState().text || !printable(event.key) || event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)))
                    inputExtensions.key(tab->id, stroke(event.key));
            }
            break;
        case SDL_EVENT_TEXT_INPUT:
            composing = false;
            if (prompt != Prompt::None || dialog || !menu.empty()) break;
            if (searchFocused()) context.textInput(event.text.text);
            else if (!settings.active) {
                if (auto tab = visibleDocument()) {
                    commitText(tab->id, event.text.text ? event.text.text : "");
                }
            }
            break;
        case SDL_EVENT_TEXT_EDITING:
            if (auto input = context.input()) {
                auto text = std::string_view(event.edit.text ? event.edit.text : "");
                composing = !text.empty();
                input->composition(text, byteAt(text, event.edit.start));
                break;
            }
            if (prompt == Prompt::None && !palette && !dialog && menu.empty()) {
                if (auto tab = visibleDocument(); tab && editorFocus) {
                    auto value = std::string_view(event.edit.text ? event.edit.text : "");
                    composing = !value.empty();
                    textInputShift = false;
                    prepareFont(*tab, value);
                    tab->editor.composition(value, byteAt(value, event.edit.start));
                }
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            pointerX = event.motion.x * sx;
            pointerY = event.motion.y * sy;
            context.pointerMove(pointerX, pointerY);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_RIGHT) {
                if (dialog || palette || prompt != Prompt::None || !menu.empty()) break;
                pointerX = event.button.x * sx;
                pointerY = event.button.y * sy;
                if (editorAt(pointerX, pointerY)) openEditorMenu(pointerX, pointerY);
                else context.contextMenu(pointerX, pointerY);
                break;
            }
            if (event.button.button != SDL_BUTTON_LEFT || dialog) break;
            pointerX = event.button.x * sx;
            pointerY = event.button.y * sy;
            if (event.button.clicks == 2 && customTitleBarVisible() && !windowFullscreen && windowDraggableAt(pointerX, pointerY)) {
                if (changeWindow) changeWindow(WindowAction::TitleBarDoubleClick);
                break;
            }
            context.pointerDown(pointerX, pointerY, ui::platform::modifiers(modifiers(SDL_GetModState())), event.button.clicks);
            // A press focuses the control under it, or nothing.
            focusEditor(context.focused() == "editor");
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button != SDL_BUTTON_LEFT) break;
            pointerX = event.button.x * sx;
            pointerY = event.button.y * sy;
            context.pointerMove(pointerX, pointerY);
            context.pointerUp(pointerX, pointerY);
            break;
        case SDL_EVENT_MOUSE_WHEEL: {
            float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
            context.wheel(event.wheel.y * direction, event.wheel.x * direction);
            break;
        }
    }
    syncInputExtensions();
    updateFind();
}

DocumentTab* EditorApp::textInputTarget() const
{
    auto tab = visibleDocument();
    return editorFocus && tab && !tab->loading && !tab->editor.options().readOnly && tab->editor.inputState().text ? tab : nullptr;
}

std::optional<ui::Rect> EditorApp::textInputArea()
{
    if (!windowFocused || dialog || prompt != Prompt::None || !menu.empty()) return {};
    if (auto input = context.input()) return input->caretBounds;
    if (palette) return ui::Rect{(width - std::min(550.0f, width - 32)) / 2 + 12, std::min(90.0f, height / 8) + (recentPalette ? 43 : 12), 1, 34};
    if (searchFocused()) {
        if (findFocused()) {
            auto r = context.focused() == "find/query" ? find.queryBounds : find.replacementBounds;
            return ui::Rect{r.x + 8, r.y, 1, r.h};
        }
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

}  // namespace tn
