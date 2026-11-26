#pragma once

#include "shell.h"
#include <SDL3/SDL.h>

namespace tn
{
KeyEvent keyEvent(const SDL_KeyboardEvent&);
// Hands an input event to the app; returns whether the frame may change.
bool send(App&, const SDL_Event&, float windowToLogicalX, float windowToLogicalY);
}  // namespace tn
