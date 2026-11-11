#include "app_identity.h"
#include <thornote/app/app_identity_data.h>
#include <thornote/platform/native_window.h>
#include <thorvg.h>
#include <ui/ui.h>
#include <algorithm>
#include <initializer_list>
#include <memory>
#include <vector>

namespace tn
{
namespace
{
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

SDL_Surface* renderAppIcon(int size, float margin)
{
    Surface surface(SDL_CreateSurface(size, size, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
    if (!surface) return nullptr;
    std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen());
    std::unique_ptr<tvg::Picture> picture(createAppIconPicture());
    float width = 0, height = 0;
    if (!canvas || !picture
        || picture->size(&width, &height) != tvg::Result::Success || width <= 0 || height <= 0) {
        SDL_SetError("Cannot load the embedded ThorNote icon");
        return nullptr;
    }
    float scale = size * (1 - 2 * margin) / std::max(width, height);
    width *= scale;
    height *= scale;
    picture->translate((size - width) / 2, (size - height) / 2);
    if (picture->size(width, height) != tvg::Result::Success
        // SDL icons use straight alpha; premultiplied edges would acquire dark fringes.
        || canvas->target(static_cast<uint32_t*>(surface->pixels), surface->pitch / 4, size, size, tvg::ColorSpace::ARGB8888S) != tvg::Result::Success
        || canvas->add(picture.get()) != tvg::Result::Success) {
        SDL_SetError("Cannot load the embedded ThorNote icon");
        return nullptr;
    }
    picture.release();
    if (canvas->draw(true) != tvg::Result::Success || canvas->sync() != tvg::Result::Success) {
        SDL_SetError("Cannot render the ThorNote icon");
        return nullptr;
    }
    return surface.release();
}
}  // namespace

bool initializeAppIdentity()
{
    return SDL_SetAppMetadata("ThorNote", identity::version, identity::identifier);
}

tvg::Picture* createAppIconPicture()
{
    std::unique_ptr<tvg::Picture> picture(tvg::Picture::gen());
    if (!picture || picture->load(identity::icon, sizeof(identity::icon) - 1, "svg") != tvg::Result::Success) {
        SDL_SetError("Cannot load the embedded ThorNote icon");
        return nullptr;
    }
    return picture.release();
}

tvg::Picture* createBrandPicture(ui::Color color)
{
    std::unique_ptr<tvg::Picture> picture(tvg::Picture::gen());
    if (!picture) return nullptr;
    picture->accessible = true;
    if (picture->load(identity::symbol, sizeof(identity::symbol) - 1, "svg", nullptr, true) != tvg::Result::Success) return nullptr;
    auto symbol = const_cast<tvg::Paint*>(picture->paint(tvg::Accessor::id("symbol")));
    if (!symbol || symbol->type() != tvg::Type::Shape) return nullptr;
    static_cast<tvg::Shape*>(symbol)->fill(color.r, color.g, color.b, color.a);
    return picture.release();
}

SDL_Surface* createAppIcon(float margin)
{
    Surface icon(renderAppIcon(32, margin), SDL_DestroySurface);
    if (!icon) return nullptr;
    for (int size : {64, 128, 256, 512}) {
        Surface image(renderAppIcon(size, margin), SDL_DestroySurface);
        if (!image || !SDL_AddSurfaceAlternateImage(icon.get(), image.get())) return nullptr;
    }
    return icon.release();
}

bool applyAppIcon(SDL_Window* window)
{
    auto style = nativeAppIconStyle();
    if (style.provided) return true;
    Surface icon(createAppIcon(style.margin), SDL_DestroySurface);
    return icon && SDL_SetWindowIcon(window, icon.get());
}
}  // namespace tn

namespace tn
{
std::filesystem::path appData(std::string_view relative)
{
    // Prefer bundle resources or a relocated install, then the configured install and source tree.
    static const auto root = [] {
        std::vector<std::filesystem::path> candidates;
        if (auto base = SDL_GetBasePath()) {
            candidates.push_back(utf8Path(base));
            candidates.push_back(utf8Path(base) / ".." / "share" / "thornote");
        }
        candidates.push_back(utf8Path(THORNOTE_INSTALL_DATA));
        for (const auto& candidate : candidates) {
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate / "assets" / "Pretendard-Regular.ttf", error) && std::filesystem::is_regular_file(candidate / "assets" / "Pretendard-Bold.ttf", error) && std::filesystem::is_regular_file(candidate / "assets" / "DejaVuSansMono.ttf", error)) return candidate.lexically_normal();
        }
        return utf8Path(THORNOTE_SOURCE_DATA);
    }();
    return root / utf8Path(relative);
}
}  // namespace tn
