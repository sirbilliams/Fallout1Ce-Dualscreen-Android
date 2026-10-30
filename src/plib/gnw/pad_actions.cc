#include "plib/gnw/pad_actions.h"
#include "mui/mui.h"
#include "mui/mui_settings.h"
#include <cctype>
#include <map>

namespace fallout {

namespace {
    enum Action {
        None,
        Click,
        Context,
        Gesture,
        Back,
        Menu,
        Inventory,
        Skilldex,
        Character,
        Pipboy,
        Automap,
        Sneak,
        Swap,
        ItemMode,
        Combat,
        EndTurn,
        EndCombat,
        QuickSave,
        QuickLoad,
    };

    const char* const kButtonNames[] = { "a", "b", "x", "y", "start", "back", "lb", "rb", "ls", "rs", "up", "down", "left", "right", "lt", "rt" };
    struct ActionName {
        const char* name;
        SDL_Scancode key;
    };
    const ActionName kActionNames[] = {
        { "none", SDL_SCANCODE_UNKNOWN }, { "click", SDL_SCANCODE_UNKNOWN },
        { "context", SDL_SCANCODE_UNKNOWN }, { "gesture", SDL_SCANCODE_UNKNOWN },
        { "back", SDL_SCANCODE_ESCAPE }, { "menu", SDL_SCANCODE_O },
        { "inventory", SDL_SCANCODE_I }, { "skilldex", SDL_SCANCODE_S },
        { "character", SDL_SCANCODE_C }, { "pipboy", SDL_SCANCODE_P },
        { "automap", SDL_SCANCODE_TAB }, { "sneak", SDL_SCANCODE_1 },
        { "swap", SDL_SCANCODE_B }, { "itemmode", SDL_SCANCODE_N },
        { "combat", SDL_SCANCODE_A }, { "endturn", SDL_SCANCODE_SPACE },
        { "endcombat", SDL_SCANCODE_RETURN }, { "quicksave", SDL_SCANCODE_F6 },
        { "quickload", SDL_SCANCODE_F7 }
    };
    const int kDefaultBindings[] = {
        Click,
        Context,
        Skilldex,
        Inventory,
        Menu,
        EndTurn,
        Character,
        Automap,
        Sneak,
        Gesture,
        Combat,
        EndCombat,
        Back,
        Pipboy,
        Swap,
        ItemMode,
    };
} // namespace

const char* padButtonName(int button) { return kButtonNames[button]; }
const char* padActionName(int action) { return kActionNames[action].name; }
int padActionCount() { return sizeof(kActionNames) / sizeof(kActionNames[0]); }
void padParseBindings(const std::string& text, int* bindings)
{
    for (int b = 0; b < kPadButtonCount; b++)
        bindings[b] = kDefaultBindings[b];
    size_t position = 0;
    while (position < text.size()) {
        size_t end = text.find_first_of(", ;", position);
        if (end == std::string::npos) end = text.size();
        std::string pair = text.substr(position, end - position);
        position = end + 1;
        for (char& c : pair)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        size_t equals = pair.find('=');
        if (equals == std::string::npos) continue;
        for (int b = 0; b < kPadButtonCount; b++)
            if (pair.substr(0, equals) == kButtonNames[b])
                for (int a = 0; a < padActionCount(); a++)
                    if (pair.substr(equals + 1) == kActionNames[a].name) bindings[b] = a;
    }
}
std::string padSerializeBindings(const int* bindings)
{
    std::string text;
    for (int b = 0; b < kPadButtonCount; b++)
        if (bindings[b] != kDefaultBindings[b]) {
            if (!text.empty()) text += ',';
            text += std::string(kButtonNames[b]) + '=' + padActionName(bindings[b]);
        }
    return text;
}
int padBinding(int button)
{
    static std::string source;
    static int bindings[kPadButtonCount];
    static bool loaded = false;
    if (!loaded || source != settings.touch.pad_bindings) {
        source = settings.touch.pad_bindings;
        padParseBindings(source, bindings);
        loaded = true;
    }
    return bindings[button];
}
void padSetBinding(int button, int action)
{
    int bindings[kPadButtonCount];
    padParseBindings(settings.touch.pad_bindings, bindings);
    bindings[button] = action;
    settings.touch.pad_bindings = padSerializeBindings(bindings);
    muiSettingsSetMouse();
}
void padResetBindings()
{
    settings.touch.pad_bindings.clear();
    muiSettingsSetMouse();
}
int padActionsUpdate(unsigned int held)
{
    if (muiOptionsPadActive()) return 0;
    int mouse = 0;
    for (int b = 0; b < kPadButtonCount; b++) {
        if (!(held & (1u << b))) continue;
        int action = padBinding(b);
        if (action >= Click && action <= Gesture) mouse |= 1 << (action - Click);
    }
    return mouse;
}
void padActionsHandleEvent(const SDL_Event* event)
{
    static std::map<SDL_JoystickID, unsigned int> held;
    int button = -1;
    bool down = false;
    SDL_JoystickID device;
    if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
        held.erase(event->cdevice.which);
        return;
    }
    if (event->type == SDL_CONTROLLERBUTTONDOWN || event->type == SDL_CONTROLLERBUTTONUP) {
        const SDL_GameControllerButton buttons[] = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
        for (int b = 0; b < 14; b++)
            if (event->cbutton.button == buttons[b]) button = b;
        down = event->type == SDL_CONTROLLERBUTTONDOWN;
        device = event->cbutton.which;
    } else if (event->type == SDL_CONTROLLERAXISMOTION) {
        if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) button = 14;
        if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) button = 15;
        down = event->caxis.value > 16384;
        device = event->caxis.which;
    } else
        return;
    if (button < 0) return;
    unsigned int bit = 1u << button;
    bool wasDown = (held[device] & bit) != 0;
    if (down)
        held[device] |= bit;
    else
        held[device] &= ~bit;
    if (!down || wasDown || muiOptionsPadActive()) return;
    int action = padBinding(button);
    if (kActionNames[action].key == SDL_SCANCODE_UNKNOWN) return;
    // The ordinary keyboard path handles disabled input, layout, text and modals.
    SDL_Event key = {};
    key.type = SDL_KEYDOWN;
    key.key.state = SDL_PRESSED;
    key.key.keysym.scancode = kActionNames[action].key;
    SDL_PushEvent(&key);
    key.type = SDL_KEYUP;
    key.key.state = SDL_RELEASED;
    SDL_PushEvent(&key);
}
} // namespace fallout
