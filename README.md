# re3-fpv

FPV drone simulation integration for the open-source `re3` (*Grand Theft Auto III*) engine on Linux using `libfpv`, `safetyhook`, and RenderWare (`librw`) / OpenGL 3.3.

> **No Game Files Included:** This repository contains **only** hook/bridge source code, CMake build configuration, and documentation. No *Grand Theft Auto III* binaries, `.img`/`.txd`/`.dff` archives, audio, save files, or proprietary game assets are included or distributed here. You must supply your own legally acquired copy of *Grand Theft Auto III* and a locally built `re3` executable.

## Features

- **In-Game Drone Spawn & Recall (`F7`):** Spawns and tears down an `fpv_drone` instance relative to the player's position in Liberty City.
- **Respawn / Reset (`R`):** Re-spawns the drone near the player while active.
- **RenderWare Camera & World Anchor:** Pushes drone attitude and position into `TheCamera` and `rw::Frame` (`RwFrameUpdateObjects`), and routes `FindPlayerCentreOfWorld` to the active drone position.
- **World Raycast Collision:** Connects `fpv_world_set_raycast_callback` to `CWorld::ProcessLineOfSight` for collision against buildings, vehicles, peds, objects, and dummies.
- **Player Control Lockout:** Pauses `CPlayerPed::ProcessControl` while the drone is active or the overlay is open so inputs do not move the player character underneath.
- **Betaflight OSD & Settings Overlay (`F8`):** Renders the Betaflight OSD and interactive `libfpv` overlay via `fpv_render_gl3.h` with OpenGL state preservation.
- **Drone-Centered Streaming Budget:** Expands `CStreaming::ms_memoryAvailable` while flying and calls `CStreaming::StreamZoneModels` around the drone's position.

## Pre-reqs




## Requirements

- Linux (`x86_64`)
- CMake `3.24+` and a C++23-capable compiler (`g++` / `clang++`)
- OpenGL development libraries (`GL`)
- [Compiled libfpv](https://github.com/hammerill/libfpv) (`libfpv.so`, `libfpv.h`, and `docs/renderers/fpv_render_gl3.h`)
- A source-built `re3` executable with symbol table entries (`nm` / ELF symbols) for runtime resolution
- Network access on initial CMake configure to fetch `safetyhook` (and `zydis` / `zycore`)

## Building

```bash
git clone [https://github.com/cfultz/re3-fpv.git](https://github.com/cfultz/re3-fpv.git)
cd re3-fpv
cmake -S . -B build -DLIBFPV_DIR=/path/to/libfpv
cmake --build build -j\$(nproc)
```

You can also set `LIBFPV_DIR` via environment variable before running CMake, or place `libfpv` at `../libfpv` if `CMakeLists.txt` defaults there.

## Running

Launch `re3` from your game directory with `LD_PRELOAD` pointing to `build/libfpv_re3.so`:

```bash
cd "/path/to/Grand Theft Auto III"
LD_PRELOAD=/path/to/re3-fpv/build/libfpv_re3.so ./re3
```

`libfpv` stores its runtime data (`./fpv_data` by default) in the working directory where `./re3` is launched.

## Controls

| Key | Action |
| --- | --- |
| `F7` | Spawn / recall drone |
| `R` | Respawn drone near player (when active and overlay closed) |
| `F8` | Toggle `libfpv` overlay (presets, radio/input setup, settings) |
| `Enter` | Arm / disarm (depending on `libfpv` / Betaflight configuration) |
| `W` / `A` / `S` / `D` + Arrow keys | Flight / stick / trim inputs |
| `T` | Toggle turtle mode (if configured) |

**Note: A controller can be utilized as well, they will need to be configured using F8** 

## Notes & Limitations

* **Struct Offsets & Symbols:** `src/re3_bridge.h` resolves `re3` symbols at load time and uses structure offsets (`CMatrix`, `CColPoint`, `rw::Camera`, `rw::Frame`) verified against a specific `re3` build. Different `re3` commits or compiler settings may require re-checking offsets with `gdb`.
* **Game Assets:** No *Grand Theft Auto III* binaries, data archives, or textures are included in this repository.

## Credits & Acknowledgments

- [libfpv by hammerill](https://github.com/cfultz/re3-fpv.git) for the FPV drone simulation library, OSD/overlay draw data APIs, and fpv_render_gl3.h OpenGL 3.3 renderer.

- safetyhook for inline function hooking on x86_64.

- The open-source re3 project for the Grand Theft Auto III engine implementation and RenderWare (librw) integration.

## AI Disclaimer

Portions of the code, build configuration, debugging notes, and documentation in this repository were written or refined with assistance from AI tools and have been tested and reviewed against re3 and libfpv.

## License

This project is licensed under the **GNU General Public License v3.0** (`GPL-3.0-only`). See the [LICENSE](https://www.google.com/search?q=LICENSE) file for the full license text.