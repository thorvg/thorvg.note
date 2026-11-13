#include <common/doctest/doctest.h>
#include <ui/ui.h>

namespace
{
using ui::Key;

// Two lines of ten-pixel cells, twenty pixels high.
struct View
{
    tn::Document document;
    tn::EditSession session{document};
    ui::TextView view{session};

    View()
    {
        REQUIRE(document.text("hello world\nsecond line"));
        REQUIRE(view.layout({line(0, 11, 0), line(12, 11, 20)}));
    }

    static ui::TextLine line(size_t offset, size_t length, float y)
    {
        ui::TextLine result;
        result.bounds = {0, y, length * 10.0f, 20};
        for (size_t i = 0; i <= length; ++i) {
            result.offsets.push_back(offset + i);
            result.x.push_back(i * 10.0f);
        }
        return result;
    }

    void checkSelection(size_t anchor, size_t caret)
    {
        CHECK(session.selection().anchor == anchor);
        CHECK(session.selection().caret == caret);
    }
};
}  // namespace

TEST_SUITE_BEGIN("textview");

TEST_CASE_FIXTURE(View, "geometry_maps_points_to_offsets")
{
    auto broken = line(0, 11, 0);
    broken.x.pop_back();
    CHECK(!view.layout({broken}));
    REQUIRE(view.layout().size() == 2);

    CHECK(view.hit(34, 5) == 3);
    CHECK(view.hit(500, 25) == 23);
    session.select(14, 16);
    auto caret = view.caretRect();
    CHECK(caret.x == 40);
    CHECK(caret.y == 20);
    auto selection = view.selectionRects();
    REQUIRE(selection.size() == 1);
    CHECK(selection[0].x == 20);
    CHECK(selection[0].w == 20);
}

TEST_CASE_FIXTURE(View, "keys_navigate_select_and_copy")
{
    std::string copied;
    view.writeClipboard = [&](std::string_view text) {
        copied = text;
    };
    CHECK(view.key(Key::Right));
    CHECK(view.key(Key::Down));
    checkSelection(13, 13);
    CHECK(view.key(Key::End, {.shift = true}));
    checkSelection(13, 23);
    CHECK(view.key(Key::C, {.primary = true}));
    CHECK(copied == "econd line");
    CHECK(view.key(Key::Home, {.primary = true}));
    checkSelection(0, 0);
    CHECK(view.key(Key::Right, {.shift = true, .word = true}));
    checkSelection(0, 6);
    CHECK(!view.key(Key::Enter));
}

TEST_CASE_FIXTURE(View, "pointer_drags_and_selects_words")
{
    view.pointerDown(12, 5);
    view.pointerMove(48, 25);
    view.pointerUp();
    checkSelection(1, 17);

    view.pointerDown(75, 5, false, 2);
    view.pointerUp();
    checkSelection(6, 11);
    view.pointerDown(12, 5, true);
    checkSelection(6, 1);
}

TEST_CASE_FIXTURE(View, "range_geometry_is_independent_of_selection_and_clips_to_the_viewport")
{
    auto first = line(0, 11, 0);
    first.breakWidth = 10;
    REQUIRE(view.layout({first, line(12, 11, 20)}));
    session.select(1, 3);
    auto rectangles = view.rangeRects(6, 15);
    REQUIRE(rectangles.size() == 2);
    CHECK(rectangles[0].x == 60);
    CHECK(rectangles[0].w == 60);
    CHECK(rectangles[1].w == 30);
    checkSelection(1, 3);
    rectangles = view.rangeRects(6, 15, ui::Rect{10, 20, 100, 20});
    REQUIRE(rectangles.size() == 1);
    CHECK(rectangles[0].x == 10);
    CHECK(rectangles[0].w == 20);
    rectangles = view.rangeRects(14, 14);
    REQUIRE(rectangles.size() == 1);
    CHECK(rectangles[0].x == 20);
    CHECK(rectangles[0].w == doctest::Approx(1.5f));
    CHECK(view.rangeRects(15, 6).empty());
}

TEST_SUITE_END();

TEST_CASE_FIXTURE(View, "caret_lookup_preserves_wrapped_boundaries_and_empty_rows")
{
    REQUIRE(document.text("abcdefghij"));
    REQUIRE(view.layout({line(0, 3, 0), line(3, 0, 20), line(3, 4, 40), line(7, 0, 60), line(7, 3, 80), line(10, 0, 100)}));
    for (const auto& [offset, y] : std::vector<std::pair<size_t, float>>{{0, 0}, {2, 0}, {3, 40}, {6, 40}, {7, 80}, {9, 80}, {10, 100}}) {
        session.select(offset, offset);
        CHECK(view.caretRect().y == y);
    }
    REQUIRE(view.layout(1, 2, {line(3, 4, 20)}, 0, -20));
    session.select(3, 3);
    CHECK(view.caretRect().y == 20);
    session.select(10, 10);
    CHECK(view.caretRect().y == 80);
}

