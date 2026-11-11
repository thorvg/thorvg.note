#include <thornote/platform/system_fonts.h>
#include <thornote/platform/font_coverage.h>
#include <thornote/preferences/font_catalog.h>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <CoreText/CoreText.h>

namespace tn
{
namespace
{
std::string utf8(CFStringRef value)
{
    if (!value) return {};
    std::string result(size_t(CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8)) + 1, '\0');
    if (!CFStringGetCString(value, result.data(), result.size(), kCFStringEncodingUTF8)) return {};
    result.resize(std::char_traits<char>::length(result.c_str()));
    return result;
}
void u16(std::vector<unsigned char>& bytes, size_t at, uint16_t value)
{
    bytes[at] = value >> 8;
    bytes[at + 1] = value & 255;
}
void u32(std::vector<unsigned char>& bytes, size_t at, uint32_t value)
{
    u16(bytes, at, value >> 16);
    u16(bytes, at + 2, value & 65535);
}
uint32_t checksum(const std::vector<unsigned char>& bytes)
{
    uint32_t result = 0;
    for (size_t i = 0; i < bytes.size(); i += 4) {
        uint32_t word = 0;
        for (size_t j = 0; j < 4; ++j)
            word = (word << 8) | (i + j < bytes.size() ? bytes[i + j] : 0);
        result += word;
    }
    return result;
}
// Copy only the selected face's tables. A system TTC can contain hundreds of MiB
// of unrelated faces; no system-font filename or collection index is assumed.
std::vector<unsigned char> fontData(CTFontRef font)
{
    struct Table
    {
        CTFontTableTag tag;
        std::vector<unsigned char> bytes;
    };
    std::vector<Table> tables;
    for (auto tag : {kCTFontTableHead, kCTFontTableHhea, kCTFontTableMaxp, kCTFontTableHmtx, kCTFontTableCmap,
                     kCTFontTableLoca, kCTFontTableGlyf, kCTFontTableCFF, kCTFontTableName, kCTFontTableKern,
                     kCTFontTableGPOS, kCTFontTableOS2, kCTFontTablePost}) {
        auto data = CTFontCopyTable(font, tag, kCTFontTableOptionNoOptions);
        if (!data) continue;
        auto length = CFDataGetLength(data);
        if (!length) {
            CFRelease(data);
            continue;
        }
        auto begin = CFDataGetBytePtr(data);
        tables.push_back({tag, {begin, begin + length}});
        CFRelease(data);
        if (tag == kCTFontTableHead && tables.back().bytes.size() >= 12) u32(tables.back().bytes, 8, 0);
    }
    if (std::none_of(tables.begin(), tables.end(), [](const auto& table) { return table.tag == kCTFontTableGlyf || table.tag == kCTFontTableCFF; })) return {};
    std::sort(tables.begin(), tables.end(), [](const auto& a, const auto& b) { return a.tag < b.tag; });
    std::vector<unsigned char> bytes(12 + tables.size() * 16);
    bool cff = std::any_of(tables.begin(), tables.end(), [](const auto& table) { return table.tag == kCTFontTableCFF; });
    u32(bytes, 0, cff ? 0x4f54544f : 0x00010000);
    u16(bytes, 4, uint16_t(tables.size()));
    uint16_t power = 1, selector = 0;
    while (power * 2 <= tables.size()) {
        power *= 2;
        ++selector;
    }
    u16(bytes, 6, power * 16);
    u16(bytes, 8, selector);
    u16(bytes, 10, uint16_t(tables.size() * 16 - power * 16));
    size_t head = 0;
    for (size_t i = 0; i < tables.size(); ++i) {
        auto& table = tables[i];
        auto offset = bytes.size();
        u32(bytes, 12 + i * 16, table.tag);
        u32(bytes, 16 + i * 16, checksum(table.bytes));
        u32(bytes, 20 + i * 16, uint32_t(offset));
        u32(bytes, 24 + i * 16, uint32_t(table.bytes.size()));
        if (table.tag == kCTFontTableHead && table.bytes.size() >= 12) head = offset;
        bytes.insert(bytes.end(), table.bytes.begin(), table.bytes.end());
        bytes.resize((bytes.size() + 3) & ~size_t(3), 0);
    }
    if (head) u32(bytes, head + 8, 0xb1b0afba - checksum(bytes));
    return bytes;
}
}  // namespace

