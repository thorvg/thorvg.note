#include <storage/internal.h>
#include "common/files.h"

using namespace storage;

namespace
{
Result<Text> decodeText(std::string_view bytes, std::optional<size_t> limit = {})
{
    detail::Decoder decoder(limit);
    if (auto error = decoder.append(bytes)) return {{}, error};
    return decoder.finish();
}

Result<std::string> encodeText(std::string_view text, FileFormat format = {}, std::optional<size_t> limit = {})
{
    if (auto error = detail::validateOutput(text, format, limit)) return {{}, error};
    std::string bytes;
    auto error = detail::encodeChunks(text, format, [&](std::string_view chunk) {
        bytes.append(chunk);
        return Error{};
    });
    return {std::move(bytes), error};
}

std::string hex(const std::array<uint8_t, 32>& digest)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto byte : digest) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
}  // namespace

TEST_SUITE_BEGIN("codec");

TEST_CASE("round_trip")
{
    struct Case
    {
        const char* name;
        const char* text;
        FileFormat format;
        const char* bytes;
    };
    const Case cases[] = {
        {"lf", "first\nsecond\n", {LineEnding::Lf, false}, "first\nsecond\n"},
        {"crlf_bom", "한글 é 🙂\nsecond", {LineEnding::Crlf, true}, "\xef\xbb\xbf한글 é 🙂\r\nsecond"},
        {"cr", "first\nsecond", {LineEnding::Cr, false}, "first\rsecond"},
        {"empty", "", {}, ""},
        {"bom_only", "", {LineEnding::Lf, true}, "\xef\xbb\xbf"},
    };
    for (const auto& entry : cases) {
        CAPTURE(entry.name);
        auto encoded = encodeText(entry.text, entry.format);
        REQUIRE(encoded);
        CHECK_EQ(encoded.value, entry.bytes);

        auto decoded = decodeText(entry.bytes);
        REQUIRE(decoded);
        CHECK_EQ(decoded.value.text, entry.text);
        CHECK(decoded.value.format.ending == entry.format.ending);
        CHECK_EQ(decoded.value.format.bom, entry.format.bom);
    }
}

TEST_CASE("chunk_boundaries")
{
    const std::string bytes = "\xef\xbb\xbf한🙂\r\n둘\r\n끝";
    SUBCASE("two_chunks")
    {
        for (size_t split = 0; split <= bytes.size(); ++split) {
            CAPTURE(split);
            detail::Decoder decoder;
            REQUIRE(!decoder.append(std::string_view(bytes).substr(0, split)));
            REQUIRE(!decoder.append(std::string_view(bytes).substr(split)));
            auto result = decoder.finish();
            REQUIRE(result);
            CHECK_EQ(result.value.text, "한🙂\n둘\n끝");
            CHECK(result.value.format.bom);
            CHECK(result.value.format.ending == LineEnding::Crlf);
        }
    }
    SUBCASE("single_bytes")
    {
        detail::Decoder decoder;
        for (char byte : bytes)
            REQUIRE(!decoder.append({&byte, 1}));
        auto result = decoder.finish();
        REQUIRE(result);
        CHECK_EQ(result.value.text, "한🙂\n둘\n끝");
    }
}

TEST_CASE("mixed_line_endings")
{
    auto mixed = decodeText("one\r\ntwo\nthree\rfour");
    REQUIRE(mixed);
    CHECK_EQ(mixed.value.text, "one\ntwo\nthree\nfour");
    CHECK(mixed.value.format.ending == LineEnding::Mixed);
    CHECK(encodeText(mixed.value.text, mixed.value.format).error.code == ErrorCode::InvalidText);

    auto format = mixed.value.format;
    format.ending = LineEnding::Crlf;
    auto encoded = encodeText(mixed.value.text, format);
    REQUIRE(encoded);
    CHECK_EQ(encoded.value, "one\r\ntwo\r\nthree\r\nfour");
}

TEST_CASE("invalid_utf8")
{
    const std::pair<const char*, const char*> cases[] = {
        {"continuation", "\x80"},
        {"overlong_2", "\xc0\x80"},
        {"overlong_3", "\xe0\x80\xaf"},
        {"surrogate", "\xed\xa0\x80"},
        {"unicode_range", "\xf4\x90\x80\x80"},
        {"invalid_lead", "\xf5\x80\x80\x80"},
        {"truncated_3", "ok\xe2\x82"},
        {"truncated_4", "\xf0\x90"},
    };
    for (const auto& entry : cases) {
        CAPTURE(entry.first);
        auto decoded = decodeText(entry.second);
        CHECK(decoded.error.code == ErrorCode::NotText);
        CHECK(decoded.value.text.empty());
        CHECK(encodeText(entry.second).error.code == ErrorCode::InvalidText);
    }
}

TEST_CASE("invalid_input")
{
    SUBCASE("unsupported_encoding")
    {
        const std::string_view cases[] = {{"\xff\xfeX\0", 4}, {"\xfe\xff\0X", 4}, {"\0\0\xfe\xff", 4}};
        for (size_t i = 0; i < std::size(cases); ++i) {
            CAPTURE(i);
            CHECK(decodeText(cases[i]).error.code == ErrorCode::NotText);
        }
    }
    SUBCASE("binary")
    {
        auto bytes = std::string_view("a\0b", 3);
        CHECK(decodeText(bytes).error.code == ErrorCode::NotText);
        CHECK(encodeText(bytes).error.code == ErrorCode::InvalidText);
    }
    SUBCASE("unnormalized_text")
    {
        CHECK(encodeText("valid\r\ntext").error.code == ErrorCode::InvalidText);
    }
    SUBCASE("size_limit")
    {
        CHECK(decodeText("four", 3).error.code == ErrorCode::TooLarge);
        CHECK(decodeText("", 0));
        CHECK(encodeText("", {LineEnding::Lf, true}, 2).error.code == ErrorCode::TooLarge);
    }
}

TEST_CASE("stream_validation")
{
    struct Case
    {
        std::string_view bytes;
        ErrorCode error;
    };
    const Case cases[] = {
        {"Text\r\n한글 🙂\rLast", ErrorCode::None},
        {"\xef\xbb\xbfText", ErrorCode::None},
        {{"a\0b", 3}, ErrorCode::NotText},
        {{"\xff\xfeX\0", 4}, ErrorCode::NotText},
        {"\x80", ErrorCode::NotText},
        {"end\xe2\x82", ErrorCode::NotText},
    };
    for (size_t i = 0; i < std::size(cases); ++i) {
        CAPTURE(i);
        detail::Decoder validator({}, true);
        for (char byte : cases[i].bytes)
            validator.append({&byte, 1});
        auto result = validator.finish();
        CHECK(result.error.code == cases[i].error);
        CHECK(result.value.text.empty());
    }
}

TEST_CASE("sha256")
{
    const std::pair<std::string, std::string> cases[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
    };
    for (size_t i = 0; i < std::size(cases); ++i) {
        CAPTURE(i);
        const auto& entry = cases[i];
        detail::Sha256 hash;
        for (size_t offset = 0; offset < entry.first.size(); offset += 17)
            hash.append(std::string_view(entry.first).substr(offset, 17));
        CHECK_EQ(hex(hash.finish()), entry.second);
    }
}

TEST_SUITE_END();
