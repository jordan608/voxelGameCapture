# Voxon Capture Rig

Plays supported PC games on a Voxon volumetric display (VX2 / VX2-XL), or in the Voxon simulator when no display is
attached. There is one capture method: a **cube of depth cameras**. Up to six cameras sit on the faces of a box around
the player and capture the game's lit colour and depth. The GPU turns those pictures into voxels, and this app draws
them on the display.

## Using it

1. **Start `VoxonCaptureRig.exe`.** The left side shows the volume as the display sees it: drag to orbit, scroll to
   zoom, double-click to reset the view.
2. **Games tab: pick your game.** The app finds Steam and Epic installs by itself. If it says *not found*, use
   **Browse...** and choose the folder the game is installed in.
3. **Close the game, then press Install rig.** This adds the capture rig to the game's folder. Nothing else on your PC
   is changed.
4. **Start the game and load into play.** The rig switches itself on once there is a player. **F7** stops and starts it.
5. **Cameras tab: adjust while you play.** Changes reach the game within a second.
   - *Display*: the size of the Voxon display (or simulator model) running, as the Voxon runtime reports it. The
     cameras are framed to it, and to a new one within a second of a change.
   - *Cameras*: which faces of the cube have a camera. More cameras see more sides of things, and each one costs frame rate.
   - *Resolution*: pixels per camera. 1024 px is about the most a VX2-XL can show, and it is plenty.
   - *World radius*: how much of the game world fits in the display, in metres from the player to the edge.
   - *Camera distance*, *Rig rotation*, *Side camera tilt*: how the cameras sit around the volume.
   - *World X, World Y, World height*: how much world each axis holds, as a multiple of World radius. Above 1 more is
     squashed in, below 1 it is stretched. A fighting game's stage, deep behind the fighters, fits at World X 2-3.
   - *Turn the volume with the player*: on, the volume turns as the player looks around, and what is ahead on the
     screen is always toward the back of the display. Off, the world keeps its own orientation.
   - *Lighting*: how much of the game's lighting the volume shows. 100% is the game's light and shadow; lower mixes
     it over the surfaces' own colours, and 0% is those colours alone, evenly lit. Games with harsh lighting (Mortal
     Kombat 1) look best low. Below 100% every camera renders twice.
   - *Auto-exposure: lowest* and *highest*: how far, in stops, the automatic exposure may darken or brighten what the
     cameras took while it brings the volume's average to middle grey. Both 0 turns it off.
   - *Brightness* and *Colour range*: the exposure on top of the automatic one, and the band the display draws a
     voxel's brightness into (try 80 and 220). Only the brightness moves: each colour keeps its hue and saturation.
   - *Gamma* and *Saturation*: above 1, gamma lifts the dark and middle tones without blowing out the bright ones.
     Saturation 0 is grey, 1 the colours as captured, 2 twice as vivid.
   - *Player look* (Unreal and Unity games): on, each camera also captures the player alone, so the player gets a brightness
     (stops on top of the world's), gamma and saturation of their own, and a *density*: their surface drawn that many
     voxels deep, so they look more solid and brighter on the display. Each camera captures once more.
   - *See-through surfaces* (Unreal and Unity games): glass, holograms, lightsaber blades and bolts are see-through to
     the game and leave no depth, so the cameras put their colour on whatever is behind them, or nowhere. Tick *List
     the see-through materials*, play on, and the list fills with the ones the cameras meet; tick a material and it
     stands where it is, in its own colours. While a ticked one is near, each camera renders once more. *Every one at
     once* also makes fog, smoke and outlines solid, so ticking what you need is usually better. *Solid where at least
     this opaque* (Unreal games) keeps a surface solid only where it is that opaque: a lightsaber's mesh is wider than
     its glowing core, and at 0% the whole mesh stands as a flat card. Above 0% each camera renders once more while a
     ticked one is near. Jedi: Fallen Order starts with its lightsaber blade ticked, from 50%; The Ascent with its
     hologram doors.
   - *Feet x, y, z*: where the player stands in the display. With the volume turning, move the feet toward the front
     (y up to 4) to leave more of the volume for what is ahead.
6. **To undo it, close the game and press Remove rig.** The game's folder goes back exactly as it was, including any
   ReShade, UE4SS, BepInEx, Cyber Engine Tweaks, RED4ext or Codeware you had installed yourself.

### Good to know

- **Turn off frame generation** (DLSS-G / FSR 3 frame generation) in the game.
- **Use DX12** when the game offers it. DX11 is supported, but it is limited by the CPU and slower with six cameras.
- **Offline play only.** The rig is a mod running inside the game. Keep it out of online and competitive modes; for
  Mortal Kombat 1, use Practice and Towers.
- Glass, smoke and other see-through effects have no depth, so the cameras cannot place them in the volume.
- Every game the rig supports is listed in the Games tab. Each one notes whether it has been tested.

## Supported games

The list is in [`games.json`](games.json): The Ascent (tested), Risk of Rain 2 (tested), Mortal Kombat 1 (tested), The
Witcher 3 (tested), Cyberpunk 2077 (tested), Star Wars Jedi: Fallen Order (tested), Star Wars Battlefront II (tested,
offline), The Sims 4 (tested), Crysis (tested; the game needs c1-launcher to start), Gang Beasts (tested in a local match), Totally Accurate Battle
Simulator (tested; the volume follows the game camera's view), F1 2020 (the Showroom tested), Minecraft Dungeons II
(tested; needs Blueprint Loader, the player's download), Black
Myth: Wukong, Oblivion Remastered, Sifu, Mothergunship, Train Sim World 2, and Unreal Tournament. Unreal Engine 4 and 5
games on DX11 or DX12 are covered, and Unity games on DX11 (Mono through BepInEx 5; IL2CPP through BepInEx 6, in
testing), all with the same cube of cameras; The Witcher 3 and Cyberpunk 2077 each have a rig of their own,
Battlefront II's faces are views of its engine's own, added by the add-on, The Sims 4's and Crysis's are drawn by
the game's own renderer from cameras the add-on makes, F1 2020's are the game's own reflection cubemap, and Minecraft
Dungeons II's cube is a Blueprint Loader mod of ours, its code being protected.

[`docs/GAMES.md`](docs/GAMES.md) covers every game in the Steam and Epic libraries: its engine, graphics API, and
anti-cheat, whether the rig can go in, and how. It also covers the games in focus and where each stands, Skyrim and
GTA V among them (both need the renderer-hook route; neither is started).

## For developers

- [`docs/DEVELOPER_GUIDE.md`](docs/DEVELOPER_GUIDE.md): how this was made, what is in git and what is not (the
  third-party files to fetch after cloning), how to build it, and how to add a game. Start here.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): how it works, what each folder is, and how to add a game.
- [`docs/RESULTS.md`](docs/RESULTS.md): what the rig costs and delivers, as measured.
- Build: `tools\package.ps1`. To check the installer and the pipe without a game:
  `VoxonCaptureRig.exe --selftest` (writes `selftest.txt`). For the GPU decode: `addon\build.ps1`.
