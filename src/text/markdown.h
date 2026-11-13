#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace tn
{
enum class MarkdownKind : uint8_t
{
    Heading,
    Strong,
    Emphasis,
    Code,
    Link,
    Image,
    Quote,
    Marker,
    Strike,
    List,
    NumberedList
};

struct MarkdownSpan
{
    uint32_t start, end;  ///< Source offsets fit the parser's 32-bit input range.
    MarkdownKind kind;
    uint8_t level = 0;
    MarkdownKind element = MarkdownKind::Marker;  ///< Owning element for a Marker range.
};

struct MarkdownBlock
{
    size_t start = 0, end = 0;
    // Span offsets are relative to start, so edits do not rewrite subsequent spans.
    std::vector<MarkdownSpan> spans;
    bool independent = false;  ///< Inline syntax is closed within each unindented row.
};

// Ranges refer to the original UTF-8 source, including visible Markdown delimiters.
std::vector<MarkdownSpan> parseMarkdown(std::string_view source);
std::vector<MarkdownBlock> parseMarkdownBlocks(std::string_view source);
}  // namespace tn
