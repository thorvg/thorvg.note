#include <ui/ui.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace ui
{

namespace
{
bool has(ControlFlag flags, ControlFlag flag)
{
    return (uint8_t(flags) & uint8_t(flag)) != 0;
}

struct Control
{
    std::string id;
    Rect bounds;
    Context::Handler handler;
    bool enabled;
    ControlFlag flags;
    Cursor cursor;

    bool visible() const { return bounds.w > 0 && bounds.h > 0; }
};

struct Layer
{
    tvg::Scene* scene = tvg::Scene::gen();
    Rect bounds;
    std::vector<tvg::Paint*> pending;
    std::vector<std::unique_ptr<Layer>> clips;
    std::vector<std::pair<uint64_t, tvg::Paint*>> drawn;
    size_t usedClips = 0, usedDrawn = 0;

    Layer() { scene->ref(); }
    ~Layer()
    {
        for (auto paint : pending)
            paint->unref();
        for (auto& entry : drawn)
            entry.second->unref();
        scene->unref();
    }
    void begin()
    {
        for (auto paint : pending)
            paint->unref();
        pending.clear();
        usedClips = usedDrawn = 0;
    }
};
}  // namespace

double frameTime()
{
    using Clock = std::chrono::steady_clock;
    static const auto epoch = Clock::now();
    return std::chrono::duration<double>(Clock::now() - epoch).count();
}

bool Rect::contains(float px, float py) const
{
    return w > 0 && h > 0 && px >= x && py >= y && px < x + w && py < y + h;
}

Rect Rect::inset(float amount) const
{
    return {x + amount, y + amount, std::max(0.0f, w - amount * 2), std::max(0.0f, h - amount * 2)};
}

Rect Rect::intersect(Rect other) const
{
    auto left = std::max(x, other.x), top = std::max(y, other.y);
    return {left, top, std::max(0.0f, std::min(x + w, other.x + other.w) - left), std::max(0.0f, std::min(y + h, other.y + other.h) - top)};
}

void transform(tvg::Paint* paint, const tvg::Matrix& matrix)
{
    if (!paint) return;
    const auto current = paint->transform();
    if (current.e11 == matrix.e11 && current.e12 == matrix.e12 && current.e13 == matrix.e13 && current.e21 == matrix.e21 && current.e22 == matrix.e22 && current.e23 == matrix.e23 && current.e31 == matrix.e31 && current.e32 == matrix.e32 && current.e33 == matrix.e33) return;
    paint->transform(matrix);
}

uint64_t paintHash(std::string_view bytes, uint64_t seed)
{
    for (unsigned char byte : bytes)
        seed = (seed ^ byte) * 0x100000001b3ull;
    return seed;
}

struct Context::Impl
{
    std::unique_ptr<Layer> root = std::make_unique<Layer>();
    Layer* target = root.get();
    std::unordered_map<tvg::Paint*, Layer*> paintLayers;
    Icons icons;
    size_t iconSequence = 0;
    double seconds = 0;
    std::optional<double> nextFrame;
    const Theme* theme = nullptr;
    Rect clip;
    std::vector<Control> controls;
    std::string focusId, pressedId, savedFocus;
    float pointerX = -1, pointerY = -1;
    bool modalActive = false, modalFrame = false;
    std::function<void()> dismissModal;

    Control* find(std::string_view id)
    {
        for (auto& control : controls) {
            if (control.id == id) return &control;
        }
        return nullptr;
    }

    Control* hit(float x, float y)
    {
        for (auto it = controls.rbegin(); it != controls.rend(); ++it) {
            if (it->bounds.contains(x, y)) return &*it;
        }
        return nullptr;
    }

    void send(std::string_view id, const Event& event)
    {
        auto target = find(id);
        if (!target || !target->enabled || !target->handler) return;
        // Callbacks may change the model or invalidate a contribution. Never retain a control reference across a callback.
        auto handler = target->handler;
        handler(event);
    }

    void cancelPointer()
    {
        auto id = std::move(pressedId);
        pressedId.clear();
        auto target = find(id);
        if (target && has(target->flags, ControlFlag::Capture)) send(id, {.type = EventType::Cancel, .x = pointerX, .y = pointerY});
    }

    void discard(Layer& layer)
    {
        for (auto paint : layer.scene->paints())
            paintLayers.erase(paint);
        for (auto& clip : layer.clips)
            discard(*clip);
        layer.scene->remove();
    }

    void finish(Layer& layer)
    {
        std::unordered_set<tvg::Paint*> desired(layer.pending.begin(), layer.pending.end());
        const auto& attached = layer.scene->paints();
        for (auto it = attached.begin(); it != attached.end();) {
            auto paint = *it++;
            if (desired.count(paint)) continue;
            paintLayers.erase(paint);
            // ThorVG damages a removed paint only while another reference keeps it alive.
            paint->ref();
            layer.scene->remove(paint);
            paint->unref();
        }
        auto next = attached.begin();
        for (auto paint : layer.pending) {
            if (next != attached.end() && *next == paint) ++next;
            else {
                auto found = paintLayers.find(paint);
                if (found != paintLayers.end()) found->second->scene->remove(paint);
                layer.scene->add(paint, next == attached.end() ? nullptr : *next);
            }
            paintLayers[paint] = &layer;
            paint->unref();
        }
        layer.pending.clear();
        for (size_t i = layer.usedDrawn; i < layer.drawn.size(); ++i)
            layer.drawn[i].second->unref();
        layer.drawn.resize(layer.usedDrawn);
        for (size_t i = layer.usedClips; i < layer.clips.size(); ++i)
            discard(*layer.clips[i]);
        layer.clips.resize(layer.usedClips);
    }
};

Context::Context() :
    impl(std::make_unique<Impl>()) {}
Context::~Context() = default;

void Context::begin(const Theme& theme, Rect viewport, double seconds)
{
    impl->root->begin();
    impl->target = impl->root.get();
    if (!std::isfinite(seconds) || seconds < 0) seconds = frameTime();
    impl->seconds = seconds;
    impl->nextFrame.reset();
    impl->icons.begin(seconds);
    impl->iconSequence = 0;
    impl->theme = &theme;
    impl->clip = viewport;
    impl->controls.clear();
    impl->modalFrame = false;
}

void Context::end()
{
    impl->finish(*impl->root);
    impl->icons.end();
    if (impl->icons.animating()) requestAnimationFrame();
    if (impl->modalActive && !impl->modalFrame) impl->focusId = impl->savedFocus;
    impl->modalActive = impl->modalFrame;
    if (!impl->modalActive) impl->dismissModal = {};
    auto focused = impl->find(impl->focusId);
    if (!focused || !focused->enabled || !has(focused->flags, ControlFlag::Focusable)) impl->focusId.clear();
    auto pressed = impl->find(impl->pressedId);
    if (!pressed || !pressed->enabled) impl->pressedId.clear();
}

double Context::time() const
{
    return impl->seconds;
}

void Context::requestFrameAt(double when)
{
    if (std::isfinite(when) && when >= 0 && (!impl->nextFrame || when < *impl->nextFrame)) impl->nextFrame = when;
}

void Context::requestAnimationFrame()
{
    requestFrameAt(impl->seconds + 1.0 / 60);
}

std::optional<double> Context::nextFrame() const
{
    return impl->nextFrame;
}

void Context::modal(std::string_view initialFocus, std::function<void()> dismiss)
{
    if (!impl->modalActive) impl->cancelPointer();
    impl->controls.clear();
    impl->dismissModal = std::move(dismiss);
    if (!impl->modalActive) {
        impl->savedFocus = impl->focusId;
        focus(initialFocus);
    }
    impl->modalFrame = true;
}

tvg::Scene* Context::scene() const
{
    return impl->root->scene;
}

const Theme& Context::theme() const
{
    return *impl->theme;
}

State Context::control(std::string_view id, Rect bounds, Handler handler, bool enabled, ControlFlag flags, Cursor cursor)
{
    bounds = bounds.intersect(impl->clip);
    impl->controls.push_back({std::string(id), bounds, std::move(handler), enabled, flags, cursor});
    return {enabled && bounds.contains(impl->pointerX, impl->pointerY), enabled && impl->pressedId == id, enabled && impl->focusId == id};
}

void Context::focus(std::string_view id)
{
    impl->focusId = id;
}

const std::string& Context::focused() const
{
    return impl->focusId;
}

bool Context::focusedWithin(Rect bounds) const
{
    auto control = impl->find(impl->focusId);
    if (!control || !control->enabled || !has(control->flags, ControlFlag::Focusable) || !control->visible()) return false;
    return bounds.contains(control->bounds.x + control->bounds.w / 2, control->bounds.y + control->bounds.h / 2);
}

bool Context::controlAt(float x, float y) const
{
    return impl->hit(x, y) != nullptr;
}

void Context::cancelPointer()
{
    impl->cancelPointer();
}

Cursor Context::cursor() const
{
    auto pressed = impl->find(impl->pressedId);
    if (pressed && pressed->enabled && pressed->cursor != Cursor::Default) return pressed->cursor;
    auto hovered = impl->hit(impl->pointerX, impl->pointerY);
    return hovered && hovered->enabled ? hovered->cursor : Cursor::Default;
}

void Context::pointerPosition(float x, float y)
{
    impl->pointerX = x;
    impl->pointerY = y;
}

void Context::pointerMove(float x, float y)
{
    pointerPosition(x, y);
    if (!impl->pressedId.empty()) impl->send(impl->pressedId, {.type = EventType::Drag, .x = x, .y = y});
}

void Context::pointerDown(float x, float y, Modifiers modifiers, int clicks)
{
    impl->cancelPointer();
    pointerPosition(x, y);
    auto target = impl->hit(x, y);
    if (!target || !target->enabled) {
        impl->focusId.clear();
        return;
    }
    impl->pressedId = target->id;
    if (has(target->flags, ControlFlag::Focusable)) impl->focusId = target->id;
    impl->send(impl->pressedId, {.type = EventType::Press, .x = x, .y = y, .modifiers = modifiers, .clicks = clicks});
}

void Context::pointerUp(float x, float y)
{
    pointerPosition(x, y);
    auto id = std::move(impl->pressedId);
    impl->pressedId.clear();
    auto captured = impl->find(id);
    bool release = captured && has(captured->flags, ControlFlag::Capture);
    auto target = impl->hit(x, y);
    bool activate = target && target->id == id;
    if (release) impl->send(id, {.type = EventType::Release, .x = x, .y = y});
    if (activate) impl->send(id, {.type = EventType::Activate, .x = x, .y = y});
}

void Context::contextMenu(float x, float y)
{
    pointerPosition(x, y);
    auto target = impl->hit(x, y);
    if (target && target->enabled) impl->send(target->id, {.type = EventType::ContextMenu, .x = x, .y = y});
}

void Context::key(Key key, Modifiers modifiers)
{
    auto captured = impl->find(impl->pressedId);
    if (key == Key::Escape && captured && has(captured->flags, ControlFlag::Capture)) {
        impl->cancelPointer();
        return;
    }
    if (key == Key::Escape && impl->modalActive && impl->dismissModal) {
        auto dismiss = impl->dismissModal;
        dismiss();
        return;
    }
    auto focused = impl->find(impl->focusId);
    if (key == Key::Tab && !(focused && focused->enabled && has(focused->flags, ControlFlag::TabInput))) {
        std::vector<const std::string*> order;
        for (const auto& control : impl->controls) {
            if (control.enabled && has(control.flags, ControlFlag::Focusable) && control.visible()) order.push_back(&control.id);
        }
        if (order.empty()) return;
        auto found = std::find_if(order.begin(), order.end(), [&](auto id) { return *id == impl->focusId; });
        auto index = found == order.end() ? (modifiers.shift ? 0 : -1) : int(found - order.begin());
        impl->focusId = *order[(index + (modifiers.shift ? int(order.size()) - 1 : 1)) % int(order.size())];
        return;
    }
    impl->send(impl->focusId, {.type = EventType::Key, .key = key, .modifiers = modifiers});
}

void Context::textInput(std::string_view text)
{
    impl->send(impl->focusId, {.type = EventType::Text, .text = std::string(text)});
}

bool Context::wheel(float vertical, float horizontal)
{
    if (!std::isfinite(vertical) || !std::isfinite(horizontal) || (vertical == 0 && horizontal == 0)) return false;
    for (size_t i = impl->controls.size(); i > 0;) {
        const auto& control = impl->controls[--i];
        if (!control.enabled || !has(control.flags, ControlFlag::Scrollable) || !control.bounds.contains(impl->pointerX, impl->pointerY)) continue;
        auto id = control.id;
        Event event{.type = EventType::Scroll, .x = horizontal, .y = vertical};
        impl->send(id, event);
        if (event.handled) return true;
        i = std::min(i, impl->controls.size());
    }
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (Module['onUnhandledWheel']) Module['onUnhandledWheel']($0, $1); }, horizontal * 100, -vertical * 100);
#endif
    return false;
}

