# To-do: GTA V on the cube camera rig (Machine B)

This repository is **Voxon Capture Rig**: one capture method (a cube of depth cameras round the player), a GPU decode
(a ReShade add-on), and an isolated app that installs the rig and draws the volume on a Voxon display or its
simulator. It was moved to Machine B, the PC with GTA V (Legacy, build 3889, on a different Steam account from
Machine A), to add GTA V. Read `README.md`, then `docs/ARCHITECTURE.md`, before changing anything.

## The rules - always, on every machine

- **One capture method: the cube depth-camera rig.** No mesh layers, no draw-call capture, no other route. A game the
  cube cannot get into is not supported.
- **No arbitrary limits.** No voxel budgets or caps the data does not require. The host takes 1.8 million voxels a
  volume (`docs/RESULTS.md` section 10).
- **Simple and readable.** Engine-specific parts live in `rig/<engine>/`, with one installer type each. The app, the
  host and the pipe stay the same for every game. Temporary diagnostics come out before a commit.
- **Players install it themselves, without an AI**: the app's Install rig and Remove rig buttons, nothing to edit by
  hand. Remove puts the game's folder back exactly as it was; `--selftest` checks that.
- **Offline only.** The rig is a mod inside the game. GTA V: story mode only, never GTA Online (BattlEye).
- **Never commit** game files, ScriptHookV (its author does not allow redistribution) or ScriptHookVDotNet (the player
  downloads both), or the Voxon runtime (`LedHost.dll`, `LedWin.dll`, `FTD3XXWU.dll`, `ledhost.ini`).
- **Never commit third-party files** either: only our own work is in git (`.gitignore`; what is fetched after cloning,
  and from where: `docs/DEVELOPER_GUIDE.md`).
- **Commit and push only when the user asks.** Push as the jordan608 account. The repository was started afresh on
  2026-10-21 with our own work only; its remote is not set yet (the old one, `github.com/jordan608/CubeRig`, holds the
  earlier history with the third-party files).

The older GTA V work is in **VoxelSteamTap** (`github.com/jordan608/VoxelSteamTap`, `a4-scripthook/`). Its README
and PLAN carry real measurements, but some of its rules are **superseded here**: "do not reconstruct geometry from a
depth buffer" and "do not drive the gameplay camera" were that project's rules. The cube rig is a depth-buffer
reconstruction, and for GTA V it has to move the game's own camera.

## Why GTA V is different

Unreal and Unity games get extra cameras that the engine renders in the same frame (`rig/unreal`, `rig/unity`). GTA
V's engine (RAGE) has no way to add a camera, so the cube has to be **swept**: the game's own camera is moved to one
face of the cube per game frame, and each frame's colour and depth become that face. The player only sees the
frames left over. The earlier sweep turned the camera *outward* through 4 poses at about 43 Hz and left 22% of
frames on the monitor, and the user judged it "not working well". An *inward* sweep, with each camera standing on
a face of the volume, looking in, its near plane on the volume's face, as the Unreal rig does, has not been tried.

## The to-do

**Superseded in part (2026-10-16): no sweeps.** The user ruled out sweeping the game's own camera: only cameras the
engine renders in the same frame as the player's view. Gates 1 and 2 below, and the sweep in step 3, belong to the
sweep and are kept as the record of the plan. GTA V now needs route 3 (`docs/GAMES.md`): the add-on or a ScriptHookV
plugin hooks RAGE's own drawing and has it draw the world again from each face's camera, as The Witcher 3, Battlefront
II, The Sims 4 and Crysis do. Step 0 (setting up Machine B) and the installer, profile and docs work in step 3 stand.

### 0. Set up Machine B
- [ ] Clone it from its remote (`git clone <remote> VoxonCaptureRig`), then put the third-party files in place
      (`docs/DEVELOPER_GUIDE.md`, *What is in git, and what is not*): they are not in git.
- [ ] Install the .NET 9 SDK (the app), Visual Studio 2022 with "Desktop development with C++" and the Windows SDK
      (the add-on and its shaders), and the Voxon VLED runtime (the simulator; never bundled).
- [ ] Build and check: `dotnet build host\VoxonCaptureRig -c Release`, then `VoxonCaptureRig.exe --selftest`
      (installer and pipe) and `addon\build.ps1` (GPU decode self-test on WARP). Both must pass before any GTA work.
