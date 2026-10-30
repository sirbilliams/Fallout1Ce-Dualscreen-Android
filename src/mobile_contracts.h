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
    EndTurn,
    // Lane 3a: the bottom console's buttons. OpenScreen takes a MobileScreen
    // as id (Inventory, Character, Pipboy, Map = automap); UseItem with id -1
    // is the active hand's item button.
    EndCombat,
    SwapHand,
    CycleAttackMode,
    Menu,
    Save,
    Load };
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
// The game's input loops (main loop, combat input) call this when they have no
// key: it turns the next queued command into the key code the classic
// interface would have produced, after the same legality checks the keys have
// (-1 for none). Every call also tells the console the game is listening.
int mobilePollKey(bool inCombat);
// The game's loops polled a moment ago: console buttons are live. Without it
// (a classic loop runs, the enemy plays) they are dimmed and tapped commands
// are dropped.
bool mobileDispatchLive();
void mobileRenderBottom();
}
#endif
