#ifndef FALLOUT_DEV_AUTOTEST_H_
#define FALLOUT_DEV_AUTOTEST_H_

namespace fallout {

// `--dev-autotest` (desktop, headless): contract tests, then every ported
// mobile screen is opened on a test bottom display and captured as BMP into
// $FALLOUT_AUTOTEST_DIR (top: <name>-top.bmp, bottom: <name>-bottom.bmp).
// Returns false on failure.
void devAutotestStartMovieShots();
bool devAutotestRun();

// Same for the screens that need a loaded game (called with a map loaded).
bool devAutotestGameRun();

} // namespace fallout

#endif
