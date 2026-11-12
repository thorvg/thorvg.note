#include <thornote/preferences/theme_profiles.h>
#include <common/doctest/doctest.h>

TEST_CASE("markdown_theme_tokens_are_configurable_and_resolvable")
{
    tn::ThemeProfile profile;
    std::string error;
    REQUIRE(tn::parseTheme(R"(return {schema = 1, colors = {
        markdownHeading = "#123456", markdownHeadingMarker = "#A12345", markdownMarker = "#234567", markdownCode = "#345678",
        markdownLink = "#456789", markdownLinkMarker = "#B12345", markdownImage = "#56789A", markdownQuote = "#6789AB",
        markdownCodeBackground = "#789ABC", markdownStrong = "#C12345", markdownStrongMarker = "#D12345", markdownList = "#E12345", markdownListMarker = "#F12345"
    }})",
                           "markdown-theme.lua", false, profile, error));
    CHECK(profile.colors.markdownHeading.r == 0x12);
    CHECK(profile.colors.markdownHeadingMarker.r == 0xA1);
    CHECK(profile.colors.markdownLinkMarker.r == 0xB1);
    CHECK(profile.colors.markdownStrong.r == 0xC1);
    CHECK(profile.colors.markdownStrongMarker.r == 0xD1);
    CHECK(profile.colors.markdownList.r == 0xE1);
    CHECK(profile.colors.markdownListMarker.r == 0xF1);
    CHECK(profile.colors.markdownMarker.r == 0x23);
    CHECK(profile.colors.markdownCode.r == 0x34);
    CHECK(profile.colors.markdownLink.r == 0x45);
    CHECK(profile.colors.markdownImage.r == 0x56);
    CHECK(profile.colors.markdownQuote.r == 0x67);
    CHECK(profile.colors.markdownCodeBackground.r == 0x78);
    ui::StyleColor color;
    REQUIRE(ui::styleColor("markdownHeading", color));
    CHECK(color.resolve(profile.colors).r == 0x12);
    REQUIRE(ui::styleColor("markdownCodeBackground", color));
    CHECK(color.resolve(profile.colors).r == 0x78);
}
