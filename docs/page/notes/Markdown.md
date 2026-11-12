# Markdown Base

Write Markdown directly. Syntax stays visible while text takes on its style.
Click anywhere to edit this guide and try the examples yourself.

## Headings

Start a line with one to six # characters followed by a space.
Headings are bold and scale relative to the document font size.

### A third-level heading
#### A fourth-level heading
##### A fifth-level heading
###### A sixth-level heading

## Text styles

Use **bold text**, *italic text*, and ~~strikethrough~~ within a sentence.
Combine them with **bold and *italic* text**.

## Lists

- Start a bullet with a dash and a space.
- Keep each item on its own line.
  - Indent an item to create a nested list.

1. Start an ordered list with a number and a period.
2. Add a space before the item text.
3. Continue with the next item.

## Code

Use backticks for inline code: `canvas->draw()`.
Place three backticks on separate lines around a code block.

```cpp
auto shape = tvg::Shape::gen();
shape->appendRect(20, 20, 160, 80);
shape->fill(80, 140, 220);
```

## Links and images

Put link text in brackets and the destination in parentheses:
[ThorVG](https://www.thorvg.org)

An image reference uses the same syntax with an exclamation mark:
![A vector illustration](illustration.svg)

## Quotes

> Start a quoted line with > and a space.
> Continue the quote on the next line.

## Make it your own

Edit a heading, add a list item, or change a link above.
Use the text-size shortcuts to resize the document; headings scale with it.
Theme colors distinguish Markdown markers from the text they style.
