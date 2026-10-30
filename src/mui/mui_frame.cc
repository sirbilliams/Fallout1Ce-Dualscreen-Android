#include "mui/mui_screens.h"

#include <algorithm>

#include "mui/mui_gameskin.h"
#include "mui/mui_skin.h"

namespace fallout {

MuiRect muiScreenFrame(MuiContext& ui, const std::u32string& title, bool* back)
{
    const MuiTheme& theme = muiTheme();
    muiSkinBackdrop(ui, ui.screenRect());

    MuiRect safe = ui.safeRect().inset(ui.dp(14.0f));
    float barHeight = ui.dp(56.0f);
    MuiRect bar = { safe.x, safe.y, safe.w, barHeight };
    muiSkinPlate(ui, bar, true);

    float backWidth = back != nullptr ? std::min(ui.dp(120.0f), bar.w * 0.3f) : 0.0f;
    MuiRect titleRect = { bar.x + ui.dp(16.0f), bar.y, bar.w - backWidth - ui.dp(36.0f), bar.h };
    float size = muiFitTextSize(title, titleRect.w, ui.dp(26.0f), ui.dp(14.0f));
    muiDrawTextAligned(title, titleRect, size, theme.accent, MuiAlign::Start, MuiAlign::Center);

    if (back != nullptr) {
        MuiRect rect = { bar.right() - backWidth - ui.dp(8.0f), bar.y + ui.dp(6.0f), backWidth, bar.h - ui.dp(12.0f) };
        if (ui.button("frame.back", rect, U"BACK")) {
            *back = true;
        }
    }

    return { safe.x, bar.bottom() + ui.dp(10.0f), safe.w, safe.bottom() - bar.bottom() - ui.dp(10.0f) };
}

} // namespace fallout
