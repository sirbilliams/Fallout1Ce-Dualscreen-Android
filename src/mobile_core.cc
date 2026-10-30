#include "bottom_screen.h"
#include "game/combat.h"
#include "game/config.h"
#include "game/gconfig.h"
#include "game/intface.h"
#include "game/object.h"
#include "mobile_contracts.h"
#include "mui/mui.h"
#include "mui/mui_settings.h"
#include "platform_compat.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"
#include <deque>
#include <utility>
namespace fallout {
static MobileSnapshot snapshot;
static std::deque<MobileCommand> commands;
MobileSettings mobileSettings()
{
    MobileSettings settings;
    int enabled = 1, bottom = 1;
    config_get_value(&game_config, "touch", "enabled", &enabled);
    config_get_value(&game_config, "touch", "bottom_screen", &bottom);
    settings.enabled = enabled != 0;
    settings.bottomScreen = bottom != 0;
    return settings;
}
const MobileSnapshot& mobileSnapshot() { return snapshot; }
void mobilePublishSnapshot(MobileSnapshot next)
{
    if (next.generation != snapshot.generation) commands.clear();
    snapshot = std::move(next);
}
bool mobileQueueCommand(const MobileCommand& command)
{
    if (command.generation != snapshot.generation || commands.size() >= 64) return false;
    commands.push_back(command);
    return true;
}
bool mobileTakeCommand(MobileCommand* command)
{
    if (!command || commands.empty()) return false;
    *command = commands.front();
    commands.pop_front();
    return true;
}
void mobileClearCommands() { commands.clear(); }
static bool polled = false;
static unsigned int polledAt = 0;
// A polling loop runs every frame; this long without it and the game is busy
// with something else.
constexpr unsigned int kLiveMs = 250;
bool mobileDispatchLive() { return polled && compat_ticks() - polledAt < kLiveMs; }
int mobilePollKey(bool inCombat)
{
    polled = true;
    polledAt = compat_ticks();
    MobileCommand command;
    if (!mobileTakeCommand(&command)) return -1;
    if (command.generation != snapshot.generation || obj_dude == nullptr || !intface_is_enabled()) return -1;
    bool playerTurn = inCombat && combat_whose_turn() == obj_dude;
    switch (command.type) {
    case MobileCommandType::OpenScreen:
        switch (static_cast<MobileScreen>(command.id)) {
        case MobileScreen::Inventory:
            return KEY_LOWERCASE_I;
        case MobileScreen::Character:
            return KEY_LOWERCASE_C;
        case MobileScreen::Pipboy:
            return inCombat ? -1 : KEY_LOWERCASE_P; // the game refuses it in combat
        case MobileScreen::Map:
            return KEY_TAB;
        default:
            return -1;
        }
    case MobileCommandType::UseItem:
        return command.id == -1 ? -20 : -1;
    case MobileCommandType::SwapHand:
        return KEY_LOWERCASE_B;
    case MobileCommandType::CycleAttackMode:
        return KEY_LOWERCASE_N;
    case MobileCommandType::EndTurn:
        return playerTurn ? KEY_SPACE : -1;
    case MobileCommandType::EndCombat:
        return playerTurn ? KEY_RETURN : -1;
    case MobileCommandType::Menu:
        return KEY_LOWERCASE_O;
    case MobileCommandType::Save:
        return KEY_F4;
    case MobileCommandType::Load:
        return KEY_F5;
    default:
        return -1;
    }
}
void mobileRenderBottom()
{
    MobileSettings settings = mobileSettings();
    if (!settings.enabled || !settings.bottomScreen) return;
    muiSettingsRefresh();
    if (gSdlRenderer != nullptr) {
        muiRenderBottom(gSdlRenderer);
        return;
    }
    int width, height;
    bottomScreenGetSize(&width, &height);
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return;
    static int cachedWidth = 0, cachedHeight = 0;
    static std::vector<unsigned char> pixels;
    if (cachedWidth == width && cachedHeight == height) {
        bottomScreenPostFrame(pixels.data(), width, height, width * 4);
        return;
    }
    const char* label = "Fallout 1 CE - bottom screen";
    int oldFont = text_curr();
    text_font(100);
    int w = text_width(label), h = text_height();
    std::vector<unsigned char> mask(w * h, 0);
    text_to_buf(mask.data(), label, w, w, 1);
    text_font(oldFont);
    pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    cachedWidth = width;
    cachedHeight = height;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            auto i = (static_cast<size_t>(y) * width + x) * 4;
            int tx = x - (width - w) / 2, ty = y - (height - h) / 2;
            pixels[i + 1] = tx >= 0 && tx < w && ty >= 0 && ty < h && mask[ty * w + tx] ? 220 : 16;
            pixels[i + 3] = 255;
        }
    bottomScreenPostFrame(pixels.data(), width, height, width * 4);
}
}
