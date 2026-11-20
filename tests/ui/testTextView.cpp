#include <common/doctest/doctest.h>
#include <ui/ui.h>
#include <random>
#include <algorithm>

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

    static ui::TextLine row(size_t index, double origin = 0, bool measured = false)
    {
        ui::TextLine result;
        result.base = index * 4;
        result.length = 3;
        result.top = origin + index * 20;
        result.bounds = {0, float(result.top), measured ? 30.0f : 0.0f, 20};
        if (measured) {
            result.offsets = {0, 1, 2, 3};
            result.x = {0, 10, 20, 30};
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

TEST_CASE_FIXTURE(View, "word_drag_uses_the_glyph_under_the_pointer_at_punctuation")
{
    REQUIRE(document.text("alpha.beta()\n  gamma"));
    REQUIRE(view.layout({line(0, 12, 0), line(13, 7, 20)}));
    view.pointerDown(99, 5, false, 2);  // Right edge of the 'a' in beta.
    checkSelection(6, 10);
    view.pointerMove(49, 5);
    checkSelection(10, 0);
    view.pointerMove(19, 25);
    checkSelection(6, 15);
    view.pointerUp();
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
    std::vector<ui::TextLine> before;
    std::vector<const size_t*> boundaries;
    for (size_t row = 0; row < view.lineCount(); ++row) {
        const auto& value = view.line(row);
        before.push_back(value);
        boundaries.push_back(value.offsets.empty() ? nullptr : value.offsets.data());
    }

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
    REQUIRE(view.lineCount() == before.size() - count + inserted);
    for (size_t row = 0; row < view.lineCount(); ++row) {
        const auto& value = view.line(row);
        CAPTURE(row);
        CHECK(value.start() == row * 4);
        CHECK(value.end() == row * 4 + 3);
        CHECK(value.y() == origin + row * 20);
        CHECK(value.bounds.y == float(value.y()));
        if (row >= first && row < first + inserted) {
            CHECK(value.offsets.data() == newBoundaries[row - first]);
        } else {
            auto previous = row < first ? row : row + count - inserted;
            CHECK(value.offsets == before[previous].offsets);
            CHECK(value.x == before[previous].x);
            CHECK(value.offsets.empty() == (boundaries[previous] == nullptr));
            if (!value.offsets.empty()) CHECK(value.offsets.data() == boundaries[previous]);
        }
        if (!value.offsets.empty()) {
            view.origin(value.y());
            CHECK(view.hit(20, 5) == row * 4 + 2);
            session.select(row * 4 + 1, row * 4 + 1);
            CHECK(view.caretRect().y == 0);
            CHECK(view.caretRect().x == 10);
        }
    }
}

TEST_CASE_FIXTURE(View, "large_layout_replacements_preserve_distant_glyph_storage")
{
    constexpr size_t count = 65536;
    REQUIRE(document.text(std::string((count + 32) * 4, 'x')));
    std::vector<ui::TextLine> lines;
    lines.reserve(count);
    for (size_t index = 0; index < count; ++index)
        lines.push_back(row(index));
    const size_t anchors[] = {0, 1023, 2048, count - 1};
    std::vector<const size_t*> boundaries;
    for (auto index : anchors) {
        lines[index] = row(index, 0, true);
        boundaries.push_back(lines[index].offsets.data());
    }
    REQUIRE(view.layout(std::move(lines)));
    for (size_t inserted = 0; inserted < 9; ++inserted) {
        REQUIRE(view.layout(1, 0, {row(1)}, 4, 20));
        REQUIRE(view.lineCount() == count + inserted + 1);
        CHECK(view.line(0).offsets.data() == boundaries[0]);
        for (size_t index = 1; index < std::size(anchors); ++index) {
            const auto& value = view.line(anchors[index] + inserted + 1);
            CHECK(value.offsets.data() == boundaries[index]);
            CHECK(value.start() == (anchors[index] + inserted + 1) * 4);
        }
    }
    REQUIRE(view.layout(view.lineCount(), 0, {row(view.lineCount())}, 0, 0));
    REQUIRE(view.layout(1, 9, {}, -36, -180));
    REQUIRE(view.lineCount() == count + 1);
    for (size_t index = 0; index < std::size(anchors); ++index) {
        const auto& value = view.line(anchors[index]);
        CHECK(value.offsets.data() == boundaries[index]);
        CHECK(value.start() == anchors[index] * 4);
        CHECK(value.y() == anchors[index] * 20);
    }
    CHECK((view.visibleLines(20480, 20500) == std::pair<size_t, size_t>{1024, 1025}));
    CHECK(view.line(count).start() == count * 4);
    CHECK(view.line(count).offsets.empty());
    REQUIRE(view.layout(count, 1, {}, 0, 0));
    CHECK(view.lineCount() == count);
}

TEST_CASE_FIXTURE(View, "legacy_layout_reference_observes_replacement_and_discard")
{
    const auto& snapshot = view.layout();
    REQUIRE(snapshot.size() == 2);
    auto address = &snapshot;
    REQUIRE(view.layout(0, 1, {line(0, 5, 0), line(6, 5, 20)}, 0, 20));
    REQUIRE(snapshot.size() == 3);
    CHECK(snapshot[2].start() == 12);
    CHECK(snapshot[2].y() == 40);
    CHECK(&view.layout() == address);
    view.discard(2);
    CHECK(snapshot[2].offsets.empty());
    CHECK(snapshot[2].start() == 12);
    CHECK(snapshot[2].end() == 23);
    auto invalid = line(0, 3, 0);
    invalid.x.clear();
    CHECK_FALSE(view.layout(0, 1, {invalid}, 0, 0));
    CHECK(snapshot.size() == 3);
    CHECK(snapshot[2].start() == 12);
    REQUIRE(view.layout({}));
    CHECK(snapshot.empty());
    CHECK(view.lineCount() == 0);
    CHECK((view.visibleLines(0, 20) == std::pair<size_t, size_t>{0, 0}));
}

TEST_CASE_FIXTURE(View, "visible_line_intervals_cover_gaps_zero_height_and_unordered_rows")
{
    auto first = row(0), second = row(1), empty = row(2), last = row(3);
    first.top = 0;
    first.bounds.h = 10;
    second.top = 30;
    empty.top = 70;
    empty.bounds.h = 0;
    last.top = 100;
    last.bounds.h = 10;
    REQUIRE(view.layout({first, second, empty, last}));
    CHECK((view.visibleLines(-10, 0) == std::pair<size_t, size_t>{0, 0}));
    CHECK((view.visibleLines(0, 10) == std::pair<size_t, size_t>{0, 1}));
    CHECK((view.visibleLines(10, 30) == std::pair<size_t, size_t>{1, 1}));
    CHECK((view.visibleLines(30, 50) == std::pair<size_t, size_t>{1, 2}));
    CHECK((view.visibleLines(50, 100) == std::pair<size_t, size_t>{2, 3}));
    CHECK((view.visibleLines(70, 90) == std::pair<size_t, size_t>{3, 3}));
    CHECK((view.visibleLines(100, 110) == std::pair<size_t, size_t>{3, 4}));
    CHECK((view.visibleLines(110, 120) == std::pair<size_t, size_t>{4, 4}));
    auto infinity = std::numeric_limits<double>::infinity();
    CHECK((view.visibleLines(-infinity, infinity) == std::pair<size_t, size_t>{0, 4}));
    CHECK((view.visibleLines(20, 20) == std::pair<size_t, size_t>{0, 0}));
    CHECK((view.visibleLines(30, 20) == std::pair<size_t, size_t>{0, 0}));
    CHECK((view.visibleLines(std::numeric_limits<double>::quiet_NaN(), 20) == std::pair<size_t, size_t>{0, 0}));
    CHECK((view.visibleLines(0, std::numeric_limits<double>::quiet_NaN()) == std::pair<size_t, size_t>{0, 0}));
    // Overlapping or unordered rows need a conservative range for caller-side culling.
    REQUIRE(view.layout({last, first, second}));
    CHECK((view.visibleLines(5, 6) == std::pair<size_t, size_t>{0, 3}));
    CHECK((view.visibleLines(200, 220) == std::pair<size_t, size_t>{0, 3}));
    second.top = 5;
    REQUIRE(view.layout({first, second}));
    CHECK((view.visibleLines(0, 20) == std::pair<size_t, size_t>{0, 2}));
}

TEST_CASE_FIXTURE(View, "metadata_access_does_not_measure_and_reentrant_measurement_reacquires_rows")
{
    constexpr size_t count = 4097;
    constexpr double origin = 1000000000.25;
    REQUIRE(document.text(std::string(count * 4, 'x')));
    std::vector<ui::TextLine> lines;
    for (size_t index = 0; index < count; ++index)
        lines.push_back(row(index, origin));
    REQUIRE(view.layout(std::move(lines)));
    size_t measured = 0;
    view.measure([&](size_t index) {
        ++measured;
        auto value = view.line(index);
        CHECK(value.offsets.empty());
        value.offsets = {0, 1, 2, 3};
        value.x = {0, 10, 20, 30};
        value.bounds.w = 30;
        REQUIRE(view.layout(index, 1, {std::move(value)}, 0, 0));
    });
    CHECK(view.line(1024).offsets.empty());
    CHECK(view.line(1024).y() == origin + 1024 * 20);
    CHECK((view.visibleLines(origin + 1023 * 20, origin + 1026 * 20) == std::pair<size_t, size_t>{1023, 1026}));
    CHECK(measured == 0);
    view.origin(origin + 1023 * 20);
    session.select(1024 * 4 + 1, 1024 * 4 + 1);
    auto caret = view.caretRect();
    CHECK(caret.x == 10);
    CHECK(caret.y == 20);
    CHECK(measured == 1);
    auto ranges = view.rangeRects(1023 * 4, 1025 * 4 + 3, ui::Rect{0, 0, 100, 60});
    REQUIRE(ranges.size() == 3);
    CHECK(ranges[0].y == 0);
    CHECK(ranges[1].y == 20);
    CHECK(ranges[2].y == 40);
    CHECK(measured == 3);
    view.discard(1024);
    CHECK(view.line(1024).offsets.empty());
    CHECK(view.line(1024).start() == 1024 * 4);
    CHECK(view.line(1024).y() == origin + 1024 * 20);
    CHECK(measured == 3);
    CHECK(view.hit(20, 25) == 1024 * 4 + 2);
    CHECK(measured == 4);
}

TEST_CASE_FIXTURE(View, "chunk_boundaries_keep_precise_geometry_and_signed_source_displacements")
{
    constexpr size_t count = 4097;
    constexpr double origin = 1000000000.25;
    std::vector<ui::TextLine> lines;
    for (size_t index = 0; index < count; ++index)
        lines.push_back(row(index, origin, true));
    auto retained = lines[3000].offsets.data();
    REQUIRE(view.layout(std::move(lines)));
    std::vector<ui::TextLine> replacement;
    for (size_t index = 0; index < 3; ++index)
        replacement.push_back(row(1023 + index, origin, true));
    REQUIRE(view.layout(1023, 2, std::move(replacement), 4, 20));
    REQUIRE(view.lineCount() == count + 1);
    CHECK(view.line(3001).offsets.data() == retained);
    CHECK(view.line(3001).y() == origin + 3001 * 20);
    REQUIRE(view.layout(1024, 1025, {row(1024, origin, true)}, -4096, -20480));
    CHECK(view.line(1977).offsets.data() == retained);
    CHECK(view.line(1977).start() == 1977 * 4);
    CHECK(view.line(1977).y() == origin + 1977 * 20);
    auto first = line(0, 3, 0), second = line(4, 4, 20);
    REQUIRE(view.layout({first, second}));
    REQUIRE(view.layout(0, 1, {line(0, 2, 0)}, -1, .25));
    CHECK(view.line(1).base == -1);
    CHECK(view.line(1).start() == 3);
    CHECK(view.line(1).y() == 20.25);
    CHECK(view.line(1).bounds.y == 20.25f);
    CHECK(std::isnan(view.line(1).top));
    auto before = view.line(1).start();
    auto invalid = line(0, 2, 0);
    invalid.offsets = {2, 1, 0};
    CHECK_FALSE(view.layout(0, 1, {invalid}, 100, 100));
    CHECK(view.line(1).start() == before);
    CHECK(view.line(1).y() == 20.25);
}

TEST_CASE_FIXTURE(View, "partial_layout_changes_match_a_flat_reference")
{
    constexpr double origin = 1000000000.25;
    std::vector<ui::TextLine> reference;
    for (size_t index = 0; index < 4097; ++index)
        reference.push_back(row(index, origin, index % 17 == 0));
    REQUIRE(view.layout(reference));
    std::mt19937 random(43);
    for (size_t step = 0; step < 120; ++step) {
        CAPTURE(step);
        auto first = size_t(random()) % (reference.size() + 1);
        auto removed = std::min(reference.size() - first, size_t(random() % (step % 17 == 0 ? 1100 : 6)));
        auto inserted = size_t(random() % (step % 17 == 0 ? 1300 : 6));
        if (step == 40) first = 0, removed = reference.size(), inserted = 1300;
        if (step == 80) first = 0, removed = reference.size(), inserted = 0;
        std::vector<ui::TextLine> replacement;
        for (size_t index = 0; index < inserted; ++index)
            replacement.push_back(row(first + index, origin, index % 13 == 0));
        auto offset = (ptrdiff_t(inserted) - ptrdiff_t(removed)) * 4;
        auto y = (double(inserted) - double(removed)) * 20;
        REQUIRE(view.layout(first, removed, replacement, offset, y));
        for (size_t index = first + removed; index < reference.size(); ++index) {
            reference[index].base += offset;
            reference[index].top += y;
            reference[index].bounds.y = float(reference[index].top);
        }
        auto at = reference.erase(reference.begin() + first, reference.begin() + first + removed);
        reference.insert(at, replacement.begin(), replacement.end());
        REQUIRE(view.lineCount() == reference.size());
        for (size_t index = 0; index < reference.size(); ++index) {
            const auto& expected = reference[index];
            const auto& actual = view.line(index);
            REQUIRE(actual.base == expected.base);
            REQUIRE(actual.start() == expected.start());
            REQUIRE(actual.end() == expected.end());
            REQUIRE(actual.y() == expected.y());
            REQUIRE(actual.bounds.y == expected.bounds.y);
            REQUIRE(actual.offsets == expected.offsets);
            REQUIRE(actual.x == expected.x);
        }
        auto top = origin + double(random() % (reference.size() * 20 + 100)) - 50.25;
        auto bottom = top + double(1 + random() % 100);
        auto begin = std::lower_bound(reference.begin(), reference.end(), top, [](const auto& value, double y) { return value.y() + value.bounds.h <= y; });
        auto end = std::lower_bound(begin, reference.end(), bottom, [](const auto& value, double y) { return value.y() < y; });
        CHECK((view.visibleLines(top, bottom) == std::pair<size_t, size_t>{size_t(begin - reference.begin()), size_t(end - reference.begin())}));
    }
}
