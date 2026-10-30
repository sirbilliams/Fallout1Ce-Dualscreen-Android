#include "mui/mui_settings.h"

#include "game/config.h"
#include "game/gconfig.h"
#include "plib/gnw/pad_actions.h"

namespace fallout {

MuiSettings settings;

void muiSettingsRefresh()
{
    int value;
    MuiTouchSettings& t = settings.touch;
    t.hud_scale = 100;
    if (config_get_value(&game_config, "touch", "hud_scale", &value) && value >= 50 && value <= 300) t.hud_scale = value;
    t.bottom_theme = 2;
    if (config_get_value(&game_config, "touch", "bottom_theme", &value)) t.bottom_theme = value == 2 ? 2 : 0;
    t.bottom_text_size = 100;
    if (config_get_value(&game_config, "touch", "bottom_text_size", &value) && value >= 50 && value <= 300) t.bottom_text_size = value;
    t.reduce_motion = config_get_value(&game_config, "touch", "reduce_motion", &value) && value != 0;
    t.colour_blind_safe = config_get_value(&game_config, "touch", "colour_blind_safe", &value) && value != 0;
    t.dual_screen = true;
    if (config_get_value(&game_config, "touch", "bottom_screen", &value)) t.dual_screen = value != 0;
    t.direct_touch = !(config_get_value(&game_config, "touch", "direct_touch", &value) && value == 0);
    t.pad_cursor = !(config_get_value(&game_config, "touch", "pad_cursor", &value) && value == 0);
    char* bindings = nullptr;
    t.pad_bindings = config_get_string(&game_config, "touch", "pad_bindings", &bindings) ? bindings : "";
    t.cursor_speed = 100;
    if (config_get_value(&game_config, "touch", "cursor_speed", &value) && value >= 50 && value <= 200) t.cursor_speed = value;
    t.long_press_ms = 400;
    if (config_get_value(&game_config, "touch", "long_press_ms", &value) && value >= 200 && value <= 1000) t.long_press_ms = value;
    t.touch_hold_ms = 600;
    if (config_get_value(&game_config, "touch", "touch_hold_ms", &value) && (value == 0 || (value >= 300 && value <= 1500))) t.touch_hold_ms = value;
}

void muiSettingsSetMouse(bool persist)
{
    int bindings[kPadButtonCount];
    padParseBindings(settings.touch.pad_bindings, bindings);
    settings.touch.pad_bindings = padSerializeBindings(bindings);
    const MuiTouchSettings& t = settings.touch;
    config_set_string(&game_config, "touch", "pad_bindings", t.pad_bindings.c_str());
    config_set_value(&game_config, "touch", "direct_touch", t.direct_touch ? 1 : 0);
    config_set_value(&game_config, "touch", "pad_cursor", t.pad_cursor ? 1 : 0);
    config_set_value(&game_config, "touch", "cursor_speed", t.cursor_speed);
    config_set_value(&game_config, "touch", "long_press_ms", t.long_press_ms);
    config_set_value(&game_config, "touch", "touch_hold_ms", t.touch_hold_ms);
    if (persist) {
        gconfig_save();
    }
}

void muiSettingsSetTheme(int theme, bool persist)
{
    config_set_value(&game_config, "touch", "bottom_theme", theme == 2 ? 2 : 0);
    settings.touch.bottom_theme = theme == 2 ? 2 : 0;
    if (persist) {
        // The game's own config write path: fallout.cfg [touch] bottom_theme.
        gconfig_save();
    }
}

} // namespace fallout
