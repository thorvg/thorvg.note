#pragma once

#include <ui/ui.h>
#include <SDL3/SDL.h>
#include <array>

namespace tn
{

class Cursors
{
public:
    Cursors() = default;
    Cursors(const Cursors&) = delete;
    Cursors& operator=(const Cursors&) = delete;
    ~Cursors() { clear(); }
    void set(ui::Cursor cursor)
    {
        if (cursor == ui::Cursor::Default) {
            SDL_SetCursor(SDL_GetDefaultCursor());
            return;
        }
        constexpr SDL_SystemCursor types[] = {SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_TEXT, SDL_SYSTEM_CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_NS_RESIZE};
        auto index = size_t(cursor);
        if (!values[index]) values[index] = SDL_CreateSystemCursor(types[index]);
        SDL_SetCursor(values[index] ? values[index] : SDL_GetDefaultCursor());
    }
    void clear()
    {
        for (auto& cursor : values) {
            SDL_DestroyCursor(cursor);
            cursor = nullptr;
        }
    }

private:
    std::array<SDL_Cursor*, 4> values{};
};

}  // namespace tn
