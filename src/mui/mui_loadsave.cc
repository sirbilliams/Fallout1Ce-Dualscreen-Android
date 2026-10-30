#include "mui/mui.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"

#include <stdio.h>

#include <algorithm>

namespace fallout {

namespace {

    // Slot picker of the F1 save/load window (10 slots). The classic loop in
    // loadsave.cc runs the action: the choice here becomes its slot cursor and
    // its Done (500) or Cancel (501) key.
    class LoadSaveScreen : public MuiScreen {
    public:
        LoadSaveScreen(bool saving, std::vector<MuiSaveSlot> slots, int cursor)
            : saving(saving)
            , slots(std::move(slots))
            , selected(cursor)
        {
            modal = true;
        }

        MuiSurface surface() override { return muiGameScreenSurface(); }
        bool coversScreen() override { return true; }
        void back() override { choice = kCancel; }
        void build(MuiContext& ui) override;

        static constexpr int kCancel = -2;
        static constexpr int kNone = -1;
        // A slot chosen to act on: 0..9.
        int choice = kNone;

    private:
        bool saving;
        std::vector<MuiSaveSlot> slots;
        int selected;
    };

    void LoadSaveScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        bool back = false;
        MuiRect content = muiScreenFrame(ui, saving ? U"SAVE GAME" : U"LOAD GAME", &back);
        if (back) {
            choice = kCancel;
        }

        float actionHeight = ui.dp(54.0f);
        MuiRect actions = { content.x, content.bottom() - actionHeight, content.w, actionHeight };
        MuiRect list = { content.x, content.y, content.w, actions.y - content.y - ui.dp(10.0f) };

        std::vector<MuiRow> rows;
        for (size_t index = 0; index < slots.size(); index++) {
            const MuiSaveSlot& slot = slots[index];
            char number[16];
            snprintf(number, sizeof(number), "%02d  ", static_cast<int>(index) + 1);
            MuiRow row;
            if (slot.empty) {
                row.left = muiDecodeUtf8(number) + U"- EMPTY -";
                row.dim = true;
            } else if (slot.broken) {
                row.left = muiDecodeUtf8(number) + U"- UNUSABLE -";
                row.dim = true;
            } else {
                row.left = muiDecodeUtf8(number) + muiDecodeGameText(slot.description.c_str());
                row.right = muiDecodeGameText(slot.place.c_str());
            }
            row.selected = static_cast<int>(index) == selected;
            rows.push_back(row);
        }
        int tapped = muiListView(ui, "loadsave.list", list, rows, 54.0f);
        if (tapped >= 0) {
            selected = tapped;
        }

        // Details of the chosen slot, over the list's end.
        bool usable = selected >= 0 && selected < static_cast<int>(slots.size()) && (saving || (!slots[selected].empty && !slots[selected].broken));
        float gap = ui.dp(10.0f);
        float width = (actions.w - gap) / 2.0f;
        if (ui.button("loadsave.go", { actions.x, actions.y, width, actions.h }, saving ? U"SAVE HERE" : U"LOAD", MuiButtonStyle::Primary) && usable) {
            choice = selected;
        }
        if (ui.button("loadsave.cancel", { actions.x + width + gap, actions.y, width, actions.h }, U"CANCEL")) {
            choice = kCancel;
        }
        (void)theme;
    }

    LoadSaveScreen* gScreen = nullptr;

} // namespace

void muiLoadSaveShow(bool saving, const std::vector<MuiSaveSlot>& slots, int cursor)
{
    muiLoadSaveHide();
    if (!muiIsEnabled()) {
        return;
    }
    gScreen = new LoadSaveScreen(saving, slots, cursor);
    muiPush(gScreen);
}

void muiLoadSaveHide()
{
    if (gScreen == nullptr) {
        return;
    }
    muiRemove(gScreen);
    delete gScreen;
    gScreen = nullptr;
}

int muiLoadSaveTakeChoice()
{
    if (gScreen == nullptr || gScreen->choice == LoadSaveScreen::kNone) {
        return -1;
    }
    int choice = gScreen->choice;
    gScreen->choice = LoadSaveScreen::kNone;
    return choice == LoadSaveScreen::kCancel ? -2 : choice;
}

MuiScreen* muiLoadSaveCreate()
{
    std::vector<MuiSaveSlot> slots(10);
    for (int index = 0; index < 10; index++) {
        slots[index].empty = index > 2;
        slots[index].description = index == 0 ? "Vault 13" : (index == 1 ? "Shady Sands" : "Junktown");
        slots[index].place = index == 0 ? "VAULT 13" : "DAY 12";
    }
    return new LoadSaveScreen(false, slots, 0);
}

} // namespace fallout