void Context::scroll(std::string_view id, Rect bounds, float& x, float& y, float limitX, float limitY, ScrollOptions options)
{
    limitX = std::max(0.0f, limitX);
    limitY = std::max(0.0f, limitY);
    x = std::clamp(x, 0.0f, limitX);
    y = std::clamp(y, 0.0f, limitY);
    control(id, bounds, [&x, &y, limitX, limitY, bounds, options](const Event& event) {
        float dx = 0, dy = 0;
        if (event.type == EventType::Scroll) {
            dx = -event.x * options.step;
            dy = -event.y * options.step;
        } else if (event.type == EventType::Key) {
            switch (event.key) {
                case Key::Up: dy = -options.step; break;
                case Key::Down: dy = options.step; break;
                case Key::Left: dx = -options.step; break;
                case Key::Right: dx = options.step; break;
                case Key::PageUp: dy = -bounds.h * 0.9f; break;
                case Key::PageDown: dy = bounds.h * 0.9f; break;
                case Key::Home: dx = -limitX; dy = -limitY; break;
                case Key::End: dy = limitY; break;
                default: event.handled = false; return;
            }
        } else return;
        bool horizontal = scrollOffset(x, limitX, dx);
        bool vertical = scrollOffset(y, limitY, dy);
        event.handled = horizontal || vertical; }, options.enabled && (limitX > 0 || limitY > 0), ControlFlag::Scrollable);
}

