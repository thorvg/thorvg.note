#include <common/doctest/doctest.h>
#include <text/text.h>
#include <random>
#include <utility>

TEST_SUITE_BEGIN("session");

TEST_CASE("utf8_editing")
{
    tn::Document document;
    REQUIRE(document.text("A가é🙂"));
    tn::EditSession session(document);
    session.move(tn::Move::End);
    CHECK_EQ(session.selection().caret, document.text().size());

    REQUIRE(session.backspace());
    CHECK_EQ(document.text(), "A가é");

    REQUIRE(session.undo());
    CHECK_EQ(document.text(), "A가é🙂");
    CHECK(session.selection().empty());
    CHECK_EQ(session.selection().caret, document.text().size());

    session.move(tn::Move::Start);
    session.move(tn::Move::Right);
    REQUIRE(session.deleteForward());
    CHECK_EQ(document.text(), "Aé🙂");

    REQUIRE(session.undo());
    CHECK_EQ(session.selection().caret, size_t(1));
    CHECK(session.selection().empty());
    CHECK_EQ(document.clamp(2), size_t(1));
    CHECK_EQ(document.next(1), size_t(4));
    CHECK_EQ(document.previous(4), size_t(1));
}

// Undo restores backward selection direction; a new edit discards the redo branch.
TEST_CASE("selection_replace")
{
    tn::Document document;
    REQUIRE(document.text("hello world"));
    tn::EditSession session(document);
    session.select(11, 6);
    CHECK_EQ(session.selectedText(), "world");

    REQUIRE(session.insert("editor"));
    CHECK_EQ(document.text(), "hello editor");

    REQUIRE(session.undo());
    CHECK_EQ(session.selection().anchor, size_t(11));
    CHECK_EQ(session.selection().caret, size_t(6));

    REQUIRE(session.redo());
    CHECK_EQ(document.text(), "hello editor");

    REQUIRE(session.undo());
    REQUIRE(session.insert("view"));
    CHECK_EQ(document.text(), "hello view");
    CHECK(!session.redo());
    CHECK(!document.canRedo());
}

TEST_CASE("word_editing")
{
    tn::Document document;
    REQUIRE(document.text("one  two_three!\nnext"));
    tn::EditSession session(document);
    CHECK(document.wordNext(0) == 5);
    CHECK(document.wordPrevious(14) == 5);

    SUBCASE("extend by word")
    {
        session.move(tn::Move::WordRight);
        CHECK(session.selection().caret == 5);

        session.move(tn::Move::WordRight, true);
        CHECK(session.selectedText() == "two_three");

        REQUIRE(session.backspace());
        CHECK(document.text() == "one  !\nnext");
        REQUIRE(session.undo());
        CHECK(session.selectedText() == "two_three");
    }

    SUBCASE("backward word deletion")
    {
        session.move(tn::Move::End);
        REQUIRE(session.backspace(true));
        CHECK(document.text() == "one  two_three!\n");

        REQUIRE(session.undo());
        CHECK(session.selection().empty());
        CHECK(session.selection().caret == document.text().size());
    }

    SUBCASE("forward word deletion")
    {
        REQUIRE(session.deleteForward(true));
        CHECK(document.text() == "two_three!\nnext");
        CHECK(session.selection().caret == 0);

        REQUIRE(session.undo());
        CHECK(document.text() == "one  two_three!\nnext");
    }
}

TEST_CASE("selection")
{
    tn::Document document;
    REQUIRE(document.text("a가b"));
    tn::EditSession session(document);
    session.readOnly(true);
    CHECK(&session.document() == &document);

    session.select(99, 2);
    CHECK_EQ(session.selection().start(), size_t(1));
    CHECK_EQ(session.selection().end(), size_t(5));
    CHECK_EQ(session.selectedText(), "가b");

    session.selectAll();
    CHECK_EQ(session.selection().anchor, size_t(0));
    CHECK_EQ(session.selection().caret, document.text().size());
    CHECK_EQ(session.selectedText(), document.text());
}

