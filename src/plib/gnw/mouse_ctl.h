#ifndef FALLOUT_PLIB_GNW_MOUSE_CTL_H_
#define FALLOUT_PLIB_GNW_MOUSE_CTL_H_

#include <SDL.h>

namespace fallout {

// Mouse controls of the touch / gamepad port (SpongeBob EOTM port's feel):
//  - top screen, direct touch: the cursor follows the finger, a tap is a left
//    click, a finger held still is a right click, a two-finger tap is a right
//    click, two fingers dragging scroll;
//  - bottom screen touchpad page (mui_console.cc): relative moves with
//    acceleration, tap = click, hold then drag = carry (left button held);
//  - right stick moves the cursor, A = left (held), B = right (held),
//    R3 short = left click, R3 held = right click.
// `mouse_info` asks `mouseCtlUpdate` once per call; when it has nothing to
// say the real mouse state is read as before.

struct MouseCtlOut {
    int dx = 0;
    int dy = 0;
    // Bit 0 left, bit 1 right (as `mouse_simulate_input`).
    int buttons = 0;
    int wheelX = 0;
    int wheelY = 0;
};

void mouseCtlInit();
void mouseCtlExit();
// Controller added / removed.
void mouseCtlHandleEvent(const SDL_Event* event);

// Top screen fingers (game pixels from the event's normalised position).
void mouseCtlFingerStart(const SDL_TouchFingerEvent* event);
void mouseCtlFingerMove(const SDL_TouchFingerEvent* event);
void mouseCtlFingerEnd(const SDL_TouchFingerEvent* event);

// Touchpad page: a relative move in game pixels, the left button held (drag),
// a click of [button] (1 left, 2 right).
void mouseCtlPadMove(float dx, float dy);
void mouseCtlPadHold(bool down);
void mouseCtlPadClick(int button);

// Forgets everything pending (the cursor is hidden: movies, screens).
void mouseCtlReset();

// Fills [out] and returns true when the controls act this call.
bool mouseCtlUpdate(MouseCtlOut* out);

// Automated tests: a pretend controller. Stick -1..1 each, buttons as bits
// (1 A, 2 B, 4 R3). [enabled] false returns to the real controllers.
void mouseCtlTestPad(bool enabled, float rightX, float rightY, int buttons);

} // namespace fallout

#endif
