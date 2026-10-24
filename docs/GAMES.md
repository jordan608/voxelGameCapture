# Games

Which games the capture rig can go into, and how. There is one capture method: a **cube of depth cameras**
round the player, decoded on the GPU into voxels. What changes from engine to engine is only how the
cameras get into the game. The mesh layers the earlier projects built (Skyrim, Cyberpunk) are not carried
on.

**How sure each value is.** *(measured)* means read off an install or recorded by the earlier projects
(VoxelSteamTap, Voxel64). Values without that mark are public knowledge, not checked on these PCs. **Check
anti-cheat on the install before anything is attached:** the earlier anti-cheat verdicts were never audited
one by one.

**The PCs.** Machine A is the main games PC. Machine B is your other PC, with GTA V under a different Steam
account. Machine C is this one.

## Three ways the cube gets into a game

1. **Extra cameras, in the same frame.** The engine itself renders the extra views: Unreal's
   `SceneCapture2D`, Unity's `Camera`, REDengine 4's render-to-texture cameras (the kind its holocalls use). The
   player's own view is untouched, and each camera costs its render on the GPU. The rig places the cameras on the
   faces of a box and clips each one at the box's near face, so walls outside the volume never block them.
   **Unreal, Unity and Cyberpunk 2077: built and working.**
2. **The game's own camera, swept round the cube. Ruled out (2026-10-16): only cameras the engine renders in
   the same frame.** A mod moved the game's camera to each face in turn, one face per game frame, and the player
   saw only the frames left over. GTA V's sweep left 22% of frames on the monitor, and you judged it "not working
   well". Skyrim's and Cyberpunk's sweeps pointed **outward** from the player, which caused Skyrim's dark wedges.
   An **inward** sweep, built for Witcher 3 (2026-10-13), worked, but the moving camera set the world streaming
   in and out behind it and the free camera took the controls. It stays only as Witcher 3's fallback for game
   builds its hook does not know.
