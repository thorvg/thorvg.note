#include <thornote/platform/display_text.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <cstdint>

namespace tn
{
std::string composedText(std::string_view text)
{
    if (std::all_of(text.begin(), text.end(), [](char ch) { return uint8_t(ch) < 0x80; })) return std::string(text);
    auto source = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(text.data()), CFIndex(text.size()), kCFStringEncodingUTF8, false);
    if (!source) return std::string(text);  // Invalid UTF-8 is shown as it is.
    auto composed = CFStringCreateMutableCopy(nullptr, 0, source);
    CFRelease(source);
    if (!composed) return std::string(text);
    CFStringNormalize(composed, kCFStringNormalizationFormC);
    CFRange range{0, CFStringGetLength(composed)};
    CFIndex size = 0;
    CFStringGetBytes(composed, range, kCFStringEncodingUTF8, 0, false, nullptr, 0, &size);
    std::string result(size_t(size), '\0');
    CFStringGetBytes(composed, range, kCFStringEncodingUTF8, 0, false, reinterpret_cast<UInt8*>(result.data()), size, nullptr);
    CFRelease(composed);
    return result;
}
}  // namespace tn
