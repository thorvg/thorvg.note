#pragma once

#include <thornote/documents/workspace.h>
#include <ui/ui.h>
#include <string_view>

namespace tn
{
// The status line: an error when there is one, otherwise the state of the document and its caret.
void drawStatusBar(ui::Context&, ui::Rect, std::string_view error, bool settings, DocumentTab*);
}  // namespace tn
