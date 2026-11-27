#include "emoji.h"
#include "emoji_data.h"
#include <algorithm>
#include <array>
#include <map>
#include <cstring>
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
constexpr size_t maxBytes = 2 * 1024 * 1024;

auto token(std::string_view value)
{
    return std::lower_bound(std::begin(emojiTokens), std::end(emojiTokens), value, [](const auto& item, auto text) { return item.value < text; });
}

// Every token spans two bytes or more, so the leading pair of a text tells whether one can start there.
static_assert(std::ranges::none_of(emojiTokens, [](const auto& item) { return item.value.size() < 2; }));
constexpr auto tokenPairs = [] {
    std::array<uint64_t, 1024> bits{};
    for (const auto& item : emojiTokens) {
        auto pair = unsigned(static_cast<unsigned char>(item.value[0])) << 8 | static_cast<unsigned char>(item.value[1]);
        bits[pair / 64] |= uint64_t(1) << pair % 64;
    }
    return bits;
}();
}  // namespace

std::span<const Emoji> emojiCatalog()
{
    return emojiData;
}

size_t emojiIndex(std::string_view text)
{
    auto found = token(text);
    return found != std::end(emojiTokens) && found->value == text ? found->index : size_t(-1);
}

size_t emojiMatches(std::string_view query, std::vector<size_t>& matches)
{
    matches.clear();
    if (query.starts_with(':')) {
        query.remove_prefix(1);
        if (query.ends_with(':')) query = {};
    } else query = {};
    for (size_t i = 0; i < std::size(emojiCatalog()); ++i) {
        auto names = emojiCatalog()[i].names;
        while (!names.empty()) {
            auto end = names.find('|');
            auto name = names.substr(0, end);
            if (name.substr(1).starts_with(query)) {
                matches.push_back(i);
                break;
            }
            if (end == names.npos) break;
            names.remove_prefix(end + 1);
        }
    }
    return matches.size();
}

void matchEmoji(std::string_view text, size_t base, std::vector<ui::TextWidget>& ranges)
{
    for (size_t at = 0; at + 1 < text.size();) {
        size_t matched = 0;
        auto pair = unsigned(static_cast<unsigned char>(text[at])) << 8 | static_cast<unsigned char>(text[at + 1]);
        if ((tokenPairs[pair / 64] >> pair % 64) & 1) {
            for (size_t end = at + 2; end <= std::min(text.size(), at + 128); ++end) {
                auto prefix = text.substr(at, end - at);
                auto found = token(prefix);
                if (found == std::end(emojiTokens) || !found->value.starts_with(prefix)) break;
                if (found->value == prefix) matched = end - at;
            }
        }
        if (matched) {
            ranges.push_back({base + at, base + at + matched, ui::TextWidget::Replace, 1.0f});
            at += matched;
        } else {
            ++at;
            while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
                ++at;
        }
    }
}

struct EmojiAssets::Impl
{
    struct Entry
    {
        std::shared_ptr<ui::IconAsset> asset;
        bool pending = true;
        double used = 0, retry = 0;
    };
    std::map<std::string, std::shared_ptr<Entry>, std::less<>> entries;
#ifndef __EMSCRIPTEN__
    task::TaskQueue queue;
#endif
    double now = 0;

    static void complete(const std::shared_ptr<Entry>& entry, std::string name, std::string data)
    {
        entry->pending = false;
        if (data.empty() || data.size() > maxBytes) return;
        auto asset = std::make_shared<ui::IconAsset>();
        asset->name = std::move(name);
        asset->data = std::move(data);
        asset->mime = "lot";
        asset->tint = false;
        asset->loop = true;
        asset->markers.fill("rest");
        std::string error;
        if (ui::validateIcon(*asset, error)) entry->asset = std::move(asset);
    }

