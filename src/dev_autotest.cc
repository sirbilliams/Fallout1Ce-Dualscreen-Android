#include "dev_autotest.h"

#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include <SDL.h>

#include "bottom_screen.h"
#include "game/combat.h"
#include "game/config.h"
#include "game/critter.h"
#include "game/editor.h"
#include "game/game.h"
#include "game/gconfig.h"
#include "game/gdialog.h"
#include "game/intface.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/loadsave.h"
#include "game/map.h"
#include "game/object.h"
#include "game/pipboy.h"
#include "game/proto.h"
#include "game/proto_types.h"
#include "game/scripts.h"
#include "game/tile.h"
#include "game/worldmap.h"
#include "mobile_contracts.h"
#include "mui/mui.h"
#include "mui/mui_screens.h"
#include "mui/mui_settings.h"
#include "platform_compat.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/mouse_ctl.h"
#include "plib/gnw/pad_actions.h"
#include "plib/gnw/svga.h"

namespace fallout {

namespace {

    // Timers that run on frames: with the fixed autotest clock the capture and
    // key presses of the classic screens must land on the same frame every run
    // (SDL timers fire on wall time).
    struct Scheduled {
        unsigned long frame;
        Uint32 (*callback)(Uint32, void*);
    };
    std::vector<Scheduled> gScheduled;
    unsigned long gFrameNumber = 0;

    void frameHook()
    {
        gFrameNumber++;
        for (size_t i = 0; i < gScheduled.size();) {
            if (gScheduled[i].frame > gFrameNumber) {
                i++;
                continue;
            }
            Scheduled item = gScheduled[i];
            Uint32 next = item.callback(0, nullptr);
            if (next == 0) {
                gScheduled.erase(gScheduled.begin() + i);
            } else {
                gScheduled[i].frame = gFrameNumber + std::max<Uint32>(1, next / 16);
                i++;
            }
        }
    }

    void schedule(Uint32 delayMs, Uint32 (*callback)(Uint32, void*))
    {
        gScheduled.push_back({ gFrameNumber + std::max<Uint32>(1, delayMs / 16), callback });
    }

    bool saveRgba(const unsigned char* pixels, int width, int height, const std::string& path)
    {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<unsigned char*>(pixels), width, height, 32, width * 4, SDL_PIXELFORMAT_ABGR8888);
        if (surface == nullptr) return false;
        bool ok = SDL_SaveBMP(surface, path.c_str()) == 0;
        SDL_FreeSurface(surface);
        return ok;
    }

    bool contracts()
    {
        MobileSnapshot snapshot;
        snapshot.generation = 1;
        mobilePublishSnapshot(snapshot);
        MobileCommand command;
        command.generation = 1;
        for (int i = 0; i < 64; ++i) {
            command.id = i;
            if (!mobileQueueCommand(command)) return false;
        }
        if (mobileQueueCommand(command)) return false;
        for (int i = 0; i < 64; ++i) {
            if (!mobileTakeCommand(&command) || command.id != i) return false;
        }
        mobileQueueCommand(command);
        snapshot.generation = 2;
        mobilePublishSnapshot(snapshot);
        if (mobileTakeCommand(&command) || mobileQueueCommand(command)) return false;
        bottomScreenSetTest(640, 480, 1.0f);
        mobileRenderBottom();
        if (!bottomScreenAvailable() || bottomScreenTestFrame() == nullptr) return false;
        bottomScreenSetTest(0, 0, 1.0f);
        if (bottomScreenAvailable() || bottomScreenTestFrame() != nullptr) return false;
        mobileRenderBottom();
        bottomScreenSetTest(480, 640, 1.0f);
        mobileRenderBottom();
        if (bottomScreenTestFrame() == nullptr) return false;
        bottomScreenSetTest(0, 0, 1.0f);
        mobilePublishSnapshot(MobileSnapshot {});
        return true;
    }

    // A tap on the bottom display ([x], [y] 0..1).
    void tapBottom(float x, float y)
    {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_FINGERDOWN;
        event.tfinger.touchId = kMobileBottomTouchId;
        event.tfinger.fingerId = kMobileBottomTouchId + 1;
        event.tfinger.x = x;
        event.tfinger.y = y;
        event.tfinger.pressure = 1.0f;
        SDL_PushEvent(&event);
        for (int i = 0; i < 4; i++) {
            get_input();
            renderPresent();
        }
        event.type = SDL_FINGERUP;
        event.tfinger.pressure = 0.0f;
        SDL_PushEvent(&event);
        for (int i = 0; i < 6; i++) {
            get_input();
            renderPresent();
        }
    }

    // Frames of the game's present path (top screens + bottom surface).
    void frames(int count)
    {
        for (int i = 0; i < count; i++) {
            get_input();
            renderPresent();
        }
    }

    bool captureScene(const std::string& dir, const std::string& name, int width, int height)
    {
        frames(40);
        std::string base = dir + "/" + name;
        // The top: the game's own frame (mui screens on the top are not part of it).
        if (SDL_SaveBMP(gSdlTextureSurface, (base + "-top.bmp").c_str()) != 0) return false;
        const unsigned char* bottom = bottomScreenTestFrame();
        if (bottom == nullptr) return false;
        return saveRgba(bottom, width, height, base + "-bottom.bmp");
    }

} // namespace

bool scenes(const char* dir, int width, int height, bool game);

namespace {

    // Classic (original) screens: a timer on SDL's thread captures the game
    // frame while the screen's own loop blocks the game thread, then sends
    // Escape (and a few more keys) to close it.
    std::string gClassicPath;
    int gClassicStep = 0;
    bool gClassicQuit = false;

    void pushKey(SDL_Scancode code)
    {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_KEYDOWN;
        event.key.state = SDL_PRESSED;
        event.key.keysym.scancode = code;
        event.key.keysym.sym = SDL_GetKeyFromScancode(code);
        SDL_PushEvent(&event);
        event.type = SDL_KEYUP;
        event.key.state = SDL_RELEASED;
        SDL_PushEvent(&event);
    }

