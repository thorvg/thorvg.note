#pragma once

#include <ui/ui.h>
#include <text/markdown.h>

namespace ui
{
struct TextEditor::Impl
{
    struct Format
    {
        size_t start = 0, end = 0;
        Color Theme::*color = &Theme::text;
        unsigned heading = 0;
        bool bold = false, italic = false, code = false, strike = false, link = false;
    };
    struct Run
    {
        size_t first = 0, end = 0;
        float left = 0, leading = 0, width = 0, top = 0, scale = 1;
        FontId font = 0;
        Format format;
    };
    struct RenderRun
    {
        tvg::Text* text = tvg::Text::gen();
        RenderRun() { text->ref(); }
        ~RenderRun() { text->unref(); }
    };
    struct RenderLine
    {
        tvg::Text* body = tvg::Text::gen();
        tvg::Text* number = tvg::Text::gen();
        uint64_t style = 0;
        std::string value;
        bool assigned = false;
        std::vector<std::unique_ptr<RenderRun>> runs;
        float bodyLeft = 0, numberWidth = 0;
        RenderLine()
        {
            body->ref();
            number->ref();
        }
        ~RenderLine()
        {
            body->unref();
            number->unref();
        }
    };

    TextEditor& owner;
    tn::EditSession editing;
    TextView view;
    Context context;  // Draws the retained editor scene in local coordinates.
    Theme themeTokens = Theme::dark();
    TextEditorOptions configuration;
    EditorInputState input;
    std::unique_ptr<tvg::Text> measurer;
    std::map<size_t, std::unique_ptr<RenderLine>> linePaints;
    std::vector<std::string> displayLines;
    std::vector<std::vector<Run>> rowRuns;
    std::vector<Format> markdownFormats;
    uint64_t markdownRevision = uint64_t(-1);
    FontId boldDocumentFont = 0;
    struct Decoration
    {
        TextDecoration range;
        size_t maxEnd;
    };
    struct DecorationLayer
    {
        std::string owner;
        uint64_t revision;
        std::vector<Decoration> ranges;
    };
    std::vector<DecorationLayer> decorations;
    FontId documentFont = 0, lineNumberFont = 0, preferredFont = 0, preferredNumberFont = 0;
    float documentTop = 0;
    uint64_t textStyleRevision = 1;
    uint64_t measuredRevision = uint64_t(-1), measuredFontGeneration = 0, paintedRevision = uint64_t(-1);
    float width = 1, height = 1, horizontal = 0, vertical = 0, gutter = 0, lineHeight = 24, widest = 0, digitWidth = 0;
    // Wheel lines not yet scrolled; fractional ticks wait until they add up to a whole line.
    float wheelCarry = 0;
    bool dirty = true, layoutDirty = true, hasFocus = false, dragging = false, previousCaretVisible = false;
    bool restartCaret = true;
    double seconds = 0, caretEpoch = 0;
    std::optional<double> nextBlink;  // Set while a visible caret waits for its next phase.
    std::optional<float> horizontalGrab;
    std::string preedit, measuredPreedit;
    size_t preeditCursor = 0, measuredCursor = 0;
    // Composed rows [composedFirst, +composedRows) display document rows [composedFirst, +composedSpan).
    size_t composedFirst = 0, composedSpan = 0, composedRows = 0;
    Rect preeditCaret;
    std::vector<Rect> preeditRects;
    tn::Selection measuredSelection, paintedSelection;

    Impl(TextEditor& owner, tn::Document& document);

    void invalidate();
    void restyle();
    void resize(float w, float h);
    void focus(bool value);
    void relayout();
    void ensureCaret();
    void clampScroll();
    void edited(bool changed, bool revealCaret = true);
    bool commitEdits(const tn::EditBatch& batch, std::string& error, bool collapseUnchanged = false);
    bool update(double now);

    void handle(Context& host, std::string_view id, Rect bounds, const Event& event);
    void pointerDown(float x, float y, bool extend, int clicks);
    void pointerMove(float x, float y);
    void pointerUp();
    void cancelPointer();
    void wheel(float y, float x);
    bool key(Key key, Modifiers mods);
    void textInput(std::string_view text);

    Rect logicalCaret() const;
    Rect paintCaret();
    Rect textArea() const;
    Rect horizontalTrack() const;
    Rect horizontalThumb() const;
    float maxHorizontalScroll() const;
    float maxVerticalScroll() const;
    float contentHeight() const;

    float measure(std::string_view text, FontId font = 0) const;
    std::vector<TextLine> layoutRows(std::string_view text, size_t base, std::vector<std::string>& display, std::vector<std::vector<Run>>& runs);
    void parseMarkdown();
    Format formatAt(size_t offset) const;
    Color markdownColor(Color Theme::*token) const;
    void prepareRuns(RenderLine& line, size_t row);
    size_t replaceRows(size_t first, size_t count, std::string_view text, size_t base);
    RenderLine& prepareLine(size_t row);
    void paintDecorations(size_t first, size_t end, bool borders);
    Rect rangeRect(const TextLine& line, size_t start, size_t end) const;
};

}  // namespace ui
