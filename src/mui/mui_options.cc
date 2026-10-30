#include "mui/mui.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"
#include "mui/mui_settings.h"
#include "mui/mui_skin.h"

#include "game/config.h"
#include "game/gconfig.h"
#include "game/options.h"
#include "game/version.h"
#include "plib/gnw/pad_actions.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdio.h>
#include <string>
#include <vector>

namespace fallout {

namespace {

    constexpr const char* kPortVersion = "1.0.0"; // os/android/app/build.gradle versionName
#ifdef PORT_GIT_HASH
    constexpr const char* kPortHash = PORT_GIT_HASH;
#else
    constexpr const char* kPortHash = "";
#endif

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

        // One settings row for every tab: a plate with the name on the left
        // and the value on the right; sliders carry their track below.
        struct Item {
            enum Kind { Choice,
                Slider,
                Action,
                Info };
            Kind kind = Choice;
            std::string id;
            std::string label;
            std::string value;
            double current = 0;
            double low = 0;
            double high = 1;
            double step = 1;
            bool danger = false;
            std::function<void(double)> set;
        };

        static std::string choiceText(const char* choices, int index)
        {
            std::string all = choices;
            size_t begin = 0;
            for (int n = 0; n < index; n++) {
                size_t bar = all.find('|', begin);
                if (bar == std::string::npos) break;
                begin = bar + 1;
            }
            return all.substr(begin, all.find('|', begin) - begin);
        }

        static std::string numberText(const char* format, double value)
        {
            char text[48];
            snprintf(text, sizeof(text), format, value);
            return text;
        }

