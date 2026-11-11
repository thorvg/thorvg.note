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

TEST_SUITE_END();
