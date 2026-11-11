#include "internal.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ui::detail
{
struct Face
{
    stbtt_fontinfo font{};
    std::vector<unsigned char> bytes;
    FontInfo info;
    float unitsPerEm = 1000;
    size_t kern = 0, kernEnd = 0;  // legacy kern table range in bytes
    mutable std::unordered_map<uint32_t, uint32_t> cmap;
    mutable std::array<uint64_t, 1024> coverage{}, known{};
    static std::unique_ptr<Face> make(std::vector<unsigned char> bytes, unsigned index, std::string name, std::string& error);
    uint32_t glyph(uint32_t codepoint) const;
    float textKerning(uint32_t left, uint32_t right) const;
};

namespace
{
uint16_t u16(const unsigned char* p)
{
    return uint16_t(p[0]) << 8 | p[1];
}
uint32_t u32(const unsigned char* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
size_t table(const std::vector<unsigned char>& bytes, size_t offset, const char* tag, size_t* size = nullptr)
{
    auto count = u16(bytes.data() + offset + 4);
    for (size_t i = 0; i < count; ++i) {
        auto record = bytes.data() + offset + 12 + i * 16;
        if (std::memcmp(record, tag, 4)) continue;
        if (size) *size = u32(record + 12);
        return u32(record + 8);
    }
    return 0;
}
bool validDirectory(const std::vector<unsigned char>& bytes, size_t offset)
{
    if (offset > bytes.size() || bytes.size() - offset < 12) return false;
    auto count = u16(bytes.data() + offset + 4);
    if (!count || count > (bytes.size() - offset - 12) / 16) return false;
    for (size_t i = 0; i < count; ++i) {
        auto record = bytes.data() + offset + 12 + i * 16;
        size_t start = u32(record + 8), size = u32(record + 12);
        if (start > bytes.size() || size > bytes.size() - start) return false;
        if ((!std::memcmp(record, "head", 4) && size < 54) || (!std::memcmp(record, "hhea", 4) && size < 36)
            || (!std::memcmp(record, "maxp", 4) && size < 6) || (!std::memcmp(record, "cmap", 4) && size < 4)) return false;
    }
    return table(bytes, offset, "head") && table(bytes, offset, "hhea") && table(bytes, offset, "maxp") && table(bytes, offset, "cmap");
}
void utf8(std::string& result, uint32_t cp)
{
    if (cp < 0x80) result += char(cp);
    else if (cp < 0x800) {
        result += char(0xc0 | cp >> 6);
        result += char(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        result += char(0xe0 | cp >> 12);
        result += char(0x80 | ((cp >> 6) & 63));
        result += char(0x80 | (cp & 63));
    } else {
        result += char(0xf0 | cp >> 18);
        result += char(0x80 | ((cp >> 12) & 63));
        result += char(0x80 | ((cp >> 6) & 63));
        result += char(0x80 | (cp & 63));
    }
}
std::string name(const stbtt_fontinfo& font, int id)
{
    int size = 0;
    auto value = stbtt_GetFontNameString(&font, &size, 3, 1, 0x409, id);
    if (!value) value = stbtt_GetFontNameString(&font, &size, 0, 3, 0, id);
    if (!value && id == 6) {
        // PostScript names are ASCII, including in Macintosh Roman records.
        value = stbtt_GetFontNameString(&font, &size, 1, 0, 0, id);
        if (value && std::all_of(value, value + size, [](unsigned char ch) { return ch >= 32 && ch < 127; })) return {value, size_t(size)};
        return {};
    }
    if (!value) return {};
    std::string result;
    for (int i = 0; i + 1 < size; i += 2) {
        uint32_t cp = u16(reinterpret_cast<const unsigned char*>(value + i));
        if (cp >= 0xd800 && cp <= 0xdbff && i + 3 < size) {
            auto low = u16(reinterpret_cast<const unsigned char*>(value + i + 2));
            if (low < 0xdc00 || low > 0xdfff) continue;
            cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
            i += 2;
        }
        if (cp >= 0xd800 && cp <= 0xdfff) continue;
        utf8(result, cp);
    }
    return result;
}
}  // namespace

std::unique_ptr<Face> Face::make(std::vector<unsigned char> bytes, unsigned index, std::string requestedName, std::string& error)
{
    if (bytes.size() < 12 || bytes.size() > size_t(std::numeric_limits<int>::max()) || index > unsigned(std::numeric_limits<int>::max())) {
        error = "invalid font size or face index";
        return {};
    }
    if (!std::memcmp(bytes.data(), "ttcf", 4)) {
        error = "font collections are not supported";
        return {};
    }
    if (index) {
        error = "face index is outside the font";
        return {};
    }
    constexpr size_t offset = 0;
    if (!validDirectory(bytes, offset)) {
        error = "invalid font table directory";
        return {};
    }
    auto face = std::unique_ptr<Face>(new Face);
    face->bytes = std::move(bytes);
    auto& font = face->font;
    if (!stbtt_InitFont(&font, face->bytes.data(), int(offset))) {
        error = "unsupported font outlines";
        return {};
    }
    auto head = table(face->bytes, offset, "head");
    face->unitsPerEm = u16(face->bytes.data() + head + 18);
    if (!face->unitsPerEm || font.numGlyphs < 1) {
        error = "invalid font metrics";
        return {};
    }
    // ThorVG kerns TrueType outlines with the legacy kern table only.
    if (font.glyf) {
        size_t size = 0;
        face->kern = table(face->bytes, offset, "kern", &size);
        face->kernEnd = face->kern + size;
    }
    face->info.family = name(font, 1);
    face->info.name = requestedName.empty() ? name(font, 6) : std::move(requestedName);
    if (face->info.name.empty()) face->info.name = face->info.family;
    auto flags = u16(face->bytes.data() + head + 44);
    face->info.weight = flags & 1 ? 700 : 400;
    face->info.italic = (flags & 2) != 0;
    face->info.index = index;
    return face;
}

uint32_t Face::glyph(uint32_t codepoint) const
{
    if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return 0;
    if (codepoint < 0x10000) {
        auto bit = uint64_t(1) << (codepoint % 64);
        if ((known[codepoint / 64] & bit) && !(coverage[codepoint / 64] & bit)) return 0;
    }
    auto found = cmap.find(codepoint);
    if (found != cmap.end()) return found->second;
    auto id = uint32_t(stbtt_FindGlyphIndex(&font, int(codepoint)));
    if (codepoint < 0x10000) {
        known[codepoint / 64] |= uint64_t(1) << (codepoint % 64);
        if (id) coverage[codepoint / 64] |= uint64_t(1) << (codepoint % 64);
    }
    if (id) cmap.emplace(codepoint, id);
    return id;
}
float Face::textKerning(uint32_t left, uint32_t right) const
{
    if (left >= unsigned(font.numGlyphs) || right >= unsigned(font.numGlyphs)) return 0;
    if (kernEnd - kern < 8) return 0;
    // Follow ThorVG's kern lookup so carets land on the rendered glyphs: version 0 and 1.0 tables,
    // summing every horizontal format 0 subtable. Cross-stream values move glyphs vertically only.
    auto data = bytes.data();
    auto v1 = u16(data + kern) != 0;
    auto count = v1 ? u32(data + kern + 4) : u16(data + kern + 2);
    auto at = kern + (v1 ? 8 : 4);
    auto key = left << 16 | right;
    auto result = 0.0f;
    while (count-- > 0) {
        size_t header = v1 ? 8 : 6;
        if (kernEnd - at < header) break;
        size_t length = v1 ? u32(data + at) : u16(data + at + 2);
        if (length < header || length > kernEnd - at) break;
        auto next = at + length;
        auto coverage = u16(data + at + 4);
        auto format = v1 ? coverage & 0xff : coverage >> 8;
        auto horizontal = v1 ? !(coverage & 0x8000) : (coverage & 0x01);
        auto unsupported = v1 ? (coverage & 0x2000) : (coverage & 0x02);
        auto crossStream = v1 ? (coverage & 0x4000) : (coverage & 0x04);
        at += header;
        if (format == 0 && horizontal && !unsupported) {
            if (next - at < 8) break;
            size_t pairs = u16(data + at);
            at += 8;
            if (pairs > (next - at) / 6) break;
            size_t lo = 0, hi = pairs;
            while (lo < hi) {
                auto mid = (lo + hi) / 2;
                if (u32(data + at + mid * 6) < key) lo = mid + 1;
                else hi = mid;
            }
            if (!crossStream && lo < pairs && u32(data + at + lo * 6) == key) {
                auto value = float(int16_t(u16(data + at + lo * 6 + 4)));
                if (!v1 && (coverage & 0x08)) result = value;
                else result += value;
            }
        }
        at = next;
    }
    return result;
}

namespace
{
// UTF-8 text of a path; the standard keeps it in char8_t, which the font metadata does not use.
std::string utf8(const std::filesystem::path& path)
{
    auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path utf8Path(std::string_view text)
{
    return std::u8string(text.begin(), text.end());
}

std::string key(std::string_view name)
{
    std::string result;
    for (unsigned char ch : name)
        if (ch != ' ' && ch != '-' && ch != '_') result += char(ch < 128 ? std::tolower(ch) : ch);
    return result;
}
}  // namespace
FontManager::FontManager() = default;
FontManager::~FontManager()
{
    clear();
}
void FontManager::clear()
{
    for (const auto& entry : textFonts)
        tvg::Text::unload(entry.second.c_str());
    textFonts.clear();
    faces.clear();
    infos.clear();
    message.clear();
    ++version;
}
FontManager& fontManager()
{
    static FontManager manager;
    return manager;
}

FontId FontManager::load(const char* utf8Path, unsigned index)
{
    if (!utf8Path || !*utf8Path) {
        message = "empty font path";
        return 0;
    }
    std::error_code ec;
    auto path = std::filesystem::canonical(detail::utf8Path(utf8Path), ec);
    if (ec) {
        message = "cannot resolve font path";
        return 0;
    }
    for (const auto& info : infos)
        if (info.path == utf8(path) && info.index == index) return info.id;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    auto size = input.tellg();
    if (!input || size < 12) {
        message = "cannot read font";
        return 0;
    }
    if (size > std::numeric_limits<int>::max()) {
        message = "font exceeds parser address range";
        return 0;
    }
    std::vector<unsigned char> bytes(size_t(size), 0);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) {
        message = "cannot read complete font";
        return 0;
    }
    auto face = Face::make(std::move(bytes), index, {}, message);
    if (!face) return 0;
    face->info.id = FontId(faces.size() + 1);
    face->info.path = utf8(path);
    infos.push_back(face->info);
    faces.push_back(std::move(face));
    ++version;
    message.clear();
    return infos.back().id;
}
FontId FontManager::load(const char* name, const void* bytes, size_t size, unsigned index)
{
    if (!bytes || size < 12 || size > size_t(std::numeric_limits<int>::max())) {
        message = "invalid font bytes";
        return 0;
    }
    auto begin = static_cast<const unsigned char*>(bytes);
    auto face = Face::make({begin, begin + size}, index, name ? name : "", message);
    if (!face) return 0;
    face->info.id = FontId(faces.size() + 1);
    infos.push_back(face->info);
    faces.push_back(std::move(face));
    ++version;
    message.clear();
    return infos.back().id;
}
FontId FontManager::find(std::string_view name, unsigned weight, bool italic) const
{
    auto target = key(name);
    FontId best = 0;
    unsigned distance = ~0u;
    for (const auto& info : infos) {
        if (key(info.name) != target && key(info.family) != target && key(utf8(utf8Path(info.path).stem())) != target) continue;
        auto score = unsigned(std::abs(int(info.weight) - int(weight))) + (info.italic != italic ? 1000u : 0u);
        if (score < distance) {
            distance = score;
            best = info.id;
        }
    }
    return best;
}
float FontManager::unitsPerEm(FontId font) const
{
    auto face = lookup(font);
    return face ? face->unitsPerEm : 1000;
}
uint32_t FontManager::glyph(FontId font, uint32_t codepoint) const
{
    auto face = lookup(font);
    return face ? face->glyph(codepoint) : 0;
}
const char* FontManager::textFont(FontId font)
{
    auto found = textFonts.find(font);
    if (found != textFonts.end()) return found->second.c_str();
    auto face = lookup(font);
    if (!face) return nullptr;
    const auto& bytes = face->bytes;
    auto name = "ui-text-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" + std::to_string(font);
    if (tvg::Text::load(name.c_str(), reinterpret_cast<const char*>(bytes.data()), uint32_t(bytes.size()), "ttf", true) != tvg::Result::Success) {
        message = "ThorVG Text cannot load this font face";
        return nullptr;
    }
    return textFonts.emplace(font, std::move(name)).first->second.c_str();
}
float FontManager::textKerning(FontId font, uint32_t left, uint32_t right) const
{
    auto face = lookup(font);
    return face ? face->textKerning(left, right) : 0;
}
}  // namespace ui::detail

namespace ui
{
FontId loadFont(const char* path, unsigned index)
{
    return detail::fontManager().load(path, index);
}
FontId loadFont(const char* name, const void* bytes, size_t size, unsigned index)
{
    return detail::fontManager().load(name, bytes, size, index);
}
FontId findFont(std::string_view name, unsigned weight, bool italic)
{
    return detail::fontManager().find(name, weight, italic);
}
const std::vector<FontInfo>& fonts()
{
    return detail::fontManager().fonts();
}
const std::string& fontError()
{
    return detail::fontManager().error();
}
void unloadFonts()
{
    detail::fontManager().clear();
}
}  // namespace ui
