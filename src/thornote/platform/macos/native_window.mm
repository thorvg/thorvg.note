#include <thornote/platform/native_window.h>
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <string>

// Owns the empty toolbar that sizes the custom title bar and tracks full-screen transitions.
@interface ThorNoteTitleBarChrome : NSObject
@property(nonatomic, strong) NSToolbar* toolbar;
@property(nonatomic) BOOL transitioning;
// Leaving full screen restores the previous frame under SDL's titled style; restoring the custom chrome must keep it.
@property(nonatomic) BOOL keepFrame;
- (instancetype)initWithWindow:(NSWindow*)window;
@end

@implementation ThorNoteTitleBarChrome {
    NSArray* observers;
}

- (instancetype)initWithWindow:(NSWindow*)window
{
    if (!(self = [super init])) return nil;
    __weak ThorNoteTitleBarChrome* weakSelf = self;
    auto begin = ^(NSNotification* notification) {
      weakSelf.transitioning = YES;
      weakSelf.keepFrame = YES;
      // SDL enters full screen without a full-size content view, where a toolbar would leave a blank band.
      NSWindow* target = notification.object;
      target.toolbar = nil;
    };
    auto end = ^(NSNotification*) { weakSelf.transitioning = NO; };
    auto center = NSNotificationCenter.defaultCenter;
    observers = @[
        [center addObserverForName:NSWindowWillEnterFullScreenNotification
                            object:window
                             queue:nil
                        usingBlock:begin],
        [center addObserverForName:NSWindowWillExitFullScreenNotification
                            object:window
                             queue:nil
                        usingBlock:begin],
        [center addObserverForName:NSWindowDidEnterFullScreenNotification
                            object:window
                             queue:nil
                        usingBlock:end],
        [center addObserverForName:NSWindowDidExitFullScreenNotification
                            object:window
                             queue:nil
                        usingBlock:end],
    ];
    return self;
}

- (void)dealloc
{
    for (id observer in observers)
        [NSNotificationCenter.defaultCenter removeObserver:observer];
}
@end

