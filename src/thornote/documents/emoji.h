#pragma once

#include <ui/ui.h>

namespace tn
{
struct Emoji
{
    std::string_view code, names, text;
};

std::span<const Emoji> emojiCatalog();
size_t emojiIndex(std::string_view text);
size_t emojiMatches(std::string_view query, std::vector<size_t>& matches);
void matchEmoji(std::string_view text, size_t base, std::vector<ui::TextWidget>& ranges);

class EmojiAssets
{
public:
    EmojiAssets();
    ~EmojiAssets();
    std::shared_ptr<const ui::IconAsset> load(std::string_view name, double now);
    bool pending() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace tn
