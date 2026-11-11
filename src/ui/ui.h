/**
 * @file ui.h
 * @brief Public UI components, text editing views, and host input integration.
 *
 * Coordinates and font sizes use logical pixels; x increases rightward and y downward.
 * Text positions use UTF-8 byte offsets. Hosts provide window events, output scaling,
 * clipboard services, and Canvas update/draw/sync calls.
 *
 * @note Use UI objects and shared font resources on their owning UI thread.
 * Initialize ThorVG before creating graphics resources and synchronize the Canvas
 * before mutating or destroying submitted Paints. Destroy text views and UI contexts,
 * call unloadFonts(), then terminate ThorVG.
 */
#pragma once

#include <thorvg.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <variant>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <text/text.h>

// Declares the hidden state of a class; the pointer names the owning std smart pointer.
#define TN_UI_DECLARE_IMPL(pointer) \
private:                            \
    struct Impl;                    \
    std::pointer<Impl> impl

/**
 * @defgroup ui_fonts Font resources
 * @{
 */
namespace ui
{
/**
 * @brief Shared UI font identifier; zero denotes an invalid or missing face.
 */
using FontId = uint32_t;

/**
 * @brief Metadata for a loaded font face, retained until unloadFonts().
 */
struct FontInfo
{
    FontId id = 0;          ///< Nonzero identifier assigned on successful loading.
    std::string family;     ///< Family name from the font's name table.
    std::string name;       ///< Font name, or the caller's alias for a memory-loaded face.
    std::string path;       ///< Canonical UTF-8 file path; empty for memory-loaded faces.
    unsigned index = 0;     ///< Face index; only zero is supported.
    unsigned weight = 400;  ///< Weight inferred from the font header: 400 or 700.
    bool italic = false;    ///< Whether the font header marks the face as italic.
};

/**
 * @brief Loads a standalone font face from a filesystem path.
 *
 * @code
 * auto regular = ui::loadFont("fonts/regular.ttf");
 * if (!regular) report(ui::fontError());
 * context.fontFor = [=](std::string_view, bool) { return regular; };
 *
 * // At shutdown, after destroying editors and contexts:
 * ui::unloadFonts();
 * @endcode
 *
 * @param[in] utf8Path Non-null, nonempty UTF-8 path; resolved to a canonical path.
 * @param[in] index Face index; only zero is supported. Collections are rejected.
 *
 * @return A nonzero font ID on success, or zero; inspect fontError() on failure.
 * @note Repeated canonical path/index pairs reuse the existing face.
 *
 * @see unloadFonts()
 */
FontId loadFont(const char* utf8Path, unsigned index = 0);

/**
 * @brief Loads a standalone font face from copied memory.
 *
 * @code
 * auto bytes = readFile("fonts/mono.ttf");
 * auto mono = ui::loadFont("Mono", bytes.data(), bytes.size());  // The bytes are copied.
 * if (!mono) report(ui::fontError());
 * @endcode
 *
 * @param[in] name Optional alias; null or empty uses the font's embedded name.
 * @param[in] bytes Font data copied before the call returns.
 * @param[in] size Number of bytes available at bytes.
 * @param[in] index Face index; only zero is supported.
 *
 * @return A nonzero font ID on success, or zero; inspect fontError() on failure.
 * @note The caller may release the input bytes after this call.
 */
FontId loadFont(const char* name, const void* bytes, size_t size, unsigned index = 0);

/**
 * @brief Finds a loaded face by normalized name, family, or file stem.
 *
 * @code
 * auto regular = ui::findFont("DejaVu Sans");
 * auto strong = ui::findFont("dejavu-sans", 700);  // Case, spaces, hyphens and underscores are ignored.
 * context.fontFor = [=](std::string_view, bool bold) { return bold ? strong : regular; };
 * @endcode
 *
 * @param[in] name Lookup name; ASCII case, spaces, hyphens, and underscores are ignored.
 * @param[in] weight Requested weight used to rank matching faces.
 * @param[in] italic Requested italic flag used to rank matching faces.
 * @return Closest matching font ID, or zero if none matches.
 */
FontId findFont(std::string_view name, unsigned weight = 400, bool italic = false);

/**
 * @brief Returns metadata for all loaded faces.
 *
 * @return Borrowed vector; element references may be invalidated by loading or unloading fonts.
 */
const std::vector<FontInfo>& fonts();

/**
 * @brief Returns the shared font operation error message.
 *
 * @return Borrowed string; copy it before another font operation changes the message.
 */
const std::string& fontError();

/**
 * @brief Releases shared font data and registered ThorVG font aliases.
 *
 * @warning Destroy text editors and dependent Paints before calling this function.
 * All previous FontIds become invalid. Call before terminating ThorVG.
 */
void unloadFonts();

/** @} */
}  // namespace ui

/**
 * @defgroup ui_theme Theme
 * @{
 */
namespace ui
{

/**
 * @brief RGBA channels in the range [0, 255]; alpha zero is transparent.
 */
struct Color
{
    uint8_t r, g, b, a = 255;
};

/**
 * @brief Semantic colors, component dimensions, and default font names.
 *
 * @code
 * auto theme = ui::Theme::dark();
 * theme.accent = {255, 140, 0};  // Alpha defaults to opaque.
 * theme.fontSize = 14;
 * context.begin(theme, viewport);  // Borrowed: keep the theme alive while the frame is in use.
 * @endcode
 *
 * @note Sizes use logical pixels. Context borrows a Theme during frame construction
 * and input dispatch; keep it alive until the next Context::begin() or destruction.
 */
struct Theme
{
    Color background, surface, raised, border, text, muted;
    Color accent, accentText, selection, success, danger;
    float radius = 7;
    float fontSize = 13;  // Nominal em in logical px, independent of font-point and output-pixel units.
    float rowHeight = 32;
    std::string font = "DejaVuSans";
    std::string boldFont = "DejaVuSans-Bold";

    /** @brief Returns the built-in dark theme tokens and dimensions. */
    static Theme dark();

    /** @brief Returns the built-in light theme tokens and dimensions. */
    static Theme light();
};

/** @} */
}  // namespace ui

/**
 * @defgroup ui_frames Frame scheduling
 * @{
 */
namespace ui
{
/**
 * @brief Returns monotonic seconds relative to the first call.
 *
 * @code
 * // Host loop: draw, then sleep until input arrives or a requested frame is due.
 * context.begin(theme, viewport, ui::frameTime());
 * build();
 * context.end();
 * if (auto next = context.nextFrame()) waitForInput(std::max(0.0, *next - ui::frameTime()));
 * else waitForInput();
 * @endcode
 *
 * @return A process-local time suitable for frame deadlines; not a wall-clock timestamp.
 */
double frameTime();

/** @} */
}  // namespace ui

/**
 * @defgroup ui_icons Icons
 * @{
 */
namespace ui
{

/**
 * @brief Interaction flags used to select an icon pose and animation target.
 */
struct IconState
{
    bool hovered = false, pressed = false, active = false;
    bool enabled = true, focused = false;
};

/**
 * @brief Encoded icon data and interaction markers owned by the asset value.
 *
 * @code
 * // A Lottie icon whose hover pose is authored under the marker "over":
 * ui::IconAsset pin{.name = "pin", .data = readFile("pin.json"), .mime = "lot", .tint = false};  // Keeps the authored colors.
 * pin.markers[1] = "over";  // Order: idle, hover, pressed, active, disabled, focused.
 *
 * std::string error;
 * context.icons({pin}, error);
 * @endcode
 *
 * @note Marker order is idle, hover, pressed, active, disabled, focused. Hover,
 * active, and focused markers may supply an additional <marker>-overshoot pose.
 */
struct IconAsset
{
    std::string name, data, mime;
    bool tint = true;
    float duration = .16f;
    std::array<std::string, 6> markers{"idle", "hover", "pressed", "active", "disabled", "focused"};
};

/**
 * @brief Validates encoded icon data and marker aliases.
 *
 * @code
 * std::string error;
 * if (!ui::validateIcon(asset, error)) report(error);  // Such as "Icon 'pin': unknown pose marker 'over'".
 * @endcode
 *
 * @param[in] asset Candidate asset; missing standard poses fall back to idle/frame zero.
 * @param[out] error Diagnostic on failure; cleared on success.
 *
 * @return Whether the asset can be loaded with the requested markers.
 */
bool validateIcon(const IconAsset& asset, std::string& error);

/**
 * @brief Returns the names of the built-in icons, such as "folder", "search" or "close".
 *
 * @code
 * // Lay out every built-in icon in a row:
 * ui::Rect cell{8, 8, 24, 24};
 * for (auto name : ui::builtinIcons()) {
 *     context.icon(cell, name, theme.text);
 *     cell.x += 32;
 * }
 * @endcode
 *
 * @return Borrowed static names in asset order.
 */
const std::vector<std::string_view>& builtinIcons();

/**
 * @brief Retains icon assets and per-instance animation state across frames.
 *
 * Icons are identified by name everywhere: a user asset overrides a built-in of the same name.
 *
 * @code
 * // Context owns one and Context::icon() covers the usual case. Used on its own:
 * ui::Icons icons;
 *
 * // Each frame:
 * icons.begin(context.time());
 * auto picture = icons.picture("toolbar/pin", "star", {.hovered = hovered}, theme.text, [&](tvg::Picture* paint) { context.add(paint); });
 * if (picture) ui::transform(picture, {1, 0, bounds.x, 0, 1, bounds.y, 0, 0, 1});
 * icons.end();
 * if (icons.animating()) context.requestAnimationFrame();
 * @endcode
 *
 * @note Pair begin() and end(); instances not requested during the frame are released.
 */
class Icons
{
public:
    Icons();
    ~Icons();
    Icons(const Icons&) = delete;
    Icons& operator=(const Icons&) = delete;

    /**
     * @brief Replaces user assets after all candidates validate.
     *
     * @param[in] assets Owned candidate assets; built-ins remain available.
     * @param[out] error Diagnostic on failure.
     *
     * @return Whether replacement succeeded; failure preserves the previous asset set.
     */
    bool replace(std::vector<IconAsset> assets, std::string& error);

    /**
     * @brief Tests whether a built-in or user asset name is available.
     */
    bool contains(std::string_view name) const;

    /**
     * @brief Starts an icon frame on the host monotonic clock.
     *
     * @param[in] seconds Time used to advance retained animations.
     *
     * @see Icons::end()
     */
    void begin(double seconds);

    /**
     * @brief Returns an instance's retained picture and updates its pose.
     *
     * @param[in] instance Stable per-instance identity.
     * @param[in] name Built-in or user asset name.
     * @param[in] state Interaction flags selecting the animation target.
     * @param[in] tint Color used when the asset permits tinting.
     * @param[in] prepare Optional callback invoked before animation/color mutations; reparent here.
     *
     * @return Borrowed picture, or null if no picture is available. Do not delete or unref it.
     *
     * @note Keep the instance registered each frame; end() releases unused instances.
     */
    tvg::Picture* picture(std::string_view instance, std::string_view name, const IconState& state, Color tint,
                          const std::function<void(tvg::Picture*)>& prepare = {});

    /**
     * @brief Releases instances not requested since begin().
     */
    void end();

    /**
     * @brief Reports whether an icon still needs animation frames.
     */
    bool animating() const;

    TN_UI_DECLARE_IMPL(unique_ptr);
};

/** @} */
}  // namespace ui

/**
 * @defgroup ui_context Drawing and input
 * @{
 */
namespace ui
{

/**
 * @brief Axis-aligned bounds in logical pixels, with an upper-left origin.
 *
 * @code
 * ui::Rect panel{0, 0, 240, 400};
 * auto content = panel.inset(8);                       // {8, 8, 224, 384}
 * auto visible = content.intersect({0, 0, 100, 100});  // {8, 8, 92, 92}
 * bool inside = visible.contains(8, 8);                // True: the left and top edges are inclusive.
 * @endcode
 */
struct Rect
{
    float x = 0, y = 0, w = 0, h = 0;

    /**
     * @brief Tests inclusion with left/top inclusive and right/bottom exclusive edges.
     */
    bool contains(float px, float py) const;