- [ ] GTA V as it was left: ScriptHookV `3889.0.1158.13` matches build 3889. Install the **whole** ScriptHookV
      `bin/` folder: with `dinput8.dll` alone and no `xinput1_4.dll` the game stops at "INIT: Registering script".
      `args.txt` holds `-nobattleye -noBE`. A long first load is a loading screen, not a hang
      (VoxelSteamTap `a4-scripthook/PLAN.md`, "Resolved, 2026-09-23").

### 1. Gate: can the camera stand on each face, looking in?
The whole port depends on this. Stop and report if it fails.
- [ ] A scripted camera (`CREATE_CAM_WITH_PARAMS`, `SET_CAM_COORD`, `SET_CAM_ROT`, `SET_CAM_FOV`,
      `RENDER_SCRIPT_CAMS`) placed on a face of the cube, looking at the player. VoxelSteamTap's M1 already showed
      that culling and streaming follow a scripted camera.
- [ ] **The near plane on the volume's face** (`SET_CAM_NEAR_CLIP`: verify it exists and is honoured). Without it,
      walls between the camera and the volume block the face, as they would for the Unreal rig without
      `CustomNearClippingPlane`.
- [ ] Capture one face's colour and depth and check the geometry lands where it is (a test block at a known place,
      as `docs/RESULTS.md` section 11 did for Unity).

### 2. Gate: what the sweep costs
- [ ] Sweep N faces plus the player's own view, one per frame. Measure the frames the monitor keeps, and the
      volumes a second, for N = 1 to 5. The game ran at about 140 fps at 1280x720 before; RAGE's physics misbehaves
      above about 180 fps.
- [ ] Report the numbers to the user before building further: this is where the earlier sweep was judged not good
      enough.

### 3. Build it into this project
- [ ] **`rig/rage/`**: the sweep, as a ScriptHookV script. Same settings keys and maths as `rig/unreal/rig.lua` and
      `rig/unity/Geometry.cs` (the add-on decodes with the same formulas). It reads `voxon_capture_ue.ini` and the
      app's control file, as the other rigs do.
- [ ] **An add-on mode** (`engine=rage`). Each frame's colour and depth come from GTA V's own targets, not the rig's,
      so the add-on must know **which face each frame is**. That is the one thing that has to pass between the rig and
      the add-on at run time; today nothing does. Decide how (shared memory written by the rig per frame is the
      obvious one), and add GPU self-test cases for it, as Unity has (`addon/selftest_gpu.cpp`).
- [ ] **The player's monitor**: only the player's own frames should be presented. Work out how (the earlier tap
      swallowed capture frames in `Present`).
- [ ] **An installer type**: ScriptHookV is not bundled, so the app checks for it and tells the player where to get
      it; the rig, ReShade and the add-on go in as for the other engines, and Remove takes them out exactly. Add
      `--selftest` cases.
- [ ] `games.json` entry (`engine: "rage"`, Steam app 271590, `exe: "GTA5.exe"`), `rig/profiles/gta5.ini`.
- [ ] Docs: `docs/GAMES.md` (GTA V row and module table), `docs/RESULTS.md` (a section with the measurements),
      `docs/ARCHITECTURE.md`, `README.md`.

## Where things stand (2026-10-21, Machine C)

**Repo state (2026-10-21).** A fresh repository, branch `main`, started with one commit holding everything to date -
our own work only, no third-party files. No remote yet. The old history (branches `master` and `redengine-rigs`, up
to `cc9919a`, Battlefront II, never pushed) is kept outside the tree, at `..\VoxonCaptureRig-old-history.git`. From
the tree as it stands (after the Gang Beasts URP fix), the add-on, both Unity plugins and the app build, and every
self-test passes (the GPU decode on WARP, D3D11 and D3D12; the app's installer, pipe and Launch game, 19 sections). Every rig installed on this PC holds
an add-on older than `redist/`'s: Install rig again, with the game closed, brings it up to date.

**Open, in order of how near they are:**
- Risk of Rain 2 again (Install rig, a run): the Unity rig's follow code, `Members` and `Urp.cs` changed for Gang Beasts.
  Gang Beasts itself works in a local match; left: player look on the beast, a second local player.
- Crysis: the flicker (on some capture frames a face's picture is presented in place of the player's view); player
  look for Nomad and the allies (dark); the feet, orbiting the simulator, the first-person weapon, checked in play.
- Skyrim SE: not started on the current route. Its sweep and its mesh layers (VoxelSteamTap) are both ruled out; it
  needs route 3, the renderer hooked, through SKSE (`docs/GAMES.md`, *What each one needs next*). Installed on this
  PC (1.7.104) without SKSE.
- GTA V: route 3 on Machine B (above).
- DAVIGO (VR vs PC): the free Knight's Pass is installed (`D:\SteamLibrary\steamapps\common\DavigoKnightPass`; Unity
  2020.3.48 IL2CPP, OpenXR, no anti-cheat) - a candidate for the IL2CPP rig and for the VR + Voxon crossover the
  user wants (one player in VR, the others on Xbox controllers watching the Voxon).
