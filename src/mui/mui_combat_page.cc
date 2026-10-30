#include "mui/mui_pages.h"

#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

#include "game/combat.h"
#include "game/combat_defs.h"
#include "game/critter.h"
#include "game/intface.h"
#include "game/item.h"
#include "game/map.h"
#include "game/object.h"
#include "game/proto_types.h"
#include "game/stat.h"
#include "game/stat_defs.h"
#include "mobile_contracts.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"
#include "mui/mui_skin.h"

namespace fallout {

// The combat panel of the bottom screen: what dude may do in his turn, from
// the game's own numbers (hit chances, AP costs, the active hand's attack
// modes). It reads only; every action is a MobileCommand the combat loop runs
// through the same functions a click would.

namespace {

    constexpr int kQuickSlots = 6;
    constexpr float kRowDp = 40.0f;

    int gAimTarget = -1; // object id of the critter whose called-shot list is open
    bool gPanelWasCombat = false;

    std::u32string u(const char* text)
    {
        return muiDecodeUtf8(text);
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

    bool dudeTurn()
    {
        return isInCombat() && obj_dude != nullptr && combat_whose_turn() == obj_dude;
    }

    struct Target {
        Object* critter;
        int id;
        int distance;
        // -1: can't be attacked now.
        int chance;
        std::u32string name;
        std::u32string state;
    };

    std::u32string healthState(Object* critter)
    {
        int maximum = stat_level(critter, STAT_MAXIMUM_HIT_POINTS);
        int current = critter_get_hits(critter);
        if (maximum <= 0 || current >= maximum) return u("Unhurt");
        int percent = current * 100 / maximum;
        if (percent >= 66) return u("Hurt");
        if (percent >= 33) return u("Wounded");
        return u("Near death");
    }

    struct Mode {
        bool valid = false;
        int hitMode = 0;
        bool aiming = false;
    };

    Mode currentMode()
    {
        Mode mode;
        mode.valid = intface_get_attack(&mode.hitMode, &mode.aiming) == 0;
        return mode;
    }

    int chanceFor(Object* critter, const Mode& mode, int location)
    {
        if (!mode.valid) return -1;
        int bad = combat_check_bad_shot(obj_dude, critter, mode.hitMode, mode.aiming);
        if (bad != COMBAT_BAD_SHOT_OK && bad != COMBAT_BAD_SHOT_NOT_ENOUGH_AP) return -1;
        int chance = determine_to_hit(obj_dude, critter, location, mode.hitMode);
        return std::clamp(chance, 0, 100);
    }

    std::vector<Target> targets()
    {
        std::vector<Target> result;
        if (obj_dude == nullptr) return result;
        Mode mode = currentMode();
        for (Object* object = obj_find_first_at(obj_dude->elevation); object != nullptr; object = obj_find_next_at()) {
            if (object == obj_dude || FID_TYPE(object->fid) != OBJ_TYPE_CRITTER || (object->flags & OBJECT_HIDDEN) != 0) continue;
            if (critter_is_dead(object) || object->data.critter.combat.team == obj_dude->data.critter.combat.team) continue;
            int distance = obj_dist(obj_dude, object);
            if (distance > 30) continue;
            Target target;
            target.critter = object;
            target.id = object->id;
            target.distance = distance;
            target.chance = chanceFor(object, mode, HIT_LOCATION_UNCALLED);
            target.name = muiDecodeGameText(critter_name(object));
            target.state = healthState(object);
            result.push_back(std::move(target));
        }
        std::sort(result.begin(), result.end(), [](const Target& a, const Target& b) { return a.distance < b.distance; });
        return result;
    }

    // Colour of a hit chance: poor, fair, good.
    MuiColor chanceColor(int chance)
    {
        const MuiTheme& theme = muiTheme();
        if (chance < 0) return theme.textDim;
        if (muiSkinPipboy()) return chance < 35 ? theme.textDim : (chance < 70 ? theme.text : theme.accent);
        return chance < 35 ? muiRgb(0xFF6A4A) : (chance < 70 ? muiRgb(0xFFC040) : muiRgb(0x60FF60));
    }

    // A button that's there but can't be used now: dimmed, takes no touch.
    bool panelButton(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& label, bool enabled, MuiButtonStyle style = MuiButtonStyle::Normal)
    {
        if (enabled) {
            return ui.button(id, rect, label, style);
        }
        ui.mark(id, rect);
        muiSkinButton(ui, rect, false, false);
        muiGameButtonLabel(ui, rect, label, muiTheme().textDim);
        muiFillRoundRect(rect, ui.dp(3.0f), muiRgb(0x000000, 150));
        return false;
    }

    MuiRect cell(const MuiRect& row, int index, int count, float gap)
    {
        float width = (row.w - gap * (count - 1)) / count;
        return { row.x + (width + gap) * index, row.y, width, row.h };
    }

    std::u32string modeLabel(int action)
    {
        Object* weapon = nullptr;
        intface_get_current_item(&weapon);
        bool secondary = action == INTERFACE_ITEM_ACTION_SECONDARY || action == INTERFACE_ITEM_ACTION_SECONDARY_AIMING;
        bool aimed = action == INTERFACE_ITEM_ACTION_PRIMARY_AIMING || action == INTERFACE_ITEM_ACTION_SECONDARY_AIMING;
        // The hit mode this action stands for, from the active hand.
        int handMode = intface_is_item_right_hand() == HAND_RIGHT ? HIT_MODE_RIGHT_WEAPON_PRIMARY : HIT_MODE_LEFT_WEAPON_PRIMARY;
        int hitMode = handMode + (secondary ? 1 : 0);
        const char* base = "ATTACK";
        if (weapon == nullptr) {
            base = "PUNCH";
        } else {
            switch (item_w_subtype(weapon, hitMode)) {
            case ATTACK_TYPE_UNARMED:
                base = "STRIKE";
                break;
            case ATTACK_TYPE_MELEE:
                base = secondary ? "THRUST" : "SWING";
                break;
            case ATTACK_TYPE_THROW:
                base = "THROW";
                break;
            case ATTACK_TYPE_RANGED:
                base = item_w_rounds(weapon) > 1 && secondary ? "BURST" : "SHOOT";
                break;
            default:
                break;
            }
        }
        char label[24];
        snprintf(label, sizeof(label), aimed ? "AIM %s" : "%s", base);
        return u(label);
    }

    void actionPoints(MuiContext& ui, const MuiRect& rect, int cost)
    {
        const MuiTheme& theme = muiTheme();
        bool turn = dudeTurn();
        muiSkinPlate(ui, rect, false);
        MuiRect well = rect.inset(ui.dp(5.0f));
        muiSkinWell(ui, well);
        int maximum = obj_dude != nullptr ? stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS) : 0;
        int current = obj_dude != nullptr ? std::max(obj_dude->data.critter.combat.ap, 0) : 0;
        char left[24];
        snprintf(left, sizeof(left), "AP %d/%d", current, maximum);
        float textSize = ui.dp(20.0f);
        std::u32string leftText = u(left);
        float leftWidth = muiTextWidth(leftText, textSize) + ui.dp(10.0f);
        MuiRect inner = well.inset(ui.dp(10.0f), ui.dp(6.0f));
        muiDrawTextAligned(leftText, { inner.x, inner.y, leftWidth, inner.h }, textSize, turn ? theme.accent : theme.textDim, MuiAlign::Start, MuiAlign::Center);
        float rightWidth = 0.0f;
        if (cost >= 0) {
            char right[16];
            snprintf(right, sizeof(right), "%d AP", cost);
            std::u32string rightText = u(right);
            rightWidth = muiTextWidth(rightText, textSize) + ui.dp(10.0f);
            muiDrawTextAligned(rightText, { inner.right() - rightWidth, inner.y, rightWidth, inner.h }, textSize, theme.text, MuiAlign::End, MuiAlign::Center);
        }
        MuiRect pips = { inner.x + leftWidth, inner.y + ui.dp(4.0f), inner.w - leftWidth - rightWidth, inner.h - ui.dp(8.0f) };
        int count = std::max(std::max(maximum, current), 1);
        float gap = ui.dp(3.0f);
        float width = (pips.w - gap * (count - 1)) / count;
        // The next attack's points show in the warning colour.
        int spent = cost >= 0 && cost <= current ? current - cost : current;
        for (int index = 0; index < count; index++) {
            MuiRect pip = { pips.x + (width + gap) * index, pips.y, width, pips.h };
            MuiColor color = theme.well;
            if (index < spent) {
                color = turn ? theme.accent : theme.textDim;
            } else if (index < current) {
                color = theme.warning;
            }
            muiFillRect(pip, color);
        }
    }