    /**
     * @brief Returns bounds inset on all sides; resulting width and height are clamped to zero.
     */
    Rect inset(float amount) const;

    /**
     * @brief Returns the overlap; disjoint bounds produce zero width or height.
     */
    Rect intersect(Rect other) const;
};

/**
 * @brief Applies a Paint matrix only when it differs from the current matrix.
 *
 * @code
 * ui::transform(scene, {1, 0, bounds.x, 0, 1, bounds.y, 0, 0, 1});  // Translate to the bounds origin.
 * @endcode
 *
 * @param[in] paint Paint to update; null is ignored.
 * @param[in] matrix Desired transform.
 *
 * @note Avoids invalidating unchanged retained geometry.
 */
void transform(tvg::Paint* paint, const tvg::Matrix& matrix);

/**
 * @brief Normalized keys shared by controls and text views; committed text uses Context::textInput().
 */
enum class Key
{
    Unknown,    ///< Not a mapped key; also the value carried by events other than Key.
    Tab,        ///< Moves focus forward, or backward with shift; a TabInput control receives it instead.
    Enter,      ///< Activates the focused button or list row; breaks the line in an editor.
    Space,      ///< Activates the focused button or list row; the typed character arrives through Context::textInput().
    Escape,     ///< Cancels a captured drag, dismisses a modal layer, or drops an editor preedit.
    Left,       ///< Moves the caret back by a character, or by a word with Modifiers::word.
    Right,      ///< Moves the caret forward by a character, or by a word with Modifiers::word.
    Up,         ///< Moves the caret or the list selection up by a line or row.
    Down,       ///< Moves the caret or the list selection down by a line or row.
    Home,       ///< Moves to the start of the line, or of the document with Modifiers::primary.
    End,        ///< Moves to the end of the line, or of the document with Modifiers::primary.
    PageUp,     ///< Moves the caret up by one viewport height.
    PageDown,   ///< Moves the caret down by one viewport height.
    Backspace,  ///< Deletes before the caret, or the previous word with Modifiers::word.
    Delete,     ///< Deletes after the caret, or the next word with Modifiers::word.
    A,          ///< With Modifiers::primary, selects all.
    C,          ///< With Modifiers::primary, copies the selection.
    X,          ///< With Modifiers::primary, cuts the selection.
    V,          ///< With Modifiers::primary, pastes the clipboard.
    Z,          ///< With Modifiers::primary, undoes; with shift as well, redoes.
    Y           ///< With Modifiers::primary, redoes.
};

/**
 * @brief Semantic modifiers: selection extension, primary shortcut, and word navigation.
 *
 * @see platform::modifiers()
 */
struct Modifiers
{
    bool shift = false, primary = false, word = false;
};

/**
 * @brief Control events generated from pointer, keyboard, and text input.
 */
enum class EventType
{
    Press,        ///< The pointer went down on the control; carries position, modifiers and click count.
    Drag,         ///< The pointer moved while pressed on the control; carries position.
    Activate,     ///< The pointer was released over the control it pressed: a click.
    Key,          ///< A key for the focused control; carries key and modifiers.
    Text,         ///< Committed text for the focused control; carries text.
    Scroll,       ///< The wheel moved over a Scrollable control; x/y carry the horizontal/vertical deltas.
    Release,      ///< The pointer was released anywhere after pressing a Capture control; precedes Activate.
    Cancel,       ///< The press on a Capture control was abandoned, such as by Escape or Context::cancelPointer().
    ContextMenu,  ///< A menu was requested at the position through Context::contextMenu().
    Hover         ///< The pointer moved over a control registered with ControlFlag::Hover.
};

/**
 * @brief Host cursor requests for text and splitter hit regions.
 */
enum class Cursor
{
    Default,           ///< The regular arrow.
    Text,              ///< The I-beam shown over editable text.
    ResizeHorizontal,  ///< Left-right arrows over a divider dragged along the x axis.
    ResizeVertical     ///< Up-down arrows over a divider dragged along the y axis.
};

/**
 * @brief A dispatched control event.
 *
 * @code
 * auto handler = [&](const ui::Event& event) {
 *     if (event.type == ui::EventType::Press) anchor = {event.x, event.y};
 *     else if (event.type == ui::EventType::Drag) dragTo(event.x, event.y);
 *     else if (event.type == ui::EventType::Scroll) scroll -= event.y * 20;
 *     else if (event.type == ui::EventType::Key && event.key == ui::Key::Escape) cancel();
 *     else if (event.type == ui::EventType::Text) append(event.text);
 * };
 * context.control("canvas", bounds, handler, true, ui::ControlFlag::Focusable | ui::ControlFlag::Scrollable);
 * @endcode
 *
 * @note Pointer coordinates are logical pixels; Scroll uses x/y as horizontal/vertical
 * deltas. Press carries the click count, Key a normalized key, and Text committed UTF-8 input.
 */
struct Event
{
    EventType type;
    float x = 0, y = 0;
    Key key = Key::Unknown;
    Modifiers modifiers = {};
    int clicks = 1;
    std::string text = {};
    mutable bool handled = true;  ///< A Scroll handler may clear this to let an enclosing region or host scroll.

    /** @brief Whether a pointer activation or keyboard confirmation should invoke this control. */
    bool activated(bool space = true) const
    {
        return type == EventType::Activate || (type == EventType::Key && (key == Key::Enter || (space && key == Key::Space)));
    }
};

/**
 * @brief Interaction snapshot returned when a control is registered for the current frame.
 */
struct State
{
    bool hovered, pressed, focused;
};

/**
 * @brief Optional control behavior; combine values with operator|.
 *
 * @code
 * // A scroll area takes the wheel but never keyboard focus:
 * context.control("log/scroll", bounds, onScroll, true, ui::ControlFlag::Scrollable);
 *
 * // An editor keeps Tab for itself and is told when a drag ends outside its bounds:
 * context.control("editor", bounds, onInput, true, ui::ControlFlag::Focusable | ui::ControlFlag::Capture | ui::ControlFlag::TabInput);
 * @endcode
 */
enum class ControlFlag : uint8_t
{
    None = 0,        ///< Receives pointer events only.
    Focusable = 1,   ///< Participates in keyboard focus.
    Scrollable = 2,  ///< Receives Scroll events from Context::wheel().
    Capture = 4,     ///< Receives Release outside its bounds and Cancel.
    TabInput = 8,    ///< Receives Tab as a Key event instead of moving focus.
    Hover = 16       ///< Receives Hover events from Context::pointerMove().
};

/** @brief Wheel behavior for a scroll region; offsets and limits use the same units. */
struct ScrollOptions
{
    float step = 36;
    bool horizontal = false;  ///< Horizontal strips also accept the dominant vertical wheel delta.
    bool enabled = true;
};

constexpr ControlFlag operator|(ControlFlag a, ControlFlag b)
{
    return ControlFlag(uint8_t(a) | uint8_t(b));
}

/**
 * @brief Alignment along the component or layout axis.
 */
enum class Align
{
    Start,   ///< The left of a label; the top of a row or the left of a column.
    Center,  ///< The middle of the label bounds or of the cross axis.
    End      ///< The right of a label; the bottom of a row or the right of a column.
};

/**
 * @brief A literal RGBA color or a live pointer to a Theme color member.
 *
 * Appearance stores its colors in this form, so a component style can either fix a color
 * or keep following the theme. Scripts write the same values as strings, such as "accent"
 * or "#ff0000".
 *
 * @code
 * // Overriding the colors of a button:
 * ui::Appearance appearance{
 *     .fill = ui::StyleColor{.value = {255, 0, 0, 255}},         // A literal: always red.
 *     .hoverFill = ui::StyleColor{.token = &ui::Theme::accent},  // A token: follows the current theme.
 * };
 * ui::button(context, "delete", bounds, "Delete", discard, {.appearance = appearance});
 *
 * // A custom component reads it back against the theme of the frame:
 * auto fill = ui::resolve(appearance.fill, context.theme(), context.theme().raised);
 * context.rect(bounds, fill);
 * @endcode
 *
 * @note A non-null token takes precedence over value.
 *
 * @see Appearance
 * @see styleColor()
 * @see resolve()
 */
struct StyleColor
{
    Color value{0, 0, 0, 0};
    Color Theme::*token = nullptr;

    /**
     * @brief Resolves the referenced theme member, or the literal value when no token is set.
     */
    Color resolve(const Theme& theme) const { return token ? theme.*token : value; }
};

/**
 * @brief Optional component style overrides.
 *
 * @code
 * ui::Appearance appearance;
 * ui::StyleColor accent;
 * if (ui::styleColor("accent", accent)) appearance.fill = accent;  // Follows the current theme.
 * appearance.radius = 4;
 * ui::button(context, "ok", bounds, "OK", confirm, {.appearance = appearance});
 * @endcode
 *
 * @note Unspecified colors inherit component defaults. Negative radius inherits
 * the theme; fontSize zero inherits the component size. Tokens resolve against the current theme.
 *
 * @see StyleColor
 * @see ButtonOptions::appearance
 * @see Element::appearance
 */
struct Appearance
{
    std::optional<StyleColor> fill, border, text;
    std::optional<StyleColor> hoverFill, pressedFill, activeFill, disabledFill;
    float radius = -1, borderWidth = 1, fontSize = 0;
    bool bold = false;
    std::optional<Align> textAlign;
    bool focusRing = false;
};

/**
 * @brief Resolves an optional style color; absent values use the supplied fallback.
 *
 * @code
 * // Inside a component: the override when the caller set one, otherwise the default.
 * auto fill = ui::resolve(appearance.fill, context.theme(), context.theme().raised);
 * @endcode
 *
 * @see StyleColor::resolve()
 */
Color resolve(const std::optional<StyleColor>& color, const Theme& theme, Color fallback);

/**
 * @brief Parses a theme token, transparent, #RRGGBB, or #RRGGBBAA.
 *
 * @code
 * ui::StyleColor color;
 * ui::styleColor("accent", color);     // Theme token.
 * ui::styleColor("#3b82f680", color);  // #RRGGBBAA literal.
 * ui::styleColor("transparent", color);
 * @endcode
 *
 * @param[out] output Resolved literal or token; unchanged on failure.
 *
 * @return Whether the value is recognized.
 *
 * @see StyleColor
 */
bool styleColor(std::string_view value, StyleColor& output);

/**
 * @brief Image fitting policy: preserve aspect inside bounds, cover bounds, or stretch.
 */
enum class ImageFit
{
    Contain,  ///< Preserve aspect ratio and fit inside bounds.
    Cover,    ///< Preserve aspect ratio and fill bounds; content may extend beyond them.
    Stretch   ///< Scale each axis independently to fill bounds.
};

/**
 * @brief Owned encoded image bytes, decoder hint, and application-defined asset name.
 */
struct ImageAsset
{
    std::string name, data, mime;
};

class Context;

/**
 * @brief Shares decoded assets while retaining a separate Picture for each visible instance.
 *
 * @code
 * ui::Images images;
 * std::string error;
 * images.replace({{"logo", readFile("logo.png"), "png"}}, error);
 *
 * // Each frame, between Context::begin() and end():
 * images.begin();
 * images.render(context, "header/logo", "logo", {8, 8, 64, 64}, ui::ImageFit::Contain);
 * images.end();
 * @endcode
 *
 * @note Call begin(), render() for each instance, and end() inside a synchronized UI frame.
 */
class Images
{
public:
    Images();
    ~Images();
    Images(const Images&) = delete;
    Images& operator=(const Images&) = delete;

    /**
     * @brief Validates and replaces the image asset set.
     *
     * @param[out] error Diagnostic on failure; cleared on success.
     *
     * @return Whether replacement succeeded; failure preserves previous assets.
     */
    bool replace(std::vector<ImageAsset> assets, std::string& error);

    /**
     * @brief Tests whether a named asset is loaded.
     */
    bool contains(std::string_view name) const;

    /**
     * @brief Marks retained instances unused until render() requests them.
     */
    void begin();