bool Context::scrollKey(std::string_view id, Key key)
{
    auto control = impl->find(id);
    if (!control || !control->enabled || !has(control->flags, ControlFlag::Scrollable)) return false;
    Event event{.type = EventType::Key, .key = key, .handled = false};
    impl->send(id, event);
    return event.handled;
}

void Context::clip(Rect bounds, const std::function<void()>& render)
{
    bounds = bounds.intersect(impl->clip);
    if (bounds.w <= 0 || bounds.h <= 0) return;
    auto previousTarget = impl->target;
    auto previousBounds = impl->clip;
    auto index = previousTarget->usedClips++;
    if (index == previousTarget->clips.size()) previousTarget->clips.push_back(std::make_unique<Layer>());
    auto& layer = *previousTarget->clips[index];
    if (!layer.scene->clip() || layer.bounds.x != bounds.x || layer.bounds.y != bounds.y || layer.bounds.w != bounds.w || layer.bounds.h != bounds.h) {
        auto shape = tvg::Shape::gen();
        shape->appendRect(bounds.x, bounds.y, bounds.w, bounds.h);
        shape->fill(255, 255, 255);
        layer.scene->clip(shape);
        // Fast-track clip changes need fresh coverage for retained children.
        layer.scene->transform(layer.scene->transform());
        layer.bounds = bounds;
    }
    add(layer.scene);
    layer.begin();
    impl->target = &layer;
    impl->clip = bounds;
    render();
    impl->finish(layer);
    impl->target = previousTarget;
    impl->clip = previousBounds;
}

