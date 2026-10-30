#include "mui/mui.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"
#include "mui/mui_settings.h"

#include <algorithm>
#include <stdio.h>

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
        void back() override
        {
            if (controls) {
                controls = false;
            } else {
                choice = 504;
            }
        }
        void build(MuiContext& ui) override
        {
            if (controls) {
                buildControls(ui);
                return;
            }
            bool backTapped = false;
            MuiRect content = muiScreenFrame(ui, U"OPTIONS", &backTapped);
            if (backTapped) {
                choice = 504;
            }

            float gap = ui.dp(12.0f);
            float width = std::min(content.w, ui.dp(520.0f));
            float x = content.centerX() - width / 2.0f;
            float buttonHeight = std::min(ui.dp(58.0f), (content.h - gap * 8.0f) / 8.0f);
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
                { "options.controls", "CONTROLS", 505 },
                { "options.exit", "EXIT GAME", 503 },
                { "options.done", "DONE", 504 },
            };
            for (const Entry& entry : entries) {
                if (ui.button(entry.id, { x, y, width, buttonHeight }, muiDecodeUtf8(entry.label), entry.code == 504 ? MuiButtonStyle::Primary : MuiButtonStyle::Normal)) {
                    if (entry.code == 505) {
                        controls = true;
                    } else {
                        choice = entry.code;
                    }
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

        // The mouse controls page: how the cursor follows touch and the stick
        // (fallout.cfg [touch], saved at once like the theme).
        void buildControls(MuiContext& ui)
        {
            bool backTapped = false;
            MuiRect content = muiScreenFrame(ui, U"CONTROLS", &backTapped);
            if (backTapped) {
                controls = false;
                return;
            }

            MuiTouchSettings& t = settings.touch;
            float gap = ui.dp(10.0f);
            float width = std::min(content.w, ui.dp(520.0f));
            float x = content.centerX() - width / 2.0f;
            float rowHeight = std::min(ui.dp(60.0f), (content.h - gap * 6.0f) / 5.0f);
            float y = content.y + gap;
            bool changed = false;
            bool anyHeld = false;

            auto toggleRow = [&](const char* id, const char* label, bool* value) {
                MuiRect row = { x, y, width, rowHeight };
                muiDrawTextAligned(muiDecodeUtf8(label), { row.x, row.y, row.w - ui.dp(96.0f), row.h }, ui.dp(14.0f), muiTheme().text, MuiAlign::Start, MuiAlign::Center);
                if (ui.toggle(id, { row.right() - ui.dp(88.0f), row.y, ui.dp(88.0f), row.h }, value)) {
                    changed = true;
                }
                y += rowHeight + gap;
            };
            auto sliderRow = [&](const char* id, const char* label, int* value, int low, int high, int step, const char* offText) {
                MuiRect row = { x, y, width, rowHeight };
                char text[64];
                if (offText != nullptr && *value <= low + 50) {
                    snprintf(text, sizeof(text), "%s: %s", label, offText);
                } else {
                    snprintf(text, sizeof(text), "%s: %d", label, *value);
                }
                muiDrawTextAligned(muiDecodeUtf8(text), { row.x, row.y, row.w, row.h * 0.5f }, ui.dp(13.0f), muiTheme().text, MuiAlign::Start, MuiAlign::Center);
                float current = static_cast<float>(*value);
                bool held = false;
                float next = ui.slider(id, { row.x, row.y + row.h * 0.62f, row.w, row.h * 0.3f }, current, static_cast<float>(low), static_cast<float>(high), &held);
                anyHeld = anyHeld || held;
                int stepped = static_cast<int>(next / step + (next >= 0 ? 0.5f : -0.5f)) * step;
                if (stepped < low) stepped = low;
                if (stepped > high) stepped = high;
                // Persist once the finger is lifted: the value is saved on change below.
                if (stepped != *value) {
                    *value = stepped;
                    changed = true;
                }
                y += rowHeight + gap;
            };

            toggleRow("controls.direct", "TOUCH: CURSOR FOLLOWS FINGER", &t.direct_touch);
            toggleRow("controls.stick", "RIGHT STICK MOVES CURSOR", &t.pad_cursor);
            sliderRow("controls.speed", "CURSOR SPEED %", &t.cursor_speed, 50, 200, 10, nullptr);
            sliderRow("controls.r3", "R3 LONG PRESS (MS)", &t.long_press_ms, 200, 1000, 50, nullptr);
            // 300..1500 or off at 0: the low end of the slider is off.
            int hold = t.touch_hold_ms == 0 ? 200 : t.touch_hold_ms;
            int before = hold;
            sliderRow("controls.hold", "TOUCH HOLD = RIGHT CLICK (MS)", &hold, 200, 1500, 50, "OFF");
            if (hold != before) {
                t.touch_hold_ms = hold < 300 ? 0 : hold;
            }

            // Sliders save when the finger lets go, not on every step.
            if (changed) {
                dirty = true;
                muiSettingsSetMouse(false);
            }
            if (dirty && !anyHeld) {
                muiSettingsSetMouse(true);
                dirty = false;
            }
        }

        bool controls = false;
        bool dirty = false;
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
