#include "dev_autotest.h"

#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include <SDL.h>

#include "bottom_screen.h"
#include "game/config.h"
#include "game/editor.h"
#include "game/game.h"
#include "game/gconfig.h"
#include "game/gdialog.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/loadsave.h"
#include "game/map.h"
#include "game/object.h"
#include "game/pipboy.h"
#include "game/proto.h"
#include "game/proto_types.h"
#include "game/scripts.h"
#include "game/worldmap.h"
#include "mobile_contracts.h"
#include "mui/mui.h"
#include "mui/mui_screens.h"
#include "mui/mui_settings.h"
#include "platform_compat.h"
#include "plib/gnw/input.h"
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

    bool ok = scenes(dir, width, height, false);
    bottomScreenSetTest(0, 0, 1.0f);
    return ok;
}

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