    void request(std::string name, const std::shared_ptr<Entry>& entry)
    {
        auto code = name.substr(6);
        auto first = code.find('_');
        if (first == code.npos) first = code.size();
        if (first < 4) code.insert(0, 4 - first, '0');
        std::string url = "https://fonts.gstatic.com/s/e/notoemoji/latest/" + code + "/lottie.json";
        entry->pending = true;
        entry->retry = now + 30;
#ifdef __EMSCRIPTEN__
        struct Request
        {
            std::shared_ptr<Entry> entry;
            std::string name;
        };
        auto data = new Request{entry, std::move(name)};
        emscripten_fetch_attr_t attributes;
        emscripten_fetch_attr_init(&attributes);
        std::strcpy(attributes.requestMethod, "GET");
        attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attributes.timeoutMSecs = 10000;
        attributes.userData = data;
        attributes.onsuccess = [](emscripten_fetch_t* fetch) {
            std::unique_ptr<Request> request(static_cast<Request*>(fetch->userData));
            std::string data;
            if (fetch->numBytes <= maxBytes) data.assign(fetch->data, fetch->numBytes);
            complete(request->entry, std::move(request->name), std::move(data));
            emscripten_fetch_close(fetch);
        };
        attributes.onerror = [](emscripten_fetch_t* fetch) {
            std::unique_ptr<Request> request(static_cast<Request*>(fetch->userData));
            request->entry->pending = false;
            emscripten_fetch_close(fetch);
        };
        emscripten_fetch(&attributes, url.c_str());
#else
        queue.submit([entry, name = std::move(name), url = std::move(url)](task::TaskContext& context) mutable {
            std::string data;
            auto handle = curl_easy_init();
            if (handle) {
                curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
                curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
                curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 10000L);
                curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
                curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
                curl_easy_setopt(handle, CURLOPT_WRITEDATA, &data);
                curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, +[](char* bytes, size_t size, size_t count, void* target) -> size_t {
                    auto& output = *static_cast<std::string*>(target);
                    auto length = size * count;
                    if (length > maxBytes - output.size()) return 0;
                    output.append(bytes, length);
                    return length; });
                curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &context);
                curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, +[](void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int { return static_cast<task::TaskContext*>(context)->cancelled() ? 1 : 0; });
                if (curl_easy_perform(handle) != CURLE_OK) data.clear();
                curl_easy_cleanup(handle);
            }
            return [entry, name = std::move(name), data = std::move(data)]() mutable {
                complete(entry, std::move(name), std::move(data));
            };
        });
#endif
    }
};

EmojiAssets::EmojiAssets() :
    impl(std::make_unique<Impl>())
{
#ifndef __EMSCRIPTEN__
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    (void)initialized;
#endif
}
EmojiAssets::~EmojiAssets() = default;

bool EmojiAssets::pending() const
{
    return std::any_of(impl->entries.begin(), impl->entries.end(), [](const auto& item) { return item.second->pending; });
}

std::shared_ptr<const ui::IconAsset> EmojiAssets::load(std::string_view name, double now)
{
    if (!name.starts_with("emoji/")) return {};
    auto code = name.substr(6);
    auto item = std::lower_bound(std::begin(emojiCodes), std::end(emojiCodes), code, [](const auto& item, auto code) { return item.value < code; });
    if (item == std::end(emojiCodes) || item->value != code) return {};
    impl->now = now;
#ifndef __EMSCRIPTEN__
    impl->queue.poll();
#endif
    auto found = impl->entries.find(name);
    auto pending = std::count_if(impl->entries.begin(), impl->entries.end(), [](const auto& item) { return item.second->pending; });
    if (found == impl->entries.end()) {
        size_t bytes = 0;
        for (const auto& item : impl->entries)
            if (item.second->asset) bytes += item.second->asset->data.size();
        while (impl->entries.size() >= 96 || bytes >= 16 * 1024 * 1024) {
            auto oldest = impl->entries.end();
            for (auto it = impl->entries.begin(); it != impl->entries.end(); ++it) {
                if (it->second->pending || (it->second->asset && it->second->asset.use_count() > 1)) continue;
                if (oldest == impl->entries.end() || it->second->used < oldest->second->used) oldest = it;
            }
            if (oldest == impl->entries.end()) return {};
            if (oldest->second->asset) bytes -= oldest->second->asset->data.size();
            impl->entries.erase(oldest);
        }
        if (pending >= 6) return {};
        auto entry = std::make_shared<Impl::Entry>();
        found = impl->entries.emplace(std::string(name), entry).first;
        impl->request(found->first, entry);
    } else if (!found->second->asset && !found->second->pending && now >= found->second->retry && pending < 6) impl->request(found->first, found->second);
    found->second->used = now;
    return found->second->asset;
}
}  // namespace tn
