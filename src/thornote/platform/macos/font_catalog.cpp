#include <thornote/platform/font_catalog.h>
#include <CoreText/CoreText.h>
#include <set>

namespace tn::platform
{
std::vector<FontChoice> installedFonts()
{
    std::vector<FontChoice> result;
    auto string = [](CFStringRef value) {
        if (!value) return std::string{};
        std::string text(CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1, '\0');
        if (!CFStringGetCString(value, text.data(), text.size(), kCFStringEncodingUTF8)) return std::string{};
        text.resize(std::char_traits<char>::length(text.c_str()));
        return text;
    };
    auto collection = CTFontCollectionCreateFromAvailableFonts(nullptr);
    auto descriptors = collection ? CTFontCollectionCreateMatchingFontDescriptors(collection) : nullptr;
    std::set<std::filesystem::path> paths;
    if (descriptors) {
        for (CFIndex i = 0; i < CFArrayGetCount(descriptors); ++i) {
            auto descriptor = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(descriptors, i));
            auto url = static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute));
            if (!url) continue;
            auto path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
            if (path) {
                paths.insert(utf8Path(string(path)));
                CFRelease(path);
            }
            CFRelease(url);
        }
        CFRelease(descriptors);
    }
    if (collection) CFRelease(collection);
    for (const auto& path : paths) {
        auto extension = path.extension().string();
        if (extension != ".ttf" && extension != ".otf") continue;
        auto text = utf8(path);
        auto url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(text.data()), text.size(), false);
        auto faces = url ? CTFontManagerCreateFontDescriptorsFromURL(url) : nullptr;
        if (faces) {
            for (CFIndex i = 0; i < CFArrayGetCount(faces) && i < 256; ++i) {
                auto descriptor = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(faces, i));
                auto name = static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(descriptor, kCTFontNameAttribute));
                if (name) {
                    auto label = string(name);
                    if (!label.empty() && label.front() != '.') result.push_back({path, unsigned(i), std::move(label)});
                    CFRelease(name);
                }
            }
            CFRelease(faces);
        }
        if (url) CFRelease(url);
    }
    return result;
}
}  // namespace tn::platform
