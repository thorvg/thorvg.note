#include <ui/ui.h>

namespace ui
{
namespace
{
struct Release
{
    void operator()(tvg::Picture* value) const
    {
        if (value) value->unref();
    }
};
using Picture = std::unique_ptr<tvg::Picture, Release>;

Picture load(const ImageAsset& asset, std::string& error, std::unique_ptr<tvg::Animation>& animation)
{
    auto fail = [&](const char* reason) -> Picture {
        error = "Image '" + asset.name + "': " + reason;
        return {};
    };
    if (asset.name.empty() || asset.name.find('\0') != std::string::npos) return fail("invalid name");
    if (asset.data.empty() || asset.data.size() > 16 * 1024 * 1024) return fail("expected 1 byte to 16 MiB of image data");
    auto mime = asset.mime == "json" ? "lot" : asset.mime;
    if (mime != "svg+xml" && mime != "svg" && mime != "png" && mime != "jpeg" && mime != "webp" && mime != "gif" && mime != "lot") return fail("expected SVG, PNG, JPEG, WebP, GIF or Lottie JSON");
    if (mime == "gif" || mime == "lot") animation.reset(tvg::Animation::gen());
    auto raw = animation ? animation->picture() : tvg::Picture::gen();
    raw->ref();
    Picture picture(raw);
    if (picture->load(asset.data.data(), uint32_t(asset.data.size()), mime.c_str(), nullptr, true) != tvg::Result::Success)
        return fail("ThorVG cannot load this image format or data");
    float width = 0, height = 0;
    if (picture->size(&width, &height) != tvg::Result::Success || !std::isfinite(width) || !std::isfinite(height)
        || width <= 0 || height <= 0 || width > 32768 || height > 32768 || double(width) * height > 64 * 1024 * 1024)
        return fail("invalid or excessive image dimensions");
    return picture;
}
}  // namespace

struct Images::Impl
{
    struct Asset
    {
        Picture picture;
        std::unique_ptr<tvg::Animation> animation;
        std::string data, mime;
    };
    struct Instance
    {
        Picture picture;
        std::unique_ptr<tvg::Animation> animation;
        std::shared_ptr<Asset> asset;
        bool seen = false;
        double epoch = 0;
    };
    std::map<std::string, std::shared_ptr<Asset>, std::less<>> assets;
    std::map<std::string, Instance, std::less<>> instances;
};

Images::Images() :
    impl(std::make_unique<Impl>()) {}
Images::~Images() = default;

bool Images::replace(std::vector<ImageAsset> assets, std::string& error)
{
    decltype(impl->assets) next;
    size_t total = 0;
    if (assets.size() > 128) {
        error = "at most 128 images are allowed";
        return false;
    }
    for (auto& asset : assets) {
        total += asset.data.size();
        if (total > 64 * 1024 * 1024) {
            error = "images exceed the 64 MiB total limit";
            return false;
        }
        if (next.count(asset.name)) {
            error = "duplicate image: " + asset.name;
            return false;
        }
        std::unique_ptr<tvg::Animation> animation;
        auto picture = load(asset, error, animation);
        if (!picture) return false;
        auto value = std::make_shared<Impl::Asset>();
        if (animation) {
            value->data = std::move(asset.data);
            value->mime = asset.mime == "json" ? "lot" : std::move(asset.mime);
            value->animation = std::move(animation);
        } else value->picture = std::move(picture);
        next.emplace(std::move(asset.name), std::move(value));
    }
    impl->assets = std::move(next);
    impl->instances.clear();
    error.clear();
    return true;
}

bool Images::contains(std::string_view name) const
{
    return impl->assets.find(name) != impl->assets.end();
}
void Images::begin()
{
    for (auto& entry : impl->instances)
        entry.second.seen = false;
}
void Images::end()
{
    for (auto it = impl->instances.begin(); it != impl->instances.end();) {
        if (!it->second.seen) {
            auto& entry = it->second;
            if (entry.animation && !entry.asset->animation) entry.asset->animation = std::move(entry.animation);
            it = impl->instances.erase(it);
        } else ++it;
    }
}

void Images::render(Context& context, std::string_view instance, std::string_view name, Rect bounds, ImageFit fit, float opacity)
{
    auto asset = impl->assets.find(name);
    if (asset == impl->assets.end() || bounds.w <= 0 || bounds.h <= 0) return;
    auto& entry = impl->instances[std::string(instance)];
    if ((!entry.picture && !entry.animation) || entry.asset != asset->second) {
        entry.picture.reset();
        entry.animation.reset();
        if (!asset->second->data.empty()) {
            entry.animation = std::move(asset->second->animation);
            if (!entry.animation) {
                entry.animation.reset(tvg::Animation::gen());
                const auto& data = asset->second->data;
                if (entry.animation->picture()->load(data.data(), uint32_t(data.size()), asset->second->mime.c_str(), nullptr, true) != tvg::Result::Success) entry.animation.reset();
            }
        } else {
            auto copy = static_cast<tvg::Picture*>(asset->second->picture->duplicate());
            if (copy) copy->ref();
            entry.picture.reset(copy);
        }
        entry.asset = asset->second;
        entry.epoch = context.time();
    }
    if (!entry.picture && !entry.animation) return;
    entry.seen = true;
    auto picture = entry.animation ? entry.animation->picture() : entry.picture.get();
    // Reparent before changing a retained picture's transform or opacity.
    context.add(picture);
    if (entry.animation) {
        auto duration = entry.animation->duration();
        auto frames = entry.animation->totalFrame();
        if (duration > 0 && frames > 1) {
            auto frame = std::fmod(std::max(0.0, context.time() - entry.epoch), duration) / duration * frames;
            entry.animation->frame(float(std::floor(frame)));
            context.requestFrameAt(context.time() + 1.0 / 30);
        }
    }
    float width = 0, height = 0;
    picture->size(&width, &height);
    float sx = bounds.w / width, sy = bounds.h / height;
    if (fit != ImageFit::Stretch) sx = sy = fit == ImageFit::Cover ? std::max(sx, sy) : std::min(sx, sy);
    transform(picture, {sx, 0, bounds.x + (bounds.w - width * sx) / 2, 0, sy, bounds.y + (bounds.h - height * sy) / 2, 0, 0, 1});
    auto alpha = uint8_t(std::clamp(opacity, 0.0f, 1.0f) * 255);
    if (picture->opacity() != alpha) picture->opacity(alpha);
}

}  // namespace ui
