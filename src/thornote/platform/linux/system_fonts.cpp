#include <thornote/platform/system_fonts.h>
#include <thornote/platform/font_coverage.h>
#include <thornote/preferences/font_catalog.h>
#include <fontconfig/fontconfig.h>
#include <map>

namespace tn
{
namespace
{
using Pattern = std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)>;
using FontSet = std::unique_ptr<FcFontSet, decltype(&FcFontSetDestroy)>;
using CharSet = std::unique_ptr<FcCharSet, decltype(&FcCharSetDestroy)>;
}  // namespace

struct SystemFonts::Impl
{
    std::unique_ptr<FcConfig, decltype(&FcConfigDestroy)> config{FcInitLoadConfigAndFonts(), FcConfigDestroy};
    ui::FontId primary[2]{};
    bool attempted[2]{};
    // Character set of each registered face; null when fontconfig cannot describe it.
    std::map<ui::FontId, CharSet> charsets;
    std::map<ui::FontId, platform::FontCoverage> coverage;

    Pattern pattern(ui::FontId preferred, bool bold, FcCharSet* characters = nullptr)
    {
        Pattern result(FcPatternCreate(), FcPatternDestroy);
        if (!result) return result;
        auto family = "sans-serif";
        auto weight = bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR;
        auto slant = FC_SLANT_ROMAN;
        if (preferred && preferred <= ui::fonts().size()) {
            const auto& info = ui::fonts()[preferred - 1];
            if (!info.family.empty()) family = info.family.c_str();
            weight = FcWeightFromOpenType(int(info.weight));
            slant = info.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN;
        }
        FcPatternAddString(result.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>(family));
        FcPatternAddInteger(result.get(), FC_WEIGHT, weight);
        FcPatternAddInteger(result.get(), FC_SLANT, slant);
        FcPatternAddBool(result.get(), FC_SCALABLE, FcTrue);
        if (characters) FcPatternAddCharSet(result.get(), FC_CHARSET, characters);
        FcConfigSubstitute(config.get(), result.get(), FcMatchPattern);
        FcDefaultSubstitute(result.get());
        return result;
    }
    ui::FontId load(FcPattern* face)
    {
        FcChar8* file = nullptr;
        int index = 0;
        if (FcPatternGetString(face, FC_FILE, 0, &file) != FcResultMatch) return 0;
        FcPatternGetInteger(face, FC_INDEX, 0, &index);
        if (index < 0) return 0;
        auto id = ui::loadFont(reinterpret_cast<const char*>(file), unsigned(index));
        FcCharSet* supported = nullptr;
        if (id && !charsets.count(id) && FcPatternGetCharSet(face, FC_CHARSET, 0, &supported) == FcResultMatch)
            charsets.emplace(id, CharSet(FcCharSetCopy(supported), FcCharSetDestroy));
        return id;
    }
    FcCharSet* charset(ui::FontId id)
    {
        auto found = charsets.find(id);
        if (found != charsets.end()) return found->second.get();
        CharSet result(nullptr, FcCharSetDestroy);
        if (config && id && id <= ui::fonts().size()) {
            const auto& info = ui::fonts()[id - 1];
            auto file = reinterpret_cast<const FcChar8*>(info.path.c_str());
            // Faces outside the configured directories, such as bundled ones, become known first.
            if (!info.path.empty()) FcConfigAppFontAddFile(config.get(), file);
            Pattern query(FcPatternCreate(), FcPatternDestroy);
            std::unique_ptr<FcObjectSet, decltype(&FcObjectSetDestroy)> fields(FcObjectSetBuild(FC_CHARSET, nullptr), FcObjectSetDestroy);
            if (query && fields && !info.path.empty()) {
                FcPatternAddString(query.get(), FC_FILE, file);
                FcPatternAddInteger(query.get(), FC_INDEX, int(info.index));
                FontSet matches(FcFontList(config.get(), query.get(), fields.get()), FcFontSetDestroy);
                FcCharSet* supported = nullptr;
                if (matches && matches->nfont && FcPatternGetCharSet(matches->fonts[0], FC_CHARSET, 0, &supported) == FcResultMatch)
                    result.reset(FcCharSetCopy(supported));
            }
        }
        return charsets.emplace(id, std::move(result)).first->second.get();
    }
    bool covers(ui::FontId id, uint32_t cp)
    {
        auto supported = charset(id);
        return supported && FcCharSetHasChar(supported, cp);
    }
    ui::FontId best(ui::FontId preferred, const std::vector<uint32_t>& codepoints)
    {
        if (!config || !charset(preferred)) return preferred;
        auto covers = [&](ui::FontId id, uint32_t cp) {
            return this->covers(id, cp);
        };
        auto& state = coverage[preferred];
        auto missing = state.pending(preferred, codepoints, covers);
        if (missing.empty()) return state.best(preferred, codepoints, covers);
        CharSet characters(FcCharSetCreate(), FcCharSetDestroy);
        auto query = characters ? pattern(preferred, false, characters.get()) : Pattern(nullptr, FcPatternDestroy);
        if (!query) return state.best(preferred, codepoints, covers);
        for (auto cp : missing)
            FcCharSetAddChar(characters.get(), cp);
        FcResult result;
        FontSet matches(FcFontSort(config.get(), query.get(), FcFalse, nullptr, &result), FcFontSetDestroy);
        for (int i = 0; matches && i < matches->nfont && !missing.empty(); ++i) {
            FcCharSet* supported = nullptr;
            auto face = matches->fonts[i];
            if (FcPatternGetCharSet(face, FC_CHARSET, 0, &supported) != FcResultMatch || std::none_of(missing.begin(), missing.end(), [&](auto cp) { return FcCharSetHasChar(supported, cp); })) continue;
            auto id = load(face);
            if (!id || id == preferred) continue;
            state.add(id, covers);
            missing.erase(std::remove_if(missing.begin(), missing.end(), [&](auto cp) { return covers(id, cp); }), missing.end());
        }
        return state.best(preferred, codepoints, covers);
    }
};

SystemFonts::SystemFonts() :
    impl(std::make_unique<Impl>()) {}
SystemFonts::~SystemFonts() = default;
ui::FontId SystemFonts::defaultFont(bool bold)
{
    auto index = bold ? 1 : 0;
    if (impl->attempted[index] || !impl->config) return impl->primary[index];
    impl->attempted[index] = true;
    auto query = impl->pattern(0, bold);
    if (!query) return 0;
    FcResult result;
    FontSet matches(FcFontSort(impl->config.get(), query.get(), FcFalse, nullptr, &result), FcFontSetDestroy);
    if (matches)
        for (int i = 0; i < matches->nfont && !impl->primary[index]; ++i)
            impl->primary[index] = impl->load(matches->fonts[i]);
    return impl->primary[index];
}
ui::FontId SystemFonts::load(const FontChoice& choice, std::string& error)
{
    auto id = choice.path.empty() ? defaultFont() : ui::loadFont(utf8(choice.path).c_str(), choice.index);
    error = id ? "" : "Cannot load this font: " + ui::fontError();
    return id;
}
ui::FontId SystemFonts::fontFor(ui::FontId preferred, const std::vector<uint32_t>& codepoints)
{
    return preferred && !codepoints.empty() ? impl->best(preferred, codepoints) : preferred;
}
}  // namespace tn
