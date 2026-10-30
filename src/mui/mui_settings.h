#ifndef FALLOUT_MUI_SETTINGS_H_
#define FALLOUT_MUI_SETTINGS_H_

#include <string>

namespace fallout {

// Presentation settings of the mobile UI, read from fallout.cfg [touch]
// (keys: hud_scale, bottom_theme, bottom_text_size, reduce_motion,
// colour_blind_safe, dual_screen). bottom_theme: 0 = classic Fallout,
// 2 = Pip-Boy 2000 (default when the key is missing). Values 1 is treated as 0.
struct MuiTouchSettings {
    int hud_scale = 100;
    int bottom_theme = 2;
    int bottom_text_size = 100;
    bool reduce_motion = false;
    bool colour_blind_safe = false;
    bool dual_screen = true;
    // Mouse controls (fallout.cfg [touch]): direct_touch 1 = the cursor
    // follows the finger on the top screen (0: relative, trackpad style),
    // pad_cursor 1 = right stick moves the cursor, cursor_speed 50..200 %,
    // long_press_ms (R3 held = right click) 200..1000, touch_hold_ms (finger
    // held still on the top screen = right click) 300..1500, 0 = off.
    bool direct_touch = true;
    bool pad_cursor = true;
    std::string pad_bindings;
    int cursor_speed = 100;
    int long_press_ms = 400;
    int touch_hold_ms = 600;
};

struct MuiSettings {
    MuiTouchSettings touch;
};

extern MuiSettings settings;

// Re-reads fallout.cfg [touch] (cheap; called once per frame).
void muiSettingsRefresh();
void muiSettingsSaveDisplay();

// Saves the mouse control settings (the same path as the theme).
void muiSettingsSetMouse(bool persist = true);

// Saves the chosen theme (0 or 2) to fallout.cfg [touch] bottom_theme.
void muiSettingsSetTheme(int theme, bool persist = true);

} // namespace fallout

#endif
