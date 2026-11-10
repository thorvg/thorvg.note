#include <common/doctest/doctest.h>
#include <text/text.h>
#include <random>

TEST_SUITE_BEGIN("document");

TEST_CASE("line_offsets")
{
    tn::Document document;
    CHECK(!document.canUndo());
    CHECK(!document.canRedo());

    REQUIRE(document.text("a\r\nb\rc\n"));
    CHECK(document.text() == "a\nb\nc\n");
    REQUIRE(document.lineCount() == 4);

    // Line ends exclude LF; the final LF creates an empty fourth line.
    const size_t starts[] = {0, 2, 4, 6};
    const size_t ends[] = {1, 3, 5, 6};
    for (size_t line = 0; line < document.lineCount(); ++line) {
        CAPTURE(line);
        CHECK(document.lineStart(line) == starts[line]);
        CHECK(document.lineEnd(line) == ends[line]);
    }

    CHECK(document.lineEnd(size_t(-1)) == document.text().size());
    CHECK(document.lineAt(1) == 0);  // LF belongs to the preceding line.
    CHECK(document.lineAt(2) == 1);
    CHECK(document.lineAt(100) == 3);
}

// Rejected whole-text replacement must preserve the existing undo history.
TEST_CASE("invalid_text")
{
    tn::Document document;
    REQUIRE(document.text("original"));
    tn::EditSession session(document);
    REQUIRE(session.insert("A"));
    auto revision = document.revision();

    const struct
    {
        const char* name;
        std::string text;
    } cases[] = {
        {"overlong UTF-8", "\xc0\x80"},
        {"surrogate", "\xed\xa0\x80"},
        {"above U+10FFFF", "\xf4\x90\x80\x80"},
        {"incomplete sequence", "\xe2\x82"},
        {"embedded NUL", std::string("a\0b", 3)},
    };
    for (const auto& item : cases) {
        CAPTURE(item.name);
        CHECK(!document.text(item.text));

        CHECK(document.text() == "Aoriginal");
        CHECK(document.revision() == revision);
        CHECK(session.selection().caret == 1);
    }

    REQUIRE(session.undo());
    CHECK(document.text() == "original");
}

// All ranges use the original revision; the resulting selection uses the edited text.
TEST_CASE("edit_batches")
{
    tn::Document document;
    REQUIRE(document.text("A가 BC🙂"));
    tn::EditSession session(document);
    session.select(1, 4);

    tn::EditBatch batch;
    batch.revision = document.revision();
    batch.edits = {{0, 1, "첫"}, {5, 7, "끝\r\n"}};
    batch.selection = tn::Selection{3, 3};

    REQUIRE(session.apply(batch));
    CHECK(document.text() == "첫가 끝\n🙂");
    CHECK(session.selection().caret == 3);

    REQUIRE(session.undo());
    CHECK(document.text() == "A가 BC🙂");
    CHECK(session.selection().anchor == 1);
    CHECK(session.selection().caret == 4);

    REQUIRE(session.redo());
    CHECK(document.text() == "첫가 끝\n🙂");
}

TEST_CASE("undo_groups")
{
    tn::Document document;
    REQUIRE(document.text("word"));
    tn::EditSession session(document);
    session.select(0, 4);

    session.beginUndoGroup();
    REQUIRE(session.insert(""));
    REQUIRE(session.insert("한"));
    REQUIRE(session.apply({document.revision(), {{3, 3, "글"}}, {}}));
    CHECK(session.undoGroupOpen());

    SUBCASE("undo restores selection")
    {
        REQUIRE(session.undo());
        CHECK(!session.undoGroupOpen());

        CHECK(document.text() == "word");
        CHECK(session.selection().caret == 4);
        CHECK(session.selectedText() == "word");
        CHECK(!session.undo());

        REQUIRE(session.redo());
        CHECK(document.text() == "한글");
        CHECK(session.selection().caret == 6);
    }

    SUBCASE("end separates edits")
    {
        session.endUndoGroup();
        CHECK(!session.undoGroupOpen());

        REQUIRE(session.insert("!"));

        REQUIRE(session.undo());
        CHECK(document.text() == "한글");
        REQUIRE(session.undo());
        CHECK(document.text() == "word");
    }
}

