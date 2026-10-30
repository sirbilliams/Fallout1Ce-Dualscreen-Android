#include "mui/mui_screens.h"

#include <algorithm>
#include <string.h>

#include "mui/mui_gameskin.h"
#include "mui/mui_skin.h"

#include "game/critter.h"
#include "game/object.h"

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

int muiListView(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::vector<MuiRow>& rows, float rowHeightDp)
{
    const MuiTheme& theme = muiTheme();
    ui.panel(rect);
    MuiRect inner = rect.inset(ui.dp(6.0f));
    float rowHeight = ui.dp(rowHeightDp);
    float offset = ui.scroll(id, inner, rows.size() * rowHeight);
    int tapped = -1;

    muiPushClip(inner);
    for (size_t index = 0; index < rows.size(); index++) {
        const MuiRow& row = rows[index];
        MuiRect line = { inner.x, inner.y + index * rowHeight - offset, inner.w, rowHeight };
        if (line.bottom() < inner.y || line.y > inner.bottom()) {
            continue;
        }
        if (row.selected) {
            muiFillRect(line, theme.selected);
        }
        if (!row.header && ui.touchable(id + "." + std::to_string(index), { line.x, std::max(line.y, inner.y), line.w, std::min(line.bottom(), inner.bottom()) - std::max(line.y, inner.y) })) {
            tapped = static_cast<int>(index);
        }
        MuiColor color = row.header ? theme.accent : (row.dim ? theme.textDim : theme.text);
        float textX = line.x + ui.dp(10.0f);
        if (row.icon != -1) {
            float side = rowHeight - ui.dp(6.0f);
            int artWidth;
            int artHeight;
            SDL_Texture* texture = muiArtTexture(row.icon, 0, &artWidth, &artHeight);
            if (texture != nullptr && artWidth > 0 && artHeight > 0) {
                SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
                muiDrawTexture(texture, muiFitRect({ textX, line.y + ui.dp(3.0f), side * 1.3f, side }, static_cast<float>(artWidth), static_cast<float>(artHeight)), { 255, 255, 255, 255 });
            }
            textX += side * 1.4f;
        }
        float size = ui.dp(row.header ? 15.0f : 17.0f);
        float rightWidth = row.right.empty() ? 0.0f : muiTextWidth(row.right, size) + ui.dp(12.0f);
        MuiRect leftRect = { textX, line.y, line.right() - textX - rightWidth - ui.dp(8.0f), line.h };
        muiDrawTextEllipsis(row.left, leftRect, size, color, MuiAlign::Start, MuiAlign::Center);
        if (!row.right.empty()) {
            muiDrawTextAligned(row.right, { line.x, line.y, line.w - ui.dp(10.0f), line.h }, size, color, MuiAlign::End, MuiAlign::Center);
        }
    }
    muiPopClip();

    // A position bar when more rows hide beyond the well.
    float total = rows.size() * rowHeight;
    if (total > inner.h + 1.0f) {
        MuiInk ink = muiSkinInk();
        MuiRect bar = { inner.right() - ui.dp(4.0f), inner.y + ui.dp(2.0f), ui.dp(3.0f), inner.h - ui.dp(4.0f) };
        muiFillRect(bar, ink.dim.withAlpha(60));
        float thumb = std::max(bar.h * inner.h / total, ui.dp(20.0f));
        muiFillRect({ bar.x, bar.y + (bar.h - thumb) * std::clamp(offset / (total - inner.h), 0.0f, 1.0f), bar.w, thumb }, ink.accent);
    }
    return tapped;
}

std::u32string muiPlayerName()
{
    const char* name = obj_dude != nullptr ? critter_name(obj_dude) : nullptr;
    if (name == nullptr || name[0] == '\0' || strcmp(name, "None") == 0) {
        return U"Vault Dweller";
    }
    return muiDecodeGameText(name);
}

void muiReadout(MuiContext& ui, const MuiRect& rect, const std::u32string& label, const std::u32string& value)
{
    const MuiTheme& theme = muiTheme();
    float size = ui.dp(17.0f);
    muiDrawTextAligned(label, rect, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
    muiDrawTextAligned(value, rect, size, theme.text, MuiAlign::End, MuiAlign::Center);
}

} // namespace fallout