        // Rows in a scrolling list with a position bar; pad focus keeps the
        // focused row whole inside the list. Returns true while a slider is held.
        bool drawItems(MuiContext& ui, std::vector<Item>& items, const MuiRect& content, const char* scrollId)
        {
            const MuiTheme& theme = muiTheme();
            MuiInk ink = muiSkinInk();
            float gap = ui.dp(6.0f);
            float margin = ui.dp(4.0f);
            float barRoom = ui.dp(14.0f);
            float x = content.x;
            float width = content.w - barRoom;

            std::vector<float> top(items.size());
            std::vector<float> height(items.size());
            float total = margin;
            for (size_t i = 0; i < items.size(); i++) {
                height[i] = ui.dp(items[i].kind == Item::Slider ? 72.0f : 44.0f);
                top[i] = total;
                total += height[i] + gap;
            }
            total += margin;

            int focusable = 0;
            std::vector<int> focusIndex(items.size(), -1);
            for (size_t i = 0; i < items.size(); i++) {
                if (items[i].kind != Item::Info) focusIndex[i] = focusable++;
            }

            if (padMoved) {
                for (size_t i = 0; i < items.size(); i++) {
                    if (focusIndex[i] != padFocus) continue;
                    float want = scrollOffset;
                    if (top[i] - margin < want) want = top[i] - margin;
                    if (top[i] + height[i] + margin > want + content.h) want = top[i] + height[i] + margin - content.h;
                    ui.setScroll(scrollId, std::max(0.0f, want));
                }
                padMoved = false;
            }
            float offset = ui.scroll(scrollId, content, total);
            scrollOffset = offset;

            muiPushClip(content);
            bool anyHeld = false;
            for (size_t i = 0; i < items.size(); i++) {
                Item& item = items[i];
                MuiRect row = { x, content.y + top[i] - offset, width, height[i] };
                if (row.bottom() < content.y || row.y > content.bottom()) continue;
                bool focused = focusIndex[i] == padFocus && focusIndex[i] >= 0;
                bool whole = row.y >= content.y && row.bottom() <= content.bottom();
                bool pressed = false;
                bool tapped = false;
                // Only rows that are whole take touches: no taps on cut-off rows.
                if (item.kind != Item::Info && whole)
                    tapped = ui.touchable(item.id, row, &pressed);
                else if (item.kind == Item::Info)
                    ui.mark(item.id, row);

                muiSkinWell(ui, row);
                if (pressed) muiFillRect(row, theme.buttonPressed.withAlpha(90));
                if (focused) {
                    muiFillRect(row, ink.selected);
                    muiStrokeRoundRect(row.inset(ui.dp(1.0f)), ui.dp(3.0f), ui.dp(2.0f), ink.accent);
                }

                float size = ui.dp(13.0f);
                MuiRect line = { row.x + ui.dp(14.0f), row.y, row.w - ui.dp(28.0f), item.kind == Item::Slider ? ui.dp(38.0f) : row.h };
                if (item.kind == Item::Action) {
                    muiDrawTextEllipsis(muiDecodeUtf8(item.label.c_str()), line, size, item.danger ? ink.warning : ink.text, MuiAlign::Center, MuiAlign::Center);
                } else {
                    std::u32string value = muiDecodeUtf8(item.value.c_str());
                    float valueWidth = std::min(muiTextWidth(value, size), line.w * 0.6f);
                    std::u32string label = muiDecodeUtf8(item.label.c_str());
                    float labelRoom = std::max(line.w - valueWidth - ui.dp(14.0f), line.w * 0.3f);
                    muiDrawTextEllipsis(label, { line.x, line.y, labelRoom, line.h }, size, ink.text, MuiAlign::Start, MuiAlign::Center);
                    muiDrawTextEllipsis(value, { line.right() - valueWidth, line.y, valueWidth, line.h }, size, item.kind == Item::Info ? ink.dim : ink.accent, MuiAlign::End, MuiAlign::Center);
                }

                bool activated = (tapped && item.kind != Item::Slider) || (padActivate && focused);
                if (item.kind == Item::Slider) {
                    float inset = ui.dp(36.0f);
                    MuiRect track = { row.x + inset, row.y + ui.dp(52.0f), row.w - inset * 2.0f, ui.dp(6.0f) };
                    bool held = false;
                    double next = item.current;
                    if (whole) next = ui.slider(item.id + ".track", track, static_cast<float>(item.current), static_cast<float>(item.low), static_cast<float>(item.high), &held);
                    anyHeld = anyHeld || held;
                    next = std::round((next - item.low) / item.step) * item.step + item.low;
                    if (activated && padActivate && focused) next = item.current + item.step * padDelta;
                    next = std::clamp(next, item.low, item.high);
                    if (next != item.current && item.set) item.set(next);
                } else if (activated && item.kind == Item::Choice) {
                    double next = item.current + (padActivate && focused ? padDelta : 1) * item.step;
                    if (next > item.high) next = item.low;
                    if (next < item.low) next = item.high;
                    if (item.set) item.set(next);
                } else if (activated && item.kind == Item::Action) {
                    if (item.set) item.set(1);
                }
            }
            muiPopClip();

            // Position bar in the right margin, and a fade where more rows hide.
            if (total > content.h) {
                float barX = content.right() - ui.dp(8.0f);
                MuiRect bar = { barX, content.y + ui.dp(4.0f), ui.dp(4.0f), content.h - ui.dp(8.0f) };
                muiFillRect(bar, ink.dim.withAlpha(60));
                float thumbH = std::max(bar.h * content.h / total, ui.dp(24.0f));
                float thumbY = bar.y + (bar.h - thumbH) * std::clamp(offset / (total - content.h), 0.0f, 1.0f);
                muiFillRect({ bar.x, thumbY, bar.w, thumbH }, ink.accent);
                float fade = ui.dp(22.0f);
                MuiColor dark = theme.background.withAlpha(235);
                MuiColor clear = dark.withAlpha(0);
                if (offset > 1.0f) muiFillGradient({ content.x, content.y, width, fade }, dark, dark, clear, clear);
                if (offset < total - content.h - 1.0f) muiFillGradient({ content.x, content.bottom() - fade, width, fade }, clear, clear, dark, dark);
            }
            padActivate = false;
            padDelta = 1;
            return anyHeld;
        }

