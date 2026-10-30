#include "mui/mui.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"
#include "mui/mui_settings.h"

#include "game/config.h"
#include "game/gconfig.h"
#include "game/options.h"
#include "game/version.h"
#include "plib/gnw/pad_actions.h"
#include <SDL.h>
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
                if (standalone) {
                    muiRemove(this);
                    standaloneClosed = true;
                }
            } else {
                choice = 504;
            }
        }
        void build(MuiContext& ui) override
        {
            if (controls) {
                buildSettings(ui);
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
                        tab = 3;
                    } else {
                        choice = entry.code;
                    }
                }
                y += buttonHeight + gap;
            }
        }

        // The mouse controls page: how the cursor follows touch and the stick
        // (fallout.cfg [touch], saved at once like the theme).
        void buildControls(MuiContext& ui)
        {
            bool backTapped = false;
            MuiRect content = settingsContent;
            if (backTapped) {
                back();
                return;
            }

            MuiTouchSettings& t = settings.touch;
            float gap = ui.dp(10.0f);
            float width = std::min(content.w, ui.dp(520.0f));
            float x = content.centerX() - width / 2.0f;
            float rowHeight = ui.dp(60.0f);
            float stride = rowHeight + gap;
            if (padMoved) {
                ui.setScroll("controls", std::max(0.0f, padFocus * stride - content.h / 2));
                padMoved = false;
            }
            float offset = ui.scroll("controls", content, 22 * stride + gap);
            float y = content.y + gap - offset;
            muiPushClip(content);
            bool changed = false;
            bool anyHeld = false;
            int rowIndex = 0;

            auto toggleRow = [&](const char* id, const char* label, bool* value) {
                MuiRect row = { x, y, width, rowHeight };
                if (rowIndex == padFocus) muiFillRect(row, muiTheme().selected);
                muiDrawTextAligned(muiDecodeUtf8(label), { row.x, row.y, row.w - ui.dp(96.0f), row.h }, ui.dp(14.0f), muiTheme().text, MuiAlign::Start, MuiAlign::Center);
                if (padActivate && rowIndex == padFocus) {
                    *value = !*value;
                    changed = true;
                }
                rowIndex++;
                if (row.y >= content.y && row.bottom() <= content.bottom() && ui.toggle(id, { row.right() - ui.dp(88.0f), row.y, ui.dp(88.0f), row.h }, value)) {
                    changed = true;
                }
                y += rowHeight + gap;
            };
            auto sliderRow = [&](const char* id, const char* label, int* value, int low, int high, int step, const char* offText) {
                MuiRect row = { x, y, width, rowHeight };
                if (rowIndex == padFocus) muiFillRect(row, muiTheme().selected);
                char text[64];
                if (offText != nullptr && *value <= low + 50) {
                    snprintf(text, sizeof(text), "%s: %s", label, offText);
                } else {
                    snprintf(text, sizeof(text), "%s: %d", label, *value);
                }
                muiDrawTextAligned(muiDecodeUtf8(text), { row.x, row.y, row.w, row.h * 0.5f }, ui.dp(13.0f), muiTheme().text, MuiAlign::Start, MuiAlign::Center);
                if (padActivate && rowIndex == padFocus) {
                    *value = std::clamp(*value + step * padDelta, low, high);
                    changed = true;
                }
                rowIndex++;
                float current = static_cast<float>(*value);
                bool held = false;
                float next = current;
                if (row.y >= content.y && row.bottom() <= content.bottom()) next = ui.slider(id, { row.x, row.y + row.h * 0.62f, row.w, row.h * 0.3f }, current, static_cast<float>(low), static_cast<float>(high), &held);
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

            for (int b = 0; b <= kPadButtonCount; b++) {
                std::string id = "controls.binding." + std::to_string(b);
                std::string label = b == kPadButtonCount ? "RESET PAD TO DEFAULTS" : std::string(buttonLabels[b]) + "  —  " + actionLabels[padBinding(b)];
                MuiRect row = { x, y, width, rowHeight };
                bool visible = row.y >= content.y && row.bottom() <= content.bottom();
                bool activated = false;
                if (visible) activated = ui.button(id, row, muiDecodeUtf8(label.c_str()), padFocus == b + 5 ? MuiButtonStyle::Primary : MuiButtonStyle::Normal);
                if (visible && padFocus == b + 5) muiStrokeRoundRect(row.inset(ui.dp(2.0f)), ui.dp(4.0f), ui.dp(2.0f), muiTheme().accent);
                if (activated || (padActivate && padFocus == b + 5)) {
                    if (b == kPadButtonCount)
                        padResetBindings();
                    else
                        padSetBinding(b, (padBinding(b) + padDelta + padActionCount()) % padActionCount());
                }
                y += stride;
            }
            padActivate = false;
            padDelta = 1;
            muiPopClip();

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

        MuiRect settingsContent;
        bool standalone = false;
        bool standaloneClosed = false;
        int tab = 0;
        static constexpr const char* buttonLabels[] = { "A", "B", "X", "Y", "Start", "Select", "LB", "RB", "L3", "R3", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "L2", "R2" };
        static constexpr const char* actionLabels[] = { "Unbound", "Left click", "Right click", "Tap click / hold right click", "Back", "Game menu", "Inventory", "Skilldex", "Character", "Pip-Boy", "Automap", "Sneak", "Swap hands", "Item mode", "Start combat", "End turn", "End combat", "Quick save", "Quick load" };
        void buildSettings(MuiContext& ui)
        {
            bool close = false;
            MuiRect area = muiScreenFrame(ui, U"SETTINGS", &close);
            if (close) {
                back();
                return;
            }
            int previous = tab;
            ui.segmented("settings.tabs", { area.x, area.y, area.w, ui.dp(40) }, { U"Game", U"Sound", U"Display", U"Controls", U"About" }, &tab);
            if (previous != tab) {
                padFocus = 0;
                padMoved = true;
            }
            muiDrawTextAligned(tab < 2 ? U"Original game preferences • saved immediately" : U"Port settings • saved immediately", { area.x, area.y + ui.dp(42), area.w, ui.dp(24) }, ui.dp(11), muiTheme().textDim, MuiAlign::Start, MuiAlign::Center);
            settingsContent = { area.x, area.y + ui.dp(70), area.w, area.h - ui.dp(70) };
            if (tab == 3) {
                buildControls(ui);
                return;
            }
            struct Row {
                const char* label;
                int index;
                double low, high, step;
                const char* choices;
            };
            const Row game[] = {
                { "Game difficulty", 0, 0, 2, 1, "Easy|Normal|Hard" }, { "Combat difficulty", 1, 0, 2, 1, "Wimpy|Normal|Rough" },
                { "Violence", 2, 0, 3, 1, "None|Minimal|Normal|Maximum blood" }, { "Target highlight", 3, 0, 2, 1, "Off|On|Targeting only" },
                { "Combat looks", 4, 0, 1, 1, "Off|On" }, { "Combat messages", 5, 0, 1, 1, "Verbose|Brief" },
                { "Combat taunts", 6, 0, 1, 1, "Off|On" }, { "Language filter", 7, 0, 1, 1, "Off|On" },
                { "Running", 8, 0, 1, 1, "Normal|Always" }, { "Subtitles", 9, 0, 1, 1, "Off|On" },
                { "Item highlight", 10, 0, 1, 1, "Off|On" }, { "Combat speed", 11, 0, 50, 1, nullptr },
                { "Text delay (seconds)", 12, 1, 6, .25, nullptr }, { "Player speedup", 19, 0, 1, 1, "Off|On" },
                { "Brightness", 17, 1, 1.17999267578125, .01, nullptr }, { "Mouse sensitivity", 18, 1, 2.5, .1, nullptr }
            };
            const Row sound[] = { { "Master volume", 13, 0, 32767, 1024, nullptr }, { "Music volume", 14, 0, 32767, 1024, nullptr }, { "Sound effects volume", 15, 0, 32767, 1024, nullptr }, { "Speech volume", 16, 0, 32767, 1024, nullptr } };
            const Row display[] = { { "Theme", 0, 0, 1, 1, "Pip-Boy 2000|Classic" }, { "HUD scale (%)", 1, 50, 300, 10, nullptr }, { "Bottom text size (%)", 2, 50, 300, 10, nullptr }, { "Reduce motion", 3, 0, 1, 1, "Off|On" }, { "Colour-blind safe", 4, 0, 1, 1, "Off|On" }, { "Dual screen", 5, 0, 1, 1, "Off|On" } };
            if (tab == 4) {
                char version[VERSION_MAX];
                getverstr(version, sizeof(version));
                const std::string lines[] = { "Fallout 1 Community Edition", std::string("Version: ") + version, "Build: " __DATE__ " " __TIME__, "Source: sirbilliams/Fallout1Ce-Dualscreen-Android", "Engine and component licences: LICENSE.md and third_party" };
                for (int i = 0; i < 5; i++)
                    muiDrawTextAligned(muiDecodeUtf8(lines[i].c_str()), { area.x, settingsContent.y + i * ui.dp(32), area.w, ui.dp(30) }, ui.dp(12), muiTheme().text, MuiAlign::Start, MuiAlign::Center);
                if (ui.button("settings.source", { area.x, settingsContent.y + ui.dp(180), area.w, ui.dp(50) }, U"Open source repository") || padActivate) SDL_OpenURL("https://github.com/sirbilliams/Fallout1Ce-Dualscreen-Android");
                padActivate = false;
                return;
            }
            const Row* rows = tab == 0 ? game : tab == 1 ? sound
                                                         : display;
            int count = tab == 0 ? 16 : tab == 1 ? 4
                                                 : 6;
            float stride = ui.dp(68);
            if (padMoved) {
                ui.setScroll("settings.rows", std::max(0.f, padFocus * stride - settingsContent.h / 2));
                padMoved = false;
            }
            float offset = ui.scroll("settings.rows", settingsContent, (count + 1) * stride);
            muiPushClip(settingsContent);
            for (int i = 0; i <= count; ++i) {
                MuiRect rect = { area.x, settingsContent.y + i * stride - offset, area.w, ui.dp(58) };
                bool visible = rect.y >= settingsContent.y && rect.bottom() <= settingsContent.bottom();
                if (i == count) {
                    if ((visible && ui.button("settings.reset", rect, U"Reset to defaults")) || (padActivate && padFocus == i)) {
                        if (tab < 2)
                            optionsResetValues(tab == 1);
                        else {
                            auto defaults = MuiTouchSettings();
                            auto& t = settings.touch;
                            t.bottom_theme = defaults.bottom_theme;
                            t.hud_scale = defaults.hud_scale;
                            t.bottom_text_size = defaults.bottom_text_size;
                            t.reduce_motion = false;
                            t.colour_blind_safe = false;
                            t.dual_screen = true;
                            muiSettingsSaveDisplay();
                        }
                    }
                    continue;
                }
                const Row& row = rows[i];
                auto& t = settings.touch;
                double value = tab != 2 ? optionsGetValue(row.index) : i == 0 ? (t.bottom_theme == 2 ? 0 : 1)
                    : i == 1                                                  ? t.hud_scale
                    : i == 2                                                  ? t.bottom_text_size
                    : i == 3                                                  ? t.reduce_motion
                    : i == 4                                                  ? t.colour_blind_safe
                                                                              : t.dual_screen;
                double next = value;
                std::string text = row.label;
                if (row.choices) {
                    std::string choices = row.choices;
                    size_t begin = 0;
                    for (int n = 0; n < int(value); n++)
                        begin = choices.find('|', begin) + 1;
                    text += "  —  " + choices.substr(begin, choices.find('|', begin) - begin);
                } else {
                    char number[40];
                    snprintf(number, sizeof(number), "  %.2f", value);
                    text += number;
                }
                if (visible && ui.button("settings.row." + std::to_string(i), { rect.x, rect.y, rect.w, row.choices ? rect.h : rect.h / 2 }, muiDecodeUtf8(text.c_str()), padFocus == i ? MuiButtonStyle::Primary : MuiButtonStyle::Normal)) next = value + row.step > row.high ? row.low : value + row.step;
                if (!row.choices && visible) next = ui.slider("settings.slider." + std::to_string(i), { rect.x, rect.y + rect.h / 2, rect.w, rect.h / 2 }, next, row.low, row.high);
                if (padActivate && padFocus == i) {
                    next = value + row.step * padDelta;
                    if (row.choices) {
                        if (next > row.high) next = row.low;
                        if (next < row.low) next = row.high;
                    } else
                        next = std::clamp(next, row.low, row.high);
                }
                if (next != value) {
                    if (tab != 2)
                        optionsSetValue(row.index, next);
                    else {
                        if (i == 0)
                            t.bottom_theme = next == 0 ? 2 : 0;
                        else if (i == 1)
                            t.hud_scale = int(next);
                        else if (i == 2)
                            t.bottom_text_size = int(next);
                        else if (i == 3)
                            t.reduce_motion = next != 0;
                        else if (i == 4)
                            t.colour_blind_safe = next != 0;
                        else
                            t.dual_screen = next != 0;
                        muiSettingsSaveDisplay();
                    }
                }
            }
            muiPopClip();
            padActivate = false;
            padDelta = 1;
        }

        int padFocus = 0;
        int padDelta = 1;
        bool padMoved = false;
        bool padActivate = false;
        bool controls = false;
        bool dirty = false;
        int choice = -1;
    };

    OptionsScreen* gOptionsScreen = nullptr;
    OptionsScreen* gSettingsScreen = nullptr;

} // namespace

