#include <thornote/platform/native_text_input.h>
#include <ui/ui.h>
#import <AppKit/AppKit.h>
#include <algorithm>
#include <deque>

@interface ThorNoteInputView : NSView <NSTextInputClient> {
@public
    tn::NativeTextInput::Impl* owner;
    NSMutableString* content;
    NSString* marked;
    NSRange selection, replacement, markedSelection;
    SDL_Rect inputArea;
    bool handledKey, commandKey;
}
@end

// A read-only selection mirror supplies AppKit's viewer actions without a second editing history.
@interface ThorNoteContextView : NSTextView <NSMenuItemValidation, NSViewContentSelectionInfo> {
@public
    tn::NativeTextInput::Impl* owner;
    NSRect anchor;
}
- (void)performEditorCommand:(NSMenuItem*)item;
@end

namespace tn
{
struct NativeTextInput::Impl
{
    SDL_Window* window = nullptr;
    NSWindow* native = nil;
    ThorNoteInputView* view = nil;
    ThorNoteContextView* contextView = nil;
    const ui::CommandRegistry* menuCommands = nullptr;
    bool menuOpen = false;
    struct MenuEvent
    {
        uint32_t type;
        uint64_t timestamp;
    };
    std::deque<MenuEvent> menuEvents;
    uint32_t eventType = 0;
    uint64_t document = 0, revision = 0;
    std::string snapshot;
    bool active = false, watching = false;
    struct Key
    {
        SDL_KeyboardEvent event;
        bool consumed = false;
    };
    std::deque<Key> keys;
    std::function<void(const NativeTextEdit&)> apply;
    std::function<void(const SDL_KeyboardEvent&)> key;
    std::function<bool(const SDL_KeyboardEvent&)> beforeKey;

    void edit(NativeTextEdit edit)
    {
        edit.document = document;
        view->handledKey = true;
        apply(edit);
        // Candidate selection and dictation can change text outside keyDown.
        SDL_Event event{};
        event.type = eventType;
        event.user.data1 = this;
        SDL_PushEvent(&event);
    }
    static bool SDLCALL watch(void* data, SDL_Event* event)
    {
        auto self = static_cast<Impl*>(data);
        bool input = event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP || event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_TEXT_EDITING || event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_MOUSE_WHEEL;
        if (SDL_IsMainThread() && self->menuOpen && input && event->key.windowID == SDL_GetWindowID(self->window)) {
            self->menuEvents.push_back({event->type, event->common.timestamp});
            return true;
        }
        if (SDL_IsMainThread() && self->active && event->type == SDL_EVENT_KEY_DOWN && event->key.windowID == SDL_GetWindowID(self->window))
            self->keys.push_back({event->key});
        return true;
    }
};
}  // namespace tn

namespace
{
NSString* string(std::string_view text)
{
    return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding] ?: @"";
}
size_t byteOffset(NSString* text, NSUInteger offset)
{
    return [[text substringToIndex:std::min(offset, text.length)] lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
}
NSUInteger utf16Offset(std::string_view text, size_t offset)
{
    return string(text.substr(0, std::min(offset, text.size()))).length;
}
NSRange clamped(NSRange range, NSUInteger length)
{
    range.location = std::min(range.location, length);
    range.length = std::min(range.length, length - range.location);
    return range;
}

const char* textMenuCommand(SEL action)
{
    if (action == @selector(cut:)) return "edit.cut";
    if (action == @selector(copy:)) return "edit.copy";
    if (action == @selector(paste:) || action == @selector(pasteAsPlainText:)) return "edit.paste";
    if (action == @selector(selectAll:)) return "edit.selectAll";
    if (action == @selector(undo:)) return "edit.undo";
    if (action == @selector(redo:)) return "edit.redo";
    return nullptr;
}

bool textFormattingMenu(NSMenu* menu)
{
    for (NSMenuItem* item in menu.itemArray) {
        if (item.action == @selector(orderFrontFontPanel:) || item.action == @selector(changeFont:) || item.action == @selector(showGuessPanel:) || item.action == @selector(checkSpelling:) || item.action == @selector(orderFrontSubstitutionsPanel:) || item.action == @selector(toggleAutomaticQuoteSubstitution:)) return true;
        if (item.submenu && textFormattingMenu(item.submenu)) return true;
    }
    return false;
}

void targetTextMenu(NSMenu* menu, NSView* view)
{
    for (NSMenuItem* item in menu.itemArray) {
        if (!item.target && item.action && [view respondsToSelector:item.action]) item.target = view;
        if (item.submenu) targetTextMenu(item.submenu, view);
    }
}
}  // namespace

