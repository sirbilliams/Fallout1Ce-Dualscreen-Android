# Lane 1 platform handoff

`src/mobile_contracts.h` owns the F1 value-only `MobileSnapshot`, bounded FIFO
`MobileCommand` queue, `MobileScreen` identifiers and `MobileSettings` contract.
The recon specified no exact snapshot/command type names; these are the shared
names for subsequent lanes. The donor's independent `bottomScreen*` names remain.

All snapshot publication, command enqueue/dequeue and UI rendering happen on the
game thread. JNI only pushes SDL events. Readers copy snapshot values and must not
retain references across publication. The active screen adapter increments
`generation` whenever ownership/modal context changes; publishing a new generation
clears pending commands. Enqueue rejects stale generations and the 65th command.
The owning lane 2/3 adapter must recheck generation, F1 legality/AP/turn state and
consume each command once. No gameplay dispatcher is enabled in lane 1.
`entries` and integer IDs are placeholders for typed F1 view models in lanes 2/3;
never insert engine pointers or F2 save/object assumptions into these models.

Settings: `fallout.cfg`, section `[touch]`, integer booleans `enabled=1` and
`bottom_screen=1`. Missing keys default to enabled. Native reads these settings;
lane 4 owns persistence, Java onboarding and further schema. Unknown keys remain
under the original config system. The Presentation currently follows activity
resume/pause and eligible display availability independently of native settings.

The top SDL renderer keeps the classic 640x480 logical coordinate system and
letterbox-aware SDL finger normalization. The existing GNW gesture recognizer
maps that to the mouse. SDL touch/mouse synthesis is disabled to avoid duplicate
input. Bottom events use a dedicated touch device and finger range and are
consumed without reaching classic gestures. Lane 2 can route them into a bottom
UI; do not feed them into both surfaces. Cancel currently ends a bottom pointer;
future bottom UI must treat cancellation as cancellation, not a click.

The donor Presentation/display selection and mutex-protected RGBA transport are
ported with F1 JNI symbols. Frames are owned by the game thread, windows by Java;
size mismatches and detached surfaces drop frames. Native render reset recreates
top textures. No display means classic single-screen fallback. Desktop has no
bottom display except `bottomScreenSetTest`. Lane 1 posts only the static label
“Fallout 1 CE - bottom screen”, drawn with the F1 font.

## Local verification

Build with `timeout 900 cmake --build build-desktop --target fallout-ce -j 8`.
Configured dependencies were reused with `FETCHCONTENT_FULLY_DISCONNECTED=ON`.
Use a scratch working directory containing `fallout.cfg` with absolute references
to the user's real MASTER.DAT, CRITTER.DAT and DATA directory; never copy game data.

`timeout -k 5 120 env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
FALLOUT_AUTOTEST_BMP=/tmp/menu.bmp /path/to/fallout-ce --dev-autotest`
starts the real engine, skips intro movies, tests queue capacity/FIFO/staleness and
bottom attach/detach/resize, captures the actual main-menu texture as BMP, and
exits via the normal menu shutdown. Missing capture path/save failure returns 2.
The offscreen driver alone may continue without physical relative mouse capture.
Convert the BMP outside the repo to PNG. Final capture:
`~/Projects/f1-shots/lane1-menu.png`.

Android Gradle cannot run in this sandbox. CI must verify both ABIs, JNI linkage,
SDL Java/native sync, SDK 35/JDK 17/NDK 27.3 builds, APK identity and signing.
Secrets `THOR_KEYSTORE_B64` and `THOR_KEYSTORE_PASSWORD` use alias `fallout1thor`;
without them CI uses an ephemeral debug key for the release APK. On main push it
publishes the `latest` prerelease. PR builds only upload artifacts.

Lane 5 still needs Android launch with real F1 data, landscape edge/letterbox taps,
Thor and emulator attach/detach, pause/resume, texture reset, stale/cancel input,
page-size/device packaging verification and real-game scenario screenshots.
Lanes 2–4 implement screens, gameplay adapters, saves/onboarding and persistence.
