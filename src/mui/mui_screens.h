#ifndef FALLOUT_MUI_SCREENS_H_
#define FALLOUT_MUI_SCREENS_H_

#include "mui/mui.h"

namespace fallout {

// Backdrop, title bar (with a Back button when [back] is given: set when
// tapped) of a full screen; returns the content area under it.
MuiRect muiScreenFrame(MuiContext& ui, const std::u32string& title, bool* back = nullptr);

struct MuiRow {
    std::u32string left;
    std::u32string right;
    // Section title: accent color, not tappable.
    bool header = false;
    // The chosen row.
    bool selected = false;
    // Dim (unavailable) row.
    bool dim = false;
    // Optional art (an inventory picture fid) before the text, -1 none.
    int icon = -1;
};

// A scrolling list in a well. Returns the index of the tapped row, -1 none.
// [rowHeightDp] is the row height in dp.
int muiListView(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::vector<MuiRow>& rows, float rowHeightDp = 38.0f);

// A labelled readout line (label left, value right) drawn in [rect].
// The player's name for headers; "Vault Dweller" while the character is unnamed ("None").
std::u32string muiPlayerName();
// Port version for the About page, the idle screens ("1.0.0 (abc1234)").
std::string muiPortVersion();

void muiReadout(MuiContext& ui, const MuiRect& rect, const std::u32string& label, const std::u32string& value);

// Ported screens that can be created without opening an F1 modal (used by the
// autotest and by the engine hooks).
struct MuiSceneEntry {
    const char* name;
    MuiScreen* (*make)();
    // Needs a loaded map and player (opened after the menu scenes).
    bool needsGame;
};

MuiScreen* muiMainMenuCreate();
MuiScreen* muiOptionsCreate();
MuiScreen* muiSettingsCreate(int tab);
MuiScreen* muiCharacterCreate();
MuiScreen* muiInventoryCreate();
MuiScreen* muiLoadSaveCreate();
MuiScreen* muiDialogueCreate();
MuiScreen* muiBarterCreate();
MuiScreen* muiPipboyCreate();

// Terminated by { nullptr, nullptr }.
const MuiSceneEntry* muiSceneEntries();

} // namespace fallout

#endif