3. **The game's own renderer, hooked: extra views in the same frame.** For an engine with no scene capture,
   the add-on hooks the engine's own drawing and has it draw the world again from each face's camera, in the
   same frame as the player's view, which stays untouched, as with route 1. **Built and working for The
   Witcher 3 (2026-10-13), Star Wars Battlefront II (2026-10-16, the engine's own list of views), The Sims 4
   (2026-10-18) and Crysis (2026-10-20).** Each needed its way in found in that game's code, so each knows its own
   game build. This is the route for Skyrim and GTA V. (REDengine 4 was a candidate too, but Cyberpunk turned
   out to have cameras of its own: route 1.)

**Ruled out (2026-10-12): replaying a frame's draw calls from extra viewpoints** at the graphics API
(`NEXT_SIX.md` §2 proposed it for Witcher 3). The rig captures live gameplay, colour and depth from cameras the
game itself renders. A game none of the ways above can get into is not supported.

## The focus games

| Game | Where | Engine | Graphics API | Anti-cheat | How the cube gets in | Where it stands |
|---|---|---|---|---|---|---|
| **The Ascent** | C, Steam | Unreal 4.26 *(measured)* | DX11, DX12 | none | UE4SS, extra cameras | **Working.** Six 1024 px cameras: 30 volumes a second, the game at 168-179 fps on DX12 |
| **Mortal Kombat 1** | C, Steam | Unreal 4, NetherRealm's fork | DX12 | none known (Denuvo DRM), online-heavy | UE4SS, extra cameras, if UE4SS attaches | **Working** (2026-10-12): 5 cameras of 1024 px, 30 volumes a second, the game at its 60 fps cap. Offline modes only |
| **Risk of Rain 2** | C (D:) and A, Steam | Unity 2021.3.45 Mono *(measured)* | DX11 *(measured: no DX12 files)* | none | BepInEx 5, extra cameras | **Working** (`rig/unity`, 2026-10-12). Five 512 px cameras: 30 volumes a second, 120,000-200,000 voxels, checked not mirrored |
| **The Elder Scrolls V: Skyrim SE** | A (E:), and C (Steam, `C:\Program Files (x86)\Steam`; no SKSE there) | Creation, 1.7.104 *(measured)* | DX11 | none | SKSE64 (+ Address Library); the renderer hooked (route 3), not built | **Not started on the current route.** Both earlier routes, in VoxelSteamTap, are ruled out: the outward sweep (2026-10-07: dark wedges, 4 of 13 frames on screen) by the no-sweeps rule, and the mesh layers that followed by the one-method rule. What carries over is its SKSE plugin's reading of the world camera (`Main::WorldRootCamera`), the player camera and the player's position |
| **Star Wars Jedi: Fallen Order** | C (D:), Epic (the EA app installs and starts it) | Unreal 4.21 *(measured)* | DX11 | none | UE4SS, extra cameras | **Working** (2026-10-15): five 512 px faces, 30 volumes a second, the game at 60 fps; player look, and the lightsaber blade solid where it is opaque (`docs/RESULTS.md` sections 14 and 16) |
| **Cyberpunk 2077** | C (D:) and A, Steam | REDengine 4, 2.31 *(measured)* | DX12 | none | Cyber Engine Tweaks + RED4ext + Codeware; the engine's own render-to-texture cameras on V (`rig/redengine4`) | **Working (2026-10-14)**: five 512 px faces in the player's frame, 30 complete cubes a second, the game at about 67 fps on an RTX 3060 Ti; the player's controls, frames and HUD untouched |
| **The Witcher 3** | C (D:) and A, Steam | REDengine 3, Remastered 5.0 *(measured: DX12 only, 5.0.0.1048522)* | DX12 | none | the add-on hooks the renderer (`addon/red3_native.hpp`) + a script mod (`rig/redengine3`) | **Working (2026-10-13)**: the faces rendered as extra views in the player's frame, 40 complete cubes a second with four faces at 1280 x 800 on an RTX 3060 Ti, steady, the player's controls and frames untouched. The inward sweep works too and is the fallback, but decays (world streaming) and takes the controls |
| **Need for Speed Heat** | C (D:), Steam (the EA app starts it) | Frostbite *(measured)* | DX11 *(measured: no DX12 option)* | none known (Denuvo DRM); Play Solo only | none: Frostbite has no camera to add and no extra view to reuse | **Not supported** (parked 2026-10-16): only a sweep of the game's own camera was possible, and sweeps are ruled out. What was measured is in `docs/RESULTS.md` section 17, for a later Frostbite game |
| **Star Wars Battlefront II (2017)** | C (D:), Epic (the EA app installs and starts it) | Frostbite 1.1.8.16175 *(measured)* | DX11 *(measured)* | none *(measured)*; offline only (campaign, Arcade) | no loader: the add-on adds the faces to the renderer's own list of views (`addon/frostbite_views.hpp`), three at most | **Working** (2026-10-16): in play, three 1024 px faces at 30 volumes a second, the game at 110-150 fps; the volume stands on the soldier's feet; player look by where the player stands (`docs/RESULTS.md` section 18) |
| **The Sims 4** | C (D:), Epic (the EA app installs and starts it) | Maxis's own, 1.128.90.1030 *(measured)* | DX11 *(measured)* | none *(measured)*; single player | no loader: the add-on hooks the game's render job and has its renderer draw each face from a camera of the add-on's (`addon/sims4_views.hpp`) | **Working** (2026-10-18): in a lot, five faces at the game's window size, 30 volumes a second, the game at about 80 fps (170 without); the volume follows the game's camera, its ground at the feet's height (`docs/RESULTS.md` section 19) |
| **Crysis** (2007) | C (D:), EA Play through the Xbox app (`D:\XboxGames\Crysis`) | CryEngine 2, 1.2.1 build 6156 *(measured)* | DX10, 64-bit *(measured)* | none in single player (PunkBuster: multiplayer servers only) | no loader: the add-on takes the 3D engine's RenderWorld and has the engine draw each face from a camera of the add-on's (`addon/cryengine2_views.hpp`) | **Working, with a flicker** (2026-10-20): five faces, 30 volumes a second, the game at 270-280 fps (374 without); the game itself needs c1-launcher to start on a current PC (`docs/RESULTS.md` section 20). On some capture frames a face's picture is shown in place of the player's view: the cause is found, the fix not yet made |
| **Gang Beasts** | C (D:), Xbox app (PC Game Pass, `D:\XboxGames\Gang Beasts`) | Unity 2021.3.33 IL2CPP, URP *(measured)* | DX11 *(measured)* | none in local play; stay out of online play | BepInEx 6 (IL2CPP) + the Unity rig's IL2CPP build (`rig/unity/il2cpp`), extra cameras | **Working in a local match** (2026-10-21): the rig follows the local beast; five 512 px faces at 30 volumes a second, the game at its 60 fps. URP's renderer features are turned off while the rig's cameras draw, or their depth stays empty (`docs/RESULTS.md` 21) |
| **Totally Accurate Battle Simulator** | C (D:), Xbox app (PC Game Pass, `D:\XboxGames\Totally Accurate Battle Simulator`) | Unity 2019.4.33 Mono, built-in renderer *(measured)* | DX11 *(measured)* | none *(measured)*; Photon for online battles: campaign and sandbox only | BepInEx 5 + the Unity rig, extra cameras | **Working** (2026-10-21): nobody is the player, so the volume follows the game camera's view (`followView`): the ground it looks at, scaled with how far away that is, turning with it. Five 512 px faces at 30 volumes a second, the game at 440-670 fps (`docs/RESULTS.md` 23) |
| **Gears 5** | C (D:), Xbox app (PC Game Pass, `D:\XboxGames\Gears 5`) | Unreal 4, The Coalition's fork *(measured: GearGame, 1.1.967.0)* | DX12 | **Easy Anti-Cheat on every launch** *(measured: the Xbox app starts `Gears5_EAC.exe`, EAC's launcher, which starts `Gears5.exe`; its installer adds the EAC service)* | none: UE4SS and ReShade would have to load into the protected process | **Not supported** (2026-10-21): anti-cheat over the campaign too. Starting `Gears5.exe` without its launcher would be getting round the anti-cheat. Nothing was installed |
| **Minecraft Dungeons** | C (D:), Xbox app (PC Game Pass, `D:\XboxGames\Minecraft Dungeons`) | Unreal 4.22.3 *(measured)* | DX11 *(measured)* | none *(measured)*; a protected exe whose import table is a stub *(measured)* | none: UE4SS faults on the stub import table | **Not supported** (2026-10-23): UE4SS stops the game as it starts (PolyHook's import hook needs name tables the exe does not have); changing the protected exe to suit it would be getting round its protection (`docs/RESULTS.md` 25) |
| **Minecraft Dungeons II** | C (D:), Xbox app (PC Game Pass, `D:\XboxGames\Minecraft Dungeons II`) | Unreal 5.6.1 with AngelScript *(measured)* | DX12 *(measured)* | none *(measured)*; anti-tamper protection *(measured: encrypted code, an anti-debugger check)* | Blueprint Loader (the player's download) + the rig as a Blueprint mod of ours (`tools/mcdungeons2-mod`), extra cameras; its settings from the app as a PNG | **Working** (2026-10-23): five 512 px faces, 30 volumes a second, the game at 330-420 fps; every Cameras tab setting reaches it within a second; Player look covers the hero and the mobs. UE4SS could not go in: the game ends itself once its code is hooked (`docs/RESULTS.md` 25) |
| **Minecraft: Java Edition** | C (D:), Minecraft Launcher (Xbox app; `%APPDATA%\.minecraft`) | Minecraft's own, 26.3 *(measured)*, Java 25 | OpenGL 3.3 *(measured; Vulkan in the 26.4 snapshots)* | none; single player | Fabric Loader (the player's download) + the rig as a Fabric mod of ours (`rig/minecraft`): the game's own level drawn once more per face, decoded by `voxon_capture_mc.exe` beside the game | **Working** (2026-10-24): six 512 px faces, 30 captures and 22-23 volumes a second, the game at 60 fps; 1024 px faces too. The faces agree to a centimetre (`docs/RESULTS.md` 27) |
| **Gears Tactics** | C (D:), Xbox app (PC Game Pass, `D:\XboxGames\Gears Tactics`) | Unreal 4.11, The Coalition's branch *(measured: UE4SS's scan)* | DX12 *(measured)* | none *(measured)*; copy protection that checks its own folder *(measured)* | none: UE4SS supports 4.12 and up, and the game kills itself when files are added | **Not supported** (2026-10-21): with any file of the rig's in its folder, even none loaded, the game ends itself about 20 s after it starts (fail-fast 0xC0000409, always at exe+0x5D4F600); untouched it runs. Getting round that would be getting round its protection (`docs/RESULTS.md` 22) |
| **GTA V** | **B, other Steam account** | RAGE, build 3889 Legacy *(measured)* | DX11 | BattlEye for GTA Online; story mode runs with `-nobattleye` | ScriptHookV + ScriptHookVDotNet; route 3 needed | A scripted camera swept through 4 poses at about 43 Hz left the monitor 22% of frames: **not working well**, and sweeps are now ruled out. Needs route 3, the renderer hooked, on Machine B |