    Uint32 classicTimer(Uint32, void*)
    {
        if (gClassicStep++ == 0) {
            SDL_SaveBMP(gSdlTextureSurface, gClassicPath.c_str());
            return 300;
        }
        if (gClassicQuit) game_user_wants_to_quit = 1;
        pushKey(SDL_SCANCODE_ESCAPE);
        if (gClassicStep > 12) return 0;
        return 400;
    }

    void classicShot(const char* dir, const char* name, int delayMs)
    {
        gClassicPath = std::string(dir) + "/" + name + "-top.bmp";
        gClassicStep = 0;
        SDL_InitSubSystem(SDL_INIT_TIMER);
        schedule(delayMs, classicTimer);
    }

    // Barter script (see devAutotestGameRun), one step per call.
    std::string gBarterDir;
    int gBarterWidth = 0;
    int gBarterHeight = 0;
    int gBarterStep = 0;

    void barterShot(const char* name)
    {
        std::string base = gBarterDir + "/" + name;
        SDL_SaveBMP(gSdlTextureSurface, (base + "-top.bmp").c_str());
        const unsigned char* bottom = bottomScreenTestFrame();
        if (bottom != nullptr) saveRgba(bottom, gBarterWidth, gBarterHeight, base + "-bottom.bmp");
    }

    void barterFinger(bool down, float x, float y)
    {
        SDL_Event event;
        SDL_zero(event);
        event.type = down ? SDL_FINGERDOWN : SDL_FINGERUP;
        event.tfinger.touchId = kMobileBottomTouchId;
        event.tfinger.fingerId = kMobileBottomTouchId + 3;
        event.tfinger.x = x;
        event.tfinger.y = y;
        event.tfinger.pressure = down ? 1.0f : 0.0f;
        SDL_PushEvent(&event);
    }

    Uint32 barterTimer(Uint32, void*)
    {
        switch (gBarterStep++) {
        case 0:
            pushKey(SDL_SCANCODE_B);
            return 800;
        case 1:
            barterShot("25-barter");
            barterFinger(true, 0.25f, 0.22f); // first of your items
            return 200;
        case 2:
            barterFinger(false, 0.25f, 0.22f);
            return 400;
        case 3:
            barterShot("25b-barter-offer");
            barterFinger(true, 0.75f, 0.22f); // first of theirs
            return 200;
        case 4:
            barterFinger(false, 0.75f, 0.22f);
            return 400;
        case 5:
            barterShot("25c-barter-ask");
            pushKey(SDL_SCANCODE_ESCAPE); // leaves the barter window
            return 800;
        default:
            game_user_wants_to_quit = 1; // and the conversation
            return 0;
        }
    }

    std::string gLiveDir;
    int gLiveWidth = 0;
    int gLiveHeight = 0;
    int gLiveStep = 0;
    std::string gLiveName;
    float gLiveX = 0.75f;
    float gLiveY = 0.945f;

    // Runs on SDL's timer thread while the classic load loop is blocking the
    // game thread: captures the touch picker, then taps its Cancel button.
    Uint32 liveTimer(Uint32, void*)
    {
        SDL_Event event;
        SDL_zero(event);
        event.tfinger.touchId = kMobileBottomTouchId;
        event.tfinger.fingerId = kMobileBottomTouchId + 2;
        event.tfinger.x = gLiveX;
        event.tfinger.y = gLiveY;
        if (gLiveStep++ == 0) {
            const unsigned char* bottom = bottomScreenTestFrame();
            if (bottom != nullptr) {
                saveRgba(bottom, gLiveWidth, gLiveHeight, gLiveDir + "/" + gLiveName + "-bottom.bmp");
            }
            event.type = SDL_FINGERDOWN;
            event.tfinger.pressure = 1.0f;
            SDL_PushEvent(&event);
            return 200;
        }
        event.type = SDL_FINGERUP;
        SDL_PushEvent(&event);
        return 0;
    }

} // namespace

namespace {
    std::string gMovieShotDir;
    int gMovieShotIndex = 0;

    Uint32 movieShotTimer(Uint32 interval, void*)
    {
        char name[32];
        snprintf(name, sizeof(name), "/movie-%03d.bmp", gMovieShotIndex++);
        if (gSdlTextureSurface != nullptr) SDL_SaveBMP(gSdlTextureSurface, (gMovieShotDir + name).c_str());
        return interval;
    }
} // namespace

// FALLOUT_AUTOTEST_MOVIE_SHOTS=<dir>: saves the top screen every second from
// startup (the intro movies play when this is set).
void devAutotestStartMovieShots()
{
    const char* dir = getenv("FALLOUT_AUTOTEST_MOVIE_SHOTS");
    if (dir == nullptr) return;
    gMovieShotDir = dir;
    SDL_InitSubSystem(SDL_INIT_TIMER);
    SDL_AddTimer(1000, movieShotTimer, nullptr);
}

