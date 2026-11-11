#pragma once

#include <thornote/path.h>
#include <filesystem>
#include <string>
#include <vector>

namespace tn
{
struct FontChoice
{
    std::filesystem::path path;
    // Native descriptor ordinal on macOS; may include named variations.
    unsigned index = 0;
    std::string name;
};

// Metadata only. An empty path denotes System (Automatic), not a font file.
// Font outlines are loaded by the app when a face is selected.
std::vector<FontChoice> availableFonts();
}  // namespace tn
