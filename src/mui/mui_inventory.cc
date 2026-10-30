#include "mui/mui.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"

#include <stdio.h>

#include <algorithm>

#include "game/combat.h"
#include "game/intface.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/object.h"
#include "game/object_types.h"
#include "game/protinst.h"
#include "game/proto_types.h"
#include "game/stat.h"
#include "game/stat_defs.h"

namespace fallout {

namespace {

    std::u32string text(const char* value)
    {
        return muiDecodeGameText(value != nullptr ? value : "");
    }

    std::u32string number(int value)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", value);
        return muiDecodeUtf8(buffer);
    }

    // Touch inventory of the player. Items keep their F1 flags (right hand,
    // left hand, worn): equipping sets them and `adjust_fid` rebuilds the
    // player's look as the classic window does when it closes. Drop and Use go
    // through the same engine calls as the classic menu.
    class InventoryScreen : public MuiScreen {
    public:
        explicit InventoryScreen(Object* owner)
            : owner(owner)
        {
            modal = true;
        }

        MuiSurface surface() override { return muiGameScreenSurface(); }
        bool coversScreen() override { return true; }
        void back() override { finished = true; }
        void build(MuiContext& ui) override;

    private:
        Object* owner;
        int selected = -1;
        std::vector<Object*> items;

        void refresh()
        {
            items.clear();
            Inventory* inventory = &owner->data.inventory;
            for (int index = 0; index < inventory->length; index++) {
                items.push_back(inventory->items[index].item);
            }
            if (selected >= static_cast<int>(items.size())) {
                selected = -1;
            }
        }

        int quantityOf(Object* item) const
        {
            Inventory* inventory = &owner->data.inventory;
            for (int index = 0; index < inventory->length; index++) {
                if (inventory->items[index].item == item) {
                    return inventory->items[index].quantity;
                }
            }
            return 0;
        }

        void equip(Object* item)
        {
            if (item_get_type(item) == ITEM_TYPE_ARMOR) {
                Object* worn = inven_worn(owner);
                if (worn != nullptr) {
                    worn->flags &= ~OBJECT_WORN;
                }
                item->flags |= OBJECT_WORN;
            } else {
                bool right = intface_is_item_right_hand() != 0;
                Object* previous = right ? inven_right_hand(owner) : inven_left_hand(owner);
                if (previous != nullptr) {
                    previous->flags &= ~(right ? OBJECT_IN_RIGHT_HAND : OBJECT_IN_LEFT_HAND);
                }
                item->flags |= right ? OBJECT_IN_RIGHT_HAND : OBJECT_IN_LEFT_HAND;
            }
            changed();
        }

        void unequip(Object* item)
        {
            item->flags &= ~OBJECT_EQUIPPED;
            changed();
        }

        void changed()
        {
            adjust_fid();
            if (owner == obj_dude) {
                intface_update_ac(false);
                intface_update_items(false);
            }
        }
    };

    void InventoryScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        refresh();

        bool back = false;
        MuiRect content = muiScreenFrame(ui, U"INVENTORY", &back);
        if (back) {
            finished = true;
        }

        // Weight line.
        int carried = item_total_weight(owner);
        int capacity = stat_level(owner, STAT_CARRY_WEIGHT);
        float readoutHeight = ui.dp(40.0f);
        MuiRect readout = { content.x, content.y, content.w, readoutHeight };
        ui.panel(readout);
        muiReadout(ui, readout.inset(ui.dp(10.0f), 0.0f), U"CARRIED WEIGHT", number(carried) + U" / " + number(capacity) + U" LB");

        // Actions for the chosen item.
        float actionHeight = ui.dp(54.0f);
        MuiRect actions = { content.x, content.bottom() - actionHeight, content.w, actionHeight };

        MuiRect infoRect = { content.x, actions.y - ui.dp(64.0f) - ui.dp(8.0f), content.w, ui.dp(64.0f) };
        MuiRect list = { content.x, readout.bottom() + ui.dp(8.0f), content.w, infoRect.y - readout.bottom() - ui.dp(16.0f) };

