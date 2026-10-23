# The Minecraft Dungeons II mod

Minecraft Dungeons II ends itself when its code is hooked, so the rig cannot go in through UE4SS. It loads content mods
through Blueprint Loader, though, and the rig goes in as one: `VoxonRig`, rig.lua's cube of cameras as a Blueprint
actor (`docs/RESULTS.md` 25). Its built pak files are in `redist/blueprint/mcdungeons2/`, which Install rig copies
into the game's `Dungeons\Content\Paks\~mods\VoxonRig\`. Build it again only when `make_mod.py` changes.

| File | What it is |
|---|---|
| `make_mod.py` | Makes the whole mod - the actor, its 18 capture components, its graph, its settings target - inside the Blueprint Mod Template, in the editor run headless. The settings file's layout in it must match `host/VoxonCaptureRig/BlueprintBridge.cs` |
| `VoxonGraph/` | An editor plugin of ours: Python cannot make Blueprint graph nodes, so `make_mod.py` asks this for them (events, calls, variables, branches, links, pin values). Editor only: nothing of it is in the mod |

## Building it

Needs **Unreal Engine 5.6.1** (the game's version; Epic Games Launcher) and the **Blueprint Mod Template** 2.4
(Nexus Mods, Minecraft Dungeons II, Blueprint Loader, its Files tab; `docs/DEVELOPER_GUIDE.md` has its SHA-256). Keep
paths short: Unreal's build fails past Windows' 260-character path limit.

1. Unzip the template to a folder of its own, say `D:\Unreal Projects\VoxonRigMod`. The project inside is called
   `Dungeons`, as the game's is, and must stay so.
2. Build `VoxonGraph` for the editor (`<UE>` is the engine's folder):
   ```
   <UE>\Engine\Build\BatchFiles\RunUAT.bat BuildPlugin -Plugin=<repo>\tools\mcdungeons2-mod\VoxonGraph\VoxonGraph.uplugin -Package=D:\UEBuild\VoxonGraph -TargetPlatforms=Win64
   ```
   If it crawls (Unreal Build Accelerator: the compiler at almost no CPU for many minutes), stop it and build the host
   project it made directly, about 8 minutes:
   ```
   <UE>\Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -Project=D:\UEBuild\VoxonGraph\HostProject\HostProject.uproject -Plugin=D:\UEBuild\VoxonGraph\HostProject\Plugins\VoxonGraph\VoxonGraph.uplugin -NoUBA -NoHotReload
   ```
   Copy the built plugin (`VoxonGraph.uplugin`, `Binaries`, `Source`) into the template's `Dungeons\Plugins\VoxonGraph`.
3. Make the mod:
   ```
   <UE>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe <template>\Dungeons\Dungeons.uproject -run=pythonscript -script=<repo>\tools\mcdungeons2-mod\make_mod.py -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosplash -nullrhi -stdout
   ```
   It replaces `Content/Mods/VoxonRig` each time. A link the graph cannot make stops it with the pins both nodes have.
4. Cook and pack it with the template's own script: `python <template>\build_mod.py VoxonRig --engine <UE>`, which
   leaves `VoxonRig_P.pak`, `.ucas` and `.utoc` in `<template>\Build\VoxonRig`.
5. Copy those three into `redist/blueprint/mcdungeons2/`, build the app again, and Install rig.

## How it works

- **Settings.** A Blueprint cannot read the app's `control.txt`, so the app writes the rig worked out - each camera's
  place, turn, lens and clip - as `Dungeons\voxon_rig.png` beside the game (32 x 1, 16 bits a channel, 72 values),
  and the mod reads it twice a second. Until it is there the mod uses the profile's settings, worked out by
  `make_mod.py` the same way.
- **Rebuilds.** Whenever which faces, their size, Lighting below 1 or Player look change, the mod makes new targets
  for every capture and clears each to its face code, and holds the captures off 2 s while the add-on reads them, as
  rig.lua does. The add-on only looks for codes in targets made in its last few seconds.
- **Player look** shows each face's third capture only the level's Pawns: the hero and the mobs.
- Not in it: see-through surfaces, and F7.
