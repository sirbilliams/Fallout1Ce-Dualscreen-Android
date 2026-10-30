#include "plib/gnw/mouse_ctl.h"

#include <math.h>
#include <stdlib.h>

#include <algorithm>
#include <deque>
#include <vector>

#include "mui/mui.h"
#include "mui/mui_settings.h"
#include "platform_compat.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/pad_actions.h"
#include "plib/gnw/svga.h"

namespace fallout {

namespace {

    constexpr int kLeft = 1;
    constexpr int kRight = 2;

    // Top screen (game pixels).
    constexpr int kTouchSlop = 10;
    // A two-finger tap must be over within this.
    constexpr unsigned int kTwoFingerTapMs = 450;

    // Right stick: SpongeBob's curve (a square past a 20 % dead zone, up to
    // 3/4 of the screen width per second).
    constexpr float kStickDead = 0.2f;
    constexpr float kStickSpeed = 0.75f;
    // R3: the cursor may move this far (game pixels) and still be a long press.
    constexpr int kR3Still = 8;

    struct Finger {
        bool down = false;
        SDL_FingerID id = 0;
        int x = 0;
        int y = 0;
        int startX = 0;
        int startY = 0;
    };

    Finger gFingers[2];
    // One touch sequence: from the first finger down to the last finger up.
    bool gSeq = false;
    bool gMoved = false;
    bool gFired = false;
    bool gTwo = false;
    bool gScrolled = false;
    unsigned int gSeqStart = 0;
    // The cursor goes here (single finger).
    bool gTarget = false;
    int gTargetX = 0;
    int gTargetY = 0;
    // Two fingers: centroid for scrolling.
    int gCenterX = 0;
    int gCenterY = 0;
    int gStartCenterX = 0;
    int gStartCenterY = 0;
    int gWheelX = 0;
    int gWheelY = 0;

    // Pad page.
    float gPadX = 0.0f;
    float gPadY = 0.0f;
    bool gPadHeld = false;

    std::deque<int> gPulses;
    bool gForceSend = false;
    int gLastButtons = 0;

    // Controllers.
    std::vector<SDL_GameController*> gPads;
    bool gTestPad = false;
    float gTestX = 0.0f;
    float gTestY = 0.0f;
    int gTestButtons = 0;
    float gStickRestX = 0.0f;
    float gStickRestY = 0.0f;
    unsigned int gLastUpdate = 0;

    bool gR3Down = false;
    bool gR3Fired = false;
    unsigned int gR3Start = 0;
    int gR3X = 0;
    int gR3Y = 0;

    int toGameX(const SDL_TouchFingerEvent* event)
    {
        return static_cast<int>(event->x * screenGetWidth());
    }

    int toGameY(const SDL_TouchFingerEvent* event)
    {
        return static_cast<int>(event->y * screenGetHeight());
    }

    Finger* findFinger(SDL_FingerID id)
    {
        for (Finger& finger : gFingers) {
            if (finger.down && finger.id == id) {
                return &finger;
            }
        }
        return nullptr;
    }

    int fingersDown()
    {
        return (gFingers[0].down ? 1 : 0) + (gFingers[1].down ? 1 : 0);
    }

    void clearSequence()
    {
        gSeq = gMoved = gFired = gTwo = gScrolled = gTarget = false;
    }

    // A stick axis -1..1 on the square curve.
    float stickCurve(float a)
    {
        a = std::clamp(a, -1.0f, 1.0f);
        if (a > kStickDead) {
            a = (a - kStickDead) / (1.0f - kStickDead);
        } else if (a < -kStickDead) {
            a = (a + kStickDead) / (1.0f - kStickDead);
        } else {
            a = 0.0f;
        }
        return a * fabsf(a);
    }

    float rawAxis(SDL_GameControllerAxis axis)
    {
        float value = 0.0f;
        for (SDL_GameController* pad : gPads) {
            float a = SDL_GameControllerGetAxis(pad, axis) / 32767.0f;
            if (fabsf(a) > fabsf(value)) {
                value = a;
            }
        }
        return value;
    }

    // Aggregate controllers, then resolve bindings to held mouse buttons.
    int padButtons()
    {
        if (gTestPad) {
            return gTestButtons;
        }
        unsigned int physical = 0;
        const SDL_GameControllerButton buttons[] = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
        for (SDL_GameController* pad : gPads) {
            for (int b = 0; b < 14; b++)
                if (SDL_GameControllerGetButton(pad, buttons[b])) physical |= 1u << b;
            if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384) physical |= 1u << 14;
            if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384) physical |= 1u << 15;
        }
        int mask = padActionsUpdate(physical);
        return mask;
    }

    void addPad(int index)
    {
        if (!SDL_IsGameController(index)) {
            return;
        }
        SDL_GameController* pad = SDL_GameControllerOpen(index);
        if (pad == nullptr) {
            return;
        }
        for (SDL_GameController* known : gPads) {
            if (known == pad) {
                // Open is reference counted.
                SDL_GameControllerClose(pad);
                return;
            }
        }
        gPads.push_back(pad);
    }

} // namespace

