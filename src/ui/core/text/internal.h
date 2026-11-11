#pragma once

#include <ui/ui.h>
#include <thorvg.h>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace ui::detail
{
struct Face;

class FontManager
{
public:
    FontManager();
    ~FontManager();
    FontId load(const char* utf8Path, unsigned index = 0);
    FontId load(const char* name, const void* bytes, size_t size, unsigned index = 0);
    FontId find(std::string_view name, unsigned weight = 400, bool italic = false) const;
    const std::vector<FontInfo>& fonts() const { return infos; }
    float unitsPerEm(FontId font) const;
    uint32_t glyph(FontId font, uint32_t codepoint) const;
    const char* textFont(FontId font);
    float textKerning(FontId font, uint32_t left, uint32_t right) const;
    uint64_t generation() const { return version; }
    const std::string& error() const { return message; }
    void clear();

private:
    std::vector<std::unique_ptr<Face>> faces;
    std::vector<FontInfo> infos;
    std::unordered_map<FontId, std::string> textFonts;
    uint64_t version = 1;
    std::string message;
    Face* lookup(FontId id) const { return id && id <= faces.size() ? faces[id - 1].get() : nullptr; }
};
FontManager& fontManager();
}  // namespace ui::detail