    void weaponRow(MuiContext& ui, const MuiRect& row, bool turn, int* cost)
    {
        const MuiTheme& theme = muiTheme();
        float gap = ui.dp(8.0f);
        float swapWidth = ui.dp(80.0f);
        float chipsWidth = std::min(row.w * 0.52f, ui.dp(310.0f));
        MuiRect card = { row.x, row.y, row.w - swapWidth - chipsWidth - gap * 2.0f, row.h };
        MuiRect swap = { card.right() + gap, row.y, swapWidth, row.h };
        MuiRect chips = { swap.right() + gap, row.y, chipsWidth, row.h };

        muiSkinPlate(ui, card, false);
        MuiRect well = card.inset(ui.dp(5.0f));
        muiSkinWell(ui, well);
        MuiRect text = well.inset(ui.dp(8.0f), ui.dp(3.0f));
        Object* item = nullptr;
        intface_get_current_item(&item);
        std::u32string name = item != nullptr ? muiDecodeGameText(item_name(item)) : u("UNARMED");
        muiDrawTextAligned(name, { text.x, text.y, text.w, text.h * 0.5f }, muiFitTextSize(name, text.w, ui.dp(17.0f), ui.dp(10.0f)), theme.accent, MuiAlign::Start, MuiAlign::Center);
        char detail[48] = "";
        if (item != nullptr && item_get_type(item) == ITEM_TYPE_WEAPON && item_w_max_ammo(item) > 0) {
            snprintf(detail, sizeof(detail), "%s \xC2\xB7 %d/%d", intface_is_item_right_hand() == HAND_RIGHT ? "R" : "L", item_w_curr_ammo(item), item_w_max_ammo(item));
        } else {
            snprintf(detail, sizeof(detail), "%s HAND", intface_is_item_right_hand() == HAND_RIGHT ? "RIGHT" : "LEFT");
        }
        std::u32string detailText = u(detail);
        muiDrawTextAligned(detailText, { text.x, text.y + text.h * 0.5f, text.w, text.h * 0.5f }, muiFitTextSize(detailText, text.w, ui.dp(14.0f), ui.dp(10.0f)), theme.textDim, MuiAlign::Start, MuiAlign::Center);

        if (panelButton(ui, "combat.swap", swap, U"SWAP", turn)) {
            send(MobileCommandType::SwapHand);
        }

        // Attack modes: the active hand's own, the current one lit.
        int actions[4];
        int count = intface_get_available_actions(actions, 4);
        int current = INTERFACE_ITEM_ACTION_PRIMARY;
        bool weapon, disabled;
        intface_get_current_state(&current, &weapon, &disabled);
        Mode mode = currentMode();
        *cost = mode.valid ? item_mp_cost(obj_dude, mode.hitMode, mode.aiming) : -1;
        float chipGap = ui.dp(4.0f);
        for (int index = 0; index < 4; index++) {
            int column = index % 2;
            int line = index / 2;
            float width = (chips.w - chipGap) / 2.0f;
            float height = (chips.h - chipGap) / 2.0f;
            MuiRect rect = { chips.x + column * (width + chipGap), chips.y + line * (height + chipGap), width, height };
            std::string id = "combat.mode." + std::to_string(index);
            if (index >= count) {
                ui.mark(id, rect);
                muiFillRoundRect(rect, ui.dp(3.0f), muiRgb(0x000000, 90));
                continue;
            }
            bool selected = actions[index] == current;
            if (panelButton(ui, id, rect, modeLabel(actions[index]), turn, selected ? MuiButtonStyle::Primary : MuiButtonStyle::Normal) && !selected) {
                send(MobileCommandType::SetAttackAction, actions[index]);
            }
            if (selected) {
                muiStrokeRoundRect(rect, ui.dp(3.0f), ui.dp(2.5f), theme.accent);
            }
        }
    }