void mouseCtlInit()
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        return;
    }
    for (int index = 0; index < SDL_NumJoysticks(); index++) {
        addPad(index);
    }
}

void mouseCtlExit()
{
    for (SDL_GameController* pad : gPads) {
        SDL_GameControllerClose(pad);
    }
    gPads.clear();
}

void mouseCtlHandleEvent(const SDL_Event* event)
{
    if (event->type == SDL_CONTROLLERDEVICEADDED) {
        addPad(event->cdevice.which);
    } else if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
        for (size_t index = 0; index < gPads.size(); index++) {
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gPads[index])) == event->cdevice.which) {
                SDL_GameControllerClose(gPads[index]);
                gPads.erase(gPads.begin() + index);
                break;
            }
        }
    }
}

void mouseCtlFingerStart(const SDL_TouchFingerEvent* event)
{
    int x = toGameX(event);
    int y = toGameY(event);
    Finger* slot = nullptr;
    for (Finger& finger : gFingers) {
        if (!finger.down) {
            slot = &finger;
            break;
        }
    }
    if (slot == nullptr) {
        return;
    }
    bool first = fingersDown() == 0;
    *slot = { true, event->fingerId, x, y, x, y };
    if (first) {
        clearSequence();
        gSeq = true;
        gSeqStart = compat_ticks();
        gTarget = true;
        gTargetX = x;
        gTargetY = y;
    } else {
        gTwo = true;
        gTarget = false;
        gSeqStart = std::min(gSeqStart, compat_ticks());
        gCenterX = gStartCenterX = (gFingers[0].x + gFingers[1].x) / 2;
        gCenterY = gStartCenterY = (gFingers[0].y + gFingers[1].y) / 2;
    }
}

void mouseCtlFingerMove(const SDL_TouchFingerEvent* event)
{
    Finger* finger = findFinger(event->fingerId);
    if (finger == nullptr) {
        return;
    }
    finger->x = toGameX(event);
    finger->y = toGameY(event);

    if (!gTwo) {
        gTargetX = finger->x;
        gTargetY = finger->y;
        if (abs(finger->x - finger->startX) > kTouchSlop || abs(finger->y - finger->startY) > kTouchSlop) {
            gMoved = true;
        }
        return;
    }

    if (gFingers[0].down && gFingers[1].down) {
        int cx = (gFingers[0].x + gFingers[1].x) / 2;
        int cy = (gFingers[0].y + gFingers[1].y) / 2;
        if (abs(cx - gStartCenterX) > kTouchSlop || abs(cy - gStartCenterY) > kTouchSlop) {
            gScrolled = true;
        }
        if (gScrolled) {
            // As the earlier trackpad mode: the wheel follows the centroid.
            gWheelX += (gCenterX - cx) / 2;
            gWheelY += (cy - gCenterY) / 2;
            gCenterX = cx;
            gCenterY = cy;
        }
    }
}

void mouseCtlFingerEnd(const SDL_TouchFingerEvent* event)
{
    Finger* finger = findFinger(event->fingerId);
    if (finger == nullptr) {
        return;
    }
    if (!gTwo) {
        gTargetX = toGameX(event);
        gTargetY = toGameY(event);
    }
    finger->down = false;
    if (fingersDown() != 0) {
        return;
    }

    if (gSeq) {
        if (gTwo) {
            if (!gScrolled && compat_ticks() - gSeqStart <= kTwoFingerTapMs) {
                gPulses.push_back(kRight);
            }
        } else if (!gMoved && !gFired) {
            gPulses.push_back(kLeft);
        }
    }
    // The cursor still goes to the last position of this call's frame.
    gSeq = false;
    gTwo = false;
}

void mouseCtlPadMove(float dx, float dy)
{
    gPadX += dx;
    gPadY += dy;
}

void mouseCtlPadHold(bool down)
{
    gPadHeld = down;
}

void mouseCtlPadClick(int button)
{
    gPulses.push_back(button == kRight ? kRight : kLeft);
}

void mouseCtlReset()
{
    for (Finger& finger : gFingers) {
        finger.down = false;
    }
    clearSequence();
    gPulses.clear();
    gPadX = gPadY = 0.0f;
    gPadHeld = false;
    gWheelX = gWheelY = 0;
    gR3Down = false;
}

