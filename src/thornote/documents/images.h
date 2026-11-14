#pragma once

#include <ui/ui.h>
#include <storage/storage.h>
#include <thornote/path.h>

namespace tn
{
struct MarkdownImage
{
    size_t start = 0, end = 0;
    std::string source;
    size_t sourceStart = 0, sourceEnd = 0;
};
std::vector<MarkdownImage> markdownImages(std::string_view text);
std::string imagePath(std::string_view source, const storage::Path& document);
std::string imageMarkdown(const storage::Path& path, const storage::Path& document);

class ImagePreviews
{
public:
    ImagePreviews();
    ~ImagePreviews();
    void begin();
    void end();
    bool pending() const;
    void render(ui::Context&, std::string_view id, ui::Rect, const std::string& source);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace tn
