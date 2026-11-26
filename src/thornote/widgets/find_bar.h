#pragma once

#include <thornote/documents/workspace.h>
#include <thornote/shell/shell.h>
#include <thornote/widgets/hint.h>
#include <ui/ui.h>
#include <optional>
#include <string>
#include <vector>

namespace tn
{
// Literal search and replacement in the document under the bar.
class FindBar
{
public:
    FindBar(ui::Context& context, Workspace& workspace, ui::CommandRegistry& commands) :
        context(context), workspace(workspace), commands(commands) {}

    // Whether a field of the bar has keyboard focus.
    bool focused() const;
    // Whether the query occurs in the document, reusing the results while they are current.
    bool found(const DocumentTab&) const;
    // Opens on the document with its single-line selection as the query.
    void show(DocumentTab&, bool replacing);
    void toggleReplace();
    // Follows the document under the bar, or none, and its edits.
    void update(DocumentTab*);
    // Selects the next match in the direction, or the nearest one for zero.
    void step(DocumentTab*, int direction);
    void substitute(DocumentTab*, bool all);
    // Takes the top of the bounds and leaves the rest to the editor.
    void draw(ui::Rect& bounds, DocumentTab&, Hint&);
    // The command a key runs while the bar is open, or null.
    const char* command(Key, bool shift) const;
    // The caret area of the focused field.
    ui::Rect caret() const;

    bool open = false, replace = false;
    ui::TextInput query, replacement;
    ui::Rect queryBounds{}, replacementBounds{};
    uint64_t document = 0, revision = uint64_t(-1);
    std::string searchedQuery;
    std::vector<ui::TextDecoration> matches;
    std::optional<size_t> current;

private:
    ui::Context& context;
    Workspace& workspace;
    ui::CommandRegistry& commands;
};
}  // namespace tn
