# The Coffin of Andy and Leyley for PSP

An unofficial PSP port of The Coffin of Andy and Leyley (version 3.0.12), with an engine written from scratch.

This repository contains **no game content**. You bring your own copy of the game, and the builder turns it into a
single `EBOOT.PBP` for a PSP or PPSSPP. Every image, sound and line of dialogue comes from your copy. Please
[buy the game on Steam](https://store.steampowered.com/app/2378900/); the port is useless without it.

## How to get it

**In the browser:** open [psp-tcoaal.ox80.cc](https://psp-tcoaal.ox80.cc), drop in your game folder (or a .zip of
it), press Build, and download the result. Your game files never leave your computer.

**On the desktop:** download the builder for your system from Releases and double-click it. It finds the game in
your Steam library (or asks for the folder) and asks where to save.

Either way you end up with `PSP/GAME/TCOAAL/EBOOT.PBP`. Copy the `PSP` folder to the root of your memory stick, or
open the EBOOT in PPSSPP. Saves go into `PSP/GAME/TCOAAL/saves` and are kept when you update.

You need a PSP-2000, PSP-3000 or PSP Go with custom firmware (the PSP-1000 lacks the extra memory), or PPSSPP, and
about 170 MB free on the memory stick.

## Controls

| PSP | Action |
|---|---|
| D-pad | Move, select |
| Cross | Interact, confirm, advance text |
| Circle | Cancel |
| Triangle / Start | Menu |
| Square (hold) | Run |
| R (hold) | Fast-forward dialogue |
| L / R | Page up / down in lists |

The analog stick doesn't do anything yet. The stick on my own PSP-3000 drifts, so I can't test it properly; stick
support is planned once I've fixed mine.

## Status and plans

- **Now:** the engine plays version 3.0.12 on real PSP hardware and in PPSSPP. It's a work in progress, so expect
  bugs; reports are welcome.
- **Voice acting:** I'm working on bringing the fan voice-over by Aster and Fenrir (from their let's plays) into the
  port. I need their approval before it goes in, so it isn't part of the port yet.
- **Mods:** mods are being ported too and will ship together with the dubbing expansion. ETA: about two weeks.

## How it works

- `engine/` is a C++ port of the RPG Maker MV runtime (maps, events, interpreter, pictures, screen effects) plus
  re-implementations of the plugins and custom systems the game uses. It runs on the PSP (GU, sceAudio) and on the
  desktop (SDL2 with a software renderer that matches the PSP output, used for testing).
- `tools/builder/` is a C99 builder that decrypts your game files, scales the artwork to 480x272, packs maps, events
  and dialogue, renders the game font and stores it all inside the `EBOOT.PBP` next to the prebuilt engine. It refuses
  other game versions and warns when a copy looks cracked.
- `web/` is the in-browser builder: the same C code compiled to WebAssembly.

## Building from source

You need `make`, `cmake`, a C compiler and `curl`. The PSP toolchain and zig are downloaded into `_deps/` on first use.

```
make                  # build the PSP engine and the builder for this machine
make run              # run the builder (GAME=path/to/game and OUT=folder are optional)
make desktop          # desktop test build (needs SDL2, libvorbis, zlib); make play runs it
make web              # build the in-browser builder into build/web
make release          # cross-compile the builder for Linux, Windows and macOS
make help             # every target
```

## License

All rights reserved; this is not open source. You may build and run it for your own personal use, but you may not
modify, redistribute or publish it or builds of it. That's only so the port can't be used to pirate the game or to
pass around malware under its name: every release comes from here. See [LICENSE](LICENSE), which also lists the
third-party parts and their own licenses.

The Coffin of Andy and Leyley is (c) Kit9 Studio / Nemlei. This project is not affiliated with or endorsed by them.
