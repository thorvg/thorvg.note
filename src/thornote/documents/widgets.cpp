#include "widgets.h"
#include "emoji.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tn
{
namespace
{
int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
        : c >= 'A' && c <= 'F'                                   ? c - 'A' + 10
                                                                 : -1;
}

bool color(std::string_view text, ui::Color& result)
{
    if (text.empty() || text.front() != '#') return false;
    text.remove_prefix(1);
    if (text.size() != 3 && text.size() != 4 && text.size() != 6 && text.size() != 8) return false;
    bool shortForm = text.size() <= 4;
    ui::Color value{0, 0, 0, 255};
    uint8_t* channels[]{&value.r, &value.g, &value.b, &value.a};
    for (size_t i = 0; i < text.size() / (shortForm ? 1 : 2); ++i) {
        auto first = hex(text[i * (shortForm ? 1 : 2)]);
        auto second = shortForm ? first : hex(text[i * 2 + 1]);
        if (first < 0 || second < 0) return false;
        *channels[i] = uint8_t(first * 16 + second);
    }
    result = value;
    return true;
}

std::string literal(ui::Color value, bool alpha)
{
    char text[10];
    if (alpha) std::snprintf(text, sizeof(text), "#%02X%02X%02X%02X", value.r, value.g, value.b, value.a);
    else std::snprintf(text, sizeof(text), "#%02X%02X%02X", value.r, value.g, value.b);
    return text;
}

bool word(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

size_t listContent(std::string_view text, size_t first)
{
    if (first == text.npos) return text.npos;
    auto marker = first;
    if (text[first] == '-' || text[first] == '+' || text[first] == '*') ++first;
    else {
        while (first < text.size() && text[first] >= '0' && text[first] <= '9')
            ++first;
        if (first == marker || first - marker > 9 || first == text.size() || (text[first] != '.' && text[first] != ')')) return text.npos;
        ++first;
    }
    if (first == text.size() || (text[first] != ' ' && text[first] != '\t')) return text.npos;
    first = text.find_first_not_of(" \t", first);
    return first == text.npos ? text.size() : first;
}

size_t task(std::string_view text)
{
    auto first = listContent(text, text.find_first_not_of(" \t"));
    if (first == text.npos || first + 3 > text.size() || text[first] != '[' || text[first + 2] != ']' || (text[first + 1] != ' ' && text[first + 1] != 'x' && text[first + 1] != 'X') || (first + 3 < text.size() && text[first + 3] != ' ' && text[first + 3] != '\t')) return text.npos;
    return first;
}

size_t columns(std::string_view text, size_t end)
{
    size_t column = 0;
    for (size_t i = 0; i < end; ++i)
        column += text[i] == '\t' ? 4 - column % 4 : 1;
    return column;
}

void replace(ui::TextEditor& editor, const ui::TextWidgetEvent& event, std::string_view text)
{
    std::string error;
    editor.replaceWidget(event, text, error);
}
}  // namespace

void DocumentWidgets::attach(ui::TextEditor& editor)
{
    ui::TextWidgetRule colors;
    colors.code = true;
    colors.popupWidth = 288;
    colors.popupHeight = 160;
    colors.match = [](std::string_view text, size_t base, std::vector<ui::TextWidget>& ranges) {
        for (size_t at = text.find('#'); at != text.npos; at = text.find('#', at + 1)) {
            if (at && word(text[at - 1])) continue;
            size_t end = at + 1;
            while (end < text.size() && hex(text[end]) >= 0)
                ++end;
            ui::Color parsed;
            if (end < text.size() && word(text[end])) continue;
            if (color(text.substr(at, end - at), parsed)) {
                ui::Color ink{parsed.r, parsed.g, parsed.b};
                ui::Color background{uint8_t(255 - parsed.r), uint8_t(255 - parsed.g), uint8_t(255 - parsed.b), 28};
                ranges.push_back({base + at, base + end, ui::TextWidget::After, 1.5f, ink, background});
            }
        }
    };
    colors.draw = [](ui::Context& context, std::string_view, ui::Rect bounds, ui::State state, std::string_view source) {
        ui::Color value;
        if (!color(source, value)) return;
        auto side = std::min(bounds.w - 6, bounds.h - 4);
        ui::Rect swatch{bounds.x + 3, bounds.y + (bounds.h - side) / 2, side, side};
        context.rect(swatch, context.theme().background, 3, state.hovered ? context.theme().accent : context.theme().border);
        context.rect(swatch.inset(2), value, 1);
    };
    colors.popup = [this](ui::Context& context, std::string_view id, ui::Rect bounds, ui::TextEditor& editor, const ui::TextWidgetEvent& event) {
        colorPopup(context, id, bounds, editor, event);
    };
    editor.widgets("color", std::move(colors));

    ui::TextWidgetRule tasks;
    tasks.match = [](std::string_view text, size_t base, std::vector<ui::TextWidget>& ranges) {
        auto at = task(text);
        if (at != text.npos) ranges.push_back({base + at, base + at + 3, ui::TextWidget::Replace, 1.4f});
    };
    tasks.draw = [](ui::Context& context, std::string_view id, ui::Rect bounds, ui::State state, std::string_view source) {
        bool checked = source[1] != ' ';
        auto side = std::min(bounds.w - 4, bounds.h - 4);
        ui::Rect box{bounds.x + 2, bounds.y + (bounds.h - side) / 2, side, side};
        const auto& theme = context.theme();
        context.rect(box, checked ? theme.accent : theme.background, 3, state.hovered ? theme.accent : theme.border);
        if (checked) context.icon(std::string(id) + "/check", box, "check", theme.accentText, {.hovered = state.hovered, .pressed = state.pressed}, side * 0.9f);
    };
    tasks.activate = [](ui::TextEditor& editor, const ui::TextWidgetEvent& event) {
        replace(editor, event, event.value[1] == ' ' ? "[x]" : "[ ]");
    };
    editor.widgets("task", std::move(tasks));

    ui::TextWidgetRule emojis;
    emojis.popupWidth = 232;
    emojis.popupHeight = 48;
    emojis.popupPadding = 6;
    emojis.match = matchEmoji;
    emojis.draw = [](ui::Context& context, std::string_view id, ui::Rect bounds, ui::State state, std::string_view source) {
        auto index = emojiIndex(source);
        if (index != size_t(-1)) context.icon(id, bounds, "emoji/" + std::string(emojiCatalog()[index].code), {255, 255, 255}, {.hovered = state.hovered, .pressed = state.pressed}, std::min(bounds.w, bounds.h));
    };
    emojis.clipboard = [](std::string_view source) {
        auto index = emojiIndex(source);
        return std::string(index == size_t(-1) ? source : emojiCatalog()[index].text);
    };
    emojis.suggest = [this](std::string_view prefix) {
        auto at = prefix.rfind(':');
        if (at == prefix.npos || (at && (prefix[at - 1] == ':' || prefix[at - 1] == '/' || prefix[at - 1] == '\\'))) return prefix.npos;
        if (at) {
            auto previous = prefix.rfind(':', at - 1);
            if (previous != prefix.npos && emojiIndex(prefix.substr(previous)) != size_t(-1)) return prefix.npos;
        }
        auto query = prefix.substr(at);
        if (query.size() > 96) return prefix.npos;
        return emojiMatches(query, emojiCandidates) ? at : prefix.npos;
    };
    emojis.popupKey = [this](ui::Key key, ui::TextEditor& editor, const ui::TextWidgetEvent& event) {
        auto count = emojiCandidates.size();
        if (!count) return false;
        if (key == ui::Key::Enter || key == ui::Key::Tab) replace(editor, event, emojiCatalog()[emojiCandidates[emojiSelected % count]].text);
        else if (key == ui::Key::Down || key == ui::Key::Right) emojiSelected = (emojiSelected + 1) % count;
        else if (key == ui::Key::Up || key == ui::Key::Left) emojiSelected = (emojiSelected + count - 1) % count;
        else return false;
        if (emojiSelected < emojiFirst) emojiFirst = emojiSelected;
        else if (emojiSelected >= emojiFirst + emojiColumns) emojiFirst = emojiSelected + 1 - emojiColumns;
        return true;
    };
    emojis.popup = [this](ui::Context& context, std::string_view id, ui::Rect bounds, ui::TextEditor& editor, const ui::TextWidgetEvent& event) {
        if (emojiQuery != event.value) {
            emojiQuery = event.value;
            emojiMatches(event.value, emojiCandidates);
            emojiFirst = 0;
            emojiSelected = 0;
        }
        auto count = emojiCandidates.size();
        emojiColumns = std::clamp(size_t(bounds.w / 44), size_t(1), size_t(5));
        context.scroll(std::string(id) + "/scroll", bounds, emojiFirst, count > emojiColumns ? count - emojiColumns : 0, {.step = 1, .horizontal = true});
        if (count) emojiSelected = std::clamp(emojiSelected, emojiFirst, std::min(count, emojiFirst + emojiColumns) - 1);
        context.clip(bounds, [&] {
            for (size_t i = emojiFirst; i < std::min(count, emojiFirst + emojiColumns); ++i) {
                ui::Rect button{bounds.x + (i - emojiFirst) * 44, bounds.y, 40, bounds.h};
                auto hit = button.intersect(bounds);
                if (hit.w <= 0) continue;
                auto index = emojiCandidates[i];
                auto key = std::string(id) + "/" + std::to_string(index);
                auto state = context.control(key, hit, [&editor, event, index](const ui::Event& input) {
                    if (input.activated()) replace(editor, event, emojiCatalog()[index].text); }, !editor.options().readOnly, ui::ControlFlag::None);
                if (state.hovered || i == emojiSelected) context.rect(button, context.theme().selection, 4);
                context.icon(key + "/icon", button, "emoji/" + std::string(emojiCatalog()[index].code), {255, 255, 255}, {.hovered = state.hovered, .pressed = state.pressed}, 26);
            }
        });
    };
    editor.widgets("emoji", std::move(emojis));
}

void DocumentWidgets::colorPopup(ui::Context& context, std::string_view id, ui::Rect bounds, ui::TextEditor& editor, const ui::TextWidgetEvent& event)
{
    if (!colorInput) colorInput = std::make_unique<ui::TextInput>();
    if (colorRevision != event.revision || colorStart != event.start) {
        colorRevision = event.revision;
        colorStart = event.start;
        colorInput->value(event.value);
    }
    ui::Color value;
    bool valid = color(colorInput->value(), value);
    if (!valid) color(event.value, value);
    bool alpha = event.value.size() == 5 || event.value.size() == 9;
    const auto& theme = context.theme();
    bool compact = bounds.w < 160;
    ui::textField(context, std::string(id) + "/hex", {bounds.x, bounds.y, compact ? bounds.w : bounds.w - 66, 30}, *colorInput, "#RRGGBB", !editor.options().readOnly, [this, &editor, event](ui::Key key) {
        ui::Color parsed;
        if (key != ui::Key::Enter || !color(colorInput->value(), parsed)) return false;
        replace(editor, event, colorInput->value());
        return true;
    });
    if (!compact) ui::button(context, std::string(id) + "/apply", {bounds.x + bounds.w - 58, bounds.y, 58, 30}, "Apply", [this, &editor, event] { replace(editor, event, colorInput->value()); }, {.enabled = valid && !editor.options().readOnly});
    if (bounds.w < 80) return;
    uint8_t ui::Color::*channels[]{&ui::Color::r, &ui::Color::g, &ui::Color::b};
    const char* labels[]{"R", "G", "B"};
    for (size_t i = 0; i < 3; ++i) {
        ui::Rect track{bounds.x + 24, bounds.y + 48 + i * 30, bounds.w - 64, 16};
        auto channel = channels[i];
        context.text({bounds.x, track.y - 4, 20, 24}, labels[i], theme.muted, 10);
        auto low = value, high = value;
        low.*channel = 0;
        high.*channel = 255;
        low.a = high.a = 255;
        context.paint(ui::paintKey('G', track, low, high), [track, low, high] {
            auto gradient = tvg::LinearGradient::gen();
            gradient->linear(track.x, track.y, track.x + track.w, track.y);
            tvg::Fill::ColorStop stops[]{{0, low.r, low.g, low.b, 255}, {1, high.r, high.g, high.b, 255}};
            gradient->colorStops(stops, 2);
            auto shape = tvg::Shape::gen();
            shape->appendRect(track.x, track.y, track.w, track.h, 3, 3);
            shape->fill(gradient);
            return shape;
        });
        context.rect({track.x + track.w * (value.*channel) / 255 - 2, track.y - 2, 4, track.h + 4}, theme.text, 2, theme.background);
        context.control(std::string(id) + "/channel/" + labels[i], track, [this, track, channel, value, alpha](const ui::Event& input) mutable {
            if (input.type != ui::EventType::Press && input.type != ui::EventType::Drag) return;
            value.*channel = uint8_t(std::lround(std::clamp((input.x - track.x) / track.w, 0.0f, 1.0f) * 255));
            colorInput->value(literal(value, alpha)); }, !editor.options().readOnly, ui::ControlFlag::Capture);
        context.text({track.x + track.w + 6, track.y - 4, 34, 24}, std::to_string(value.*channel), theme.muted, 10);
    }
}

std::vector<DocumentWidgets::Chunk> DocumentWidgets::scan(std::string_view text, size_t start, size_t end, ScanState state) const
{
    std::vector<Chunk> result;
    Chunk chunk{start, start, 0, 0, state};
    for (size_t at = start; at < end;) {
        auto stop = std::min(text.find('\n', at), end);
        auto row = text.substr(at, stop - at);
        auto first = row.find_first_not_of(" \t\r");
        bool blank = first == row.npos;
        auto indent = columns(row, blank ? row.size() : first);
        if (!state.marker && !blank && state.blank && indent < state.listBase) state.listBase = state.listIndent = 0;
        // Four extra columns inside a list item start code, rather than a child item.
        auto relative = indent - (state.listIndent && indent >= state.listIndent ? state.listIndent : state.listBase && indent >= state.listBase ? state.listBase
                                                                                                                                                 : 0);
        bool delimiter = !blank && relative <= 3 && (row[first] == '`' || row[first] == '~');
        if (delimiter) {
            auto last = row.find_first_not_of(row[first], first);
            auto count = (last == row.npos ? row.size() : last) - first;
            if (!state.marker && count >= 3) {
                state.marker = row[first];
                state.length = count;
                if (indent < state.listBase) state.listBase = state.listIndent = 0;
            } else if (state.marker == row[first] && count >= state.length && (last == row.npos || row.find_first_not_of(" \t\r", last) == row.npos)) {
                state.marker = 0;
                state.length = 0;
            }
        } else if (!state.marker && !blank && relative <= 3) {
            auto content = listContent(row, first);
            if (content != row.npos) {
                auto next = columns(row, content);
                if (!state.listBase || indent < state.listBase) state.listBase = next;
                state.listIndent = next;
            }
            auto marker = task(row);
            if (marker != row.npos) {
                ++chunk.total;
                chunk.completed += row[marker + 1] != ' ';
            }
        }
        state.blank = blank;
        at = stop < end ? stop + 1 : end;
        chunk.end = at;
        chunk.state = state;
        if (at - chunk.start >= 65536 || at == end) {
            result.push_back(chunk);
            chunk = {at, at, 0, 0, state};
        }
    }
    return result;
}

void DocumentWidgets::update(const Document& document)
{
    if (revision == document.revision()) return;
    size_t first = 0, end = 0;
    auto change = document.changedSince(revision, first, end);
    std::string_view text = document.text();
    if (change == ChangeStatus::Untracked || chunks.empty()) {
        chunks = scan(text, 0, text.size(), {});
        total = completed = 0;
        for (const auto& chunk : chunks) {
            total += chunk.total;
            completed += chunk.completed;
        }
    } else {
        auto shift = ptrdiff_t(text.size()) - ptrdiff_t(sourceSize);
        auto oldEnd = size_t(ptrdiff_t(end) - shift);
        auto block = [&](size_t offset) {
            return size_t(std::upper_bound(chunks.begin(), chunks.end(), offset, [](size_t value, const auto& chunk) { return value < chunk.start; }) - chunks.begin() - 1);
        };
        auto begin = block(first), last = block(oldEnd);
        auto start = chunks[begin].start;
        auto state = begin ? chunks[begin - 1].state : ScanState{};
        std::vector<Chunk> replacement;
        for (;;) {
            auto stop = size_t(ptrdiff_t(chunks[last].end) + shift);
            replacement = scan(text, start, stop, state);
            auto after = replacement.empty() ? state : replacement.back().state;
            if (last + 1 == chunks.size() || after == chunks[last].state) break;
            last = std::min(chunks.size() - 1, last + std::max(size_t(1), last - begin + 1));
        }
        for (size_t i = begin; i <= last; ++i) {
            total -= chunks[i].total;
            completed -= chunks[i].completed;
        }
        for (const auto& chunk : replacement) {
            total += chunk.total;
            completed += chunk.completed;
        }
        for (size_t i = last + 1; i < chunks.size(); ++i) {
            chunks[i].start += shift;
            chunks[i].end += shift;
        }
        auto at = chunks.erase(chunks.begin() + begin, chunks.begin() + last + 1);
        chunks.insert(at, replacement.begin(), replacement.end());
    }
    revision = document.revision();
    sourceSize = text.size();
}

}  // namespace tn
