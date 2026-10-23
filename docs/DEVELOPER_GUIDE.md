# Developer guide

How Voxon Capture Rig was made, what is in this repository and what is not, how to build it, and how to add a game.
The other documents go deeper: [`ARCHITECTURE.md`](ARCHITECTURE.md) is how each part works, [`RESULTS.md`](RESULTS.md)
what was measured, game by game, [`GAMES.md`](GAMES.md) every game looked at and where each stands, and
[`../CLAUDE.md`](../CLAUDE.md) where things stand today.

## What it is, in one paragraph

A PC game is played as usual, and a **cube of depth cameras** round the player - up to six, one on each face of a box
- captures the game's lit colour and depth, in the same frame as the player's own view. A ReShade add-on in the game
turns those pictures into voxels on the GPU and sends them down a named pipe to this app, which draws them on a Voxon
display (VX2, VX2-XL) or the Voxon simulator. What changes from engine to engine is only **how the cameras get into
the game**. The player installs and removes it all with two buttons in the app.

## How it was made

### Where it came from

Four earlier pieces of work fed into it:

- **VoxelSteamTap** (`github.com/jordan608/VoxelSteamTap`): GTA V through ScriptHookV, its own camera swept round the
  player one pose per frame and the frames tapped at `Present`. It worked, but the player saw only the frames left over
  (22% of them), and it was judged not good enough.
- **Voxel64**: games turned into voxels from their meshes ("mesh layers"), for Skyrim and Cyberpunk, and a host app.
- **GameKit** (`github.com/jordan608/GameKit`): the Voxon SDK's C# wrappers, the display layer that draws voxels with
  `DrawVox_Batch` and renders the simulator preview, and the message protocol this app still speaks. Copied into
  `host/VoxonCaptureRig/Voxon/` (see `Voxon/COPIED.md`).
- **Voxon's VLED Volume Capture plugin for Unreal**: capture cameras whose colour and depth two compute shaders turn into
  voxels. Those shaders, adapted, are `addon/shaders/` (see `addon/PORTED.md`).

On 2026-10-12 the project was started here with **one capture method**: cameras the game's engine renders in the same
frame, decoded on the GPU. Mesh layers and replaying a frame's draw calls from other viewpoints were dropped that day;
sweeping the game's own camera was ruled out on 2026-10-16. One method kept the app, the decode and the wire the same
for every game, and kept each game's look its own, lit by the game.

### How it grew

