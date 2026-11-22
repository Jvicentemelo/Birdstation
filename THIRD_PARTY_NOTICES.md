# Third-party software

Birdstation's own code is MIT licensed (see `LICENSE`). It uses:

| Component | Use | License |
|---|---|---|
| [SDL2](https://libsdl.org) | window, audio, gamepad (downloaded by CMake, linked statically) | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) (`third_party/imgui`) | user interface | MIT |
| [stb_image](https://github.com/nothings/stb) (`third_party/stb`) | cover image decoding | MIT / public domain |
| [libretro.h](https://github.com/libretro/libretro-common) (`third_party/libretro`) | libretro API header | MIT |
| [Snes9x](https://github.com/snes9xgit/snes9x) via the [libretro port](https://github.com/libretro/snes9x) | SNES emulation core, **loaded at run time** from `cores\snes9x_libretro.dll` | Snes9x license (non-commercial, see below) |

## Snes9x

Snes9x is distributed under its own license, which allows free use and redistribution
**for non-commercial purposes only**. The full text is in the Snes9x repository:
<https://github.com/snes9xgit/snes9x/blob/master/LICENSE>.
The core DLL is downloaded from the official libretro build bot (`scripts\fetch-core.ps1`)
and is not part of the Birdstation source code. Do not sell Birdstation bundled with it.

## Content

* No ROMs or BIOS files are included. Use only games you legally own.
* Box art is optionally downloaded from the
  [libretro-thumbnails](https://github.com/libretro-thumbnails) project. The artwork belongs to
  its respective owners and is not distributed with Birdstation.