void mouseCtlTestPad(bool enabled, float rightX, float rightY, int buttons)
{
    gTestPad = enabled;
    gTestX = rightX;
    gTestY = rightY;
    gTestButtons = buttons;
}

bool mouseCtlUpdate(MouseCtlOut* out)
{
    const MuiTouchSettings& config = settings.touch;
    // Nothing going on (no finger, no pad, no controller): the clock is not
    // read (the autotest's fixed clock moves on every read).
    if (!gSeq && gPulses.empty() && !gForceSend && gPadX == 0.0f && gPadY == 0.0f && !gPadHeld && gWheelX == 0 && gWheelY == 0
        && !gR3Down && gLastButtons == 0 && !gTestPad && gPads.empty()) {
        gLastUpdate = 0;
        return false;
    }

    unsigned int now = compat_ticks();
    float dt = gLastUpdate == 0 ? 0.0f : std::min(now - gLastUpdate, 50u) / 1000.0f;
    gLastUpdate = now;

    int curX;
    int curY;
    mouse_get_position(&curX, &curY);

    float moveX = 0.0f;
    float moveY = 0.0f;
    int held = gPadHeld ? kLeft : 0;

    // Top screen: hold still = right click; the cursor follows the finger.
    if (gSeq && !gTwo && !gMoved && !gFired && config.touch_hold_ms > 0
        && now - gSeqStart >= static_cast<unsigned int>(config.touch_hold_ms)) {
        gFired = true;
        gPulses.push_back(kRight);
    }
    if (gTarget && !gTwo) {
        moveX += gTargetX - curX;
        moveY += gTargetY - curY;
    }

    // Touchpad page.
    moveX += gPadX;
    moveY += gPadY;
    gPadX = gPadY = 0.0f;

    // Buttons remain usable when stick movement is disabled.
    int buttons = padButtons();
    {
        float rx = !config.pad_cursor ? 0.0f : (gTestPad ? gTestX : rawAxis(SDL_CONTROLLER_AXIS_RIGHTX));
        float ry = !config.pad_cursor ? 0.0f : (gTestPad ? gTestY : rawAxis(SDL_CONTROLLER_AXIS_RIGHTY));
        float sx = stickCurve(rx);
        float sy = stickCurve(ry);
        if (sx == 0.0f && sy == 0.0f) {
            gStickRestX = gStickRestY = 0.0f;
        } else {
            float speed = kStickSpeed * screenGetWidth() * config.cursor_speed / 100.0f;
            gStickRestX += sx * speed * dt;
            gStickRestY += sy * speed * dt;
            int ix = static_cast<int>(gStickRestX);
            int iy = static_cast<int>(gStickRestY);
            gStickRestX -= ix;
            gStickRestY -= iy;
            moveX += ix;
            moveY += iy;
        }

        if (buttons & 1) held |= kLeft;
        if (buttons & 2) held |= kRight;
        // Giving the options UI the pad cancels an unfinished gesture.
        if (muiOptionsPadActive()) gR3Down = false;
        bool r3 = (buttons & 4) != 0;
        if (r3 && !gR3Down) {
            gR3Down = true;
            gR3Fired = false;
            gR3Start = now;
            gR3X = curX;
            gR3Y = curY;
        } else if (r3 && gR3Down && !gR3Fired && now - gR3Start >= static_cast<unsigned int>(config.long_press_ms)
            && abs(curX - gR3X) <= kR3Still && abs(curY - gR3Y) <= kR3Still) {
            // On the threshold, not on release.
            gR3Fired = true;
            gPulses.push_back(kRight);
        } else if (!r3 && gR3Down) {
            gR3Down = false;
            if (!gR3Fired) {
                gPulses.push_back(kLeft);
            }
        }
    }

    // One click at a time, with a call in between where the button is up.
    int pulse = 0;
    if (gForceSend) {
        gForceSend = false;
    } else if (!gPulses.empty()) {
        pulse = gPulses.front();
        gPulses.pop_front();
        gForceSend = true;
    }

    int dx = static_cast<int>(moveX);
    int dy = static_cast<int>(moveY);
    buttons = held | pulse;
    int wheelX = gWheelX;
    int wheelY = gWheelY;
    gWheelX = gWheelY = 0;

    if (!gSeq) {
        gTarget = false;
    }

    bool active = dx != 0 || dy != 0 || buttons != 0 || gLastButtons != 0 || wheelX != 0 || wheelY != 0 || gForceSend;
    gLastButtons = buttons;
    if (!active) {
        return false;
    }
    out->dx = dx;
    out->dy = dy;
    out->buttons = buttons;
    out->wheelX = wheelX;
    out->wheelY = wheelY;
    return true;
}

} // namespace fallout