    /**
     * @brief Adds a retained image instance to the current Context frame.
     *
     * @note The instance name must be stable and unique. Opacity is clamped to [0, 1].
     * Missing assets and nonpositive bounds produce no Paint. Cover requires a host clip to bounds.
     */
    void render(Context& context, std::string_view instance, std::string_view name, Rect bounds, ImageFit fit, float opacity = 1);

    /**
     * @brief Releases instances not requested since begin().
     */
    void end();

    TN_UI_DECLARE_IMPL(unique_ptr);
};

/**
 * @brief Geometry source for a rectangle, ellipse, or authored path.
 */
enum class ShapeKind
{
    Rect,     ///< A rectangle filling the bounds, rounded by the radius argument of shape().
    Ellipse,  ///< An ellipse inscribed in the bounds.
    Path      ///< ShapeData::path scaled from the view size into the bounds.
};

/**
 * @brief Path command interpreted by shape().
 */
enum class PathCommand
{
    Move,   ///< Starts a subpath at points[0..1].
    Line,   ///< Draws a straight line to points[0..1].
    Cubic,  ///< Draws a Bezier curve with controls points[0..1] and points[2..3], ending at points[4..5].
    Close   ///< Closes the subpath back to its start; uses no points.
};

/**
 * @brief One path command in view-space coordinates.
 *
 * @note Move/Line use x,y; Cubic uses two control points and one endpoint; Close uses no points.
 */
struct PathSegment
{
    PathCommand command = PathCommand::Move;
    std::array<float, 6> points{};
};

/**
 * @brief Shape geometry scaled from viewWidth/viewHeight into the requested bounds.
 */
struct ShapeData
{
    ShapeKind kind = ShapeKind::Rect;
    float viewWidth = 100, viewHeight = 100;
    std::vector<PathSegment> path;
};

/**
 * @brief Draws authored geometry with fill and optional border in the current frame.
 *
 * @code
 * ui::ShapeData triangle{.kind = ui::ShapeKind::Path, .viewWidth = 10, .viewHeight = 10, .path = {
 *     {ui::PathCommand::Move, {5, 0}},
 *     {ui::PathCommand::Line, {10, 10}},
 *     {ui::PathCommand::Line, {0, 10}},
 *     {ui::PathCommand::Close},
 * }};
 * ui::shape(context, {8, 8, 40, 40}, triangle, theme.accent, theme.border);
 * @endcode
 *
 * @note Bounds and stroke dimensions use logical pixels; path points use ShapeData view coordinates.
 */
void shape(Context& context, Rect bounds, const ShapeData& data, Color fill, Color border, float borderWidth = 1, float radius = 0);

/**
 * @brief Extends a Context::paint() cache key with raw bytes.
 *
 * @code
 * // The Paint of a label depends on its text as well as on its geometry:
 * auto key = ui::paintHash(title, ui::paintKey('L', bounds, color));
 * context.paint(key, [&] { return makeLabel(title, bounds, color); });
 * @endcode
 *
 * @param[in] bytes Bytes affecting the produced Paint, such as label text.
 * @param[in] seed Key to extend; the default starts a new key.
 */
uint64_t paintHash(std::string_view bytes, uint64_t seed = 0xcbf29ce484222325ull);

/**
 * @brief Hashes trivially copyable values into a Context::paint() cache key.
 *
 * @code
 * context.paint(ui::paintKey('D', center, radius, color), [&] {
 *     auto dot = tvg::Shape::gen();
 *     dot->appendCircle(center.x, center.y, radius, radius);
 *     dot->fill(color.r, color.g, color.b, color.a);
 *     return dot;
 * });
 * @endcode
 *
 * @note Includes raw object bytes and padding; use initialized values, not serialized or persistent keys.
 */
template<typename... T>
uint64_t paintKey(const T&... values)
{
    static_assert((std::is_trivially_copyable<T>::value && ...), "paint keys hash raw bytes");
    uint64_t key = paintHash({});
    ((key = paintHash({reinterpret_cast<const char*>(&values), sizeof(values)}, key)), ...);
    return key;
}

/**
 * @brief Builds immediate UI frames backed by retained ThorVG Paints.
 *
 * Pair begin()/end(). Drawing calls reuse Paints at the same call position when
 * inputs match. Register controls every frame; input dispatch uses the last frame.
 *
 * @code
 * ui::Context context;
 * canvas->add(context.scene());
 *
 * // Each frame, after synchronizing the Canvas:
 * context.begin(theme, {0, 0, width, height});
 * ui::button(context, "save", {8, 8, 80, 28}, "Save", [&] { save(); });
 * context.end();
 *
 * // Between frames, forward host input:
 * context.pointerDown(x, y);
 * context.pointerUp(x, y);
 * @endcode
 *
 * @note Keep the borrowed Theme, callback captures, and referenced models alive until
 * the next begin(). Synchronize the Canvas before rebuilding a previously drawn scene.
 */
class Context
{
public:
    using Handler = std::function<void(const Event&)>;

    Context();
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    /**
     * @brief Begins a frame and resets controls, drawing registration, and frame requests.
     *
     * @param[in] theme Borrowed theme; keep alive through subsequent input dispatch.
     * @param[in] viewport Root bounds and clip in logical pixels.
     * @param[in] seconds Monotonic frame time; a negative or nonfinite value uses frameTime().
     *
     * @note Call end() after registering this frame's components.
     */
    void begin(const Theme& theme, Rect viewport, double seconds = -1);

    /**
     * @brief Finalizes registration and removes Paints omitted from this frame.
     *
     * @note Does not call Canvas::update(), draw(), or sync().
     */
    void end();

    /**
     * @brief Returns the current frame time in monotonic seconds.
     */
    double time() const;

    /**
     * @brief Requests a frame at an absolute monotonic deadline; retains the earliest request.
     *
     * @code
     * // A blinking marker: draw the current phase and wake up when it changes.
     * bool lit = std::fmod(context.time(), 1.0) < 0.5;
     * if (lit) context.rect(bounds, theme.text);
     * context.requestFrameAt(std::floor(context.time() * 2 + 1) / 2);
     * @endcode
     */
    void requestFrameAt(double when);

    /**
     * @brief Requests another frame approximately 1/60 second after the current frame time.
     *
     * @code
     * // Ease a panel open, asking for frames until it settles:
     * width += (target - width) * 0.2f;
     * if (std::abs(target - width) > 0.5f) context.requestAnimationFrame();
     * @endcode
     */
    void requestAnimationFrame();

    /**
     * @brief Returns the earliest frame time requested during the current frame.
     *
     * @return Monotonic seconds on the begin() clock, or empty when the host may wait for input.
     */
    std::optional<double> nextFrame() const;

    /**
     * @brief Restricts subsequent input registration to a modal layer.
     *
     * @code
     * // Draw after the rest of the frame; controls registered earlier stop receiving input.
     * if (confirming) {
     *     context.modal("confirm/ok", [&] { confirming = false; });  // Escape dismisses.
     *     context.rect(viewport, {0, 0, 0, 125});
     *     ui::button(context, "confirm/ok", {200, 150, 80, 28}, "Delete", [&] { confirming = false; });
     * }
     * @endcode
     *
     * @param[in] initialFocus Control ID focused when entering modal mode.
     * @param[in] dismiss Optional Escape dismissal callback.
     *
     * @note Re-register the modal layer each frame while it remains open.
     */
    void modal(std::string_view initialFocus, std::function<void()> dismiss = {});

    /**
     * @brief Returns the retained root scene.
     *
     * @return Borrowed Scene owned by this Context; retain with ref() if needed beyond its lifetime.
     *
     * @note Submit through the host Canvas or Context::add(); do not mutate its children externally.
     */
    tvg::Scene* scene() const;

    /**
     * @brief Returns the borrowed current theme.
     *
     * @pre begin() has established the current frame theme.
     */
    const Theme& theme() const;

    /**
     * @brief Optional font selector invoked for label measurement and rendering.
     *
     * @code
     * context.fontFor = [=](std::string_view text, bool bold) {
     *     if (!ascii(text)) return fallback;  // A face covering the script of the label.
     *     return bold ? strong : regular;
     * };
     * @endcode
     *
     * @return A shared UI font ID; zero falls back to the theme font name.
     *
     * @note The callback and captured state must remain valid during frame construction.
     */
    std::function<FontId(std::string_view text, bool bold)> fontFor;

    /**
     * @brief Registers a stable control ID, hit region, and input handler for the frame.
     *
     * @code
     * auto state = context.control("swatch", bounds, [&](const ui::Event& event) {
     *     if (event.type == ui::EventType::Activate) pickColor();
     * });
     * context.rect(bounds, state.hovered ? theme.raised : theme.surface);
     * @endcode
     *
     * @param[in] id Identity used for focus and pointer capture; keep unique within the Context.
     * @param[in] bounds Logical hit bounds, intersected with the current clip.
     * @param[in] handler Callback retained until the next begin().
     * @param[in] enabled Whether input can be dispatched to this control.
     * @param[in] flags Focus, scroll, capture, and Tab behavior.
     * @param[in] cursor Cursor requested while the pointer is over this control.
     *
     * @return Current hover, press, and focus state.
     */
    State control(std::string_view id, Rect bounds, Handler handler, bool enabled = true, ControlFlag flags = ControlFlag::Focusable, Cursor cursor = Cursor::Default);

    /**
     * @brief Requests keyboard focus by stable control ID; an empty ID clears focus.
     *
     * @code
     * context.focus("search");  // The control registered under this ID receives keys and text.
     * if (context.focused() == "search") showHint();
     * context.focus({});  // Clears focus.
     * @endcode
     */
    void focus(std::string_view id);

    /**
     * @brief Returns the focused control ID, or an empty string when no control is focused.
     */
    const std::string& focused() const;

    /**
     * @brief Tests whether the focused enabled control center lies inside the supplied logical bounds.
     *
     * @code
     * // Outline a panel while one of its controls has keyboard focus:
     * if (context.focusedWithin(panel)) context.focusRing(panel);
     * @endcode
     */
    bool focusedWithin(Rect bounds) const;

    /**
     * @brief Tests whether any registered control occupies the point, including disabled controls.
     *
     * @code
     * // Title bar hit test: drag the window only where no control sits.
     * bool draggable = y < 40 && !context.controlAt(x, y);
     * @endcode
     *
     * @note Exclude these bounds from native window drag regions.
     */
    bool controlAt(float x, float y) const;

    /**
     * @brief Clears pointer capture and sends Cancel to a capturing control when applicable.
     *
     * @code
     * // The window lost focus while a splitter or a selection was being dragged:
     * context.cancelPointer();
     * @endcode
     */
    void cancelPointer();

    /**
     * @brief Returns the cursor requested by the control under the pointer.
     *
     * @code
     * context.pointerMove(x, y);
     * setNativeCursor(context.cursor());  // Such as Cursor::Text over an editor.
     * @endcode
     */
    Cursor cursor() const;

    /**
     * @brief Updates hover coordinates without sending a drag event.
     *
     * @code
     * // The content moved under a resting pointer; refresh hover without sending a drag:
     * context.pointerPosition(x, y);
     * @endcode
     *
     * @note Use after host scrolling or output-scale changes.
     */
    void pointerPosition(float x, float y);

    /**
     * @brief Updates hover and dispatches captured drag in logical coordinates.
     */
    void pointerMove(float x, float y);

    /**
     * @brief Begins pointer interaction at a logical position and updates focus/capture.
     *
     * @code
     * // Host pointer events, converted to logical pixels:
     * context.pointerDown(x, y, ui::platform::modifiers({shift, control, alt, meta}), clicks);
     * context.pointerMove(x, y);
     * context.pointerUp(x, y);
     * @endcode
     *
     * @param[in] modifiers Modifiers held while pressing; shift extends text selections.
     * @param[in] clicks Consecutive click count reported by the host.
     */
    void pointerDown(float x, float y, Modifiers modifiers = {}, int clicks = 1);

    /**
     * @brief Releases pointer interaction and activates the matching pressed control when applicable.
     */
    void pointerUp(float x, float y);