bool devAutotestRun()
{
    compat_setFixedClock(true);
    compat_setFrameHook(frameHook);
    if (!contracts()) return false;

    const char* theme = getenv("FALLOUT_AUTOTEST_THEME");
    if (theme != nullptr) muiSettingsSetTheme(atoi(theme), false);
    // The options screen's own call (saves fallout.cfg): used to test that the
    // choice survives a restart.
    const char* persist = getenv("FALLOUT_AUTOTEST_PERSIST_THEME");
    if (persist != nullptr) muiSettingsSetTheme(atoi(persist), true);
    muiSettingsRefresh();
    int original = settings.touch.bottom_theme;
    muiSettingsSetTheme(original == 2 ? 0 : 2);
    Config disk;
    if (!config_init(&disk)) return false;
    int saved = -1;
    bool persisted = config_load(&disk, "fallout.cfg", false)
        && config_get_value(&disk, "touch", "bottom_theme", &saved)
        && saved == (original == 2 ? 0 : 2);
    config_exit(&disk);
    muiSettingsSetTheme(original);
    if (!persisted) return false;
    SDL_Log("autotest-settings: theme disk persistence PASS");
    const char* dir = getenv("FALLOUT_AUTOTEST_DIR");
    if (dir == nullptr) {
        const char* capture = getenv("FALLOUT_AUTOTEST_BMP");
        if (capture == nullptr) return false;
        renderPresent();
        return SDL_SaveBMP(gSdlTextureSurface, capture) == 0;
    }

    const int width = 1240;
    const int height = 1080;
    bottomScreenSetTest(width, height, 2.0f);

    if (!captureScene(dir, "00-dashboard", width, height)) return false;

    muiSettingsShow(2);
    frames(4);
    float x, y;
    bool bottom = false;
    if (!muiGetWidgetCenter("settings.row.0", &x, &y, &bottom) || !bottom) return false;
    tapBottom(x, y);
    if (settings.touch.bottom_theme == original) return false;
    muiOptionsPadButton(SDL_CONTROLLER_BUTTON_A);
    frames(3);
    if (settings.touch.bottom_theme != original) return false;
    // Shoulder tabs, focus scrolling, and Back all use the same pad router
    // as the in-game controls test.
    muiOptionsPadButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    muiOptionsPadButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    for (int i = 0; i < 15; i++)
        muiOptionsPadButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    frames(3);
    if (!muiGetWidgetCenter("settings.row.15", &x, &y, &bottom)) return false;
    muiOptionsPadButton(SDL_CONTROLLER_BUTTON_B);
    if (muiOptionsPadActive()) return false;
    SDL_Log("autotest-settings: touch theme, pad theme, tabs, scroll, back PASS");
    bool ok = scenes(dir, width, height, false);
    bottomScreenSetTest(0, 0, 1.0f);
    return ok;
}

namespace {

    // Lane 3a: the bottom console, tapped like a finger would (FALLOUT_AUTOTEST_CONSOLE=1).
    // The loop below is the game's own (get_input + game_handle_input), so the
    // queued commands are carried out as in play; a screen a button opens blocks
    // it, and a scheduled script captures it and closes it again.
    std::string gConsoleDir;
    int gConsoleWidth = 0;
    int gConsoleHeight = 0;
    std::string gConsoleName;
    bool gConsoleDone = false;
    int gConsoleStep = 0;
    // Widget to tap on the second step of a script (combat), "" none.
    std::string gConsoleThen;
    std::string gConsoleThenName;

    void consoleLoop(int count)
    {
        for (int i = 0; i < count && game_user_wants_to_quit == 0; i++) {
            int key = get_input();
            game_handle_input(key, false);
            renderPresent();
        }
    }

    void consoleShot(const std::string& name)
    {
        std::string base = gConsoleDir + "/" + name;
        SDL_SaveBMP(gSdlTextureSurface, (base + "-top.bmp").c_str());
        const unsigned char* bottom = bottomScreenTestFrame();
        if (bottom != nullptr) saveRgba(bottom, gConsoleWidth, gConsoleHeight, base + "-bottom.bmp");
    }

    void consoleFinger(bool down, float x, float y)
    {
        SDL_Event event;
        SDL_zero(event);
        event.type = down ? SDL_FINGERDOWN : SDL_FINGERUP;
        event.tfinger.touchId = kMobileBottomTouchId;
        event.tfinger.fingerId = kMobileBottomTouchId + 7;
        event.tfinger.x = x;
        event.tfinger.y = y;
        event.tfinger.pressure = down ? 1.0f : 0.0f;
        SDL_PushEvent(&event);
    }

    // Taps widget [id] without waiting (callable from a scheduled script).
    bool consoleTapNow(const char* id)
    {
        float x, y;
        bool onBottom = false;
        if (!muiGetWidgetCenter(id, &x, &y, &onBottom) || !onBottom) {
            SDL_Log("autotest: console widget %s not found", id);
            return false;
        }
        consoleFinger(true, x, y);
        return true;
    }

    // Finger up a few frames after the down.
    Uint32 consoleUpTimer(Uint32, void*);
    float gConsoleUpX = 0.0f;
    float gConsoleUpY = 0.0f;
    Uint32 consoleUpTimer(Uint32, void*)
    {
        consoleFinger(false, gConsoleUpX, gConsoleUpY);
        return 0;
    }

    bool consoleTapScripted(const char* id)
    {
        float x, y;
        bool onBottom = false;
        if (!muiGetWidgetCenter(id, &x, &y, &onBottom) || !onBottom) {
            SDL_Log("autotest: console widget %s not found", id);
            return false;
        }
        consoleFinger(true, x, y);
        gConsoleUpX = x;
        gConsoleUpY = y;
        schedule(64, consoleUpTimer);
        return true;
    }

    // Runs while a screen the tapped button opened blocks the game loop:
    // captures it, then closes it with Escape until the loop is back.
    Uint32 consoleCloseTimer(Uint32, void*)
    {
        if (gConsoleDone) return 0;
        if (gConsoleStep++ == 0) {
            consoleShot(gConsoleName);
            return 400;
        }
        if (gConsoleStep > 8) return 0;
        pushKey(SDL_SCANCODE_ESCAPE);
        return 400;
    }

    // Taps [id] from the main thread and lets the game loop run on; a blocking
    // screen is captured as [name].
    bool consoleTapShoot(const char* id, const char* name)
    {
        gConsoleName = name;
        gConsoleStep = 0;
        gConsoleDone = false;
        schedule(1800, consoleCloseTimer);
        if (!consoleTapScripted(id)) {
            gConsoleDone = true;
            return false;
        }
        consoleLoop(40);
        // Back from the screen (or nothing opened).
        gConsoleDone = true;
        consoleLoop(40);
        return true;
    }

    // Combat: a script of taps and captures while the combat loop blocks.
    int gCombatStep = 0;
    std::string gCombatButton;
    std::string gCombatName;
    Uint32 combatTimer(Uint32, void*)
    {
        switch (gCombatStep++) {
        case 0:
            consoleShot(gCombatName + "-1"); // the console in combat, player's turn
            consoleTapScripted(gCombatButton.c_str());
            return 600;
        case 1:
            consoleShot(gCombatName + "-2"); // after the button
            return 400;
        default:
            if (isInCombat()) {
                // Still in combat: finish it so the script goes on.
                pushKey(SDL_SCANCODE_RETURN);
                return 400;
            }
            return 0;
        }
    }

