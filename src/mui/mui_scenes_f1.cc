#include "mui/mui_screens.h"

namespace fallout {

static const MuiSceneEntry kScenes[] = {
    { "01-main-menu", muiMainMenuCreate },
    { "02-options", muiOptionsCreate },
    { nullptr, nullptr },
};

const MuiSceneEntry* muiSceneEntries() { return kScenes; }

} // namespace fallout
