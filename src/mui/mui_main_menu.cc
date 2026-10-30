#include "mui/mui.h"
#include "mui/mui_screens.h"

#include <algorithm>
#include <string>

#include "game/mainmenu.h"
#include "game/version.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_skin.h"

namespace fallout {

namespace {

    // Title menu of Fallout 1: the game's menu picture and the five buttons of
    // the classic menu (the menu's loop runs the choice).
    class MainMenuScreen : public MuiScreen {
    public:
        MainMenuScreen() { modal = true; }

        MuiSurface surface() override { return muiGameScreenSurface(); }
        bool coversScreen() override { return true; }
        void back() override { choice = MAIN_MENU_EXIT; }
        void build(MuiContext& ui) override;

        int choice = -1;
    };

    void MainMenuScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        MuiRect screen = ui.screenRect();
        MuiRect safe = ui.safeRect();
        bool wide = screen.w >= screen.h;

        muiFillRect(screen, muiRgb(0x000000));

        // The game's picture, fitted into the part not taken by the buttons.
        int artWidth = 0;
        int artHeight = 0;
        MuiRect column;
        MuiRect picture;
        float margin = ui.dp(20.0f);
        if (wide) {
            float width = std::min(ui.dp(300.0f), safe.w * 0.4f);
            column = { safe.x + margin, safe.y + margin, width, safe.h - margin * 2.0f };
            picture = { column.right() + margin, safe.y, safe.right() - column.right() - margin, safe.h };
        } else {
            column = { safe.x + margin, safe.y + safe.h * 0.42f, safe.w - margin * 2.0f, safe.h * 0.58f - margin };
            picture = { safe.x, safe.y, safe.w, safe.h * 0.42f };
        }
        if (muiGameArtSize(MuiArtGroup::Interface, "mainmenu", &artWidth, &artHeight) && artWidth > 0) {
            MuiRect fit = muiFitRect(picture.inset(ui.dp(6.0f)), static_cast<float>(artWidth), static_cast<float>(artHeight));
            muiGameDrawArt(MuiArtGroup::Interface, "mainmenu", fit);
            // The picture's riveted plate was left blank for the original's button
            // labels: a readout in it carries the version and what to do.
            MuiRect plate = { fit.x + fit.w * 0.632f, fit.y + fit.h * 0.05f, fit.w * 0.325f, fit.h * 0.56f };
            muiSkinWell(ui, plate);
            MuiInk ink = muiSkinInk();
            MuiRect text = plate.inset(ui.dp(3.0f), ui.dp(6.0f));
            char engine[VERSION_MAX];
            getverstr(engine, sizeof(engine));
            std::string port = muiPortVersion();
            size_t space = port.find(' ');
            std::u32string lines[] = { muiDecodeGameText(engine), U"Dual screen", U"Port " + muiDecodeUtf8(port.substr(0, space).c_str()), space == std::string::npos ? U"" : muiDecodeUtf8(port.substr(space + 1).c_str()), U"Pick one", U"to begin" };
            float lineHeight = text.h / 6.0f;
            for (int index = 0; index < 6; index++) {
                MuiRect line = { text.x, text.y + lineHeight * index, text.w, lineHeight };
                muiDrawTextEllipsis(lines[index], line, muiFitTextSize(lines[index], line.w, ui.dp(10.5f), ui.dp(10.0f)), index == 0 ? ink.accent : (index >= 4 ? ink.text : ink.dim), MuiAlign::Center, MuiAlign::Center);
            }
        }

        float gap = ui.dp(10.0f);
        float buttonHeight = std::min(ui.dp(52.0f), (column.h - gap * 5.0f) / 6.0f);

        struct Button {
            const char* id;
            const char* label;
            int choice;
        };
        const Button buttons[] = {
            { "mainmenu.new", "NEW GAME", MAIN_MENU_NEW_GAME },
            { "mainmenu.load", "LOAD GAME", MAIN_MENU_LOAD_GAME },
            { "mainmenu.intro", "INTRO", MAIN_MENU_INTRO },
            { "mainmenu.credits", "CREDITS", MAIN_MENU_CREDITS },
            { "mainmenu.settings", "SETTINGS", -2 },
            { "mainmenu.exit", "EXIT", MAIN_MENU_EXIT },
        };
        float height = (buttonHeight + gap) * 6.0f - gap;
        float y = column.y + std::max((column.h - height) / 2.0f, 0.0f);
        for (const Button& button : buttons) {
            MuiRect rect = { column.x, y, column.w, buttonHeight };
            if (ui.button(button.id, rect, muiDecodeUtf8(button.label), button.choice == MAIN_MENU_NEW_GAME ? MuiButtonStyle::Primary : MuiButtonStyle::Normal)) {
                if (button.choice == -2)
                    muiSettingsShow();
                else
                    choice = button.choice;
            }
            y += buttonHeight + gap;
        }

        char version[VERSION_MAX];
        getverstr(version, sizeof(version));
        std::u32string text = muiDecodeGameText(version);
        MuiRect footer = { column.x, column.bottom() - ui.dp(22.0f), column.w, ui.dp(22.0f) };
        muiDrawTextAligned(text, footer, muiFitTextSize(text, footer.w, ui.dp(11.0f), ui.dp(8.0f)), theme.textDim, MuiAlign::Start, MuiAlign::End);
    }

    MainMenuScreen* gMainMenuScreen = nullptr;

} // namespace

void muiMainMenuShow()
{
    if (gMainMenuScreen != nullptr || !muiIsEnabled()) {
        return;
    }
    gMainMenuScreen = new MainMenuScreen();
    muiPush(gMainMenuScreen);
}

void muiMainMenuHide()
{
    if (gMainMenuScreen == nullptr) {
        return;
    }
    muiRemove(gMainMenuScreen);
    delete gMainMenuScreen;
    gMainMenuScreen = nullptr;
}

int muiMainMenuTakeChoice()
{
    if (gMainMenuScreen == nullptr) {
        return -1;
    }
    int choice = gMainMenuScreen->choice;
    gMainMenuScreen->choice = -1;
    return choice;
}

MuiScreen* muiMainMenuCreate()
{
    return new MainMenuScreen();
}

} // namespace fallout
