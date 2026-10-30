#ifndef FALLOUT_MUI_SCREENS_H_
#define FALLOUT_MUI_SCREENS_H_

#include "mui/mui.h"

namespace fallout {

// Ported screens that can be created without opening an F1 modal (used by the
// autotest and by the engine hooks).
struct MuiSceneEntry {
    const char* name;
    MuiScreen* (*make)();
};

// Terminated by { nullptr, nullptr }.
const MuiSceneEntry* muiSceneEntries();

} // namespace fallout

#endif
