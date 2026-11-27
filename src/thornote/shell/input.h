#pragma once

#include "shell.h"
#include <SDL3/SDL.h>

namespace tn
{
KeyEvent keyEvent(const SDL_KeyboardEvent&);
// Hands an input event to the app; returns whether the frame may change.
// wheelPixels gives the pixels per unit of a precise wheel event, or 0 for wheel steps.
bool send(App&, const SDL_Event&, float windowToLogicalX, float windowToLogicalY, float wheelPixels = 0);
}  // namespace tn
