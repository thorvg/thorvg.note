#include "renderer.h"
#include <fstream>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

namespace tn
{
struct Renderer::Impl
{
    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;
    std::unique_ptr<tvg::GlCanvas> canvas;
    int width = 0, height = 0;
    bool attached = false;

    ~Impl()
    {
        canvas.reset();
        if (gl) SDL_GL_DestroyContext(gl);
    }
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

SDL_Window* Renderer::createWindow(const char* title, int width, int height, SDL_WindowFlags flags)
{
#ifdef __EMSCRIPTEN__
    constexpr int profile = SDL_GL_CONTEXT_PROFILE_ES, major = 3, minor = 0;
#elif defined(__APPLE__)
    constexpr int profile = SDL_GL_CONTEXT_PROFILE_CORE, major = 4, minor = 1;
#else
    constexpr int profile = SDL_GL_CONTEXT_PROFILE_CORE, major = 3, minor = 3;
#endif
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile) || !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major) || !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor)) return nullptr;
    auto window = SDL_CreateWindow(title, width, height, flags | SDL_WINDOW_OPENGL);
    if (!window) return nullptr;
    impl = std::make_unique<Impl>();
    impl->window = window;
    impl->gl = SDL_GL_CreateContext(window);
    if (impl->gl) impl->canvas.reset(tvg::GlCanvas::gen());
    if (impl->canvas) return window;
    if (impl->gl) SDL_SetError("ThorVG renderer is unavailable");
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
    return "gl";
}

bool Renderer::draw(tvg::Scene* scene, int width, int height)
{
    auto& canvas = *impl->canvas;
    if (width != impl->width || height != impl->height) {
        if (canvas.target(nullptr, nullptr, impl->gl, 0, width, height, tvg::ColorSpace::ABGR8888S) != tvg::Result::Success) return SDL_SetError("Cannot set the GL render target");
        impl->width = width;
        impl->height = height;
    }
    if (!impl->attached) {
        if (canvas.add(scene) != tvg::Result::Success) return false;
        impl->attached = true;
    }
    if (canvas.update() != tvg::Result::Success || canvas.draw(true) != tvg::Result::Success || canvas.sync() != tvg::Result::Success) return SDL_SetError("Cannot draw the GL frame");
    return true;
}

bool Renderer::present()
{
    return SDL_GL_SwapWindow(impl->window);
}

// Every frame is drawn whole.
void Renderer::invalidate() {}

bool Renderer::snapshot(const std::string& path) const
{
    auto width = impl->width, height = impl->height;
    if (width <= 0 || height <= 0) return false;
#ifdef __EMSCRIPTEN__
    auto readPixels = glReadPixels;
#else
    auto readPixels = reinterpret_cast<decltype(&glReadPixels)>(SDL_GL_GetProcAddress("glReadPixels"));
    if (!readPixels) return false;
#endif
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    readPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream output(path, std::ios::binary);
    output << "P6\n"
           << width << ' ' << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        auto row = pixels.data() + size_t(y) * width * 4;
        for (int x = 1; x < width; ++x) {
            row[x * 3] = row[x * 4];
            row[x * 3 + 1] = row[x * 4 + 1];
            row[x * 3 + 2] = row[x * 4 + 2];
        }
        output.write(reinterpret_cast<const char*>(row), size_t(width) * 3);
    }
    return bool(output);
}
}  // namespace tn
