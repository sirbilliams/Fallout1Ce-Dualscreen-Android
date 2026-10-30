#include "mui/mui.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"

#include <stdio.h>

#include <algorithm>

namespace fallout {

namespace {

    std::u32string number(int value)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", value);
        return muiDecodeUtf8(buffer);
    }

    // Touch trading screen. The classic barter loop (inventry.cc) stays in
    // charge: it feeds this screen the four item lists every frame and takes
    // its taps back as moves (done by the loop with the engine's own item
    // calls), the Offer key or the Talk key.
    class BarterScreen : public MuiScreen {
    public:
        BarterScreen() { modal = true; }

        MuiSurface surface() override { return muiGameScreenSurface(); }
        bool coversScreen() override { return true; }
        void back() override { action = { MuiBarterAction::Talk, 0, false }; }
        void build(MuiContext& ui) override;

        MuiBarterView view;
        MuiBarterAction action = { MuiBarterAction::None, 0, false };
        bool moveAll = false;

    private:
        // One list; a tap queues a move.
        void list(MuiContext& ui, const char* id, const char* title, const MuiRect& rect, const std::vector<MuiBarterItem>& items, MuiBarterAction::Kind kind, bool showPrice)
        {
            const MuiTheme& theme = muiTheme();
            MuiRect head = { rect.x, rect.y, rect.w, ui.dp(26.0f) };
            muiDrawTextAligned(muiDecodeUtf8(title), head, ui.dp(16.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
            MuiRect body = { rect.x, head.bottom(), rect.w, rect.h - head.h };
            std::vector<MuiRow> rows;
            for (const MuiBarterItem& item : items) {
                MuiRow row;
                row.left = muiDecodeGameText(item.name.c_str());
                row.right = (item.quantity > 1 ? U"x" + number(item.quantity) + U"  " : std::u32string()) + (showPrice ? number(item.unitCost * item.quantity) : std::u32string());
                row.icon = item.fid;
                rows.push_back(row);
            }
            if (rows.empty()) {
                rows.push_back({ U"EMPTY", U"", false, false, true });
            }
            int tapped = muiListView(ui, id, body, rows, 44.0f);
            if (tapped >= 0 && tapped < static_cast<int>(items.size())) {
                action = { kind, tapped, moveAll };
            }
        }
    };

    void BarterScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        bool backTapped = false;
        MuiRect content = muiScreenFrame(ui, U"BARTER: " + muiDecodeGameText(view.trader.c_str()), &backTapped);
        if (backTapped) {
            action = { MuiBarterAction::Talk, 0, false };
        }

        float gap = ui.dp(10.0f);
        float footer = ui.dp(54.0f);
        float totals = ui.dp(44.0f);
        float listsHeight = content.h - footer - totals - gap * 3.0f;
        float upper = listsHeight * 0.55f;
        float lower = listsHeight - upper;
        float half = (content.w - gap) / 2.0f;

        MuiRect topLeft = { content.x, content.y, half, upper };
        MuiRect topRight = { content.x + half + gap, content.y, half, upper };
        list(ui, "barter.yours", "YOUR ITEMS", topLeft, view.yours, MuiBarterAction::OfferItem, false);
        list(ui, "barter.theirs", "THEIR ITEMS", topRight, view.theirs, MuiBarterAction::AskItem, false);

        float y = content.y + upper + gap;
        MuiRect lowLeft = { content.x, y, half, lower };
        MuiRect lowRight = { content.x + half + gap, y, half, lower };
        list(ui, "barter.offer", "YOUR OFFER", lowLeft, view.offer, MuiBarterAction::TakeOffer, true);
        list(ui, "barter.ask", "THEY GIVE", lowRight, view.ask, MuiBarterAction::TakeAsk, true);

        // Totals: what you offer against what they want for the asked items.
        y += lower + gap;
        MuiRect bar = { content.x, y, content.w, totals };
        ui.panel(bar);
        MuiRect inner = bar.inset(ui.dp(10.0f), 0.0f);
        bool enough = view.offerValue >= view.askValue;
        muiDrawTextAligned(U"OFFER $" + number(view.offerValue), inner, ui.dp(20.0f), enough ? theme.accent : theme.text, MuiAlign::Start, MuiAlign::Center);
        muiDrawTextAligned(U"ASKING $" + number(view.askValue), inner, ui.dp(20.0f), theme.text, MuiAlign::End, MuiAlign::Center);

        y += totals + gap;
        float third = (content.w - gap * 2.0f) / 3.0f;
        if (ui.button("barter.amount", { content.x, y, third, footer }, moveAll ? U"MOVE: ALL" : U"MOVE: ONE")) {
            moveAll = !moveAll;
        }
        if (ui.button("barter.offerbtn", { content.x + third + gap, y, third, footer }, U"OFFER", MuiButtonStyle::Primary)) {
            action = { MuiBarterAction::Offer, 0, false };
        }
        if (ui.button("barter.talk", { content.x + (third + gap) * 2.0f, y, third, footer }, U"TALK")) {
            action = { MuiBarterAction::Talk, 0, false };
        }
    }

    BarterScreen* gScreen = nullptr;

} // namespace

void muiBarterUpdate(const MuiBarterView& view)
{
    if (!muiIsEnabled()) {
        return;
    }
    if (gScreen == nullptr) {
        gScreen = new BarterScreen();
        muiPush(gScreen);
    }
    gScreen->view = view;
}

MuiBarterAction muiBarterTakeAction()
{
    if (gScreen == nullptr) {
        return { MuiBarterAction::None, 0, false };
    }
    MuiBarterAction action = gScreen->action;
    gScreen->action = { MuiBarterAction::None, 0, false };
    return action;
}

void muiBarterHide()
{
    if (gScreen == nullptr) {
        return;
    }
    muiRemove(gScreen);
    delete gScreen;
    gScreen = nullptr;
}

MuiScreen* muiBarterCreate()
{
    BarterScreen* screen = new BarterScreen();
    screen->view.trader = "Vic";
    screen->view.yours = { { "10mm Pistol", 1, 250, -1 }, { "Stimpak", 3, 175, -1 }, { "Bottle Caps", 120, 1, -1 } };
    screen->view.theirs = { { "Leather Armor", 1, 700, -1 }, { "Jet", 2, 500, -1 } };
    screen->view.offer = { { "Stimpak", 2, 175, -1 } };
    screen->view.ask = { { "Jet", 1, 500, -1 } };
    screen->view.offerValue = 350;
    screen->view.askValue = 500;
    return screen;
}

} // namespace fallout
