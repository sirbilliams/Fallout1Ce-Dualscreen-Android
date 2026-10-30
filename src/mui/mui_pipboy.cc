#include "mui/mui.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"

#include <algorithm>

namespace fallout {

namespace {

    // Touch face of F1's Pip-Boy 2000: tabs for what it has (status, automaps,
    // archives, alarm clock) and the lists behind them. The classic Pip-Boy loop
    // (pipboy.cc) keeps running the real thing: taps become its key codes
    // (500..504 tabs, 506.. list lines).
    class PipboyScreen : public MuiScreen {
    public:
        PipboyScreen() { modal = true; }

        MuiSurface surface() override { return muiGameScreenSurface(); }
        bool coversScreen() override { return true; }
        void back() override { choice = 503; }
        void build(MuiContext& ui) override;

        MuiPipboyView view;
        int tab = 0;
        int choice = -1;
    };

    void PipboyScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        bool back = false;
        MuiRect content = muiScreenFrame(ui, U"PIP-BOY 2000", &back);
        if (back) {
            choice = 503;
        }

        // Tabs.
        struct Tab {
            const char16_t* label;
            int key;
        };
        static const Tab tabs[] = { { u"STATUS", 500 }, { u"AUTOMAPS", 501 }, { u"ARCHIVES", 502 }, { u"ALARM", 504 } };
        float gap = ui.dp(8.0f);
        float tabWidth = (content.w - gap * 3.0f) / 4.0f;
        float tabHeight = ui.dp(50.0f);
        for (int index = 0; index < 4; index++) {
            MuiRect rect = { content.x + index * (tabWidth + gap), content.y, tabWidth, tabHeight };
            std::u32string label(tabs[index].label, tabs[index].label + std::char_traits<char16_t>::length(tabs[index].label));
            if (ui.button(std::string("pipboy.tab.") + std::to_string(index), rect, label, tab == index ? MuiButtonStyle::Primary : MuiButtonStyle::Normal)) {
                tab = index;
                choice = tabs[index].key;
            }
        }

        MuiRect body = { content.x, content.y + tabHeight + gap, content.w, content.h - tabHeight - gap };
        std::vector<MuiRow> rows;
        int keyBase = 506;
        if (tab == 0) {
            rows.push_back({ muiDecodeGameText(view.status.c_str()), U"", true });
            rows.push_back({ muiDecodeGameText(view.place.c_str()), muiDecodeGameText(view.hp.c_str()) });
            rows.push_back({ U"QUESTS", U"", true });
            for (const std::string& quest : view.quests) {
                rows.push_back({ muiDecodeGameText(quest.c_str()), U"" });
            }
            if (view.quests.empty()) {
                rows.push_back({ U"NOTHING YET", U"", false, false, true });
            }
            keyBase = 507 - 3;
        } else if (tab == 1) {
            rows.push_back({ U"AUTOMAPS", U"", true });
            rows.push_back({ U"OPEN THE MAP LIST ON THE MAIN SCREEN", U"", false, false, true });
            keyBase = -1;
        } else if (tab == 2) {
            rows.push_back({ U"HOLODISKS", U"", true });
            for (const std::string& disk : view.disks) {
                rows.push_back({ muiDecodeGameText(disk.c_str()), U"" });
            }
            if (view.disks.empty()) {
                rows.push_back({ U"NO HOLODISKS", U"", false, false, true });
            }
            keyBase = 506 - 1;
        } else {
            rows.push_back({ U"ALARM CLOCK - REST", U"", true });
            for (const std::string& option : view.rest) {
                MuiRow row;
                row.left = muiDecodeGameText(option.c_str());
                row.dim = !view.canRest;
                rows.push_back(row);
            }
            keyBase = 510 - 1;
        }
        int tapped = muiListView(ui, "pipboy.list." + std::to_string(tab), body, rows, 48.0f);
        if (tapped >= 0 && keyBase > 0) {
            // Rows before the data (titles) are not entries.
            int index = tapped - (tab == 0 ? 3 : 1);
            if (tab == 0 && index >= 0 && index < static_cast<int>(view.quests.size())) {
                choice = 507 + index;
            } else if (tab == 2 && index >= 0 && index < static_cast<int>(view.disks.size())) {
                choice = 506 + index;
            } else if (tab == 3 && index >= 0 && index < static_cast<int>(view.rest.size())) {
                choice = 510 + index;
            }
        }
        (void)theme;
    }

    PipboyScreen* gScreen = nullptr;

} // namespace

void muiPipboyUpdate(const MuiPipboyView& view)
{
    if (!muiIsEnabled()) {
        return;
    }
    if (gScreen == nullptr) {
        gScreen = new PipboyScreen();
        muiPush(gScreen);
    }
    gScreen->view = view;
}

int muiPipboyTakeChoice()
{
    if (gScreen == nullptr) {
        return -1;
    }
    int choice = gScreen->choice;
    gScreen->choice = -1;
    return choice;
}

void muiPipboyHide()
{
    if (gScreen == nullptr) {
        return;
    }
    muiRemove(gScreen);
    delete gScreen;
    gScreen = nullptr;
}

MuiScreen* muiPipboyCreate()
{
    PipboyScreen* screen = new PipboyScreen();
    screen->view.status = "STATUS";
    screen->view.place = "Vault 13";
    screen->view.hp = "HP 30/30";
    screen->view.quests = { "Vault 13", "Shady Sands", "Junktown" };
    screen->view.disks = { "Vault 13 Holodisk", "Cult of Children Holodisk" };
    screen->view.rest = { "Rest for ten minutes", "Rest for thirty minutes", "Rest for one hour", "Rest until morning" };
    screen->view.canRest = true;
    return screen;
}

} // namespace fallout
