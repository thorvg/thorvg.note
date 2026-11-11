#pragma once

#include <filesystem>
#include <memory>
#include <vector>
#include <thorvg.h>
#include <common/doctest/doctest.h>
#include <common/engine.h>
#include <ui/ui.h>
#include <ui/core/text/internal.h>

namespace ui::test
{
inline std::string fontPath(const char* name)
{
    auto path = (std::filesystem::path(TEST_RESOURCE_DIR) / "font" / name).u8string();
    return {path.begin(), path.end()};
}
using tn::test::Engine;
struct Fonts
{
    detail::FontManager* manager = &detail::fontManager();
    FontId plain = 0, kern = 0, fallback = 0;
    Fonts()
    {
        unloadFonts();
        plain = loadFont(fontPath("M1Plain.ttf").c_str());
        kern = loadFont(fontPath("M1Kern.ttf").c_str());
        fallback = loadFont(fontPath("M1Fallback.otf").c_str());
        REQUIRE(plain);
        REQUIRE(kern);
        REQUIRE(fallback);
    }
    ~Fonts() { unloadFonts(); }
};
}  // namespace ui::test