TEST_CASE("navigation")
{
    tn::Document document;
    REQUIRE(document.text("one two\nabc"));
    tn::EditSession session(document);
    session.readOnly(true);
    const std::pair<tn::Move, size_t> steps[] = {
        {tn::Move::WordRight, 4},
        {tn::Move::WordLeft, 0},
        {tn::Move::Right, 1},
        {tn::Move::Left, 0},
        {tn::Move::LineEnd, 7},
        {tn::Move::LineStart, 0},
        {tn::Move::End, 11},
        {tn::Move::Start, 0},
    };
    for (auto [move, caret] : steps) {
        session.move(move);
        CHECK_EQ(session.selection().caret, caret);
        CHECK(session.selection().empty());
    }
    session.select(6, 4);
    session.move(tn::Move::Left);
    CHECK_EQ(session.selection().caret, size_t(4));

    session.select(4, 6);
    session.move(tn::Move::Right);
    CHECK_EQ(session.selection().caret, size_t(6));
}

// Text history is shared, but each session restores only its own recorded selections.
TEST_CASE("two_sessions")
{
    tn::Document document;
    REQUIRE(document.text("one two"));
    tn::EditSession first(document), second(document);

    SUBCASE("shared undo")
    {
        first.select(0, 3);
        second.select(4, 7);
        REQUIRE(first.insert("1"));
        CHECK(document.text() == "1 two");
        CHECK(second.selectedText() == "two");

        REQUIRE(second.undo());
        CHECK(document.text() == "one two");
        CHECK(second.selection().anchor == 4);
        CHECK(second.selection().caret == 7);
        CHECK(first.selection().caret == 3);

        // The session that recorded the edit restores its original selections.
        REQUIRE(first.redo());
        CHECK(document.text() == "1 two");
        CHECK(first.selection().caret == 1);

        REQUIRE(first.undo());
        CHECK(first.selection().anchor == 0);
        CHECK(first.selection().caret == 3);
    }

    SUBCASE("interleaved edits")
    {
        first.beginUndoGroup();
        REQUIRE(first.insert("a"));
        second.select(8, 8);
        REQUIRE(second.insert("b"));
        REQUIRE(first.insert("c"));
        first.endUndoGroup();
        CHECK(document.text() == "acone twob");

        REQUIRE(first.undo());
        CHECK(document.text() == "aone twob");
        REQUIRE(second.undo());
        CHECK(document.text() == "aone two");
        REQUIRE(first.undo());
        CHECK(document.text() == "one two");
    }
}

TEST_CASE("following")
{
    tn::Document document;
    tn::EditSession editing(document);
    tn::EditSession viewing(document);
    viewing.readOnly(true);
    // A selection in "abcdefgh", the range another session replaces, its new text and where the selection ends up.
    const struct
    {
        tn::Selection selection;
        size_t start, end;
        const char* text;
        tn::Selection followed;
    } cases[] = {
        {{2, 4}, 2, 2, "xy", {4, 6}},  // text inserted at either edge stays outside
        {{4, 2}, 4, 4, "xy", {4, 2}},
        {{3, 3}, 3, 3, "xy", {5, 5}},   // a caret moves past text inserted at it
        {{2, 4}, 2, 4, "xyz", {2, 5}},  // replaced text stays selected
        {{2, 5}, 4, 6, "", {2, 4}},     // a deletion across an edge shrinks the selection
        {{3, 4}, 2, 6, "xy", {4, 4}},   // a selection inside a replaced range collapses after it
    };
    for (const auto& item : cases) {
        REQUIRE(document.text("abcdefgh"));
        viewing.select(item.selection.anchor, item.selection.caret);
        editing.select(item.start, item.end);
        REQUIRE(editing.insert(item.text));
        CHECK_EQ(viewing.selection().anchor, item.followed.anchor);
        CHECK_EQ(viewing.selection().caret, item.followed.caret);
    }

    // Every selection follows, and the ones that meet merge.
    REQUIRE(document.text("abcdefgh"));
    viewing.select(2, 2);
    viewing.add(4, 4);
    viewing.add(6, 7);
    editing.select(1, 5);
    REQUIRE(editing.insert(""));
    CHECK_EQ(viewing.selections().size(), size_t(2));
    CHECK_EQ(viewing.selections()[0].caret, size_t(1));
    CHECK_EQ(viewing.selection().anchor, size_t(2));

    // A session that has not been read for many edits still follows all of them.
    REQUIRE(document.text("end"));
    viewing.select(3, 3);
    editing.select(0, 0);
    for (int i = 0; i < 2000; ++i)
        editing.insert("a");
    CHECK_EQ(viewing.selection().caret, document.text().size());

    // Replacing the whole text leaves nothing to follow, so the selection is only clamped.
    REQUIRE(document.text("ab"));
    CHECK_EQ(viewing.selection().caret, size_t(2));
}

