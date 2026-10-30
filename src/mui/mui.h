#ifndef FALLOUT_MUI_H_
#define FALLOUT_MUI_H_

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "mobile_contracts.h"
#include "mui/mui_draw.h"

namespace fallout {

// Mobile UI: touch-first interface drawn by the GPU at the screen's native
// resolution over the game (world and original 8-bit windows), sized in dp
// like the touch HUD.
//
// Screens are immediate mode: every frame `MuiScreen::build` lays out, draws
// and handles input of its widgets through `MuiContext`. Screens are kept in
// a stack, the top one gets input. Modal screens take every touch, others
// only touches on their widgets (the rest goes to the game).
//
// Look (colors, sizes) comes from `MuiTheme`, so the visual style can be
// replaced later without touching screens.

// `[touch] enabled` setting.
bool muiIsEnabled();

struct MuiTheme {
    MuiColor dim;
    MuiColor panel;
    MuiColor panelBorder;
    MuiColor text;
    MuiColor textDim;
    MuiColor accent;
    MuiColor button;
    MuiColor buttonPressed;
    MuiColor buttonBorder;
    MuiColor buttonText;
    MuiColor buttonPrimary;
    MuiColor buttonPrimaryText;

    // dp
    float radius;
    float padding;
    float gap;
    float buttonHeight;
    float borderWidth;
    float titleSize;
    float bodySize;
    float buttonTextSize;
    float touchSlop;
    // Behind everything (full screens), under the metal.
    MuiColor background;
    // Recessed monitor wells (lists, maps, readouts).
    MuiColor well;
    // A chosen row or item in a well.
    MuiColor selected;
    // Mistakes, not enough, danger.
    MuiColor warning;
    // The metal plates' tint (mui_skin.h).
    MuiColor metal;
};

const MuiTheme& muiTheme();

enum class MuiButtonStyle {
    Normal,
    Primary,
};

class MuiContext;

// The device's screens the mobile UI draws on: the game's (Top) and, on
// dual-screen devices (AYN Thor), the bottom screen (bottom_screen.h).
// Without a bottom screen everything is on Top, as on any phone.
enum class MuiSurface {
    Top,
    Bottom,
};

constexpr int kMuiSurfaceCount = 2;

// Touch device id of the bottom screen's fingers: BottomScreen.java's touches
// come into the game's event queue as SDL finger events with it
// (bottom_screen.cc), so they reach the bottom surface only, never the map.

// The bottom screen is there and in use (`[touch] dual_screen`).
bool muiBottomActive();

// Where the game's own screens (inventory, character, Pip-Boy, map, loot,
// barter...) go: the bottom screen while it's active, the top otherwise.
MuiSurface muiGameScreenSurface();

// View of content moved and zoomed by fingers (`MuiContext::panZoom`):
// content point at the view's center, output pixels per content unit.
struct MuiPanZoom {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float zoom = 1.0f;
    float minZoom = 0.1f;
    float maxZoom = 10.0f;
};

class MuiScreen {
public:
    virtual ~MuiScreen() = default;
    virtual void build(MuiContext& ui) = 0;

    // Back button of the screen, Android back button, swipe back.
    virtual void back() { }

    // Other keys from game input loop (keyboard).
    virtual void key(int keyCode) { }

    // Inactive screens (e.g. HUD under a game screen) draw nothing and don't
    // take input; the topmost active screen gets input.
    virtual bool isActive() { return true; }

    // The screen is about to show (the game sets up its state), the last
    // shown frame stays instead of the game's windows under it.
    virtual bool holdsFrame() { return false; }

    // Opaque over the whole screen: the map and the game's screen under it
    // aren't drawn while the screen is active.
    virtual bool coversScreen() { return false; }

    // Over the map without covering it, but the HUD's buttons shouldn't show
    // under it (a game screen the game shows over the map: the elevator).
    virtual bool hidesHud() { return false; }