namespace
{
const char titleBarChromeKey = 0;

ThorNoteTitleBarChrome* titleBarChrome(NSWindow* window)
{
    ThorNoteTitleBarChrome* chrome = objc_getAssociatedObject(window, &titleBarChromeKey);
    if (!chrome) {
        chrome = [[ThorNoteTitleBarChrome alloc] initWithWindow:window];
        objc_setAssociatedObject(window, &titleBarChromeKey, chrome, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return chrome;
}

void showTitleBarToolbar(NSWindow* window, ThorNoteTitleBarChrome* chrome, bool visible)
{
    if (@available(macOS 11.0, *)) {
        if (visible && !chrome.toolbar) {
            chrome.toolbar = [[NSToolbar alloc] initWithIdentifier:@"ThorNoteTitleBar"];
            chrome.toolbar.allowsUserCustomization = NO;
        }
        NSToolbar* toolbar = visible ? chrome.toolbar : nil;
        if (window.toolbar != toolbar) window.toolbar = toolbar;
        // A compact unified toolbar gives AppKit's title bar the custom bar's 40 pt height and centers the traffic lights in it.
        if (visible) window.toolbarStyle = NSWindowToolbarStyleUnifiedCompact;
    }
}

BOOL mouseDownMovesWindow(NSView*, SEL)
{
    return NO;
}

NSRect titleBarContentRect(NSView* view, SEL)
{
    // AppKit computes title-bar drag regions before delivering mouse events.
    // Claim custom content here; the input monitor starts caption drags explicitly.
    return view.window.styleMask & NSWindowStyleMaskFullSizeContentView ? view.bounds : NSZeroRect;
}

Class titleBarContentClass(Class parent)
{
    auto name = std::string("ThorNoteTitleBar_") + class_getName(parent);
    if (auto existing = objc_lookUpClass(name.c_str())) return existing;
    auto subclass = objc_allocateClassPair(parent, name.c_str(), 0);
    if (!subclass) return Nil;
    auto booleanType = std::string(@encode(BOOL)) + "@:";
    auto rectType = std::string(@encode(NSRect)) + "@:";
    class_addMethod(subclass, @selector(mouseDownCanMoveWindow), reinterpret_cast<IMP>(mouseDownMovesWindow), booleanType.c_str());
    // Private AppKit title-bar content hook for macOS 27.
    class_addMethod(subclass, sel_registerName("_opaqueRectForWindowMoveWhenInTitlebar"), reinterpret_cast<IMP>(titleBarContentRect), rectType.c_str());
    objc_registerClassPair(subclass);
    return subclass;
}
}  // namespace

namespace tn
{
WindowControls nativeWindowControls()
{
    return {WindowControlStyle::TrafficLights};
}

AppIconStyle nativeAppIconStyle()
{
    auto info = NSBundle.mainBundle.infoDictionary;
    return {info[@"CFBundleIconFile"] != nil || info[@"CFBundleIconName"] != nil, 100.f / 1024};
}

bool nativeWindowChromeAvailable()
{
    return true;
}
const char* nativeWindowZoomCommand()
{
    return "window.fullscreen";
}

SDL_HitTestResult nativeWindowHitTest(SDL_Window* window, const SDL_Point&, bool draggable)
{
    return draggable && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}

bool nativeWindowChromeSupported(SDL_Window* window)
{
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr) != nullptr;
}

bool performNativeTitleBarDoubleClick(SDL_Window* window)
{
    auto flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_FULLSCREEN) return true;
    auto defaults = NSUserDefaults.standardUserDefaults;
    auto action = [defaults stringForKey:@"AppleActionOnDoubleClick"];
    if ([action isEqualToString:@"Minimize"] || (!action && [defaults boolForKey:@"AppleMiniaturizeOnDoubleClick"])) return SDL_MinimizeWindow(window);
    if (!action || [action isEqualToString:@"Maximize"] || [action isEqualToString:@"Zoom"] || [action isEqualToString:@"Fill"])
        return flags & SDL_WINDOW_MAXIMIZED ? SDL_RestoreWindow(window) : SDL_MaximizeWindow(window);
    return true;
}

struct NativeTitleBarInput::Impl
{
    NSWindow* window = nil;
    NSView* contentView = nil;
    Class originalViewClass = Nil;
    SDL_WindowID windowId = 0;
    id monitor = nil;
    bool armed = false, forwarding = false, watching = false, locked = false, movable = true;
    NSEvent* dragEvent = nil;
    SDL_EventType mouseType = SDL_EVENT_MOUSE_BUTTON_DOWN;
    uint8_t nativeClicks = 0;
    SDL_FPoint nativePoint{};
    std::deque<SDL_MouseButtonEvent> clicks;
    std::function<bool(float, float)> draggable;
    std::function<void()> doubleClick;

    void release()
    {
        if (locked) window.movable = movable;
        locked = armed = forwarding = false;
        dragEvent = nil;
    }

    static bool SDLCALL watch(void* data, SDL_Event* event)
    {
        auto self = static_cast<Impl*>(data);
        if (SDL_IsMainThread() && (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) && event->button.windowID == self->windowId && event->button.button == SDL_BUTTON_LEFT) {
            auto click = event->button;
            click.clicks = 0;
            if (self->nativeClicks && event->type == self->mouseType) {
                click.clicks = self->nativeClicks;
                click.x = self->nativePoint.x;
                click.y = self->nativePoint.y;
                self->nativeClicks = 0;
            }
            self->clicks.push_back(click);
        }
        return true;
    }
};

NativeTitleBarInput::NativeTitleBarInput() :
    impl(std::make_unique<Impl>()) {}
NativeTitleBarInput::~NativeTitleBarInput()
{
    stop();
}
SDL_HitTestResult NativeTitleBarInput::hitTest(SDL_HitTestResult result) const
{
    // SDL's background-drag state can outlive the caption hit and drop a button event.
    // Start caption drags explicitly through AppKit instead.
    return result == SDL_HITTEST_DRAGGABLE ? SDL_HITTEST_NORMAL : result;
}
bool NativeTitleBarInput::event(SDL_Event& event)
{
    auto& state = *impl;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID == state.windowId) state.release();
    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.windowID == state.windowId) {
        auto found = std::find_if(state.clicks.begin(), state.clicks.end(), [&](const auto& click) {
            return click.type == event.type && click.timestamp == event.button.timestamp && click.button == event.button.button;
        });
        if (found != state.clicks.end()) {
            if (found->clicks) {
                // Keep AppKit's click classification and actual release position.
                event.button.clicks = found->clicks;
                event.button.x = found->x;
                event.button.y = found->y;
            }
            state.clicks.erase(found);
        }
    }
    return false;
}
void NativeTitleBarInput::update(bool custom, bool)
{
    if (!custom) impl->release();
}

