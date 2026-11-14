#pragma once

#include <ui/ui.h>

namespace tn
{
class DocumentWidgets
{
public:
    void attach(ui::TextEditor& editor);
    void update(const Document& document);
    size_t total = 0, completed = 0;
    std::function<void(ui::Context&, std::string_view, ui::Rect, std::string_view)> image;
    std::function<void(const ui::TextWidgetEvent&)> chooseImage;

private:
    struct ScanState
    {
        char marker = 0;
        size_t length = 0;
        size_t listBase = 0, listIndent = 0;
        bool blank = false;
        bool operator==(const ScanState&) const = default;
    };
    struct Chunk
    {
        size_t start, end, total, completed;
        ScanState state;
    };
    std::vector<Chunk> chunks;
    uint64_t revision = uint64_t(-1), colorRevision = uint64_t(-1);
    size_t sourceSize = 0, colorStart = 0;
    std::unique_ptr<ui::TextInput> colorInput;
    std::string emojiQuery;
    std::vector<size_t> emojiCandidates;
    size_t emojiFirst = 0, emojiSelected = 0, emojiColumns = 5;
    std::vector<Chunk> scan(std::string_view text, size_t start, size_t end, ScanState state) const;
    void colorPopup(ui::Context&, std::string_view id, ui::Rect, ui::TextEditor&, const ui::TextWidgetEvent&);
    void emojiPopup(ui::Context&, std::string_view id, ui::Rect, ui::TextEditor&, const ui::TextWidgetEvent&);
};
}  // namespace tn
