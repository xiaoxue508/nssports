# NS Sports Recomp

[English](README.md) | [简体中文](README.zh-CN.md)

**NS Sports Recomp** (`nssports`) is a static recompilation of *Nintendo Switch Sports* (Nintendo Switch, USA v0) into a native Windows x86-64 application.

> [!IMPORTANT]
> **No game files are included.** You need your own legally obtained copy of *Nintendo Switch Sports* (USA v0), dumped from your own console, along with your own `prod.keys` and Switch Mii system firmware resources. Instructions for obtaining those files will not be provided.
>
> The game is built from your files on your machine, so the result contains the game. It is for your own use. **Do not share the built executable or extracted game files.**
>
> This project is an independent, non-commercial research effort and is not affiliated with, endorsed by, or sponsored by Nintendo Co., Ltd. *Nintendo Switch Sports*, *Nintendo Switch*, *Joy-Con*, and *Mii* are trademarks of Nintendo. Online services / Nintendo Switch Online multiplayer are not supported.

## Status

Early development / active porting. The game boots through the wrist-strap screen and intro into Spocco Square (main plaza) on Windows with Vulkan, and supports local 1P & 2P play with physical Joy-Con (L/R) controllers (using Guest accounts for 2P). Visual glitches, missing intro video frames, and game-specific issues in individual sports are still being worked on.

## Features

- Native x86-64 code, not emulation
- Vulkan rendering backend with `VK_NV_geometry_shader_passthrough` + `VK_NV_viewport_array2`, wait-free 16-bit atomics, and indirect-clear vertex shader emulation
- Configurable **3D Render Scale** (`50%`–`200%`), **AMD FSR 1** / Bilinear upscaling, sharpness, color presets, HDR10 output, and `F12` on-demand frame dumps
- Native Windows Bluetooth/USB HIDAPI driver for **Joy-Con (L)**, **Joy-Con (R)**, and **Pro Controller**, with per-controller 6-axis IMU motion fusion and HD rumble
- Local **2-Player** support with automatic single Joy-Con splitting (`auto` / `dual` / `single` pairing modes) and automatic Player LED updates
- Offline `nn::account` and `nn::mii` database support
- In-game settings menu (`ESC` / `F1`) and performance overlay (`F10`)
- Stereo or 5.1 surround sound output

## Known Limitations

- The H.264 intro cutscene currently decodes `0 frames` (passes through cleanly to the main menu).
- Local 2-player mode is verified with Guest accounts (`null_guard_2p` patch in `recomp/patches-v0.txt`).
- Individual sports modes are still undergoing testing and verification.

## Building (Windows)

Requirements: Windows 10/11 (x86-64), a Vulkan 1.3 capable GPU, Bluetooth adapter (for wireless Joy-Cons), MSYS2/Clang/Ninja toolchain, `.NET` SDK (shader translator), and Python `.venv`.

1. Place your legally dumped base NSP in `rom/` and `prod.keys` in `keys/` (both directories are gitignored).
2. Configure `config.local.toml` with the paths to your base NSP, keys, firmware Mii directory, and local toolchains.
3. Run the full build (keep `TEMP`/`TMP` on `E:` to avoid writing build caches to `C:`):
   ```powershell
   $env:TEMP="E:\nssports\build\tmp"; $env:TMP="E:\nssports\build\tmp"
   E:\nssports\.venv\Scripts\python.exe tools/build_game.py --version v0 --jobs 8
   ```
4. For incremental `runtime/` C/C++ builds after the initial generation:
   ```powershell
   $env:TEMP="E:\nssports\build\tmp"; $env:TMP="E:\nssports\build\tmp"
   E:\dkb\DKB-Setup\bin\ninja.exe -C E:\nssports -f E:\nssports\build\v0\build.ninja
   ```

## Running & Controls

1. Pair your **Joy-Con (L)** and/or **Joy-Con (R)** to Windows via Bluetooth.
2. Run `build/v0/nssports.exe`:
   ```powershell
   cd E:\nssports\build\v0
   .\nssports.exe
   ```
3. **Hotkeys**:
   - `ESC` / `F1`: Open or close the in-game settings menu (Joy-Con pairing mode, gyro calibration, 3D render scale, FSR, audio device, etc.)
   - `F10`: Cycle performance overlay (`Off` / `FPS` / `Full`)
   - `F11`: Toggle fullscreen
   - `F12`: Trigger a single-frame render pass & texture dump

## Credits & Third-Party Notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for license details of included or adapted open-source components.
Built upon the static recompilation runtime and tooling of [totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp) / MacNativeGames (MIT), `nxnative` (MIT), and `Ryujinx.Graphics.Shader` (MIT).