    /**
     * @brief Dispatches a context-menu request to the enabled control at the logical point.
     *
     * @code
     * // Secondary click: the control under the point receives EventType::ContextMenu.
     * context.contextMenu(x, y);
     * @endcode
     */
    void contextMenu(float x, float y);

    /**
     * @brief Dispatches a normalized key to the focused control, including focus traversal.
     *
     * @code
     * context.key(ui::Key::Tab);                   // Focuses the next control.
     * context.key(ui::Key::Tab, {.shift = true});  // Focuses the previous control.
     * context.key(ui::Key::Z, {.primary = true});  // Reaches the focused control as a Key event.
     * @endcode
     */
    void key(Key key, Modifiers modifiers = {});

    /**
     * @brief Dispatches committed UTF-8 text to the focused enabled control.
     *
     * @code
     * // Committed text from the keyboard or the IME, not raw key codes:
     * context.textInput(committed);
     * @endcode
     */
    void textInput(std::string_view text);

    /**
     * @brief Dispatches scroll deltas to the topmost enabled scrollable control under the pointer.
     *
     * @code
     * // Scroll the page when no control under the pointer took the wheel:
     * if (!context.wheel(vertical, horizontal)) page -= vertical * 40;
     * @endcode
     *
     * @return Whether a control consumed the scroll event.
     */
    bool wheel(float vertical, float horizontal = 0);

    /**
     * @brief Registers a scroll region and clamps its retained offset to the current limit.
     * @note Offset must remain alive until the next begin(). Integral offsets advance in whole steps.
     * Empty regions and exhausted directions release the wheel to enclosing regions or the host.
     */
    template<typename T>
    void scroll(std::string_view id, Rect bounds, T& offset, T limit, ScrollOptions options = {})
    {
        offset = std::clamp(offset, T(0), std::max(T(0), limit));
        control(id, bounds, [&offset, limit, options](const Event& event) {
            if (event.type != EventType::Scroll) return;
            float delta = options.horizontal ? (std::abs(event.x) > std::abs(event.y) ? event.x : -event.y) : -event.y;
            event.handled = scrollOffset(offset, limit, delta * options.step); }, options.enabled && limit > 0, ControlFlag::Scrollable);
    }

    /** @brief Registers both axes of a scroll region; horizontal and vertical deltas stay independent. */
    void scroll(std::string_view id, Rect bounds, float& x, float& y, float limitX, float limitY, ScrollOptions options = {});

    /** @brief Routes navigation keys to a registered scroll region without changing keyboard focus. */
    bool scrollKey(std::string_view id, Key key);

    /**
     * @brief Draws a callback inside bounds intersected with the current clip.
     *
     * @code
     * context.clip({0, 40, 240, 400}, [&] {
     *     context.text({8, 48 - scroll, 400, 20}, title, theme.text);  // Cut at the clip edges.
     * });
     * @endcode
     *
     * @note The callback executes immediately; an empty intersection skips it.
     */
    void clip(Rect bounds, const std::function<void()>& render);

    /**
     * @brief Returns the active nested clip in logical coordinates.
     */
    Rect clip() const;

    /**
     * @brief Registers an existing Paint in the current layer for this frame.
     *
     * @code
     * auto chart = tvg::Scene::gen();
     * chart->ref();  // The component keeps its own reference across frames.
     *
     * // Each frame the chart is visible:
     * context.add(chart);
     *
     * // When the component is destroyed:
     * chart->unref();
     * @endcode
     *
     * @param[in] paint Borrowed Paint; null and duplicate registration in a layer are ignored.
     *
     * @note Context retains the Paint. Register it every frame; omission removes it.
     * Queue reusable component Scenes before changing their children so damage uses their previous bounds.
     */
    void add(tvg::Paint* paint);

    /**
     * @brief Reuses a Paint at this drawing position when its key matches the previous frame.
     *
     * @param[in] key Hash of all inputs affecting the produced Paint.
     * @param[in] make Factory called on a cache miss; returns a new Paint whose ownership is adopted.
     *
     * @note A null factory result draws nothing. Use add() for externally owned component Scenes.
     *
     * @see paintKey()
     */
    void paint(uint64_t key, const std::function<tvg::Paint*()>& make);

    /**
     * @brief Draws a rounded rectangle with optional border.
     *
     * @code
     * context.rect(bounds, theme.surface, theme.radius, theme.border);
     * context.line(bounds.x, bounds.y + 30, bounds.x + bounds.w, bounds.y + 30, theme.border);
     * context.text({bounds.x + 8, bounds.y, bounds.w - 16, 30}, "Notes", theme.text, 14, true);
     * context.text({bounds.x + 8, bounds.y, bounds.w - 16, 30}, "3 items", theme.muted, 0, false, ui::Align::End);
     * @endcode
     *
     * @note Radius and stroke use logical pixels; a zero-alpha border disables the stroke.
     */
    void rect(Rect bounds, Color fill, float radius = 0, Color border = {0, 0, 0, 0}, float stroke = 1);

    /**
     * @brief Draws a round-capped line with a logical-pixel stroke width.
     */
    void line(float x1, float y1, float x2, float y2, Color color, float width = 1);

    /**
     * @brief Draws a single-style label clipped to logical bounds.
     *
     * @param[in] size Nominal em in logical pixels; zero uses Theme::fontSize.
     * @param[in] horizontalOffset Horizontal scroll displacement in logical pixels.
     *
     * @note fontFor selects the face; glyph bounds and line height depend on that face.
     */
    void text(Rect bounds, std::string_view value, Color color, float size = 0, bool bold = false, Align align = Align::Start, float horizontalOffset = 0);

    /**
     * @brief Measures label ink width in logical pixels using the selected font.
     *
     * @code
     * // Size a badge to its label:
     * float width = context.textWidth(count, 11) + 12;
     * ui::Rect badge{bounds.x, bounds.y, width, 18};
     * context.rect(badge, theme.accent, 9);
     * context.text(badge, count, theme.accentText, 11, false, ui::Align::Center);
     * @endcode
     *
     * @param[in] fontContext Full label text used for font selection when measuring a substring.
     *
     * @return Measured ink width; empty input returns zero.
     */
    float textWidth(std::string_view value, float size = 0, bool bold = false, std::string_view fontContext = {}) const;

    /**
     * @brief Shortens a label to fit while preserving its suffix.
     *
     * @code
     * // Shortens the name and keeps the extension, such as "a very long….md":
     * auto label = context.elideText("a very long note name", bounds.w, 0, false, ".md");
     * context.text(bounds, label, theme.text);
     * @endcode
     *
     * @return UTF-8 label with ellipsis when shortened, or empty if no character plus ellipsis fits.
     */
    std::string elideText(std::string_view value, float width, float size = 0, bool bold = false, std::string_view suffix = {}) const;

    /**
     * @brief Draws a named icon with a frame-local instance identity.
     *
     * @code
     * context.icon({8, 8, 24, 24}, "folder", theme.muted);
     * @endcode
     */
    void icon(Rect bounds, std::string_view name, Color color);

    /**
     * @brief Draws a retained named icon instance.
     *
     * @code
     * // A stable instance name carries the hover and active animation across frames:
     * auto state = context.control("pin", bounds, [&](const ui::Event& event) {
     *     if (event.type == ui::EventType::Activate) pinned = !pinned;
     * });
     * context.icon("pin/icon", bounds, "star", theme.text, {.hovered = state.hovered, .pressed = state.pressed, .active = pinned});
     * @endcode
     *
     * @param[in] size Asset viewport size; built-ins use a 16-pixel outline in a 24-pixel viewport.
     *
     * @note Keep instance stable across frames for continuous animation. An empty or unknown name draws nothing.
     */
    void icon(std::string_view instance, Rect bounds, std::string_view name, Color color, IconState state = {}, float size = 24);

    /**
     * @brief Replaces user icon assets transactionally.
     *
     * @code
     * std::string error;
     * if (!context.icons({{"pin", readFile("pin.json"), "lot"}}, error)) report(error);
     * context.icon({8, 8, 24, 24}, "pin", theme.text);
     * @endcode
     *
     * @param[out] error Diagnostic if any asset is invalid.
     *
     * @return Whether all candidates were accepted; built-ins remain available.
     */
    bool icons(std::vector<IconAsset> assets, std::string& error);

    /**
     * @brief Draws the theme focus indicator around logical bounds.
     *
     * @code
     * auto state = context.control("swatch", bounds, handler);
     * if (state.focused) context.focusRing(bounds);
     * @endcode
     */
    void focusRing(Rect bounds);

private:
    template<typename T>
    static bool scrollOffset(T& offset, T limit, float delta)
    {
        if (!std::isfinite(delta) || delta == 0) return false;
        double amount = delta;
        if constexpr (std::is_integral_v<T>) amount = std::copysign(std::max(1.0, std::floor(std::abs(amount))), amount);
        auto next = static_cast<T>(std::clamp(double(offset) + amount, 0.0, double(limit)));
        bool changed = next != offset;
        offset = next;
        return changed;
    }

    TN_UI_DECLARE_IMPL(unique_ptr);
};

/** @} */
}  // namespace ui

/**
 * @defgroup ui_controls Controls
 * @{
 */
namespace ui
{

/**
 * @brief Component color treatment for standard, primary, and quiet actions.
 *
 * @note Default and Quiet turn to Theme::selection while hovered, pressed or active.
 */
enum class ButtonStyle
{
    Default,  ///< Filled with Theme::raised: a regular action that stands out from its panel.
    Primary,  ///< Filled with Theme::accent, and Theme::success while hovered: the main action of a view.
    Quiet     ///< Filled with Theme::surface, so it blends into a surface panel until hovered: toolbar and close buttons.
};

/**
 * @brief Optional button presentation; name the members that differ.
 *
 * @code
 * ui::button(context, "close", bounds, "", close, {.style = ui::ButtonStyle::Quiet, .icon = "close"});
 * @endcode
 */
struct ButtonOptions
{
    ButtonStyle style = ButtonStyle::Default;
    std::string_view icon;  ///< Built-in or user icon name; empty draws a label only.
    bool enabled = true;
    bool active = false;  ///< Draws the selected treatment, such as an open menu or a checked toggle.
    Appearance appearance = {};
};

/**
 * @brief Registers and draws a button using a stable control identity.
 *
 * @code
 * ui::button(context, "save", {8, 8, 80, 28}, "Save", [&] { save(); });
 *
 * auto state = ui::button(context, "pin", {96, 8, 28, 28}, "", togglePin, {.style = ui::ButtonStyle::Quiet, .icon = "star", .active = pinned});
 * if (state.hovered) showTooltip("Pin");
 * @endcode
 *
 * @return Interaction state for this frame.
 *
 * @note Activation runs synchronously during input dispatch; keep captured models alive
 * until the next Context::begin(). Disabled buttons do not invoke activate.
 */
State button(Context& context, std::string_view id, Rect bounds, std::string_view label, std::function<void()> activate, const ButtonOptions& options = {});

/**
 * @brief Registers and draws a checkbox; change receives the proposed value.
 *
 * @code
 * bool wrap = true;  // Kept by the application across frames.
 *
 * ui::checkbox(context, "wrap", {8, 8, 160, 24}, "Wrap lines", wrap, [&](bool value) { wrap = value; });
 * @endcode
 *
 * @note The application retains the value and applies it in its own model.
 */
void checkbox(Context& context, std::string_view id, Rect bounds, std::string_view label, bool value, std::function<void(bool)> change, bool enabled = true);

/**
 * @brief Single-line control text with a UTF-8 byte cursor.
 *
 * @code
 * ui::TextInput input;  // Kept by the application; textField() edits it from Text and Key events.
 *
 * input.insert("note");            // value "note", cursor 4
 * input.edit(ui::Key::Home);       // cursor 0
 * input.insert("my ");             // value "my note", cursor 3
 * input.edit(ui::Key::Backspace);  // value "mynote", cursor 2
 *
 * // A custom control forwards its own events the same way:
 * context.control("rename", bounds, [&](const ui::Event& event) {
 *     if (event.type == ui::EventType::Text) input.insert(event.text);
 *     else if (event.type == ui::EventType::Key) input.edit(event.key);
 * });
 * context.text(bounds, input.value, theme.text);
 * @endcode
 *
 * @note This value has no Document or undo history. Selection, grapheme navigation,
 * and IME preedit are not implemented.
 */
struct TextInput
{
    std::string value;
    size_t cursor = 0;