TEST_CASE("multiple_selections")
{
    tn::Document document;
    REQUIRE(document.text("one two three"));
    tn::EditSession session(document);
    session.readOnly(true);

    // Overlapping selections merge and so does a caret touching another selection; touching selections stay apart.
    const struct
    {
        tn::Selection first, second;
        size_t count;
    } cases[] = {
        {{0, 3}, {2, 5}, 1},
        {{0, 3}, {3, 3}, 1},
        {{0, 3}, {3, 5}, 2},
        {{4, 4}, {4, 4}, 1},
        {{4, 4}, {5, 5}, 2},
    };
    for (const auto& item : cases) {
        session.select(item.first.anchor, item.first.caret);
        session.add(item.second.anchor, item.second.caret);
        CHECK_EQ(session.selections().size(), item.count);
    }

    // The added selection becomes the primary one and stays so once merged.
    session.select(8, 13);
    session.add(0, 3);
    CHECK_EQ(session.selection().caret, size_t(3));
    CHECK_EQ(session.selectedText(), "one\nthree");

    session.add(2, 5);
    CHECK_EQ(session.selections().size(), size_t(2));
    CHECK_EQ(session.selection().anchor, size_t(0));
    CHECK_EQ(session.selection().caret, size_t(5));

    // Every caret moves, and the ones that meet merge.
    session.select(3, 3);
    session.add(7, 7);
    session.move(tn::Move::WordLeft);
    CHECK_EQ(session.selections()[0].caret, size_t(0));
    CHECK_EQ(session.selections()[1].caret, size_t(4));

    session.move(tn::Move::Start);
    CHECK_EQ(session.selections().size(), size_t(1));

    // The primary index refers to the input list, before sorting and merging.
    session.select({{8, 8}, {0, 3}, {2, 2}}, 2);
    CHECK_EQ(session.selections().size(), size_t(2));
    CHECK_EQ(session.selection().anchor, size_t(0));
}

