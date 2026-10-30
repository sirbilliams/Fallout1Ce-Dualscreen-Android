#include "bottom_screen.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/combat_defs.h"
#include "game/config.h"
#include "game/critter.h"
#include "game/gconfig.h"
#include "game/intface.h"
#include "game/item.h"
#include "game/map_defs.h"
#include "game/object.h"
#include "game/protinst.h"
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
// The object [id] of dude's elevation (a critter), or of dude's inventory.
static Object* mobileFindCritter(int id)
{
    for (Object* object = obj_find_first_at(obj_dude->elevation); object != nullptr; object = obj_find_next_at()) {
        if (object->id == id && FID_TYPE(object->fid) == OBJ_TYPE_CRITTER && object != obj_dude) return object;
    }
    return nullptr;
}
static Object* mobileFindInventoryItem(int id)
{
    Inventory* inventory = &obj_dude->data.inventory;
    for (int index = 0; index < inventory->length; index++) {
        Object* item = inventory->items[index].item;
        if (item->id == id) return item;
    }
    return nullptr;
}
// Actions of the panels run here, in the game's input loop, with the checks
// of the clicks they stand for (the game's own functions do the rest).
static void mobileRunAction(const MobileCommand& command, bool inCombat, bool playerTurn)
{
    switch (command.type) {
    case MobileCommandType::AttackTarget: {
        if (inCombat && !playerTurn) return;
        Object* target = mobileFindCritter(command.id);
        if (target == nullptr || critter_is_dead(target)) return;
        int action;
        bool weapon, disabled;
        if (intface_get_current_state(&action, &weapon, &disabled) != 0 || disabled) return;
        int previous = action;
        if (command.value >= 0) {
            // A called shot: the aimed form of the current mode, the part chosen.
            int aimed = action == INTERFACE_ITEM_ACTION_SECONDARY ? INTERFACE_ITEM_ACTION_SECONDARY_AIMING : INTERFACE_ITEM_ACTION_PRIMARY_AIMING;
            if (action != INTERFACE_ITEM_ACTION_PRIMARY_AIMING && action != INTERFACE_ITEM_ACTION_SECONDARY_AIMING && !intface_set_item_action(aimed)) return;
            combat_set_preset_hit_location(command.value);
            combat_attack_this(target);
            combat_set_preset_hit_location(-1);
            if (obj_dude != nullptr && !critter_is_dead(obj_dude)) intface_set_item_action(previous);
        } else {
            combat_attack_this(target);
        }
        break;
    }
    case MobileCommandType::SetAttackAction:
        if (!inCombat || playerTurn) intface_set_item_action(command.id);
        break;
    case MobileCommandType::UseQuickItem: {
        if (inCombat && !playerTurn) return;
        Object* item = mobileFindInventoryItem(command.id);
        if (item == nullptr || item_get_type(item) != ITEM_TYPE_DRUG) return;
        if (item_d_take_drug(obj_dude, item)) {
            item_remove_mult(obj_dude, item, 1);
            obj_connect(item, obj_dude->tile, obj_dude->elevation, nullptr);
            obj_destroy(item);
            intface_update_hit_points(true);
        }
        break;
    }
    case MobileCommandType::Walk: {
        if (command.id < 0 || command.id >= HEX_GRID_SIZE || (inCombat && !playerTurn)) return;
        int points = inCombat ? combat_free_move + obj_dude->data.critter.combat.ap : -1;
        if (inCombat && points <= 0) return;
        register_begin(ANIMATION_REQUEST_RESERVED);
        if (command.value != 0) {
            register_object_run_to_tile(obj_dude, command.id, obj_dude->elevation, points, 0);
        } else {
            register_object_move_to_tile(obj_dude, command.id, obj_dude->elevation, points, 0);
        }
        register_end();
        break;
    }
    default:
        break;
    }
}
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
    case MobileCommandType::AttackTarget:
    case MobileCommandType::SetAttackAction:
    case MobileCommandType::UseQuickItem:
    case MobileCommandType::Walk:
        mobileRunAction(command, inCombat, playerTurn);
        return -1;
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