bool muiOptionsPadActive() { return (gSettingsScreen && !gSettingsScreen->standaloneClosed) || gOptionsScreen; }

bool muiOptionsPadButton(int button)
{
    if (!muiOptionsPadActive()) return false;
    auto& screen = *(gSettingsScreen && !gSettingsScreen->standaloneClosed ? gSettingsScreen : gOptionsScreen);
    if (button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_START)
        screen.back();
    else if (!screen.controls) {
        if (button == SDL_CONTROLLER_BUTTON_A) screen.controls = true;
        screen.tab = 3;
    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
        int count = screen.tab == 0 ? 17 : screen.tab == 1 ? 5
            : screen.tab == 2                              ? 7
            : screen.tab == 3                              ? 22
                                                           : 1;
        screen.padFocus = (screen.padFocus + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? count - 1 : 1)) % count;
        screen.padMoved = true;
    } else if (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
        screen.tab = (screen.tab + (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? 4 : 1)) % 5;
        screen.padFocus = 0;
        screen.padMoved = true;
    } else if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
        screen.padActivate = true;
        screen.padDelta = button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1;
    }
    return true;
}

void muiSettingsShow(int tab)
{
    static OptionsScreen screen;
    screen.standaloneClosed = false;
    screen.controls = true;
    screen.standalone = true;
    screen.tab = tab;
    screen.padFocus = 0;
    screen.padMoved = true;
    gSettingsScreen = &screen;
    muiPush(&screen);
}
MuiScreen* muiSettingsCreate(int tab)
{
    auto* screen = new OptionsScreen();
    screen->controls = true;
    screen->tab = tab;
    return screen;
}

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
