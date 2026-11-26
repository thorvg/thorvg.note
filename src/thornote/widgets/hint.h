#pragma once

#include <ui/ui.h>
#include <string>

namespace tn
{
// A tooltip for the hovered control, shown when the frame ends.
struct Hint
{
    std::string text;
    ui::Rect bounds{};
};
}  // namespace tn