| Date | What was added |
|---|---|
| 2026-10-12 | The Unreal rig (a UE4SS Lua mod), the ReShade add-on and its GPU decode, the app with its installer, built and measured on The Ascent: six 1024 px cameras at 30 volumes a second on an RTX 3060 Ti. The Unity rig (a BepInEx 5 plugin) on Risk of Rain 2. Mortal Kombat 1, an Unreal fork, with a UE4SS of its own. Per-axis scale. |
| 2026-10-13 | The Witcher 3: the first game whose own renderer draws the faces (route 3, below). |
| 2026-10-14/15 | Cyberpunk 2077 through its engine's own render-to-texture cameras. Jedi: Fallen Order. Player look, gamma, saturation, see-through surfaces. |
| 2026-10-16 | Need for Speed Heat parked (only a sweep could get in). Sweeps ruled out. Star Wars Battlefront II through Frostbite's own list of views. |
| 2026-10-18 | The Sims 4: the game's thumbnail renderer drawing the lot from the add-on's cameras. |
| 2026-10-20 | Crysis: CryEngine 2's world renderer called once per face; its D3D10 pictures decoded on a D3D11 device of the add-on's. Unity IL2CPP games through BepInEx 6. |
| 2026-10-21 | Gang Beasts working in a match (URP's intermediate textures). The app's Launch game and Set folder buttons. Gears 5 and Gears Tactics refused (anti-cheat; copy protection). Totally Accurate Battle Simulator, the first game with no player: the volume follows the game camera's view and scales with it. |

### How the work is done

It was built with Claude Code (Anthropic's AI coding agent) working in this repository, directed by the Voxon team: the
team chose the games, set the rules below, played each game to where a test needed it, and judged what the display
showed; the agent read the engines and the games' code, wrote the rigs, the add-on and the app, and measured. Each game
went the same way:

1. **Check before touching it**: anti-cheat, copy protection, an offline mode (below).
2. **Find how the cameras get in**, and build the smallest thing that shows one face.
3. **Measure in the game**: volumes a second, the game's frame rate with and without the rig, the voxels landing where
   the surfaces are.
4. **Write it down**: a section in `RESULTS.md` with the numbers and with each finding that shaped the design.

Work moved between three PCs: Machine A, the main games PC; Machine B, with GTA V; Machine C, where most of this was
built. `CLAUDE.md` is the hand-over between them: the rules, and where things stand.

The self-tests are the safety net, and every new installer type or decode mode adds a case: the GPU self-test draws a
synthetic scene the way each engine draws it and checks the voxels against the real surfaces; the app's self-test
installs into throwaway game folders and checks that Remove puts each one back byte for byte.

## The rules

- **One capture method: the cube of depth cameras, rendered by the engine in the same frame as the player's view.**
  Never a sweep of the game's own camera, never draw calls replayed, never mesh layers. A game none of the three ways in
  can reach is not supported.
- **Offline only. Never circumvent anti-cheat or copy protection.** A game with kernel anti-cheat in the modes that
  matter, or online only, is refused. Where a game cannot start on a current PC without a community fix (Crysis:
  c1-launcher), the player gets it themselves, and the rig leaves the protection alone.
- **No arbitrary limits.** No voxel budgets or caps the data does not need. The app takes 1.8 million voxels a volume.
- **Players install it themselves, without an AI**: Install rig and Remove rig in the app, nothing edited by hand.
  Remove puts the game's folder back exactly as it was, a player's own mods included.
- **Simple and readable.** Engine knowledge lives in its own folder or file; the app, the host and the pipe are the
  same for every game. Temporary diagnostics come out before a commit.
- **Never commit** game files, third-party files (below), the Voxon runtime, or ScriptHookV.
- **Commit and push only when asked.** Push as the jordan608 account.

The repository was started afresh on 2026-10-21 with our own work only. The history before that, which carried
third-party files, is kept apart from it (and on `github.com/jordan608/CubeRig`).

## What is in git, and what is not

**In git: our own work.** The app (`host/`), the rigs (`rig/`), the add-on and its shaders (`addon/`), the game list and
profiles, the tools and the docs. The Voxon SDK wrappers copied from GameKit, and the shaders adapted from Voxon's VLED
plugin, are Voxon's own. Some built files in `redist/` are ours and stay in git:
`voxon_capture_ue.addon64`, `VoxonCaptureRig.Unity.dll`, `VoxonCaptureRig.Unity.IL2CPP.dll`, and the Minecraft
Dungeons II mod, `blueprint/mcdungeons2/VoxonRig_P.pak`, `.ucas` and `.utoc`. The Unity plugins can only be built
against a game's own assemblies, so a PC without Risk of Rain 2 or Gang Beasts could not make them; the mod needs
Unreal 5.6.1 and the Blueprint Mod Template (`tools/mcdungeons2-mod/README.md`).

**Not in git: everything third-party.** Put these in place by hand after cloning (`.gitignore` keeps them out). Check
each download against its SHA-256; a different file may not match what the rig was tested with. Keep a copy of the set
somewhere the team shares: the experimental UE4SS build is a moving tag and may not be downloadable again.

| File | Goes in | Where it comes from | Licence | SHA-256 |
|---|---|---|---|---|
| `ReShade64.dll` 6.7.3, the build with add-on support | `redist/` | reshade.me, "with full add-on support"; open the setup exe as an archive (7-Zip) and take `ReShade64.dll` | BSD 3-Clause | `ec9245d05c11751f2ac0d2256e6921ad8fb36be9172ef6d587856591eb729a25` |
| ReShade add-on SDK headers 6.7.3 | `addon/reshade-sdk/` | github.com/crosire/reshade, tag `v6.7.3`, `include/*.hpp` (see `VENDORED.md` there) | BSD 3-Clause or MIT | - |
| `UE4SS_v3.0.1.zip` | `redist/` | github.com/UE4SS-RE/RE-UE4SS, release v3.0.1 | MIT | `4b47d4bceddd2f561a4e395bfa00924ccfc945af576a2d0c613e6537846c57ec` |
| `UE4SS_v3.0.1-1165-g12ec45a0.zip` (Mortal Kombat 1) | `redist/` | the same, release experimental-latest as of 2026-10-12 | MIT | `d51512869bde8c32a1ebd6732a01fbb1343e6b3e0fb9cd0d8a9b1841b68cba97` |
| `BepInEx_win_x64_5.4.23.5.zip` | `redist/` | github.com/BepInEx/BepInEx, release v5.4.23.5 | LGPL-2.1 | `82f9878551030f54657792c0740d9d51a09500eeae1fba21106b0c441e6732c4` |
| `BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.788+5b766a3.zip` | `redist/` | builds.bepinex.dev, BepInEx bleeding edge, build 788 | LGPL-2.1 | `f4cc496bd098a0df4164b81e3737297707f13a47c2478dba2f60eefab784817a` |
| `cet_1.37.1.zip` (Cyber Engine Tweaks) | `redist/` | github.com/maximegmd/CyberEngineTweaks, release v1.37.1 | MIT | `1855017796a27f518199f5b7d7210ef1db7a5c5f0af468c68e04e6e666ad248c` |
| `red4ext-1.30.0.zip` | `redist/` | github.com/wopss/RED4ext, release v1.30.0 | MIT | `3a72225c9d2c46c99f4a4159d952b9d24366357c2423eb7ea255c84e9e11c0b0` |
| `Codeware-1.20.5.zip` | `redist/` | github.com/psiberx/cp2077-codeware, release v1.20.5 | MIT | `102989e199bad650fe6e53395c22bac53fdd7abecc6eeec3b0046886631591f0` |
| `BlueprintModTemplate` 2.4 (only to build the Minecraft Dungeons II mod; never in the repository or in a game) | a folder of its own outside the repository | Nexus Mods, Minecraft Dungeons II, Blueprint Loader, Files tab (ewanhowell5195) | the author's | `953592b6d0df1ede3de9391ecbcb1eb4813f208ede68c946b406c97b2864e9bf` |

Never anywhere in the repository: the **Voxon runtime** (`LedHost.dll`, `LedWin.dll`, `FTD3XXWU.dll`, `ledhost.ini`;
the app finds the installed VLED runtime), **ScriptHookV** and its loader (the player downloads them), **c1-launcher**
(the Crysis player's own download), **Blueprint Loader** (the Minecraft Dungeons II player's own download), and **any
game's files**. Avalonia comes from NuGet at build time.

## Building

**Needs:** the .NET 9 SDK; Visual Studio 2022 with *Desktop development with C++* and the Windows SDK (the add-on and
its shaders); the Voxon VLED runtime (the simulator); and the third-party files above.

1. **The add-on**: `addon\build.ps1 -Redist`. Compiles the shaders and the add-on, runs the GPU self-test on WARP
   (D3D11 and D3D12), and copies the add-on into `redist/`. Every case must pass.
2. **The Unity plugins**, only when `rig/unity/` changes. `rig\unity\build.ps1` builds the Mono plugin against Risk of
   Rain 2's assemblies (`-UnityManaged <game>\<game>_Data\Managed` for another game's). `rig\unity\il2cpp\build.ps1`
   builds the IL2CPP plugin against Gang Beasts' interop assemblies (`-GameRoot <game folder>` for another game's;
   BepInEx 6 must have run there once). Both copy into `redist/`.
   **The Minecraft Dungeons II mod**, only when `tools/mcdungeons2-mod/make_mod.py` changes: Unreal 5.6.1, the
   Blueprint Mod Template and the steps in `tools/mcdungeons2-mod/README.md`; its three pak files go into
   `redist/blueprint/mcdungeons2/`.