        void buildControls(MuiContext& ui)
        {
            MuiTouchSettings& t = settings.touch;
            std::vector<Item> items;
            bool changed = false;
            auto toggle = [&](const char* id, const char* label, bool* value) {
                Item item;
                item.id = id;
                item.label = label;
                item.value = *value ? "On" : "Off";
                item.current = *value ? 1 : 0;
                item.set = [&, value](double next) {
                    *value = next != 0;
                    changed = true;
                };
                items.push_back(item);
            };
            auto slider = [&](const char* id, const char* label, int* value, int low, int high, int step, const char* format, const char* offText) {
                Item item;
                item.kind = Item::Slider;
                item.id = id;
                item.label = label;
                item.value = offText != nullptr && *value < low + 100 ? std::string(offText) : numberText(format, *value);
                item.current = *value;
                item.low = low;
                item.high = high;
                item.step = step;
                item.set = [&, value](double next) {
                    *value = static_cast<int>(next);
                    changed = true;
                };
                items.push_back(item);
            };
            toggle("controls.direct", "Touch moves the cursor", &t.direct_touch);
            toggle("controls.stick", "Right stick moves the cursor", &t.pad_cursor);
            slider("controls.speed", "Cursor speed", &t.cursor_speed, 50, 200, 10, "%.0f%%", nullptr);
            slider("controls.r3", "Right stick press: hold time", &t.long_press_ms, 200, 1000, 50, "%.0f ms", nullptr);
            // 300..1500 or off at 0: the low end of the slider is off.
            int hold = t.touch_hold_ms == 0 ? 200 : t.touch_hold_ms;
            int before = hold;
            slider("controls.hold", "Touch hold: right click after", &hold, 200, 1500, 50, "%.0f ms", "Off");
            if (hold != before) {
                t.touch_hold_ms = hold < 300 ? 0 : hold;
                changed = true;
            }
            for (int b = 0; b < kPadButtonCount; b++) {
                Item item;
                item.id = "controls.binding." + std::to_string(b);
                item.label = buttonLabels[b];
                item.value = actionLabels[padBinding(b)];
                item.current = padBinding(b);
                item.high = padActionCount() - 1;
                item.set = [b](double next) { padSetBinding(b, static_cast<int>(next)); };
                items.push_back(item);
            }
            Item reset;
            reset.kind = Item::Action;
            reset.id = "controls.binding." + std::to_string(kPadButtonCount);
            reset.label = "Reset controls to defaults";
            reset.danger = true;
            reset.set = [](double) { padResetBindings(); };
            items.push_back(reset);

            bool held = drawItems(ui, items, settingsContent, "controls");
            // Sliders save when the finger lets go, not on every step.
            if (changed) {
                dirty = true;
                muiSettingsSetMouse(false);
            }
            if (dirty && !held) {
                muiSettingsSetMouse(true);
                dirty = false;
            }
        }

        MuiRect settingsContent;
        bool standalone = false;
        bool standaloneClosed = false;
        int tab = 0;
        static constexpr const char* buttonLabels[] = { "A", "B", "X", "Y", "Start", "Select", "Left bumper", "Right bumper", "Left stick press", "Right stick press", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Left trigger", "Right trigger" };
        static constexpr const char* actionLabels[] = { "Not set", "Left click", "Right click", "Tap click / hold right click", "Back", "Game menu", "Inventory", "Skilldex", "Character", "Pip-Boy", "Automap", "Sneak", "Swap hands", "Item mode", "Start combat", "End turn", "End combat", "Quick save", "Quick load" };
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
                scrollOffset = 0;
                resetArmed = 0;
            }
            static const char32_t* const captions[] = { U"Original game preferences - saved immediately", U"Volume levels - saved immediately", U"Port display options - saved immediately", U"Touch, stick and button mapping - saved immediately", U"Version and licences" };
            muiDrawTextAligned(captions[tab], { area.x + ui.dp(4), area.y + ui.dp(42), area.w, ui.dp(26) }, ui.dp(11), muiSkinInk().dim, MuiAlign::Start, MuiAlign::Center);
            settingsContent = { area.x, area.y + ui.dp(72), area.w, area.h - ui.dp(72) };
            if (tab == 3) {
                buildControls(ui);
                return;
            }