TEST_CASE_FIXTURE(View, "caret_lookup_preserves_sparse_and_overlapping_layouts")
{
    REQUIRE(document.text("abcdefghij"));
    ui::TextLine empty;
    empty.bounds = {0, 20, 0, 20};
    REQUIRE(view.layout({line(0, 3, 0), empty, line(5, 5, 40)}));
    session.select(4, 4);
    CHECK(view.caretRect().y == 0);
    session.select(5, 5);
    CHECK(view.caretRect().y == 40);
    REQUIRE(view.layout({line(0, 8, 0), line(5, 5, 20)}));
    session.select(6, 6);
    CHECK(view.caretRect().y == 0);
    session.select(8, 8);
    CHECK(view.caretRect().y == 20);
    REQUIRE(view.layout({line(0, 0, 0), line(0, 0, 20), line(0, 10, 40)}));
    session.select(0, 0);
    CHECK(view.caretRect().y == 40);
}

TEST_CASE_FIXTURE(View, "caret_lookup_follows_offsets_after_partial_layout_replacement")
{
    REQUIRE(document.text("abcdefghij"));
    REQUIRE(view.layout({line(0, 3, 0), line(4, 2, 20), line(7, 1, 40)}));
    REQUIRE(view.layout(1, 1, {line(4, 3, 20)}, 1, 0));
    session.select(7, 7);
    CHECK(view.caretRect().y == 20);
    session.select(8, 8);
    CHECK(view.caretRect().y == 40);
    auto broken = line(0, 1, 0);
    broken.x.clear();
    CHECK_FALSE(view.layout({broken}));
    CHECK(view.caretRect().y == 40);
}

TEST_CASE_FIXTURE(View, "relative_offsets_preserve_hit_testing_and_range_geometry_after_shifts")
{
    REQUIRE(document.text("abcdefghijkl"));
    auto first = line(0, 3, 0), second = line(0, 4, 20);
    second.base = 4;
    REQUIRE(view.layout({first, second}));
    session.select(6, 6);
    CHECK(view.caretRect().y == 20);
    CHECK(view.caretRect().x == second.x[2]);
    CHECK(view.hit(second.x[2], 25) == 6);
    CHECK(second.position(0) == second.x.front());
    CHECK(second.position(std::numeric_limits<size_t>::max()) == second.x.back());
    CHECK(second.rangeRect(5, 7).w == second.x[3] - second.x[1]);
    REQUIRE(view.layout(0, 1, {line(0, 2, 0)}, -1, 0));
    CHECK(view.layout()[1].offsets == second.offsets);
    CHECK(view.layout()[1].start() == 3);
    CHECK(view.layout()[1].end() == 7);
    CHECK(view.hit(second.x[2], 25) == 5);
    REQUIRE(view.layout({first, line(4, 4, 20)}));
    REQUIRE(view.layout(0, 1, {line(0, 2, 0)}, -1, 0));
    session.select(5, 5);
    CHECK(view.caretRect().x == second.x[2]);
    CHECK(view.hit(second.x[2], 25) == 5);
    CHECK(view.layout()[1].base == -1);
    CHECK(view.layout()[1].position(std::numeric_limits<size_t>::max()) == second.x.back());
}

TEST_CASE_FIXTURE(View, "lazy_line_measurement_uses_precise_viewport_relative_coordinates")
{
    size_t measured = 0;
    std::vector<ui::TextLine> lines;
    for (size_t row = 0; row < 2; ++row) {
        ui::TextLine value;
        value.base = row * 12;
        value.length = 11;
        value.top = 1000000000.0 + row * 20;
        value.bounds = {0, 0, 110, 20};
        lines.push_back(std::move(value));
    }
    view.measure([&](size_t row) {
        ++measured;
        auto value = line(row * 12, 11, 0);
        value.top = 1000000000.0 + row * 20;
        value.length = 11;
        std::vector<ui::TextLine> replacement;
        replacement.push_back(std::move(value));
        REQUIRE(view.layout(row, 1, std::move(replacement), 0, 0));
    });
    REQUIRE(view.layout(std::move(lines)));
    view.origin(1000000003.25);
    session.select(0, 0);
    CHECK(view.caretRect().y == -3.25f);
    CHECK(measured == 1);
    CHECK(view.hit(34, 22) == 15);
    CHECK(measured == 2);
    view.discard(1);
    CHECK(view.layout()[1].offsets.empty());
    CHECK(view.layout()[1].start() == 12);
    CHECK(view.layout()[1].end() == 23);
    CHECK(view.hit(34, 22) == 15);
    CHECK(measured == 3);
}