- Untested profiles: Black Myth: Wukong, Oblivion Remastered, Sifu, Mothergunship, Train Sim World 2, Unreal Tournament.

- The Ascent (Unreal, DX11/DX12) and Risk of Rain 2 (Unity, DX11) are tested; six 1024 px cameras give 30 volumes a
  second on an RTX 3060 Ti.
- Mortal Kombat 1 (Unreal, DX12) works with its own experimental UE4SS (`rig/unreal/games/mk1`), following the
  fighters, with the surfaces' own colours (the Lighting slider). Not needed on Machine B.
- The app's Cameras tab has the player's feet position and "Turn the volume with the player": the player's view
  points to the back of the display (display +X right, +Y toward the viewer, +Z down).
- It also has World X / Y / height (squash or stretch the world per axis to fit the display); the display's size is
  the one the Voxon runtime reports (boundr, boundz), sent to the game when it changes (2026-10-16; was a Display picker). Self-tested on D3D11, D3D12 and Unity; not yet tried in a game. A GTA V rig
  should honour the same settings (`ueScaleX/Y/Z`, `ueBoundR/Z` in the control file; `capture_gpu.hpp`'s Derive).
- The colour range (the app's floor and ceiling) is applied on the GPU, in the pack pass, with the brightness remap.
- The Witcher 3 (Machine C, 2026-10-13) works, and shows a way for engines with no scene capture that GTA V may also
  need: the add-on hooks the game's own "render this frame" and renders the world again from each face's camera in
  the same frame as the player's view (`addon/red3_native.hpp`, `rig/redengine3`). About 34 complete cubes a second
  with five faces, 40 with four, the player's controls, frames and HUD untouched. Sweeping the game's one camera
  instead (its fallback) made the world stream in and out behind the moving camera and took the controls.
- Cyberpunk 2077 (Machine C, 2026-10-14) works through route 1, extra cameras in the same frame: its engine has
  render-to-texture cameras of its own, and the rig (`rig/redengine4`, a Cyber Engine Tweaks mod) puts five on V
  while she is loaded and stands them round her; the add-on finds each face's picture by its size and carries its
  depth out of the cameras' shared depth buffer (`addon/red4_rtt.hpp`). 30 complete cubes a second with five 512 px
  faces, the game at about 67 fps. The installer brings CET, RED4ext and Codeware (redist, MIT) unless they are there.
  Each camera needs a `virtualCameraName` of its own (sharing the default, all rendered one view) and a far plane the
  decode knows (`farPlaneOverride`). REDengine's faces now stand on the sides the app names, as Unreal's and Unity's
  do, tilt as theirs do, and take the automatic exposure (Red3Place and the pack pass changed; The Witcher 3 shares
  both, and was run again with them on 2026-10-15).
- Star Wars Jedi: Fallen Order (Machine C, 2026-10-15; UE 4.21, D3D11, Epic) works with the Unreal rig. The app's
  Cameras tab now has the world's Gamma and Saturation, and Player look for Unreal and Unity games: a capture a face
  of the player alone, so the player takes a brightness, gamma, saturation and density (voxels deep) of their own
  (`docs/ARCHITECTURE.md`, `docs/RESULTS.md` sections 11 and 14). Face codes must stay below 2048 (exact in half
  floats). The Witcher 3 and The Ascent were run again after the shared changes and work (`docs/RESULTS.md` 15).
- See-through surfaces (2026-10-15, Unreal and Unity): the rig lists the translucent materials it meets, the app's
  Cameras tab ticks them, and the ticked ones are drawn opaque by a fourth capture a face (code 1250 + face), which
  the add-on takes where it is nearer than the face's depth. With `solidOpacity` above 0 (Unreal), a fifth capture of
  them as the game draws them (code 1750 + face) keeps them solid only where that opaque: Jedi: Fallen Order's blade
  mesh is far wider than its glowing core (2026-10-16). Jedi: Fallen Order's profile ticks the blade from 50%, The
  Ascent's the hologram doors (`docs/RESULTS.md` 16). Not for REDengine yet.
- Need for Speed Heat (Frostbite, D3D11): parked 2026-10-16, not supported. Frostbite has no camera to add and the
  game no extra view to reuse, so only a sweep of its own camera was possible, and the user ruled sweeps out: only
  cameras the engine renders in the same frame. The code is out; what was measured is in `docs/RESULTS.md` 17.
- Star Wars Battlefront II (2017; Frostbite, D3D11, 2026-10-16): working in play (Instant Action, offline). No
  rig module: the add-on links views of its own onto the renderer's list (exe+0x3FFCD88) only while the frame builder
  runs, and sets their cameras with a hardware breakpoint in it (`addon/frostbite_views.hpp`, engine=frostbite). Three
  faces at most (in play a fourth overflows a table of four per-view records and crashes the game); each face drawn at
  faceRes + 8k so its targets are its own (taking turns cost three quarters of the frame rate). The game stopping
  itself every few minutes (0xC0000409) was the add-on: a D3D call under its own lock, whose destroy event re-took the
  lock on the same thread. Rule: no D3D call under a lock that the destroy_resource handler takes (`docs/RESULTS.md`
  18). The volume stands on
  the soldier's feet, read from the game each frame; player look marks what stands within 60 cm of them. Squadrons
  (Easy Anti-Cheat) and Battlefield 6 (Javelin) refused; the EA plan is `docs/EA_GAMES.md` (`docs/RESULTS.md` 18).
- The Sims 4 (1.128.90.1030, D3D11, Epic/EA app; 2026-10-18): working in a lot. No rig module: the add-on hooks the
  lot's render job (exe+0x11C4910) and has the game's thumbnail routine draw the world once per face from a game camera
  object of its own, into targets of its own the size of the game's picture; the face is the top-left square of an
  off-axis lens, because the routine reads back and restores viewports (scaled viewports shrank every pass). It needs
  the scene manager's own flags (+0x710) or it draws the roofs. Depth is forward: the decode gets the planes swapped.
  The volume follows the game's camera, its orbit point (on the ground) at the feet. Five faces: 30 volumes a second,
  the game at about 80 fps (170 without) (`addon/sims4_views.hpp`, `docs/RESULTS.md` 19).
- Crysis (2007; CryEngine 2 1.2.1, 64-bit D3D10, EA Play via the Xbox app; 2026-10-20): working in a level. No rig
  module: the add-on replaces I3DEngine::RenderWorld's vtable slot (2) and has the engine draw the world once per face
  from a CCamera of its own before the player's view; it copies the middle square of the back buffer and the linear
  depth target ($ZTarget) into textures shared with a D3D11 device of its own, where the decoder runs (D3D10 has no
  compute). The feet: the entity "Dude" through the game's own entity system. Five faces: 30 volumes a second, the game
  at 270-280 fps (374 without). The game itself does not start on a current PC without c1-launcher (the player's
  download; its startup crash is the game's copy protection, which the rig leaves alone) (`addon/cryengine2_views.hpp`,
  `docs/RESULTS.md` 20). Not yet checked in play: the feet, orbiting the simulator, the first-person weapon. Not yet
  fixed: a flicker - on some capture frames the player's view does not replace the last face's picture in the back
  buffer, and the face is presented.
- Gang Beasts (Unity 2021.3.33 IL2CPP, URP, D3D11, Xbox app copy; 2026-10-20/21): working in a local match. The Unity rig built a
  second time from the same sources as a BepInEx 6 plugin (`rig/unity/il2cpp`); the installer gives an IL2CPP game
  BepInEx 6 in place of 5, and finds Xbox app copies through the drives' `.GamingRoot` files (`xboxFolder`). The game
  tags no player, so the rig can now follow the first object of one of the game's classes meeting conditions
  (`followComponent`, `followWhen`, `followPoint`: the local, non-AI `Femur.Actor`, its feet from hips and feet). On the
  title screen the five cameras render (28.8 captures a second) and the add-on finds every face and depth. In a match
  the volumes first came out empty: URP drew the cameras into textures of its own (the renderer has features and keeps
  intermediate textures always) and copied only the colour across, so the faces' depth stayed empty. `rig/unity/Urp.cs`
  turns the renderer's features off and its intermediate textures to "auto" while the rig's cameras render only. Five
  512 px faces: 30 volumes a second, the game at its 60 fps (`docs/RESULTS.md` 21). The add-on's statistics line counts
  the draws into each face ("drawn into").
- Every game: the rig's scale (World radius) from inside the game - hold Page Up / Page Down (the app's Cameras tab
  picks the keys); the volume follows smoothly (`host/VoxonCaptureRig/ScaleKeys.cs`, `docs/ARCHITECTURE.md`).
