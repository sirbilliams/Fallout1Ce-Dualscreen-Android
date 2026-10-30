#ifndef FALLOUT_PAD_ACTIONS_H_
#define FALLOUT_PAD_ACTIONS_H_
#include <SDL.h>
#include <string>
namespace fallout {
constexpr int kPadButtonCount = 16;
const char* padButtonName(int button);
const char* padActionName(int action);
int padActionCount();
int padBinding(int button);
void padParseBindings(const std::string& text, int* bindings);
std::string padSerializeBindings(const int* bindings);
void padSetBinding(int button, int action);
void padResetBindings();
// Aggregate held physical buttons; returns mouse bits (left, right, R3 gesture).
int padActionsUpdate(unsigned int held);
void padActionsHandleEvent(const SDL_Event* event);
}
#endif