3. **The app**: `dotnet build host\VoxonCaptureRig -c Release`. The build copies `redist/`, `rig/` and `games.json`
   into the app's `payload\`, which is what Install rig puts into games, so **build the app again after anything in
   `redist/` changes**.
4. **Check it**: `VoxonCaptureRig.exe --selftest` writes `selftest.txt`, which must end `ALL CHECKS PASSED`.
5. **A player's folder**: `tools\package.ps1` (`-Addon` builds the add-on first) makes `dist\VoxonCaptureRig`: the
   app, its payload and the .NET runtime, and runs the self-test on it.

## Adding a game

### 1. Before touching it

- **Anti-cheat.** Look in the install for EasyAntiCheat, BattlEye, Javelin, Vanguard and the like, and at the game's
  store page. A kernel anti-cheat over the modes that matter, or a game that is online only, is not supported. Where
  the anti-cheat covers only online play, the rig is for the offline modes, and its notes in `games.json` say so.
- **Copy protection.** Never patched, bypassed or worked round. A plain exe has the usual sections (`.text`, `.rdata`,
  `.data`, ...); odd section names or a huge exe are worth a second look before going on. An Xbox app exe cannot be
  read on disk; read it from the running game instead. Signs of protection seen so far: an import table with one
  import per DLL and no name tables, code still encrypted in memory early in the start-up, a game that ends itself
  when its folder or its code changes or a debugger attaches (Gears Tactics, Minecraft Dungeons I and II:
  `docs/RESULTS.md` 22 and 25). A game that loads content mods may still take the rig as one, with its code untouched
  (Minecraft Dungeons II, through Blueprint Loader).