    // The screen of the device it is drawn on and takes touches from.
    virtual MuiSurface surface() { return MuiSurface::Top; }

    bool modal = true;
    bool finished = false;

    // Set when pushed, for appear animations.
    unsigned int openTime = 0;
};

class MuiContext {
public:
    float dp(float value) const { return value * pixelsPerDp; }

    // Whole screen and its part outside display cutouts.
    MuiRect screenRect() const { return screen; }
    MuiRect safeRect() const { return safe; }

    // The screen being built gets input.
    bool interactive() const { return isInteractive; }

    // 0..1 progress of the screen's appear animation.
    float appear() const;

    // Darkens everything below (for modal screens).
    void dim();

    void panel(const MuiRect& rect);

    // Generic touchable area: returns true when tapped (finger up inside after
    // going down inside, not moved away), `pressed` - finger is down on it now,
    // `longPressed` - set once when the finger is held still long enough (the
    // tap is not reported then).
    bool touchable(const std::string& id, const MuiRect& rect, bool* pressed = nullptr, bool* longPressed = nullptr);

    bool button(const std::string& id, const MuiRect& rect, const std::u32string& label, MuiButtonStyle style = MuiButtonStyle::Normal);

    // Tap outside of [rect] (e.g. to close a popup).
    bool tappedOutside(const MuiRect& rect);

    // Area catching touches of a non-modal screen.
    void region(const MuiRect& rect);

    // Vertical scroll area over [rect] with content of [contentHeight]:
    // finger drag scrolls, a fling continues with inertia. Widgets inside
    // must use ids starting with "<id>." - they lose their press when the
    // finger starts scrolling. Returns scroll offset (0 - top of content).
    float scroll(const std::string& id, const MuiRect& rect, float contentHeight);
    // The same sideways over content of [contentWidth] (0 - its left end).
    float scrollHorizontal(const std::string& id, const MuiRect& rect, float contentWidth);
    void setScroll(const std::string& id, float offset);
    bool isScrolling(const std::string& id) const;

    // Drag and drop. Call after `touchable` of widget [id]: when the finger
    // holding it moves mostly sideways ([anyDirection] - moves at all, for
    // widgets outside scroll areas), a drag of [payload] starts (vertical
    // moves stay with scroll areas, the tap is not reported). Holding still is
    // a long press (action menu), it never starts a drag. Returns true while
    // this widget is dragged.
    bool dragSource(const std::string& id, int payload, bool anyDirection = false);
    // A drag is in progress, [payload] is set.
    bool dragging(int* payload) const;
    // Dragged payload was released over [rect] (reported once).
    bool dropped(const MuiRect& rect, int* payload);

    // Registers [rect] of widget [id] drawn without `touchable` (automated
    // tests find widgets by id).
    void mark(const std::string& id, const MuiRect& rect);

    // Finger position (output pixels) and state, for custom widgets.
    float pointerX() const;
    float pointerY() const;
    bool pointerDown() const;
    // This frame's transitions of the finger, and where / when it went down
    // (`compat_ticks`), for the touchpad.
    bool pointerPressed() const;
    bool pointerReleased() const;
    float pointerStartX() const;
    float pointerStartY() const;
    // A second finger is down with the captured one.
    bool secondFingerDown() const;

    // Content under [rect] (maps): one finger drags it, two fingers pinch
    // it (zoom around them). Returns true while touched. [tapped] - the finger
    // went up where it went down, no pinch (at `pointerX` / `pointerY`).
    bool panZoom(const std::string& id, const MuiRect& rect, MuiPanZoom* view, bool* tapped = nullptr);

    // Text typed since the last call (text input started with
    // `beginTextInput`), taken by the screen reading it.
    std::u32string takeTextInput();

    // Horizontal slider over [rect], returns the value (finger sets it).
    // [dragging] - the finger holds it now.
    float slider(const std::string& id, const MuiRect& rect, float value, float minValue, float maxValue, bool* dragging = nullptr);

