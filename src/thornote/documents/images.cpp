#include "images.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#else
#include <curl/curl.h>
#include <task/task.h>
#endif

namespace tn
{
namespace
{
constexpr size_t maxBytes = 16 * 1024 * 1024;
int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
        : c >= 'A' && c <= 'F'                                   ? c - 'A' + 10
                                                                 : -1;
}
std::string decode(std::string_view value, bool escapes)
{
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (escapes && value[i] == '\\' && i + 1 < value.size()) result += value[++i];
        else if (!escapes && value[i] == '%' && i + 2 < value.size() && hex(value[i + 1]) >= 0 && hex(value[i + 2]) >= 0) {
            result += char(hex(value[i + 1]) * 16 + hex(value[i + 2]));
            i += 2;
        } else result += value[i];
    }
    return result;
}
bool remote(std::string_view source)
{
    return source.starts_with("https://") || source.starts_with("http://");
}
std::string mime(std::string_view bytes)
{
    if (bytes.starts_with("\x89PNG\r\n\x1a\n")) return "png";
    if (bytes.starts_with("\xff\xd8\xff")) return "jpeg";
    if (bytes.starts_with("GIF87a") || bytes.starts_with("GIF89a")) return "gif";
    if (bytes.starts_with("RIFF") && bytes.substr(8, 4) == "WEBP") return "webp";
    auto first = bytes.find_first_not_of(" \t\r\n");
    if (first != bytes.npos && bytes[first] == '{') return "lot";
    if (bytes.find("<svg") != bytes.npos) return "svg+xml";
    return {};
}
}  // namespace

std::vector<MarkdownImage> markdownImages(std::string_view text)
{
    std::vector<MarkdownImage> result;
    for (size_t at = text.find("!["); at != text.npos && result.size() < 16; at = text.find("![", at + 2)) {
        size_t slashes = 0;
        for (size_t i = at; i && text[i - 1] == '\\'; --i)
            ++slashes;
        if (slashes % 2) continue;
        size_t pos = at + 2, depth = 1;
        for (; pos < text.size() && depth; ++pos) {
            if (text[pos] == '\\' && pos + 1 < text.size()) ++pos;
            else if (text[pos] == '[') ++depth;
            else if (text[pos] == ']') --depth;
        }
        if (depth || pos == text.size() || text[pos++] != '(') continue;
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
            ++pos;
        bool angle = pos < text.size() && text[pos] == '<';
        if (angle) ++pos;
        auto first = pos;
        depth = 0;
        for (; pos < text.size(); ++pos) {
            auto c = text[pos];
            if (c == '\\' && pos + 1 < text.size()) {
                ++pos;
                continue;
            }
            if (angle) {
                if (c == '>') break;
            } else if (c == '(') ++depth;
            else if (c == ')') {
                if (!depth) break;
                --depth;
            } else if (!depth && (c == ' ' || c == '\t')) break;
        }
        if (pos == text.size() || depth) continue;
        auto end = pos;
        if (angle && text[pos++] != '>') continue;
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
            ++pos;
        if (pos < text.size() && (text[pos] == '\'' || text[pos] == '"')) {
            auto quote = text[pos++];
            while (pos < text.size() && text[pos] != quote) {
                if (text[pos] == '\\' && pos + 1 < text.size()) ++pos;
                ++pos;
            }
            if (pos == text.size()) continue;
            ++pos;
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
                ++pos;
        }
        if (pos == text.size() || text[pos] != ')') continue;
        result.push_back({at, pos + 1, decode(text.substr(first, end - first), true), angle ? first - 1 : first, angle ? end + 1 : end});
        at = pos;
    }
    return result;
}

std::string imagePath(std::string_view source, const storage::Path& document)
{
    if (source.empty() || remote(source)) return std::string(source);
    if (source.starts_with("file://")) source.remove_prefix(7);
    else if (source.find("://") != source.npos) return {};
    auto decoded = decode(source, false);
    if (decoded.find('\0') != decoded.npos) return {};
    auto path = utf8Path(decoded);
    if (path.is_relative() && !document.empty()) path = document.parent_path() / path;
    return utf8(path.lexically_normal());
}

std::string imageMarkdown(const storage::Path& path, const storage::Path& document)
{
    auto relative = document.empty() ? path : path.lexically_relative(document.parent_path());
    auto value = utf8(relative.empty() ? path : relative);
    std::string result = "![](<";
    constexpr char digits[] = "0123456789ABCDEF";
    for (unsigned char c : value) {
        if (c <= 32 || c == '<' || c == '>' || c == '%' || c == '#' || c == '\\') {
            result += '%';
            result += digits[c >> 4];
            result += digits[c & 15];
        } else result += char(c);
    }
    return result + ">)";
}

