#include "mui/mui_screens.h"

namespace fallout {

static const MuiSceneEntry kScenes[] = {
    { "01-main-menu", muiMainMenuCreate, false },
    { "02-options", muiOptionsCreate, false },
    { "03-character", muiCharacterCreate, true },
    { "05-loadsave", muiLoadSaveCreate, false },
    { "04-inventory", muiInventoryCreate, true },
    { nullptr, nullptr, false },
};

const MuiSceneEntry* muiSceneEntries() { return kScenes; }

} // namespace fallout