@implementation ThorNoteContextView
- (NSView*)hitTest:(NSPoint)point
{
    return nil;
}
- (NSRect)selectionAnchorRect
{
    return anchor;
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    if (actual) *actual = self.selectedRange;
    return [self.window convertRectToScreen:[self convertRect:anchor toView:nil]];
}
- (void)performEditorCommand:(NSMenuItem*)item
{
    if (owner && owner->menuCommands) owner->menuCommands->execute([(NSString*)item.representedObject UTF8String]);
}
- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)item
{
    if (item.action == @selector(performEditorCommand:)) {
        if (!owner || !owner->menuCommands) return NO;
        auto commandId = [(NSString*)[(NSMenuItem*)item representedObject] UTF8String];
        for (const auto& command : owner->menuCommands->commands())
            if (command.id == commandId) return command.enabled();
        return NO;
    }
    return [super validateUserInterfaceItem:item];
}
- (BOOL)validateMenuItem:(NSMenuItem*)item
{
    return [self validateUserInterfaceItem:item];
}
@end

@implementation ThorNoteInputView
- (BOOL)acceptsFirstResponder
{
    return YES;
}
- (NSView*)hitTest:(NSPoint)point
{
    return nil;
}
- (BOOL)hasMarkedText
{
    return marked.length != 0;
}
- (NSRange)markedRange
{
    return self.hasMarkedText ? NSMakeRange(replacement.location, marked.length) : NSMakeRange(NSNotFound, 0);
}
- (NSRange)selectedRange
{
    return self.hasMarkedText ? NSMakeRange(replacement.location + markedSelection.location, markedSelection.length) : selection;
}
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
    return @[];
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    return self.selectedRange.location;
}
- (void)doCommandBySelector:(SEL)selector
{
    if (selector == @selector(cancelOperation:) && self.hasMarkedText) {
        [self setMarkedText:@"" selectedRange:NSMakeRange(0, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
        [self.inputContext discardMarkedText];
        commandKey = true;
    } else commandKey = true;
}

// Translate ranges in the virtual text (including preedit) to committed text.
- (NSRange)committedRange:(NSRange)range
{
    if (range.location == NSNotFound) return self.hasMarkedText ? replacement : selection;
    if (self.hasMarkedText) {
        auto map = [&](NSUInteger offset, bool end) {
            if (offset <= replacement.location) return offset;
            if (offset >= replacement.location + marked.length) return offset - marked.length + replacement.length;
            return replacement.location + (end ? replacement.length : 0);
        };
        auto begin = map(range.location, false), end = map(NSMaxRange(range), true);
        range = NSMakeRange(begin, end - begin);
    }
    return clamped(range, content.length);
}
- (void)insertText:(id)value replacementRange:(NSRange)range
{
    if (!owner || !owner->active) return;
    NSString* text = [value isKindOfClass:NSAttributedString.class] ? [value string] : value;
    range = [self committedRange:range];
    tn::NativeTextEdit edit;
    edit.begin = byteOffset(content, range.location);
    edit.end = byteOffset(content, NSMaxRange(range));
    edit.text = text.UTF8String;
    [content replaceCharactersInRange:range withString:text];
    selection = NSMakeRange(range.location + text.length, 0);
    marked = nil;
    owner->edit(std::move(edit));
}
- (void)setMarkedText:(id)value selectedRange:(NSRange)selected replacementRange:(NSRange)range
{
    if (!owner || !owner->active) return;
    NSString* text = [value isKindOfClass:NSAttributedString.class] ? [value string] : value;
    replacement = [self committedRange:range];
    selection = replacement;
    marked = [text copy];
    markedSelection = clamped(selected, marked.length);
    tn::NativeTextEdit edit;
    edit.begin = byteOffset(content, replacement.location);
    edit.end = byteOffset(content, NSMaxRange(replacement));
    edit.text = marked.UTF8String;
    edit.cursor = byteOffset(marked, markedSelection.location);
    edit.composition = true;
    owner->edit(std::move(edit));
}
- (void)unmarkText
{
    if (self.hasMarkedText) [self insertText:marked replacementRange:NSMakeRange(NSNotFound, 0)];
}
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    NSMutableString* text = [content mutableCopy];
    if (self.hasMarkedText) [text replaceCharactersInRange:replacement withString:marked];
    if (range.location == NSNotFound || range.location > text.length) {
        if (actual) *actual = NSMakeRange(NSNotFound, 0);
        return nil;
    }
    range = clamped(range, text.length);
    if (actual) *actual = range;
    return [[NSAttributedString alloc] initWithString:[text substringWithRange:range]];
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    if (actual) *actual = self.selectedRange;
    auto rect = NSMakeRect(inputArea.x, self.window.contentView.bounds.size.height - inputArea.y - inputArea.h, inputArea.w, inputArea.h);
    return [self.window convertRectToScreen:rect];
}
- (void)keyDown:(NSEvent*)event
{
    if (!owner || !owner->active) return;
    auto key = !owner->keys.empty() && owner->keys.back().event.raw == event.keyCode ? &owner->keys.back() : nullptr;
    if (!key) return;
    key->consumed = true;
    handledKey = commandKey = false;
    bool dispatched = false;
    if (!self.hasMarkedText && owner->beforeKey) {
        dispatched = true;
        if (owner->beforeKey(key->event) || !owner->active) return;
    }
    if (event.modifierFlags & NSEventModifierFlagCommand) commandKey = true;
    else [self interpretKeyEvents:@[ event ]];
    // AppKit can pump several native keys before SDL_PollEvent returns. Apply
    // text and commands together now, so the next IME query sees the new caret.
    // The queued SDL copy is consumed by NativeTextInput::event().
    if (!dispatched && (!handledKey || commandKey)) owner->key(key->event);
}
- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    if (owner && owner->active && (event.modifierFlags & NSEventModifierFlagCommand)) {
        [self keyDown:event];
        return YES;
    }
    return NO;
}
@end