    bool consoleScenes(const char* dir, int width, int height)
    {
        gConsoleDir = dir;
        gConsoleWidth = width;
        gConsoleHeight = height;

        // A weapon in hand.
        Object* weapon = nullptr;
        Inventory* inventory = &obj_dude->data.inventory;
        for (int i = 0; i < inventory->length; i++) {
            Object* item = inventory->items[i].item;
            if (item_get_type(item) == ITEM_TYPE_WEAPON) {
                weapon = item;
                break;
            }
        }
        if (weapon != nullptr) {
            inven_wield(obj_dude, weapon, HAND_RIGHT);
            intface_update_items(false);
            if (intface_is_item_right_hand() != HAND_RIGHT) intface_toggle_items(false);
        }
        SDL_Log("autotest: console weapon: %s", weapon != nullptr ? item_name(weapon) : "(none)");

        consoleLoop(40);
        consoleShot("30-console");

        static const char* kButtons[][2] = {
            { "console.inv", "31-console-inv" },
            { "console.cha", "32-console-cha" },
            { "console.pip", "33-console-pip" },
            { "console.map", "34-console-map" },
            { "console.menu", "35-console-menu" },
            { "console.save", "36-console-save" },
            { "console.load", "37-console-load" },
        };
        for (const auto& button : kButtons) {
            consoleTapShoot(button[0], button[1]);
            consoleLoop(10);
        }
        // The status card opens the Pip-Boy too.
        consoleTapShoot("console.status", "38-console-status-pip");

        // Hands and attack mode (the card shows the change).
        consoleTapScripted("console.swap");
        consoleLoop(30);
        consoleShot("40-console-swapped");
        consoleTapScripted("console.swap");
        consoleLoop(30);
        consoleTapScripted("console.mode");
        consoleLoop(30);
        consoleShot("41-console-mode");

        // In combat: the weapon card starts one; END TURN, then END COMBAT.
        static const char* kCombat[][2] = {
            { "console.endturn", "50-console-endturn" },
            { "console.endcombat", "51-console-endcombat" },
        };
        for (const auto& step : kCombat) {
            gCombatStep = 0;
            gCombatButton = step[0];
            gCombatName = step[1];
            schedule(2400, combatTimer);
            consoleTapScripted("console.use");
            consoleLoop(300);
            SDL_Log("autotest: combat step %s, in combat now: %d", step[1], isInCombat() ? 1 : 0);
            consoleLoop(20);
            consoleShot(std::string(step[1]) + "-after");
        }
        return true;
    }

    // ---- bottom pages (FALLOUT_AUTOTEST_PAGES): the live map and the combat panel ----

    int gPagesStep = 0;

    Object* spawnFoe()
    {
        Object* chosen = nullptr;
        for (int pid = 0x1000001; pid < 0x1000030 && chosen == nullptr; pid++) {
            Object* critter = nullptr;
            if (obj_pid_new(&critter, pid) != 0 || critter == nullptr) continue;
            const char* name = critter_name(critter);
            SDL_Log("autotest: critter pid %x: %s", pid, name != nullptr ? name : "?");
            if (name != nullptr && (strstr(name, "Rat") != nullptr || strstr(name, "corpion") != nullptr || strstr(name, "Dog") != nullptr)) {
                chosen = critter;
            } else {
                obj_erase_object(critter, nullptr);
            }
        }
        if (chosen == nullptr) return nullptr;
        int tile = tile_num_in_direction(obj_dude->tile, 2, 4);
        if (obj_connect(chosen, tile, obj_dude->elevation, nullptr) != 0) return nullptr;
        chosen->data.critter.combat.team = 99;
        return chosen;
    }

    Object* gFoe = nullptr;

    Uint32 pagesCombatTimer(Uint32, void*)
    {
        SDL_Log("autotest: panel step %d: dude ap %d hp %d, foe hp %d, turn %d", gPagesStep, obj_dude->data.critter.combat.ap, critter_get_hits(obj_dude), gFoe != nullptr ? critter_get_hits(gFoe) : -1, combat_whose_turn() == obj_dude ? 1 : 0);
        switch (gPagesStep++) {
        case 0:
            consoleShot("71-combat");
            return 500;
        case 1:
            consoleShot("71b-combat-your-turn");
            consoleTapScripted("combat.aim.0");
            return 500;
        case 2:
            consoleShot("72-combat-aim");
            return 300;
        case 3:
            consoleTapScripted("combat.part.0");
            return 2500;
        case 4:
            consoleShot("73-combat-after-aimed");
            consoleTapScripted("combat.aimback");
            consoleTapScripted("combat.target.0");
            return 2500;
        case 5:
            consoleShot("73b-combat-after-attack");
            return 300;
        case 6:
            consoleTapScripted("combat.mode.1");
            return 400;
        case 7:
            consoleShot("74-combat-mode");
            return 300;
        default:
            if (isInCombat()) {
                // END COMBAT on the panel (disabled in the enemy's turn).
                consoleTapScripted("combat.endcombat");
                return 400;
            }
            return 0;
        }
    }

