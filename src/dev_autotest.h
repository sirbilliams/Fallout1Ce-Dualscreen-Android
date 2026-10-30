#ifndef FALLOUT_DEV_AUTOTEST_H_
#define FALLOUT_DEV_AUTOTEST_H_

namespace fallout {

// `--dev-autotest` (desktop, headless): contract tests, then every ported
// mobile screen is opened on a test bottom display and captured as BMP into
// $FALLOUT_AUTOTEST_DIR (top: <name>-top.bmp, bottom: <name>-bottom.bmp).
// Returns false on failure.
bool devAutotestRun();

} // namespace fallout

#endif