namespace tn
{
NativeTextInput::NativeTextInput() :
    impl(std::make_unique<Impl>()) {}
NativeTextInput::~NativeTextInput()
{
    stop();
}

bool NativeTextInput::initialize(SDL_Window* window, std::function<void(const NativeTextEdit&)> apply, std::function<void(const SDL_KeyboardEvent&)> key,
                                 std::function<bool(const SDL_KeyboardEvent&)> beforeKey)
{
    stop();
    auto native = (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!native) return false;  // SDL's dummy driver and other platforms use SDL text events.
    auto& state = *impl;
    state.eventType = SDL_RegisterEvents(1);
    if (state.eventType == uint32_t(-1)) return false;
    state.window = window;
    state.apply = std::move(apply);
    state.key = std::move(key);
    state.beforeKey = std::move(beforeKey);
    state.native = native;
    state.view = [[ThorNoteInputView alloc] initWithFrame:NSZeroRect];
    state.view->owner = &state;
    state.view->content = [NSMutableString string];
    [native.contentView addSubview:state.view];
    state.watching = SDL_AddEventWatch(Impl::watch, &state);
    if (!state.watching) {
        stop();
        return false;
    }
    return true;
}

void NativeTextInput::stop()
{
    auto& state = *impl;
    if (state.watching) SDL_RemoveEventWatch(Impl::watch, &state);
    state.watching = false;
    state.active = false;
    if (state.contextView) {
        state.contextView->owner = nullptr;
        if (state.native.firstResponder == state.contextView) [state.native makeFirstResponder:state.native.contentView];
        [state.contextView removeFromSuperview];
    }
    state.contextView = nil;
    state.menuCommands = nullptr;
    state.menuOpen = false;
    state.menuEvents.clear();
    if (state.view) {
        state.view->owner = nullptr;
        state.view->marked = nil;
        [state.view.inputContext discardMarkedText];
        if (state.native.firstResponder == state.view) [state.native makeFirstResponder:state.native.contentView];
        [state.view removeFromSuperview];
    }
    state.view = nil;
    state.native = nil;
    state.window = nullptr;
    state.keys.clear();
    state.apply = {};
    state.key = {};
    state.beforeKey = {};
}

void NativeTextInput::update(uint64_t document, const tn::Document* text, size_t anchor, size_t caret, const SDL_Rect* area)
{
    auto& state = *impl;
    if (!state.view) return;
    bool active = document && text && area;
    if (!active || state.document != document) {
        state.active = false;
        state.view->marked = nil;
        [state.view.inputContext discardMarkedText];
        // Keep first responder across document switches: macOS rebuilds its input source cursor UI each
        // time the input context is reactivated, which stalled the app when tabs closed in quick succession.
        if (!active && state.native.firstResponder == state.view) [state.native makeFirstResponder:state.native.contentView];
    }
    if (!active) return;
    bool changed = !state.active || state.document != document || state.revision != text->revision();
    state.document = document;
    state.active = true;
    state.view->inputArea = *area;
    const auto& value = text->text();
    bool reset = changed && value != state.view->content.UTF8String;
    if (state.view.hasMarkedText) {
        auto range = state.view->replacement;
        bool moved = std::min(anchor, caret) != byteOffset(state.view->content, range.location) || std::max(anchor, caret) != byteOffset(state.view->content, NSMaxRange(range));
        reset |= moved;
    }
    // Korean input methods can retain a syllable after committing unmarked text.
    // External edits such as Backspace must end that session before a source switch.
    if (reset) {
        state.view->marked = nil;
        [state.view.inputContext discardMarkedText];
    }
    if (changed) {
        state.snapshot = value;
        state.view->content = [string(state.snapshot) mutableCopy];
        state.revision = text->revision();
    }
    if (!state.view.hasMarkedText) {
        auto first = utf16Offset(state.snapshot, std::min(anchor, caret));
        auto last = utf16Offset(state.snapshot, std::max(anchor, caret));
        state.view->selection = NSMakeRange(first, last - first);
    }
    if (SDL_TextInputActive(state.window)) SDL_StopTextInput(state.window);
    if (!state.menuOpen) {
        if (state.native.firstResponder != state.view) [state.native makeFirstResponder:state.view];
        if (state.native.isKeyWindow && NSTextInputContext.currentInputContext != state.view.inputContext) [state.view.inputContext activate];
    }
}

bool NativeTextInput::contextMenu(float x, float y, std::string_view selection, const ui::CommandRegistry& commands)
{
    auto& state = *impl;
    if (!state.native || state.menuOpen) return false;
    if (!state.contextView) {
        state.contextView = [[ThorNoteContextView alloc] initWithFrame:state.native.contentView.bounds];
        state.contextView->owner = &state;
        state.contextView.editable = NO;
        state.contextView.richText = NO;
        state.contextView.usesFontPanel = NO;
        state.contextView.alphaValue = 0;
        [state.native.contentView addSubview:state.contextView];
    }
    auto view = state.contextView;
    view.frame = state.native.contentView.bounds;
    view.string = string(selection);
    view.selectedRange = NSMakeRange(0, view.string.length);
    auto point = NSMakePoint(x, state.native.contentView.bounds.size.height - y);
    view->anchor = [view convertRect:NSMakeRect(point.x, point.y, 1, 1) fromView:nil];
    auto mouseEvent = [&](NSPoint position) {
        return [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
                                  location:position
                             modifierFlags:0
                                 timestamp:NSProcessInfo.processInfo.systemUptime
                              windowNumber:state.native.windowNumber
                                   context:nil
                               eventNumber:0
                                clickCount:1
                                  pressure:1];
    };
    // Build the system menu over the mirrored selection, then show it at the real pointer.
    auto query = [view convertPoint:NSMakePoint(8, 8) toView:nil];
    NSMenu* menu = [view menuForEvent:mouseEvent(query)];
    if (!menu) return false;
    NSMutableDictionary* images = [NSMutableDictionary dictionary];
    NSMutableDictionary* titles = [NSMutableDictionary dictionary];
    for (NSMenuItem* item in [menu.itemArray copy]) {
        if (auto command = textMenuCommand(item.action)) {
            auto key = string(command);
            if (item.image && !images[key]) images[key] = item.image;
            if (!titles[key]) titles[key] = item.title;
            [menu removeItem:item];
        } else if (item.submenu && textFormattingMenu(item.submenu)) [menu removeItem:item];
    }
    struct Entry
    {
        const char* title;
        const char* command;
        const char* key;
        bool shift;
    };
    const Entry entries[]{
        {"Undo", "edit.undo", "z", false}, {"Redo", "edit.redo", "z", true}, {"Cut", "edit.cut", "x", false}, {"Copy", "edit.copy", "c", false}, {"Paste", "edit.paste", "v", false}, {"Select All", "edit.selectAll", "a", false}, {"Find…", "edit.find", "f", false}, {"Replace…", "edit.replace", "", false}};
    NSInteger index = 0;
    for (const auto& entry : entries) {
        if (index == 2) [menu insertItem:NSMenuItem.separatorItem atIndex:index++];
        auto key = string(entry.command);
        auto item = [[NSMenuItem alloc] initWithTitle:titles[key] ?: string(entry.title)
                                               action:@selector(performEditorCommand:)
                                        keyEquivalent:string(entry.key)];
        item.keyEquivalentModifierMask = NSEventModifierFlagCommand | (entry.shift ? NSEventModifierFlagShift : 0);
        item.representedObject = key;
        item.target = view;
        item.image = images[key];
        [menu insertItem:item atIndex:index++];
    }
    [menu insertItem:NSMenuItem.separatorItem atIndex:index];
    for (NSInteger i = menu.numberOfItems - 1; i >= 0; --i) {
        if ([menu itemAtIndex:i].separatorItem && (i == 0 || i == menu.numberOfItems - 1 || [menu itemAtIndex:i - 1].separatorItem)) [menu removeItemAtIndex:i];
    }
    targetTextMenu(menu, view);
    state.menuCommands = &commands;
    auto responder = state.native.firstResponder;
    state.menuOpen = true;
    @try {
        [state.native makeFirstResponder:view];
        [NSMenu popUpContextMenu:menu withEvent:mouseEvent(point) forView:view];
    } @finally {
        [state.native makeFirstResponder:responder];
        state.menuOpen = false;
        state.menuCommands = nullptr;
    }
    return true;
}

bool NativeTextInput::event(const SDL_Event& event)
{
    auto& state = *impl;
    auto menuEvent = std::find_if(state.menuEvents.begin(), state.menuEvents.end(), [&](const auto& copy) {
        return copy.type == event.type && copy.timestamp == event.common.timestamp;
    });
    if (menuEvent != state.menuEvents.end()) {
        state.menuEvents.erase(menuEvent);
        return true;
    }
    if (event.type == state.eventType && event.user.data1 == &state) {
        return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN) {
        auto key = std::find_if(state.keys.begin(), state.keys.end(), [&](const auto& key) { return key.event.timestamp == event.key.timestamp && key.event.raw == event.key.raw; });
        if (key != state.keys.end()) {
            bool consumed = key->consumed;
            state.keys.erase(key);
            return consumed;
        }
    }
    return false;
}
}  // namespace tn