bool NativeTitleBarInput::initialize(SDL_Window* window, std::function<bool(float, float)> draggable, std::function<void()> doubleClick)
{
    stop();
    auto& state = *impl;
    state.window = (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!state.window) return false;
    state.contentView = state.window.contentView;
    state.originalViewClass = object_getClass(state.contentView);
    auto contentClass = titleBarContentClass(state.originalViewClass);
    if (!contentClass) {
        stop();
        return false;
    }
    object_setClass(state.contentView, contentClass);
    state.windowId = SDL_GetWindowID(window);
    state.draggable = std::move(draggable);
    state.doubleClick = std::move(doubleClick);
    auto self = &state;
    state.monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskLeftMouseDragged
                                                          handler:^NSEvent*(NSEvent* event) {
                                                            self->nativeClicks = 0;
                                                            bool down = event.type == NSEventTypeLeftMouseDown;
                                                            bool up = event.type == NSEventTypeLeftMouseUp;
                                                            if (down) self->release();
                                                            if (event.window != self->window) {
                                                                self->release();
                                                                return event;
                                                            }
                                                            auto view = self->window.contentView;
                                                            auto point = event.locationInWindow;
                                                            auto target = [view.superview hitTest:[view.superview convertPoint:point fromView:nil]];
                                                            // The traffic lights and window frame sit above the content view and track their own clicks.
                                                            if (down && target != view && ![target isDescendantOf:view]) return event;
                                                            if (down || up) {
                                                                self->mouseType = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                                                                self->nativeClicks = uint8_t(std::clamp<NSInteger>(event.clickCount, 0, 255));
                                                                self->nativePoint = {float(point.x), float(view.frame.size.height - point.y)};
                                                                // SDL reconciles hardware button state before AppKit runs local monitors.
                                                                // Its release may already be queued with the previous motion position.
                                                                if (!self->clicks.empty() && self->clicks.back().type == self->mouseType && !self->clicks.back().clicks) {
                                                                    auto& click = self->clicks.back();
                                                                    click.clicks = self->nativeClicks;
                                                                    click.x = self->nativePoint.x;
                                                                    click.y = self->nativePoint.y;
                                                                    self->nativeClicks = 0;
                                                                }
                                                            }
                                                            if (self->forwarding) {
                                                                if (up) {
                                                                    [view mouseUp:event];
                                                                    self->release();
                                                                } else [view mouseDragged:event];
                                                                return nil;
                                                            }
                                                            bool hit = self->draggable(point.x, view.frame.size.height - point.y);
                                                            if (self->dragEvent) {
                                                                self->nativeClicks = 0;
                                                                if (up) {
                                                                    bool activate = self->armed && event.clickCount == 2 && hit;
                                                                    self->release();
                                                                    if (activate) self->doubleClick();
                                                                } else {
                                                                    auto start = self->dragEvent.locationInWindow;
                                                                    if (std::hypot(point.x - start.x, point.y - start.y) >= 8) {
                                                                        auto press = self->dragEvent;
                                                                        self->release();
                                                                        [self->window performWindowDragWithEvent:press];
                                                                    }
                                                                }
                                                                return nil;
                                                            }
                                                            if (down && hit) {
                                                                self->dragEvent = event;
                                                                self->armed = event.clickCount == 2;
                                                                self->nativeClicks = 0;
                                                                return nil;
                                                            }
                                                            if (down && !hit && (self->window.styleMask & NSWindowStyleMaskFullSizeContentView) && !(self->window.styleMask & NSWindowStyleMaskFullScreen)) {
                                                                // AppKit can still move a transparent title bar even outside SDL drag regions.
                                                                self->movable = self->window.movable;
                                                                self->window.movable = NO;
                                                                self->locked = true;
                                                                if (point.y >= NSMaxY(self->window.contentLayoutRect)) {
                                                                    // Bypass the native frame's double-click action over custom controls.
                                                                    self->forwarding = true;
                                                                    [view mouseDown:event];
                                                                    return nil;
                                                                }
                                                            }
                                                            if (up) self->release();
                                                            return event;
                                                          }];
    state.watching = state.monitor && SDL_AddEventWatch(Impl::watch, &state);
    if (!state.watching) stop();
    return state.watching;
}

