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

Picture load(const ImageAsset& asset, std::string& error)
{
    auto fail = [&](const char* reason) -> Picture {
        error = "Image '" + asset.name + "': " + reason;
        return {};
    };
    if (asset.name.empty() || asset.name.find('\0') != std::string::npos) return fail("invalid name");
    if (asset.data.empty() || asset.data.size() > 16 * 1024 * 1024) return fail("expected 1 byte to 16 MiB of image data");
    if (asset.mime != "svg+xml" && asset.mime != "png" && asset.mime != "jpeg" && asset.mime != "webp") return fail("expected SVG, PNG, JPEG or WebP");
    auto raw = tvg::Picture::gen();
    raw->ref();
    Picture picture(raw);
    if (picture->load(asset.data.data(), uint32_t(asset.data.size()), asset.mime.c_str(), nullptr, true) != tvg::Result::Success)
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
    };
    struct Instance
    {
        Picture picture;
        std::shared_ptr<Asset> asset;
        bool seen = false;
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
        auto picture = load(asset, error);
        if (!picture) return false;
        auto value = std::make_shared<Impl::Asset>();
        value->picture = std::move(picture);
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
        if (!it->second.seen) it = impl->instances.erase(it);
        else ++it;
    }
}

void Images::render(Context& context, std::string_view instance, std::string_view name, Rect bounds, ImageFit fit, float opacity)
{
    auto asset = impl->assets.find(name);
    if (asset == impl->assets.end() || bounds.w <= 0 || bounds.h <= 0) return;
    auto& entry = impl->instances[std::string(instance)];
    if (!entry.picture || entry.asset != asset->second) {
        auto copy = static_cast<tvg::Picture*>(asset->second->picture->duplicate());
        if (copy) copy->ref();
        entry.picture.reset(copy);
        entry.asset = asset->second;
    }
    if (!entry.picture) return;
    entry.seen = true;
    auto picture = entry.picture.get();
    // Reparent before changing a retained picture's transform or opacity.
    context.add(picture);
    float width = 0, height = 0;
    picture->size(&width, &height);
    float sx = bounds.w / width, sy = bounds.h / height;
    if (fit != ImageFit::Stretch) sx = sy = fit == ImageFit::Cover ? std::max(sx, sy) : std::min(sx, sy);
    transform(picture, {sx, 0, bounds.x + (bounds.w - width * sx) / 2, 0, sy, bounds.y + (bounds.h - height * sy) / 2, 0, 0, 1});
    auto alpha = uint8_t(std::clamp(opacity, 0.0f, 1.0f) * 255);
    if (picture->opacity() != alpha) picture->opacity(alpha);
}

}  // namespace ui
