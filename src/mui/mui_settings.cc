#include "mui/mui_settings.h"

#include "game/config.h"
#include "game/gconfig.h"

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
}

void muiSettingsSetTheme(int theme)
{
    config_set_value(&game_config, "touch", "bottom_theme", theme == 2 ? 2 : 0);
    settings.touch.bottom_theme = theme == 2 ? 2 : 0;
}

} // namespace fallout