    /**
     * @brief Inserts committed text at the byte cursor and advances the cursor.
     */
    void insert(std::string_view text);

    /**
     * @brief Applies basic cursor movement or deletion at UTF-8 codepoint boundaries.
     */
    void edit(Key key);
};

/**
 * @brief Registers and draws a single-line input control.
 *
 * @code
 * ui::TextInput query;  // Kept by the application across frames.
 *
 * auto submit = [&](ui::Key key) {
 *     if (key != ui::Key::Enter) return false;
 *     search(query.value);
 *     return true;
 * };
 * ui::textField(context, "search", {8, 8, 240, 30}, query, "Search notes", true, submit, "search");
 * @endcode
 *
 * @param[in] keyHandler Optional filter; a true result consumes the key before default editing.
 * @param[in] icon Optional leading icon name.
 *
 * @note Input state is borrowed through the next Context::begin(). Clipboard and IME
 * composition are not supplied here.
 */
void textField(Context& context, std::string_view id, Rect bounds, TextInput& input, std::string_view placeholder, bool enabled = true, std::function<bool(Key)> keyHandler = {}, std::string_view icon = {}, const Appearance& appearance = {});

/** @} */
}  // namespace ui

/**
 * @defgroup ui_navigation Navigation
 * @{
 */
namespace ui
{

/**
 * @brief Main axis for layout allocation and splitter movement.
 */
enum class Axis
{
    Horizontal,  ///< Sizes are widths; a divider moves left and right.
    Vertical     ///< Sizes are heights; a divider moves up and down.
};

/** @brief Maps a content offset to scrollbar thumb bounds; returns empty bounds when content fits. */
Rect scrollbarThumb(Rect track, float viewport, float content, float offset, Axis axis = Axis::Vertical, float minimum = 24);

/** @brief Draws a passive vertical scroll indicator. */
void scrollbar(Context& context, Rect track, float viewport, float content, float offset, float minimum = 24);

/** @brief Draws a scrollbar with track clicks, pointer capture, and dragging. */
void scrollbar(Context& context, std::string_view id, Rect track, float content, float& offset, std::optional<float>& grab, Axis axis = Axis::Vertical, bool enabled = true);

/**
 * @brief Edge whose movement changes the requested pane size.
 */
enum class ResizeEdge
{
    Leading,  ///< The left or top edge: dragging it left or up grows the pane.
    Trailing  ///< The right or bottom edge: dragging it right or down grows the pane.
};

/**
 * @brief List identity, labels, icon name, indentation depth, and interaction availability.
 */
struct ListItem
{
    std::string id, title, detail;
    std::string icon = "file";
    int depth = 0;
    bool enabled = true;
};

/**
 * @brief Persistent vertical scroll displacement of a list, in logical pixels.
 */
struct ListState
{
    float scroll = 0;
};

/**
 * @brief Draws the visible list rows and registers selection/context-menu callbacks by item ID.
 *
 * Rows outside bounds are skipped; the wheel and keyboard selection scroll through state.
 *
 * @code
 * std::vector<ui::ListItem> items{
 *     {.id = "inbox", .title = "Inbox", .detail = "12", .icon = "folder"},
 *     {.id = "draft", .title = "Draft", .depth = 1},
 * };
 * ui::ListState state;  // Kept by the application across frames.
 * std::string selected = "inbox";
 *
 * ui::list(context, "notes", {0, 40, 240, 400}, items, selected, state, [&](std::string id) { selected = std::move(id); });
 * @endcode
 *
 * @param[in] rowHeight Row height in logical pixels; zero inherits Theme::rowHeight.
 *
 * @note Keep borrowed items and state alive through input dispatch.
 */
void list(Context& context, std::string_view id, Rect bounds, const std::vector<ListItem>& items, std::string_view selected, ListState& state, std::function<void(std::string)> select, float rowHeight = 0, std::function<void(std::string, Rect)> contextMenu = {});

/**
 * @brief Tab surface geometry for segmented, connected, and underline treatments.
 */
enum class TabStyle
{
    Segmented,  ///< A rounded pill: Theme::selection when selected, Theme::raised when hovered.
    Connected,  ///< A bordered Theme::background tab whose curved feet merge into the content below it.
    Underline   ///< A Theme::accent line under the selected tab; hovering shows a Theme::raised inset.
};

/**
 * @brief Tab surface dimensions in logical pixels.
 */
struct TabOptions
{
    TabStyle style = TabStyle::Segmented;
    float radius = -1;   // Negative values inherit the theme's corner radius.
    float shoulder = 6;  // Outward curve at the bottom of a connected tab, in logical pixels.
};

/**
 * @brief Draws a tab surface without registering input or drawing a label.
 *
 * @code
 * for (size_t i = 0; i < tabs.size(); ++i) {
 *     ui::Rect bounds{8 + i * 120.0f, 4, 116, 28};
 *     auto state = context.control("tab/" + tabs[i], bounds, [&, i](const ui::Event& event) {
 *         if (event.type == ui::EventType::Activate) current = i;
 *     });
 *     ui::tabSurface(context, bounds, current == i, state.hovered, {.style = ui::TabStyle::Connected});
 *     context.text(bounds.inset(8), tabs[i], theme.text);
 * }
 * @endcode
 *
 * @note The caller owns selection and hit targets. Connected tabs extend by shoulder; reserve side room.
 */
void tabSurface(Context& context, Rect bounds, bool selected, bool hovered, const TabOptions& options = {});

/**
 * @brief Registers a resize divider and calls change with a constrained proposed size.
 *
 * @code
 * float sidebar = 240;  // Kept by the application across frames.
 *
 * ui::Rect handle{sidebar - 3, 0, 6, height};
 * ui::splitter(context, "sidebar/resize", handle, ui::Axis::Horizontal, sidebar, 150, 400, [&](float width) { sidebar = width; });
 * @endcode
 *
 * @note Values, bounds, and limits use logical pixels; the application stores the resulting size.
 */
void splitter(Context& context, std::string_view id, Rect bounds, Axis axis, float value, float minimum, float maximum, std::function<void(float)> change, ResizeEdge edge = ResizeEdge::Trailing);

/** @} */
}  // namespace ui

/**
 * @defgroup ui_commands Commands
 * @{
 */
namespace ui
{

/**
 * @brief Application action shared by menus, buttons, and command lists.
 *
 * @code
 * commands.add({
 *     .id = "file.save",
 *     .title = "Save",
 *     .category = "File",
 *     .shortcut = ui::platform::shortcutLabel("Mod+S"),
 *     .run = [&] { save(); },
 *     .available = [&] { return modified; },  // Evaluated by enabled().
 *     .unavailableReason = "No unsaved changes",
 * });
 * @endcode
 *
 * @note An empty run callback disables the command. An optional available callback
 * is evaluated when enabled() is queried; unavailableReason supplies presentation text.
 */
struct Command
{
    std::string id = {}, title = {}, category = {}, shortcut = {};
    std::function<void()> run = {};
    std::function<bool()> available = {};
    std::string unavailableReason = {};
    std::string owner = {};  ///< Optional registrant, such as a script name, used by CommandRegistry::release().

    /**
     * @brief Tests for a run callback and evaluates the optional availability predicate.
     */
    bool enabled() const { return bool(run) && (!available || available()); }
};

/**
 * @brief Stores application actions under unique, nonempty command IDs.
 *
 * @code
 * ui::CommandRegistry commands;
 * commands.add({.id = "file.save", .title = "Save", .category = "File", .shortcut = "Mod+S", .run = [&] { save(); }});
 * commands.execute("file.save");
 * @endcode
 *
 * @note Callbacks execute synchronously on the caller thread. The registry has no file
 * or document ownership; the application implements those actions.
 */
class CommandRegistry
{
public:
    CommandRegistry();
    ~CommandRegistry();
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    /**
     * @brief Registers a command with a unique nonempty ID.
     *
     * @return False for an empty or duplicate ID; existing commands remain unchanged.
     */
    bool add(Command command);

    /**
     * @brief Removes a command by ID.
     *
     * @return Whether an entry was removed.
     */
    bool remove(std::string_view id);

    /**
     * @brief Removes every command registered with the given nonempty owner.
     *
     * @code
     * commands.add({.id = "plugin.run", .title = "Run", .run = run, .owner = "plugin.lua"});
     * commands.release("plugin.lua");  // Removes every command registered by that owner.
     * @endcode
     *
     * @return The number of removed commands.
     */
    size_t release(std::string_view owner);

    /**
     * @brief Executes an available command synchronously.
     *
     * @return False when the ID is missing or disabled.
     *
     * @note Exceptions from application callbacks propagate to the caller.
     */
    bool execute(std::string_view id) const;

    /**
     * @brief Finds a command by ID.
     *
     * @return Borrowed command, or null; invalidated by add(), remove() and release().
     */
    const Command* find(std::string_view id) const;

    /**
     * @brief Returns entry indices matching a case-insensitive substring of title, category, or ID.
     *
     * @code
     * for (auto index : commands.matching("sav"))
     *     show(commands.commands()[index].title);
     * @endcode
     *
     * @note Indices refer to commands() and may change after add()/remove().
     */
    std::vector<size_t> matching(std::string_view query) const;

    /**
     * @brief Returns registered commands in insertion order.
     *
     * @return Borrowed vector; mutations may invalidate element references.
     */
    const std::vector<Command>& commands() const;

    TN_UI_DECLARE_IMPL(unique_ptr);
};

/**
 * @brief Persistent search input and selected index within the current command matches.
 */
struct CommandListState
{
    TextInput query;
    size_t selected = 0;
};

/**
 * @brief Borrowed display labels for a command list or palette.
 *
 * @code
 * ui::CommandListOptions options{.title = "Open Note", .placeholder = "Search notes...", .emptyMessage = "No matching notes"};
 * ui::commandList(context, "notes", bounds, notes, state, {}, options);
 * @endcode
 *
 * @note Referenced character storage must remain valid while the options are used.
 */
struct CommandListOptions
{
    std::string_view title, placeholder = "Search commands...";
    std::string_view emptyMessage = "No matching commands";
};

/**
 * @brief Draws searchable commands using persistent query and selected-match state.
 *
 * @code
 * ui::CommandListState state;  // Kept by the application across frames.
 *
 * ui::commandList(context, "commands", {16, 16, 400, 300}, commands, state);
 * @endcode
 *
 * @note Borrowed registry and state must survive input dispatch; executed runs after successful execution.
 */
void commandList(Context& context, std::string_view id, Rect bounds, const CommandRegistry& registry, CommandListState& state, std::function<void()> executed = {}, CommandListOptions options = {});

/**
 * @brief Builds a modal command list with backdrop and close handling.
 *
 * @code
 * ui::CommandListState state;  // Kept by the application across frames.
 * bool open = true;
 *
 * // Draw last, so the modal layer covers the rest of the frame:
 * if (open) ui::commandPalette(context, viewport, commands, state, [&] { open = false; });
 * @endcode
 *
 * @note The host controls visibility and must supply a valid close callback.
 */
void commandPalette(Context& context, Rect viewport, const CommandRegistry& registry, CommandListState& state, std::function<void()> close, CommandListOptions options = {});

/** @} */
}  // namespace ui

/**
 * @defgroup ui_composition Composition
 * @{
 */
