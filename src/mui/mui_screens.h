#ifndef FALLOUT_MUI_SCREENS_H_
#define FALLOUT_MUI_SCREENS_H_

#include "mui/mui.h"

namespace fallout {

// Backdrop, title bar (with a Back button when [back] is given: set when
// tapped) of a full screen; returns the content area under it.
MuiRect muiScreenFrame(MuiContext& ui, const std::u32string& title, bool* back = nullptr);

// Ported screens that can be created without opening an F1 modal (used by the
// autotest and by the engine hooks).
struct MuiSceneEntry {
    const char* name;
    MuiScreen* (*make)();
};

MuiScreen* muiMainMenuCreate();
MuiScreen* muiOptionsCreate();

// Terminated by { nullptr, nullptr }.
const MuiSceneEntry* muiSceneEntries();

} // namespace fallout

#endif
