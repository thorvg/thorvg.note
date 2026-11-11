#pragma once

#include <SDL3/SDL.h>
#include <thornote/path.h>
#include <filesystem>
#include <string_view>

namespace tvg
{
struct Picture;
}

namespace ui
{
struct Color;
}

namespace tn
{
bool initializeAppIdentity();
tvg::Picture* createAppIconPicture();
tvg::Picture* createBrandPicture(ui::Color color);
// The caller owns the surface, including its high-DPI representations; margin is the transparent share of each edge.
SDL_Surface* createAppIcon(float margin = 0);
// Leaves an icon the platform already shows in place, such as the macOS bundle icon in the Dock.
bool applyAppIcon(SDL_Window*);
// A file such as "assets/Pretendard-Regular.ttf", from the app bundle, installed data directory or source tree.
std::filesystem::path appData(std::string_view relative);
}  // namespace tn