// Insertions at selection edges must not become part of the selected text.
TEST_CASE("adjacent_edits")
{
    tn::Document document;
    REQUIRE(document.text("abcdef"));
    tn::EditSession session(document);
    session.select(1, 6);

    tn::EditBatch batch;
    batch.revision = document.revision();
    batch.edits = {
        {0, 1, "X"},
        {1, 1, "가"},
        {3, 4, ""},
        {6, 6, "!"},
    };

    REQUIRE(session.apply(batch));
    CHECK(document.text() == "X가bcef!");
    CHECK(session.selectedText() == "bcef");
    CHECK(session.selection().anchor == 4);
    CHECK(session.selection().caret == 8);

    REQUIRE(session.undo());
    CHECK(document.text() == "abcdef");
    CHECK(session.selection().anchor == 1);
    CHECK(session.selection().caret == 6);

    REQUIRE(session.redo());
    CHECK(document.text() == "X가bcef!");
    CHECK(session.selection().anchor == 4);
    CHECK(session.selection().caret == 8);
}

// Mixed edits must keep cached line starts and the reported change range consistent.
TEST_CASE("change_tracking")
{
    tn::Document document;
    REQUIRE(document.text("alpha\nβeta\n\ngamma 한글\n"));
    tn::EditSession session(document);
    const std::vector<std::string> pieces{"x", "\n", "한", "ab\ncd", "", "\n\n", "🙂"};
    std::mt19937 random(11);
    auto pick = [&](size_t limit) {
        return std::uniform_int_distribution<size_t>(0, limit)(random);
    };
    auto offset = [&] {
        return document.clamp(pick(document.text().size()));
    };
    auto piece = [&]() -> const std::string& {
        return pieces[pick(pieces.size() - 1)];
    };

    for (int step = 0; step < 400; ++step) {
        CAPTURE(step);
        auto before = document.text();
        auto revision = document.revision();
        auto action = pick(4);

        if (action == 0 && document.canUndo()) session.undo();
        else if (action == 1 && document.canRedo()) session.redo();
        else if (action == 2) {
            auto first = offset();
            auto second = offset();
            tn::EditBatch batch;
            batch.revision = revision;
            batch.edits = {{first, first, piece()}};
            if (second != first) batch.edits.push_back({second, second, piece()});
            REQUIRE(session.apply(batch));
        } else {
            auto anchor = offset();
            auto caret = offset();
            session.select(anchor, caret);
            session.insert(piece());
        }

        tn::Document rebuilt;
        REQUIRE(rebuilt.text(document.text()));
        REQUIRE(document.lineCount() == rebuilt.lineCount());
        for (size_t line = 0; line < rebuilt.lineCount(); ++line)
            CHECK(document.lineStart(line) == rebuilt.lineStart(line));

        size_t start = 0, end = 0;
        auto status = document.changedSince(revision, start, end);
        if (document.revision() == revision) {
            CHECK(status == tn::ChangeStatus::Unchanged);
            continue;
        }
        CHECK(status == tn::ChangeStatus::Changed);

        // Text outside the reported range must match the original prefix and suffix.
        const auto& after = document.text();
        REQUIRE(start <= end);
        REQUIRE(end <= after.size());
        auto suffixLength = after.size() - end;
        REQUIRE(start + suffixLength <= before.size());
        CHECK(after.substr(0, start) == before.substr(0, start));
        CHECK(after.substr(end) == before.substr(before.size() - suffixLength));
    }
}

TEST_CASE("change_status")
{
    tn::Document document;
    tn::EditSession session(document);
    size_t start = 99, end = 99;
    auto revision = document.revision();
    CHECK(document.changedSince(revision, start, end) == tn::ChangeStatus::Unchanged);
    CHECK(start == 0);
    CHECK(end == 0);
    CHECK(document.changedSince(revision + 1, start, end) == tn::ChangeStatus::Untracked);

    REQUIRE(session.insert("abc"));
    CHECK(document.changedSince(revision, start, end) == tn::ChangeStatus::Changed);
    CHECK(start == 0);
    CHECK(end == 3);

    revision = document.revision();
    session.selectAll();
    REQUIRE(session.deleteForward());
    // A deletion changes the document even when its resulting range is empty.
    CHECK(document.changedSince(revision, start, end) == tn::ChangeStatus::Changed);
    CHECK(start == 0);
    CHECK(end == 0);

    REQUIRE(session.undo());
    REQUIRE(session.redo());
    CHECK(document.changedSince(revision, start, end) == tn::ChangeStatus::Changed);

    revision = document.revision();
    CHECK(!session.insert(""));
    CHECK(document.changedSince(revision, start, end) == tn::ChangeStatus::Unchanged);

    // Whole-text replacement discards the earlier splice records.
    REQUIRE(document.text("reset"));
    CHECK(document.changedSince(revision, start, end) == tn::ChangeStatus::Untracked);
    CHECK(start == 0);
    CHECK(end == 0);
}