- **Engine and graphics API**, from the files: Unreal has a `*-Win64-Shipping.exe` and an `Engine` folder; Unity has
  `UnityPlayer.dll` and a `<Name>_Data` folder, with `Managed\` (Mono) or `GameAssembly.dll` (IL2CPP). Other engines
  show themselves in the exe's strings and class names. DX11 and DX12 are decoded directly; DX10 through a D3D11
  device of the add-on's (Crysis).
- Write what was found into `GAMES.md`'s library tables.

### 2. Choose the way in

| The game is | The way in | What it takes |
|---|---|---|
| Unreal Engine 4 or 5 | `rig/unreal`: `SceneCapture2D` cameras from a UE4SS Lua mod | Usually a `games.json` entry and a profile |
| Unity on DX11, Mono or IL2CPP | `rig/unity`: Unity `Camera`s from a BepInEx 5 or 6 plugin | Usually a `games.json` entry and a profile |
| An engine with cameras of its own a mod can reach | **Route 1**: a new rig module that places them (Cyberpunk 2077's render-to-texture cameras, `rig/redengine4`) | A module, an installer type, maybe an add-on mode |
| Unreal, with its code protected, but a content-mod loader | The cube as a **Blueprint mod**, its settings through `BlueprintBridge` (Minecraft Dungeons II, `tools/mcdungeons2-mod`) | A `make_mod.py` for the game, its pak files in `redist/blueprint/<id>/`, `engine: "blueprint"` |
| Anything else | **Route 3**: the add-on hooks the game's renderer and has it draw the world again from each face's camera (The Witcher 3, Battlefront II, The Sims 4, Crysis) | An add-on mode, its engine file, a self-test case |

### 3a. An engine the rig already has

1. **`games.json`**: an entry with `id`, `name`, `profile`, `engine` (`unreal` by default; `unity`, `redengine3`,
   `redengine4`, `frostbite`, `sims4`, `cryengine2`), where the app finds it (`steamAppId`, `epicAppName`,
   `installRegistryKey` and `installRegistryValue`, or `xboxFolder` for the Xbox app), `exeDir` (empty: the app
   searches), `exe` when it is not the usual one, `launch` when the game must be started by something other than its
   store (a link, or an exe in its folder), and the player-facing `status`, `graphics` and `notes`.
2. **`rig/profiles/<id>.ini`**: copy the nearest game's and set `hostGame=<id>`, `engine=` and `radiusM` (how much
   world fits in the display). These are only starting values: the app's Cameras tab overrides them while it runs.
3. **Unreal extras**: `engineMajor` / `engineMinor` in `games.json` when UE4SS times out scanning; for an engine fork
   UE4SS does not fit, a newer UE4SS (`ue4ss`, a zip in `redist/`), only the hooks the rig needs (`ue4ssHooks`), and
   what it needs beside it in `rig/unreal/games/<id>/` (Mortal Kombat 1: `VTableLayout.ini`); `followClass` in the
   profile when the player is not a pawn.
4. **Unity extras**: `followTag` if the player is not tagged `Player`; for a game that tags no player,
   `followComponent` (a class of the game's), `followWhen` (its members that must be true, `!` for false, `&` between)
   and `followPoint` (where the feet are); for a game with no player at all, watched from a free camera,
   `followView` (the volume follows what the camera looks at and scales with how far away it is); `playerModel` when
   the player's model is kept elsewhere. URP games are handled by `rig/unity/Urp.cs`. Unity on D3D12 is not decoded
   yet.
5. Test it (step 4).

### 3b. A new rig module (route 1)

1. `rig/<engine>/`: the mod that makes the cameras and stands them round the player. Use the same settings keys and
   the same placing as the other rigs (`rig/unreal/rig.lua`, `rig/unity/Geometry.cs`, `Red3Place` in the add-on), so
   the decode needs no new maths.
2. A way for the add-on to know each face's picture: a face code cleared into it (Unreal, Unity), or a size of its own
   (Cyberpunk: `faceRes + 1 + face`).
3. An add-on mode only if the decode must read something new (a depth buffer of its own, a picture already
   display-encoded): a `kEngine...` value in `addon/capture_gpu.hpp`, and a case in `addon/selftest_gpu.cpp` that draws
   faces the way that engine does.
4. An installer type in `host/VoxonCaptureRig/RigInstaller.cs`: every file it adds or changes goes into the install
   record, so Remove restores the folder exactly; a loader the player already has is kept; another mod in a slot the
   rig needs is refused. A section in `SelfTest.cs` for it: an empty game, one with the player's own mods, one with a
   foreign mod in the way.
5. The `engine` value in `games.json`, and the profile.

### 3c. Route 3: the game's renderer, hooked

Each of the four games done this way needed its way in found in that game's code, so each knows its own game build.
The steps were the same each time:

1. **Find what draws one view**: the engine's per-view render call (class names from RTTI, vtables, strings, a
   debugger). Battlefront II's views are a list the engine walks; The Sims 4's thumbnail renderer already draws the
   world from a camera it is given; Crysis's `I3DEngine::RenderWorld` is a vtable slot.
2. **Go in as lightly as possible**, and only into a build that is known: replace a vtable slot (Crysis), set hardware
   breakpoints, which change no code (Battlefront II), or put in a jump only when the bytes there read as expected
   (The Witcher 3, The Sims 4). Check before touching anything, and stay out of a build that does not match.
3. **Before the player's own view, draw once per face** from a camera of your own: a copy of the game's camera, placed
   as `Red3Place` places the faces (a square 90-degree lens, the near plane on the box's face, the far just past it).
   Put back everything that was changed, so the player's view is untouched.
4. **Get each face's colour and depth**: targets of its own, or copied at the moment the face is drawn. Know each face
   by its size or by the order it was drawn in.
5. **Measure, never guess, the depth and the world's axes**: reversed, forward or linear depth; Y or Z up. A floor
   looked at from several heights must land at one height, and neighbouring faces must agree to a centimetre.
6. **Where the volume stands**: the player's feet read from the game (an entity found by name, the game's list of
   local players), the camera as a stand-in.
7. **Where it goes in the code**: the engine knowledge in `addon/<engine>_views.hpp`, with no ReShade types in it so
   the self-test can build it; the ReShade side in `addon/voxon_capture_ue.cpp`; a `kEngine...` mode; `engine=<name>`
   in the profile; the engine in `AddonOnly` in `RigInstaller.cs` (only ReShade, the add-on and its settings go in);
   and a GPU self-test case and an app self-test section.

### 4. Test it in the game

- Install with the app while the game is closed, start it with Launch game, and get into play, offline.
- **Logs**, beside the game's exe: `voxon_capture_ue.log` (the add-on: every 5 s the game's frame rate, each face's
  pictures written and the draws into it, volumes decoded and sent, voxels in the last one, the worst present time),
  `ReShade.log`, and the rig's own (`UE4SS.log`, `BepInEx\LogOutput.log`).
- **Working means**: volumes at the profile's rate (30 a second), voxels where the surfaces are in the simulator, the
  game's frame rate with and without the rig, the feet in the right place, the volume orbiting and turning as set, and
  Remove leaving the folder as it was.
- **Traps met so far**: many games pause when they lose focus, so a screenshot tool that brings another window to the
  front pauses them; some ignore synthetic keyboard and mouse input (GameInput), so a person drives the menus; a game
  started with BepInEx has its console as its main window; a crash may show only as a hidden dialog or a dump file;
  "0 voxels" with every face found means the faces' depth is empty (Gang Beasts: `RESULTS.md` 21).

### 5. Write it down

- `RESULTS.md`: a section with the date, the game's and engine's versions, the numbers, and each finding that shaped
  the design.
- `GAMES.md`: the focus row, the module table if a module was added, the library row.
- `ARCHITECTURE.md` when a mechanism is new; `README.md`'s list of games; the player-facing `status` and `notes` in
  `games.json`; `CLAUDE.md`'s *Where things stand*.

### Checklist

- [ ] Anti-cheat and copy protection checked; an offline mode exists
- [ ] `games.json` entry and `rig/profiles/<id>.ini`
- [ ] New module or add-on mode: its installer type, its app self-test section, its GPU self-test case
- [ ] `addon\build.ps1` and `--selftest` both pass
- [ ] Tested in the game through the app's Install rig and Launch game; Remove checked
- [ ] Measured and written up (`RESULTS.md`, `GAMES.md`, `README.md`, `CLAUDE.md`)
- [ ] Temporary diagnostics out; no game or third-party files staged

## Lessons worth knowing before starting

Each is written up where it was found; these cost the most time.

- **Unity URP draws a camera into textures of its own** and copies only the colour across when its renderer has
  features, leaving the target's depth empty (`ARCHITECTURE.md`, *The cameras*).
- **A Unity camera renders into one target with its own depth.** Colour and depth from two targets left the colour
  black under deferred shading.
- **Unreal must grow its scene buffers, not resize them** (`r.SceneRenderTargetResizeMethod 2`), and use perspective
  lenses: UE4 drops dynamic lights from orthographic captures.
- **Face codes stay below 2048**, where half floats hold every integer exactly.
- **No D3D call under a lock that the add-on's destroy handler takes**: Battlefront II stopped itself every few
  minutes until this was found.
- **An engine's limits are real**: Battlefront II takes three views beyond the player's; a fourth overflowed a table
  and crashed the game.