void NativeTitleBarInput::stop()
{
    auto& state = *impl;
    state.release();
    if (state.watching) SDL_RemoveEventWatch(Impl::watch, &state);
    state.watching = false;
    if (state.monitor) [NSEvent removeMonitor:state.monitor];
    state.monitor = nil;
    if (state.contentView) object_setClass(state.contentView, state.originalViewClass);
    state.contentView = nil;
    state.originalViewClass = Nil;
    state.window = nil;
    state.windowId = 0;
    state.nativeClicks = 0;
    state.clicks.clear();
    state.draggable = {};
    state.doubleClick = {};
}

bool updateNativeWindowChrome(SDL_Window* window, bool nativeTitleBar)
{
    auto native = (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    auto flags = SDL_GetWindowFlags(window);
    bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) || (native.styleMask & NSWindowStyleMaskFullScreen);
    if (!native) return fullscreen || SDL_SetWindowBordered(window, nativeTitleBar);
    auto chrome = titleBarChrome(native);
    // Leave the window style alone while AppKit animates into or out of full screen.
    fullscreen = fullscreen || chrome.transitioning;

    if (!fullscreen) {
        auto view = native.contentView;
        auto size = view.frame.size;
        auto frame = native.frame;
        auto responder = native.firstResponder;
        // A titled AppKit frame supplies the platform's corner mask and shadow.
        if (!SDL_SetWindowBordered(window, true)) return false;
        auto style = nativeTitleBar ? native.styleMask & ~NSWindowStyleMaskFullSizeContentView
                                    : native.styleMask | NSWindowStyleMaskFullSizeContentView;
        if (native.styleMask != style) {
            // AppKit rewires the responder chain when changing window styles.
            auto next = view.nextResponder;
            view.nextResponder = nil;
            native.styleMask = style;
            view.nextResponder = next;
        }
        showTitleBarToolbar(native, chrome, !nativeTitleBar);
        if (chrome.keepFrame) {
            // AppKit keeps the content size when the style changes, which would shrink the restored window by its title bar.
            if (!NSEqualRects(native.frame, frame)) [native setFrame:frame display:YES];
            chrome.keepFrame = NO;
        } else if (!NSEqualSizes(view.frame.size, size)) {
            [native setContentSize:size];
            [native setFrameOrigin:NSMakePoint(frame.origin.x, NSMaxY(frame) - native.frame.size.height)];
        }
        if (responder && native.firstResponder != responder) [native makeFirstResponder:responder];
    } else showTitleBarToolbar(native, chrome, false);
    // AppKit keeps drawing the traffic lights; the custom title bar only reserves their area.
    native.titleVisibility = nativeTitleBar ? NSWindowTitleVisible : NSWindowTitleHidden;
    native.titlebarAppearsTransparent = !nativeTitleBar;
    if (@available(macOS 11.0, *))
        native.titlebarSeparatorStyle = nativeTitleBar ? NSTitlebarSeparatorStyleAutomatic : NSTitlebarSeparatorStyleNone;
    return true;
}
}  // namespace tn
