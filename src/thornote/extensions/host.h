#pragma once

#include <ui/ui.h>
#include <thornote/path.h>
#include <filesystem>
#include <memory>

namespace tn
{
class InputExtensions
{
public:
    using Resolve = std::function<ui::TextEditor*(uint64_t)>;
    // `focused` reports whether an editor owns keyboard input; a hidden editor keeps the state of its last frame.
    InputExtensions(ui::CommandRegistry&, Resolve, std::function<uint64_t()> activeEditor, std::function<bool(uint64_t)> focused);
    ~InputExtensions();
    InputExtensions(const InputExtensions&) = delete;
    InputExtensions& operator=(const InputExtensions&) = delete;
    void setDirectory(std::filesystem::path);
    bool reload();
    bool poll();
    void setEnabled(bool);
    bool enabled() const;
    const std::string& error() const;
    std::filesystem::path edit(std::string& error);
    ui::InputResult key(uint64_t editor, const ui::KeyStroke&);
    ui::InputResult text(uint64_t editor, std::string_view, bool shift = false);
    // Observe native edits, selections, focus and document lifetime between input/render calls.
    bool observe(const std::vector<uint64_t>& editors);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace tn
