#pragma once

#include <SDL3/SDL.h>
#include <thorvg.h>
#include <memory>
#include <string>

namespace tn
{
// Draws the app scene with the engine chosen at build time and shows it in the window.
class Renderer
{
public:
    Renderer();
    ~Renderer();
    // Creates the window the engine draws into, along with the engine context and the canvas.
    SDL_Window* createWindow(const char* title, int width, int height, SDL_WindowFlags flags);
    // Releases the canvas and the engine context. Paints released after this can no longer reach the engine.
    void stop();
    const char* engine() const;
    // Draws the scene at the size in pixels; the first draw adds the scene to the canvas.
    bool draw(tvg::Scene* scene, int width, int height);
    // Shows the last drawn frame in the window.
    bool present();
    // The window may have lost the presented frame, so the next present shows all of it.
    void invalidate();
    // Writes the last drawn frame as a binary PPM image.
    bool snapshot(const std::string& path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace tn
