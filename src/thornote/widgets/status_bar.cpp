#include "status_bar.h"
#include <algorithm>
#include <string>

namespace tn
{
void drawStatusBar(ui::Context& context, ui::Rect r, std::string_view error, bool settings, DocumentTab* tab)
{
    const auto& theme = context.theme();
    context.rect(r, theme.surface);
    context.line(r.x, r.y, r.x + r.w, r.y, theme.border);
    if (!error.empty()) {
        context.text({r.x + 12, r.y, r.w - 24, r.h}, context.elideText(error, std::max(0.0f, r.w - 24), 10), theme.danger, 10);
        return;
    }
    if (settings) {
        context.text({r.x + 12, r.y, r.w - 24, r.h}, "Settings", theme.muted, 10);
        return;
    }
    if (!tab) return;
    auto caret = tab->editor.session().selection().caret;
    auto line = tab->document.lineAt(caret);
    size_t column = 1;
    for (auto pos = tab->document.lineStart(line); pos < caret; pos = tab->document.next(pos))
        ++column;
    const char* eol = tab->format.ending == storage::LineEnding::Lf ? "LF" : tab->format.ending == storage::LineEnding::Crlf ? "CRLF"
        : tab->format.ending == storage::LineEnding::Cr                                                                      ? "CR"
                                                                                                                             : "Mixed EOL";
#ifdef __EMSCRIPTEN__
    constexpr auto savedLabel = "Saved in browser";
#else
    constexpr auto savedLabel = "Saved";
#endif
    std::string state = tab->loading ? "Opening…" : tab->saving ? "Saving…"
        : tab->dirty()                                          ? "Modified"
        : tab->stamp.exists()                                   ? savedLabel
                                                                : "Not saved";
    if (!tab->editor.inputState().status.empty()) state = tab->editor.inputState().status + " · " + state;
    tab->widgets.update(tab->document);
    if (tab->widgets.total) {
        state += " · Tasks " + std::to_string(tab->widgets.completed) + '/' + std::to_string(tab->widgets.total);
        ui::Rect progress{r.x + r.w / 2 - 70, r.y + r.h / 2 - 2, 58, 4};
        context.rect(progress, theme.border, 2);
        progress.w *= float(tab->widgets.completed) / tab->widgets.total;
        if (progress.w > 0) context.rect(progress, theme.accent, 2);
    }
    context.text({r.x + 12, r.y, r.w / 2 - (tab->widgets.total ? 86 : 12), r.h}, state, theme.muted, 10);
    context.text({r.x + r.w / 2, r.y, r.w / 2 - 12, r.h}, "Ln " + std::to_string(line + 1) + ", Col " + std::to_string(column) + "     UTF-8     " + eol, theme.muted, 10, false, ui::Align::End);
}
}  // namespace tn
