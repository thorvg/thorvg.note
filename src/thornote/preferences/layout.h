#pragma once

#include <cstddef>
#include <array>

namespace tn
{

enum class Layout
{
    Memo,
    Note
};
constexpr size_t layoutCount = 2;

constexpr const char* layoutId(Layout layout)
{
    return layout == Layout::Memo ? "memo" : "note";
}

constexpr const char* layoutName(Layout layout)
{
    return layout == Layout::Memo ? "Memo" : "Note";
}

struct LayoutState
{
    bool sidebar, properties, lineNumbers;
    float sidebarWidth, notesWidth = 236, propertiesWidth = 216;
};

inline constexpr std::array<LayoutState, layoutCount> defaultLayouts{{
    {false, false, false, 224},
    {true, false, false, 168},
}};

}  // namespace tn