    bool pagesScenes(const char* dir, int width, int height)
    {
        gConsoleDir = dir;
        gConsoleWidth = width;
        gConsoleHeight = height;

        Object* weapon = nullptr;
        Inventory* inventory = &obj_dude->data.inventory;
        for (int i = 0; i < inventory->length; i++) {
            Object* item = inventory->items[i].item;
            if (item_get_type(item) == ITEM_TYPE_WEAPON) {
                weapon = item;
                break;
            }
        }
        // Unarmed: the test character's art has no knife swing here, so a weapon would not attack.
        (void)weapon;

        consoleLoop(40);
        const char* only = getenv("FALLOUT_AUTOTEST_PAGES");
        bool wantMap = strcmp(only, "combat") != 0;
        bool wantCombat = strcmp(only, "map") != 0;
        // The live map: open, legend, center, a tap on open ground walks.
        if (wantMap) {
            consoleTapScripted("console.map");
            consoleLoop(30);
            consoleShot("60-livemap");
            consoleTapScripted("map.legend");
            consoleLoop(20);
            consoleShot("61-livemap-legend");
            int before = obj_dude->tile;
            float x, y;
            bool onBottom = false;
            if (muiGetWidgetCenter("map.view", &x, &y, &onBottom)) {
                consoleFinger(true, x + 0.12f, y + 0.08f);
                consoleLoop(3);
                consoleFinger(false, x + 0.12f, y + 0.08f);
                consoleLoop(120);
                SDL_Log("autotest: live map tap walk: tile %d -> %d", before, obj_dude->tile);
            } else {
                SDL_Log("autotest: map.view not found");
            }
            consoleShot("62-livemap-walked");
            consoleTapScripted("map.center");
            consoleLoop(20);
            consoleTapScripted("frame.back");
            consoleLoop(20);
            consoleShot("63-console-after-map");
        }
        if (!wantCombat) return true;

        // The panel: a foe, combat starts with its attack, dude's turn follows.
        Object* foe = spawnFoe();
        if (foe == nullptr) {
            SDL_Log("autotest: no foe could be made");
            return false;
        }
        STRUCT_664980 attack;
        memset(&attack, 0, sizeof(attack));
        attack.attacker = foe;
        attack.defender = obj_dude;
        gFoe = foe;
        gPagesStep = 0;
        schedule(1500, pagesCombatTimer);
        combat(&attack);
        SDL_Log("autotest: combat returned, in combat now: %d", isInCombat() ? 1 : 0);
        consoleLoop(40);
        consoleShot("75-after-combat");
        return true;
    }

    // ---- mouse controls (FALLOUT_AUTOTEST_MOUSE) -------------------------

    int gMouseL = 0;
    int gMouseR = 0;
    int gMouseLeftDown = 0;
    std::string gMouseDir;

    // A frame of the game's loop without its actions (clicks must not walk the
    // player), counting button edges as the game sees them.
    void mouseFrame(int count = 1)
    {
        for (int i = 0; i < count; i++) {
            get_input();
            mobilePollKey(false);
            int buttons = mouse_get_buttons();
            if (buttons & MOUSE_EVENT_LEFT_BUTTON_DOWN) gMouseL++;
            if (buttons & MOUSE_EVENT_RIGHT_BUTTON_DOWN) gMouseR++;
            gMouseLeftDown = (buttons & (MOUSE_EVENT_LEFT_BUTTON_DOWN | MOUSE_EVENT_LEFT_BUTTON_REPEAT)) ? 1 : 0;
            renderPresent();
        }
    }

    void mouseFinger(bool bottom, int type, int id, float x, float y)
    {
        SDL_Event event;
        SDL_zero(event);
        event.type = type;
        event.tfinger.touchId = bottom ? kMobileBottomTouchId : 1;
        event.tfinger.fingerId = (bottom ? kMobileBottomTouchId : 1000) + id;
        event.tfinger.x = x;
        event.tfinger.y = y;
        event.tfinger.pressure = type == SDL_FINGERUP ? 0.0f : 1.0f;
        SDL_PushEvent(&event);
    }

    void mouseShot(const char* name)
    {
        std::string base = gMouseDir + "/" + name;
        SDL_SaveBMP(gSdlTextureSurface, (base + "-top.bmp").c_str());
        const unsigned char* bottom = bottomScreenTestFrame();
        if (bottom != nullptr) saveRgba(bottom, gConsoleWidth, gConsoleHeight, base + "-bottom.bmp");
    }

    void mouseReport(const char* what)
    {
        int x, y;
        mouse_get_position(&x, &y);
        SDL_Log("autotest-mouse: t=%u %-28s cursor=(%d,%d) L=%d R=%d leftHeld=%d", compat_ticks(), what, x, y, gMouseL, gMouseR, gMouseLeftDown);
    }

    bool padKeyboardTest()
    {
        kb_clear();
        SDL_Event event = {};
        event.type = SDL_CONTROLLERBUTTONDOWN;
        event.cbutton.which = -999;
        event.cbutton.button = SDL_CONTROLLER_BUTTON_X;
        SDL_PushEvent(&event);
        SDL_PushEvent(&event); // A repeated down must not post another shortcut.
        event.type = SDL_CONTROLLERBUTTONUP;
        SDL_PushEvent(&event);
        GNW95_process_message();
        GNW95_process_message();
        if (kb_getch() != KEY_LOWERCASE_S || kb_getch() != -1) return false;
        event = {};
        event.type = SDL_CONTROLLERAXISMOTION;
        event.caxis.which = -999;
        event.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
        event.caxis.value = 20000;
        SDL_PushEvent(&event);
        event.caxis.value = 24000;
        SDL_PushEvent(&event); // Still held above half travel.
        event.caxis.value = 0;
        SDL_PushEvent(&event);
        event.caxis.value = 20000;
        SDL_PushEvent(&event); // Released then pressed again.
        event.caxis.value = 0;
        SDL_PushEvent(&event);
        GNW95_process_message();
        GNW95_process_message();
        if (kb_getch() != KEY_LOWERCASE_B || kb_getch() != KEY_LOWERCASE_B || kb_getch() != -1) return false;
        kb_disable();
        event = {};
        event.type = SDL_CONTROLLERBUTTONDOWN;
        event.cbutton.which = -999;
        event.cbutton.button = SDL_CONTROLLER_BUTTON_X;
        SDL_PushEvent(&event);
        event.type = SDL_CONTROLLERBUTTONUP;
        SDL_PushEvent(&event);
        GNW95_process_message();
        GNW95_process_message();
        kb_enable();
        if (kb_getch() != -1) return false;
        SDL_Log("autotest-pad: native keyboard queue, trigger rearm, disabled input PASS");
        return true;
    }

