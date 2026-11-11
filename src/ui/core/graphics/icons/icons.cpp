#include <ui/ui.h>
#include <ui/core/graphics/icons/builtin_data.h>
#include <thorvg_lottie.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace ui
{
namespace
{
constexpr const char* poses[]{"idle", "hover", "pressed", "active", "disabled", "focused"};

int pose(const IconState& state)
{
    return !state.enabled ? 4 : state.pressed ? 2
        : state.active                        ? 3
        : state.hovered                       ? 1
        : state.focused                       ? 5
                                              : 0;
}

bool same(Color a, Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool same(const IconAsset& a, const IconAsset& b)
{
    return a.name == b.name && a.data == b.data && a.mime == b.mime && a.tint == b.tint
        && a.duration == b.duration && a.markers == b.markers;
}

std::string mime(const IconAsset& asset)
{
    auto value = asset.mime;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (value.empty()) {
        auto first = asset.data.find_first_not_of(" \t\r\n");
        return first != std::string::npos && asset.data[first] == '<' ? "svg" : "lot";
    }
    if (value == "svg" || value == "svg+xml" || value == "image/svg+xml") return "svg";
    if (value == "lot" || value == "json" || value == "lottie" || value == "lottie+json"
        || value == "application/json" || value == "application/lottie+json") return "lot";
    return {};
}

bool zeroInPoint(std::string_view json)
{
    size_t at = 0;
    auto whitespace = [&] {
        while (at < json.size() && (json[at] == ' ' || json[at] == '\t' || json[at] == '\r' || json[at] == '\n'))
            ++at;
    };
    auto string = [&](std::string* decoded) {
        if (at >= json.size() || json[at++] != '"') return false;
        while (at < json.size()) {
            auto c = json[at++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 32) return false;
            if (c == '\\') {
                if (at == json.size()) return false;
                c = json[at++];
                if (c == 'u') {
                    unsigned code = 0;
                    for (unsigned i = 0; i < 4; ++i) {
                        if (at == json.size()) return false;
                        auto digit = static_cast<unsigned char>(json[at++]);
                        if (digit >= '0' && digit <= '9') code = code * 16 + digit - '0';
                        else if (digit >= 'a' && digit <= 'f') code = code * 16 + digit - 'a' + 10;
                        else if (digit >= 'A' && digit <= 'F') code = code * 16 + digit - 'A' + 10;
                        else return false;
                    }
                    c = code < 128 ? char(code) : char(127);
                } else if (c == 'b') c = '\b';
                else if (c == 'f') c = '\f';
                else if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
                else if (c != '"' && c != '\\' && c != '/') return false;
            }
            if (decoded) {
                // ThorVG compares keys as C strings, so an embedded NUL cannot be ignored.
                if (!c) return false;
                decoded->push_back(c);
            }
        }
        return false;
    };
    auto value = [&] {
        auto first = at;
        unsigned depth = 0;
        while (at < json.size()) {
            auto c = json[at];
            if (c == '"') {
                if (!string(nullptr)) return false;
                continue;
            }
            if (c == '[' || c == '{') {
                if (++depth > 256) return false;
            } else if (c == ']' || c == '}') {
                if (!depth) return c == '}' && at > first;
                --depth;
            } else if (c == ',' && !depth) return at > first;
            ++at;
        }
        return false;
    };
    auto zero = [](std::string_view number) {
        auto last = number.find_last_not_of(" \t\r\n");
        if (last == std::string_view::npos) return false;
        number = number.substr(0, last + 1);
        size_t i = number.front() == '-' ? 1 : 0;
        if (i == number.size() || number[i++] != '0') return false;
        if (i < number.size() && number[i] == '.') {
            auto first = ++i;
            while (i < number.size() && number[i] == '0')
                ++i;
            if (i == first) return false;
        }
        if (i < number.size() && (number[i] == 'e' || number[i] == 'E')) {
            ++i;
            if (i < number.size() && (number[i] == '+' || number[i] == '-')) ++i;
            auto first = i;
            while (i < number.size() && number[i] >= '0' && number[i] <= '9')
                ++i;
            if (i == first) return false;
        }
        return i == number.size();
    };
    whitespace();
    if (at == json.size() || json[at++] != '{') return false;
    whitespace();
    if (at < json.size() && json[at] == '}') {
        ++at;
        whitespace();
        return at == json.size();
    }
    while (at < json.size()) {
        std::string key;
        if (!string(&key)) return false;
        whitespace();
        if (at == json.size() || json[at++] != ':') return false;
        whitespace();
        auto first = at;
        if (!value()) return false;
        if (key == "ip" && !zero(json.substr(first, at - first))) return false;
        if (json[at++] == '}') {
            whitespace();
            return at == json.size();
        }
        whitespace();
    }
    return false;
}

struct Release
{
    void operator()(tvg::Picture* picture) const { picture->unref(); }
};

struct Drawing
{
    std::shared_ptr<const IconAsset> asset;
    std::unique_ptr<tvg::LottieAnimation> animation;
    std::unique_ptr<tvg::Picture, Release> still;
    std::array<float, 6> frames{}, overshoots{};
    Color color{};
    uint32_t slot = 0;
    float target = 0;
    double started = 0, duration = 0;
    bool moving = false, overshooting = false, settling = false, seen = false, colored = false;

    ~Drawing()
    {
        if (slot) animation->del(slot);
    }

    tvg::Picture* picture() const { return animation ? animation->picture() : still.get(); }

    void tick(double now)
    {
        if (!moving) return;
        auto progress = std::clamp((now - started) / duration, 0.0, 1.0);
        if (progress == 1) {
            animation->frame(target);
            moving = overshooting = settling = false;
            return;
        }
        // Complete the authored peak before settling, even when a render skips the boundary.
        if (overshooting && progress >= .55) {
            animation->tween(1.0f);
            animation->tweenTo(target);
            overshooting = false;
            settling = true;
        }
        if (overshooting) progress /= .55;
        else if (settling) progress = (progress - .55) / .45;
        auto eased = float(overshooting ? 1 - std::pow(1 - progress, 3)
                                        : progress * progress * (3 - 2 * progress));
        animation->tween(eased);
    }

    void state(const IconState& state, double now)
    {
        if (!animation) return;
        auto index = pose(state);
        auto next = frames[index];
        if (next == target) return;
        tick(now);
        target = next;
        overshooting = overshoots[index] != target;
        settling = false;
        auto seconds = overshooting ? (index == 1 ? .28 : index == 3 ? .32
                                                                     : .24)
                                    : (index == 2 ? .12 : index == 0 ? .20
                                                                     : .16);
        duration = seconds * double(asset->duration) / .16;
        if (duration == 0) {
            animation->frame(target);
            moving = overshooting = false;
        } else if (animation->tweenTo(overshooting ? overshoots[index] : target) == tvg::Result::Success) {
            started = now;
            moving = true;
            animation->tween(0.0f);
        } else {
            animation->frame(target);
            moving = overshooting = false;
        }
    }

    void tint(Color next)
    {
        if (!asset->tint || (colored && same(color, next))) return;
        if (animation) {
            // Delete first: deleting an old slot resets its target properties.
            if (slot) animation->del(slot);
            char json[192];
            std::snprintf(json, sizeof(json), "{\"icon-ink\":{\"p\":{\"a\":0,\"k\":[%.8f,%.8f,%.8f]}}}",
                          next.r / 255.0, next.g / 255.0, next.b / 255.0);
            slot = animation->gen(json);
            if (slot) animation->apply(slot);
        } else {
            auto recolor = [](const tvg::Paint* paint, void* value) {
                if (paint->type() != tvg::Type::Shape) return true;
                auto shape = const_cast<tvg::Shape*>(static_cast<const tvg::Shape*>(paint));
                auto color = *static_cast<Color*>(value);
                uint8_t r, g, b, a;
                if (shape->fill(&r, &g, &b, &a) == tvg::Result::Success) shape->fill(color.r, color.g, color.b, a);
                if (shape->strokeFill(&r, &g, &b, &a) == tvg::Result::Success) shape->strokeFill(color.r, color.g, color.b, a);
                return true;
            };
            std::unique_ptr<tvg::Accessor> accessor(tvg::Accessor::gen());
            accessor->set(picture(), recolor, &next);
        }
        picture()->opacity(next.a);
        color = next;
        colored = true;
    }
};

std::unique_ptr<Drawing> load(const IconAsset& asset, std::string& error)
{
    auto fail = [&](const char* reason) -> std::unique_ptr<Drawing> {
        error = "Icon '" + asset.name + "': " + reason;
        return nullptr;
    };
    if (asset.name.empty()) return fail("name is empty");
    if (asset.data.empty() || asset.data.find('\0') != std::string::npos
        || asset.data.size() > std::numeric_limits<uint32_t>::max()) return fail("empty or invalid image data");
    if (!std::isfinite(asset.duration) || asset.duration < 0) return fail("duration must be finite and nonnegative");
    auto type = mime(asset);
    if (type.empty()) return fail("expected SVG or Lottie JSON");
    auto drawing = std::make_unique<Drawing>();
    if (type == "lot") drawing->animation.reset(tvg::LottieAnimation::gen());
    else {
        auto picture = tvg::Picture::gen();
        picture->ref();
        drawing->still.reset(picture);
    }
    auto picture = drawing->picture();
    if (picture->load(asset.data.data(), uint32_t(asset.data.size()), type.c_str(), nullptr, true) != tvg::Result::Success)
        return fail("cannot load image data");
    // Accessor traversal finishes asynchronous parsing, even for an empty pose.
    unsigned nodes = 0;
    std::unique_ptr<tvg::Accessor> accessor(tvg::Accessor::gen());
    accessor->set(picture, [](const tvg::Paint*, void* count) { return ++*static_cast<unsigned*>(count) < 2; }, &nodes);
    float width = 0, height = 0;
    if (nodes < 2 || picture->size(&width, &height) != tvg::Result::Success
        || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        return fail("invalid image dimensions or content");
    if (drawing->animation && !zeroInPoint(asset.data))
        return fail("Lottie root ip must be numeric zero (or omitted): ThorVG 1.2 dynamic tweening requires ip=0");
    std::unordered_map<std::string, float> markers;
    if (drawing->animation) {
        auto animation = drawing->animation.get();
        auto total = animation->totalFrame();
        if (!std::isfinite(total) || total <= 0 || !std::isfinite(animation->duration()) || animation->duration() <= 0)
            return fail("invalid animation duration");
        for (uint32_t i = 0; i < animation->markersCnt(); ++i) {
            float begin = 0, end = 0;
            auto name = animation->marker(i, &begin, &end);
            if (!name || !std::isfinite(begin) || !std::isfinite(end) || begin < 0 || begin >= total || end < begin)
                return fail("invalid pose marker range");
            if (!markers.emplace(name, begin).second) return fail("duplicate pose marker name");
        }
    }
    for (size_t i = 0; i < asset.markers.size(); ++i) {
        const auto& alias = asset.markers[i];
        if (!alias.empty() && alias != poses[i] && markers.find(alias) == markers.end()) {
            error = "Icon '" + asset.name + "': unknown pose marker '" + alias + "'";
            return nullptr;
        }
    }
    auto idle = markers.find(asset.markers[0]);
    auto fallback = idle == markers.end() ? 0.0f : idle->second;
    for (size_t i = 0; i < drawing->frames.size(); ++i) {
        auto found = markers.find(asset.markers[i]);
        drawing->frames[i] = found == markers.end() ? fallback : found->second;
        auto peak = markers.find(asset.markers[i] + "-overshoot");
        drawing->overshoots[i] = found != markers.end() && (i == 1 || i == 3 || i == 5) && peak != markers.end()
            ? peak->second
            : drawing->frames[i];
    }
    return drawing;
}
}  // namespace

const std::vector<std::string_view>& builtinIcons()
{
    static const std::vector<std::string_view> names = [] {
        std::vector<std::string_view> result;
        for (const auto& asset : detail::builtinIcons)
            result.emplace_back(asset.name);
        return result;
    }();
    return names;
}

bool validateIcon(const IconAsset& asset, std::string& error)
{
    error.clear();
    return bool(load(asset, error));
}

struct Icons::Impl
{
    std::unordered_map<std::string, std::shared_ptr<const IconAsset>> users, builtins;
    std::unordered_map<std::string, std::unique_ptr<Drawing>> instances;
    double now = 0;

    const detail::BuiltinIcon* builtin(std::string_view name) const
    {
        for (const auto& asset : detail::builtinIcons)
            if (name == asset.name) return &asset;
        return nullptr;
    }

    std::shared_ptr<const IconAsset> find(const std::string& name)
    {
        auto user = users.find(name);
        if (user != users.end()) return user->second;
        auto cached = builtins.find(name);
        if (cached != builtins.end()) return cached->second;
        auto source = builtin(name);
        if (!source) return nullptr;
        auto asset = std::make_shared<IconAsset>();
        asset->name = source->name;
        asset->data = source->data;
        asset->mime = "lot";
        asset->tint = source->tint;
        builtins.emplace(name, asset);
        return asset;
    }
};

Icons::Icons() :
    impl(std::make_unique<Impl>()) {}
Icons::~Icons() = default;

bool Icons::replace(std::vector<IconAsset> assets, std::string& error)
{
    error.clear();
    std::unordered_map<std::string, std::shared_ptr<const IconAsset>> next;
    for (auto& asset : assets) {
        if (next.find(asset.name) != next.end()) {
            error = "Duplicate icon name '" + asset.name + "'";
            return false;
        }
        auto previous = impl->users.find(asset.name);
        if (previous != impl->users.end() && same(*previous->second, asset)) {
            next.emplace(asset.name, previous->second);
            continue;
        }
        if (!validateIcon(asset, error)) return false;
        auto name = asset.name;
        next.emplace(std::move(name), std::make_shared<IconAsset>(std::move(asset)));
    }
    impl->users.swap(next);
    return true;
}

bool Icons::contains(std::string_view name) const
{
    return impl->users.find(std::string(name)) != impl->users.end() || impl->builtin(name);
}

void Icons::begin(double seconds)
{
    if (std::isfinite(seconds)) impl->now = std::max(impl->now, seconds);
    for (auto& item : impl->instances)
        item.second->seen = false;
}

tvg::Picture* Icons::picture(std::string_view instance, std::string_view name, const IconState& state, Color tint,
                             const std::function<void(tvg::Picture*)>& prepare)
{
    if (instance.empty()) return nullptr;
    auto asset = impl->find(std::string(name));
    if (!asset) return nullptr;
    auto& entry = impl->instances[std::string(instance)];
    if (!entry || entry->asset != asset) {
        std::string error;
        auto next = load(*asset, error);
        if (!next) return nullptr;
        next->asset = std::move(asset);
        next->target = next->frames[pose(state)];
        if (next->animation) next->animation->frame(next->target);
        entry = std::move(next);
    }
    entry->seen = true;
    if (prepare) prepare(entry->picture());
    entry->tick(impl->now);
    entry->state(state, impl->now);
    entry->tint(tint);
    return entry->picture();
}

void Icons::end()
{
    for (auto it = impl->instances.begin(); it != impl->instances.end();) {
        if (!it->second || !it->second->seen) it = impl->instances.erase(it);
        else ++it;
    }
}

bool Icons::animating() const
{
    return std::any_of(impl->instances.begin(), impl->instances.end(), [](const auto& item) {
        return item.second && item.second->moving;
    });
}

}  // namespace ui
