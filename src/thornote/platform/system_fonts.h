#pragma once

#include <ui/ui.h>
#include <algorithm>
#include <memory>

namespace tn
{
struct FontChoice;

// App-owned platform adapter over the shared UI font registry.
// Native providers resolve the UI face and the face that draws a text best.
class SystemFonts
{
public:
    SystemFonts();
    ~SystemFonts();
    ui::FontId defaultFont(bool bold = false);  // 0 when a platform provider is unavailable.
    ui::FontId load(const FontChoice&, std::string& error);
    // One face draws a whole label or document: preferred while it covers the codepoints,
    // else the loaded substitute that misses the fewest. Zero stays zero.
    ui::FontId fontFor(ui::FontId preferred, const std::vector<uint32_t>& codepoints);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Adds the non-ASCII codepoints of valid UTF-8 text to a sorted list of distinct values.
inline bool collectCodepoints(std::string_view utf8, std::vector<uint32_t>& sorted)
{
    bool grew = false;
    for (size_t i = 0; i < utf8.size();) {
        auto lead = uint8_t(utf8[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        size_t size = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3
                                                      : 2;
        if (lead < 0xc0 || i + size > utf8.size()) {
            ++i;
            continue;
        }
        uint32_t cp = lead & (0xff >> (size + 1));
        for (size_t j = 1; j < size; ++j)
            cp = (cp << 6) | (uint8_t(utf8[i + j]) & 0x3f);
        i += size;
        auto at = std::lower_bound(sorted.begin(), sorted.end(), cp);
        if (at != sorted.end() && *at == cp) continue;
        sorted.insert(at, cp);
        grew = true;
    }
    return grew;
}

}  // namespace tn
