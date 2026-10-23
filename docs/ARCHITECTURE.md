# How it works

One capture method, three parts, and one wire between them.

```
 THE GAME'S PROCESS                                              THE APP (VoxonCaptureRig.exe)
 ┌───────────────────────────────┐                              ┌──────────────────────────────────┐
 │ 1. The cameras                │                              │ 3. The host                      │
 │    rig/unreal (UE4SS) or      │  render targets              │    FeedServer: newest volume     │
 │    rig/unity (BepInEx): up to │──────────────┐               │    CaptureLoop: Voxon SDK, draw  │
 │    six cameras round a player │              ▼               │    MainWindow: games, cameras    │
 │ 2. The decode                 │   GPU: colour + depth        │    RigInstaller: in and out      │
 │    addon/ (ReShade add-on)    │   → display-space voxels     │                                  │
 │    pack.hlsl, compact.hlsl    │──── named pipe ─────────────▶│  → Voxon display / simulator     │
 └───────────────────────────────┘   VoxelBatch, 30 a second    └──────────────────────────────────┘
                 ▲                                                          │
                 └──────── control.txt: which cameras, size, scale ◀────────┘
```

1. **The cameras** (`rig/unreal/`). A UE4SS Lua mod spawns up to six `SceneCapture2D` cameras on the faces of a box
   around the player. The box follows the player and never turns. Each camera renders the game's lit colour, with
   depth in alpha, into an RGBA16F render target. When a target is created it is cleared once to a **face code**
   (R = -1234, G = 1000 + face, A = -4321), so the decode can tell the faces apart without knowing anything about
   the engine.

   **Unity games** (`rig/unity/`) get the same cube from a BepInEx 5 plugin. It places ordinary Unity `Camera`s by
   the same maths (`Geometry.cs` is `rig.lua`'s, turned into Unity's axes) and renders them itself at the profile's
   rate. A shipped Unity game cannot compile a new shader, so depth cannot go into the colour's alpha: each camera
   keeps its own depth buffer, and the decode reads that instead. The volume follows the object with the profile's
   Unity tag (`followTag`, `Player` by default), or, in a game that tags no player, the first object carrying a
   component of one of the game's classes (`followComponent`) for which the profile's conditions hold (`followWhen`:
   that class's members, true, or false with `!`, joined by `&`); its feet are the object's position, or above the
   middle of the transforms `followPoint` names and as low as the lowest - a ragdoll's hips and feet (`Follow.cs`).
   In a game watched from a free camera, where nobody is the player, the volume follows the **view** instead
   (`followView`): its feet are the first thing straight ahead of the game's camera that the game does not move (a
   rigidbody is passed over), and it holds World radius when the camera is `followView` metres from that, in proportion
   nearer and further, eased. The rig scales itself to do it - every length in `Geometry` goes with the radius - and
   perspective lenses take the same pictures as of a world that much smaller, so the add-on, decoding at the settings'
   radius, needs no word of it.
   **IL2CPP games** (a `GameAssembly.dll`, no `Managed` folder) get the same rig, built a second time from the same
   sources against BepInEx 6's interop assemblies (`rig/unity/il2cpp/`, its own small entry file); the installer puts
   in BepInEx 6 in place of 5. F7 is read from Windows while the game is in front, as some games turn Unity's old
   input off. **URP games** (the Universal Render Pipeline): URP draws a camera into textures of its own and copies
   only the colour to the camera's target whenever its renderer has features (ambient occlusion, reflections...) and
   keeps intermediate textures always, its default - leaving the target's depth empty. So each camera asks URP for no
   depth or opaque texture, post-processing or anti-aliasing, and while the rig's cameras render, the renderer's
   features are off and its intermediate textures left to "auto"; straight after, it is as the game had it (`Urp.cs`,
   all by reflection, nothing in a game without URP). **HDRP games** (the High Definition Render Pipeline) hand a
   camera's target only the finished picture: each camera asks HDRP, through an AOV request, for its lit colour into the
   face's target and its depth into a second target with a code of its own (1100 + face), by which the add-on finds it
   (`Hdrp.cs`; Mono and IL2CPP, the IL2CPP build through BepInEx's interop classes).

   **Blueprint games** (`engine=blueprint`: Minecraft Dungeons II, whose protection ends it once its code is hooked)
   get the same cube as a **content mod**: a Blueprint actor that Blueprint Loader, the player's download, spawns in
   every level, built by `tools/mcdungeons2-mod/make_mod.py` (`docs/RESULTS.md` 25). It cannot read the control file,
   so the app works the cameras' places, turns, lenses and clips out itself and writes them, with the settings the mod
   acts on, as a small 16-bit PNG beside the game (`BlueprintBridge.cs`), which the mod reads twice a second. Like
   rig.lua it makes new targets, cleared to their codes, whenever which faces, their size, Lighting or Player look
   change. The add-on needs nothing new: it finds the faces by their codes, as rig.lua's.
2. **The decode** (`addon/`). A ReShade add-on, loaded as `dxgi.dll` beside the game's exe. It finds the camera
   targets by their face code, then runs two compute passes on the GPU. *Pack* tone-maps the colour, maps its brightness into the Colour range, and encodes depth;
   *compact* turns each pixel into a voxel in display space and keeps **one voxel per display cell**, because the
   display adds light and overlapping cameras would otherwise double it. Only the voxels are copied back. A worker
   thread sends each volume down the pipe. With `engine=unity` it also takes the depth buffer bound with each face's
   target and builds the pack pass with `DEPTH_BUFFER`, which turns Unity's reversed-Z depth back into distance and
   reads rows bottom-up, as Unity stores them on D3D11. The shaders come from Voxel Photonics' VLED plugin: see
   [`addon/PORTED.md`](../addon/PORTED.md).
3. **The host** (`host/VoxonCaptureRig/`). It reads volumes from the pipe, always keeping the newest, and draws them
   with the Voxon SDK (`Voxon/`, copied from GameKit: see `Voxon/COPIED.md`). On the simulator it draws at most 60
   volumes a second, so the game keeps its CPU. It also installs and removes the rig, and writes `control.txt`, which
   both halves in the game read again as soon as it changes (the add-on checks its time every frame, the Unreal and
   Unity rigs read it every capture, Cyberpunk's script 30 times a second through the add-on's `rig.txt`).

**The wire** is two messages of GameKit's protocol: *Reset* (8) when the add-on connects, and *VoxelBatch* (9) once a
volume. Each message is a u32 length, a type byte, a version byte, then the payload. A VoxelBatch payload is an i32
count followed by 15 bytes per voxel: x, y, z as floats in display units, then r, g, b. The pipe is
`\\.\pipe\voxon_capture_rig`.

**Settings** are the game's profile (`rig/profiles/<id>.ini`), copied beside its exe as `voxon_capture_ue.ini`, plus
`control.txt` from the app's Cameras tab. The control file wins whenever it names this game (`game=<id>`).

**The rig's scale from inside the game** (`ScaleKeys.cs`): while the game's window is in front, holding one key (Page Up
by default) grows World radius and another (Page Down) shrinks it - doubling or halving every second, easing in and out
over a sixth of a second - and the app writes the control file as it moves, up to 30 times a second, and saves the
value once it settles. Every rig places its cameras from the radius each capture and none rebuilds for it, so the volume
follows in a frame or two; a change of scale alone is not logged step by step (the add-on logs where it settled).

**Turning with the player** (`turnWithPlayer`) is the one setting that needs both halves to agree on more than the
settings. The rig turns its cameras by the player's view each capture, which only it knows. The add-on turns the
volume back by the rig's own yaw and a quarter turn (`DisplayTurnDeg` in `capture_gpu.hpp`), so the view lands on
display -Y, away from a viewer at the front; display +Y is toward them. The feet (`feetX`, `feetY`, `feetZ`) only move
the volume's centre, on the rig's side.

## Folders

| Folder | What it is |
|---|---|
| `games.json` | The supported games: id, name, profile, Steam app id or Epic app name, and the exe folder |
| `rig/unreal/` | The cameras for Unreal games: `main.lua` (loads the rig, F7) and `rig.lua` |
| `rig/unity/` | The cameras for Unity games: a BepInEx 5 plugin (`Rig.cs`, `Geometry.cs`, `RigSettings.cs`, `Follow.cs`, `Urp.cs`). `build.ps1` builds it into `redist/` against Risk of Rain 2's Unity assemblies; `il2cpp/build.ps1` builds the same sources as a BepInEx 6 plugin for IL2CPP games, against Gang Beasts' interop assemblies (BepInEx 6 must have run there once) |
| `rig/redengine3/` | The Witcher 3: a script mod (`voxonRig.ws`) that says where the volume is (or, on a game build the add-on's renderer hook does not know, sweeps the game's own camera round the cube), and `r4Player.patch.json`, the three lines the installer adds to the player's own copy of the game's player script |
| `rig/redengine4/` | Cyberpunk 2077: a Cyber Engine Tweaks mod (`voxon_rig/init.lua`) that puts five of the engine's own render-to-texture cameras on V and stands them round her |
| `rig/profiles/` | One settings file per game, the starting values for the Cameras tab |
| `tools/mcdungeons2-mod/` | Minecraft Dungeons II's rig, a Blueprint Loader mod: `make_mod.py`, which makes it in Unreal 5.6.1's editor, and `VoxonGraph`, the editor plugin that makes its graph nodes. Built into `redist/blueprint/mcdungeons2/` (its `README.md`) |
| `addon/` | The GPU decode: the ReShade add-on, its shaders, its self-test, its build script |
| `host/VoxonCaptureRig/` | The app |
| `redist/` | What the installer puts into a game: UE4SS 3.0.1, BepInEx 5.4.23.5 or BepInEx 6.0.0 be.788 (IL2CPP; LGPL-2.1), or Cyber Engine Tweaks 1.37.1, RED4ext 1.30.0 and Codeware 1.20.5 (all MIT); ReShade 6.7.3 (add-on build), the built add-on, the built Unity plugins (Mono and IL2CPP), and the built Blueprint mods (`blueprint/<game>/`). Only the last four are in git; the third-party files are fetched (`DEVELOPER_GUIDE.md`) |
| `docs/` | This file, and `RESULTS.md`, the measurements |
| `tools/` | `package.ps1`, which builds the folder a player gets; `mcdungeons2-mod/` (above) |

## Adding a game

1. Add an entry to `games.json`, with `"engine": "unity"` for a Unity game (Unreal is the default). Leave `exeDir`
   empty if you do not know it, and the app will search for the `*-Win64-Shipping.exe`, or for a Unity game the exe
   beside its `<Name>_Data` folder. For an Xbox app (PC Game Pass) copy, `xboxFolder` names its folder: the app finds
   every Xbox library from the drives' `.GamingRoot` files and looks in `<library>\<xboxFolder>\Content`.
2. Add `rig/profiles/<id>.ini`. Copy the nearest game's profile and set `radiusM`, which is how much world fits in
   the display. Set `hostGame=<id>`. A Unity game also needs `engine=unity`, and `followTag` if its player is not
   tagged `Player`.
3. A Unity game must be DX11: Unity on D3D12 is not decoded yet, and the add-on says so in its log. A Mono game (a
   `<Name>_Data/Managed` folder) takes BepInEx 5 and the rig; an IL2CPP game (`GameAssembly.dll`) takes BepInEx 6
   and the rig's IL2CPP build - the installer picks by what it finds, and refuses a folder that already has the
   other BepInEx. An IL2CPP game's first start after installing takes a minute or two longer, online: BepInEx 6
   builds the game's interop assemblies once, downloading Unity's base libraries to do it. In testing (Gang Beasts).
   A game that tags no player: `followComponent`, `followWhen` and `followPoint`; a game with no player at all, watched
   from a free camera: `followView` (above, *The cameras*).
4. If UE4SS fails to start with *"PS scan timed out"*, add `engineMajor` / `engineMinor` (Wukong needs 5 / 0).
5. An engine fork UE4SS does not fit (Mortal Kombat 1: the game crashes in UE4SS, or engine calls do nothing): give the
   game a newer UE4SS (`"ue4ss"`, a zip in `redist/`), only the hooks the rig needs (`"ue4ssHooks": ["EngineTick"]`),
   and whatever UE4SS needs beside it in `rig/unreal/games/<id>/` (MK1: a `VTableLayout.ini`). The installer brings
   all three, and Remove takes them out.
6. A game whose player is not a pawn (Mortal Kombat 1's fighters): `followClass=<class>` in its profile. The volume
   follows the middle of that class's visible actors. The rig's log says, every 5 s, what it is waiting for.

## Checks

- `VoxonCaptureRig.exe --selftest` runs the installer against throwaway game folders, Unreal and Unity: one empty,
  one already modded, and one with another mod's `dxgi.dll` or `winhttp.dll`. Remove must leave each one byte-for-byte as it was. The test also sends a
  fake add-on's volumes down the pipe. REDengine 3 and 4 have their own cases: Cyberpunk's with nothing in it, with
  the player's own loaders and a mod of theirs (kept), and with another mod in a loader's slot (refused). A Frostbite
  game's case, The Sims 4's and Crysis's check that only ReShade, the add-on and its settings go in. A Unity IL2CPP
  game's case checks that BepInEx 6 and the rig's IL2CPP build go in, that a folder with BepInEx 5 is refused, and that
  Remove also takes out what BepInEx 6 generates on its first run (its interop assemblies and .NET runtime).
- `addon\build.ps1` builds the add-on and runs its GPU self-test on WARP, on D3D11 and D3D12: a synthetic scene
  rendered from each camera, decoded, and checked against where the surfaces really are. The Unity cases render it
  the way Unity does (rows bottom-up, reversed-Z depth in a separate buffer). Other cases cover turning with the
  player, the surface colour, player look (the box drawn as the player: its own exposure, the floor untouched, density
  3 three deep; Unreal's way and Unity's), see-through surfaces (a see-through box's colour lands behind it, and the solid capture puts it back; Unreal's way and Unity's; with the glow capture, only its opaque half stands), the world's gamma and saturation, per-axis scale (squashed, stretched, turned) and the VX2's shape, and REDengine 3's swept
  camera: five faces drawn as the script places them, in the game's own depth and lens, decoded from the camera block
  alone. REDengine 4's cases draw the five faces as the Cyberpunk rig places them, each picture its own size and the
  depth at the face resolution, reversed (near / z). Frostbite's draw three faces as the add-on places the engine's views,
  through Frostbite's camera rows and back, picture and depth both at each face's own size. The Sims 4's draw five
  faces into pictures the size of a game window, wide and tall (480 x 256, 256 x 384), each face the square at the top
  left, the rest near and bright where the decode must not read; the colour 8-bit, the depth forward. Crysis's draw
  five square faces, the colour 8-bit and the depth linear (the sky 1), and check each face camera's frustum.

**Surface colour** (`lighting` below 1): each Unreal camera gets a second capture of the surfaces' base colour, with
lighting, shadows, occlusion and fog off, into a target cleared to the face's second code (2000 + face). The add-on
finds both by the same handshake and runs the `SURFACE_COLOUR` build of the pack pass: depth from the lit capture,
colour from the base colour, the lit colour mixed over it by `lighting`. Below 1 every camera renders twice.

**Player look** (`playerLook`, Unreal, Unity and Frostbite): in an Unreal game each camera gets a third capture, of the player alone, with the face's lens
and clip: only the pawn (with `followClass`, every visible actor of the class: both fighters) and the actors hung
under their components are drawn (`PRM_UseShowOnlyList`, listed again once a
second), and only their scene depth, in red (`SCS_SceneDepth`), into a target cleared to the face's third code (1500 +
face). Every face code stays below 2048, where half floats hold each integer exactly: at 3000 + face, 3001 read back as
3000 and 3003 as 3004, and two faces were taken for others. The pack pass takes a pixel for the player's where the
player's depth is the scene's (within 3 cm or 1%), gives it the player's exposure (stops on top of the world's), gamma
and saturation, and flags it (bit 24 of the colour word); the rest takes the world's gamma and saturation. The compact
pass draws a flagged voxel `playerDensity` deep: that many copies, a display cell apart, along its face's line of sight
into the player's body, each merged as any voxel is. The attached actors are found by walking each component's
`AttachChildren`, not by `GetAttachedActors`: once the player had actors attached, what that call's out-array handed
back crashed Jedi: Fallen Order inside UE4SS. Measured there (UE 4.21, D3D11, RTX 3060 Ti, 2026-10-15): five 2048 px
faces with their player captures at 25-27 volumes a second, the game at 42-49 fps.

In a Unity game (`rig/unity`) each face gets a second camera with the same lens, rendered by hand right after the
faces, forward-rendered into a target of its own cleared to the same third code. While those cameras render, and
only then, the player's renderers stand on a spare layer and the cameras draw that layer alone: a layer with no name,
else one the faces never draw, so nothing else on it can be the surface a face sees (Risk of Rain 2 names all 32 and
lends its `PlayerFakeActor`). The player's renderers are those under the followed object, and under the model a
profile names when the game keeps it apart (`playerModel=Component.member`; Risk of Rain 2:
`ModelLocator.modelTransform`). The add-on reads that camera's depth buffer, found as a face's own is, and the
`DEPTH_BUFFER` pack pass turns it into distance by the face's planes before the same comparison.

**See-through surfaces** (`seeList`, `solidMaterials`, `solidAll`; Unreal and Unity). A translucent material writes no
depth, so a face lays its colour on whatever is behind it: a lightsaber's blade on the wall, a glass door nowhere. The
rig keeps a list of the see-through materials it finds and writes it beside the app's control file
(`see_through_<game>.txt`: path, mesh or particle, the actors carrying it, how many now), and the app shows it as tick
boxes. Unreal: every mesh, particle and Niagara component, looked at once - the world looked through when the rig
starts, when the player changes (a load: Jedi: Fallen Order streams its levels into one world, which keeps the rig's
cameras, and nothing of the level was listed until this), and every 10 s after, paced at 300 a tick, those already
looked at passed over by name. Not `NotifyOnNewObject`: it calls back on whichever thread made the object, and The Ascent
(UE 4.26) makes them on its loading thread - the rig crashed the game as it started. Nor UE4SS's BeginPlay hook: it
listed nothing in Jedi: Fallen Order, and with it on that game crashed inside UE4SS in a fight (a lock released by a
thread that did not hold it). A material's blend mode 2 and up is
see-through; a dynamic instance goes by the asset it was made from, so a choice holds in every session. Unity: the
scene's renderers, every 2 s, and a material above the transparent queue (2500); its name is "shader/material".

For the chosen ones (or all) each face gets one more capture, the solid capture, cleared to the face's fourth code
(1250 + face). Just before it, each chosen slot of each component within reach of the volume (its half-diagonal and
100 m; where a component is, asked about once a second, at a time of its own) is set to the engine's opaque
`WorldGridMaterial` (Unity: a stand-in on `Hidden/Internal-Colored`, opaque queue, both sides); the solid and player
captures run, and every slot is set back. A capture sends the pending render changes first, so it sees the swap; the
player's own view never does. The solid capture draws those components alone (Unreal `PRM_UseShowOnlyList`, listed
again only when they change; Unity: the spare layer, as the player's renderers) and only their depth, and is skipped
while there are none, after one capture that leaves its target empty. The pack pass takes its depth wherever it is
nearer than the face's own (or the face saw nothing): a blade in front of the wall stands where it is, a door behind a
wall stays hidden. The colour stays the face's, the surface's own glow over what is behind. The player capture sees
the swap too, so a see-through part of the player (Cal's blade) takes the player's look. A component the game has
hidden (a blade put away) is left out, so with nothing else chosen near, the solid capture is skipped.

**Their opacity** (`solidOpacity`, Unreal). The stand-in is drawn wherever the mesh is, and a see-through mesh is often
wider than what shows: Jedi: Fallen Order's blade is a translucent mesh whose material fades a thin glowing core out to
the mesh's edge, so the whole mesh made solid stood as a flat card (`docs/RESULTS.md` 16). Above 0, each face gets a
fifth capture, the glow capture, cleared to the face's fifth code (1750 + face): the same components, drawn as the game
draws them, just before the swap (`SCS_SceneColorHDR`, with the see-through pass on: in alpha, the light they let
through, 1 where nothing was drawn). The pack pass takes the solid capture's depth, and the player capture's in front of
the scene, only where the glow says they are at least `solidOpacity` opaque; elsewhere the face's own depth stays, so a
blade's faint edge lies on what is behind it, as before. The game's own moving of the blade's vertices on the GPU (its
swing smear) is drawn in the glow but not by the stand-in, so the smear is never made solid.

The player and solid captures are depth captures, and a capture drawn with every show flag on is a full lit render:
each is added to its actor (`AddComponentByClass`, as the surface colour's) with lighting, shadows, occlusion,
reflections, fog, the see-through pass and post-processing off before it registers. Engines before 4.26 (Jedi: Fallen
Order) have no `AddComponentByClass` and keep the actor's own component, at the full cost. Measured in The Ascent
(`docs/RESULTS.md` section 16).

**Scale and display.** The cameras frame a box of half-extents World radius times `scaleX`, `scaleY`, `scaleZ` along the
rig's axes (the world's at Rig rotation 0), and the compact pass scales each axis back into the display: above 1 more
world is squashed in, below 1 it is stretched. Each side camera frames its own box (faces 0 and 2 look through X, 1 and
3 through Y); the top and bottom cover the larger of the two. The display's size (`boundR`, `boundZ`) is the one the Voxon
runtime reports while it drives the display or the simulator (`vxl_state_t`'s `boundr` and `boundz`: the volume is round,
one radius for x and y and a half-height; its `boundr2` is the radius squared), read every second and sent on when it
changes, so the cameras are framed to whatever display is running: a VX2-XL is 8 x 8 x 4 display units, a VX2 4 x 4 x 4,
the simulator's other models their own. On a display less than twice as wide as tall the side cameras take square
pictures, not half-height ones. With every scale 1 the maths is the original. Perspective lenses only.

**REDengine 3 (The Witcher 3): the faces rendered by the game's own renderer.** REDengine 3 has no scene capture, so
the add-on hooks the renderer's "render this frame" (`addon/red3_native.hpp`: `0x141C1A210(renderer, CRenderFrame*,
scene)` in witcher3.exe 5.0, found through its RTTI; a 14-byte jump over its first five instructions, put in when the
game's swapchain is made and only when those bytes read as expected). For each world frame it renders the world again
from each face's camera before the player's own: the frame's two camera sets (at +0x20 and +0x600, every matrix
measured and rebuilt from position, axes, lens and near/far) are rewritten for the face, the near plane on the box's
face and the far just past it, and put back afterwards. The game's camera never moves, so the world does not stream in
and out behind a swept camera and the player keeps the controls and every frame: about 40 complete cubes a second with
four faces at 1280 x 800 on an RTX 3060 Ti. Every render presents, and does all its GPU work before it returns
(measured), so the present path takes an extra view's picture and scene depth as the face being rendered, and puts the
player's last frame back over it on the monitor. The depth is reversed, as Unity's is, though the projection in the
camera block the GPU gets runs forward; the add-on reads each frame's camera from that block (camera-to-world at byte
400, the projection at 464, near and far at 608; `addon/sweep_red3.hpp`) and decodes the latest faces together round
the latest face's centre; the pack pass takes whole frames (`CamRel`, `DisplayColour`, `SrcStep`) and the compact
pass any face height (`ActiveRows`). The game draws its HUD into a layer of its own and lays it over the scene in one
last draw onto the back buffer; the add-on learns that layer from the player's renders and skips every draw into it
while a face renders, so the faces have no HUD and the player keeps theirs. The rig's script
(`rig/redengine3/voxonRig.ws`) says where the volume is - its centre at the player's feet and the rig's yaw. The app's settings reach the
script, and its report comes back, through memory (`addon/red3_link.hpp`): the script keeps an object of floats that
starts with three markers and counts its ticks, and the add-on finds it. The rig runs only while the app is taking
volumes, and never in a menu, a dialogue or a cutscene. REDengine's world is right-handed, Z up, metres; the display's
frame is it with Y and Z turned over, so nothing is mirrored.

NVIDIA Streamline's frame-generation plug-in hooks the same function as the game starts and, on a GPU without frame
generation, unloads again without taking its hook out; the game then crashes on its first world frame. The add-on puts
a dead hook there back to the function's own bytes before the first frame.

On a build of the game the hook does not know, the script sweeps the game's own camera instead: the engine's free camera
moved onto one face of the box per game frame, every face on, then one frame of the player's own, with the world's near
plane on the box's face and its far plane a code for the face (5000 m + 100 m x face) by which the add-on knows each
face frame; the HUD is hidden while it sweeps. It works, but the world streams in and out behind the moving camera (about 125 MB/s of reads, and frame
time growing by 1-2.5 ms a minute, measured), the player sees one frame in six, and the free camera takes the keyboard
and mouse.

**REDengine 4 (Cyberpunk 2077): the engine's own render-to-texture cameras.** REDengine 4 has cameras that render the
world into a texture in the same frame as the player's view (`entRenderToTextureCameraComponent`, the kind holocalls
use). The rig (`rig/redengine4/voxon_rig/init.lua`, a Cyber Engine Tweaks mod, with Codeware for `AddComponent`) gives
V five of them, one a face, and every frame stands them where Red3Place stands The Witcher 3's faces: a square lens of
90 degrees, the near plane on the box's face, each face on the side the app names (face 0 on +X, as for Unreal and
Unity), the side cameras raised and looking down by the tilt as Unreal's are. The pictures are the game's finished,
display-encoded ones: the pack pass meters and exposes them in linear light and encodes them again (`DisplayColour`),
so the app's automatic exposure and Brightness work as for the other engines (The Witcher 3's too). Measured on 2.31
(2026-10-13/14), and each one shaped the design:
- A component added to an entity already in the world is never attached: a light added to V lit nothing, and cameras
  so added never rendered. Added while V is assembled (`PlayerPuppet:OnRequestComponents`, which a save loading calls
  about 21 times for her) they work, so a save is loaded once after the install.
- Cameras on one entity that share a `virtualCameraName` all render the view of the one added last. Unset, every one is
  "Component": five on V gave five pictures of the top face's view, and the volume five copies of it stood on their
  sides. Each camera has a name of its own (its component name); then each renders its own view.
- Each camera writes a spare render texture of the game's (`.dtex`), whose size and format the rig sets while the game
  runs. A camera renders at its own `resolutionWidth` x `Height` (100 x 100 unset) and its picture is scaled to the
  texture; all of them render at the largest camera's resolution, one after another, into one shared transient depth
  buffer (D32 S8). So every camera renders at `faceRes`, and each texture is `faceRes + 1 + face` square: the add-on
  knows a face's picture by its size.
- A camera's last pass turns the shared depth buffer from depth-write to read, its picture is drawn next, and then the
  render graph hands the buffer's memory on. The add-on copies the buffer into a carry texture of its own at each such
  barrier (in the state the game's barrier set) and the carry into the face's own copy when the face's picture is bound
  for drawing (`addon/red4_rtt.hpp`); in the GPU's order that is the face's depth, complete. The picture is copied at
  present. The depth is reversed, 1 at the near plane and 0 at the far one. Unset, the far plane is about a kilometre
  away (decoded as infinite, the top face put the ground 12 cm below the side faces'), so the rig sets each camera's
  (`farPlaneOverride`, `Red4FarM`) just past the box, and the decode uses the same: the faces then agree within a
  centimetre, and nothing beyond the box is drawn. The pack pass reads the depth texel for texel and the picture at
  the nearest texel (`SrcSize`).
- The cameras follow moves, turns and near- and far-plane changes at once. `Game.GetPlayer()` hands back a new object every
  frame, so the rig finds V's cameras again only when she spawns.
The faces are decoded as REDengine 3's native views' are, in the rig's own frame, from the settings alone; the settings
reach the script as `rig.txt` beside it, written by the add-on (`on=1` only while the app is taking volumes). About 30
complete cubes a second with five 512 px faces, the game at about 67 fps on an RTX 3060 Ti. The installer brings Cyber
Engine Tweaks, RED4ext and Codeware unless the game has them (a player's own are kept), and gives a Cyber Engine Tweaks
of its own `~` as its overlay key: on its first start it otherwise takes the next key pressed and holds the keyboard.

**Frostbite (Star Wars Battlefront II): the engine's own list of views.** Frostbite gives a mod no camera and no script,
and there is no rig module: the add-on does it all (`addon/frostbite_views.hpp`). The renderer draws every view on a
list it keeps, and draws each in full - its own G-buffer, lighting, shadows and exposure - into targets of the view's
own size; a view that is not the player's own is not put on the screen. So the add-on links a node of its own per
face onto that list: a copy of the player's view state with its own id, viewport, lens (90 degrees square, the physical
camera off: on, the lens is the physical camera's, not the one given) and near and far planes, where Red3Place puts
them. Each face's camera is set where the frame builder has each view's camera in registers (xmm2-5, the node in rdi):
a hardware execute breakpoint, so the game's code is not changed; the same breakpoint reads the player's camera, and
the volume stands ahead of, beside and below it (`chaseAhead`, `chaseLeft`, `chaseDown`: the camera looks over the
soldier's right shoulder). Two more breakpoints, where the frame builder starts and where its ways out meet, link the
faces' nodes and take them off again: they are on the list only while it runs. Frostbite's world is Y up and a camera
looks along minus its third row; the add-on carries REDengine's placing into those axes. Measured on Battlefront II 1.1.8.16175
(2026-10-16), and each one shaped the design:
- The engine takes three views beyond the player's. In play each view adds a record each frame to a table of four,
  unchecked: a fifth view's record lands on the pointer after it, and the game crashed a second or two later (caught
  with a hardware write watch). In the menu a fifth view only dropped the frame rate to about 5 a second. So at most
  three faces are drawn: the first three of those the app has on.
- Left on the list between frames, the faces' nodes crashed the game as a level loaded (something else walks the list
  then), so they are linked only while the frame builder runs.
- A view's targets are its viewport's size rounded up to a multiple of 4, and views of one size share them, one after
  another. So face k is drawn `faceRes + 8k` square (`FbFaceSize`): each keeps its own, and the add-on knows a face by
  its size. Its scene depth (D32 S8) and its lit colour (R11G11B10, HDR, before the game's exposure and HUD) are the
  first such pair bound together in a frame, and are copied at present. The lit colour is written whole by the
  lighting and never cleared; a second R11G11B10 target of the face's size is cleared to black each frame and bound with
  the same depth too - in play before the lit one, in the menu after - so a target cleared that frame is not taken.
- The depth is reversed, between the near and far planes the add-on gives the view (looking down at a floor from four
  heights put it at the same height to a millimetre). The pack pass reads picture and depth at the nearest texel of
  the face's own size, and casts the ray through that texel (`SrcDepthScaled`).
- A face left out of a frame and drawn again the next is costly: four faces taking turns three at a time ran the game
  at about 40 frames a second where three every frame ran at 150. So the faces drawn are the first three of those on.
- The player: Battlefront II keeps its local players' soldiers in a list in its own data; the first is the player's, and
  its aim entity holds its feet. The volume stands on them, wherever the camera is - a third-person camera orbits the
  soldier, and a centre taken from it rose and fell as the player looked down and up. Without a soldier (dead,
  deploying) the camera and the chase offsets stand in.
- Player look: no capture of the player alone is possible, so the player is what stands within 60 cm of their feet and
  up to 2.4 m above them (the FROSTBITE build of the pack pass, its own constant buffer: b0 is as large as the D3D12
  root signature allows). A wall or a droid that close takes the player's look too.
The faces are decoded as Cyberpunk's are, in the rig's own frame, through the D3D11 decoder's frames. The add-on checks
the game's code at the four places it uses before it touches anything, and stays out of a build it does not know; it
knows the player's soldier and aim entity by their vtables, and reads them guarded.

**The Sims 4: the game's renderer, from the add-on's own cameras.** No rig module either (`addon/sims4_views.hpp`).
In a lot, the game's scene manager hands its render job to a worker thread each frame, which records the frame's scene
into a D3D11 deferred context; the add-on hooks that job (a jump over its first instructions, put in only when they
read as this build's) and, after the game's own work, has the game draw the world again once per face with the routine
its thumbnail renderer uses, from a camera of the add-on's: a copy of the game's main camera object, made each frame,
given the face's place and planes and rebuilt by the camera's own update. Those draws reach the GPU before the frame's
own scene, which clears the targets and draws over them, so the player's picture is untouched. Measured on The Sims 4 1.128.90.1030 (2026-10-18), and each one shaped the design:
- The routine draws into the frame's main colour (B8G8R8A8, the finished picture: no HDR) and depth (D24 S8) - the
  targets bound with a depth buffer - and clears neither. Its binds of them are turned to the face's own targets as
  they happen (`Ts4Bind`), cleared at the first; its passes of its own (copies of the picture so far, no depth buffer)
  are left alone.
- Between those passes it reads back the viewport and puts it back afterwards. Faces of their own size, with the
  viewports scaled to them, shrank each time round (512, 128, 32 ... texels). So a face's targets are the size of the
  game's picture, every viewport and scissor stays the game's, and the face's camera has a lens as wide and tall as
  that picture at the face's scale, shifted off its axis (the camera's lens centre), so that the square at its top left
  is the face's lens of 90 degrees (`Ts4Lens`). The decode reads that square (`SrcSize`, `SrcDepthScaled`); the rest is
  drawn and not used.
- Called as the thumbnail renderer calls it, the routine draws the roofs and the levels above the one shown. With the
  flags and mask the scene manager draws its own view with, it draws what the player sees.
- The depth runs forward, 0 at the near plane and 1 at the far one. The decode reads depth reversed; it is handed the
  planes the other way round, which turns its formula into the forward one.
- The camera orbits a point on the ground it looks at - the floor of the level shown, or the terrain - and follows
  the terrain as it pans. The volume stands on that point (the feet), so the ground stays at the height the app gives
  the feet, and with Turn the volume with the player it turns as the camera does.
The game's world is Y up, a camera looking along minus its third row - Frostbite's frame, so Battlefront II's helpers
place the faces. Five faces of 2048 x 1032 (the game's window) ran the game at about 80 frames a second where it ran at
170 without them; the faces are drawn in a lot only, live and build mode, not in the menus. The add-on checks the
render job's first bytes before it touches anything, and knows the scene manager by its vtable.

**Crysis: the game's 3D engine, from the add-on's own cameras.** No rig module (`addon/cryengine2_views.hpp`). Once a
frame the engine draws the player's view with `I3DEngine::RenderWorld`, on its main thread, returning with the view
drawn. The add-on finds the engine's global environment by its shape, replaces that function's slot in the 3D engine's
vtable (no code is patched), and before the game's own view calls the original once per face with a camera of its own:
a copy of the game's, given the face's matrix, a 90-degree vertical lens and its frustum worked out again. Each face
renders its own shadow maps; the middle square of the back buffer (the finished picture) and of the engine's linear
depth target, as many texels a side as the picture is tall, is exactly the face's 90 degrees, and is copied out the
moment the face is drawn. The game is D3D10, which has no compute shaders: the add-on makes a D3D11 device of its own on
the same GPU, the copies go into textures the two share (keyed mutexes), and the D3D11 decoder reads them with the depth
linear (`DepthLinear`: the value times the far plane). The world is Z up, its cameras' columns right, forward and up -
REDengine's frame, so the faces are placed by the same helpers. The feet are the player's entity's place, found by its
name through the game's own entity system. Five faces cost the game about 1 ms a frame at 30 volumes a second
(`docs/RESULTS.md` 20).

**F1 2020: the game's own reflection cubemap, from the add-on's cameras.** No rig module (`addon/ego_views.hpp`). With
Vehicle Reflections at Ultra High the engine draws a cubemap round the car every frame, six full views. Each view's
camera is a record in a view object, filled from the game's camera commands as the renderer replays them; the add-on
hooks the command handler (to learn which views are the cube's faces, and where the car is) and the replay (to write the
rig's cameras into five of the six once every command is in, before anything reads them). The cube's colour and its
shared depth are known by the names the game gives its D3D12 resources; each face's depth is copied in the game's own
command list as the face's lighting pass binds its slice, its colour at present, and the D3D12 decoder reads them in
Frames mode. The game's world is Frostbite's frame, so the faces are placed by the same helpers (`docs/RESULTS.md` 24).

**Snapshot** (`snapshot=<n>` in the game's settings): change the number and the rig writes every camera's picture, lit
and surface, as `.hdr` files to a `snapshots` folder beside the app. For seeing what the cameras see.

## Lessons this design keeps

- **Unreal must grow its scene buffers, not resize them** (`r.SceneRenderTargetResizeMethod 2`, which `rig.lua` sets).
  Otherwise any camera larger than the game's window reallocates them twice a frame, and frame rate falls by about
  three times.
- **Perspective lenses, not orthographic, on UE4.** UE4 drops dynamic lights from orthographic captures.
- **A Unity camera renders into one target with its own depth** (`targetTexture`). Colour and depth from two targets
  (`SetTargetBuffers`) left the colour black under deferred shading: Unity lit into a buffer of its own and never
  copied it across.
- **A shipping build has no unlit view.** Turning a capture's `Lighting` show flag off leaves only emissive light
  there (Mortal Kombat 1): the base colour, a capture source of its own, is the surfaces' colours.
- **UE4SS finds `ProcessEvent` by the stock vtable layout.** A fork with virtuals of its own (Mortal Kombat 1: two more
  in UObject) makes every engine call silently do nothing, and its hooks land on other functions. Its log's
  "VTable and scan addresses differ" is the sign.
- **Nothing large is allocated per volume** anywhere on the path. Volumes are 6-27 MB, 20-30 a second.
- **No voxel limit.** The host takes every volume, measured up to 1.8 million voxels.
