# Controller controls

SETTINGS is available from the main menu and bottom console. In-game OPTIONS >
PREFERENCES opens Game; OPTIONS > CONTROLS opens the Controls tab.
Game contains the original difficulty, combat, text, running and display
preferences; Sound has four volume sliders. Display has Pip-Boy 2000 / Classic,
HUD scale, bottom text size, reduce motion, colour-blind safe and dual screen.
About shows version/build, a source link and a licences note.

Tap tabs or use LB/RB to switch. D-pad up/down moves focus and scrolls it into
view; A/right cycles forward and left cycles backward. B/Start goes back.
Touch sliders drag; pad sliders change one step. Every change saves immediately
through the existing game preferences or fallout.cfg setter. Reset on Game,
Sound and Display restores that tab. RESET PAD TO DEFAULTS resets bindings only.
Both themes share these controls. Remap labels use player-facing button/action
names (for example “B — Right click” and “D-pad left — Back”).

| Button | Default action | F1 input |
|---|---|---|
| A | Click (held) | Left mouse at cursor |
| B | Right click (held) | Right mouse at cursor |
| X | Skilldex | S |
| Y | Inventory | I |
| Start | Game menu | O |
| Back / Select | End turn | Space |
| LB | Character | C |
| RB | Automap | Tab |
| L3 | Sneak | 1 |
| R3 | Tap click / hold right click | Existing mouse gesture |
| D-pad up | Start combat | A |
| D-pad down | End combat | Enter |
| D-pad left | Back | Esc |
| D-pad right | Pip-Boy | P |
| L2 | Swap hands | B |
| R2 | Item mode | N |

Right stick moves the cursor with the existing dead zone, curve and speed.
A/B held mouse behavior and R3 tap/hold timing are unchanged by default.
Triggers become buttons above half travel, rearming below half travel.
Keyboard shortcuts fire once per press through SDL's ordinary keyboard event
queue; active screens, text fields and dialogs process them exactly as physical
keys. They do not invoke gameplay functions behind a modal screen. CONTROLS
consumes controller navigation and suppresses mapped mouse/keyboard actions.
N changes weapon/item mode in F1; it does **not** use the active item.
Space ends a turn; Enter requests combat end, subject to the game's rules.
These shortcuts are verified in `src/game/game.cc` and `src/game/combat.cc`.

## Config remapping

`fallout.cfg` section `[touch]`, key `pad_bindings`, overrides code defaults:

```ini
[touch]
pad_bindings=x=inventory,y=skilldex,up=quicksave,down=none
```

Button names: `a b x y start back lb rb ls rs up down left right lt rt`.
L3/R3 use `ls`/`rs`; L2/R2 use `lt`/`rt` (the F2 names).
Action names: `none click context gesture back menu inventory skilldex character
pipboy automap sneak swap itemmode combat endturn endcombat quicksave quickload`.
`context` means right click; `gesture` means the R3 tap/hold behavior.
Quick save/load use F6/F7 and are available for remapping without default bindings.
Names ignore case; commas, spaces and semicolons separate pairs. Unknown pairs
are ignored and the last valid pair wins. Saving writes only differences from
defaults, in button order; reset writes an empty override string.

## Verification

The desktop `--dev-autotest` with `FALLOUT_AUTOTEST_MOUSE=1` and
`FALLOUT_AUTOTEST_DIR=<existing output directory>` checks parsing and minimal
serialization round trips, the native keyboard queue, trigger rearming,
disabled input, touch cycling, pad focus/scroll/cycling, reset, and
captures CONTROLS in both themes (`68` through `71` BMPs). Run with `timeout 120`,
`SDL_VIDEODRIVER=offscreen`, `SDL_AUDIODRIVER=dummy`, and a scratch working
folder whose config references legally installed F1 data by absolute path.
The standard captures now include `08-settings-{game,sound,display,controls,about}`
(top/bottom BMPs). The settings autotest changes the theme, loads fallout.cfg
into a fresh Config, checks the saved value, and restores the original theme.
Run each theme with `FALLOUT_AUTOTEST_THEME=2` or `0`.

Vault-kit goldens affected: `01-main-menu-bottom`, `02-options-bottom`,
`10-dashboard-ingame-bottom` (the SETTINGS button); add the five `08-settings-*`
top/bottom pairs for each theme. Mouse captures `67` through `71` also change.