TEST_CASE("multi_cursor_editing")
{
    tn::Document document;
    REQUIRE(document.text("one two three"));
    tn::EditSession session(document);
    session.select(3, 3);
    session.add(7, 7);
    session.add(13, 13);

    // Typing goes to every caret as one undo step and leaves a caret after each new text.
    REQUIRE(session.insert("!"));
    CHECK_EQ(document.text(), "one! two! three!");
    const size_t typed[] = {4, 9, 16};
    for (size_t i = 0; i < 3; ++i)
        CHECK_EQ(session.selections()[i].caret, typed[i]);
    CHECK_EQ(session.selection().caret, size_t(16));

    REQUIRE(session.backspace());
    CHECK_EQ(document.text(), "one two three");

    REQUIRE(session.undo());
    CHECK_EQ(document.text(), "one! two! three!");
    CHECK_EQ(session.selections().size(), size_t(3));
    CHECK_EQ(session.selection().caret, size_t(16));

    REQUIRE(session.undo());
    CHECK_EQ(document.text(), "one two three");
    CHECK_EQ(session.selections()[1].caret, size_t(7));

    REQUIRE(session.redo());
    CHECK_EQ(session.selections()[1].caret, size_t(9));

    // Deletions that overlap become one, and the carets that meet merge.
    REQUIRE(document.text("abc def"));
    session.select(2, 2);
    session.add(3, 3);
    REQUIRE(session.backspace(true));
    CHECK_EQ(document.text(), " def");
    CHECK_EQ(session.selections().size(), size_t(1));
    CHECK_EQ(session.selection().caret, size_t(0));

    // Selected text and carets are replaced in the same step.
    REQUIRE(document.text("abcdef"));
    session.select(1, 3);
    session.add(5, 5);
    REQUIRE(session.insert("XY"));
    CHECK_EQ(document.text(), "aXYdeXYf");
    CHECK_EQ(session.selections()[0].caret, size_t(3));
    CHECK_EQ(session.selections()[1].caret, size_t(7));
}

TEST_CASE("selection_invariants")
{
    tn::Document document;
    REQUIRE(document.text("alpha beta\ngamma 한글 🙂\n\nend"));
    tn::EditSession first(document), second(document);
    tn::EditSession* sessions[] = {&first, &second};
    std::mt19937 random(5);
    auto pick = [&](size_t limit) {
        return std::uniform_int_distribution<size_t>(0, limit)(random);
    };
    const char* pieces[] = {"x", "\n", "한", "", "ab\ncd", "🙂"};
    for (int step = 0; step < 1000; ++step) {
        CAPTURE(step);
        auto& session = *sessions[pick(1)];
        auto size = document.text().size();
        switch (pick(8)) {
            case 1: session.select(pick(size), pick(size)); break;
            case 2: session.move(tn::Move(pick(7)), pick(1) == 1); break;
            case 3: session.insert(pieces[pick(5)]); break;
            case 4: session.backspace(pick(1) == 1); break;
            case 5: session.deleteForward(pick(1) == 1); break;
            case 6: session.undo(); break;
            case 7: session.redo(); break;
            default: session.add(pick(size), pick(size)); break;
        }
        // Selections sit on character boundaries in document order and only touch when both are selected text.
        for (auto other : sessions) {
            const auto& list = other->selections();
            REQUIRE(!list.empty());
            auto primary = other->selection();
            bool found = false;
            for (size_t i = 0; i < list.size(); ++i) {
                CHECK_EQ(document.clamp(list[i].anchor), list[i].anchor);
                CHECK_EQ(document.clamp(list[i].caret), list[i].caret);
                if (i) {
                    const auto& previous = list[i - 1];
                    CHECK(previous.end() <= list[i].start());
                    if (previous.end() == list[i].start()) {
                        CHECK(!previous.empty());
                        CHECK(!list[i].empty());
                    }
                }
                found = found || (list[i].anchor == primary.anchor && list[i].caret == primary.caret);
            }
            CHECK(found);
        }
    }
}

// Session copies share the document but keep independent selections and read-only policies.
TEST_CASE("copy_state")
{
    tn::Document document;
    REQUIRE(document.text("AV"));
    tn::EditSession original(document);
    original.select(1, 1);
    REQUIRE(original.insert("X"));
    original.readOnly(true);

    tn::EditSession copied(original);
    CHECK(&copied.document() == &document);
    CHECK(copied.readOnly());
    CHECK(copied.selection().caret == 2);

    copied.readOnly(false);
    copied.select(0, 0);
    CHECK(original.readOnly());
    CHECK(original.selection().caret == 2);

    // Copying also preserves the selection-restoration records for existing history.
    REQUIRE(copied.undo());
    CHECK(document.text() == "AV");
    CHECK(copied.selection().caret == 1);
    CHECK(original.selection().caret == 1);

    REQUIRE(copied.redo());
    CHECK(document.text() == "AXV");
    CHECK(copied.selection().caret == 2);
}

