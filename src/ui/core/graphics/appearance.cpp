#include <ui/ui.h>

namespace ui
{

Color resolve(const std::optional<StyleColor>& color, const Theme& theme, Color fallback)
{
    return color ? color->resolve(theme) : fallback;
}

bool styleColor(std::string_view value, StyleColor& output)
{
    const std::pair<std::string_view, Color Theme::*> tokens[] = {
        {"background", &Theme::background},
        {"surface", &Theme::surface},
        {"raised", &Theme::raised},
        {"border", &Theme::border},
        {"text", &Theme::text},
        {"muted", &Theme::muted},
        {"accent", &Theme::accent},
        {"accentText", &Theme::accentText},
        {"selection", &Theme::selection},
        {"highlight", &Theme::highlight},
        {"highlightActive", &Theme::highlightActive},
        {"success", &Theme::success},
        {"danger", &Theme::danger},
        {"markdownHeading", &Theme::markdownHeading},
        {"markdownMarker", &Theme::markdownMarker},
        {"markdownCode", &Theme::markdownCode},
        {"markdownLink", &Theme::markdownLink},
        {"markdownImage", &Theme::markdownImage},
        {"markdownQuote", &Theme::markdownQuote},
        {"markdownCodeBackground", &Theme::markdownCodeBackground},
        {"markdownHeadingMarker", &Theme::markdownHeadingMarker},
        {"markdownStrong", &Theme::markdownStrong},
        {"markdownStrongMarker", &Theme::markdownStrongMarker},
        {"markdownEmphasis", &Theme::markdownEmphasis},
        {"markdownEmphasisMarker", &Theme::markdownEmphasisMarker},
        {"markdownStrike", &Theme::markdownStrike},
        {"markdownStrikeMarker", &Theme::markdownStrikeMarker},
        {"markdownCodeMarker", &Theme::markdownCodeMarker},
        {"markdownLinkMarker", &Theme::markdownLinkMarker},
        {"markdownImageMarker", &Theme::markdownImageMarker},
        {"markdownList", &Theme::markdownList},
        {"markdownListMarker", &Theme::markdownListMarker},
        {"markdownQuoteMarker", &Theme::markdownQuoteMarker},
        {"markdownNumber", &Theme::markdownNumber},
        {"markdownNumberMarker", &Theme::markdownNumberMarker},
    };
    for (const auto& token : tokens) {
        if (value == token.first) {
            output = {{0, 0, 0, 0}, token.second};
            return true;
        }
    }
    if (value == "transparent") {
        output = {{0, 0, 0, 0}, nullptr};
        return true;
    }
    if ((value.size() != 7 && value.size() != 9) || value[0] != '#') return false;
    std::array<uint8_t, 4> channels{0, 0, 0, 255};
    for (size_t i = 1; i < value.size(); ++i) {
        auto c = value[i];
        int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
            : c >= 'A' && c <= 'F'                                        ? c - 'A' + 10
                                                                          : -1;
        if (digit < 0) return false;
        auto& channel = channels[(i - 1) / 2];
        channel = i % 2 ? uint8_t(digit * 16) : uint8_t(channel + digit);
    }
    output = {{channels[0], channels[1], channels[2], channels[3]}, nullptr};
    return true;
}

}  // namespace ui
