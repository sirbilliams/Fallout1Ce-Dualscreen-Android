#include "mui/mui.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"

#include <algorithm>

namespace fallout {

namespace {

    // Conversation screen: what the NPC says, the options to answer and the
    // barter button. The classic dialogue loop (gdialog.cc) stays in charge: it
    // feeds this screen every frame and takes the answer as a key press.
    class DialogueScreen : public MuiScreen {
    public:
        DialogueScreen() { modal = true; }

        MuiSurface surface() override { return muiGameScreenSurface(); }
        bool coversScreen() override { return true; }
        void back() override { }
        void build(MuiContext& ui) override;

        std::string name;
        std::string reply;
        std::vector<std::string> options;
        bool barter = false;
        int choice = -1;
    };

    void DialogueScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        MuiRect content = muiScreenFrame(ui, muiDecodeGameText(name.c_str()), nullptr);

        float gap = ui.dp(10.0f);
        float barterHeight = barter ? ui.dp(54.0f) + gap : 0.0f;
        float optionHeight = ui.dp(60.0f);
        size_t count = options.size();
        float optionsHeight = std::min(content.h * 0.66f, count * (optionHeight + gap));

        MuiRect replyRect = { content.x, content.y, content.w, content.h - optionsHeight - barterHeight - gap };
        ui.panel(replyRect);
        MuiRect inner = replyRect.inset(ui.dp(12.0f));
        float size = ui.dp(19.0f);
        std::vector<std::u32string> lines = muiWrapText(muiDecodeGameText(reply.c_str()), inner.w, size);
        float lineHeight = muiLineHeight(size);
        float offset = ui.scroll("dialogue.reply", inner, lines.size() * lineHeight);
        muiPushClip(inner);
        for (size_t index = 0; index < lines.size(); index++) {
            float y = inner.y + index * lineHeight - offset;
            if (y + lineHeight < inner.y || y > inner.bottom()) {
                continue;
            }
            muiDrawText(lines[index], inner.x, y, size, theme.text);
        }
        muiPopClip();

        // Answers, scrolling when there are many.
        MuiRect optionsRect = { content.x, replyRect.bottom() + gap, content.w, optionsHeight };
        float optionsOffset = ui.scroll("dialogue.options", optionsRect, count * (optionHeight + gap));
        muiPushClip(optionsRect);
        for (size_t index = 0; index < count; index++) {
            MuiRect rect = { optionsRect.x, optionsRect.y + index * (optionHeight + gap) - optionsOffset, optionsRect.w, optionHeight };
            if (rect.bottom() < optionsRect.y || rect.y > optionsRect.bottom()) {
                continue;
            }
            const std::string& text = options[index];
            // The classic list marks answers with a bullet (0x95).
            size_t skip = !text.empty() && static_cast<unsigned char>(text[0]) == 0x95 ? 2 : 0;
            std::u32string label = muiDecodeUtf8(std::to_string(index + 1).c_str()) + U". " + muiDecodeGameText(text.c_str() + std::min(skip, text.size()));
            if (ui.button("dialogue.option." + std::to_string(index), rect, label)) {
                choice = static_cast<int>(index);
            }
        }
        muiPopClip();

        if (barter) {
            MuiRect rect = { content.x, content.bottom() - ui.dp(54.0f), content.w, ui.dp(54.0f) };
            if (ui.button("dialogue.barter", rect, U"BARTER", MuiButtonStyle::Primary)) {
                choice = 100;
            }
        }
    }

    DialogueScreen* gScreen = nullptr;

} // namespace

void muiDialogueUpdate(const char* name, const char* reply, const std::vector<std::string>& options, bool barter)
{
    if (!muiIsEnabled()) {
        return;
    }
    if (gScreen == nullptr) {
        gScreen = new DialogueScreen();
        muiPush(gScreen);
    }
    gScreen->name = name != nullptr ? name : "";
    gScreen->reply = reply != nullptr ? reply : "";
    gScreen->options = options;
    gScreen->barter = barter;
}

int muiDialogueTakeChoice()
{
    if (gScreen == nullptr) {
        return -1;
    }
    int choice = gScreen->choice;
    gScreen->choice = -1;
    return choice;
}

void muiDialogueHide()
{
    if (gScreen == nullptr) {
        return;
    }
    muiRemove(gScreen);
    delete gScreen;
    gScreen = nullptr;
}

MuiScreen* muiDialogueCreate()
{
    DialogueScreen* screen = new DialogueScreen();
    screen->name = "Vic";
    screen->reply = "Hey, you're the Vault Dweller, right? The Overseer sent word about you. We could use someone with your skills out here. Tell me, what do you need?";
    screen->options = { "\x95 I need to find a water chip.", "\x95 What can you tell me about this place?", "\x95 Let's trade.", "\x95 Goodbye." };
    screen->barter = true;
    return screen;
}

} // namespace fallout
