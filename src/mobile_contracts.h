#ifndef FALLOUT_MOBILE_CONTRACTS_H_
#define FALLOUT_MOBILE_CONTRACTS_H_
#include <SDL.h>
#include <cstdint>
#include <string>
#include <vector>
namespace fallout {
constexpr SDL_TouchID kMobileBottomTouchId = 0x7B000000;
// Value-only models: publish on the game thread, never expose engine pointers.
// A modal change increments generation. Commands with stale generations must
// be rejected by the owning F1 adapter before any engine action occurs.
enum class MobileScreen { Classic,
    MainMenu,
    Inventory,
    Loot,
    Character,
    Dialogue,
    Barter,
    Pipboy,
    Map,
    LoadSave };
struct MobileSnapshot {
    uint64_t generation = 0;
    MobileScreen screen = MobileScreen::Classic;
    bool inCombat = false;
    bool playerTurn = false;
    int hitPoints = 0;
    int actionPoints = 0;
    std::vector<std::string> entries;
};
enum class MobileCommandType { OpenScreen,
    CloseScreen,
    SelectEntry,
    UseItem,
    Walk,
    EndTurn };
struct MobileCommand {
    uint64_t generation = 0;
    MobileCommandType type = MobileCommandType::CloseScreen;
    int id = -1;
    int value = 0;
};
// fallout.cfg [touch], shared with Java onboarding in lane 4.
struct MobileSettings {
    bool enabled = true;
    bool bottomScreen = true;
};
// Game-thread only. Consumer owns a copy; queue is bounded and FIFO. Lane 2/3
// dispatch exactly once through F1 after checking legality and generation.
MobileSettings mobileSettings();
const MobileSnapshot& mobileSnapshot();
void mobilePublishSnapshot(MobileSnapshot snapshot);
bool mobileQueueCommand(const MobileCommand& command);
bool mobileTakeCommand(MobileCommand* command);
void mobileClearCommands();
void mobileRenderBottom();
}
#endif
