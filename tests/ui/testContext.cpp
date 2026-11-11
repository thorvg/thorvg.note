#include "common/frame.h"

namespace
{
using ui::ControlFlag;
using ui::EventType;
using ui::Key;

struct Frame : ui::test::Frame
{
    std::vector<EventType> events;

    ui::Context::Handler record()
    {
        return [this](const ui::Event& event) {
            events.push_back(event.type);
        };
    }
};
}  // namespace

TEST_SUITE_BEGIN("context");

TEST_CASE_FIXTURE(Frame, "pointer_capture_and_activation")
{
    context.begin(theme, {0, 0, 200, 100});
    context.control("plain", {0, 0, 50, 50}, record());
    context.control("capture", {100, 0, 50, 50}, record(), true, ControlFlag::Focusable | ControlFlag::Capture);
    context.control("disabled", {0, 50, 50, 50}, record(), false);
    context.end();

    context.pointerDown(110, 10);
    context.pointerMove(190, 90);
    context.pointerUp(190, 90);
    CHECK(events == std::vector<EventType>{EventType::Press, EventType::Drag, EventType::Release});
    CHECK(context.focused() == "capture");

    // A plain control activates only when released inside, and never sees the release itself.
    events.clear();
    context.pointerDown(10, 10);
    context.pointerUp(10, 10);
    CHECK(events == std::vector<EventType>{EventType::Press, EventType::Activate});

    events.clear();
    context.pointerDown(10, 60);
    context.pointerUp(10, 60);
    CHECK(events.empty());
    CHECK(context.focused().empty());
    CHECK(context.controlAt(10, 60));
}

TEST_CASE_FIXTURE(Frame, "keys_follow_focus_and_tab_traversal")
{
    std::vector<Key> keys;
    context.begin(theme, {0, 0, 200, 100});
    context.control("first", {0, 0, 50, 50}, record());
    context.control("inert", {50, 0, 50, 50}, record(), true, ControlFlag::None);
    context.control("editor", {100, 0, 50, 50}, [&](const ui::Event& event) { keys.push_back(event.key); }, true, ControlFlag::Focusable | ControlFlag::TabInput);
    context.end();

    context.key(Key::Tab);
    CHECK(context.focused() == "first");
    context.key(Key::Tab);
    CHECK(context.focused() == "editor");
    context.key(Key::Tab);
    context.key(Key::Enter, {.shift = true});
    CHECK(context.focused() == "editor");
    CHECK(keys == std::vector<Key>{Key::Tab, Key::Enter});

    context.focus("first");
    context.key(Key::Tab, {.shift = true});
    CHECK(context.focused() == "editor");
}

TEST_CASE_FIXTURE(Frame, "wheel_reaches_the_topmost_scrollable_control")
{
    float scrolled = 0;
    context.begin(theme, {0, 0, 100, 100});
    context.control("list", {0, 0, 100, 100}, [&](const ui::Event& event) { scrolled += event.y; }, true, ControlFlag::Scrollable);
    context.control("row", {0, 0, 100, 20}, record());
    context.end();

    context.pointerPosition(10, 10);
    CHECK(context.wheel(-2));
    CHECK(scrolled == -2);
    CHECK(events.empty());
    context.pointerPosition(150, 10);
    CHECK(!context.wheel(-2));
}

TEST_CASE_FIXTURE(Frame, "modal_blocks_earlier_controls_and_restores_focus")
{
    bool dismissed = false;
    auto frame = [&](bool modal) {
        context.begin(theme, {0, 0, 100, 100});
        context.control("under", {0, 0, 100, 100}, record());
        if (modal) {
            context.modal("dialog", [&] { dismissed = true; });
            context.control("dialog", {40, 40, 20, 20}, record());
        }
        context.end();
    };
    frame(false);
    context.focus("under");
    frame(true);
    CHECK(context.focused() == "dialog");
    context.pointerDown(5, 5);
    CHECK(events.empty());
    context.key(Key::Escape);
    CHECK(dismissed);
    frame(false);
    CHECK(context.focused() == "under");
}