// Read-only sessions may navigate and follow external edits, but cannot mutate shared history.
TEST_CASE("read_only")
{
    tn::Document document;
    REQUIRE(document.text("one two"));
    tn::EditSession editing(document), viewing(document);
    REQUIRE(editing.insert("!"));
    viewing.readOnly(true);
    viewing.move(tn::Move::Start);
    viewing.move(tn::Move::End, true);
    CHECK(viewing.selectedText() == "!one two");

    auto revision = document.revision();
    CHECK(!viewing.insert("x"));
    CHECK(!viewing.backspace());
    CHECK(!viewing.deleteForward());
    CHECK(!viewing.apply({revision, {{0, 1, "x"}}, {}}));

    REQUIRE(document.canUndo());
    CHECK(!viewing.undo());
    CHECK(viewing.error() == tn::EditError::ReadOnly);
    CHECK(document.revision() == revision);
    CHECK(viewing.selectedText() == "!one two");

    REQUIRE(editing.undo());
    CHECK(viewing.selectedText() == "one two");

    // Keep redo available so failure proves the policy, rather than an empty history.
    REQUIRE(document.canRedo());
    CHECK(!viewing.redo());
    CHECK(viewing.error() == tn::EditError::ReadOnly);

    CHECK(document.text() == "one two");
}

// Rejections preserve content, revision, selections, and earlier undo history.
TEST_CASE("edit_errors")
{
    tn::Document document;
    tn::EditSession session(document);
    REQUIRE(session.insert(u8"AéB"));
    session.move(tn::Move::Start);
    auto revision = document.revision();
    CHECK(session.error() == tn::EditError::None);

    const struct
    {
        const char* name;
        tn::EditBatch batch;
        tn::EditError error;
    } cases[] = {
        {"stale revision", {revision + 1, {}, {}}, tn::EditError::StaleRevision},
        {"split codepoint", {revision, {{2, 3, "x"}}, {}}, tn::EditError::InvalidRange},
        {"overlapping ranges", {revision, {{0, 3, "x"}, {1, 4, "y"}}, {}}, tn::EditError::InvalidRange},
        {"duplicate start", {revision, {{0, 1, "x"}, {0, 0, "y"}}, {}}, tn::EditError::InvalidRange},
        {"split selection", {revision, {}, tn::Selection{2, 2}}, tn::EditError::InvalidRange},
        {"invalid UTF-8", {revision, {{0, 1, "\xff"}}, {}}, tn::EditError::InvalidText},
        {"embedded NUL", {revision, {{0, 1, std::string("\0", 1)}}, {}}, tn::EditError::InvalidText},
    };
    for (const auto& item : cases) {
        CAPTURE(item.name);
        CHECK(!session.apply(item.batch));
        CHECK(session.error() == item.error);
        CHECK(document.revision() == revision);

        CHECK(document.text() == u8"AéB");
        CHECK(session.selection().caret == 0);
    }

    CHECK(!session.insert("\xff"));
    CHECK(session.error() == tn::EditError::InvalidText);

    SUBCASE("no-op batch")
    {
        REQUIRE(session.apply({revision, {}, {}}));
        CHECK(session.error() == tn::EditError::None);
    }

    SUBCASE("no-op insert")
    {
        CHECK(!session.insert(""));
        CHECK(session.error() == tn::EditError::None);
    }

    SUBCASE("undo after rejection")
    {
        REQUIRE(session.undo());
        CHECK(document.text().empty());
        CHECK(session.error() == tn::EditError::None);
    }

    SUBCASE("empty undo and redo")
    {
        REQUIRE(document.text(""));
        CHECK(!session.undo());
        CHECK(session.error() == tn::EditError::None);

        CHECK(!session.insert("\xff"));
        CHECK(!session.redo());
        CHECK(session.error() == tn::EditError::None);
    }
}

TEST_SUITE_END();