TEST_CASE_FIXTURE(View, "row_replacement_preserves_moved_geometry_and_hit_testing")
{
    REQUIRE(document.text(std::string(160, 'x')));
    constexpr double origin = 1000000000.25;
    std::vector<ui::TextLine> lines;
    for (size_t row = 0; row < 10; ++row) {
        auto value = line(0, 3, 0);
        value.base = row * 4;
        value.top = origin + row * 20;
        value.bounds.y = float(value.top);
        value.length = 3;
        if (row % 3 == 2) {
            value.offsets.clear();
            value.x.clear();
        }
        lines.push_back(std::move(value));
    }
    REQUIRE(view.layout(std::move(lines)));
    auto before = view.layout();
    std::vector<const size_t*> boundaries;
    for (const auto& value : view.layout())
        boundaries.push_back(value.offsets.empty() ? nullptr : value.offsets.data());

    size_t first = 3, count = 1, inserted = 3;
    SUBCASE("grow") {}
    SUBCASE("shrink")
    {
        count = 3, inserted = 1;
    }
    SUBCASE("prepend")
    {
        first = 0, count = 0;
    }
    SUBCASE("append")
    {
        first = 10, count = 0, inserted = 2;
    }
    SUBCASE("remove_tail")
    {
        first = 8, count = 2, inserted = 0;
    }
    SUBCASE("replace_all")
    {
        first = 0, count = 10, inserted = 2;
    }
    SUBCASE("remove_all")
    {
        first = 0, count = 10, inserted = 0;
    }
    std::vector<ui::TextLine> replacement;
    std::vector<const size_t*> newBoundaries;
    for (size_t row = 0; row < inserted; ++row) {
        auto value = line(0, 3, 0);
        value.base = (first + row) * 4;
        value.top = origin + (first + row) * 20;
        value.bounds.y = float(value.top);
        newBoundaries.push_back(value.offsets.data());
        replacement.push_back(std::move(value));
    }
    auto offset = (ptrdiff_t(inserted) - ptrdiff_t(count)) * 4;
    auto y = (double(inserted) - double(count)) * 20;
    REQUIRE(view.layout(first, count, std::move(replacement), offset, y));
    const auto& result = view.layout();
    REQUIRE(result.size() == before.size() - count + inserted);
    for (size_t row = 0; row < result.size(); ++row) {
        CAPTURE(row);
        CHECK(result[row].start() == row * 4);
        CHECK(result[row].end() == row * 4 + 3);
        CHECK(result[row].y() == origin + row * 20);
        CHECK(result[row].bounds.y == float(result[row].y()));
        if (row >= first && row < first + inserted) {
            CHECK(result[row].offsets.data() == newBoundaries[row - first]);
        } else {
            auto previous = row < first ? row : row + count - inserted;
            CHECK(result[row].offsets == before[previous].offsets);
            CHECK(result[row].x == before[previous].x);
            CHECK(result[row].offsets.empty() == (boundaries[previous] == nullptr));
            if (!result[row].offsets.empty()) CHECK(result[row].offsets.data() == boundaries[previous]);
        }
        if (!result[row].offsets.empty()) {
            view.origin(result[row].y());
            CHECK(view.hit(20, 5) == row * 4 + 2);
            session.select(row * 4 + 1, row * 4 + 1);
            CHECK(view.caretRect().y == 0);
            CHECK(view.caretRect().x == 10);
        }
    }
}

TEST_CASE_FIXTURE(View, "large_layout_growth_keeps_bounded_spare_capacity")
{
    constexpr size_t count = 65536;
    std::vector<ui::TextLine> lines;
    lines.reserve(count);
    for (size_t row = 0; row < count; ++row) {
        ui::TextLine value;
        value.base = row * 2;
        value.length = 1;
        value.top = double(row) * 20;
        value.bounds = {0, float(value.top), 0, 20};
        lines.push_back(std::move(value));
    }
    REQUIRE(view.layout(std::move(lines)));
    ui::TextLine value;
    value.base = 2;
    value.length = 1;
    value.top = 20;
    value.bounds = {0, 20, 0, 20};
    REQUIRE(view.layout(1, 0, {value}, 2, 20));
    CHECK(view.layout().capacity() <= count + count / 4);
    auto allocated = view.layout().data();
    for (size_t i = 0; i < 8; ++i) {
        REQUIRE(view.layout(1, 0, {value}, 2, 20));
        CHECK(view.layout().data() == allocated);
    }
    CHECK(view.layout().size() == count + 9);
    CHECK(view.layout().back().start() == (count + 8) * 2);
    CHECK(view.layout().back().y() == double(count + 8) * 20);
    CHECK(view.layout().back().offsets.empty());
}
