#pragma once

#include "fonts.h"

namespace ui::test
{
// A Context frame over an initialized engine with the test fonts loaded.
struct Frame
{
    Engine engine;
    Fonts fonts;
    Theme theme = Theme::dark();
    Context context;
    Rect viewport{0, 0, 400, 300};

    Frame()
    {
        context.fontFor = [this](std::string_view, bool) {
            return fonts.plain;
        };
    }

    template<typename Build>
    void render(Build&& build)
    {
        context.begin(theme, viewport);
        build();
        context.end();
    }

    void click(float x, float y)
    {
        context.pointerDown(x, y);
        context.pointerUp(x, y);
    }
};
}  // namespace ui::test