        std::vector<MuiRow> rows;
        for (size_t index = 0; index < items.size(); index++) {
            Object* item = items[index];
            MuiRow row;
            row.left = text(item_name(item));
            if ((item->flags & OBJECT_WORN) != 0) {
                row.left += U"  (worn)";
            }
            if ((item->flags & OBJECT_IN_RIGHT_HAND) != 0) {
                row.left += U"  (right hand)";
            }
            if ((item->flags & OBJECT_IN_LEFT_HAND) != 0) {
                row.left += U"  (left hand)";
            }
            int quantity = quantityOf(item);
            row.right = (quantity > 1 ? U"x" + number(quantity) + U"  " : std::u32string()) + (item_weight(item) > 0 ? number(item_weight(item) * quantity) + U" LB" : U"-");
            row.selected = static_cast<int>(index) == selected;
            row.icon = item_inv_fid(item);
            rows.push_back(row);
        }
        if (rows.empty()) {
            rows.push_back({ U"Nothing carried", U"", false, false, true });
        }
        int tapped = muiListView(ui, "inventory.list", list, rows, 46.0f);
        if (tapped >= 0 && tapped < static_cast<int>(items.size())) {
            selected = tapped;
        }

        ui.panel(infoRect);
        Object* item = selected >= 0 && selected < static_cast<int>(items.size()) ? items[selected] : nullptr;
        MuiRect infoInner = infoRect.inset(ui.dp(10.0f));
        if (item != nullptr) {
            std::u32string description = text(item_description(item));
            float size = ui.dp(14.0f);
            auto lines = muiWrapText(description, infoInner.w, size);
            float y = infoInner.y;
            size_t fit = std::max<size_t>(1, static_cast<size_t>(infoInner.h / muiLineHeight(size)));
            for (size_t index = 0; index < lines.size() && index < fit; index++) {
                std::u32string line = lines[index];
                if (index + 1 == fit && lines.size() > fit) {
                    // More text than room: cut with an ellipsis, not mid-word.
                    while (!line.empty() && muiTextWidth(line + U"..", size) > infoInner.w) {
                        line.pop_back();
                    }
                    line += U"..";
                }
                muiDrawText(line, infoInner.x, y, size, theme.text);
                y += muiLineHeight(size);
            }
        } else {
            muiDrawTextAligned(U"Select an item", infoInner, ui.dp(15.0f), theme.textDim, MuiAlign::Center, MuiAlign::Center);
        }

        if (item == nullptr) {
            return;
        }

        bool equipped = (item->flags & OBJECT_EQUIPPED) != 0;
        int type = item_get_type(item);
        bool canEquip = type == ITEM_TYPE_ARMOR || type == ITEM_TYPE_WEAPON;
        bool canUse = type == ITEM_TYPE_DRUG || type == ITEM_TYPE_MISC;

        struct Action {
            const char* id;
            const char* label;
            bool enabled;
        };
        const Action buttons[] = {
            { "inventory.equip", equipped ? "UNEQUIP" : "EQUIP", canEquip || equipped },
            { "inventory.use", "USE", canUse },
            { "inventory.drop", "DROP", true },
        };
        int visible = 0;
        for (const Action& button : buttons) {
            visible += button.enabled ? 1 : 0;
        }
        float gap = ui.dp(10.0f);
        float width = (actions.w - gap * (visible - 1)) / visible;
        float x = actions.x;
        for (const Action& button : buttons) {
            if (!button.enabled) {
                continue;
            }
            MuiRect rect = { x, actions.y, width, actions.h };
            x += width + gap;
            if (!ui.button(button.id, rect, muiDecodeUtf8(button.label))) {
                continue;
            }
            if (button.id[10] == 'e') {
                if (equipped) {
                    unequip(item);
                } else {
                    equip(item);
                }
            } else if (button.id[10] == 'u') {
                protinst_use_item(owner, item);
                if (owner == obj_dude) {
                    intface_update_items(false);
                }
            } else {
                if (equipped) {
                    unequip(item);
                }
                obj_drop(owner, item);
                selected = -1;
            }
        }
    }

} // namespace

int muiInventoryScreenRun(Object* owner)
{
    if (!muiIsEnabled() || owner == nullptr) {
        return -1;
    }
    InventoryScreen screen(owner);
    muiRunModal(&screen);
    return 0;
}

MuiScreen* muiInventoryCreate()
{
    return new InventoryScreen(obj_dude);
}

} // namespace fallout