Rect Context::clip() const
{
    return impl->clip;
}

void Context::add(tvg::Paint* paint)
{
    auto& pending = impl->target->pending;
    if (!paint || std::find(pending.begin(), pending.end(), paint) != pending.end()) return;
    paint->ref();
    auto found = impl->paintLayers.find(paint);
    if (found != impl->paintLayers.end() && found->second != impl->target) {
        found->second->scene->remove(paint);
        impl->paintLayers.erase(found);
    }
    pending.push_back(paint);
}

void Context::paint(uint64_t key, const std::function<tvg::Paint*()>& make)
{
    auto& drawn = impl->target->drawn;
    if (impl->target->usedDrawn == drawn.size()) drawn.emplace_back();
    auto& entry = drawn[impl->target->usedDrawn++];
    if (!entry.second || entry.first != key) {
        auto paint = make();
        if (!paint) return;
        paint->ref();
        // The scene keeps a replaced paint until the layer is finished.
        if (entry.second) entry.second->unref();
        entry = {key, paint};
    }
    add(entry.second);
}

void Context::rect(Rect bounds, Color fill, float radius, Color border, float stroke)
{
    if (bounds.w <= 0 || bounds.h <= 0) return;
    paint(paintKey('R', bounds, fill, radius, border, stroke), [&] {
        auto shape = tvg::Shape::gen();
        shape->appendRect(bounds.x, bounds.y, bounds.w, bounds.h, radius, radius);
        shape->fill(fill.r, fill.g, fill.b, fill.a);
        if (border.a) {
            shape->strokeWidth(stroke);
            shape->strokeFill(border.r, border.g, border.b, border.a);
        }
        return shape;
    });
}