namespace ui
{

/**
 * @brief Main-axis size request in logical pixels.
 *
 * @code
 * auto toolbar = ui::view("toolbar", {}, ui::fixed(40));             // Exactly 40 high.
 * auto sidebar = ui::view("sidebar", ui::preferred(240, 180, 340));  // 240 wide, kept within [180, 340].
 * auto content = ui::view("content", ui::flex(2));                   // Twice the surplus of a flex(1) sibling.
 * @endcode
 *
 * @note A negative basis uses flexible allocation. Weight distributes surplus space;
 * minimum and maximum constrain the requested size.
 */
struct Length
{
    float basis = -1;
    float weight = 1;
    float minimum = 0;
    float maximum = std::numeric_limits<float>::infinity();
};

/**
 * @brief Creates a fixed logical-pixel size request.
 */
Length fixed(float pixels);

/**
 * @brief Creates a preferred size constrained by minimum and maximum logical pixels.
 */
Length preferred(float pixels, float minimum, float maximum);

/**
 * @brief Creates a flexible size request sharing surplus space by weight.
 */
Length flex(float weight = 1, float minimum = 0, float maximum = std::numeric_limits<float>::infinity());

/**
 * @brief Component type of an Element: three containers followed by seven leaves.
 */
enum class ElementKind
{
    Row,     ///< Lays children out left to right.
    Column,  ///< Lays children out top to bottom.
    Stack,   ///< Overlaps children at their x/y offsets.
    View,    ///< Application view selected by Element::source.
    Label,   ///< Draws Element::text.
    Button,  ///< Runs Element::command; titled by its text, its icon, or the command title.
    Input,   ///< Single-line text field with Element::text as placeholder; its text is kept by ID.
    Menu,    ///< Button asking the host to open Element::items through Composition::openMenu.
    Image,   ///< Draws the image asset named by Element::source with fit and opacity.
    Shape    ///< Draws Element::shape, filled with Theme::accent unless the appearance overrides it.
};

/**
 * @brief Menu action or nested submenu with application-owned command IDs.
 *
 * @code
 * ui::Element file{
 *     .kind = ui::ElementKind::Menu,
 *     .id = "menu.file",
 *     .text = "File",
 *     .items = {
 *         {.id = "file.new", .title = "New Note", .command = "note.new"},
 *         {.id = "file.separator"},
 *         {.id = "file.recent", .title = "Open Recent", .items = {{.id = "file.recent.0", .title = "todo.md", .command = "note.open.0"}}},
 *     },
 * };
 * @endcode
 */
struct MenuItem
{
    std::string id = {}, title = {}, command = {}, detail = {}, icon = {};
    bool checked = false, enabled = true;
    std::vector<MenuItem> items = {};
};

/**
 * @brief Menu title, entries, and compact presentation requested by a composition.
 */
struct MenuDefinition
{
    std::string id, title;
    std::vector<MenuItem> items;
    bool compact = false;
};

/**
 * @brief Draws a modal menu with pointer selection, keyboard navigation, and submenu actions.
 * @note Definition, selection, and registry must remain alive until the next Context::begin().
 * Back navigates to the parent menu; close dismisses the whole menu. Constrain vertical placement for context menus.
 */
void menu(Context& context, std::string_view id, Rect viewport, Rect anchor, const MenuDefinition& definition, size_t& selection,
          const CommandRegistry& commands, std::function<void(size_t)> activate, std::function<void()> back, std::function<void()> close, bool constrainVertical = false);

/**
 * @brief One node of a declarative component tree: layout request plus content.
 *
 * The same tree is built in C++ with view(), row(), column() and stack(), or read from a
 * script, then arranged by arrange() and rendered by Composition.
 *
 * @code
 * ui::Element pin{.kind = ui::ElementKind::Button, .width = ui::fixed(28), .command = "note.pin", .icon = "star"};
 *
 * auto sidebar = ui::view("sidebar", ui::preferred(240, 180, 340));
 * sidebar.resizable = true;
 * auto tree = ui::row({sidebar, ui::column({ui::row({pin}), ui::view("content")})});
 * @endcode
 *
 * @note An empty id is assigned by Composition::apply(): a View takes its source, any other
 * element its parent's id and child position. Inputs, menus and resizable elements keep
 * state by id, so unless they are views they require an explicit one.
 */
struct Element
{
    ElementKind kind = ElementKind::View;
    std::string id;

    Length width, height;
    float gap = 0, padding = 0;  ///< Space between and around children, in logical pixels.
    float x = 0, y = 0;          ///< Offset from the padded parent origin; used by Stack children.
    Align align = Align::Start;  ///< Cross-axis alignment of children.
    bool visible = true;         ///< A hidden subtree consumes no space.
    bool clipOverflow = false;   ///< Clips children at their minimum sizes instead of shrinking them.
    bool resizable = false;      ///< Adds a divider on resizeEdge.
    ResizeEdge resizeEdge = ResizeEdge::Trailing;

    std::string source;   ///< View type ID, or image asset name.
    std::string text;     ///< Label, button or menu title, or input placeholder.
    std::string command;  ///< Command executed on activation; makes any leaf interactive.
    std::string tooltip, menuTitle;
    std::string icon;  ///< Button or menu icon name.
    ButtonStyle style = ButtonStyle::Default;
    Appearance appearance;
    ShapeData shape;
    ImageFit fit = ImageFit::Contain;
    float opacity = 1;
    bool enabled = true, active = false;
    bool compact = false;  ///< Compact menu presentation.
    std::vector<MenuItem> items;

    std::vector<Element> children;
};

/**
 * @brief Creates an application view leaf whose id and source are the given view type ID.
 */
Element view(std::string source, Length width = {}, Length height = {});

/**
 * @brief Creates a horizontal container owning its children.
 */
Element row(std::vector<Element> children, float gap = 0);

/**
 * @brief Creates a vertical container owning its children.
 */
Element column(std::vector<Element> children, float gap = 0);

/**
 * @brief Creates a container overlapping its children.
 */
Element stack(std::vector<Element> children);

/**
 * @brief Validates a component tree without replacing live state.
 *
 * @code
 * std::string error;
 * if (!ui::validate(tree, error)) report(error);  // Such as "a: IDs must be nonempty and unique".
 * @endcode
 *
 * @param[out] error Diagnostic on failure; cleared before validation.
 *
 * @return Whether IDs, dimensions, and component properties are valid.
 */
bool validate(const Element& root, std::string& error);

/**
 * @brief Arranged element bounds and inherited clip in logical pixels.
 *
 * @code
 * // Drawing an arranged tree by hand instead of through Composition:
 * auto layout = ui::arrange(viewport, tree);
 * for (const auto& placement : layout.placements) {
 *     const auto& element = *placement.element;
 *     context.clip(placement.clip, [&] {
 *         if (element.kind == ui::ElementKind::Label) context.text(placement.bounds, element.text, theme.text);
 *         else if (element.kind == ui::ElementKind::View) drawView(element.source, placement.bounds);
 *     });
 * }
 * @endcode
 *
 * @note Containers are placed before their children, so drawing in order paints parents first.
 */
struct Placement
{
    const Element* element;
    Rect bounds, clip;
};

/**
 * @brief Arranged resize handle of a resizable element with its size constraints.
 *
 * @code
 * // Each divider becomes a splitter; the dragged size is applied to the tree before the next arrange().
 * for (const auto& divider : layout.dividers) {
 *     auto id = divider.element->id;
 *     ui::splitter(context, "resize/" + id, divider.bounds, divider.axis, divider.value, divider.minimum, divider.maximum, [&, id](float size) { sizes[id] = size; }, divider.element->resizeEdge);
 * }
 * @endcode
 *
 * @note value is the current main-axis size of the element; minimum and maximum come from its Length.
 */
struct Divider
{
    const Element* element;
    Rect bounds;
    Axis axis;
    float value, minimum, maximum;
};

/**
 * @brief Arranged placements in drawing order, and resize dividers.
 *
 * @code
 * auto layout = ui::arrange(viewport, tree);
 * if (auto sidebar = layout.find("sidebar")) context.rect(sidebar->bounds, theme.surface);
 * for (const auto& divider : layout.dividers)
 *     context.rect(divider.bounds, theme.border);
 * @endcode
 *
 * @note Borrows the arranged tree; keep it alive and unmodified while this result is used.
 */
struct Layout
{
    std::vector<Placement> placements;
    std::vector<Divider> dividers;

    /**
     * @brief Finds the first placement with the supplied element ID.
     *
     * @return Borrowed placement, or null.
     */
    const Placement* find(std::string_view id) const;
};

/**
 * @brief Allocates the visible elements of a tree inside logical bounds.
 *
 * @code
 * auto tree = ui::column({
 *     ui::view("toolbar", {}, ui::fixed(40)),
 *     ui::row({ui::view("sidebar", ui::preferred(200, 150, 260)), ui::view("content")}),
 * });
 * auto layout = ui::arrange({0, 0, 800, 600}, tree);
 * @endcode
 *
 * @note Hidden subtrees consume no space. Overflowing minima shrink unless clipOverflow is set.
 */
Layout arrange(Rect bounds, const Element& root);

/**
 * @brief Application renderer for a View element.
 *
 * @code
 * std::vector<ui::ViewType> types{
 *     {"content", [&](ui::Context& context, ui::Rect bounds, std::string_view instance) {
 *          editor.render(context, std::string(instance) + "/editor", bounds);
 *      }},
 * };
 * composition.render(context, "workspace", viewport, types, commands);
 * @endcode
 *
 * @note Prefix control IDs with the supplied instance string so repeated views
 * retain distinct input identities.
 */
struct ViewType
{
    std::string id;
    std::function<void(Context&, Rect, std::string_view instance)> render;
};

/**
 * @brief Renders a component tree and retains local input and resize state by element ID.
 *
 * @code
 * ui::Composition composition;
 * std::string error;
 * composition.apply(tree, error);
 *
 * // Each frame, between Context::begin() and end():
 * composition.render(context, "workspace", bounds, viewTypes, commands);
 * @endcode
 *
 * @note Apply definitions between input events, then render before dispatching more input.
 */
class Composition
{
public:
    Composition();
    ~Composition();
    Composition(const Composition&);
    Composition& operator=(const Composition&);

    /**
     * @brief Validates and replaces the definition while retaining compatible local state by ID.
     *
     * @param[out] error Diagnostic on failure.
     *
     * @return Whether accepted; invalid definitions preserve the previous tree and state.
     */
    bool apply(Element root, std::string& error);

    /**
     * @brief Replaces the composition image set after successful validation.
     *
     * @code
     * std::string error;
     * composition.images({{"logo", readFile("logo.png"), "png"}}, error);
     *
     * ui::Element logo{.kind = ui::ElementKind::Image, .source = "logo"};  // The asset name.
     * @endcode
     *
     * @return Whether accepted; previous assets remain on failure.
     */
    bool images(std::vector<ImageAsset> assets, std::string& error);

    /**
     * @brief Builds the current definition into a Context frame using host view types and commands.
     *
     * @param[in] id Prefix of the control IDs registered by this composition.
     */
    void render(Context& context, std::string_view id, Rect bounds, const std::vector<ViewType>& types, const CommandRegistry& commands);

    /**
     * @brief Returns the owned definition, including assigned IDs and user-resized sizes.
     *
     * @return Borrowed tree; invalidated by apply() or destruction.
     */
    const Element& definition() const;

    /**
     * @brief Finds persistent input state by element ID.
     *
     * @code
     * // The text typed into the Input element with the ID "query":
     * if (auto query = composition.input("query")) filter(query->value);
     * @endcode
     *
     * @return Borrowed input, or null; invalidated by apply() or destruction.
     */
    const TextInput* input(std::string_view id) const;

    /**
     * @brief Returns an owned menu description when the ID identifies a menu element or submenu.
     */
    std::optional<MenuDefinition> menu(std::string_view id) const;

    /**
     * @brief Requests that the host open the identified menu at a logical anchor.
     *
     * @code
     * composition.openMenu = [&](std::string_view id, ui::Rect anchor) {
     *     if (auto menu = composition.menu(id)) showPopup(*menu, anchor.x, anchor.y + anchor.h);
     * };
     * @endcode
     */
    std::function<void(std::string_view id, Rect anchor)> openMenu;

    /**
     * @brief Reports a menu anchor during composition rendering.
     */
    std::function<void(std::string_view id, Rect anchor)> menuAnchor;

    /**
     * @brief Reports tooltip text and its logical anchor to the host.
     *
     * @code
     * // Called from render() while the pointer rests on an element with a tooltip:
     * composition.tooltip = [&](std::string_view text, ui::Rect anchor) {
     *     hint = text;
     *     hintAnchor = anchor;
     * };
     * @endcode
     */
    std::function<void(std::string_view text, Rect anchor)> tooltip;

