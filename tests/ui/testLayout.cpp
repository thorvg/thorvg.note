#include <common/doctest/doctest.h>
#include <ui/ui.h>

namespace
{
void checkRect(ui::Rect value, ui::Rect expected)
{
    CHECK(value.x == doctest::Approx(expected.x));
    CHECK(value.y == doctest::Approx(expected.y));
    CHECK(value.w == doctest::Approx(expected.w));
    CHECK(value.h == doctest::Approx(expected.h));
}
}  // namespace

TEST_SUITE_BEGIN("layout");

TEST_CASE("rows_and_columns_share_space_by_basis_and_weight")
{
    auto hidden = ui::view("hidden", ui::fixed(500));
    hidden.visible = false;
    auto tree = ui::column({
        ui::view("toolbar", {}, ui::fixed(40)),
        ui::row({ui::view("sidebar", ui::preferred(200, 150, 260)), hidden, ui::view("content"), ui::view("wide", ui::flex(3))}, 10),
    });
    auto layout = ui::arrange({0, 0, 620, 440}, tree);

    REQUIRE(layout.placements.size() == 6);
    CHECK(!layout.find("hidden"));
    checkRect(layout.find("toolbar")->bounds, {0, 0, 620, 40});
    checkRect(layout.find("sidebar")->bounds, {0, 40, 200, 400});
    checkRect(layout.find("content")->bounds, {210, 40, 100, 400});
    checkRect(layout.find("wide")->bounds, {320, 40, 300, 400});
}

TEST_CASE("overflow_shrinks_or_clips_children")
{
    auto tree = ui::row({ui::view("a", ui::fixed(300)), ui::view("b", ui::fixed(100))});
    checkRect(ui::arrange({0, 0, 200, 10}, tree).find("b")->bounds, {150, 0, 50, 10});

    tree.clipOverflow = true;
    auto clipped = ui::arrange({0, 0, 200, 10}, tree);
    checkRect(clipped.find("b")->bounds, {300, 0, 100, 10});
    checkRect(clipped.find("b")->clip, {300, 0, 0, 10});
}

TEST_CASE("stack_offsets_children_inside_its_padding")
{
    auto badge = ui::view("badge", ui::fixed(20), ui::fixed(10));
    badge.x = 30;
    badge.y = 5;
    auto tree = ui::stack({ui::view("fill"), badge});
    tree.padding = 4;
    auto layout = ui::arrange({10, 10, 100, 50}, tree);
    checkRect(layout.find("fill")->bounds, {14, 14, 92, 42});
    checkRect(layout.find("badge")->bounds, {44, 19, 20, 10});
}

TEST_CASE("resizable_elements_expose_a_divider_within_their_limits")
{
    auto sidebar = ui::view("sidebar", ui::preferred(200, 150, 260));
    sidebar.resizable = true;
    auto panel = ui::view("panel", ui::preferred(100, 80, 400));
    panel.resizable = true;
    panel.resizeEdge = ui::ResizeEdge::Leading;
    auto tree = ui::row({sidebar, ui::view("content", ui::flex(1, 100)), panel});
    auto layout = ui::arrange({0, 0, 600, 300}, tree);

    REQUIRE(layout.dividers.size() == 2);
    const auto& trailing = layout.dividers[0];
    CHECK(trailing.element->id == "sidebar");
    CHECK(trailing.axis == ui::Axis::Horizontal);
    checkRect(trailing.bounds, {197, 0, 6, 300});
    CHECK(trailing.value == doctest::Approx(200));
    CHECK(trailing.minimum == doctest::Approx(150));
    CHECK(trailing.maximum == doctest::Approx(260));
    // The leading divider sits on the panel's left edge and stops where the other minima begin.
    checkRect(layout.dividers[1].bounds, {497, 0, 6, 300});
    CHECK(layout.dividers[1].maximum == doctest::Approx(350));
}

TEST_SUITE_END();