void Context::line(float x1, float y1, float x2, float y2, Color color, float width)
{
    paint(paintKey('L', x1, y1, x2, y2, color, width), [&] {
        auto shape = tvg::Shape::gen();
        shape->moveTo(x1, y1);
        shape->lineTo(x2, y2);
        shape->strokeWidth(width);
        shape->strokeCap(tvg::StrokeCap::Round);
        shape->strokeFill(color.r, color.g, color.b, color.a);
        return shape;
    });
}

void Context::icon(Rect bounds, std::string_view name, Color color)
{
    icon("icon/" + std::to_string(impl->iconSequence++), bounds, name, color);
}

void Context::icon(std::string_view instance, Rect bounds, std::string_view name, Color color, IconState state, float size)
{
    if (bounds.w <= 0 || bounds.h <= 0 || !std::isfinite(size) || size <= 0 || name.empty()) return;
    auto picture = impl->icons.picture(instance, name, state, color, [this](tvg::Picture* paint) { add(paint); });
    if (!picture) return;
    float width = 0, height = 0;
    if (picture->size(&width, &height) != tvg::Result::Success || width <= 0 || height <= 0) return;
    float scale = size / std::max(width, height);
    transform(picture, {scale, 0, bounds.x + (bounds.w - width * scale) / 2, 0, scale, bounds.y + (bounds.h - height * scale) / 2, 0, 0, 1});
}

bool Context::icons(std::vector<IconAsset> assets, std::string& error)
{
    return impl->icons.replace(std::move(assets), error);
}

void Context::focusRing(Rect bounds)
{
    rect(bounds.inset(-3), {0, 0, 0, 0}, impl->theme->radius + 3, impl->theme->accent, 1.5f);
}

}  // namespace ui