    void targetList(MuiContext& ui, const MuiRect& area, bool turn, const std::vector<Target>& list, bool canAim)
    {
        const MuiTheme& theme = muiTheme();
        muiSkinPlate(ui, area, false);
        MuiRect well = area.inset(ui.dp(5.0f));
        muiSkinWell(ui, well);
        if (list.empty()) {
            muiDrawTextAligned(u("NO TARGETS IN SIGHT"), well, ui.dp(18.0f), theme.textDim, MuiAlign::Center, MuiAlign::Center);
            return;
        }
        float rowHeight = ui.dp(kRowDp + 16.0f);
        float gap = ui.dp(4.0f);
        MuiRect inner = well.inset(ui.dp(4.0f));
        float offset = ui.scroll("combat.targets", inner, list.size() * (rowHeight + gap));
        muiPushClip(inner);
        float aimWidth = ui.dp(84.0f);
        for (size_t index = 0; index < list.size(); index++) {
            const Target& target = list[index];
            MuiRect row = { inner.x, inner.y + index * (rowHeight + gap) - offset, inner.w, rowHeight };
            if (row.bottom() < inner.y || row.y > inner.bottom()) continue;
            MuiRect body = { row.x, row.y, row.w - aimWidth - gap, row.h };
            MuiRect aim = { body.right() + gap, row.y, aimWidth, row.h };
            bool canHit = turn && target.chance >= 0;
            // Visible part only (a row half under the edge takes no touch outside it).
            MuiRect visible = { body.x, std::max(body.y, inner.y), body.w, std::min(body.bottom(), inner.bottom()) - std::max(body.y, inner.y) };
            std::string id = "combat.target." + std::to_string(index);
            bool pressed = false;
            bool tapped = canHit && visible.h > ui.dp(20.0f) ? ui.touchable(id, visible, &pressed) : false;
            if (!canHit) ui.mark(id, visible);
            muiSkinButton(ui, body, pressed, false);
            MuiRect text = body.inset(ui.dp(12.0f), ui.dp(4.0f));
            float chanceWidth = ui.dp(74.0f);
            float distWidth = ui.dp(70.0f);
            MuiRect left = { text.x, text.y, text.w - chanceWidth - distWidth, text.h };
            MuiColor nameColor = canHit ? theme.accent : theme.textDim;
            muiDrawTextAligned(target.name, { left.x, left.y, left.w, left.h * 0.55f }, muiFitTextSize(target.name, left.w, ui.dp(18.0f), ui.dp(11.0f)), nameColor, MuiAlign::Start, MuiAlign::Center);
            muiDrawTextAligned(target.state, { left.x, left.y + left.h * 0.55f, left.w, left.h * 0.45f }, ui.dp(13.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
            char distance[16];
            snprintf(distance, sizeof(distance), "%d hex", target.distance);
            muiDrawTextAligned(u(distance), { left.right(), text.y, distWidth, text.h }, ui.dp(15.0f), theme.text, MuiAlign::Center, MuiAlign::Center);
            char chance[16];
            if (target.chance >= 0) {
                snprintf(chance, sizeof(chance), "%d%%", target.chance);
            } else {
                snprintf(chance, sizeof(chance), "--");
            }
            muiDrawTextAligned(u(chance), { text.right() - chanceWidth, text.y, chanceWidth, text.h }, ui.dp(22.0f), chanceColor(target.chance), MuiAlign::End, MuiAlign::Center);
            if (tapped) {
                send(MobileCommandType::AttackTarget, target.id, -1);
            }
            std::string aimId = "combat.aim." + std::to_string(index);
            MuiRect aimVisible = { aim.x, std::max(aim.y, inner.y), aim.w, std::min(aim.bottom(), inner.bottom()) - std::max(aim.y, inner.y) };
            bool aimOk = turn && canAim && target.chance >= 0 && aimVisible.h > ui.dp(20.0f);
            if (aimOk) {
                if (ui.touchable(aimId, aimVisible)) {
                    gAimTarget = target.id;
                }
                muiSkinButton(ui, aim, false, false);
                muiGameButtonLabel(ui, aim, U"AIM", theme.text);
            } else {
                ui.mark(aimId, aimVisible);
                muiSkinButton(ui, aim, false, false);
                muiGameButtonLabel(ui, aim, U"AIM", theme.textDim);
                muiFillRoundRect(aim, ui.dp(3.0f), muiRgb(0x000000, 130));
            }
        }
        muiPopClip();
    }

    // Called shot: the parts of the chosen critter, each with its own chance.
    void calledShot(MuiContext& ui, const MuiRect& area, bool turn, const std::vector<Target>& list)
    {
        const MuiTheme& theme = muiTheme();
        const Target* chosen = nullptr;
        for (const Target& target : list) {
            if (target.id == gAimTarget) chosen = &target;
        }
        if (chosen == nullptr || !turn) {
            gAimTarget = -1;
            return;
        }
        muiSkinPlate(ui, area, false);
        MuiRect well = area.inset(ui.dp(5.0f));
        muiSkinWell(ui, well);
        MuiRect inner = well.inset(ui.dp(10.0f), ui.dp(6.0f));
        float headHeight = ui.dp(34.0f);
        std::u32string title = u("CALLED SHOT \xC2\xB7 ") + chosen->name;
        muiDrawTextAligned(title, { inner.x, inner.y, inner.w - ui.dp(100.0f), headHeight }, muiFitTextSize(title, inner.w - ui.dp(110.0f), ui.dp(17.0f), ui.dp(11.0f)), theme.accent, MuiAlign::Start, MuiAlign::Center);
        if (ui.button("combat.aimback", { inner.right() - ui.dp(92.0f), inner.y - ui.dp(2.0f), ui.dp(92.0f), headHeight + ui.dp(4.0f) }, U"BACK")) {
            gAimTarget = -1;
            return;
        }
        static const int parts[8] = { HIT_LOCATION_HEAD, HIT_LOCATION_EYES, HIT_LOCATION_TORSO, HIT_LOCATION_GROIN, HIT_LOCATION_LEFT_ARM, HIT_LOCATION_RIGHT_ARM, HIT_LOCATION_LEFT_LEG, HIT_LOCATION_RIGHT_LEG };
        MuiRect grid = { inner.x, inner.y + headHeight + ui.dp(8.0f), inner.w, inner.h - headHeight - ui.dp(8.0f) };
        float gap = ui.dp(6.0f);
        int rows = 4;
        float width = (grid.w - gap) / 2.0f;
        float height = std::max((grid.h - gap * (rows - 1)) / rows, ui.dp(36.0f));
        // The chance of an aimed shot is the aimed mode's, even if the card shows another.
        Mode mode = currentMode();
        Mode aimed = mode;
        aimed.aiming = true;
        for (int index = 0; index < 8; index++) {
            int column = index % 2;
            int line = index / 2;
            MuiRect rect = { grid.x + column * (width + gap), grid.y + line * (height + gap), width, height };
            const char* name = combat_get_loc_name(chosen->critter, parts[index]);
            std::u32string label = name != nullptr ? muiDecodeGameText(name) : u("?");
            int chance = chanceFor(chosen->critter, aimed, parts[index]);
            bool enabled = chance >= 0;
            std::string id = "combat.part." + std::to_string(index);
            bool tapped = false;
            if (enabled) {
                tapped = ui.touchable(id, rect);
            } else {
                ui.mark(id, rect);
            }
            muiSkinButton(ui, rect, false, false);
            MuiRect text = rect.inset(ui.dp(12.0f), ui.dp(3.0f));
            muiDrawTextAligned(label, { text.x, text.y, text.w - ui.dp(70.0f), text.h }, muiFitTextSize(label, text.w - ui.dp(76.0f), ui.dp(17.0f), ui.dp(11.0f)), enabled ? theme.text : theme.textDim, MuiAlign::Start, MuiAlign::Center);
            char value[16];
            if (enabled) {
                snprintf(value, sizeof(value), "%d%%", chance);
            } else {
                snprintf(value, sizeof(value), "--");
            }
            muiDrawTextAligned(u(value), text, ui.dp(21.0f), chanceColor(chance), MuiAlign::End, MuiAlign::Center);
            if (tapped) {
                send(MobileCommandType::AttackTarget, chosen->id, parts[index]);
                gAimTarget = -1;
            }
        }
    }

    struct QuickItem {
        Object* item;
        int id;
        int count;
    };

    std::vector<QuickItem> quickItems()
    {
        std::vector<QuickItem> result;
        if (obj_dude == nullptr) return result;
        Inventory* inventory = &obj_dude->data.inventory;
        for (int index = 0; index < inventory->length && static_cast<int>(result.size()) < kQuickSlots; index++) {
            Object* item = inventory->items[index].item;
            if (item_get_type(item) != ITEM_TYPE_DRUG) continue;
            result.push_back({ item, item->id, inventory->items[index].quantity });
        }
        return result;
    }

    void hotbar(MuiContext& ui, const MuiRect& row, bool turn)
    {
        const MuiTheme& theme = muiTheme();
        muiSkinPlate(ui, row, false);
        MuiRect well = row.inset(ui.dp(5.0f));
        muiSkinWell(ui, well);
        std::vector<QuickItem> items = quickItems();
        MuiRect inner = well.inset(ui.dp(5.0f));
        for (int slot = 0; slot < kQuickSlots; slot++) {
            MuiRect rect = cell(inner, slot, kQuickSlots, ui.dp(5.0f));
            std::string id = "combat.quick." + std::to_string(slot);
            bool have = slot < static_cast<int>(items.size());
            bool pressed = false;
            bool tapped = false;
            if (have && turn) {
                tapped = ui.touchable(id, rect, &pressed);
            } else {
                ui.mark(id, rect);
            }
            muiFillRect(rect, have ? theme.well : muiRgb(0x000000, 120));
            muiStrokeRoundRect(rect, ui.dp(3.0f), ui.dp(1.0f), pressed ? theme.accent : theme.panelBorder);
            if (!have) {
                char number[4];
                snprintf(number, sizeof(number), "%d", slot + 1);
                muiDrawTextAligned(u(number), rect, ui.dp(22.0f), theme.textDim.withAlpha(120), MuiAlign::Center, MuiAlign::Center);
                continue;
            }
            int width, height;
            SDL_Texture* texture = muiArtTexture(item_inv_fid(items[slot].item), 0, &width, &height);
            MuiRect art = rect.inset(ui.dp(4.0f));
            if (texture != nullptr && width > 0 && height > 0) {
                muiDrawTexture(texture, muiFitRect(art, static_cast<float>(width), static_cast<float>(height)), turn ? MuiColor { 255, 255, 255, 255 } : MuiColor { 150, 150, 150, 255 });
            } else {
                std::u32string name = muiDecodeGameText(item_name(items[slot].item));
                muiDrawTextAligned(name, art, muiFitTextSize(name, art.w, ui.dp(13.0f), ui.dp(9.0f)), theme.text, MuiAlign::Center, MuiAlign::Center);
            }
            if (items[slot].count > 1) {
                char count[12];
                snprintf(count, sizeof(count), "x%d", items[slot].count);
                std::u32string text = u(count);
                float size = ui.dp(13.0f);
                float tagWidth = muiTextWidth(text, size) + ui.dp(8.0f);
                MuiRect tag = { rect.right() - tagWidth - ui.dp(2.0f), rect.bottom() - ui.dp(20.0f), tagWidth, ui.dp(18.0f) };
                muiFillRoundRect(tag, ui.dp(3.0f), muiRgb(0x000000, 190));
                muiDrawTextAligned(text, tag, size, theme.text, MuiAlign::Center, MuiAlign::Center);
            }
            if (tapped) {
                send(MobileCommandType::UseQuickItem, items[slot].id);
            }
        }
    }

} // namespace

bool muiCombatPageActive()
{
    bool now = isInCombat() && obj_dude != nullptr;
    if (!now) {
        gAimTarget = -1;
    }
    gPanelWasCombat = now;
    return now;
}

void muiCombatPageReset()
{
    gAimTarget = -1;
}

void muiCombatPage(MuiContext& ui, int* request)
{
    muiSkinBackdrop(ui, ui.screenRect());
    MuiRect safe = ui.safeRect().inset(ui.dp(14.0f));
    float gap = ui.dp(8.0f);
    bool turn = dudeTurn();

    // Nav: the game's screens and the touchpad (the panel replaces the console).
    float navHeight = ui.dp(40.0f);
    MuiRect nav = { safe.x, safe.y, safe.w, navHeight };
    struct Nav {
        const char* id;
        const char16_t* label;
        int request;
    };
    const Nav items[] = { { "combat.inv", u"INV", 10 }, { "combat.cha", u"CHA", 11 }, { "combat.map", u"MAP", 1 }, { "combat.pad", u"PAD", 2 }, { "combat.menu", u"MENU", 12 } };
    for (int index = 0; index < 5; index++) {
        std::u32string label(items[index].label, items[index].label + std::char_traits<char16_t>::length(items[index].label));
        if (panelButton(ui, items[index].id, cell(nav, index, 5, gap), label, true)) {
            if (items[index].request == 10) {
                send(MobileCommandType::OpenScreen, static_cast<int>(MobileScreen::Inventory));
            } else if (items[index].request == 11) {
                send(MobileCommandType::OpenScreen, static_cast<int>(MobileScreen::Character));
            } else if (items[index].request == 12) {
                send(MobileCommandType::Menu);
            } else {
                *request = items[index].request;
            }
        }
    }

    float y = nav.bottom() + gap;
    int cost = -1;
    float apHeight = ui.dp(48.0f);
    float weaponHeight = ui.dp(80.0f);
    float quickHeight = ui.dp(64.0f);
    float endHeight = ui.dp(52.0f);
    float listBottom = safe.bottom() - endHeight - gap - quickHeight - gap;

    // The weapon row is built first (it knows the attack's AP cost).
    MuiRect apRect = { safe.x, y, safe.w, apHeight };
    MuiRect weaponRect = { safe.x, apRect.bottom() + gap, safe.w, weaponHeight };
    weaponRow(ui, weaponRect, turn, &cost);
    actionPoints(ui, apRect, cost);

    MuiRect listRect = { safe.x, weaponRect.bottom() + gap, safe.w, std::max(listBottom - weaponRect.bottom() - gap, ui.dp(90.0f)) };
    std::vector<Target> list = targets();
    int actions[4];
    int actionCount = intface_get_available_actions(actions, 4);
    bool canAim = false;
    for (int index = 0; index < actionCount; index++) {
        canAim = canAim || actions[index] == INTERFACE_ITEM_ACTION_PRIMARY_AIMING || actions[index] == INTERFACE_ITEM_ACTION_SECONDARY_AIMING;
    }
    if (gAimTarget != -1) {
        // The called shot takes the hotbar's room too.
        MuiRect tall = { listRect.x, listRect.y, listRect.w, safe.bottom() - endHeight - gap - listRect.y };
        calledShot(ui, tall, turn, list);
    }
    if (gAimTarget == -1) {
        targetList(ui, listRect, turn, list, canAim);
        hotbar(ui, { safe.x, safe.bottom() - endHeight - gap - quickHeight, safe.w, quickHeight }, turn);
    }

    MuiRect buttons = { safe.x, safe.bottom() - endHeight, safe.w, endHeight };
    if (panelButton(ui, "combat.endturn", cell(buttons, 0, 2, gap), turn || !isInCombat() ? U"END TURN" : U"ENEMY TURN", turn, MuiButtonStyle::Primary)) {
        send(MobileCommandType::EndTurn);
    }
    if (panelButton(ui, "combat.endcombat", cell(buttons, 1, 2, gap), U"END COMBAT", turn)) {
        send(MobileCommandType::EndCombat);
    }
}

} // namespace fallout