    bool mouseScenes(const char* dir, int width, int height)
    {
        int bindings[kPadButtonCount];
        int restored[kPadButtonCount];
        padParseBindings("A=none; rs=inventory bogus=click,x=bad y=context a=click lt=NONE", bindings);
        std::string overrides = padSerializeBindings(bindings);
        if (overrides != "y=context,rs=inventory,lt=none") return false;
        padParseBindings(overrides, restored);
        for (int b = 0; b < kPadButtonCount; b++)
            if (bindings[b] != restored[b]) return false;
        padParseBindings("", bindings);
        if (!padSerializeBindings(bindings).empty()) return false;
        SDL_Log("autotest-pad: override round trip PASS");
        if (!padKeyboardTest()) return false;
        if (padActionsUpdate(1u << 0) != 1 || padActionsUpdate(1u << 1) != 2 || padActionsUpdate(1u << 9) != 4) return false;
        gMouseDir = dir;
        gConsoleWidth = width;
        gConsoleHeight = height;
        mouse_show();
        mouseFrame(40);
        mouseReport("start");
        mouseShot("60-mouse-console");

        // --- Top screen, direct touch.
        mouseFinger(false, SDL_FINGERDOWN, 1, 0.5f, 0.4f);
        mouseFrame(2);
        mouseShot("61-touch-follow");
        mouseReport("finger down (hover)");
        mouseFinger(false, SDL_FINGERMOTION, 1, 0.3f, 0.3f);
        mouseFrame(2);
        mouseReport("finger moved");
        mouseFinger(false, SDL_FINGERUP, 1, 0.3f, 0.3f);
        mouseFrame(1);
        mouseReport("tap = left click (L+1)");
        mouseFrame(3);
        // Hold still = right click on the threshold, nothing on release.
        int l0 = gMouseL;
        mouseFinger(false, SDL_FINGERDOWN, 1, 0.6f, 0.5f);
        mouseFrame(30);
        mouseReport("hold ~30 frames (no click yet)");
        mouseFrame(15);
        mouseReport("hold ~45 frames (R+1)");
        mouseFinger(false, SDL_FINGERUP, 1, 0.6f, 0.5f);
        mouseFrame(4);
        SDL_Log("autotest-mouse: long press left clicks: %d (want 0)", gMouseL - l0);
        // Two-finger tap = right click.
        mouseFinger(false, SDL_FINGERDOWN, 1, 0.45f, 0.5f);
        mouseFinger(false, SDL_FINGERDOWN, 2, 0.55f, 0.5f);
        mouseFrame(3);
        mouseFinger(false, SDL_FINGERUP, 1, 0.45f, 0.5f);
        mouseFinger(false, SDL_FINGERUP, 2, 0.55f, 0.5f);
        mouseFrame(4);
        mouseReport("two-finger tap (R+1)");

        // --- Right stick, A, B, R3 (a pretend controller).
        mouseFinger(false, SDL_FINGERDOWN, 1, 0.2f, 0.5f);
        mouseFrame(1);
        mouseFinger(false, SDL_FINGERMOTION, 1, 0.2f, 0.5f);
        mouseFinger(false, SDL_FINGERUP, 1, 0.2f, 0.5f);
        mouseFrame(8);
        mouseReport("cursor parked");
        mouseCtlTestPad(true, 1.0f, 0.0f, 0);
        mouseFrame(30);
        mouseReport("stick right full, 30 frames");
        mouseCtlTestPad(true, 0.0f, 0.5f, 0);
        mouseFrame(30);
        mouseReport("stick down half (curve)");
        mouseCtlTestPad(true, 0.15f, 0.15f, 0);
        mouseFrame(10);
        mouseReport("inside dead zone (no move)");
        mouseShot("62-stick-moved");
        mouseCtlTestPad(true, 0, 0, 1);
        mouseFrame(2);
        SDL_Log("autotest-mouse: A held, leftHeld=%d", gMouseLeftDown);
        mouseCtlTestPad(true, 0, 0, 0);
        mouseFrame(3);
        mouseReport("A released");
        mouseCtlTestPad(true, 0, 0, 2);
        mouseFrame(3);
        mouseCtlTestPad(true, 0, 0, 0);
        mouseFrame(3);
        mouseReport("B = right click (R+1)");
        mouseCtlTestPad(true, 0, 0, 4);
        mouseFrame(6);
        mouseCtlTestPad(true, 0, 0, 0);
        mouseFrame(4);
        mouseReport("R3 short = left click (L+1)");
        int r0 = gMouseR;
        l0 = gMouseL;
        mouseCtlTestPad(true, 0, 0, 4);
        mouseFrame(12);
        SDL_Log("autotest-mouse: R3 held ~12 frames (<400ms): R+%d (want 0)", gMouseR - r0);
        mouseFrame(30);
        SDL_Log("autotest-mouse: R3 held ~42 frames (>400ms): R+%d (want 1, fired while held)", gMouseR - r0);
        mouseCtlTestPad(true, 0, 0, 0);
        mouseFrame(4);
        SDL_Log("autotest-mouse: R3 long released: L+%d (want 0) R+%d (want 1)", gMouseL - l0, gMouseR - r0);
        mouseCtlTestPad(false, 0, 0, 0);

        // --- Touchpad page on the bottom screen.
        float bx, by;
        bool onBottom = false;
        if (!muiGetWidgetCenter("console.pad", &bx, &by, &onBottom) || !onBottom) {
            SDL_Log("autotest-mouse: TOUCHPAD button not found");
            return false;
        }
        mouseFinger(true, SDL_FINGERDOWN, 7, bx, by);
        mouseFrame(3);
        mouseFinger(true, SDL_FINGERUP, 7, bx, by);
        mouseFrame(20);
        mouseShot("63-pad-page");
        if (!muiGetWidgetCenter("pad.area", &bx, &by, &onBottom)) {
            SDL_Log("autotest-mouse: pad page not open");
            return false;
        }
        // Slow drag, then a fast one: the fast one goes farther per pad pixel.
        mouseFinger(true, SDL_FINGERDOWN, 7, bx, by);
        mouseFrame(2);
        mouseReport("pad finger down");
        float x = bx;
        for (int i = 0; i < 8; i++) {
            x += 0.006f;
            mouseFinger(true, SDL_FINGERMOTION, 7, x, by);
            mouseFrame(2);
        }
        mouseReport("pad slow drag (8 x 0.006)");
        for (int i = 0; i < 4; i++) {
            x += 0.032f;
            mouseFinger(true, SDL_FINGERMOTION, 7, x, by);
            mouseFrame(1);
        }
        mouseFinger(true, SDL_FINGERUP, 7, x, by);
        mouseFrame(4);
        mouseReport("pad fast drag (4 x 0.032)");
        mouseShot("64-pad-dragged");
        // Tap.
        mouseFinger(true, SDL_FINGERDOWN, 7, bx, by);
        mouseFrame(3);
        mouseFinger(true, SDL_FINGERUP, 7, bx, by);
        mouseFrame(4);
        mouseReport("pad tap (L+1)");
        // Hold then drag = carry.
        mouseFinger(true, SDL_FINGERDOWN, 7, bx, by);
        mouseFrame(30);
        mouseReport("pad hold 480ms (left down)");
        mouseShot("65-pad-holding");
        for (int i = 0; i < 6; i++) {
            mouseFinger(true, SDL_FINGERMOTION, 7, bx - 0.02f * (i + 1), by + 0.01f * (i + 1));
            mouseFrame(2);
        }
        mouseReport("pad carry (still held)");
        mouseFinger(true, SDL_FINGERUP, 7, bx - 0.12f, by + 0.06f);
        mouseFrame(4);
        mouseReport("pad carry dropped (left up)");
        // Two-finger tap = right click.
        mouseFinger(true, SDL_FINGERDOWN, 7, bx, by);
        mouseFinger(true, SDL_FINGERDOWN, 8, bx + 0.05f, by);
        mouseFrame(3);
        mouseFinger(true, SDL_FINGERUP, 7, bx, by);
        mouseFinger(true, SDL_FINGERUP, 8, bx + 0.05f, by);
        mouseFrame(4);
        mouseReport("pad two-finger tap (R+1)");
        // Back to the console.
        if (!muiGetWidgetCenter("frame.back", &bx, &by, &onBottom)) return false;
        mouseFinger(true, SDL_FINGERDOWN, 7, bx, by);
        mouseFrame(3);
        mouseFinger(true, SDL_FINGERUP, 7, bx, by);
        mouseFrame(20);
        mouseShot("66-pad-closed");

        // --- Controls page of the options.
        muiOptionsShow();
        mouseFrame(30);
        mouseShot("67-options");
        if (!muiGetWidgetCenter("options.controls", &bx, &by, &onBottom)) return false;
        mouseFinger(true, SDL_FINGERDOWN, 7, bx, by);
        mouseFrame(3);
        mouseFinger(true, SDL_FINGERUP, 7, bx, by);
        mouseFrame(30);
        mouseShot("68-controls");
        std::string originalBindings = settings.touch.pad_bindings;
        int originalTheme = settings.touch.bottom_theme;
        for (int i = 0; i < 6; i++)
            muiOptionsPadButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        mouseFrame(3);
        mouseShot("69-controls-pad-focus");
        int oldBinding = padBinding(1);
        muiOptionsPadButton(SDL_CONTROLLER_BUTTON_A);
        mouseFrame(3);
        if (padBinding(1) != (oldBinding + 1) % padActionCount()) return false;
        // Touch the same visible row and verify it cycles, too.
        if (!muiGetWidgetCenter("controls.binding.1", &bx, &by, &onBottom)) return false;
        mouseFinger(onBottom, SDL_FINGERDOWN, 7, bx, by);
        mouseFrame(3);
        mouseFinger(onBottom, SDL_FINGERUP, 7, bx, by);
        mouseFrame(3);
        if (padBinding(1) != (oldBinding + 2) % padActionCount()) return false;
        muiSettingsSetTheme(0, false);
        mouseFrame(10);
        mouseShot("70-controls-classic");
        // Navigate to Reset; focus follows the row through the scroll area.
        for (int i = 0; i < 15; i++)
            muiOptionsPadButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        mouseFrame(3);
        mouseShot("71-controls-reset");
        muiOptionsPadButton(SDL_CONTROLLER_BUTTON_A);
        mouseFrame(3);
        if (!settings.touch.pad_bindings.empty()) return false;
        settings.touch.pad_bindings = originalBindings;
        muiSettingsSetMouse();
        muiSettingsSetTheme(originalTheme, false);
        SDL_Log("autotest-pad: touch, pad focus, reset PASS");
        muiOptionsHide();
        mouseFrame(10);
        return true;
    }

} // namespace

