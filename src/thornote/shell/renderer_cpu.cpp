#include "renderer.h"
#include <fstream>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Copies the changed regions of the frame to the page canvas, or all of it when asked. Assigning the canvas size
// clears it and resets the context state, so a context without the marked line width also receives the whole frame.
EM_JS(void, thornotePresent, (const uint32_t* pixels, int width, int height, const tvg::Region* regions, int count, bool whole), {
    var canvas = Module['canvas'];
    var state = Module['thornoteFrame'];
    if (!state || state.canvas != canvas) state = Module['thornoteFrame'] = {canvas : canvas, context : canvas.getContext('2d', {'alpha' : false}), image : null};
    var context = state.context;
    var image = state.image;
    // Shared memory cannot back an image, so threaded builds copy the changed rows out first.
    var shared = typeof SharedArrayBuffer != 'undefined' && HEAPU8.buffer instanceof SharedArrayBuffer;
    if (shared) {
        if (!image || image.width != width || image.height != height) image = new ImageData(width, height);
    } else if (!image || image.width != width || image.height != height || image.data.buffer != HEAPU8.buffer || image.data.byteOffset != pixels) {
        // Growing the memory replaces the buffer the image views.
        image = new ImageData(new Uint8ClampedArray(HEAPU8.buffer, pixels, width * height * 4), width, height);
    }
    state.image = image;
    var put = function(x, y, w, h)
    {
        if (shared) {
            for (var row = y; row < y + h; ++row) {
                var from = (row * width + x) * 4;
                image.data.set(HEAPU8.subarray(pixels + from, pixels + from + w * 4), from);
            }
        }
        context.putImageData(image, 0, 0, x, y, w, h);
    };
    if (whole || context.lineWidth != 2) {
        context.lineWidth = 2;
        put(0, 0, width, height);
        return;
    }
    for (var i = 0, region = regions >> 2; i < count; ++i, region += 4) {
        var x = HEAP32[region], y = HEAP32[region + 1];
        put(x, y, HEAP32[region + 2] - x, HEAP32[region + 3] - y);
    }
});
#endif

namespace tn
{
namespace
{
#ifdef __EMSCRIPTEN__
// The byte order of the page canvas.
constexpr auto colors = tvg::ColorSpace::ABGR8888;
constexpr int red = 0, blue = 16;
#else
constexpr auto colors = tvg::ColorSpace::ARGB8888;
constexpr int red = 16, blue = 0;
#endif
}  // namespace

struct Renderer::Impl
{
    SDL_Window* window = nullptr;
    std::vector<uint32_t> pixels;           // The render target, kept between frames for partial rendering.
    std::unique_ptr<tvg::SwCanvas> canvas;  // Released before the pixels it draws into.
    std::vector<tvg::Region> regions;       // Changed by the last frame.
    int width = 0, height = 0;
    bool attached = false, whole = true;
#ifndef __EMSCRIPTEN__
    SDL_Surface* frame = nullptr;  // Views the pixels for blits.
    SDL_Surface* shown = nullptr;  // The window surface of the last presented frame.
    std::vector<SDL_Rect> rects;

    ~Impl() { SDL_DestroySurface(frame); }
#endif
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

SDL_Window* Renderer::createWindow(const char* title, int width, int height, SDL_WindowFlags flags)
{
    auto window = SDL_CreateWindow(title, width, height, flags);
    if (!window) return nullptr;
    impl = std::make_unique<Impl>();
    impl->window = window;
    impl->canvas.reset(tvg::SwCanvas::gen(tvg::EngineOption::SmartRender));
    if (impl->canvas) return window;
    SDL_SetError("ThorVG renderer is unavailable");
    impl.reset();
    SDL_DestroyWindow(window);
    return nullptr;
}

void Renderer::stop()
{
    impl.reset();
}

const char* Renderer::engine() const
{
    return "cpu";
}

bool Renderer::draw(tvg::Scene* scene, int width, int height)
{
    auto& canvas = *impl->canvas;
    if (width != impl->width || height != impl->height) {
        impl->pixels.assign(size_t(width) * height, 0);
        if (canvas.target(impl->pixels.data(), width, width, height, colors) != tvg::Result::Success) return SDL_SetError("Cannot set the render target");
#ifndef __EMSCRIPTEN__
        SDL_DestroySurface(impl->frame);
        impl->frame = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_XRGB8888, impl->pixels.data(), width * 4);
        if (!impl->frame) return false;
#endif
        impl->width = width;
        impl->height = height;
    }
    if (!impl->attached) {
        if (canvas.add(scene) != tvg::Result::Success) return false;
        impl->attached = true;
    }
    // Unchanged paints keep their pixels, so ThorVG redraws and reports only the regions that changed.
    if (canvas.update() != tvg::Result::Success || canvas.draw(false) != tvg::Result::Success) return SDL_SetError("Cannot draw the frame");
    auto& regions = impl->regions;
    regions.resize(canvas.dirtyRegions());
    if ((!regions.empty() && canvas.dirtyRegions(regions.data(), uint32_t(regions.size())) != tvg::Result::Success) || canvas.sync() != tvg::Result::Success) return SDL_SetError("Cannot draw the frame");
    return true;
}

bool Renderer::present()
{
#ifdef __EMSCRIPTEN__
    thornotePresent(impl->pixels.data(), impl->width, impl->height, impl->regions.data(), int(impl->regions.size()), impl->whole);
    impl->whole = false;
    return true;
#else
    auto surface = SDL_GetWindowSurface(impl->window);
    if (!surface) return false;
    // A new window surface has none of the regions that did not change.
    bool whole = impl->whole || surface != impl->shown;
    impl->whole = false;
    impl->shown = surface;
    if (whole) return SDL_BlitSurface(impl->frame, nullptr, surface, nullptr) && SDL_UpdateWindowSurface(impl->window);
    auto& rects = impl->rects;
    rects.clear();
    SDL_Rect bounds{0, 0, surface->w, surface->h};
    for (const auto& region : impl->regions) {
        SDL_Rect rect{region.xMin, region.yMin, region.xMax - region.xMin, region.yMax - region.yMin};
        if (!SDL_GetRectIntersection(&rect, &bounds, &rect)) continue;
        if (!SDL_BlitSurface(impl->frame, &rect, surface, &rect)) return false;
        rects.push_back(rect);
    }
    return rects.empty() || SDL_UpdateWindowSurfaceRects(impl->window, rects.data(), int(rects.size()));
#endif
}

void Renderer::invalidate()
{
    impl->whole = true;
}

bool Renderer::snapshot(const std::string& path) const
{
    auto width = impl->width, height = impl->height;
    if (width <= 0 || height <= 0) return false;
    std::ofstream output(path, std::ios::binary);
    output << "P6\n"
           << width << ' ' << height << "\n255\n";
    std::vector<char> row(size_t(width) * 3);
    for (int y = 0; y < height; ++y) {
        auto pixel = impl->pixels.data() + size_t(y) * width;
        for (int x = 0; x < width; ++x) {
            row[x * 3] = char(pixel[x] >> red);
            row[x * 3 + 1] = char(pixel[x] >> 8);
            row[x * 3 + 2] = char(pixel[x] >> blue);
        }
        output.write(row.data(), std::streamsize(row.size()));
    }
    return bool(output);
}
}  // namespace tn
