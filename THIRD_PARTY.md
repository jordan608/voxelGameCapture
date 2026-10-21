# Third-party software

The ones from outside Voxon (UE4SS, BepInEx, ReShade and its SDK headers, Cyber Engine Tweaks, RED4ext, Codeware) are
**not in git**: they are fetched after cloning, and checked by their SHA-256 (`docs/DEVELOPER_GUIDE.md`, *What is in git,
and what is not*). The player's packaged folder carries them, under their licences.

| What | Where | Source | Licence |
|---|---|---|---|
| UE4SS 3.0.1 | `redist/UE4SS_v3.0.1.zip`, installed into Unreal games | github.com/UE4SS-RE/RE-UE4SS, release v3.0.1 | MIT |
| UE4SS 3.0.1-1165-g12ec45a0 (experimental) | `redist/UE4SS_v3.0.1-1165-g12ec45a0.zip`, installed into the games that name it in `games.json` (Mortal Kombat 1) | github.com/UE4SS-RE/RE-UE4SS, release experimental-latest, 2026-10-12 | MIT |
| BepInEx 5.4.23.5 | `redist/BepInEx_win_x64_5.4.23.5.zip`, installed into Unity games | github.com/BepInEx/BepInEx, release v5.4.23.5 | LGPL-2.1 (the HarmonyX, MonoMod and Mono.Cecil libraries inside it: MIT) |
| BepInEx 6.0.0 be.788 (Unity IL2CPP) | `redist/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.788+5b766a3.zip`, installed into Unity IL2CPP games | builds.bepinex.dev, bleeding edge build 788 | LGPL-2.1 |
| Cyber Engine Tweaks 1.37.1, RED4ext 1.30.0, Codeware 1.20.5 | `redist/cet_1.37.1.zip`, `redist/red4ext-1.30.0.zip`, `redist/Codeware-1.20.5.zip`, installed into Cyberpunk 2077 | github.com/maximegmd/CyberEngineTweaks, github.com/wopss/RED4ext, github.com/psiberx/cp2077-codeware | MIT |
| ReShade 6.7.3, the build with add-on support | `redist/ReShade64.dll`, installed into games as `dxgi.dll` | reshade.me | BSD 3-Clause |
| ReShade add-on SDK headers 6.7.3 | `addon/reshade-sdk/` | github.com/crosire/reshade (see `VENDORED.md`) | BSD 3-Clause |
| VLED Volume Capture shaders (pack, compact) | `addon/shaders/`, adapted | Voxel Photonics, VLED Unreal plugin 1.0 (see `addon/PORTED.md`) | Voxel Photonics |
| Voxon SDK wrappers and display layer | `host/VoxonCaptureRig/Voxon/` | GameKit, commit 2dc424e (see `Voxon/COPIED.md`) | GameKit / Voxon Photonics |
| Avalonia 11.3.0 | NuGet, at build time | avaloniaui.net | MIT |

**Not included, ever:** the Voxon runtime DLLs (`LedHost.dll`, `LedWin.dll`, `FTD3XXWU.dll`) and `ledhost.ini`. The
app finds the installed VLED runtime on the PC it runs on.

The ReShade build with add-on support is meant for single-player games. The rig is installed for offline play only.