bool devAutotestGameRun()
{
    const char* dir = getenv("FALLOUT_AUTOTEST_DIR");
    if (dir == nullptr) return true;
    const int width = 1240;
    const int height = 1080;
    bottomScreenSetTest(width, height, 2.0f);
    // A few real items for the inventory screens.
    static const int kPids[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 40, 41 };
    for (int pid : kPids) {
        if (getenv("FALLOUT_AUTOTEST_NOITEMS")) break;
        Object* item = nullptr;
        if (obj_pid_new(&item, pid) == 0 && item != nullptr) {
            obj_disconnect(item, nullptr);
            if (item_add_force(obj_dude, item, pid == 40 ? 3 : 1) != 0) {
                obj_erase_object(item, nullptr);
            }
        }
    }
    if (!captureScene(dir, "10-dashboard-ingame", width, height)) return false;
    if (getenv("FALLOUT_AUTOTEST_MOUSE") != nullptr) {
        bool mouseOk = mouseScenes(dir, width, height);
        bottomScreenSetTest(0, 0, 1.0f);
        return mouseOk;
    }
    if (getenv("FALLOUT_AUTOTEST_PAGES") != nullptr) {
        bool pagesOk = pagesScenes(dir, width, height);
        bottomScreenSetTest(0, 0, 1.0f);
        return pagesOk;
    }
    if (getenv("FALLOUT_AUTOTEST_CONSOLE") != nullptr) {
        bool consoleOk = consoleScenes(dir, width, height);
        bottomScreenSetTest(0, 0, 1.0f);
        return consoleOk;
    }
    if (!scenes(dir, width, height, true)) return false;

    // The real load screen: the classic loop runs with the touch picker over
    // it; a timer taps Cancel (the real save slots are listed, none loaded).
    gLiveDir = dir;
    gLiveWidth = width;
    gLiveHeight = height;
    SDL_InitSubSystem(SDL_INIT_TIMER);
    gLiveStep = 0;
    gLiveName = "05b-loadsave-live";
    gLiveX = 0.75f;
    gLiveY = 0.945f;
    schedule(2500, liveTimer);
    int rc = LoadGame(LOAD_SAVE_MODE_NORMAL);
    SDL_Log("autotest: LoadGame returned %d (0 = cancelled by touch)", rc);

    // The real Pip-Boy: the classic loop runs with the touch face over it; the
    // timer captures it and taps Back.
    gLiveStep = 0;
    gLiveName = "07b-pipboy-live";
    gLiveX = 0.9f;
    gLiveY = 0.065f;
    schedule(2500, liveTimer);
    int pipRc = pipboy(PIPBOY_OPEN_INTENT_UNSPECIFIED);
    SDL_Log("autotest: pipboy returned %d", pipRc);

    // Classic screens on the top surface (touch faces switched off).
    config_set_value(&game_config, "touch", "enabled", 0);
    classicShot(dir, "20-classic-inventory", 2000);
    handle_inventory();
    classicShot(dir, "21-classic-character-editor", 3000);
    editor_design(false);
    gClassicQuit = true; // the world map only leaves via a town or quit
    classicShot(dir, "22-classic-worldmap", 3000);
    {
        WorldMapContext ctx;
        ctx.state = 0;
        ctx.town = our_town;
        ctx.section = 0;
        world_map(ctx);
        KillWorldWin();
    }
    gClassicQuit = false;
    game_user_wants_to_quit = 0;
    classicShot(dir, "23-classic-pipboy", 2500);
    pipboy(PIPBOY_OPEN_INTENT_UNSPECIFIED);
    {
        const char* dlgMap = getenv("FALLOUT_AUTOTEST_DIALOGUE_MAP");
        static char mapName[64];
        snprintf(mapName, sizeof(mapName), "%s", dlgMap != nullptr ? dlgMap : "SHADYW.MAP");
        int mrc = map_load(mapName);
        SDL_Log("autotest: map_load(%s) = %d", mapName, mrc);
    }
    for (Object* obj = obj_find_first(); obj != nullptr; obj = obj_find_next()) {
        if (obj == obj_dude || obj->sid == -1 || PID_TYPE(obj->pid) != OBJ_TYPE_CRITTER) continue;
        SDL_Log("autotest: dialogue with pid %x", obj->pid);
        obj_move_to_tile(obj_dude, obj->tile + 2, obj->elevation, nullptr);
        classicShot(dir, "24-classic-dialogue", 4000);
        gClassicQuit = true; // Escape does not close a conversation: quit it
        gdialog_enter(obj, 0);
        gClassicQuit = false;
        game_user_wants_to_quit = 0;
        break;
    }
    config_set_value(&game_config, "touch", "enabled", 1);
    // Barter: a touch-driven trade with a real trader. A scheduled script
    // (frames, not wall time) opens the barter window from the conversation,
    // taps an item of each side over to the tables and leaves again.
    gBarterStep = 0;
    gBarterDir = dir;
    gBarterWidth = width;
    gBarterHeight = height;
    {
        static const char* kMaps[] = { "SHADYW.MAP", "SHADYE.MAP", "JUNKENT.MAP", "JUNKCSNO.MAP", "HUBENT.MAP", "HUBDWNTN.MAP", nullptr };
        const char* only = getenv("FALLOUT_AUTOTEST_BARTER_MAP");
        Object* trader = nullptr;
        for (int mapIndex = 0; kMaps[mapIndex] != nullptr && trader == nullptr; mapIndex++) {
            const char* name = only != nullptr ? only : kMaps[mapIndex];
            char mapName[64];
            snprintf(mapName, sizeof(mapName), "%s", name);
            if (map_load(mapName) != 0) {
                if (only != nullptr) break;
                continue;
            }
            for (Object* obj = obj_find_first(); obj != nullptr; obj = obj_find_next()) {
                if (obj == obj_dude || obj->sid == -1 || PID_TYPE(obj->pid) != OBJ_TYPE_CRITTER) continue;
                Proto* proto;
                if (proto_ptr(obj->pid, &proto) == -1) continue;
                SDL_Log("autotest: %s critter pid %x barter=%d", name, obj->pid, (proto->critter.data.flags & CRITTER_BARTER) != 0);
                if ((proto->critter.data.flags & CRITTER_BARTER) != 0) {
                    trader = obj;
                    break;
                }
            }
            if (only != nullptr) break;
        }
        if (trader != nullptr) {
            SDL_Log("autotest: barter with pid %x", trader->pid);
            // Something to sell: the pistol and a few of the stackable items are in the pack.
            obj_move_to_tile(obj_dude, trader->tile + 2, trader->elevation, nullptr);
            schedule(2000, barterTimer);
            gdialog_enter(trader, 0);
            game_user_wants_to_quit = 0;
        } else {
            SDL_Log("autotest: no trader found");
        }
    }
    bottomScreenSetTest(0, 0, 1.0f);
    return rc == 0;
}

bool scenes(const char* dir, int width, int height, bool game)
{
    for (const MuiSceneEntry* entry = muiSceneEntries(); entry->name != nullptr; entry++) {
        if (entry->needsGame != game) continue;
        MuiScreen* screen = entry->make();
        if (screen == nullptr) continue;
        muiPush(screen);
        bool ok = captureScene(dir, entry->name, width, height);
        if (ok && game && strcmp(entry->name, "04-inventory") == 0) {
            // Select the 10mm pistol (second row), then equip it.
            frames(5);
            tapBottom(0.3f, 0.37f);
            ok = captureScene(dir, "04b-inventory-selected", width, height);
            tapBottom(0.25f, 0.945f);
            ok = ok && captureScene(dir, "04c-inventory-equipped", width, height);
            SDL_Log("autotest: right hand item: %s", inven_right_hand(obj_dude) != nullptr ? item_name(inven_right_hand(obj_dude)) : (inven_left_hand(obj_dude) != nullptr ? item_name(inven_left_hand(obj_dude)) : "(none)"));
        }
        muiRemove(screen);
        delete screen;
        if (!ok) return false;
        frames(2);
    }

    return true;
}

} // namespace fallout