            auto& t = settings.touch;
            std::vector<Item> items;
            // One of the original preferences (options.cc index), shown as a choice, a slider or a percentage.
            auto choice = [&](const char* label, int index, const char* choices, int count) {
                Item item;
                item.id = "settings.row." + std::to_string(items.size());
                item.label = label;
                double value = optionsGetValue(index);
                item.current = value;
                item.high = count - 1;
                item.value = choiceText(choices, static_cast<int>(value));
                item.set = [index](double next) { optionsSetValue(index, next); };
                items.push_back(item);
            };
            // [scale] maps the stored value to the number shown (a percentage).
            auto slider = [&](const char* label, int index, double low, double high, double step, double shown0, double shownScale, const char* format) {
                Item item;
                item.kind = Item::Slider;
                item.id = "settings.row." + std::to_string(items.size());
                item.label = label;
                double value = optionsGetValue(index);
                item.current = value;
                item.low = low;
                item.high = high;
                item.step = step;
                item.value = numberText(format, (value - low) * shownScale + shown0);
                item.set = [index](double next) { optionsSetValue(index, next); };
                items.push_back(item);
            };
            auto portChoice = [&](const char* label, const char* choices, double value, double high, std::function<void(double)> set) {
                Item item;
                item.id = "settings.row." + std::to_string(items.size());
                item.label = label;
                item.current = value;
                item.high = high;
                item.value = choiceText(choices, static_cast<int>(value));
                item.set = [set](double next) {
                    set(next);
                    muiSettingsSaveDisplay();
                };
                items.push_back(item);
            };
            auto portSlider = [&](const char* label, int* value, int low, int high, int step) {
                Item item;
                item.kind = Item::Slider;
                item.id = "settings.row." + std::to_string(items.size());
                item.label = label;
                item.current = *value;
                item.low = low;
                item.high = high;
                item.step = step;
                item.value = numberText("%.0f%%", *value);
                item.set = [value](double next) {
                    *value = static_cast<int>(next);
                    muiSettingsSaveDisplay();
                };
                items.push_back(item);
            };
            auto resetRow = [&](std::function<void()> action) {
                Item item;
                item.kind = Item::Action;
                item.id = "settings.reset";
                bool armed = resetArmed != 0 && static_cast<int>(ui.now - resetArmed) < 3000;
                item.label = armed ? "Tap again to confirm reset" : "Reset to defaults";
                item.danger = true;
                item.set = [this, &ui, armed, action](double) {
                    if (armed) {
                        action();
                        resetArmed = 0;
                    } else {
                        resetArmed = ui.now;
                    }
                };
                items.push_back(item);
            };

