#ifndef FALLOUT_MUI_PAGES_H_
#define FALLOUT_MUI_PAGES_H_

#include "mui/mui.h"

namespace fallout {

// Pages of the bottom screen's console (mui_console.cc), each built into the
// console's slot while it is open. They only read the game; what they do goes
// out as MobileCommands (mobile_contracts.h) that the game's input loops carry
// out.

// Live map (mui_live_map.cc): the local map around dude with critters, items,
// doors, exits and a legend. [open] is cleared by its Back button. The
// AUTOMAP button there opens the game's own automap (on the top screen).
void muiLiveMapPage(MuiContext& ui, bool* open);

// Combat panel (mui_combat_page.cc): targets with hit chances, aimed shots,
// attack modes, quick items, AP, END TURN / END COMBAT. Takes the console's
// slot for dude's combat; false when it isn't there (not in combat, or
// not in combat), so the caller builds the console. [request] is set to 1 for
// the live map, 2 for the touchpad.
bool muiCombatPageActive();
void muiCombatPage(MuiContext& ui, int* request);
// A new fight shows the panel again after the player left it for the console.
void muiCombatPageReset();

} // namespace fallout

#endif /* FALLOUT_MUI_PAGES_H_ */
