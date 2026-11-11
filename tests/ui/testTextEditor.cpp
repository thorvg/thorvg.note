#include "common/frame.h"

namespace
{
using ui::Key;

struct Editor : ui::test::Frame
{
    tn::Document document;
    ui::TextEditor editor{document};
    ui::Rect bounds{20, 10, 300, 200};

    Editor()
    {
        editor.font(fonts.plain);
    }

    void draw()
    {
        render([&] { editor.render(context, "editor", bounds); });
    }
};
}  // namespace

TEST_SUITE_BEGIN("editor");

TEST_CASE_FIXTURE(Editor, "input_reaches_the_editor_through_the_context")
{
    int changes = 0;
    editor.onChange = [&] {
        ++changes;
    };
    draw();
    CHECK(!editor.focused());
    context.textInput("ignored");
    CHECK(document.text().empty());

    context.focus("editor");
    draw();
    CHECK(editor.focused());
    CHECK(editor.wantsTextInput());
    context.textInput("hello");
    context.key(Key::Enter);
    context.key(Key::Tab);
    context.textInput("world");
    context.key(Key::Backspace);
    CHECK(document.text() == "hello\n\tworl");
    CHECK(changes == 5);
    CHECK(context.focused() == "editor");

    context.key(Key::A, {.primary = true});
    CHECK(editor.session().selection().start() == 0);
    CHECK(editor.session().selection().end() == document.text().size());

    context.focus({});
    draw();
    CHECK(!editor.focused());
}

TEST_CASE_FIXTURE(Editor, "pointer_selects_and_wheel_scrolls_in_host_coordinates")
{
    std::string text = "alpha beta gamma";
    for (int i = 0; i < 100; ++i)
        text += "\nline";
    REQUIRE(document.text(text));
    editor.select(7, 7);
    draw();
    auto caret = editor.caretRect();
    float x = bounds.x + caret.x + 2, y = bounds.y + caret.y + caret.h / 2;

    context.pointerMove(x, y);
    CHECK(context.cursor() == ui::Cursor::Text);
    context.pointerDown(x, y, {}, 2);
    context.pointerUp(x, y);
    CHECK(editor.focused());
    CHECK(editor.session().selectedText() == "beta");

    CHECK(context.wheel(-3));
    CHECK(editor.scrollY() == doctest::Approx(caret.h * 3));
    draw();
    CHECK(editor.caretRect().y == doctest::Approx(caret.y - caret.h * 3));
    context.pointerPosition(bounds.x + bounds.w + 10, y);
    CHECK(!context.wheel(-3));
}

TEST_CASE_FIXTURE(Editor, "wheel_is_released_at_editor_scroll_boundaries")
{
    std::string text;
    for (int i = 0; i < 100; ++i)
        text += "line\n";
    REQUIRE(document.text(text));
    draw();
    context.pointerMove(bounds.x + 20, bounds.y + 20);
    CHECK(!context.wheel(1));
    CHECK(context.wheel(-0.25f));
    CHECK(editor.scrollY() == 0);
    CHECK(context.wheel(-1000));
    CHECK(editor.scrollY() > 0);
    CHECK(!context.wheel(-1));
    CHECK(context.wheel(1));
}

TEST_CASE_FIXTURE(Editor, "wheel_is_released_when_content_fits")
{
    REQUIRE(document.text("Short note"));
    draw();
    context.pointerMove(bounds.x + 20, bounds.y + 20);
    CHECK(!context.wheel(-1));
    context.focus("editor");
    CHECK(!context.wheel(1));
    CHECK(editor.scrollY() == 0);
}

TEST_CASE_FIXTURE(Editor, "composition_and_read_only_leave_the_document_unchanged")
{
    REQUIRE(document.text("note"));
    context.focus("editor");
    draw();
    editor.composition("ka", 2);
    CHECK(editor.composing());
    std::string error;
    CHECK(!editor.insert("x", error));
    CHECK(error == "IME composition is active");
    context.key(Key::Escape);
    CHECK(!editor.composing());

    auto options = editor.options();
    options.readOnly = true;
    editor.options(options);
    context.textInput("x");
    context.key(Key::Backspace);
    CHECK(!editor.insert("x", error));
    CHECK(error == "editor is read-only");
    CHECK(!editor.wantsTextInput());
    CHECK(document.text() == "note");
}

TEST_CASE_FIXTURE(Editor, "multibyte_layout")
{
    editor.font(fonts.fallback);
    std::string text = "A가中";
    for (size_t i = 0; i < 500; ++i)
        text += "\nA";
    REQUIRE(document.text(text));
    context.focus("editor");
    draw();

    editor.select(0, 0);
    auto start = editor.caretRect().x;
    editor.select(1, 1);
    auto latin = editor.caretRect().x;
    editor.select(4, 4);
    auto korean = editor.caretRect().x;
    editor.select(7, 7);
    auto chinese = editor.caretRect().x;
    CHECK(latin > start);
    CHECK(korean > latin);
    CHECK(chinese > korean);

    context.textInput("가");
    CHECK(document.text().substr(0, 10) == "A가中가");
    CHECK(editor.caretRect().x == doctest::Approx(chinese + korean - latin));
    REQUIRE(editor.undo());
    CHECK(document.text() == text);
    CHECK(editor.caretRect().x == doctest::Approx(chinese));
}

TEST_SUITE_END();