    /**
     * @brief Reports the new main-axis size after the user drags a resizable element's divider.
     *
     * @code
     * // The composition has already applied the size; store it for the next session.
     * composition.resized = [&](std::string_view id, float size) {
     *     if (id == "sidebar") sidebarWidth = size;
     * };
     * @endcode
     */
    std::function<void(std::string_view id, float size)> resized;

    TN_UI_DECLARE_IMPL(unique_ptr);
};

/** @} */
}  // namespace ui

/**
 * @defgroup ui_text Text interaction
 * @{
 */
namespace ui
{

/**
 * @brief Source byte boundaries and caret x positions for one displayed line.
 *
 * @code
 * // "hello" in ten-pixel cells: byte boundaries 0 to 5 and a caret position for each.
 * ui::TextLine line;
 * line.bounds = {0, 0, 50, 20};
 * line.offsets = {0, 1, 2, 3, 4, 5};
 * line.x = {0, 10, 20, 30, 40, 50};
 *
 * auto offset = line.hit(27);     // 3: the closest boundary.
 * auto caret = line.position(3);  // 30
 * @endcode
 *
 * @note offsets and x have equal lengths and nondecreasing values. Bounds and x
 * use unscrolled document coordinates; separate cells may occupy the same vertical range.
 */
struct TextLine
{
    Rect bounds;
    std::vector<size_t> offsets;
    std::vector<float> x;
    float breakWidth = 0;

    /**
     * @brief Finds the closest source byte boundary at a document-space x coordinate.
     */
    size_t hit(float x) const;

    /**
     * @brief Returns the caret x coordinate at or after a source byte offset.
     *
     * @note Positions outside the line are clamped to its first/last stored boundary.
     */
    float position(size_t offset) const;
};

/**
 * @brief Selection and navigation over host-supplied text geometry without rendering.
 *
 * @code
 * tn::Document document;
 * tn::EditSession session(document);
 * ui::TextView view(session);
 * view.layout(lines);  // One TextLine per displayed row, measured by the host.
 *
 * view.pointerDown(x + scrollX, y + scrollY);
 * view.key(ui::Key::Right, {.shift = true});  // Extends the selection.
 * for (auto rect : view.selectionRects())
 *     context.rect(rect, theme.selection);
 * @endcode
 *
 * @note The borrowed session must outlive the view. The host supplies layout,
 * scroll-coordinate conversion, focus, and optional clipboard access.
 */
class TextView
{
public:
    /**
     * @brief Borrows a selection session without taking ownership of its Document.
     */
    explicit TextView(tn::EditSession& session);
    ~TextView();
    TextView(const TextView&) = delete;
    TextView& operator=(const TextView&) = delete;

    /**
     * @brief Replaces all displayed line geometry after validation.
     *
     * @return False for invalid arrays or bounds; previous layout remains.
     */
    bool layout(std::vector<TextLine> value);

    /**
     * @brief Replaces a contiguous range of lines after validating replacement geometry.
     *
     * @param[in] first First existing line to replace.
     * @param[in] count Number of existing lines removed.
     * @param[in] value Replacement lines with matching, sorted offset/x arrays.
     * @param[in] offset Byte displacement applied to source offsets in later lines.
     * @param[in] y Logical vertical displacement applied to later lines.
     *
     * @return Whether the replacement was accepted; invalid input preserves the previous layout.
     */
    bool layout(size_t first, size_t count, std::vector<TextLine> value, ptrdiff_t offset, float y);

    /**
     * @brief Returns current unscrolled document-space lines; mutations invalidate element references.
     */
    const std::vector<TextLine>& layout() const;

    /**
     * @brief Maps unscrolled document-space coordinates to a source byte boundary.
     */
    size_t hit(float x, float y) const;

    /**
     * @brief Returns the primary caret in unscrolled document coordinates.
     */
    Rect caretRect() const;

    /**
     * @brief Returns selection rectangles in document coordinates.
     *
     * @param[in] viewport Optional document-space viewport restricting the queried lines.
     */
    std::vector<Rect> selectionRects(std::optional<Rect> viewport = {}) const;

    /**
     * @brief Starts or extends selection at document-space coordinates.
     *
     * @note Click count selects larger text units; the host controls focus and event routing.
     */
    void pointerDown(float x, float y, bool extend = false, int clicks = 1);

    /**
     * @brief Extends an active pointer selection using document-space coordinates.
     */
    void pointerMove(float x, float y);

    /**
     * @brief Ends an active pointer selection without clearing the selected ranges.
     */
    void pointerUp();

    /**
     * @brief Ends pointer selection and resets the repeated-click selection cycle.
     */
    void cancelPointer();

    /**
     * @brief Clears the horizontal target retained across vertical caret moves.
     */
    void resetPreferredX();

    /**
     * @brief Handles selection, navigation, and copying without editing the Document.
     *
     * @param[in] pageHeight Document-space height used for page navigation.
     *
     * @return Whether the key was handled.
     */
    bool key(Key key, Modifiers modifiers = {}, float pageHeight = 0);

    /**
     * @brief Optional synchronous copy callback; copy the borrowed UTF-8 bytes during the call.
     */
    std::function<void(std::string_view)> writeClipboard;

    TN_UI_DECLARE_IMPL(unique_ptr);
};

/** @} */
}  // namespace ui

/**
 * @defgroup ui_editor Text editor
 * @{
 */
namespace ui
{

/**
 * @brief Editor display dimensions and edit policy.
 *
 * @code
 * auto options = editor.options();
 * options.fontSize = 18;
 * options.lineNumbers = false;
 * editor.options(options);
 * @endcode
 *
 * @note Sizes use logical pixels. Read-only mode permits selection and copying.
 */
struct TextEditorOptions
{
    static constexpr float maxFontSize = 4096;
    float fontSize = 16;
    float padding = 12;  // Horizontal inset; font metrics and line spacing provide vertical space.
    bool lineNumbers = true, readOnly = false;
};

/**
 * @brief Caret geometry requested by an input extension or application.
 */
enum class CaretStyle
{
    Bar,       ///< A thin vertical line before the character at the caret.
    Block,     ///< A translucent rectangle covering the character at the caret.
    Underline  ///< A line under the character at the caret.
};

/**
 * @brief Text-input permission, caret appearance, and application-visible mode status.
 *
 * @code
 * // A modal input extension enters its command mode:
 * editor.inputState({.text = false, .caret = ui::CaretStyle::Block, .status = "NORMAL"});
 *
 * // Back to regular typing:
 * editor.inputState({});
 * @endcode
 */
struct EditorInputState
{
    bool text = true;                    ///< Whether committed text is inserted; false also keeps the IME off.
    CaretStyle caret = CaretStyle::Bar;  ///< Caret shape drawn by the editor.
    std::string status;                  ///< Mode label for the application to display, such as "NORMAL".
};

/**
 * @brief Editable Document view with retained text, selection, caret, and scroll presentation.
 *
 * The editor is a control of the host Context: render() draws it and registers its input,
 * so pointer, wheel, key, and committed text events reach it through the Context.
 *
 * @code
 * tn::Document document;
 * ui::TextEditor editor(document);
 *
 * // Each frame, between Context::begin() and end():
 * editor.render(context, "editor", bounds);
 *
 * // Host input:
 * context.focus("editor");
 * context.textInput("hello");
 * context.key(ui::Key::Enter);
 * @endcode
 *
 * @note The Document must outlive the editor. The host owns Canvas output, scaling,
 * clipboard access, and IME. Destroy the editor before unloadFonts().
 */
class TextEditor
{
public:
    /**
     * @brief Creates an editor borrowing the supplied Document.
     */
    explicit TextEditor(tn::Document& document);
    ~TextEditor();
    TextEditor(const TextEditor&) = delete;
    TextEditor& operator=(const TextEditor&) = delete;

    /**
     * @brief Selects loaded UI font IDs and invalidates layout.
     *
     * @code
     * editor.font(ui::findFont("mono"), ui::findFont("DejaVu Sans"));  // Document face, line-number face.
     * editor.theme(ui::Theme::light());
     * @endcode
     *
     * @param[in] font Document face; zero resolves Theme::font by name.
     * @param[in] numberFont Line-number face; zero uses the document face.
     */
    void font(FontId font = 0, FontId numberFont = 0);

    /**
     * @brief Copies theme tokens and invalidates text style and layout.
     */
    void theme(Theme value);

    /**
     * @brief Applies display options and synchronizes the editing session read-only policy.
     *
     * @note Invalid font size retains the previous size; accepted range is finite (0, maxFontSize].
     * Horizontal padding is clamped to [0, 64] logical pixels.
     */
    void options(TextEditorOptions value);

    /**
     * @brief Returns the current display options.
     */
    const TextEditorOptions& options() const;

    /**
     * @brief Draws the editor inside bounds and registers it as a control of the host Context.
     *
     * @param[in] host Context of the current frame; receives the editor Scene and frame requests.
     * @param[in] id Stable control ID; focus it with Context::focus().
     * @param[in] bounds Editor bounds in the host coordinate space.
     *
     * @pre Synchronize the host Canvas before changing submitted Paints.
     */
    void render(Context& host, std::string_view id, Rect bounds);

    /**
     * @brief Reports whether the editor control held keyboard focus at the last render().
     */
    bool focused() const;

    /**
     * @brief Tests focus, read-only policy, and the text-input permission for host IME activation.
     *
     * @code
     * // After each frame, keep the native IME in step with the editor:
     * enableTextInput(editor.wantsTextInput());
     * auto caret = editor.caretRect();  // Editor-local: offset by the bounds passed to render().
     * placeCandidateWindow(bounds.x + caret.x, bounds.y + caret.y + caret.h);
     * @endcode
     */
    bool wantsTextInput() const;

    /**
     * @brief Reports whether IME preedit text is displayed without committing to the Document.
     */
    bool composing() const;

    /**
     * @brief Displays IME preedit without changing the Document.
     *
     * @code
     * // IME callbacks from the host:
     * editor.composition(preedit, cursor);  // Shown at the caret; the Document is unchanged.
     * editor.composition({}, 0);            // The composition ended.
     * context.textInput(committed);         // Its result arrives as ordinary committed text.
     * @endcode
     *
     * @param[in] text Preedit text replacing the current visual selection.
     * @param[in] cursor UTF-8 byte offset within preedit; clamped to a codepoint boundary.
     */
    void composition(std::string_view text, size_t cursor);

    /**
     * @brief Returns the current mode presentation and text-input permission.
     */
    const EditorInputState& inputState() const;

    /**
     * @brief Applies mode presentation and text-input permission.
     */
    void inputState(EditorInputState value);

    /**
     * @brief Applies a revision-checked edit batch through the editing session.
     *
     * @code
     * tn::EditBatch batch{
     *     .revision = editor.session().document().revision(),
     *     .edits = {{0, 0, "# "}},  // Insert at the start of the document.
     * };
     * std::string error;
     * if (!editor.applyEdits(batch, error)) report(error);
     * @endcode
     *
     * @param[out] error Diagnostic when rejected.
     *
     * @return Whether accepted; read-only mode and active composition reject edits.
     */
    bool applyEdits(const tn::EditBatch& batch, std::string& error);

    /**
     * @brief Replaces the primary selection with validated text through the common editing path.
     *
     * @code
     * std::string error;
     * if (!editor.insert("TODO: ", error)) report(error);  // Rejected while read-only or composing.
     * @endcode
     *
     * @param[out] error Diagnostic when rejected.
     *
     * @return Whether accepted, including unchanged text; resulting selections collapse after replacement.
     */
    bool insert(std::string_view text, std::string& error);

