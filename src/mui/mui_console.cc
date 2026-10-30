#include "mui/mui_dash.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_settings.h"
#include "mui/mui_skin.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <string>

#include "game/combat.h"
#include "game/combat_defs.h"
#include "game/critter.h"
#include "game/intface.h"
#include "game/item.h"
#include "game/map.h"
#include "game/object.h"
#include "game/proto_types.h"
#include "game/scripts.h"
#include "game/stat.h"
#include "game/stat_defs.h"
#include "mobile_contracts.h"
#include "mui/mui_screens.h"
#include "plib/gnw/mouse_ctl.h"
#include "plib/gnw/svga.h"

namespace fallout {

// The bottom screen's console (lane 3a). Nothing here touches the engine's
// state: taps are queued as MobileCommands (mobile_contracts.h) and the game's
// own input loops carry them out, with the checks of the keys they stand for.
// What it reads (HP, AP, the active hand) is only read.

namespace {

    constexpr const char* kMonths[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

    // Console state mirrored to the snapshot: a new generation whenever what
    // the buttons may do changes, so taps meant for an older state are dropped.
    uint64_t gGeneration = 1;
    bool gLastLive = false;
    bool gLastCombat = false;
    bool gLastTurn = false;

    struct State {
        bool live;
        bool inCombat;
        bool playerTurn;
    };

    State publishState()
    {
        State state;
        state.live = mobileDispatchLive();
        state.inCombat = isInCombat();
        state.playerTurn = state.inCombat && obj_dude != nullptr && combat_whose_turn() == obj_dude;
        if (state.live != gLastLive || state.inCombat != gLastCombat || state.playerTurn != gLastTurn) {
            gGeneration++;
            gLastLive = state.live;
            gLastCombat = state.inCombat;
            gLastTurn = state.playerTurn;
        }

        MobileSnapshot snapshot;
        snapshot.generation = gGeneration;
        snapshot.screen = MobileScreen::Classic;
        snapshot.inCombat = state.inCombat;
        snapshot.playerTurn = state.playerTurn;
        snapshot.hitPoints = obj_dude != nullptr ? critter_get_hits(obj_dude) : 0;
        snapshot.actionPoints = obj_dude != nullptr ? obj_dude->data.critter.combat.ap : 0;
        mobilePublishSnapshot(std::move(snapshot));
        // Nobody is listening: nothing may wait in the queue for later.
        if (!state.live) {
            mobileClearCommands();
        }
        return state;
    }

    void send(MobileCommandType type, int id = -1, int value = 0)
    {
        MobileCommand command;
        command.generation = mobileSnapshot().generation;
        command.type = type;
        command.id = id;
        command.value = value;
        mobileQueueCommand(command);
    }

    std::u32string u(const char* text)
    {
        return muiDecodeUtf8(text);
    }

    // A button that is there but can't be used now: dimmed, takes no touch.
    bool consoleButton(MuiContext& ui, const std::string& id, const MuiRect& rect, const char* label, bool enabled, MuiButtonStyle style = MuiButtonStyle::Normal)
    {
        if (enabled) {
            return ui.button(id, rect, u(label), style);
        }
        ui.mark(id, rect);
        muiSkinButton(ui, rect, false, false);
        muiGameButtonLabel(ui, rect, u(label), muiRgb(0x6A5F2A));
        muiFillRoundRect(rect, ui.dp(3.0f), muiRgb(0x000000, 150));
        return false;
    }

    // [count] equal buttons in a row over [row].
    MuiRect cell(const MuiRect& row, int index, int count, float gap)
    {
        float width = (row.w - gap * (count - 1)) / count;
        return { row.x + (width + gap) * index, row.y, width, row.h };
    }

    struct HandInfo {
        bool valid = false;
        bool right = false;
        std::u32string item;
        std::u32string detail;
        const char* verb = "USE";
        bool usable = false;
    };

    HandInfo readHand(bool inCombat, bool playerTurn)
    {
        HandInfo hand;
        int action;
        bool isWeapon;
        bool isDisabled;
        if (obj_dude == nullptr || intface_get_current_state(&action, &isWeapon, &isDisabled) != 0) {
            return hand;
        }
        hand.valid = true;
        hand.right = intface_is_item_right_hand() == HAND_RIGHT;

        Object* item = nullptr;
        intface_get_current_item(&item);
        hand.item = item != nullptr ? muiDecodeGameText(item_name(item)) : u("UNARMED");

        char detail[96] = "";
        int hitMode = 0;
        bool aiming = false;
        int cost = -1;
        const char* mode = nullptr;
        if (action == INTERFACE_ITEM_ACTION_RELOAD) {
            hand.verb = "RELOAD";
            mode = "RELOAD";
            cost = item_mp_cost(obj_dude, hand.right ? HIT_MODE_RIGHT_WEAPON_RELOAD : HIT_MODE_LEFT_WEAPON_RELOAD, false);
        } else if (action == INTERFACE_ITEM_ACTION_USE) {
            hand.verb = "USE";
            mode = "ITEM";
        } else if (intface_get_attack(&hitMode, &aiming) == 0) {
            hand.verb = "ATTACK";
            switch (hitMode) {
            case HIT_MODE_LEFT_WEAPON_PRIMARY:
            case HIT_MODE_RIGHT_WEAPON_PRIMARY:
                mode = "PRIMARY";
                break;
            case HIT_MODE_LEFT_WEAPON_SECONDARY:
            case HIT_MODE_RIGHT_WEAPON_SECONDARY:
                mode = "SECONDARY";
                break;
            default:
                mode = "UNARMED";
                break;
            }
            cost = item_mp_cost(obj_dude, hitMode, aiming);
        }
        int len = snprintf(detail, sizeof(detail), "%s%s", mode != nullptr ? mode : "", aiming ? " AIMED" : "");
        if (cost >= 0) {
            len += snprintf(detail + len, sizeof(detail) - len, " \xC2\xB7 %d AP", cost);
        }
        if (item != nullptr && item_get_type(item) == ITEM_TYPE_WEAPON) {
            int maximum = item_w_max_ammo(item);
            if (maximum > 0) {
                snprintf(detail + len, sizeof(detail) - len, " \xC2\xB7 %d/%d", item_w_curr_ammo(item), maximum);
            }
        }
        hand.detail = muiDecodeUtf8(detail);
        // Same as the interface bar's item button: not while the enemy plays.
        hand.usable = !isDisabled && (!inCombat || playerTurn);
        return hand;
    }

    void statusCard(MuiContext& ui, const MuiRect& card, bool pipboyEnabled)
    {
        const MuiTheme& theme = muiTheme();
        bool pressed = false;
        bool tapped = pipboyEnabled ? ui.touchable("console.status", card, &pressed) : false;
        if (!pipboyEnabled) {
            ui.mark("console.status", card);
        }
        if (tapped) {
            send(MobileCommandType::OpenScreen, static_cast<int>(MobileScreen::Pipboy));
        }

        float bezel = ui.dp(10.0f);
        muiSkinPlate(ui, card, true);
        MuiRect well = card.inset(bezel);
        muiSkinWell(ui, well);
        if (pressed) {
            muiFillRect(well, theme.accent.withAlpha(40));
        }
        muiSkinLabel(ui, card.centerX(), card.y + bezel / 2.0f, u(pipboyEnabled ? "STATUS \xC2\xB7 TAP FOR PIP-BOY" : "STATUS"));

        float pad = ui.dp(12.0f);
        MuiRect inner = well.inset(pad, ui.dp(6.0f));
        float y = inner.y + ui.dp(6.0f);

        // Place, level.
        std::u32string place;
        int mapNumber = map_get_index_number();
        if (mapNumber >= 0) {
            place = muiDecodeGameText(map_get_short_name(mapNumber));
        }
        if (place.empty()) {
            place = u("Wasteland");
        }
        float half = inner.w * 0.5f;
        MuiRect placeRect = { inner.x, y, half, ui.dp(22.0f) };
        muiDrawTextAligned(place, placeRect, muiFitTextSize(place, half, ui.dp(16.0f), ui.dp(10.0f)), theme.accent, MuiAlign::Start, MuiAlign::Center);
        std::u32string who = muiDecodeGameText(critter_name(obj_dude)) + u(" \xC2\xB7 LVL ") + muiDecodeUtf8(std::to_string(stat_pc_get(PC_STAT_LEVEL)).c_str());
        MuiRect whoRect = { inner.x + half, y, half, ui.dp(22.0f) };
        muiDrawTextAligned(who, whoRect, muiFitTextSize(who, half, ui.dp(15.0f), ui.dp(10.0f)), theme.text, MuiAlign::End, MuiAlign::Center);
        y += ui.dp(26.0f);

        // Clock and date left, the Geiger counter right.
        int hour = game_time_hour();
        char clock[16];
        snprintf(clock, sizeof(clock), "%02d:%02d", hour / 100, hour % 100);
        MuiRect clockRect = { inner.x, y, half, ui.dp(48.0f) };
        muiDrawTextAligned(muiDecodeUtf8(clock), clockRect, ui.dp(46.0f), theme.text, MuiAlign::Start, MuiAlign::Center);
        int month;
        int day;
        int year;
        game_time_date(&month, &day, &year);
        char date[32];
        snprintf(date, sizeof(date), "%d %s %d", day, kMonths[std::clamp(month, 1, 12) - 1], year);
        MuiRect dateRect = { inner.x, y + ui.dp(48.0f), half, ui.dp(20.0f) };
        muiDrawTextAligned(muiDecodeUtf8(date), dateRect, ui.dp(15.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);

        float radius = ui.dp(40.0f);
        muiDashRadGauge(ui, inner.x + half + half * 0.5f, y + radius + ui.dp(2.0f), radius, static_cast<float>(critter_get_rads(obj_dude)));

        // HP along the bottom.
        muiDashHitPointsBar(ui, { inner.x, inner.bottom() - ui.dp(26.0f), inner.w, ui.dp(24.0f) });
    }

    void weaponRow(MuiContext& ui, const MuiRect& row, const State& state)
    {
        const MuiTheme& theme = muiTheme();
        float gap = ui.dp(10.0f);
        float side = ui.dp(104.0f);
        MuiRect card = { row.x, row.y, row.w - (side + gap) * 2.0f, row.h };
        HandInfo hand = readHand(state.inCombat, state.playerTurn);

        bool enabled = state.live && hand.valid && hand.usable;
        bool pressed = false;
        bool tapped = enabled ? ui.touchable("console.use", card, &pressed) : false;
        if (!enabled) {
            ui.mark("console.use", card);
        }
        if (tapped) {
            send(MobileCommandType::UseItem, -1);
        }
        muiSkinPlate(ui, card, false);
        MuiRect well = card.inset(ui.dp(7.0f));
        muiSkinWell(ui, well);
        if (pressed) {
            muiFillRect(well, theme.accent.withAlpha(50));
        }
        MuiColor dim = enabled ? theme.text : theme.textDim;
        if (hand.valid) {
            MuiRect text = well.inset(ui.dp(10.0f), ui.dp(4.0f));
            float verbWidth = ui.dp(86.0f);
            MuiRect left = { text.x, text.y, text.w - verbWidth, text.h };
            float line = left.h / 3.0f;
            muiDrawTextAligned(u(hand.right ? "RIGHT HAND" : "LEFT HAND"), { left.x, left.y, left.w, line }, ui.dp(12.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
            muiDrawTextAligned(hand.item, { left.x, left.y + line, left.w, line }, muiFitTextSize(hand.item, left.w, ui.dp(18.0f), ui.dp(10.0f)), theme.accent, MuiAlign::Start, MuiAlign::Center);
            muiDrawTextAligned(hand.detail, { left.x, left.y + line * 2.0f, left.w, line }, muiFitTextSize(hand.detail, left.w, ui.dp(13.0f), ui.dp(10.0f)), dim, MuiAlign::Start, MuiAlign::Center);
            MuiRect verb = { text.right() - verbWidth, text.y + ui.dp(6.0f), verbWidth, text.h - ui.dp(12.0f) };
            muiStrokeRoundRect(verb, ui.dp(4.0f), ui.dp(1.5f), enabled ? theme.accent : theme.textDim.withAlpha(120));
            muiDrawTextAligned(u(hand.verb), verb, muiFitTextSize(u(hand.verb), verb.w - ui.dp(8.0f), ui.dp(15.0f), ui.dp(10.0f)), dim, MuiAlign::Center, MuiAlign::Center);
        }

        MuiRect swap = { card.right() + gap, row.y, side, row.h };
        MuiRect mode = { swap.right() + gap, row.y, side, row.h };
        bool handsOk = state.live && hand.valid && (!state.inCombat || state.playerTurn);
        if (consoleButton(ui, "console.swap", swap, "SWAP", handsOk)) {
            send(MobileCommandType::SwapHand);
        }
        if (consoleButton(ui, "console.mode", mode, "MODE", handsOk)) {
            send(MobileCommandType::CycleAttackMode);
        }
    }

    void combatRow(MuiContext& ui, const MuiRect& row, const State& state)
    {
        const MuiTheme& theme = muiTheme();
        float gap = ui.dp(10.0f);
        float barHeight = ui.dp(26.0f);
        MuiRect bar = { row.x, row.y, row.w, barHeight };
        MuiRect buttons = { row.x, row.y + barHeight + gap, row.w, row.h - barHeight - gap };

        // AP: a pip for each point, the free moves after them.
        int maximum = obj_dude != nullptr ? stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS) : 0;
        int current = obj_dude != nullptr ? std::max(obj_dude->data.critter.combat.ap, 0) : 0;
        muiFillRoundRect(bar, ui.dp(4.0f), muiRgb(0x0A1008, 230));
        muiStrokeRoundRect(bar, ui.dp(4.0f), ui.dp(1.0f), theme.panelBorder);
        char label[40];
        if (state.inCombat && maximum > 0) {
            MuiRect pips = bar.inset(ui.dp(5.0f));
            float pipGap = ui.dp(3.0f);
            int count = std::max(maximum, current);
            float pipWidth = (pips.w - pipGap * (count - 1)) / count;
            for (int index = 0; index < count; index++) {
                MuiRect pip = { pips.x + (pipWidth + pipGap) * index, pips.y, pipWidth, pips.h };
                bool on = index < current;
                muiFillRect(pip, on ? (state.playerTurn ? theme.accent : theme.textDim) : theme.well);
            }
            snprintf(label, sizeof(label), "AP %d/%d", current, maximum);
        } else {
            snprintf(label, sizeof(label), "AP \xE2\x80\x94  NO COMBAT");
        }
        // The count over the pips, with a dark outline-ish plate for legibility.
        std::u32string text = muiDecodeUtf8(label);
        float textSize = ui.dp(15.0f);
        float textWidth = muiTextWidth(text, textSize) + ui.dp(16.0f);
        MuiRect plate = { bar.centerX() - textWidth / 2.0f, bar.y + ui.dp(2.0f), textWidth, bar.h - ui.dp(4.0f) };
        muiFillRoundRect(plate, ui.dp(4.0f), muiRgb(0x000000, 170));
        muiDrawTextAligned(text, plate, textSize, theme.text, MuiAlign::Center, MuiAlign::Center);

        bool turn = state.live && state.playerTurn;
        if (consoleButton(ui, "console.endturn", cell(buttons, 0, 2, gap), "END TURN", turn, MuiButtonStyle::Primary)) {
            send(MobileCommandType::EndTurn);
        }
        if (consoleButton(ui, "console.endcombat", cell(buttons, 1, 2, gap), "END COMBAT", turn)) {
            send(MobileCommandType::EndCombat);
        }
    }

} // namespace

// Touchpad page (the TOUCHPAD button): the bottom screen as a trackpad for the
// game's cursor. The feel is the SpongeBob port's: a drag moves the cursor
// relatively with acceleration (slow = precise, fast = across the screen), a
// quick tap is a left click, press and hold without moving then drag = the
// left button held (carry an item), a two-finger tap is a right click.
namespace {

    // The pad is 1160 "layout px" wide in the numbers of the reference port.
    constexpr float kPadLayoutWidth = 1160.0f;
    constexpr float kPadSlop = 22.0f;
    constexpr unsigned int kTapMs = 320;
    constexpr unsigned int kHoldMs = 380;
    // A two-finger tap may take a little longer to land both fingers.
    constexpr unsigned int kTwoTapMs = 450;

    bool gPadOpen = false;

    struct PadTouch {
        bool on = false;
        bool slopLeft = false;
        bool held = false;
        bool two = false;
        float startX = 0.0f;
        float startY = 0.0f;
        float lastX = 0.0f;
        float lastY = 0.0f;
        unsigned int startT = 0;
        unsigned int lastT = 0;
        float speed = 0.0f;
    };
    PadTouch gPad;

    void padReset()
    {
        if (gPad.held) {
            mouseCtlPadHold(false);
        }
        gPad = PadTouch();
    }

    void padPage(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        bool back = false;
        MuiRect content = muiScreenFrame(ui, U"TOUCHPAD", &back);
        if (back) {
            padReset();
            gPadOpen = false;
            return;
        }

        float hintHeight = ui.dp(54.0f);
        MuiRect area = { content.x, content.y, content.w, content.h - hintHeight - ui.dp(8.0f) };
        MuiRect hint = { content.x, area.bottom() + ui.dp(8.0f), content.w, hintHeight };

        ui.panel(area);
        bool pressed = false;
        ui.touchable("pad.area", area, &pressed);

        float unit = area.w / kPadLayoutWidth;
        float scale = static_cast<float>(screenGetWidth()) / 640.0f * static_cast<float>(settings.touch.cursor_speed) / 100.0f;
        unsigned int now = ui.now;

        if (ui.pointerPressed() && area.contains(ui.pointerStartX(), ui.pointerStartY())) {
            padReset();
            gPad.on = true;
            gPad.startX = gPad.lastX = ui.pointerStartX();
            gPad.startY = gPad.lastY = ui.pointerStartY();
            gPad.startT = gPad.lastT = now;
        }

        if (gPad.on) {
            float x = ui.pointerX();
            float y = ui.pointerY();
            if (ui.secondFingerDown()) {
                gPad.two = true;
            }
            float away = hypotf(x - gPad.startX, y - gPad.startY) / unit;

            if (!gPad.held && !gPad.slopLeft && !gPad.two && ui.pointerDown() && now - gPad.startT >= kHoldMs && away < kPadSlop) {
                gPad.held = true;
                mouseCtlPadHold(true);
            }

            if (!gPad.slopLeft && away >= kPadSlop) {
                // Not a tap / a hold any more (a held button stays down); the
                // cursor starts from here, no jump by the slop.
                gPad.slopLeft = true;
                gPad.lastX = x;
                gPad.lastY = y;
                gPad.lastT = now;
            } else if ((gPad.slopLeft || gPad.held) && !gPad.two) {
                float dx = (x - gPad.lastX) / unit;
                float dy = (y - gPad.lastY) / unit;
                unsigned int dt = std::max(1u, now - gPad.lastT);
                gPad.lastX = x;
                gPad.lastY = y;
                gPad.lastT = now;
                if (dx != 0.0f || dy != 0.0f) {
                    float speed = hypotf(dx, dy) / static_cast<float>(dt);
                    gPad.speed = gPad.speed * 0.6f + speed * 0.4f;
                    float gain = 0.45f + std::min(1.8f, gPad.speed * 0.9f);
                    mouseCtlPadMove(dx * gain * scale, dy * gain * scale);
                }
            }

            if (ui.pointerReleased()) {
                unsigned int duration = now - gPad.startT;
                if (gPad.held) {
                    mouseCtlPadHold(false);
                } else if (gPad.two) {
                    if (!gPad.slopLeft && duration <= kTwoTapMs) {
                        mouseCtlPadClick(2);
                    }
                } else if (!gPad.slopLeft && duration <= kTapMs) {
                    mouseCtlPadClick(1);
                }
                gPad = PadTouch();
            }
        }

        // Nothing but the trackpad's own marks: a faint cross, the state.
        MuiRect middle = { area.x, area.centerY() - ui.dp(36.0f), area.w, ui.dp(40.0f) };
        muiDrawTextAligned(U"TOUCHPAD", middle, ui.dp(20.0f), theme.textDim, MuiAlign::Center, MuiAlign::Center);
        if (gPad.held) {
            MuiRect holding = { area.x, area.bottom() - ui.dp(48.0f), area.w, ui.dp(36.0f) };
            muiDrawTextAligned(U"HOLDING", holding, ui.dp(16.0f), theme.accent, MuiAlign::Center, MuiAlign::Center);
        }

        std::u32string line1 = u("drag = move cursor \xC2\xB7 tap = click \xC2\xB7 two-finger tap = right click");
        std::u32string line2 = u("hold then drag = carry");
        MuiRect first = { hint.x, hint.y, hint.w, hint.h / 2.0f };
        MuiRect second = { hint.x, hint.y + hint.h / 2.0f, hint.w, hint.h / 2.0f };
        float size = std::min(muiFitTextSize(line1, hint.w, ui.dp(13.0f), ui.dp(10.0f)), ui.dp(13.0f));
        muiDrawTextAligned(line1, first, size, theme.text, MuiAlign::Center, MuiAlign::Center);
        muiDrawTextAligned(line2, second, size, theme.text, MuiAlign::Center, MuiAlign::Center);
    }

} // namespace

bool muiDashConsoleActive()
{
    bool active = obj_dude != nullptr && muiIsPlaying() && !muiIsTalking();
    if (!active) {
        padReset();
    }
    return active;
}

void muiDashConsole(MuiContext& ui)
{
    State state = publishState();
    if (gPadOpen) {
        padPage(ui);
        return;
    }
    MuiRect screen = ui.screenRect();

    // Heights in dp; a screen shorter than the layout squeezes the gaps and
    // the status card, never the buttons (36 dp = 72 px at the Thor's density).
    float margin = ui.dp(20.0f);
    float gap = ui.dp(10.0f);
    float navHeight = ui.dp(60.0f);
    float weaponHeight = ui.dp(76.0f);
    float combatHeight = ui.dp(88.0f);
    float systemHeight = ui.dp(56.0f);
    float fixed = navHeight + weaponHeight + combatHeight + systemHeight + gap * 4.0f + margin * 2.0f;
    float cardHeight = std::clamp(screen.h - fixed, ui.dp(130.0f), ui.dp(172.0f));

    float width = std::min(screen.w - margin * 2.0f, ui.dp(560.0f));
    float x = screen.centerX() - width / 2.0f;
    float used = cardHeight + fixed;
    float y = std::max((screen.h - used) / 2.0f, 0.0f) + margin;

    bool pipboyEnabled = state.live && !state.inCombat;
    statusCard(ui, { x, y, width, cardHeight }, pipboyEnabled);
    y += cardHeight + gap;

    // Nav rail: the game's screens.
    MuiRect nav = { x, y, width, navHeight };
    struct Nav {
        const char* id;
        const char* label;
        MobileScreen screen;
        bool enabled;
    };
    const Nav items[] = {
        { "console.inv", "INV", MobileScreen::Inventory, state.live },
        { "console.cha", "CHA", MobileScreen::Character, state.live },
        { "console.pip", "PIP", MobileScreen::Pipboy, pipboyEnabled },
        { "console.map", "MAP", MobileScreen::Map, state.live },
    };
    for (int index = 0; index < 4; index++) {
        if (consoleButton(ui, items[index].id, cell(nav, index, 4, gap), items[index].label, items[index].enabled)) {
            send(MobileCommandType::OpenScreen, static_cast<int>(items[index].screen));
        }
    }
    y += navHeight + gap;

    weaponRow(ui, { x, y, width, weaponHeight }, state);
    y += weaponHeight + gap;

    combatRow(ui, { x, y, width, combatHeight }, state);
    y += combatHeight + gap;

    // MENU / SAVE / LOAD one share each, TOUCHPAD a share and a half.
    MuiRect system = { x, y, width, systemHeight };
    float unit = (width - gap * 4.0f) / 5.5f;
    MuiRect menuCell = { x, y, unit, systemHeight };
    MuiRect saveCell = { menuCell.right() + gap, y, unit, systemHeight };
    MuiRect loadCell = { saveCell.right() + gap, y, unit, systemHeight };
    MuiRect settingsCell = { loadCell.right() + gap, y, unit, systemHeight };
    MuiRect padCell = { settingsCell.right() + gap, y, system.right() - settingsCell.right() - gap, systemHeight };
    if (consoleButton(ui, "console.menu", menuCell, "MENU", state.live)) {
        send(MobileCommandType::Menu);
    }
    if (consoleButton(ui, "console.save", saveCell, "SAVE", state.live && !state.inCombat)) {
        send(MobileCommandType::Save);
    }
    if (consoleButton(ui, "console.load", loadCell, "LOAD", state.live && !state.inCombat)) {
        send(MobileCommandType::Load);
    }
    if (consoleButton(ui, "console.settings", settingsCell, "SETTINGS", state.live)) muiSettingsShow();
    if (consoleButton(ui, "console.pad", padCell, "TOUCHPAD", state.live)) {
        gPadOpen = true;
    }
}

} // namespace fallout