### What each one needs next

- **The Ascent.** In the app: Games, then **Install rig**. That takes over the older hand install, and
  Remove would put it back.
- **Mortal Kombat 1.** Working (2026-10-12): **Install rig**, then the tutorial, Practice or Towers. The
  installer brings a newer UE4SS, one hook and a vtable layout; the volume follows the two fighters. Lighting at
  or near 0% (the surfaces' own colours) suits its harsh lighting. World X at 2-3 squashes its deep stage in.
- **Risk of Rain 2.** Working: in the app, Games, then **Install rig**. Next is the camera-size sweep, as
  for The Ascent. Its Frame Rate Limit is set to 30 on this PC, which holds the game at 30 fps.
- **Cyberpunk 2077.** Working (2026-10-14): in the app, Games, then **Install rig**, then start the game and load a
  save (the cameras go on V as she is loaded). The installer brings Cyber Engine Tweaks, RED4ext and Codeware unless
  the game has them already; a player's own are kept, with their mods.
- **The Sims 4.** Working (2026-10-18): in the app, Games, then **Install rig**, then load a household into its lot
  (the faces are drawn only in a lot). The volume follows the game's camera: the point it looks at on the ground
  stands where the feet are set, and the volume turns as the camera does. No player of its own yet: player look does
  not apply.
- **Crysis.** Working (2026-10-20), in testing: in the app, Games, then **Install rig** (for the EA app or Xbox app
  copy, **Browse** to the game's `Bin64`), then start the game with c1-launcher, which the player downloads: as
  installed the game hangs or crashes at startup on a current PC, with or without the rig. Still to check in play: the
  feet (from the player's entity), orbiting the simulator against the game, and the first-person weapon in the faces.
  To fix: the flicker - on some capture frames the player's view does not replace the last face's picture in the
  back buffer, so the face is presented (seen in a grabbed frame: the camera pitched about 31 degrees down, as the
  side faces are). Asked for: player look for Crysis, so Nomad and the allies, mostly dark, can be brightened and
  drawn denser.
- **Gang Beasts.** Working in a local match (2026-10-21): the rig follows the local beast (the first `Femur.Actor`
  that is local and not AI, its feet from the hips and feet bones), 30 volumes a second. Next: the beast's look (player
  look), a second local player, and Risk of Rain 2 run again, as the follow code and `Urp.cs` are shared.
- **Skyrim: route 3, from the start.** The question these games once shared - can the camera be placed on each
  face? - belonged to the sweep, which is ruled out. What is needed is the renderer hooked, as for The Witcher 3,
  The Sims 4 and Crysis: an SKSE plugin (with the Address Library, so it is not tied to one game build) that, after
  the game draws its world each frame, has the engine cull and draw it again from each face's camera into targets of
  the rig's own. Leads to follow: the world camera the earlier plugin read (`Main::WorldRootCamera`, a `NiCamera`),
  the engine's culling and render passes as CommonLibSSE names them, and the local map, which already draws the world
  from a second camera into a texture. Then a profile, an installer type for SKSE (the player downloads SKSE; the
  Address Library likewise), and a self-test case.
- **GTA V: route 3 too, on Machine B.** Its sweep is ruled out. The way in to look for is RAGE's own drawing of
  extra views (its mirrors, reflections or secondary cameras). The tap (`VoxelSteamTap/a4-scripthook`) is injected
  after the game starts, because loading it as an `.asi` hangs startup; packaging it means an installer type for
  ScriptHookV, written and tested on that PC.
- **Every installed game: Install rig again** after the add-on changes. On this PC (2026-10-21) each installed rig
  (Jedi: Fallen Order, Battlefront II, The Sims 4, Cyberpunk 2077, Mortal Kombat 1, Risk of Rain 2, The Witcher 3,
  Crysis) holds an add-on older than the one in `redist/`; Install rig again, with the game closed, replaces it.

## Keeping it modular

Each engine is one **rig module**. A module brings its own folder, its own installer type in the app, and
its entries in `games.json`. The app, the host and the pipe stay the same for every game. The ReShade
add-on gets a new mode only when a module needs one, and that mode gets its own self-test case, so changing
one engine can never quietly break another.

| Module | Engines | Loader the installer puts in | How the cameras get in | Status |
|---|---|---|---|---|
| `rig/unreal` | Unreal 4, Unreal 5 | UE4SS 3.0.1 | `SceneCapture2D`, same frame | **Built** |
| `rig/unity` | Unity, DX11: Mono and IL2CPP | BepInEx 5 (Mono); BepInEx 6 (IL2CPP), with the rig's IL2CPP build (`rig/unity/il2cpp`) | `Camera`, same frame | **Built** (Risk of Rain 2); IL2CPP **working** (Gang Beasts) |
| `rig/creation` | Skyrim SE | SKSE64 + Address Library | the renderer hooked (route 3) | Not started: its sweep is ruled out |
| `rig/redengine4` | Cyberpunk 2077 | Cyber Engine Tweaks + RED4ext + Codeware (a player's own kept); the rig is a CET mod | the engine's render-to-texture cameras on V, same frame | **Working** |
| `rig/redengine3` | The Witcher 3 | a script mod (`mods\modVoxonRig`), the player script patched from the game's own; the add-on hooks the renderer | extra views in the player's frame (the sweep as fallback) | **Working** |
| `rig/rage` | GTA V | ScriptHookV (+ .NET) | the renderer hooked (route 3) | Not started: its sweep (VoxelSteamTap) is ruled out; needs Machine B |
| none: the add-on alone | Frostbite (Battlefront II), The Sims 4, CryEngine 2 (Crysis) | none | the engine's own views (Frostbite); the game's renderer from the add-on's cameras (The Sims 4, Crysis) | **Working** |

**Easy to set up** means the player only ever presses **Install rig** and **Remove rig**. Two things keep
that true as modules are added:
- Each installer type records what it changed, so Remove restores the game exactly. The self-test checks
  this for every type.
- The installer should refuse a game it finds `EasyAntiCheat` or `BattlEye` in. That is a planned check,
  not built yet.

## Legend for the library tables

- **Yes**: the cube rig fits as it stands (Unreal), or with the Unity module now being built.
- **Later**: the rig fits, but through a loader still in testing: Unity IL2CPP goes in through BepInEx 6 and the
  rig's IL2CPP build (2026-10-20), so far tried only on Gang Beasts, where it works.
- **Needs its own rig**: a focus-game engine; see above.
- **No rig for this engine yet**: no module for it, and not a focus game.
- **No**: anti-cheat or online-only, a 2D game (no depth worth capturing), or DX9, OpenGL or Vulkan-only
  (the add-on decodes DX11 and DX12).

*Check* in the anti-cheat column means unknown: look for `EasyAntiCheat` or `BattlEye` folders in the
install. *DX11/12 likely* means it is an Unreal or Unity game, which nearly always run DX11 or DX12.

## All three libraries, by capture method

[`CAPTURE_PLAN.md`](CAPTURE_PLAN.md) puts every game in the Steam, Epic and PC Game Pass libraries under the capture
method that would get the cube into it - the Unreal rig, the Unity rig, an engine module already built, or a new
engine module (one per engine, ranked by how many games it would serve) - and lists every game ruled out, with why.

## PC Game Pass (the Xbox app)

Every game in the PC Game Pass catalogue, 515 on 2026-10-22, grouped by engine (Unreal, Unity, the rest), is in
[`XBOX_GAMES.md`](XBOX_GAMES.md): 123 on Unreal (65 on Unreal 5, 50 on Unreal 4), 132 on Unity.

## Steam library

From your Steam library (Machine A's screenshots, 2026-10-10, and what is installed on these PCs). GTA V is
under Machine B's account.

| Game | Engine | Graphics API | Anti-cheat | Camera rig |
|---|---|---|---|---|
| The Ascent | Unreal 4.26 | DX11, DX12 | none | **Yes**: working (Unreal rig) |
| Mortal Kombat 1 | Unreal 4, NetherRealm fork | DX12 | none known (Denuvo DRM) | **Yes**: Unreal rig, working (newer UE4SS) |
| Black Myth: Wukong | Unreal 5 (measured) | DX12 | none | **Yes**: profile ready (needs the engine override) |
| The Elder Scrolls IV: Oblivion Remastered | Unreal 5 over Gamebryo (measured) | DX12 | none | **Yes**: profile ready |
| ACE COMBAT 7: SKIES UNKNOWN | Unreal 4 | DX11 | none | **Yes**: Unreal rig |
| Stray | Unreal 4 | DX11, DX12 | none | **Yes**: Unreal rig |
| Hogwarts Legacy | Unreal 4 | DX12 | none (Denuvo DRM) | **Yes**: Unreal rig |
| Borderlands 3 | Unreal 4 | DX11, DX12 | check | **Yes**: Unreal rig |
| Risk of Rain 2 | Unity 2021.3 Mono (measured) | DX11 | none | **Yes**: Unity rig (working) |
| Cult of the Lamb | Unity Mono | DX11 | none | Yes: Unity rig, but its characters are flat sprites |
| Crab Game | Unity IL2CPP | DX11 | none | Later: Unity rig on BepInEx 6 |
| 3D Aim Trainer | Unity? | ? | none | Not planned |
| The Elder Scrolls V: Skyrim Special Edition | Creation (measured) | DX11 | none | Needs its own rig (SKSE plugin): see focus games |
| Cyberpunk 2077 | REDengine 4 (measured) | DX12 | none | **Yes**: its own rig (`rig/redengine4`, the engine's render-to-texture cameras): see focus games |
| The Witcher 3: Wild Hunt (Remastered) | REDengine 3 | DX12 *(measured)* | none | **Yes**: its own rig (`rig/redengine3`, the renderer hooked): see focus games |
| F1 2020 | EGO | DX12 (DX11 crashes here) | none | **Showroom working** (`addon/ego_views.hpp`, the game's own cubemap); in a race the player's own car is left out |
| Need for Speed Heat | Frostbite | DX11 | none known | No: parked, no extra cameras (`docs/RESULTS.md` 17) |
| DARK SOULS III | FromSoftware | DX11 | server-side bans online: offline only | No rig for this engine yet |
| HITMAN 2 | Glacier 2 | DX11, DX12 | none | No rig for this engine yet |
| Mass Effect Legendary Edition | Unreal 3, modified | DX11 | none | No: Unreal 3 (UE4SS cannot attach) |
| Mortal Kombat 11 | Unreal 3, NetherRealm fork | DX11 | none | No: Unreal 3 |
| Borderlands 2 | Unreal 3 | DX9 | none | No: Unreal 3, DX9 |
| Men of War: Assault Squad 2 | GEM 2 | DX9 | none | No: DX9 |
| Torchlight II | OGRE | DX9 | none | No: DX9 |
| DOOM Eternal | id Tech 7 | Vulkan only (measured) | none (Denuvo DRM) | No: Vulkan, and no way into the engine |
| Wolfenstein: The New Order | id Tech 5 | OpenGL | none | No: OpenGL |
| Planetary Annihilation: TITANS | Uber | OpenGL (measured) | none | No: OpenGL |
| Shadow Warrior Classic (1997) | Build | software / OpenGL | none | Not this project: already a Voxel64 cartridge |
| Garry's Mod | Source | DX9 | VAC (online) | No: DX9 |
| Battle vs Chess | unknown | ? | none | Unknown: check on install |
| Box Maze 2: Agent Cubert | unknown | ? | none | Unknown: check on install |
| Dark Side of War | unknown | ? | none | Unknown: check on install |
| DEPLOYMENT | unknown | ? | none | Unknown: check on install |
| Dungeon of Elements | unknown | ? | none | Unknown: check on install |
| Among Us | Unity IL2CPP (measured) | DX11 | none | No: 2D |
| Blasphemous 2 | Unity IL2CPP (measured) | DX11 | none | No: 2D |
| Children of Morta | Unity | ? | none | No: 2D |
| Hades | Supergiant's own | DX11, Vulkan | none | No: 2D |
| Helltaker | Unity | ? | none | No: 2D |
| Hollow Knight: Silksong | Unity Mono (measured) | ? | none | No: 2D |
| Undertale | GameMaker (measured) | ? | none | No: 2D |
| River City Melee Mach!! | ? | ? | none | No: 2D (believed) |
| Stardew Valley | MonoGame | ? | none | No: 2D |
| Terraria | XNA / FNA | ? | none | No: 2D |
| ELDEN RING | FromSoftware | DX12 (measured) | EasyAntiCheat, kernel | No: anti-cheat |
| THE FINALS | Unreal 5 | DX12 | kernel anti-cheat | No: anti-cheat |
| Sea of Thieves | Unreal 4 | DX11 | kernel anti-cheat | No: anti-cheat |
| Splitgate | Unreal 4 | DX11 | kernel anti-cheat | No: anti-cheat |
| Apex Legends | Source (modified) | DX11, DX12 | kernel anti-cheat | No: anti-cheat |
| Battlefield 1 / 4 / V / 2042 / 6 | Frostbite | DX11, DX12 | kernel anti-cheat (EA Javelin; Battlefield 6 also needs Secure Boot) | No: anti-cheat |
| Call of Duty | IW | DX12 | kernel anti-cheat | No: anti-cheat |
| Counter-Strike 2 | Source 2 | DX11, Vulkan | VAC | No: anti-cheat |
| Fall Guys | ? | ? | kernel anti-cheat | No: anti-cheat |
| FOR HONOR | AnvilNext | DX11 | kernel anti-cheat | No: anti-cheat |
| Rainbow Six Siege | AnvilNext 2.0 | DX11, Vulkan | kernel anti-cheat | No: anti-cheat |
| War Thunder | Dagor | DX11, DX12, Vulkan | EasyAntiCheat | No: anti-cheat |
| EVE Online | Trinity | DX11, DX12 | online only; policy forbids client changes | No: online only |
| Path of Exile / Path of Exile 2 | own | DX11, DX12, Vulkan | online only; policy forbids client changes | No: online only |
| World of Tanks | own | DX11 | online only, account risk | No: online only |
| MultiVersus | Unreal 5 | DX12 | servers shut down | No: nothing to run |
| Grand Theft Auto V (Machine B's account) | RAGE (measured) | DX11 (Legacy) | BattlEye for GTA Online; story mode without it | Needs its own rig (ScriptHookV): see focus games |
| Gang Beasts | Unity 2021.3.33 IL2CPP (measured; the Xbox app copy) | DX11 (measured) | none in local play | **Working** in a local match: the Unity rig's IL2CPP build (see focus games) |
| DAVIGO: Knight's Pass | Unity 2020.3.48 IL2CPP (measured), OpenXR | DX11 likely | none (measured) | Later: the IL2CPP rig. The free PC (knight) side of DAVIGO; the VR giant needs DAVIGO itself and a headset |
| 30XX | own | ? | none | No: 2D (save folder only on C) |
| River City Ransom: Underground | own | ? | none | No: 2D (save folder only on C) |
| TowerFall Ascension | MonoGame | ? | none | No: 2D (save folder only on C) |

## Epic Games library

All 239 games in your Epic library. The engine is read from each game's file list (VoxelSteamTap `data/epic-engines.csv`), except where it says *(known)*. Sorted by what the rig can do.

| Game | Engine | Graphics API | Anti-cheat | Camera rig |
|---|---|---|---|---|
| ABZU | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| ARK: Survival Evolved | Unreal 4/5? | DX11/12 likely | check | **Yes**: Unreal rig |
| Blair Witch | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Borderlands 3 | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Breathedge | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Bus Simulator 21 Next Stop | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Chess Ultra | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| City of Brass | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Core | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| DAEMON X MACHINA | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Dead Island 2 | Unreal 4/5? | DX11/12 likely | check | **Yes**: Unreal rig |
| Deliver Us Mars | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Eximius: Seize the Frontline | Unreal 5 | DX11/12 likely | check | **Yes**: Unreal rig |
| F.I.S.T.: Forged In Shadow Torch | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| F1® Manager 2024 | Unreal 5 | DX11/12 likely | check | **Yes**: Unreal rig |
| Filament | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| First Class Trouble | Unreal 4/5? | DX11/12 likely | check | **Yes**: Unreal rig |
| Ghostrunner 2 | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Ghostrunner Demo | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Ghostwire Tokyo | Unreal 4/5? | DX11/12 likely | check | **Yes**: Unreal rig |
| Hello Mod Kit | Unreal 4/5? | DX11/12 likely | check | **Yes**: Unreal rig |
| Hogwarts Legacy | Unreal 4/5? | DX11/12 likely | check | **Yes**: Unreal rig |
| Just Die Already | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Maneater | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Mortal Shell | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Mortal Shell Tech Beta | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Mothergunship | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Mutant Year Zero Road to Eden | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Nobody Wants to Die | Unreal 5 | DX11/12 likely | check | **Yes**: Unreal rig |
| Obduction | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Pinball FX | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Poker Club | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Redout: Enhanced Edition | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Relicta | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Remnant: From the Ashes | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Severed Steel | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Sifu | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Solitairica | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Soulstice | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Star Wars: Jedi Fallen Order | Unreal 4.21 (measured) | DX11 | none (single player) | **Working (2026-10-15)**: Unreal rig, five 512 px faces at 30 volumes a second, the game at 60 fps. Needed the rig to read enum values by name (4.21's render-target format and capture source differ from 4.26's) and the older CreateRenderTarget2D. The Epic copy is installed by the EA app, found through its registry value |
| The Lord of The Rings: Return to Moria™ | Unreal 4/5? | DX11/12 likely | check | **Yes**: Unreal rig |
| The Outer Worlds | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| The Outer Worlds: Spacer's Choice Edition | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Tiny Tina's Wonderlands | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Train Sim World 2 | Unreal 4 | DX11/12 likely | check | **Yes**: Unreal rig |
| Unreal Tournament | Unreal 4 (known) | DX11/12 likely | check | **Yes**: Unreal rig |
| Wildgate | Unreal 5 | DX11/12 likely | check | **Yes**: Unreal rig |
| Among the Sleep - Enhanced Edition | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Automachef | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Backpack Hero | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Bad North Jotunn Edition | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Beyond Blue | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Black Widow: Recharged | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Citizen Sleeper | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Cris Tales | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Crying Suns | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Darkwood | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| DEMON'S TILT | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Doki Doki Literature Club Plus! | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Doors - Paradox | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Empyrion - Galactic Survival | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Enter the Gungeon | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Epistory - Typing Chronicles | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Escape Academy | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| For The King | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| GNOG | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| God's Trigger | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Golden Light | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Halcyon 6 | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Hell is other demons | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Homeworld: Deserts of Kharak | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Horizon Chase Turbo | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| HUMANKIND™ | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| INSIDE | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Iratus | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Jotun Valhalla Edition | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Kingdom New Lands | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Last Day of June | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Lost Castle | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Mages of Mystralia | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Moonlighter | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Never Alone (Kisima Ingitchuna) | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Night in the Woods | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Offworld Trading Company | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Out of Line | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Overcooked | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Overcooked! 2 | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Rising Hell | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| ROBOBEAT | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Runbow | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Shadow Tactics: Blades of the Shogun | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Sheltered | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Slain: Back From Hell | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Speed Brawl | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Stranded Deep | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Sunless Sea | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Tales of the Neon Sea | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Terraforming Mars | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Tharsis | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| The Bridge | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| The Drone Racing League Simulator | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| The Escapists 2 | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| The First Tree | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| The Forest Quartet | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| The Messenger | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| The Silent Age | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Train Valley 2 | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Tunche | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Undying | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Void Bastards | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Warhammer 40,000: Mechanicus - Standard Edition | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| while True: learn() | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Wildcat Gun Machine | Unity Mono | DX11/12 likely | check | **Yes**: Unity rig (next) |
| Gloomhaven | Unity | DX11/12 likely | check | Yes or later (Mono or IL2CPP?) |
| GRIME | Unity | DX11/12 likely | check | Yes or later (Mono or IL2CPP?) |
| Monument Valley | Unity (known) | DX11/12 likely | check | Yes or later (Mono or IL2CPP?) |
| SKALD Against the Black Priory | Unity | DX11/12 likely | check | Yes or later (Mono or IL2CPP?) |
| Against All Odds | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Behind the Frame: The Finest Scenery | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Bloons TD 6 | Unity IL2CPP (known) | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Centipede Recharged | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Eternal Threads | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Gods Will Fall | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Godzilla Voxel Wars | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Lawn Mowing Simulator | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| LEGO® Builder's Journey | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Monument Valley 2 | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Moving Out | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Project Winter | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Ring of Pain | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Riverbond | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Sable | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| The Battle of Polytopia | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| The Dungeon Of Naheulbeuk: The Amulet Of Chaos | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| TOEM | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| Windbound | Unity IL2CPP | DX11/12 likely | check | Later: Unity rig on BepInEx 6 |
| A Plague Tale: Innocence | other | ? | check | No rig for this engine yet |
| Alien: Isolation | other | ? | check | No rig for this engine yet |
| Amnesia: A Machine for Pigs | other | ? | check | No rig for this engine yet |
| Amnesia: Rebirth | other | ? | check | No rig for this engine yet |
| Ancient Enemy | other | ? | check | No rig for this engine yet |
| Antstream Arcade | other | ? | check | No rig for this engine yet |
| BioShock 2 Remastered | other | ? | check | No rig for this engine yet |
| BioShock Infinite: Complete Edition | other | ? | check | No rig for this engine yet |
| BioShock Remastered | other | ? | check | No rig for this engine yet |
| Borderlands 2 | other | ? | check | No rig for this engine yet |
| Borderlands: The Pre-Sequel | other | ? | check | No rig for this engine yet |
| Brothers - A Tale of Two Sons | other | ? | check | No rig for this engine yet |
| Cassette Beasts | other | ? | check | No rig for this engine yet |
| Castlevania Anniversary Collection | other | ? | check | No rig for this engine yet |
| Cave Story+ | other | ? | check | No rig for this engine yet |
| Control | other | ? | check | No rig for this engine yet |
| DEATHLOOP | other | ? | check | No rig for this engine yet |
| Destiny 2 | other | ? | check | No rig for this engine yet |
| Deus Ex - Mankind Divided | other | ? | check | No rig for this engine yet |
| Dishonored - Definitive Edition | other | ? | check | No rig for this engine yet |
| Dishonored®: Death of the Outsider™ | other | ? | check | No rig for this engine yet |
| Dragon Age: Inquisition – Game of the Year Edition | other | ? | check | No rig for this engine yet |
| Dying Light | other | ? | check | No rig for this engine yet |
| Evoland Legendary Edition | other | ? | check | No rig for this engine yet |
| Fallout 3: Game of the Year Edition | other | ? | check | No rig for this engine yet |
| Fallout: New Vegas | other | ? | check | No rig for this engine yet |
| Faster Than Light | other | ? | check | No rig for this engine yet |
| Frostpunk | other | ? | check | No rig for this engine yet |
| Galactic Civilizations III | other | ? | check | No rig for this engine yet |
| Galactic Civilizations III (Test branch) | other | ? | check | No rig for this engine yet |
| Geneforge 1: Mutagen | other | ? | check | No rig for this engine yet |
| Guacamelee! 2 | other | ? | check | No rig for this engine yet |
| Guacamelee! Super Turbo Championship Edition | other | ? | check | No rig for this engine yet |
| HITMAN | other | ? | check | No rig for this engine yet |
| HITMAN 3 | other | ? | check | No rig for this engine yet |
| Hyper Light Drifter | other | ? | check | No rig for this engine yet |
| Into The Breach | other | ? | check | No rig for this engine yet |
| Killing Floor 2 | other | ? | check | No rig for this engine yet |
| KillingFloor2Beta | other | ? | check | No rig for this engine yet |
| LEGO® Star Wars™: The Skywalker Saga | other | ? | check | No rig for this engine yet |
| Limbo | other | ? | check | No rig for this engine yet |
| Metro Last Light Redux | other | ? | check | No rig for this engine yet |
| NBA 2K21 | other | ? | check | No rig for this engine yet |
| Nioh: The Complete Edition | other | ? | check | No rig for this engine yet |
| Paladins | other | ? | check | No rig for this engine yet |
| Paradigm | other | ? | check | No rig for this engine yet |
| Pathway | other | ? | check | No rig for this engine yet |
| PAYDAY 2 | other | ? | check | No rig for this engine yet |
| PREY | other | ? | check | No rig for this engine yet |
| Rebel Galaxy | other | ? | check | No rig for this engine yet |
| Rise of the Tomb Raider: 20 Year Celebration | other | ? | check | No rig for this engine yet |
| Rising Storm 2: Vietnam | other | ? | check | No rig for this engine yet |
| RollerCoaster Tycoon® 3: Complete Edition | other | ? | check | No rig for this engine yet |
| RPG in a Box | other | ? | check | No rig for this engine yet |
| Saints Row IV Re-Elected | other | ? | check | No rig for this engine yet |
| Saints Row The Third Remastered | other | ? | check | No rig for this engine yet |
| Second Extinction™ | other | ? | check | No rig for this engine yet |
| Shadow of the Tomb Raider: Definitive Edition | other | ? | check | No rig for this engine yet |
| shapez | other | ? | check | No rig for this engine yet |
| Sniper Ghost Warrior Contracts | other | ? | check | No rig for this engine yet |
| Sonic Mania | other | ? | check | No rig for this engine yet |
| Star Wars Squadrons | Frostbite (known) | DX11 | Easy Anti-Cheat *(measured: in the install, its launcher starts the game)* | No: anti-cheat |
| STAR WARS™ Battlefront™ II: Celebration Edition | Frostbite *(measured)* | DX11 *(measured)* | none *(measured: the EA app starts the exe directly)* | **In progress**: the engine's own views (see focus games) |
| Super Meat Boy Forever | other | ? | check | No rig for this engine yet |
| The Elder Scrolls Online | other | ? | check | No rig for this engine yet |
| The End is Nigh | other | ? | check | No rig for this engine yet |
| The Escapists | other | ? | check | No rig for this engine yet |
| The Evil Within | other | ? | check | No rig for this engine yet |
| The Jackbox Party Pack 4 | other | ? | check | No rig for this engine yet |
| The Sims™ 4 | Maxis's own *(measured)* | DX11 *(measured)* | none *(measured)* | **Yes**: the add-on draws the faces through the game's renderer (see focus games) |
| theHunter: Call of the Wild™ | other | ? | check | No rig for this engine yet |
| Tiny Tina's Assault on Dragon Keep: A Wonderlands One-shot Adventure | other | ? | check | No rig for this engine yet |
| Tomb Raider GAME OF THE YEAR EDITION | other | ? | check | No rig for this engine yet |
| Tomb Raider I-III Remastered Starring Lara Croft | other | ? | check | No rig for this engine yet |
| Torchlight II | other | ? | check | No rig for this engine yet |
| Total War: THREE KINGDOMS | other | ? | check | No rig for this engine yet |
| Total War: WARHAMMER | other | ? | check | No rig for this engine yet |
| Total War: WARHAMMER III | other | ? | check | No rig for this engine yet |
| Trackmania Starter Access | other | ? | check | No rig for this engine yet |
| Valley of the Ancient | other | ? | check | No rig for this engine yet |
| Valley of the Ancient | other | ? | check | No rig for this engine yet |
| VALORANT | other | ? | check | No rig for this engine yet |
| Warface: Clutch | other | ? | check | No rig for this engine yet |
| Warframe | other | ? | check | No rig for this engine yet |
| Watch Dogs 2 | other | ? | check | No rig for this engine yet |
| Wolfenstein: The New Order | other | ? | check | No rig for this engine yet |
| Wonder Boy The Dragons Trap | other | ? | check | No rig for this engine yet |
| World of Warships | other | ? | check | No rig for this engine yet |
| World War Z | other | ? | check | No rig for this engine yet |
| Yoku's Island Express | other | ? | check | No rig for this engine yet |
| Death's Gambit - Afterlife | GameMaker | ? | check | No: 2D |
| Loop Hero | GameMaker | ? | check | No: 2D |
| Minit | GameMaker | ? | check | No: 2D |
| The Textorcist | GameMaker | ? | check | No: 2D |
| Chivalry 2 | Unreal 4 | DX11/12 likely | EAC | No: anti-cheat |
| Chivalry 2 - Public Testing | Unreal 4 | DX11/12 likely | EAC | No: anti-cheat |
| Dead by Daylight | Unreal 5 | DX11/12 likely | EAC | No: anti-cheat |
| Fall Guys | Unity IL2CPP | DX11/12 likely | EAC (kernel) | No: anti-cheat |
| Fortnite | Unreal 5 | DX11/12 likely | EAC + BattlEye (kernel) | No: anti-cheat |
| Hood Outlaws and Legends | Unreal 4 | DX11/12 likely | EAC | No: anti-cheat |
| MORDHAU | Unreal 4/5? | DX11/12 likely | EAC | No: anti-cheat |
| Rocket League® | other | ? | EAC | No: anti-cheat |
| Spellbreak | Unreal 4 | DX11/12 likely | EAC (servers closed) | No: anti-cheat |

## Where this comes from

- The focus games: VoxelSteamTap `NEXT_FOUR.md`, `NEXT_SIX.md`, `cyberpunk/PLAN.md`, `skyrim/PLAN.md` (branch machine-a), `a4-scripthook/`, `s2-unity/`, `the-ascent/PLAN.md` and `s1-unreal/RESULTS.md`.
- Steam: VoxelSteamTap `LIBRARY_PLAN.md` and `STEAM_LIBRARY_CANDIDATES.md`, plus what is on these PCs.
- Epic: VoxelSteamTap `data/epic-engines.csv` and `data/epic-library.csv`.
- Anti-cheat for Epic titles: the three the earlier projects recorded (Fortnite, Dead by Daylight, Rocket League) and a few documented ones (Chivalry 2, MORDHAU, Hood, Spellbreak, Fall Guys). Every *check* still needs looking at.
