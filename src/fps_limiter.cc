#include "fps_limiter.h"
#include "platform_compat.h"

#include <SDL.h>

namespace fallout {

FpsLimiter::FpsLimiter(unsigned int fps)
    : _fps(fps)
    , _ticks(0)
{
}

void FpsLimiter::mark()
{
    _ticks = compat_ticks();
}

void FpsLimiter::throttle() const
{
    if (compat_fixedClock()) {
        return;
    }
    if (1000 / _fps > SDL_GetTicks() - _ticks) {
        SDL_Delay(1000 / _fps - (SDL_GetTicks() - _ticks));
    }
}

} // namespace fallout
