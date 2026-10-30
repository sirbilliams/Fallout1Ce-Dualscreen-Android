#include "dev_autotest.h"

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include <SDL.h>

#include "bottom_screen.h"
#include "mobile_contracts.h"
#include "mui/mui.h"
#include "mui/mui_screens.h"
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

bool devAutotestRun()
{
    if (!contracts()) return false;

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

    for (const MuiSceneEntry* entry = muiSceneEntries(); entry->name != nullptr; entry++) {
        MuiScreen* screen = entry->make();
        if (screen == nullptr) continue;
        muiPush(screen);
        bool ok = captureScene(dir, entry->name, width, height);
        muiRemove(screen);
        delete screen;
        if (!ok) return false;
        frames(2);
    }

    bottomScreenSetTest(0, 0, 1.0f);
    return true;
}

} // namespace fallout