    // On / off switch over [rect]: a tap flips [value]. Returns true when it
    // changed.
    bool toggle(const std::string& id, const MuiRect& rect, bool* value);

    // Gamepad: the widget [id] (built in this frame) takes the focus when the
    // screen shows, instead of the first one (the content, not the tabs).
    void padPrefer(const std::string& id);

    // One of [labels] in a row of joined buttons over [rect] (ids
    // "<id>.<index>"): a tap picks it. Returns true when [selected] changed.
    bool segmented(const std::string& id, const MuiRect& rect, const std::vector<std::u32string>& labels, int* selected);

    float scrollAlong(const std::string& id, const MuiRect& rect, float contentSize, bool horizontal);

    // Internal state, filled by mui.cc.
    float pixelsPerDp = 1.0f;
    // The screen of the device being built.
    MuiSurface surface = MuiSurface::Top;
    MuiRect screen;
    MuiRect safe;
    bool isInteractive = false;
    MuiScreen* current = nullptr;
    unsigned int now = 0;
};

// Output pixels of the surface being built (the top outside of building) to
// game screen (logical) pixels. On the bottom screen: its own logical space,
// as wide as the game's screen (`muiSurfaceMetrics`).
void muiToScreen(float x, float y, int* screenX, int* screenY);

// Layout metrics of [surface] in game screen (logical) pixels, as
// `hudMetricsGet` for the top: the bottom screen is as wide as the game's
// screen there, its height and dp in proportion, no cutouts.

// Text [id] of `game\ce.msg` (mobile UI texts localized with game data),
// [fallback] without it.
const char* muiText(int id, const char* fallback);

void muiPush(MuiScreen* screen);
void muiRemove(MuiScreen* screen);
// The surface being built (the top outside of building).
MuiSurface muiCurrentSurface();

// An active modal screen on any surface.
bool muiHasModal();
// An active modal screen on [surface].
bool muiHasModalOn(MuiSurface surface);

// A screen holds the last frame, don't present a new one (see
// `MuiScreen::holdsFrame`).
bool muiHoldsFrame();

// A full screen closes for another one the game opens next (tabs of the game
// screens): the last frame stays until that one covers the screen, so the
// map doesn't show between them. [from] is the screen closing.
void muiHoldFrameForSwitch(MuiScreen* from);

// An active screen covers the whole top screen (see
// `MuiScreen::coversScreen`).
bool muiCoversScreen();

// An active screen on the HUD's screen of the device hides the HUD (see
// `MuiScreen::hidesHud`).
bool muiHidesHud();

// An active screen covers the whole of [surface].
bool muiCoversSurface(MuiSurface surface);

// The surface of the HUD, its log and the notifications: the bottom screen
// while it's active, the top otherwise.
MuiSurface muiHudSurface();

// An active modal screen on the bottom screen: the game shows a screen there
// and the world on top is frozen (the game's own loop runs the screen).
bool muiBottomHoldsGame();

// Runs game input and rendering until [screen] is finished (synchronous flows
// like dialog boxes). Esc / Android back call `MuiScreen::back`.
void muiRunModal(MuiScreen* screen);

// svga.cc: draws screens over the game.
void muiRender(SDL_Renderer* renderer);
// svga.cc: draws the bottom screen's screens and hands the frame to it
// (bottom_screen.h); nothing without one.
void muiRenderBottom(SDL_Renderer* renderer);
void muiResetRenderer();

// input.cc: returns true if the finger event belongs to the mobile UI.
bool muiHandleFingerEvent(const SDL_Event* event);

// input.cc: forgets fingers not in [down] (the [count] fingers really down
// now): ends that never came.
// Returns true if any was forgotten.
bool muiForgetMissingFingers(const SDL_FingerID* down, int count);

// input.cc: typed text (UTF-8).
void muiHandleTextInput(const char* text);

// Back (Android's back button): at the next frame the topmost active modal
// screen's `back()`, or with none the overlays' (the HUD opens the game's
// menu, as Esc on the map). Screens' Back buttons do the same.
void muiRequestBack();

// Title menu (mui_main_menu.cc): shown while the F1 menu loop runs; the loop
// takes the choice (a MainMenuOption, -1 for none yet).
void muiMainMenuShow();
void muiMainMenuHide();
int muiMainMenuTakeChoice();

// Option menu (mui_options.cc), shown while options.cc's loop runs; the
// choice is a key code of that loop (500..504), -1 for none yet.
void muiSettingsShow(int tab = 0);
bool muiOptionsPadActive();
bool muiOptionsPadButton(int button);
void muiOptionsShow();
void muiOptionsHide();
int muiOptionsTakeChoice();

// Character sheet (mui_character.cc). Returns -1 when the mobile UI is off
// (the caller opens the classic editor), 0 when shown and closed, 1 when
// the player asked for the classic editor.
int muiCharacterScreenRun();

// Inventory (mui_inventory.cc) of [owner] (the player). -1: mobile UI off
// (classic window), 0: shown and closed.
struct Object;
int muiInventoryScreenRun(Object* owner);

// Save/load slot picker (mui_loadsave.cc), shown while the classic save/load
// loop runs. TakeChoice: slot 0..9 to act on, -2 cancel, -1 nothing yet.
struct MuiSaveSlot {
    bool empty = true;
    bool broken = false;
    std::string description;
    std::string place;
};
void muiLoadSaveShow(bool saving, const std::vector<MuiSaveSlot>& slots, int cursor);
void muiLoadSaveHide();
int muiLoadSaveTakeChoice();

// Conversation (mui_dialogue.cc), fed every frame by the classic dialogue loop.
// TakeChoice: answer index 0.., 100 = barter, -1 nothing.
void muiDialogueUpdate(const char* name, const char* reply, const std::vector<std::string>& options, bool barter);
int muiDialogueTakeChoice();
void muiDialogueHide();

// Barter (mui_barter.cc), fed every frame by the classic barter loop
// (inventry.cc), which performs the moves the player taps.
struct MuiBarterItem {
    std::string name;
    int quantity = 1;
    int unitCost = 0;
    // Inventory picture fid, -1 none.
    int fid = -1;
};
struct MuiBarterView {
    std::string trader;
    std::vector<MuiBarterItem> yours;
    std::vector<MuiBarterItem> theirs;
    std::vector<MuiBarterItem> offer;
    std::vector<MuiBarterItem> ask;
    int offerValue = 0;
    int askValue = 0;
};
struct MuiBarterAction {
    enum Kind {
        None,
        OfferItem, // your item -> your offer
        AskItem, // their item -> what they give
        TakeOffer, // offer -> your items
        TakeAsk, // asked -> their items
        Offer, // the barter key
        Talk, // back to the conversation
    };
    Kind kind;
    int index;
    bool all;
};
void muiBarterUpdate(const MuiBarterView& view);
MuiBarterAction muiBarterTakeAction();
void muiBarterHide();

// Pip-Boy 2000 (mui_pipboy.cc), fed every frame by the classic Pip-Boy loop.
// TakeChoice: a key code of that loop (500..504 tabs, 506.. lines), -1 none.
struct MuiPipboyView {
    std::string status;
    std::string place;
    std::string hp;
    std::vector<std::string> quests;
    std::vector<std::string> disks;
    std::vector<std::string> rest;
    bool canRest = false;
};
void muiPipboyUpdate(const MuiPipboyView& view);
int muiPipboyTakeChoice();
void muiPipboyHide();

// Autotest: center of widget [id] (0..1 of its screen) on either surface;
// [onBottom] says which. False when no widget has that id.
bool muiGetWidgetCenter(const char* id, float* x, float* y, bool* onBottom);

// Called every frame of `muiRunModal` (autotest hook).
void muiSetModalTick(void (*tick)());

} // namespace fallout

#endif /* FALLOUT_MUI_H_ */
