#include <thornote/platform/linux/title_bar.h>
#include "window_shadow.h"
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <vector>

namespace tn
{
namespace
{
class X11TitleBarInput : public LinuxTitleBarInput
{
public:
    X11TitleBarInput(SDL_Window* window, Display* display, Window xid, std::function<bool(float, float)> draggable, std::function<void()> doubleClick) :
        LinuxTitleBarInput(window, std::move(draggable), std::move(doubleClick)), display(display), xid(xid), clicks(gesture()), shadow(window, display)
    {
        auto screen = int(SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_SCREEN_NUMBER, 0));
        root = RootWindow(display, screen);
        moveAtom = XInternAtom(display, "_NET_WM_MOVERESIZE", False);
        int event = 0, error = 0;
        XQueryExtension(display, "XInputExtension", &xiOpcode, &event, &error);
        instances().push_back(this);
        SDL_SetX11EventHook(dispatch, nullptr);
    }
    ~X11TitleBarInput() override
    {
        auto& list = instances();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
        if (list.empty()) SDL_SetX11EventHook(nullptr, nullptr);
    }
    void update(bool custom, bool focused) override { shadow.update(custom, focused); }

private:
    static std::vector<X11TitleBarInput*>& instances()
    {
        static std::vector<X11TitleBarInput*> values;
        return values;
    }
    static bool SDLCALL dispatch(void*, XEvent* event)
    {
        for (auto input : instances())
            if (!input->receive(*event)) return false;
        return true;
    }
    void move()
    {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = xid;
        event.xclient.message_type = moveAtom;
        event.xclient.format = 32;
        event.xclient.data.l[0] = rootX;
        event.xclient.data.l[1] = rootY;
        event.xclient.data.l[2] = 8;
        event.xclient.data.l[3] = Button1;
        event.xclient.data.l[4] = 1;
        XUngrabPointer(display, CurrentTime);
        XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(display);
    }
    bool button(uint32_t time, float x, float y, bool down)
    {
        if (down) {
            if (!hit(x, y)) {
                clicks.cancel();
                return true;
            }
            clicks.press(time, x, y);
            return false;
        }
        if (!clicks.active()) return !(released && releasedAt == time);
        released = true;
        releasedAt = time;
        if (clicks.release(x, y, hit(x, y)) == TitleBarGesture::Action::DoubleClick) doubleClick();
        return false;
    }
    bool motion(float x, float y)
    {
        if (!clicks.active()) return true;
        if (clicks.motion(x, y) == TitleBarGesture::Action::Move) move();
        return false;
    }
    bool receive(const XEvent& event)
    {
        if (event.xany.display != display) return true;
        // SDL can replace the native window when creating its framebuffer.
        xid = Window(SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        if (event.type == GenericEvent && event.xcookie.extension == xiOpcode && event.xcookie.data && (event.xcookie.evtype == XI_ButtonPress || event.xcookie.evtype == XI_ButtonRelease || event.xcookie.evtype == XI_Motion)) {
            auto input = static_cast<const XIDeviceEvent*>(event.xcookie.data);
            if (input->event != xid) return true;
            rootX = long(input->root_x);
            rootY = long(input->root_y);
            if (event.xcookie.evtype == XI_Motion) return motion(float(input->event_x), float(input->event_y));
            if (input->detail == Button1) return button(uint32_t(input->time), float(input->event_x), float(input->event_y), event.xcookie.evtype == XI_ButtonPress);
            clicks.cancel();
            return true;
        }
        if (event.xany.window != xid) return true;
        if (event.type == ButtonPress || event.type == ButtonRelease) {
            rootX = event.xbutton.x_root;
            rootY = event.xbutton.y_root;
            if (event.xbutton.button == Button1) return button(uint32_t(event.xbutton.time), float(event.xbutton.x), float(event.xbutton.y), event.type == ButtonPress);
            clicks.cancel();
        } else if (event.type == MotionNotify) {
            rootX = event.xmotion.x_root;
            rootY = event.xmotion.y_root;
            return motion(float(event.xmotion.x), float(event.xmotion.y));
        } else if (event.type == FocusOut || (event.type == LeaveNotify && (event.xcrossing.mode == NotifyNormal || clicks.dragging()))) clicks.cancel();
        return true;
    }
    Display* display;
    Window xid, root;
    Atom moveAtom;
    int xiOpcode = 0;
    long rootX = 0, rootY = 0;
    TitleBarGesture clicks;
    X11WindowShadow shadow;
    uint32_t releasedAt = 0;
    bool released = false;
};
}  // namespace

std::unique_ptr<LinuxTitleBarInput> createX11TitleBarInput(SDL_Window* window, std::function<bool(float, float)> draggable, std::function<void()> doubleClick)
{
    auto properties = SDL_GetWindowProperties(window);
    auto display = static_cast<Display*>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
    auto xid = Window(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    if (!display || !xid) return {};
    return std::make_unique<X11TitleBarInput>(window, display, xid, std::move(draggable), std::move(doubleClick));
}
}  // namespace tn