            if (tab == 0) {
                choice("Game difficulty", 0, "Easy|Normal|Hard", 3);
                choice("Combat difficulty", 1, "Wimpy|Normal|Rough", 3);
                choice("Violence", 2, "None|Minimal|Normal|Maximum blood", 4);
                choice("Target highlight", 3, "Off|On|Targeting only", 3);
                choice("Combat looks", 4, "Off|On", 2);
                choice("Combat messages", 5, "Verbose|Brief", 2);
                choice("Combat taunts", 6, "Off|On", 2);
                choice("Language filter", 7, "Off|On", 2);
                choice("Running", 8, "Normal|Always", 2);
                choice("Subtitles", 9, "Off|On", 2);
                choice("Item highlight", 10, "Off|On", 2);
                slider("Combat speed", 11, 0, 50, 1, 0, 1, "%.0f");
                slider("Text delay", 12, 1, 6, .25, 1, 1, "%.1f s");
                choice("Player speedup", 19, "Off|On", 2);
                // Brightness 1.00 to 1.18 shown as 0 to 100 %.
                slider("Brightness", 17, 1, 1.17999267578125, .01, 0, 100 / 0.17999267578125, "%.0f%%");
                slider("Mouse sensitivity", 18, 1, 2.5, .1, 100, 100, "%.0f%%");
                resetRow([] { optionsResetValues(false); });
            } else if (tab == 1) {
                // Stored 0..32767, shown as 0..100 %.
                const char* names[] = { "Master volume", "Music volume", "Sound effects volume", "Speech volume" };
                for (int i = 0; i < 4; i++) {
                    Item item;
                    item.kind = Item::Slider;
                    item.id = "settings.row." + std::to_string(i);
                    item.label = names[i];
                    int index = 13 + i;
                    double percent = std::round(optionsGetValue(index) * 100.0 / 32767.0);
                    item.current = percent;
                    item.high = 100;
                    item.step = 5;
                    item.value = numberText("%.0f%%", percent);
                    item.set = [index](double next) { optionsSetValue(index, std::min(next * 32767.0 / 100.0, 32767.0)); };
                    items.push_back(item);
                }
                resetRow([] { optionsResetValues(true); });
            } else if (tab == 2) {
                portChoice("Theme", "Pip-Boy 2000|Classic", t.bottom_theme == 2 ? 0 : 1, 1, [&t](double next) { t.bottom_theme = next == 0 ? 2 : 0; });
                portSlider("HUD scale", &t.hud_scale, 50, 300, 10);
                portSlider("Bottom text size", &t.bottom_text_size, 50, 300, 10);
                portChoice("Reduce motion", "Off|On", t.reduce_motion, 1, [&t](double next) { t.reduce_motion = next != 0; });
                portChoice("Colour-blind safe", "Off|On", t.colour_blind_safe, 1, [&t](double next) { t.colour_blind_safe = next != 0; });
                portChoice("Dual screen", "Off|On", t.dual_screen, 1, [&t](double next) { t.dual_screen = next != 0; });
                resetRow([&t] {
                    auto defaults = MuiTouchSettings();
                    t.bottom_theme = defaults.bottom_theme;
                    t.hud_scale = defaults.hud_scale;
                    t.bottom_text_size = defaults.bottom_text_size;
                    t.reduce_motion = false;
                    t.colour_blind_safe = false;
                    t.dual_screen = true;
                    muiSettingsSaveDisplay();
                });
            } else {
                char version[VERSION_MAX];
                getverstr(version, sizeof(version));
                auto info = [&](const char* label, const std::string& value) {
                    Item item;
                    item.kind = Item::Info;
                    item.id = "settings.info." + std::to_string(items.size());
                    item.label = label;
                    item.value = value;
                    items.push_back(item);
                };
                std::string port = muiPortVersion();
                info("Port", "Fallout 1 CE Dual Screen");
                info("Port version", port);
                info("Game engine", version);
                info("Licences", "LICENSE.md, third_party");
                Item source;
                source.kind = Item::Action;
                source.id = "settings.source";
                source.label = "Open source repository";
                source.set = [](double) { SDL_OpenURL("https://github.com/sirbilliams/Fallout1Ce-Dualscreen-Android"); };
                items.push_back(source);
            }
            drawItems(ui, items, settingsContent, tab == 0 ? "settings.rows.0" : tab == 1 ? "settings.rows.1"
                    : tab == 2                                                            ? "settings.rows.2"
                                                                                          : "settings.rows.4");
        }

        float scrollOffset = 0;
        unsigned int resetArmed = 0;
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

std::string muiPortVersion()
{
    std::string version = kPortVersion;
    if (kPortHash[0] != '\0') version += std::string(" (") + kPortHash + ")";
    return version;
}

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