// Document copies own their history; undoing a copy must leave the source unchanged.
TEST_CASE("copy_history")
{
    tn::Document document;
    REQUIRE(document.text("A\nV"));
    tn::EditSession session(document);
    REQUIRE(session.insert("X"));
    tn::Document copied(document), assigned;
    assigned = document;
    CHECK(copied.revision() == document.revision());
    CHECK(assigned.revision() > document.revision());
    for (auto snapshot : {&copied, &assigned}) {
        CHECK(snapshot->text() == "XA\nV");
        tn::EditSession copiedSession(*snapshot);
        REQUIRE(copiedSession.undo());
        CHECK(snapshot->text() == "A\nV");
        CHECK(snapshot->lineCount() == 2);

        CHECK(document.text() == "XA\nV");
        REQUIRE(copiedSession.redo());
        CHECK(snapshot->text() == "XA\nV");
    }
}

// Every subscriber observes edits from either session and direct document replacement.
TEST_CASE("subscriptions")
{
    tn::Document document;
    tn::EditSession first(document), second(document);
    int views = 0, saves = 0;
    auto view = document.subscribe([&] { ++views; });
    auto save = document.subscribe([&] { ++saves; });
    CHECK(document.subscribe({}) == 0);

    REQUIRE(first.insert("abc"));
    REQUIRE(second.insert("!"));
    CHECK(views == 2);
    CHECK(saves == 2);

    first.selectAll();
    CHECK(!first.insert(document.text()));
    CHECK(!first.insert("\xff"));
    CHECK(views == 2);

    // Whole-text replacement resets history even when the bytes are unchanged.
    REQUIRE(document.text(document.text()));
    CHECK(views == 3);
    CHECK(!first.undo());
    CHECK(views == 3);

    document.unsubscribe(view);
    document.unsubscribe(view);
    REQUIRE(document.text("new"));
    CHECK(views == 3);
    CHECK(saves == 4);

    document.unsubscribe(save);
    REQUIRE(document.text(""));
    CHECK(saves == 4);
}

// Callbacks must see the final text and restored selections, including after undo/redo.
TEST_CASE("notification_order")
{
    tn::Document document;
    tn::EditSession session(document);
    int calls = 0;
    std::string expected = "abc";
    size_t caret = 3;
    auto subscription = document.subscribe([&] {
        ++calls;
        CHECK(document.text() == expected);
        CHECK(session.selection().caret == caret);
    });
    REQUIRE(session.insert("abc"));
    expected = "AbC";
    caret = 2;
    REQUIRE(session.apply({document.revision(), {{0, 1, "A"}, {2, 3, "C"}}, tn::Selection{2, 2}}));
    CHECK(calls == 2);
    expected = "abc";
    caret = 3;
    REQUIRE(session.undo());
    expected = "AbC";
    caret = 2;
    REQUIRE(session.redo());
    CHECK(calls == 4);

    document.unsubscribe(subscription);
}

// Removal takes effect during dispatch; new subscribers wait until the next change.
TEST_CASE("subscription_changes")
{
    tn::Document document;
    int first = 0, second = 0, added = 0;
    uint64_t self = 0, other = 0, next = 0;
    self = document.subscribe([&] {
        ++first;
        document.unsubscribe(self);
        document.unsubscribe(other);
        next = document.subscribe([&] { ++added; });
    });
    other = document.subscribe([&] { ++second; });
    REQUIRE(document.text("one"));
    CHECK(first == 1);
    CHECK(second == 0);
    CHECK(added == 0);

    REQUIRE(document.text("two"));
    CHECK(first == 1);
    CHECK(second == 0);
    CHECK(added == 1);

    document.unsubscribe(next);
}

// Copies inherit no subscriptions; assignment retains those registered on the destination.
TEST_CASE("copy_subscriptions")
{
    tn::Document source, target;
    REQUIRE(source.text("source"));
    REQUIRE(target.text("target"));
    auto previous = target.revision();
    int sourceChanges = 0, targetChanges = 0;
    auto sourceSubscription = source.subscribe([&] { ++sourceChanges; });
    auto targetSubscription = target.subscribe([&] { ++targetChanges; });
    tn::Document copied(source);
    REQUIRE(copied.text("copy"));
    CHECK(sourceChanges == 0);
    target = source;
    CHECK(target.text() == "source");
    CHECK(target.revision() > previous);
    size_t start = 0, end = 0;
    CHECK(target.changedSince(previous, start, end) == tn::ChangeStatus::Untracked);
    CHECK(targetChanges == 1);
    REQUIRE(target.text("target"));
    CHECK(targetChanges == 2);
    CHECK(sourceChanges == 0);
    target.unsubscribe(targetSubscription);
    REQUIRE(target.text("unsubscribed"));
    CHECK(targetChanges == 2);
    source.unsubscribe(sourceSubscription);
}

TEST_SUITE_END();
