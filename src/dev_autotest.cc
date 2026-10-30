#include "dev_autotest.h"

#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <string>
#include <vector>

#include <SDL.h>

#include "bottom_screen.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/object.h"
#include "mobile_contracts.h"
#include "mui/mui.h"
#include "mui/mui_screens.h"
#include "mui/mui_settings.h"
#include "plib/gnw/input.h"
#include "plib/gnw/svga.h"

namespace fallout {

namespace {

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
            SDL_Delay(16);
        }
        event.type = SDL_FINGERUP;
        event.tfinger.pressure = 0.0f;
        SDL_PushEvent(&event);
        for (int i = 0; i < 6; i++) {
            get_input();
            renderPresent();
            SDL_Delay(16);
        }
    }

    // Frames of the game's present path (top screens + bottom surface).
    void frames(int count)
    {
        for (int i = 0; i < count; i++) {
            get_input();
            renderPresent();
            SDL_Delay(16);
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

bool devAutotestRun()
{
    if (!contracts()) return false;

    const char* theme = getenv("FALLOUT_AUTOTEST_THEME");
    if (theme != nullptr) muiSettingsSetTheme(atoi(theme));
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

    return scenes(dir, width, height, false);
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
    return scenes(dir, width, height, true);
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

    bottomScreenSetTest(0, 0, 1.0f);
    return true;
}

} // namespace fallout
