#include "mui/mui.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"
#include "mui/mui_settings.h"

#include <algorithm>

namespace fallout {

namespace {

    // Option menu: the F1 options window's choices as key codes the loop in
    // options.cc understands (500 save, 501 load, 502 preferences, 503 exit,
    // 504 done).
    class OptionsScreen : public MuiScreen {
    public:
        OptionsScreen() { modal = true; }

        MuiSurface surface() override { return muiGameScreenSurface(); }
        bool coversScreen() override { return true; }
        void back() override { choice = 504; }
        void build(MuiContext& ui) override
        {
            bool backTapped = false;
            MuiRect content = muiScreenFrame(ui, U"OPTIONS", &backTapped);
            if (backTapped) {
                choice = 504;
            }

            float gap = ui.dp(12.0f);
            float width = std::min(content.w, ui.dp(520.0f));
            float x = content.centerX() - width / 2.0f;
            float buttonHeight = std::min(ui.dp(58.0f), (content.h - gap * 7.0f) / 7.0f);
            float y = content.y + gap;

            struct Entry {
                const char* id;
                const char* label;
                int code;
            };
            const Entry entries[] = {
                { "options.save", "SAVE GAME", 500 },
                { "options.load", "LOAD GAME", 501 },
                { "options.prefs", "PREFERENCES", 502 },
                { "options.exit", "EXIT GAME", 503 },
                { "options.done", "DONE", 504 },
            };
            for (const Entry& entry : entries) {
                if (ui.button(entry.id, { x, y, width, buttonHeight }, muiDecodeUtf8(entry.label), entry.code == 504 ? MuiButtonStyle::Primary : MuiButtonStyle::Normal)) {
                    choice = entry.code;
                }
                y += buttonHeight + gap;
            }

            // Theme: Pip-Boy 2000 or classic Fallout.
            MuiRect label = { x, y, width, ui.dp(22.0f) };
            muiDrawTextAligned(U"SCREEN THEME", label, ui.dp(15.0f), muiTheme().textDim, MuiAlign::Start, MuiAlign::Center);
            int selected = settings.touch.bottom_theme == 2 ? 0 : 1;
            if (ui.segmented("options.theme", { x, label.bottom() + ui.dp(4.0f), width, buttonHeight * 0.8f }, { U"PIP-BOY 2000", U"CLASSIC" }, &selected)) {
                muiSettingsSetTheme(selected == 0 ? 2 : 0);
            }
        }

        int choice = -1;
    };

    OptionsScreen* gOptionsScreen = nullptr;

} // namespace

void muiOptionsShow()
{
    if (gOptionsScreen != nullptr || !muiIsEnabled()) {
        return;
    }
    gOptionsScreen = new OptionsScreen();
    muiPush(gOptionsScreen);
}

void muiOptionsHide()
{
    if (gOptionsScreen == nullptr) {
        return;
    }
    muiRemove(gOptionsScreen);
    delete gOptionsScreen;
    gOptionsScreen = nullptr;
}

int muiOptionsTakeChoice()
{
    if (gOptionsScreen == nullptr) {
        return -1;
    }
    int choice = gOptionsScreen->choice;
    gOptionsScreen->choice = -1;
    return choice;
}

MuiScreen* muiOptionsCreate()
{
    return new OptionsScreen();
}

} // namespace fallout
