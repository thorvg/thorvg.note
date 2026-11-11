#pragma once

#include <ui/ui.h>
#include <algorithm>
#include <set>

namespace tn::platform
{
// Substitute faces found for one preferred face. covers(id, codepoint) reports whether a face draws a codepoint.
struct FontCoverage
{
    std::set<uint32_t> checked;
    std::vector<ui::FontId> fallback;

    // Codepoints no known face draws; each is reported once.
    template<typename Covers>
    std::vector<uint32_t> pending(ui::FontId preferred, const std::vector<uint32_t>& points, Covers&& covers)
    {
        std::vector<uint32_t> result;
        for (auto cp : points) {
            if (!checked.insert(cp).second || covers(preferred, cp)) continue;
            if (std::none_of(fallback.begin(), fallback.end(), [&](auto id) { return covers(id, cp); })) result.push_back(cp);
        }
        return result;
    }
    // A substitute draws the whole text, so one without basic Latin is never used.
    template<typename Covers>
    void add(ui::FontId id, Covers&& covers)
    {
        if (id && covers(id, 'a') && std::find(fallback.begin(), fallback.end(), id) == fallback.end()) fallback.push_back(id);
    }
    template<typename Covers>
    ui::FontId best(ui::FontId preferred, const std::vector<uint32_t>& points, Covers&& covers) const
    {
        auto missing = [&](auto id) {
            return std::count_if(points.begin(), points.end(), [&](auto cp) { return !covers(id, cp); });
        };
        auto best = preferred;
        auto count = missing(best);
        for (auto id : fallback) {
            if (!count) break;
            auto next = missing(id);
            if (next < count) {
                best = id;
                count = next;
            }
        }
        return best;
    }
};
}  // namespace tn::platform