    /**
     * @brief Executes a named editing motion through the session or visual layout.
     *
     * @code
     * editor.navigate("word_right", 2);      // Two words forward.
     * editor.navigate("line_end", 1, true);  // Extend the selection to the end of the line.
     * @endcode
     *
     * @param[in] motion left, right, word_left, word_right, up, down, page_up, page_down,
     * line_start, line_end, start, or end.
     * @param[in] count Repetition count.
     * @param[in] extend Whether to retain selection anchors.
     *
     * @return Whether navigation was accepted; unknown names, count above 100000,
     * and active composition are rejected.
     */
    bool navigate(std::string_view motion, size_t count = 1, bool extend = false);

    /**
     * @brief Undoes one edit group and restores its selections.
     *
     * @return Whether undo occurred; read-only mode and composition reject it.
     */
    bool undo();

    /**
     * @brief Redoes one edit group and restores its selections.
     *
     * @return Whether redo occurred; read-only mode and composition reject it.
     */
    bool redo();

    /**
     * @brief Groups subsequent session edits until endUndoGroup() or an intervening edit group.
     *
     * @code
     * std::string error;
     * editor.beginUndoGroup();
     * editor.insert("(", error);
     * editor.insert(")", error);
     * editor.endUndoGroup();
     *
     * editor.undo();  // Removes both insertions as one step.
     * @endcode
     */
    void beginUndoGroup();

    /**
     * @brief Ends the current session edit group.
     */
    void endUndoGroup();

    /**
     * @brief Returns a borrowed read-only selection/session view.
     *
     * @note Apply edits through the editor methods to preserve policy and invalidation.
     */
    const tn::EditSession& session() const;

    /**
     * @brief Assigns byte-based selection, synchronizes layout, and reveals the caret.
     *
     * @code
     * editor.select(0, 5);  // Anchor at byte 0, caret at byte 5.
     * editor.selectAll();
     * @endcode
     */
    void select(size_t anchor, size_t caret);

    /**
     * @brief Selects the complete Document through this editor.
     */
    void selectAll();

    /**
     * @brief Returns the caret in editor-local logical coordinates, including scrolling.
     */
    Rect caretRect();

    /**
     * @brief Returns local text bounds excluding the gutter and scrollbars.
     */
    Rect textViewport();

    /**
     * @brief Returns the horizontal scroll displacement in logical pixels.
     */
    float scrollX() const;

    /**
     * @brief Returns the vertical scroll displacement in logical pixels.
     */
    float scrollY() const;

    /**
     * @brief Restores requested scroll offsets, clamped to current content.
     *
     * @code
     * // Leaving a document: remember where the reader was.
     * saved = {editor.scrollX(), editor.scrollY()};
     *
     * // Returning: render once so the layout exists, then restore.
     * editor.render(context, "editor", bounds);
     * editor.select(anchor, caret);
     * editor.scroll(saved.x, saved.y);
     * @endcode
     *
     * @note Render and restore the selection first, then apply the host-saved offsets.
     */
    void scroll(float x, float y);

    /**
     * @brief Optional synchronous clipboard reader returning owned UTF-8 text.
     *
     * @code
     * // The host connects its platform clipboard; the editor calls these on paste, copy and cut.
     * editor.readClipboard = []() -> std::string { return platformClipboardText(); };
     * editor.writeClipboard = [](std::string_view text) { setPlatformClipboardText(std::string(text)); };
     * @endcode
     *
     * @note Exceptions propagate through input dispatch.
     */
    std::function<std::string()> readClipboard;

    /**
     * @brief Optional synchronous clipboard writer; copy borrowed bytes during the call.
     *
     * @note Exceptions propagate through input dispatch.
     */
    std::function<void(std::string_view)> writeClipboard;

    /**
     * @brief Notifies the host after editor-driven text changes; use to schedule application work.
     *
     * @code
     * editor.onChange = [&] { modified = true; };
     * @endcode
     */
    std::function<void()> onChange;

    TN_UI_DECLARE_IMPL(unique_ptr);
};

/** @} */
}  // namespace ui

/**
 * @defgroup ui_input Extension input
 * @{
 */
namespace ui
{
/**
 * @brief Input dispatch outcome.
 *
 * @note Pass permits default handling; Handled consumes input; Pending consumes it
 * while an input extension waits for a subsequent key.
 */
enum class InputResult
{
    Pass,     ///< Continue with default input handling.
    Handled,  ///< Consume this input event.
    Pending   ///< Consume this event while waiting for the rest of a command.
};

/**
 * @brief Host-normalized key name and modifiers, including repeat and key-down state.
 *
 * @code
 * ui::KeyStroke stroke{.key = "w", .control = true, .repeated = true};  // Control+W, held down.
 * @endcode
 */
struct KeyStroke
{
    std::string key;
    bool shift = false, control = false, alt = false, meta = false, primary = false;
    bool repeated = false, down = true;
};

/** @} */
}  // namespace ui

/**
 * @defgroup ui_platform Platform keyboard
 * @{
 */
namespace ui::platform
{
/**
 * @brief Physical modifier state supplied by the platform input host.
 */
struct KeyModifiers
{
    bool shift = false, control = false, alt = false, meta = false;
};

/**
 * @brief Maps physical modifiers to platform-specific primary and word-navigation semantics.
 *
 * @code
 * // Control is the primary modifier on most desktops, Command on macOS:
 * auto semantic = ui::platform::modifiers({shift, control, alt, meta});
 * context.pointerDown(x, y, semantic);
 * @endcode
 */
Modifiers modifiers(KeyModifiers modifiers);

/**
 * @brief Applies platform navigation conventions to a key, such as primary+Left meaning Home.
 *
 * @code
 * auto key = ui::Key::Left;
 * auto modifiers = ui::platform::navigation(key, {shift, control, alt, meta});
 * context.key(key, modifiers);
 * @endcode
 *
 * @param[in,out] key Normalized key; replaced when the platform maps the combination to another key.
 *
 * @return Semantic modifiers remaining after the mapping.
 */
Modifiers navigation(Key& key, KeyModifiers modifiers);

/**
 * @brief Formats a shortcut string, such as "Mod+S", for the host platform.
 *
 * @code
 * auto label = ui::platform::shortcutLabel("Mod+S");  // "Ctrl+S", or the Command symbol and "S" on macOS.
 * @endcode
 */
std::string shortcutLabel(std::string value);

/** @} */
}  // namespace ui::platform

/**
 * @defgroup ui_lua Lua scripts
 * @{
 */
namespace ui::lua
{

using Scalar = std::variant<bool, double, std::string>;
using Record = std::map<std::string, Scalar, std::less<>>;
using Value = std::variant<bool, double, std::string, std::vector<Record>>;

/**
 * @brief Named host values passed to a script's build function as a table.
 *
 * @code
 * ui::lua::Values state{
 *     {"notes", true},
 *     {"sidebarWidth", 240.0},
 *     {"title", std::string("Inbox")},
 *     {"recent", std::vector<ui::lua::Record>{{{"title", std::string("todo.md")}, {"pinned", true}}}},
 * };
 * // In Lua: state.notes, state.sidebarWidth, state.title, state.recent[1].title
 * @endcode
 */
using Values = std::map<std::string, Value, std::less<>>;

/**
 * @brief Icon declared by a script; the host reads file and fills asset.data and asset.mime.
 *
 * @code
 * // In Lua: icons = {{name = "pin", file = "pin.json"}}, images = {{name = "logo", file = "logo.png"}}
 * std::vector<ui::IconAsset> icons;
 * for (auto icon : script.icons()) {
 *     icon.asset.data = readFile(icon.file.c_str());  // An empty mime is detected from the data.
 *     icons.push_back(std::move(icon.asset));
 * }
 * context.icons(std::move(icons), error);
 *
 * std::vector<ui::ImageAsset> images;
 * for (const auto& image : script.images())
 *     images.push_back({image.name, readFile(image.file.c_str()), "png"});
 * composition.images(std::move(images), error);
 * @endcode
 */
struct IconFile
{
    IconAsset asset;
    std::string file;
};

/**
 * @brief Image declared by a script; the host reads file into an ImageAsset of the same name.
 */
struct ImageFile
{
    std::string name, file;
};

/**
 * @brief Resource bounds of one script.
 *
 * @code
 * ui::lua::Script script(commands, {.memory = 8 * 1024 * 1024, .instructions = 50000});  // 8 MiB, 50000 instructions per call.
 * @endcode
 */
struct Limits
{
    size_t memory = 64 * 1024 * 1024;  ///< Allocation limit in bytes while script code runs; zero is unlimited.
    size_t instructions = 200000;      ///< Instruction budget of each call into the script; zero is unlimited.
};

/**
 * @brief A sandboxed Lua module that declares a component tree and commands.
 *
 * The module returns a table: build(state) produces the tree from host values, commands
 * lists actions with a run function, and icons/images declare asset files. Elements are
 * created with ui.row, ui.column, ui.stack, ui.view, ui.label, ui.button, ui.input,
 * ui.menu, ui.image and ui.shape; sizes with a number, ui.flex() or ui.preferred().
 * Inside a command, app.commands.execute(id) queues a host command.
 *
 * @code
 * return {
 *   schema = 1,
 *   commands = {
 *     {id = "note.pin", title = "Pin Note", run = function() app.commands.execute("view.sidebar.toggle") end},
 *   },
 *   build = function(state)
 *     return ui.column {
 *       ui.view {source = "titlebar", height = 40},
 *       ui.row {
 *         ui.view {source = "documents", width = ui.preferred(state.sidebarWidth, 180, 340), resizable = true},
 *         ui.view {source = "content"},
 *       },
 *       ui.button {command = "note.pin", icon = "star", visible = state.notes},
 *     }
 *   end,
 * }
 * @endcode
 *
 * @code
 * ui::lua::Script script(commands);
 * ui::Element root;
 * std::string error;
 * if (script.load(source, "workspace.lua", error) && script.build({{"notes", true}, {"sidebarWidth", 240.0}}, root, error)) {
 *     for (auto& command : script.commands()) commands.add(std::move(command));
 *     composition.apply(std::move(root), error);
 * }
 * @endcode
 *
 * @note The sandbox opens only the base, table, string, math and utf8 libraries; file
 * access and chunk loading are unavailable. Children may be given in a children field or
 * as array entries of the element table.
 */
class Script
{
public:
    /**
     * @brief Creates an empty script.
     *
     * @param[in] commands Borrowed registry executing commands queued by app.commands.execute(); must outlive the script.
     */
    explicit Script(const CommandRegistry& commands, Limits limits = {});
    ~Script();
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;

    /**
     * @brief Evaluates a module once and reads its declarations.
     *
     * @param[in] source Lua source text; binary chunks are rejected.
     * @param[in] name Chunk name used in diagnostics and as the owner of the script's commands.
     * @param[out] error Diagnostic on failure.
     *
     * @return Whether the module was accepted. Load a script at most once.
     */
    bool load(std::string_view source, std::string_view name, std::string& error);

    /**
     * @brief Calls build(state) and validates the returned tree.
     *
     * @param[out] root Parsed tree on success; unchanged on failure.
     * @param[out] error Diagnostic on failure.
     *
     * @return Whether a valid tree was produced.
     */
    bool build(const Values& state, Element& root, std::string& error);

    /**
     * @brief Returns the commands declared by the module, bound to its run functions.
     *
     * @note A command of a destroyed script does nothing; release them from the registry by owner.
     */
    std::vector<Command> commands() const;

    /**
     * @brief Returns the icon files declared by the module.
     */
    const std::vector<IconFile>& icons() const;

    /**
     * @brief Returns the image files declared by the module.
     */
    const std::vector<ImageFile>& images() const;

    /**
     * @brief Returns a counter advanced after each command; rebuild the tree when it changes.
     *
     * @code
     * // After dispatching input: a command may have changed what build() returns.
     * if (script.revision() != built) {
     *     built = script.revision();
     *     ui::Element root;
     *     if (script.build(state, root, error)) composition.apply(std::move(root), error);
     * }
     * if (!script.error().empty()) report(script.error());
     * @endcode
     */
    uint64_t revision() const;

    /**
     * @brief Returns the diagnostic of the latest failed command, or an empty string.
     */
    const std::string& error() const;

    TN_UI_DECLARE_IMPL(shared_ptr);
};

/** @} */
}  // namespace ui::lua

#undef TN_UI_DECLARE_IMPL
