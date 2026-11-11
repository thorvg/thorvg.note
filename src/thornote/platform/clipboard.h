#pragma once

#include <string>
#include <string_view>

namespace tn
{
// Throw on invalid text or platform failure; callers decide how to surface the error.
std::string readClipboardText();
void writeClipboardText(std::string_view);
}  // namespace tn