struct SystemFonts::Impl
{
    CTFontRef system = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 16, nullptr);
    CTFontRef emphasized = CTFontCreateUIFontForLanguage(kCTFontUIFontEmphasizedSystem, 16, nullptr);
    CTFontRef document = CTFontCreateUIFontForLanguage(kCTFontUIFontUser, 16, nullptr);
    ui::FontId primary = 0, boldPrimary = 0;
    std::unordered_map<std::string, ui::FontId> loaded;
    // Native counterpart of each registered face; null when the platform cannot open it.
    std::unordered_map<ui::FontId, CTFontRef> faces;
    std::unordered_map<ui::FontId, platform::FontCoverage> coverage;
    ~Impl()
    {
        for (const auto& face : faces)
            if (face.second) CFRelease(face.second);
        if (system) CFRelease(system);
        if (emphasized) CFRelease(emphasized);
        if (document) CFRelease(document);
    }
    ui::FontId load(CTFontRef font)
    {
        if (!font) return 0;
        auto name = CTFontCopyPostScriptName(font);
        auto key = utf8(name);
        if (name) CFRelease(name);
        auto found = loaded.find(key);
        if (found != loaded.end()) return found->second;
        auto bytes = fontData(font);
        auto id = bytes.empty() ? 0 : ui::loadFont(key.c_str(), bytes.data(), bytes.size());
        if (id) faces.emplace(id, static_cast<CTFontRef>(CFRetain(font)));
        loaded.emplace(std::move(key), id);
        return id;
    }
    CTFontRef face(ui::FontId id)
    {
        auto found = faces.find(id);
        if (found != faces.end()) return found->second;
        CTFontRef font = nullptr;
        if (id && id <= ui::fonts().size()) {
            const auto& path = ui::fonts()[id - 1].path;
            auto url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.data()), CFIndex(path.size()), false);
            auto descriptors = url ? CTFontManagerCreateFontDescriptorsFromURL(url) : nullptr;
            if (descriptors && CFArrayGetCount(descriptors))
                font = CTFontCreateWithFontDescriptor(static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(descriptors, 0)), 16, nullptr);
            if (descriptors) CFRelease(descriptors);
            if (url) CFRelease(url);
        }
        faces.emplace(id, font);
        return font;
    }
    bool covers(ui::FontId id, uint32_t cp)
    {
        auto font = face(id);
        if (!font) return false;
        UniChar characters[2];
        CGGlyph glyphs[2];
        return CTFontGetGlyphsForCharacters(font, characters, glyphs, utf16(cp, characters));
    }
    static CFIndex utf16(uint32_t cp, UniChar* characters)
    {
        if (cp < 0x10000) {
            characters[0] = UniChar(cp);
            return 1;
        }
        characters[0] = UniChar(0xd800 + ((cp - 0x10000) >> 10));
        characters[1] = UniChar(0xdc00 + ((cp - 0x10000) & 0x3ff));
        return 2;
    }
    ui::FontId substitute(CTFontRef base, CFStringRef string, CFRange range)
    {
        // Null language uses the current system language and its native cascade.
        auto face = CTFontCreateForStringWithLanguage(base, string, range, nullptr);
        auto id = load(face);
        if (face) CFRelease(face);
        // Private UI outlines may need a public document-font alternative.
        if (!id && document) {
            face = CTFontCreateForStringWithLanguage(document, string, range, nullptr);
            id = load(face);
            if (face) CFRelease(face);
        }
        return id;
    }
};

SystemFonts::SystemFonts() :
    impl(std::make_unique<Impl>()) {}
SystemFonts::~SystemFonts() = default;
ui::FontId SystemFonts::defaultFont(bool bold)
{
    if (bold) {
        if (!impl->boldPrimary) impl->boldPrimary = impl->load(impl->emphasized);
        return impl->boldPrimary;
    }
    if (!impl->primary) impl->primary = impl->load(impl->system);
    return impl->primary;
}

ui::FontId SystemFonts::load(const FontChoice& choice, std::string& error)
{
    bool automatic = choice.path.empty();
    auto id = automatic ? defaultFont() : ui::loadFont(utf8(choice.path).c_str(), choice.index);
    auto loadError = ui::fontError();
    if (!automatic && !choice.name.empty()) {
        auto matches = [&](ui::FontId font) {
            if (!font || font > ui::fonts().size()) return false;
            const auto& name = ui::fonts()[font - 1].name;
            return name.empty() || name == choice.name;
        };
        // CoreText also enumerates named variations. Their ordinal is not a
        // file face index; a requested default style can still be file face 0.
        if (!matches(id) && choice.index) {
            auto base = ui::loadFont(utf8(choice.path).c_str(), 0);
            if (base) id = base;
        }
        if (id && !matches(id)) {
            error = "This font style requires unsupported variation settings.";
            return 0;
        }
    }
    if (!id) {
        error = "Cannot load this font: " + loadError;
        return 0;
    }
    error.clear();
    return id;
}

ui::FontId SystemFonts::fontFor(ui::FontId preferred, const std::vector<uint32_t>& codepoints)
{
    auto base = preferred && !codepoints.empty() ? impl->face(preferred) : nullptr;
    if (!base) return preferred;
    auto covers = [&](ui::FontId id, uint32_t cp) {
        return impl->covers(id, cp);
    };
    auto& coverage = impl->coverage[preferred];
    for (auto cp : coverage.pending(preferred, codepoints, covers)) {
        UniChar characters[2];
        auto count = Impl::utf16(cp, characters);
        auto string = CFStringCreateWithCharacters(nullptr, characters, count);
        if (!string) continue;
        coverage.add(impl->substitute(base, string, CFRangeMake(0, count)), covers);
        CFRelease(string);
    }
    return coverage.best(preferred, codepoints, covers);
}

}  // namespace tn
