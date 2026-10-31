# Fallout 1 CE Mobile + Dual Screen (unofficial)

![Fallout 1 CE on the AYN Thor: top screen](docs/screenshots/hero-top.jpg)
![Fallout 1 CE on the AYN Thor: bottom screen](docs/screenshots/hero-bottom.jpg)

A touch-first Android port of [fallout1-ce](https://github.com/alexbatalov/fallout1-ce) for the AYN Thor dual-screen handheld.

This is a fan-made project. It is not affiliated with Bethesda or Interplay. You must bring your own copy of the game; no game data is included.

## Status

Playable, in active development. The newest build is always at [Releases → latest](https://github.com/sirbilliams/Fallout1Ce-Dualscreen-Android/releases/tag/latest).

- Touch UI for the main menu, options, character, inventory, save/load, dialogue, barter and Pip-Boy.
- Pip-Boy 2000 theme by default, plus a Classic theme; your choice persists.
- 960x540 widescreen at 2x on the Thor, with a wide interface bar; intro movies play correctly.
- Interactive bottom screen: status card, INV / CHA / PIP / MAP, MENU / SAVE / LOAD / SETTINGS / TOUCHPAD, your weapon with attack, swap and aim mode.
- **Live map** (MAP): the area around you with walls, doors, items, exits and critters, a legend, pan and pinch, tap to walk.
- **Combat panel**: targets with hit chance, aimed shots with a chance per body part, attack modes, quick items, END TURN / END COMBAT.
- **Settings screen** (main menu, in-game Preferences or the console): Game, Sound, Display, Controls and About tabs; changes save immediately.
- **Mouse and controller:** the cursor follows your finger, a bottom-screen TOUCHPAD, a right-stick cursor, and a map action on every controller button, all remappable in Settings → Controls. See [docs/controls.md](docs/controls.md).

Not there yet: bottom-screen world map and loot pages (the game's own screens are used).

See [docs/ui-style.md](docs/ui-style.md) for the UI style guide.

## Screenshots

Top screen above, bottom screen below.

**Pip-Boy 2000 theme (default)**

![Inventory, top](docs/screenshots/inventory-top.jpg)
![Inventory, bottom](docs/screenshots/inventory-bottom.jpg)

![Barter, top](docs/screenshots/barter-top.jpg)
![Barter, bottom](docs/screenshots/barter-bottom.jpg)

![Dialogue, bottom](docs/screenshots/dialogue-bottom.jpg)
![Pip-Boy, bottom](docs/screenshots/pipboy-bottom.jpg)

**New bottom-screen pages**

![Live map, bottom](docs/screenshots/livemap-bottom.jpg)
![Combat panel, bottom](docs/screenshots/combat-bottom.jpg)

![Called shot, bottom](docs/screenshots/called-shot-bottom.jpg)
![Settings: Controls, bottom](docs/screenshots/settings-controls-bottom.jpg)

**Classic theme**

![Barter, bottom](docs/screenshots/barter-classic-bottom.jpg)
![Dashboard, bottom](docs/screenshots/dashboard-classic.jpg)

## Install on the AYN Thor

1. Download `Fallout1-Dualscreen.apk` from [Releases → latest](https://github.com/sirbilliams/Fallout1Ce-Dualscreen-Android/releases/tag/latest).
2. Install the APK.
3. Import your own game folder or zip file. The Windows High Resolution Patch files are ignored automatically.

## Building

- Android: built by CI (GitHub Actions) on every push to main; download the APK from Releases.
- Desktop builds from source (CMake) are only used for development and tests; none are published.

## Credits

- [fallout1-ce](https://github.com/alexbatalov/fallout1-ce) by alexbatalov, the engine this port is built on.
- This port: touch UI, themes, widescreen, dual-screen pages, mouse and controller controls.


Engine documentation (configuration, desktop platforms): see the upstream [fallout1-ce README](https://github.com/alexbatalov/fallout1-ce#readme).
