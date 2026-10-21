# EA's PC games: which the rig can go into

Researched 2026-10-16 (PCGamingWiki through its API, EA's own pages, GamingOnLinux's anti-cheat list). "None listed"
means PCGamingWiki lists no anti-cheat and GamingOnLinux's list has none: likely, not proven - check the install, as
for every game (`docs/GAMES.md`). The rig is offline and single-player only, and never goes around anti-cheat: EA Javelin
covers single-player too (EA: [deep dive](https://www.ea.com/security/news/eaac-deep-dive),
[help](https://help.ea.com/en/help/pc/ea-anticheat/), [Battlefield](https://www.ea.com/security/news/eaac-and-battlefield)),
so every Javelin game is out.

Routes: **Unreal** (UE4SS, `rig/unreal`), **Unity Mono on DX11** (BepInEx 5, `rig/unity`), **Frostbite on DX11** (the
add-on's own views, `addon/frostbite_views.hpp`: each game needs its few addresses found, as for Battlefront II), and
for **The Sims 4** the game's own renderer drawing cameras of the add-on's (`addon/sims4_views.hpp`).

## Viable now (an existing rig)

| Game | Engine | API | Anti-cheat | Offline single player | Note |
|---|---|---|---|---|---|
| Star Wars Jedi: Fallen Order | UE 4.21 | DX11 | none listed | yes | Working (`docs/RESULTS.md` 14) |
| Star Wars Battlefront II (2017) | Frostbite 3 | DX11/12 | FairFight (server-side only) | campaign, Instant Action, Arcade | Working in play, three faces (`docs/RESULTS.md` 18) |
| The Sims 4 | Maxis's own | DX11 | none *(measured)* | yes | Working (2026-10-18): the game's renderer draws the faces from the add-on's cameras, `addon/sims4_views.hpp` (`docs/RESULTS.md` 19) |
| Crysis (2007) | CryEngine 2 (1.2.1, 64-bit) | DX10 | PunkBuster: multiplayer servers only *(measured)* | yes | Working (2026-10-20): the 3D engine draws the faces from the add-on's cameras, `addon/cryengine2_views.hpp`; the game needs c1-launcher to start on a current PC (`docs/RESULTS.md` 20) |
| Star Wars Jedi: Survivor | UE4 | DX12 | none listed | yes | Denuvo removed 2024; UE4SS mods exist |
| Immortals of Aveum | UE 5.1 | DX12 | none listed | yes (offline mode) | Denuvo removed 2025 |
| Tales of Kenzera: ZAU | UE 4.27 | DX12 | none listed | yes | 2.5D: little depth |
| It Takes Two | UE 4.26 (Hazelight's AngelScript fork) | DX11 | none listed | local co-op | UE4SS attaching not yet checked |
| Split Fiction | UE 5.4 (AngelScript fork) | DX12 | none listed | local co-op | UE4SS attaching not yet checked |
| A Way Out | UE 4.17 | DX11 | none listed | local co-op (two players) | UE4SS attaching not yet checked |
| Medal of Honor: Above and Beyond | UE 4.25 | DX11 | none listed | yes | VR only |
| Fe | Unity 2017.1 | DX11 | none listed | yes | if Mono (`Managed\Assembly-CSharp.dll`) |
| Sea of Solitude | Unity 2018.4 | DX11 | none listed | yes | if Mono |

## Viable with per-game work (Frostbite)

| Game | Engine | API | Anti-cheat | Offline single player | Note |
|---|---|---|---|---|---|
| Need for Speed Heat | Frostbite 3 (2018.2) | DX11 | unknown (server-side checks online) | yes | Parked as a sweep (`docs/RESULTS.md` 17); retry through the view list |
| Need for Speed Payback | Frostbite 3 | DX11 | none listed | story | |
| Need for Speed Rivals | Frostbite 3 | DX11 | none listed | yes (offline mode) | game speed tied to 30 fps |
| Mirror's Edge Catalyst | Frostbite 3 | DX11 | none listed | yes | |
| Mass Effect: Andromeda | Frostbite 3 | DX11 | none listed | yes | |
| Dragon Age: Inquisition | Frostbite 3 | DX11 | none listed | yes | Denuvo |
| Star Wars Battlefront (2015) | Frostbite 3 | DX11 | none listed | solo missions, bots | Battlefront II's older sibling |
| Battlefield 4 / Hardline / 3 | Frostbite 3 / 2 | DX11 | PunkBuster, FairFight: multiplayer only | campaign | BF3 32-bit; Hardline and BF3 launch online |
| Need for Speed: The Run, Medal of Honor: Warfighter | Frostbite 2 | DX11 | none / PunkBuster (multiplayer) | story / campaign | 32-bit |
| Battlefield: Bad Company 2 | Frostbite 1.5 | DX9-11 | PunkBuster (multiplayer) | campaign | old engine generation |
| Dead Space (2023), Need for Speed Unbound, Dragon Age: The Veilguard | Frostbite (2021+) | DX12 only | none listed | yes | the add-on's Frostbite views need a D3D12 path |
| EA Sports PGA Tour | Frostbite | probably DX12 | none listed | single-player modes | unconfirmed |

## No rig for the engine yet

Mass Effect Legendary Edition, Mirror's Edge (2009), Alice: Madness Returns, Medal of Honor (2010) (Unreal 3); Titanfall 2
(Source); Crysis 2, 3 and Remastered (later CryEngines: Crysis 1's route may carry over, unchecked); Unravel 1-2 (PhyreEngine); Dead Space 1-3 (Visceral, DX9); Dragon Age:
Origins and II; SimCity (2013), Spore; Burnout Paradise Remastered; Need for Speed: Hot Pursuit Remastered,
Most Wanted (2012), Shift 1-2; Wild Hearts; Lost in Random and Plants vs. Zombies: Replanted (Unity IL2CPP); F1 22/23,
GRID Legends, DiRT Rally 2.0, DIRT 5 (Codemasters); Command & Conquer Remastered; Super Mega Baseball 4.

## No

- **Anti-cheat in single player:** Star Wars Squadrons (Easy Anti-Cheat on every launch), Battlefield 1, V and 6, Plants
  vs. Zombies: Garden Warfare 2, EA Sports WRC, F1 24/25, EA Sports FC 24-27, Madden 24-27, College Football 27 (EA
  Javelin).
- **Online only:** Battlefield 2042, Anthem (servers shut January 2026), Need for Speed (2015), Plants vs. Zombies:
  Garden Warfare and Battle for Neighborville, Apex Legends, Titanfall, Skate (2025), Rocket Arena, Knockout City, Star
  Wars: The Old Republic.

Not EA's on PC: NHL, UFC (console only), Monopoly (Ubisoft), Kingdoms of Amalur (THQ Nordic), Star Wars Battlefront
Classic Collection (Aspyr).

**Where per-game work pays most:** Need for Speed Heat (installed, DX11), Mirror's Edge Catalyst, Battlefront (2015),
Andromeda and Inquisition (single player, no anti-cheat), and Dead Space (2023) with Need for Speed Unbound (one D3D12
path would serve both: the same engine build).

## Why Battlefront II and not Squadrons

Not the engine: both are Frostbite on DX11, and the same views would serve. Squadrons' launcher starts the game under
Easy Anti-Cheat on every launch, and the rig - a ReShade add-on, hardware breakpoints in the game's code, its memory
read and written - is what anti-cheat exists to stop; the rig never goes round it. Battlefront II has no anti-cheat on
the PC (FairFight is server-side) and the EA app starts its exe directly. PCGamingWiki describes starting
`starwarssquadrons.exe` itself, without the launcher, for single player: that one thing may be tried, offline, the
campaign only, nothing patched or spoofed, and stopped if the game refuses.

## The plan (2026-10-16)

Each game, in order, goes through the same steps: is it installed and DX11 or DX12 (its settings, its exe's imports);
is anything anti-cheat in its folder or its launch; then the route's own work below; then a profile
(`rig/profiles/<id>.ini`), a `games.json` entry, a self-test case if the route is new, and a section in
`docs/RESULTS.md`. A game that fails the anti-cheat check is dropped, not worked round.

**1. Frostbite on DX11: Battlefront II's method, game by game.** What Battlefront II needed, and what each other
Frostbite game needs found again in its own build (`addon/frostbite_views.hpp`, `docs/RESULTS.md` 18):
- the renderer's list of views and the frame builder that walks it (its entry, the instruction after a view's camera
  rows are loaded, its common exit) - found from the engine's own type data and a trace of the view list. Builds of
  2017-2019 should be close kin to Battlefront II's: the code there is the first thing to search for;
- how many views the engine takes (Battlefront II: four in all; a fifth corrupts a table of four), and that a view left
  out of a frame is costly (four faces taking turns ran at a quarter of the frame rate) - so three faces at most;
- each face's targets: its size, the depth (D32 S8) and the lit colour (R11G11B10, the one never cleared);
- where the player stands: the local soldier (Battlefront II: a list in the exe's data, the soldier's aim entity);
  until found, the camera with offsets, which bobs as the camera orbits.
Order: **Need for Speed Heat** (installed; parked as a sweep - retry by its views; the player is a car, so its
position is the car's), **Star Wars Battlefront (2015)** (Battlefront II's older sibling, likely the nearest code),
**Mirror's Edge Catalyst**, **Mass Effect: Andromeda**, **Need for Speed Payback**, **Dragon Age: Inquisition**, then
Battlefield 4 and Hardline's campaigns. Rough cost: a day or two each once the first after Battlefront II is done.

**2. Frostbite on DX12.** Dead Space (2023), Need for Speed Unbound and Dragon Age: The Veilguard are DX12 only. The
views are the engine's again; the add-on's Frostbite capture (finding the faces' targets, copying them) needs a D3D12
path, as the Unreal and REDengine ones have. One path serves all three; Dead Space first (single player, a third-person
camera like Battlefront II's).

**3. Unreal: the existing rig.** Star Wars Jedi: Survivor (UE4, DX12, Denuvo removed: the Fallen Order profile is the
start), Immortals of Aveum (UE 5.1, DX12), Tales of Kenzera: ZAU (UE 4.27). It Takes Two, Split Fiction and A Way Out
run Hazelight's AngelScript fork of Unreal: first check that UE4SS attaches at all. Each: a profile and a test in play.
Medal of Honor: Above and Beyond is VR only: not planned.

**4. Unity: the existing rig.** Fe and Sea of Solitude, if they are Mono (`Managed\Assembly-CSharp.dll` in the
install): a profile and a test in play.

**5. Squadrons**, as above: one try at starting its exe directly, offline; if it refuses, no further.

Not planned: the games in "No rig for the engine yet" (a new engine each) and those under "No".
