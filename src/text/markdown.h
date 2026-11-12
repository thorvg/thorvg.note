#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace tn
{
enum class MarkdownKind
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
    size_t start, end;
    MarkdownKind kind;
    unsigned level = 0;
    MarkdownKind element = MarkdownKind::Marker;  ///< Owning element for a Marker range.
};

// Ranges refer to the original UTF-8 source, including visible Markdown delimiters.
std::vector<MarkdownSpan> parseMarkdown(std::string_view source);
}  // namespace tn
