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
        float left = 0, firstWidth = 0, width = 0, top = 0, scale = 1;
        FontId font = 0;
        Format format;
        bool missing = false;
        std::optional<Color> foreground = {}, background = {};
    };
    struct Widget
    {
        TextWidget range;
        size_t rule;
        Rect bounds;
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
        tvg::Scene* group = nullptr;
        tvg::Scene* background = nullptr;
        uint64_t style = 0;
        std::string display;
        std::vector<std::pair<size_t, size_t>> tabOffsets;
        size_t clipFirst = 0, clipEnd = 0;
        float clipX = 0;
        std::vector<Run> styles;
        std::vector<Widget> widgets;
        bool assigned = false, measured = false;
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
            if (group) group->unref();
            if (background) background->unref();
        }
    };

    TextEditor& owner;
    tn::EditSession editing;
    TextView view;
    Context context;  // Draws the retained editor scene in local coordinates.
    Context textContent, numberContent, codeContent;
    Theme themeTokens = Theme::dark();
    TextEditorOptions configuration;
    EditorInputState input;
    std::unique_ptr<tvg::Text> measurer;
    std::map<size_t, std::unique_ptr<RenderLine>> linePaints;
    std::map<float, size_t> lineWidths;
    size_t cachedFirst = 0, cachedEnd = 0, drawnFirst = 0, drawnEnd = 0;
    std::vector<tn::MarkdownBlock> markdownBlocks;
    std::map<size_t, std::vector<Format>> markdownFormats;
    bool referenceMarkdown = false;
    double renderOrigin = 0, preeditOrigin = 0;
    float drawnHorizontal = -1, drawnWidth = -1;
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
    struct WidgetRule
    {
        std::string owner;
        TextWidgetRule rule;
    };
    struct WidgetPopup
    {
        size_t rule;
        TextWidgetEvent event;
        bool opened = false;
        bool suggestion = false;
    };
    std::vector<WidgetRule> widgetRules;
    struct WidgetIdentity
    {
        size_t rule, start, end;
        uint64_t id;
        bool seen = false;
    };
    std::vector<WidgetIdentity> widgetIdentities;
    uint64_t widgetRevision = uint64_t(-1), nextWidgetId = 0;
    size_t widgetSourceSize = 0;
    std::optional<WidgetPopup> widgetPopup;
    uint64_t widgetGeneration = 0;
    uint64_t suggestionRevision = uint64_t(-1);
    size_t suggestionCaret = size_t(-1), suggestionAnchor = size_t(-1);
    Context* widgetHost = nullptr;
    std::string widgetEditorId;
    FontId documentFont = 0, lineNumberFont = 0, preferredFont = 0, preferredNumberFont = 0;
    float documentTop = 0, numberTop = 0, numberScale = 1;
    uint64_t textStyleRevision = 1;
    uint64_t measuredFontReset = 0;
    uint64_t measuredRevision = uint64_t(-1), measuredFontGeneration = 0, paintedRevision = uint64_t(-1);
    double vertical = 0;
    float width = 1, height = 1, horizontal = 0, gutter = 0, lineHeight = 24, boldAdvance = 0, widest = 0, digitWidth = 0;
    // Wheel lines not yet scrolled; fractional ticks wait until they add up to a whole line.
    float wheelCarry = 0;
    bool viewportReady = false;
    std::optional<std::pair<float, double>> pendingScroll;
    bool dirty = true, layoutDirty = true, textContentDirty = true, hasFocus = false, dragging = false, previousCaretVisible = false;
    bool restartCaret = true, plainMarkdown = false;
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
    double maxVerticalScroll() const;
    double contentHeight() const;

    float measure(std::string_view text, FontId font = 0, float scale = 1) const;
    void cacheTabs(RenderLine& line, std::string_view source);
    float prepareText(tvg::Text* text, const RenderLine& line, const Run* run = nullptr);
    std::vector<TextLine> layoutRows(std::string_view text, size_t base, std::vector<std::string>& display, std::vector<std::vector<Run>>& runs, std::vector<Widget>* widgets = nullptr, bool composed = false);
    void parseMarkdown(size_t& start, size_t& end, bool incremental);
    size_t markdownBlock(size_t offset) const;
    const std::vector<Format>& blockFormats(size_t block);
    void measureLine(size_t row);
    void updateWidth(float before, float after);
    std::vector<TextLine> rowMetadata(std::string_view text, size_t base);
    void prepareRuns(RenderLine& line);
    size_t replaceRows(size_t first, size_t count, std::string_view text, size_t base, bool composed = false);
    RenderLine& prepareLine(size_t row);
    void prepareViewport(size_t first, size_t end);
    void paintDecorations(size_t first, size_t end, bool borders);
    void matchWidgets(std::vector<Widget>& result, std::string_view text, size_t base, bool blocks = false);
    size_t widgetBoundary(size_t offset, bool forward);
    void snapWidgets(bool forward, bool expand = false);
    void paintWidgets(Context& host, std::string_view id, Rect bounds);
    Rect rangeRect(const TextLine& line, size_t start, size_t end) const;
};

}  // namespace ui
