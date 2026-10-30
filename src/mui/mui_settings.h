#ifndef FALLOUT_MUI_SETTINGS_H_
#define FALLOUT_MUI_SETTINGS_H_

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
};

struct MuiSettings {
    MuiTouchSettings touch;
};

extern MuiSettings settings;

// Re-reads fallout.cfg [touch] (cheap; called once per frame).
void muiSettingsRefresh();

// Saves the chosen theme (0 or 2) to fallout.cfg [touch] bottom_theme.
void muiSettingsSetTheme(int theme);

} // namespace fallout

#endif
