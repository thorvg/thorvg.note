#include <fstream>
#include "common/fonts.h"

namespace
{
using ui::test::fontPath;

std::vector<char> fontBytes(const char* name)
{
    std::ifstream input(fontPath(name), std::ios::binary);
    REQUIRE(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}  // namespace

TEST_SUITE_BEGIN("font");

TEST_CASE("loading_reuses_faces_and_updates_metadata")
{
    ui::detail::FontManager manager;
    auto generation = manager.generation();
    auto plain = manager.load(fontPath("M1Plain.ttf").c_str());
    REQUIRE(plain);
    CHECK(manager.generation() > generation);
    generation = manager.generation();
    CHECK(manager.load(fontPath("M1Plain.ttf").c_str()) == plain);
    CHECK(manager.generation() == generation);
    CHECK(manager.fonts().size() == 1);
    CHECK(manager.find("m1 plain") == plain);
    CHECK(manager.unitsPerEm(plain) == doctest::Approx(1000));
    CHECK(manager.glyph(plain, 'A'));
    CHECK(!manager.glyph(plain, 0xac00));
    CHECK(!manager.glyph(plain, 0x110000));
    CHECK(!manager.glyph(plain, 0xd800));

    auto fallback = manager.load(fontPath("M1Fallback.otf").c_str());
    REQUIRE(fallback);
    CHECK(manager.find("M1Fallback") == fallback);
    CHECK(manager.glyph(fallback, 0xac00));
    CHECK(manager.glyph(fallback, 0x4e2d));
}

TEST_CASE("macintosh_name_records_are_decoded")
{
    ui::detail::FontManager manager;
    auto font = manager.load(fontPath("M1MacName.ttf").c_str());
    REQUIRE(font);
    CHECK(manager.fonts().front().family == "M1 Macintosh Family");
    CHECK(manager.fonts().front().name == "M1MacName");
    CHECK(manager.find("M1MacName") == font);
}

TEST_CASE("legacy_kerning_matches_the_renderer")
{
    ui::detail::FontManager manager;
    for (auto name : {"M1Plain.ttf", "M1Kern.ttf", "M1KernV1.ttf", "M1Gpos.ttf"}) {
        CAPTURE(name);
        auto font = manager.load(fontPath(name).c_str());
        REQUIRE(font);
        auto a = manager.glyph(font, 'A'), v = manager.glyph(font, 'V');
        auto legacy = std::string_view(name) == "M1Kern.ttf" || std::string_view(name) == "M1KernV1.ttf";
        CHECK(manager.textKerning(font, a, v) == doctest::Approx(legacy ? -200 : 0));
        CHECK(manager.textKerning(font, v, a) == doctest::Approx(legacy ? -50 : 0));
    }
}

TEST_CASE("memory_loading_owns_a_copy_of_the_input")
{
    auto bytes = fontBytes("M1Plain.ttf");
    ui::detail::FontManager manager;
    auto font = manager.load("owned", bytes.data(), bytes.size());
    REQUIRE(font);
    std::fill(bytes.begin(), bytes.end(), 0);
    CHECK(manager.glyph(font, 'A'));
    CHECK(manager.find("owned") == font);
}

TEST_CASE("rejected_fonts_preserve_manager_state")
{
    ui::detail::FontManager manager;
    auto generation = manager.generation();
    SUBCASE("missing path")
    {
        CHECK(!manager.load(nullptr));
    }
    SUBCASE("truncated memory")
    {
        CHECK(!manager.load("invalid", "not a font", 10));
    }
    SUBCASE("standalone file has no second face")
    {
        CHECK(!manager.load(fontPath("M1Plain.ttf").c_str(), 1));
    }
    SUBCASE("standalone memory has no second face")
    {
        auto bytes = fontBytes("M1Plain.ttf");
        CHECK(!manager.load("invalid", bytes.data(), bytes.size(), 1));
    }
    SUBCASE("collection file")
    {
        CHECK(!manager.load(fontPath("M1Collection.ttc").c_str()));
    }
    SUBCASE("collection memory")
    {
        auto bytes = fontBytes("M1Collection.ttc");
        CHECK(!manager.load("collection", bytes.data(), bytes.size()));
    }
    CHECK(!manager.error().empty());
    CHECK(manager.fonts().empty());
    CHECK(manager.generation() == generation);

    REQUIRE(manager.load(fontPath("M1Plain.ttf").c_str()));
    CHECK(manager.error().empty());
}

TEST_SUITE_END();