struct ImagePreviews::Impl
{
    struct Entry
    {
        std::unique_ptr<ui::Images> images;
        std::string error;
        bool pending = true;
        double used = 0;
    };
    std::map<std::string, std::shared_ptr<Entry>, std::less<>> entries;
#ifndef __EMSCRIPTEN__
    task::TaskQueue queue;
#endif
    static void decode(const std::shared_ptr<Entry>& entry, std::string data)
    {
        if (data.empty()) {
            entry->error = "Could not load image";
            return;
        }
        auto type = mime(data);
        if (type.empty()) {
            entry->error = "Unsupported image format";
            return;
        }
        entry->images = std::make_unique<ui::Images>();
        if (!entry->images->replace({{"image", std::move(data), std::move(type)}}, entry->error)) entry->images.reset();
    }
    static void complete(const std::shared_ptr<Entry>& entry, std::string data)
    {
        decode(entry, std::move(data));
        entry->pending = false;
    }
    void request(const std::string& source, const std::shared_ptr<Entry>& entry)
    {
#ifdef __EMSCRIPTEN__
        if (remote(source)) {
            auto request = new std::shared_ptr<Entry>(entry);
            emscripten_fetch_attr_t attributes;
            emscripten_fetch_attr_init(&attributes);
            std::strcpy(attributes.requestMethod, "GET");
            attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
            attributes.timeoutMSecs = 10000;
            attributes.userData = request;
            attributes.onsuccess = [](emscripten_fetch_t* fetch) {
                std::unique_ptr<std::shared_ptr<Entry>> entry(static_cast<std::shared_ptr<Entry>*>(fetch->userData));
                std::string data;
                if (fetch->numBytes <= maxBytes) data.assign(fetch->data, fetch->numBytes);
                complete(*entry, std::move(data));
                emscripten_fetch_close(fetch);
            };
            attributes.onerror = [](emscripten_fetch_t* fetch) {
                std::unique_ptr<std::shared_ptr<Entry>> entry(static_cast<std::shared_ptr<Entry>*>(fetch->userData));
                complete(*entry, {});
                emscripten_fetch_close(fetch);
            };
            emscripten_fetch(&attributes, source.c_str());
            return;
        }
#else
        queue.submit([entry, source](task::TaskContext& context) {
#endif
        std::string data;
#ifndef __EMSCRIPTEN__
        if (remote(source)) {
            auto handle = curl_easy_init();
            if (handle) {
                curl_easy_setopt(handle, CURLOPT_URL, source.c_str());
                curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https");
                curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
                curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
                curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
                curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 10000L);
                curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
                curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
                curl_easy_setopt(handle, CURLOPT_WRITEDATA, &data);
                curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, +[](char* bytes, size_t size, size_t count, void* target) -> size_t {
                    auto& data = *static_cast<std::string*>(target);
                    auto length = size * count;
                    if (length > maxBytes - data.size()) return 0;
                    data.append(bytes, length);
                    return length; });
                curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &context);
                curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, +[](void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int { return static_cast<task::TaskContext*>(context)->cancelled() ? 1 : 0; });
                if (curl_easy_perform(handle) != CURLE_OK) data.clear();
                curl_easy_cleanup(handle);
            }
        } else
#endif
        {
            std::ifstream input(utf8Path(source), std::ios::binary | std::ios::ate);
            auto size = input.tellg();
            if (size > 0 && size <= std::streamoff(maxBytes)) {
                data.resize(size_t(size));
                input.seekg(0);
                if (!input.read(data.data(), size)) data.clear();
            }
        }
#ifdef __EMSCRIPTEN__
        complete(entry, std::move(data));
#else
            decode(entry, std::move(data));
            // The completion publishes the decoded asset after the worker's writes.
            return [entry] {
                entry->pending = false;
            };
        });
#endif
    }
};

ImagePreviews::ImagePreviews() :
    impl(std::make_unique<Impl>())
{
#ifndef __EMSCRIPTEN__
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    (void)initialized;
#endif
}
ImagePreviews::~ImagePreviews() = default;
bool ImagePreviews::pending() const
{
    return std::any_of(impl->entries.begin(), impl->entries.end(), [](const auto& item) { return item.second->pending; });
}
void ImagePreviews::begin()
{
#ifndef __EMSCRIPTEN__
    impl->queue.poll();
#endif
    for (auto& [source, entry] : impl->entries)
        if (!entry->pending && entry->images) entry->images->begin();
}
void ImagePreviews::end()
{
    for (auto& [source, entry] : impl->entries)
        if (!entry->pending && entry->images) entry->images->end();
}
void ImagePreviews::render(ui::Context& context, std::string_view id, ui::Rect bounds, const std::string& source)
{
    context.rect(bounds, context.theme().surface, 4, context.theme().border);
    if (source.empty()) {
        context.text(bounds.inset(12), "Choose image...", context.theme().muted, 12, false, ui::Align::Center);
        return;
    }
    auto found = impl->entries.find(source);
    if (found == impl->entries.end()) {
        auto pending = std::count_if(impl->entries.begin(), impl->entries.end(), [](const auto& item) { return item.second->pending; });
        if (pending >= 4) {
            context.requestFrameAt(context.time() + .05);
            return;
        }
        if (impl->entries.size() >= 16) {
            auto oldest = impl->entries.end();
            for (auto it = impl->entries.begin(); it != impl->entries.end(); ++it)
                if (!it->second->pending && it->second->used != context.time() && (oldest == impl->entries.end() || it->second->used < oldest->second->used)) oldest = it;
            if (oldest == impl->entries.end()) return;
            impl->entries.erase(oldest);
        }
        auto entry = std::make_shared<Impl::Entry>();
        found = impl->entries.emplace(source, entry).first;
        impl->request(source, entry);
    }
    auto& entry = *found->second;
    entry.used = context.time();
    if (entry.pending) {
        context.text(bounds.inset(12), "Loading image...", context.theme().muted, 12, false, ui::Align::Center);
        context.requestFrameAt(context.time() + .05);
        return;
    }
    if (entry.images) entry.images->render(context, id, "image", bounds.inset(6), ui::ImageFit::Contain);
    else context.text(bounds.inset(12), entry.error + " - click to choose a file", context.theme().muted, 12, false, ui::Align::Center);
}
}  // namespace tn
