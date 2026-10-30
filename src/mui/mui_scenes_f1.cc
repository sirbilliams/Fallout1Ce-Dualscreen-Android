#include "mui/mui_screens.h"

namespace fallout {

static const MuiSceneEntry kScenes[] = {
    { "01-main-menu", muiMainMenuCreate, false },
    { "02-options", muiOptionsCreate, false },
    { "08-settings-game", []() { return muiSettingsCreate(0); }, false },
    { "08-settings-sound", []() { return muiSettingsCreate(1); }, false },
    { "08-settings-display", []() { return muiSettingsCreate(2); }, false },
    { "08-settings-controls", []() { return muiSettingsCreate(3); }, false },
    { "08-settings-about", []() { return muiSettingsCreate(4); }, false },
    { "03-character", muiCharacterCreate, true },
    { "05-loadsave", muiLoadSaveCreate, false },
    { "06-dialogue", muiDialogueCreate, false },
    { "06b-barter", muiBarterCreate, false },
    { "07-pipboy", muiPipboyCreate, false },
    { "04-inventory", muiInventoryCreate, true },
    { nullptr, nullptr, false },
};

const MuiSceneEntry* muiSceneEntries() { return kScenes; }

} // namespace fallout