TEST_CASE_FIXTURE(Frame, "paints_are_reused_until_their_inputs_change")
{
    auto draw = [&](float width) {
        context.begin(theme, {0, 0, 100, 100});
        context.rect({0, 0, width, 10}, theme.accent);
        context.clip({0, 20, 50, 50}, [&] { context.line(0, 20, 50, 20, theme.border); });
        context.end();
        return context.scene()->paints().front();
    };
    auto first = draw(10);
    CHECK(context.scene()->paints().size() == 2);
    CHECK(draw(10) == first);
    CHECK(draw(20) != first);

    context.begin(theme, {0, 0, 100, 100});
    context.end();
    CHECK(context.scene()->paints().empty());
}

TEST_CASE_FIXTURE(Frame, "frame_requests_keep_the_earliest_time")
{
    context.begin(theme, {0, 0, 10, 10}, 2.0);
    CHECK(!context.nextFrame());
    context.requestFrameAt(5.0);
    context.requestAnimationFrame();
    context.requestFrameAt(-1.0);
    context.end();
    CHECK(context.nextFrame() == doctest::Approx(2.0 + 1.0 / 60));
}

TEST_CASE_FIXTURE(Frame, "scroll_regions_clamp_and_release_exhausted_directions")
{
    float outer = 0, inner = 0;
    context.begin(theme, {0, 0, 200, 100});
    context.scroll("outer", {0, 0, 200, 100}, outer, 200.0f);
    context.scroll("inner", {0, 0, 100, 100}, inner, 36.0f);
    context.end();
    context.pointerPosition(10, 10);
    CHECK(!context.wheel(1));
    CHECK(context.wheel(-1));
    CHECK(inner == 36);
    CHECK(outer == 0);
    CHECK(context.wheel(-1));
    CHECK(outer == 36);
    CHECK(context.wheel(1));
    CHECK(inner == 0);

    context.begin(theme, {0, 0, 200, 100});
    context.scroll("outer", {0, 0, 200, 100}, outer, 0.0f);
    context.end();
    CHECK(outer == 0);
    CHECK(!context.wheel(-1));
}

TEST_CASE_FIXTURE(Frame, "scroll_regions_support_rows_and_horizontal_strips")
{
    size_t row = 0;
    float x = 0;
    context.begin(theme, {0, 0, 200, 100});
    context.scroll("rows", {0, 0, 100, 100}, row, size_t(5), {.step = 3});
    context.scroll("tabs", {100, 0, 100, 100}, x, 100.0f, {.step = 48, .horizontal = true});
    context.end();
    context.pointerPosition(10, 10);
    CHECK(context.wheel(-0.1f));
    CHECK(row == 1);
    CHECK(context.wheel(-2));
    CHECK(row == 5);
    CHECK(!context.wheel(-1));
    CHECK(context.wheel(0.1f));
    CHECK(row == 4);
    context.pointerPosition(110, 10);
    CHECK(context.wheel(-1));
    CHECK(x == 48);
    CHECK(context.wheel(0, 1));
    CHECK(x == 96);
}

TEST_CASE_FIXTURE(Frame, "two_axis_scroll_regions_handle_wheel_and_keyboard")
{
    float x = 0, y = 0;
    context.begin(theme, {0, 0, 200, 100});
    context.scroll("page", {0, 0, 200, 100}, x, y, 100.0f, 300.0f);
    context.end();
    context.pointerPosition(10, 10);
    CHECK(context.wheel(-1, -1));
    CHECK(x == 36);
    CHECK(y == 36);
    CHECK(context.scrollKey("page", Key::PageDown));
    CHECK(y == 126);
    CHECK(context.scrollKey("page", Key::Home));
    CHECK(x == 0);
    CHECK(y == 0);
    CHECK(context.scrollKey("page", Key::End));
    CHECK(y == 300);
    CHECK(!context.wheel(-1));
    CHECK(!context.scrollKey("page", Key::Enter));
}

TEST_SUITE_END();
