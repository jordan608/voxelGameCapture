# Games we can capture, and how

Every game in your Steam, Epic and PC Game Pass libraries, sorted by the capture method that would get the cube of cameras into it. **One method per engine, never one per game:** a new game on an engine we already cover is a `games.json` entry and a profile; a new engine gets one module, which then serves every game on it. Sources: `GAMES.md` (Steam and Epic, with what was measured on the installs) and `XBOX_GAMES.md` (PC Game Pass, engines from PCGamingWiki), merged by name, 2026-10-22.

**Before any game is attached, check its install for anti-cheat and copy protection** (`DEVELOPER_GUIDE.md`, *Adding a game*). *check* in the anti-cheat column means nobody has looked yet; that is nearly every Game Pass title.  Offline and single-player modes only, always.

## The capture methods

| Method | How the cameras get in | Games | Status |
|---|---|---|---|
| **Unreal rig** | `rig/unreal`: UE4SS and `SceneCapture2D` cameras round the player, the same frame | 153 | built; working in 3 games (The Ascent, Mortal Kombat 1, Jedi: Fallen Order) |
| **Unity rig** | `rig/unity`: BepInEx 5 (Mono) or 6 (IL2CPP) and Unity `Camera`s; URP and HDRP handled; follows a player, a class or the view | 221 | built; working in 3 games (Risk of Rain 2, Gang Beasts, TABS: Mono, IL2CPP, URP); HDRP in progress (Cities: Skylines II) |
| **REDengine 3 module** | `rig/redengine3` + the add-on: the renderer hooked, extra views in the player's frame | 1 | working (The Witcher 3) |
| **REDengine 4 module** | `rig/redengine4`: the engine's own render-to-texture cameras (CET mod) | 1 | working (Cyberpunk 2077) |
| **Frostbite module** | the add-on alone: faces added to the engine's own list of views | 1 | working (Battlefront II) |
| **CryEngine 2 module** | the add-on alone: the 3D engine's RenderWorld called once per face | 1 | working (Crysis) |
| **The Sims 4 module** | the add-on alone: the lot's render job, the thumbnail routine from the add-on's cameras | 1 | working |
| *a new engine module* | route 3: the engine's own renderer hooked, extra views in the player's frame - one per engine (below) | 151 on 67 engines | not built |

## Unreal rig (153 games)

`rig/unreal`: UE4SS and `SceneCapture2D` cameras round the player, the same frame.

A studio fork may need a UE4SS of its own and a vtable layout (Mortal Kombat 1), all through the same rig.

| Game | Engine | Library | Anti-cheat | Status |
|---|---|---|---|---|
| A Game About Digging A Hole | Unreal Engine 5 | Game Pass | check |  |
| Abiotic Factor | Unreal Engine 5 | Game Pass | check |  |
| Abyssus | Unreal Engine 5 | Game Pass | check |  |
| ABZU | Unreal 4 | Epic | check |  |
| ACE COMBAT 7: SKIES UNKNOWN | Unreal 4 | Steam | none |  |
| Aliens: Dark Descent (Windows) | Unreal Engine 4 | Game Pass | check |  |
| Aphelion | Unreal Engine 5 | Game Pass | check |  |
| ARK: Survival Ascended | Unreal Engine 5 | Game Pass | check |  |
| ARK: Survival Evolved | Unreal 4/5? | Epic | check |  |
| ARK: Ultimate Survivor Edition | Unreal Engine 4 | Game Pass | check |  |
| ASTRONEER | Unreal Engine 4 | Game Pass | check |  |
| Avowed Standard Edition | Unreal Engine 5 | Game Pass | check |  |
| Banishers: Ghosts of New Eden | Unreal Engine 5 | Game Pass | check |  |
| Beast of Reincarnation | Unreal Engine 5 | Game Pass | check |  |
| Beastro | Unreal Engine 5 *(Wikipedia)* | Game Pass | check |  |
| Black Myth: Wukong | Unreal 5 (measured) | Steam | none | profile ready |
| Blair Witch | Unreal 4 | Epic | check |  |
| Bleeding Edge | Unreal Engine 4 | Game Pass | check |  |
| Bluey's Happy Snaps | Unreal Engine 5 *(Wikipedia)* | Game Pass | check |  |
| Borderlands 3 | Unreal 4 | Steam, Epic | check |  |
| Breathedge | Unreal 4 | Epic | check |  |
| Bus Simulator 21 Next Stop | Unreal 4 | Epic | check |  |
| Call of the Elder Gods | Unreal Engine 5 | Game Pass | check |  |
| Chess Ultra | Unreal 4 | Epic | check |  |
| City of Brass | Unreal 4 | Epic | check |  |
| Core | Unreal 4 | Epic | check |  |
| Corsair Cove | Unreal Engine 5 | Game Pass | check |  |
| Crackdown 3 | Unreal Engine 4 | Game Pass | check |  |
| Crash Bandicoot 4: It's About Time (PC) | Unreal Engine 4 | Game Pass | check |  |
| DAEMON X MACHINA | Unreal 4 | Epic | check |  |
| Dead Island 2 | Unreal Engine 4 | Epic, Game Pass | check |  |
| Dead Static Drive | Unreal Engine 4 | Game Pass | check |  |
| Deep Rock Galactic | Unreal Engine 4 | Game Pass | check |  |
| Deliver Us Mars | Unreal 4 | Epic | check |  |
| Denshattack! | Unreal Engine 5 | Game Pass | check |  |
| Drop Duchy - Complete Edition | Unreal Engine 5 | Game Pass | check |  |
| Dune: Awakening | Unreal Engine 5 | Game Pass | check |  |
| Escape the Backrooms | Unreal Engine 4 | Game Pass | check |  |
| Eternal Strands | Unreal Engine 5 | Game Pass | check |  |
| Eximius: Seize the Frontline | Unreal 5 | Epic | check |  |
| F.I.S.T.: Forged In Shadow Torch | Unreal 4 | Epic | check |  |
| F1 Manager 2024 | Unreal 5 | Epic | check |  |
| Filament | Unreal 4 | Epic | check |  |
| First Class Trouble | Unreal 4/5? | Epic | check |  |
| FragPunk | Unreal Engine 5 | Game Pass | check |  |
| Genesis Noir: Windows Edition | Unreal Engine 4 | Game Pass | check |  |
| Ghostrunner 2 | Unreal 4 | Epic | check |  |
| Ghostrunner Demo | Unreal 4 | Epic | check |  |
| Ghostwire Tokyo | Unreal Engine 4 | Epic, Game Pass | check |  |
| GOALS | Unreal Engine 5 | Game Pass | check |  |
| Goat Simulator 3: Windows Edition | Unreal Engine 4 | Game Pass | check |  |
| Grounded | Unreal Engine 4 | Game Pass | check |  |
| Grounded 2 (Game Preview) | Unreal Engine 5 | Game Pass | check |  |
| Halo: Campaign Evolved - Standard Edition | Unreal Engine 5, Halo Engine | Game Pass | check |  |
| Halo: The Master Chief Collection | Halo Engine, Saber3D Engine, Unreal Engine 4 | Game Pass | check |  |
| Hell is Us | Unreal Engine 5 | Game Pass | check |  |
| Hellblade II: Senua's Saga | Unreal Engine 5 | Game Pass | check |  |
| Hellblade: Senua's Sacrifice | Unreal Engine 4 | Game Pass | check |  |
| Hello Mod Kit | Unreal 4/5? | Epic | check |  |
| Hi-Fi RUSH | Unreal Engine 4 | Game Pass | check |  |
| High On Life | Unreal Engine 4 | Game Pass | check |  |
| High On Life 2 | Unreal Engine 5 | Game Pass | check |  |
| Hogwarts Legacy | Unreal 4 | Steam, Epic, Game Pass | none (Denuvo DRM) |  |
| INDIKA | Unreal Engine 4 | Game Pass | check |  |
| Just Die Already | Unreal 4 | Epic | check |  |
| Keeper | Unreal Engine 5 | Game Pass | check |  |
| Kernel Hearts | Unreal Engine 5 | Game Pass | check |  |
| Kiln | Unreal Engine 5 *(Wikipedia)* | Game Pass | check |  |
| Little Nightmares Enhanced Edition | Unreal Engine 4 | Game Pass | check |  |
| Lost Records: Bloom & Rage | Unreal Engine 5 | Game Pass | check |  |
| Luna Abyss | Unreal Engine 4 | Game Pass | check |  |
| Maneater | Unreal 4 | Epic | check |  |
| Manor Lords (Game Preview) | Unreal Engine 4 | Game Pass | check |  |
| MechWarrior 5: Clans | Unreal Engine 5 | Game Pass | check |  |
| MechWarrior 5: Mercenaries | Unreal Engine 4 | Game Pass | check |  |
| Medieval Dynasty | Unreal Engine 4 | Game Pass | check |  |
| Minecraft Dungeons for Windows + Launcher | Unreal Engine 4 | Game Pass | none (measured) | not supported: copy protection (`RESULTS.md` 25) |
| Minecraft Dungeons II | Unreal Engine 5 *(Wikipedia)* | Game Pass | none (measured) | **working**: the rig as a Blueprint Loader mod (`RESULTS.md` 25) |
| Mistfall Hunter | Unreal Engine 5 | Game Pass | check |  |
| Mixtape | Unreal Engine 5 | Game Pass | check |  |
| Mortal Kombat 1 | Unreal 4, NetherRealm fork | Steam, Game Pass | none known (Denuvo DRM) | working |
| Mortal Shell | Unreal 4 | Epic | check |  |
| Mortal Shell Tech Beta | Unreal 4 | Epic | check |  |
| Mothergunship | Unreal 4 | Epic | check | profile ready |
| MOTORSLICE | Unreal Engine 5 | Game Pass | check |  |
| Mutant Year Zero Road to Eden | Unreal 4 | Epic | check |  |
| NINJA GAIDEN 2 Black | Unreal Engine 5 | Game Pass | check |  |
| Nobody Wants to Die | Unreal 5 | Epic | check |  |
| Obduction | Unreal 4 | Epic | check |  |
| Palworld | Unreal Engine 5 | Game Pass | check |  |
| Pax Dei | Unreal Engine 5 | Game Pass | check |  |
| Pigeon Simulator | Unreal Engine 5 | Game Pass | check |  |
| Pinball FX | Unreal 4 | Epic | check |  |
| Poker Club | Unreal 4 | Epic | check |  |
| Police Simulator: Patrol Officers | Unreal Engine 4 | Game Pass | check |  |
| Psychonauts 2 | Unreal Engine 4 | Game Pass | check |  |
| Quarantine Zone: The Last Check | Unreal Engine 5 | Game Pass | check |  |
| Redfall | Unreal Engine 4 | Game Pass | check |  |
| Redout: Enhanced Edition | Unreal 4 | Epic | check |  |
| Relicta | Unreal 4 | Epic | check |  |
| Relooted | Unreal Engine 5 | Game Pass | check |  |
| REMATCH | Unreal Engine 5 | Game Pass | Easy Anti-Cheat (measured) | not supported: anti-cheat on every launch (`RESULTS.md` 26) |
| Remnant II - Standard Edition | Unreal Engine 5 | Game Pass | check |  |
| Remnant: From the Ashes | Unreal 4 | Epic | check |  |
| RoboCop: Rogue City | Unreal Engine 5 | Game Pass | check |  |
| Roboquest | Unreal Engine 4 | Game Pass | check |  |
| ROUTINE | Unreal Engine 5 | Game Pass | check |  |
| RuneScape: Dragonwilds | Unreal Engine 5 | Game Pass | check |  |
| RV There Yet? | Unreal Engine 5 | Game Pass | check |  |
| Severed Steel | Unreal 4 | Epic | check |  |
| Sifu | Unreal 4 | Epic | check | profile ready |
| Solarpunk | Unreal Engine 5 | Game Pass | check |  |
| Solitairica | Unreal 4 | Epic | check |  |
| Soulstice | Unreal 4 | Epic | check |  |
| South of Midnight | Unreal Engine 4 | Game Pass | check |  |
| Spyro Reignited Trilogy | Unreal Engine 4 | Game Pass | check |  |
| Star Wars: Jedi Fallen Order | Unreal 4.21 (measured) | Epic | none (single player) | working |
| STARSEEKER: Astroneer Expeditions (Game Preview) | Unreal Engine 5 *(Wikipedia)* | Game Pass | check |  |
| State of Decay 2: Juggernaut Edition | Unreal Engine 4 | Game Pass | check |  |
| Stray | Unreal 4 | Steam | none |  |
| Subnautica 2 (Game Preview) | Unreal Engine 5 | Game Pass | check |  |
| Super Meat Boy 3D | Unreal Engine 5 | Game Pass | check |  |
| Tell Me Why: Chapters 1-3 | Unreal Engine 4 | Game Pass | check |  |
| The Alters | Unreal Engine 5 | Game Pass | check |  |
| The Ascent | Unreal 4.26 | Steam | none | working |
| The Bard's Tale IV: Director's Cut | Unreal Engine 4 | Game Pass | check |  |
| The Elder Scrolls IV: Oblivion Remastered | Unreal 5 over Gamebryo (measured) | Steam, Game Pass | none | profile ready |
| The First Descendant | Unreal Engine 5 | Game Pass | check |  |
| The Gunk | Unreal Engine 4 | Game Pass | check |  |
| The Lord of The Rings: Return to Moria | Unreal 4/5? | Epic | check |  |
| The Outer Worlds | Unreal 4 | Epic | check |  |
| The Outer Worlds 2 | Unreal Engine 5 | Game Pass | check |  |
| The Outer Worlds: Spacer's Choice Edition | Unreal 4 | Epic, Game Pass | check |  |
| The Talos Principle 2 | Unreal Engine 5 | Game Pass | check |  |
| The Thaumaturge | Unreal Engine 5 | Game Pass | check |  |
| Tiny Tina's Wonderlands | Unreal 4 | Epic | check |  |
| to a T | Unreal Engine 5 | Game Pass | check |  |
| Tony Hawk's Pro Skater 3 + 4 - Cross-Gen Edition | Unreal Engine 4 | Game Pass | check |  |
| Towerborne | Unreal Engine 5 | Game Pass | check |  |
| Train Sim World 2 | Unreal 4 | Epic | check | profile ready |
| Trepang2 | Unreal Engine 4 | Game Pass | check |  |
| TRIALS of MANA | Unreal Engine 4 | Game Pass | check |  |
| Unreal Tournament | Unreal 4 (known) | Epic | check |  |
| Unreal Tournament (pre-alpha) | Unreal | Epic |  | profile ready |
| Valor Mortis | Unreal Engine 5 | Game Pass | check |  |
| VALORANT | Unreal Engine 4, Unreal Engine 5 | Epic, Game Pass | check |  |
| Visions of Mana | Unreal Engine 4 | Game Pass | check |  |
| VOID/BREAKER (Game Preview) | Unreal Engine 5 | Game Pass | check |  |
| Voidtrain | Unreal Engine 4 | Game Pass | check |  |
| We Happy Few | Unreal Engine 4 | Game Pass | check |  |
| Wildgate | Unreal 5 | Epic, Game Pass | check |  |
| WUCHANG: Fallen Feathers | Unreal Engine 5 | Game Pass | check |  |
| Wuthering Waves | Unreal Engine 4 | Game Pass | check |  |

## Unity rig (221 games)

`rig/unity`: BepInEx 5 (Mono) or 6 (IL2CPP) and Unity `Camera`s; URP and HDRP handled; follows a player, a class or the view.

IL2CPP games (`GameAssembly.dll`) take BepInEx 6; Unity on D3D12 is not decoded yet. A game with no player (a city, a battle) follows the game camera's view (`followView`).

| Game | Engine | Library | Anti-cheat | Status |
|---|---|---|---|---|
| 1000xRESIST | Unity | Game Pass | check |  |
| 9 Kings (Game Preview) | Unity | Game Pass | check |  |
| Aerial_Knight's DropShot | Unity | Game Pass | check |  |
| Against All Odds | Unity IL2CPP | Epic | check |  |
| Albion Online | Unity | Game Pass | check |  |
| Among the Sleep - Enhanced Edition | Unity Mono | Epic | check |  |
| Aniimo | Unity | Game Pass | check |  |
| ANNO : Mutationem | Unity | Game Pass | check |  |
| As Dusk Falls | Unity | Game Pass | check |  |
| Automachef | Unity Mono | Epic | check |  |
| Backpack Hero | Unity Mono | Epic | check |  |
| Bad North Jotunn Edition | Unity Mono | Epic | check |  |
| BALL x PIT | Unity | Game Pass | check |  |
| Barbie Horse Trails | Unity | Game Pass | check |  |
| BATTLETECH | Unity, Unity | Game Pass | check |  |
| Battletoads | Unity | Game Pass | check |  |
| Behind the Frame: The Finest Scenery | Unity IL2CPP | Epic | check |  |
| Ben 10: Power Trip | Unity | Game Pass | check |  |
| Beyond Blue | Unity Mono | Epic | check |  |
| Beyond These Stars | Unity | Game Pass | check |  |
| Black Widow: Recharged | Unity Mono | Epic | check |  |
| Bloons TD 6 | Unity IL2CPP (known) | Epic | check |  |
| Blue Prince | Unity | Game Pass | check |  |
| Bluey: The Videogame | Unity | Game Pass | check |  |
| Bounty Star | Unity | Game Pass | check |  |
| Buckshot Roulette | Godot, Unity | Game Pass | check |  |
| Car Mechanic Simulator 2021 | Unity | Game Pass | check |  |
| Centipede Recharged | Unity IL2CPP | Epic | check |  |
| Cities: Skylines - Windows 10 Edition | Unity | Game Pass | check |  |
| Cities: Skylines II | Unity | Steam |  | in progress |
| Cities: Skylines II - PC Edition | Unity | Game Pass | check |  |
| Citizen Sleeper | Unity Mono | Epic | check |  |
| Clone Drone in the Danger Zone | Unity, Unity | Game Pass | check |  |
| CloverPit | Unity | Game Pass | check |  |
| Construction Simulator | Unity | Game Pass | check |  |
| Cooking Simulator Windows | Unity | Game Pass | check |  |
| Crab Game | Unity IL2CPP | Steam | none |  |
| Crashout Crew | Unity | Game Pass | check |  |
| Cris Tales | Unity Mono | Epic | check |  |
| Crying Suns | Unity Mono | Epic | check |  |
| Cult of the Lamb | Unity Mono | Steam | none |  |
| Darkwood | Unity Mono | Epic | check |  |
| Date Everything! | Unity | Game Pass | check |  |
| DAVIGO: Knight's Pass | Unity 2020.3.48 IL2CPP (measured), OpenXR | Steam | none (measured) |  |
| Death Howl | Unity | Game Pass | check |  |
| Deep Dish Dungeon | Unity | Game Pass | check |  |
| DEMON'S TILT | Unity Mono | Epic | check |  |
| Descenders | Unity | Game Pass | check |  |
| Descenders Next (Game Preview) | Unity | Game Pass | check |  |
| Disco Elysium - The Final Cut | Unity | Game Pass | check |  |
| Disney Dreamlight Valley | Unity | Game Pass | check |  |
| Doki Doki Literature Club Plus! | Unity Mono | Epic | check |  |
| Doors - Paradox | Unity Mono | Epic | check |  |
| DREDGE | Unity | Game Pass | check |  |
| Easy Delivery Co. | Unity | Game Pass | check |  |
| Egging On | Unity | Game Pass | check |  |
| Empyrion - Galactic Survival | Unity Mono | Epic | check |  |
| Enter the Gungeon | Unity Mono | Epic | check |  |
| Epistory - Typing Chronicles | Unity Mono | Epic | check |  |
| Escape Academy | Unity Mono | Epic | check |  |
| Escape Simulator | Unity | Game Pass | check |  |
| Eternal Threads | Unity IL2CPP | Epic | check |  |
| FINAL FANTASY | Unity | Game Pass | check |  |
| FINAL FANTASY II | Unity | Game Pass | check |  |
| FINAL FANTASY III | Unity | Game Pass | check |  |
| FINAL FANTASY IV | Unity | Game Pass | check |  |
| Football Manager 26 | Unity | Game Pass | check |  |
| Football Manager 26 Console | Unity | Game Pass | check |  |
| For The King | Unity Mono | Epic | check |  |
| For The King II | Unity | Game Pass | check |  |
| Forever Ago | Unity | Game Pass | check |  |
| Frog Sqwad | Unity | Game Pass | check |  |
| Gang Beasts | Unity 2021.3.33 IL2CPP (measured; the Xbox app copy) | Steam, Game Pass | none in local play | working |
| Genshin Impact | Unity | Game Pass | check |  |
| Gloomhaven | Unity | Epic | check |  |
| GNOG | Unity Mono | Epic | check |  |
| God's Trigger | Unity Mono | Epic | check |  |
| Gods Will Fall | Unity IL2CPP | Epic | check |  |
| Godzilla Voxel Wars | Unity IL2CPP | Epic | check |  |
| Golden Light | Unity Mono | Epic | check |  |
| Great God Grove | Unity | Game Pass | check |  |
| GRIME | Unity | Epic | check |  |
| Halcyon 6 | Unity Mono | Epic | check |  |
| Hell is other demons | Unity Mono | Epic | check |  |
| Herdling | Unity | Game Pass | check |  |
| Heroes of Might and Magic: Olden Era (Game Preview) | Unity | Game Pass | check |  |
| Hollow Knight: Voidheart Edition | Unity, Unity, Unity, Unity | Game Pass | check |  |
| Homeworld: Deserts of Kharak | Unity Mono | Epic | check |  |
| Horizon Chase Turbo | Unity Mono | Epic | check |  |
| House Flipper | Unity, Unity | Game Pass | check |  |
| Human Fall Flat | Unity *(Wikipedia)* | Game Pass | check |  |
| HUMANKIND | Unity Mono | Epic | check |  |
| Hungry Shark World | Unity | Game Pass | check |  |
| inKONBINI: One Store. Many Stories | Unity | Game Pass | check |  |
| Inscryption | Unity | Game Pass | check |  |
| INSIDE | Unity Mono | Epic, Game Pass | check |  |
| Iratus | Unity Mono | Epic | check |  |
| Jotun Valhalla Edition | Unity Mono | Epic | check |  |
| Kingdom New Lands | Unity Mono | Epic | check |  |
| Kingdom Two Crowns | Unity *(Wikipedia)* | Game Pass | check |  |
| Last Day of June | Unity Mono | Epic | check |  |
| Lawn Mowing Simulator | Unity IL2CPP | Epic | check |  |
| Legends of Runeterra | Unity | Game Pass | check |  |
| LEGO Builder's Journey | Unity IL2CPP | Epic | check |  |
| Little Kitty, Big City | Unity | Game Pass | check |  |
| Lost Castle | Unity Mono | Epic | check |  |
| Mages of Mystralia | Unity Mono | Epic | check |  |
| Microsoft Bubble | Unity | Game Pass | check |  |
| Microsoft Jewel | Unity | Game Pass | check |  |
| Minishoot' Adventures | Unity *(Wikipedia)* | Game Pass | check |  |
| Monster Sanctuary (Windows Version) | Unity | Game Pass | check |  |
| Monument Valley | Unity (known) | Epic | check |  |
| Monument Valley 2 | Unity IL2CPP | Epic | check |  |
| Moonlighter | Unity Mono | Epic, Game Pass | check |  |
| Moonlighter 2: The Endless Vault | Unity | Game Pass | check |  |
| Moving Out | Unity IL2CPP | Epic | check |  |
| Moving Out 2 | Unity | Game Pass | check |  |
| My Friend Peppa Pig | Unity | Game Pass | check |  |
| NARAKA: BLADEPOINT | Unity | Game Pass | check |  |
| Never Alone (Kisima Ingitchuna) | Unity Mono | Epic | check |  |
| New Super Lucky's Tale | Unity | Game Pass | check |  |
| Nickelodeon All-Star Brawl 2 | Unity | Game Pass | check |  |
| Night in the Woods | Unity Mono | Epic | check |  |
| Nine Sols | Unity | Game Pass | check |  |
| NINJA GAIDEN: Ragebound | Unity | Game Pass | check |  |
| OddBallers | Unity | Game Pass | check |  |
| Offworld Trading Company | Unity Mono | Epic | check |  |
| Ori and the Blind Forest: Definitive Edition | Unity | Game Pass | check |  |
| Ori and the Will of the Wisps | Unity | Game Pass | check |  |
| Out of Line | Unity Mono | Epic | check |  |
| Outbound | Unity | Game Pass | check |  |
| Overcooked | Unity Mono | Epic | check |  |
| Overcooked! 2 | Unity Mono | Epic, Game Pass | check |  |
| Overthrown | Unity | Game Pass | check |  |
| Pentiment | Unity | Game Pass | check |  |
| Phoenix Point | Unity | Game Pass | check |  |
| Pillars of Eternity 2: Deadfire - Ultimate Edition (PC) | Unity *(Wikipedia)* | Game Pass | check |  |
| Pillars of Eternity: Definitive Edition | Unity, Unity | Game Pass | check |  |
| Planet of Lana II | Unity | Game Pass | check |  |
| PlateUp! | Unity | Game Pass | check |  |
| PowerWash Simulator 2 | Unity | Game Pass | check |  |
| Prince of Persia The Lost Crown | Unity *(Wikipedia)* | Game Pass | check |  |
| Project Winter | Unity IL2CPP | Epic | check |  |
| ReCore: Definitive Edition | Unity | Game Pass | check |  |
| Relic Hunters Legend | Unity | Game Pass | check |  |
| REPLACED | Unity | Game Pass | check |  |
| Ring of Pain | Unity IL2CPP | Epic | check |  |
| Rising Hell | Unity Mono | Epic | check |  |
| Risk of Rain 2 | Unity 2021.3 Mono (measured) | Steam | none | working |
| Riverbond | Unity IL2CPP | Epic | check |  |
| Roadside Research (Game Preview) | Unity | Game Pass | check |  |
| ROBOBEAT | Unity Mono | Epic | check |  |
| Runbow | Unity Mono | Epic | check |  |
| Sable | Unity IL2CPP | Epic | check |  |
| Shadow Tactics: Blades of the Shogun | Unity Mono | Epic | check |  |
| Shadowrun Returns PC | Unity | Game Pass | check |  |
| Shadowrun: Dragonfall - Director's Cut PC | Unity | Game Pass | check |  |
| Shadowrun: Hong Kong - Extended Edition PC | Unity | Game Pass | check |  |
| Shape of Dreams | Unity | Game Pass | check |  |
| Sheltered | Unity Mono | Epic | check |  |
| Shift At Midnight | Unity | Game Pass | check |  |
| SKALD Against the Black Priory | Unity | Epic | check |  |
| Slain: Back From Hell | Unity Mono | Epic | check |  |
| Sledding Game | Unity | Game Pass | check |  |
| Speed Brawl | Unity Mono | Epic | check |  |
| SpeedRunners 2: King of Speed | Unity | Game Pass | check |  |
| SpongeBob SquarePants: The Patrick Star Game | Unity | Game Pass | check |  |
| Star Trucker | Unity | Game Pass | check |  |
| Starsand Island | Unity | Game Pass | check |  |
| Stranded Deep | Unity Mono | Epic | check |  |
| Sunless Sea | Unity Mono | Epic | check |  |
| Super Lucky's Tale | Unity | Game Pass | check |  |
| SUPERHOT: MIND CONTROL DELETE | Unity | Game Pass | check |  |
| Symphonia | Unity | Game Pass | check |  |
| Tales of the Neon Sea | Unity Mono | Epic | check |  |
| TCG Card Shop Simulator | Unity | Game Pass | check |  |
| Tempopo | Unity | Game Pass | check |  |
| Terraforming Mars | Unity Mono | Epic | check |  |
| Tharsis | Unity Mono | Epic | check |  |
| The Bard's Tale Trilogy | Unity | Game Pass | check |  |
| The Battle of Polytopia | Unity IL2CPP | Epic | check |  |
| The Bridge | Unity Mono | Epic | check |  |
| The Drone Racing League Simulator | Unity Mono | Epic | check |  |
| The Dungeon Of Naheulbeuk: The Amulet Of Chaos | Unity IL2CPP | Epic | check |  |
| The Escapists 2 | Unity Mono | Epic | check |  |
| The First Tree | Unity Mono | Epic | check |  |
| The Forest Quartet | Unity Mono | Epic | check |  |
| The Long Dark | Unity, Unity, Unity, Unity | Game Pass | check |  |
| The Messenger | Unity Mono | Epic | check |  |
| The Planet Crafter | Unity | Game Pass | check |  |
| The Rogue Prince of Persia | Unity | Game Pass | check |  |
| The Silent Age | Unity Mono | Epic | check |  |
| TOEM | Unity IL2CPP | Epic | check |  |
| Torment: Tides of Numenera | Unity | Game Pass | check |  |
| Total Chaos | Unity | Game Pass | check |  |
| Totally Accurate Battle Simulator | Unity | Game Pass | check | working |
| Totally Reliable Delivery Service | Unity | Game Pass | check |  |
| Trailmakers | Unity | Game Pass | check |  |
| Train Valley 2 | Unity Mono | Epic | check |  |
| Transference | Unity | Game Pass | check |  |
| Tunche | Unity Mono | Epic | check |  |
| TUNIC | Unity | Game Pass | check |  |
| Ultimate Sheep Raccoon | Unity | Game Pass | check |  |
| Undisputed | Unity | Game Pass | none (measured) | **working** in a fight: IL2CPP + HDRP (`RESULTS.md` 26) |
| Undying | Unity Mono | Epic | check |  |
| UNO | Unity | Game Pass | check |  |
| Vampire Crawlers: The Turbo Wildcard from Vampire Survivors | Unity *(Wikipedia)* | Game Pass | check |  |
| Vampire Survivors | Phaser, Electron, Unity, Unity | Game Pass | check |  |
| Vapor World: Over The Mind (Game Preview) | Unity | Game Pass | check |  |
| Void Bastards | Unity Mono | Epic | check |  |
| Warhammer 40,000: Mechanicus - Standard Edition | Unity Mono | Epic | check |  |
| Warhammer 40,000: Rogue Trader | Unity | Game Pass | check |  |
| Wasteland 2: Director's Cut | Unity, Unity | Game Pass | check |  |
| Wasteland 3 (PC) | Unity | Game Pass | check |  |
| Wasteland Remastered | Unity | Game Pass | check |  |
| Wheel World | Unity | Game Pass | check |  |
| while True: learn() | Unity Mono | Epic | check |  |
| Wildcat Gun Machine | Unity Mono | Epic | check |  |
| Windbound | Unity IL2CPP | Epic | check |  |
| Wobbly Life | Unity | Game Pass | check |  |
| Zenless Zone Zero | Unity | Game Pass | check |  |

## The engine modules already built

Each was built for one game, and serves any other game on its engine build; most of these engines have no other game in your libraries.

- **REDengine 3 module** (`rig/redengine3` + the add-on: the renderer hooked, extra views in the player's frame): The Witcher 3: Wild Hunt (Remastered) (Steam, working)
- **REDengine 4 module** (`rig/redengine4`: the engine's own render-to-texture cameras (CET mod)): Cyberpunk 2077 (Steam, working)
- **Frostbite module** (the add-on alone: faces added to the engine's own list of views): STAR WARS Battlefront II: Celebration Edition (Epic, in progress)
- **CryEngine 2 module** (the add-on alone: the 3D engine's RenderWorld called once per face): Crysis (Steam, in progress)
- **The Sims 4 module** (the add-on alone: the lot's render job, the thumbnail routine from the add-on's cameras): The Sims 4 (Epic, in progress)

## Engines with no module yet

Route 3, one module per engine: the engine's own drawing hooked and made to draw the world again from each face's camera, as for The Witcher 3, Battlefront II, The Sims 4 and Crysis. Ranked by how many of your games one module would serve. A studio's engine versions differ, so "one module" may need a table of builds; still far less than a rig per game.

| Engine | Games | Which | Note |
|---|---|---|---|
| **Anvil (Ubisoft)** | 19 | Assassin's Creed 2; Assassin's Creed Brotherhood; Assassin's Creed Freedom Cry - Standalone; Assassin's Creed III Remastered; Assassin's Creed IV Black Flag - Gold Edition; Assassin's Creed Mirage; Assassin's Creed Origins; Assassin's Creed Revelations; Assassin's Creed Rogue; Assassin's Creed Syndicate; Assassin's Creed Unity; Assassin's Creed Valhalla; Immortals Fenyx Rising; Skull and Bones; STEEP; Tom Clancy's Ghost Recon Breakpoint; Tom Clancy's Ghost Recon Wildlands; Tom Clancy's Rainbow Six Extraction; Tom Clancy's Rainbow Six Siege - Elite Edition | several are online-heavy; check each install (Ubisoft Connect) |
| **id Tech** | 11 | DOOM 3; DOOM: The Dark Ages; PREY; Quake 4; Quake II; Quake III Arena; Return to Castle Wolfenstein; The Evil Within; Wolfenstein II: Standard Edition; Wolfenstein: The Old Blood (PC); Wolfenstein: Youngblood (PC) | id Tech 2-5 draw with OpenGL and 6-8 with Vulkan, neither of which the add-on reads; check each |
| **Snowdrop (Ubisoft)** | 8 | Avatar: Frontiers of Pandora; Rabbids: Party of Legends; South Park: The Fractured but Whole; Star Wars Outlaws; Starlink: Battle for Atlas; The Settlers: New Allies; Tom Clancy's The Division; Tom Clancy's The Division 2 |  |
| **Creation / Gamebryo** | 7 | Fallout 3: Game of the Year Edition; Fallout 4; Fallout: New Vegas; Starfield; The Elder Scrolls III: Morrowind Game of the Year Edition (PC); The Elder Scrolls IV: Oblivion Game of the Year Edition (PC) - 2009; The Elder Scrolls V: Skyrim Special Edition | Fallout 3 and New Vegas are DX9; Fallout 4 and Skyrim SE DX11; Starfield DX12. Skyrim SE is a focus game (SKSE) |
| **Dunia (Ubisoft)** | 7 | FAR CRY 3; FAR CRY 3 BLOOD DRAGON; FAR CRY 4; Far Cry 5; Far Cry 6; Far Cry New Dawn; FAR CRY PRIMAL |  |
| **Apex (Avalanche)** | 4 | Call of the Wild: The Angler; Generation Zero; RAGE 2 (PC); theHunter: Call of the Wild |  |
| **Cobra (Frontier)** | 4 | Disneyland Adventures; Jurassic World Evolution 3; Planet Coaster 2; Zoo Tycoon: Ultimate Animal Collection |  |
| **Godot** | 4 | Brotato; Dome Keeper; Fogpiercer; Halls of Torment | Godot 3 draws with OpenGL; Godot 4 with Vulkan, OpenGL or (4.3 and later) D3D12: check each |
| **RE Engine (Capcom)** | 4 | RESIDENT EVIL 2; RESIDENT EVIL 3 for Windows; RESIDENT EVIL 7 biohazard; Resident Evil Village |  |
| **Swarm (Saber)** | 4 | RoadCraft; SnowRunner - Windows; Warhammer 40,000: Space Marine 2; World War Z: Aftermath |  |
| **Dragon Engine (RGG)** | 3 | Like a Dragon: Infinite Wealth; Like a Dragon: Pirate Yakuza in Hawaii; Virtua Fighter 5 R.E.V.O. World Stage |  |
| **ForzaTech** | 3 | Forza Horizon 5 Standard Edition; Forza Horizon 6 Standard Edition; Forza Motorsport Standard Edition |  |
| **Kex** | 3 | DOOM + DOOM II; DOOM 64; Heretic + Hexen | Nightdive's remasters: check the graphics API |
| **Northlight (Remedy)** | 3 | Control; FBC: Firebreak; Quantum Break |  |
| **UbiArt Framework** | 3 | Child of Light; RAYMAN LEGENDS; Valiant Hearts: The Great War |  |
| **Void** | 3 | DEATHLOOP; Dishonored 2; Dishonored: Death of the Outsider |  |
| **BANG!** | 2 | Age of Empires III: Definitive Edition; Age of Mythology: Retold |  |
| **CryEngine 3+** | 2 | Kingdom Come: Deliverance; Kingdom Come: Deliverance II |  |
| **Fallout Engine** | 2 | Fallout; Fallout 2 | DirectDraw era (1997-98) |
| **GameMaker** | 2 | Shelldiver; Well Dweller |  |
| **Genie** | 2 | Age of Empires Definitive Edition; Age of Empires II: Definitive Edition |  |
| **Phoenix (Ensemble Studios)** | 2 | Halo Wars 2: Standard Edition; Halo Wars: Definitive Edition |  |
| **SCUMM** | 2 | Day of the Tentacle Remastered; Full Throttle Remastered | the remasters' own renderer: check the API |
| **XNA / MonoGame / FNA** | 2 | Absolum; MARVEL Cosmic Invasion |  |
| **XnGine** | 2 | An Elder Scrolls Legend: Battlespire; The Elder Scrolls Adventures: Redguard | DOS era: DOSBox, nothing the add-on reads |
| **Zouna** | 2 | Microsoft Flight Simulator 2024 - Standard Edition; Resonance: A Plague Tale Legacy |  |
| **Adobe AIR** | 1 | Brawlhalla |  |
| **Asura** | 1 | Zombie Army 4: Dead War |  |
| **Big Huge Engine** | 1 | Rise of Nations: Extended Edition |  |
| **BlitzTech** | 1 | Fuzion Frenzy |  |
| **Build** | 1 | Shadow Warrior Classic (1997) | a source port draws the faces itself (below) |
| **Crystal Engine** | 1 | Lara Croft and the Temple of Osiris |  |
| **Decima** | 1 | DEATH STRANDING DIRECTOR'S CUT |  |
| **Disrupt** | 1 | Watch Dogs: Legion |  |
| **EGO (Codemasters)** | 1 | F1 2020 | DX11 and DX12 exes; lead: its split-screen (measured) |
| **Essence Engine** | 1 | Age of Empires IV: Anniversary Edition |  |
| **FromSoftware** | 1 | DARK SOULS III |  |
| **GameBox** | 1 | Trackmania Turbo |  |
| **GFD** | 1 | Persona 5 Royal |  |
| **GIANTS Engine 10** | 1 | Farming Simulator 25 (PC) |  |
| **Glacier (IO Interactive)** | 1 | HITMAN 2 |  |
| **Heaps** | 1 | Wartales |  |
| **In-House Engine** | 1 | Cricket 26 - The Official Game of the Ashes |  |
| **Insomniac Engine** | 1 | Sunset Overdrive |  |
| **IW (Call of Duty)** | 1 | Call of Duty: WWII (Windows) |  |
| **Jet Engine** | 1 | Asphalt Legends |  |
| **Katana Engine** | 1 | Wo Long: Fallen Dynasty |  |
| **LWJGL** | 1 | Minecraft: Java & Bedrock Edition for PC | Java Edition draws with OpenGL; Bedrock Edition (its own engine) with DX11/12 |
| **LÖVE** | 1 | Balatro |  |
| **Messiah Engine** | 1 | Where Winds Meet |  |
| **Moai** | 1 | Broken Age |  |
| **Motor** | 1 | Indiana Jones and the Great Circle | Vulkan only (built on id Tech 7) |
| **Nitrous Engine** | 1 | Ara History Untold: Anniversary Edition |  |
| **nTT** | 1 | LEGO Star Wars: The Skywalker Saga |  |
| **Onrush engine** | 1 | DIRT 5 |  |
| **Onyx** | 1 | South Park: The Stick of Truth |  |
| **Phoenix (Micro Forté)** | 1 | Fallout Tactics |  |
| **Platinum Engine** | 1 | NINJA GAIDEN 4 Standard Edition |  |
| **Quake engine** | 1 | Quake | DOS era / OpenGL |
| **RAGE (measured)** | 1 | Grand Theft Auto V (Machine B's account) |  |
| **RD3D** | 1 | Anno 1800 |  |
| **ROMU Engine** | 1 | Wreckfest |  |
| **Slayer** | 1 | Killer Instinct: Anniversary Edition |  |
| **STEM Engine** | 1 | The Evil Within 2 (PC) |  |
| **The Forge** | 1 | Hades II |  |
| **Vicarious Visions Alchemy** | 1 | Crash Bandicoot N. Sane Trilogy |  |
| **Wolfenstein 3D engine** | 1 | Wolfenstein 3D | DOS era: DOSBox |

### Build (Shadow Warrior Classic): a camera rig through an open-source port

Voxel64 already runs Shadow Warrior Classic its own way, but a camera-rig version can be made, and it would serve every Build game. The Steam release draws through DOSBox or a software / OpenGL source port, which the add-on cannot read (it decodes DirectX 11 and 12). But the Build engine's and Shadow Warrior's source are both open (GPL), and maintained source ports run the original game data - Raze runs Shadow Warrior, Duke Nukem 3D, Blood, Redneck Rampage and Exhumed / PowerSlave. So the Build module would be such a port with the cube added: each frame it draws the world again from each face's camera itself (the engine can draw any view - Duke 3D's security cameras do), with a depth buffer, places the faces by the same settings and maths as the other rigs (`capture_gpu.hpp`'s Derive), and sends the voxels down the same pipe. Nothing is hooked, as the engine's own code is there to change; the decode would be the port's own (its OpenGL or Vulkan pictures read back, or the add-on's shaders run on them), not the ReShade add-on. What it needs: a port chosen (Raze: OpenGL and Vulkan, all the Build games; or VoidSW, Shadow Warrior alone), the player's own game data (never ours), and, the port being GPL, the module kept in a repository of its own, apart from this one's own work. Of your games only Shadow Warrior Classic is on Build today.


## Ruled out (93)

Each with its reason. Anti-cheat over the modes that matter is never got round, nor is copy protection; a store package Windows locks is not opened; the add-on decodes DirectX 11 and 12 only. *(known)*: well known, not measured on an install; *(measured)*: found on this PC; *(reported)*: from published reports.

| Game | Engine | Library | Why it is ruled out |
|---|---|---|---|
| Chivalry 2 | Unreal 4 | Epic, Game Pass | anti-cheat: EAC |
| Chivalry 2 - Public Testing | Unreal 4 | Epic | anti-cheat: EAC |
| Hood Outlaws and Legends | Unreal 4 | Epic | anti-cheat: EAC |
| MORDHAU | Unreal 4/5? | Epic | anti-cheat: EAC |
| Spellbreak | Unreal 4 | Epic | anti-cheat: EAC (servers closed) |
| War Thunder | Dagor | Steam, Game Pass | anti-cheat: EasyAntiCheat |
| ELDEN RING | FromSoftware | Steam | anti-cheat: EasyAntiCheat, kernel |
| Apex Legends | Source (modified) | Steam | anti-cheat: kernel anti-cheat |
| Call of Duty | IW | Steam | anti-cheat: kernel anti-cheat |
| Fall Guys | Unity IL2CPP | Steam, Epic | anti-cheat: kernel anti-cheat |
| FOR HONOR | AnvilNext | Steam, Game Pass | anti-cheat: kernel anti-cheat |
| Rainbow Six Siege | AnvilNext 2.0 | Steam | anti-cheat: kernel anti-cheat |
| Splitgate | Unreal 4 | Steam | anti-cheat: kernel anti-cheat |
| THE FINALS | Unreal 5 | Steam, Game Pass | anti-cheat: kernel anti-cheat |
| Battlefield 1 / 4 / V / 2042 / 6 | Frostbite | Steam | anti-cheat: kernel anti-cheat (EA Javelin; Battlefield 6 also needs Secure Boot) |
| Madden NFL 26, EA Sports FC 26, F1 25 | Frostbite; Frostbite; EGO | Game Pass (EA Play) | anti-cheat: EA Javelin, single player too (measured: `EAAntiCheat.GameServiceLauncher.exe`; `RESULTS.md` 26) |
| Counter-Strike 2 | Source 2 | Steam | anti-cheat: VAC |
| Gears of War: E-Day | Unreal Engine 5 | Game Pass | Easy Anti-Cheat (reported) |
| Gears of War: Reloaded | Unreal Engine 3 | Game Pass | Easy Anti-Cheat (reported) |
| Gears 5 Game of the Year Edition | Unreal Engine 4 | Game Pass | Easy Anti-Cheat on every launch (measured) |
| Star Wars Squadrons | Frostbite (known) | Epic | Easy Anti-Cheat on every launch (measured) |
| EVE Online | Trinity | Steam | online only |
| Path of Exile / Path of Exile 2 | own | Steam | online only |
| World of Tanks | own | Steam | online only |
| DayZ | Enfusion, Real Virtuality | Game Pass | online play under BattlEye (known) |
| Rocket League | Unreal Engine 3, Unreal Engine 6 | Epic, Game Pass | online play under Easy Anti-Cheat (known) |
| Call of Duty: Black Ops 6 - Standard Edition (Windows) | IW engine | Game Pass | online play under Ricochet, Activision's kernel anti-cheat (known) |
| Call of Duty: Black Ops 7 - Cross-Gen Bundle | IW engine | Game Pass | online play under Ricochet, Activision's kernel anti-cheat (known) |
| Call of Duty: Black Ops Cold War (Windows) | IW engine | Game Pass | online play under Ricochet, Activision's kernel anti-cheat (known) |
| Call of Duty: Modern Warfare - Digital Standard Edition (Windows) | IW engine | Game Pass | online play under Ricochet, Activision's kernel anti-cheat (known) |
| Call of Duty: Modern Warfare II - Standard Edition (Windows) | IW engine | Game Pass | online play under Ricochet, Activision's kernel anti-cheat (known) |
| Call of Duty: Modern Warfare III - Standard Edition (Windows) | IW engine | Game Pass | online play under Ricochet, Activision's kernel anti-cheat (known) |
| Call of Duty: Vanguard - Standard Edition | IW engine | Game Pass | online play under Ricochet, Activision's kernel anti-cheat (known) |
| The Elder Scrolls Online | HeroEngine | Epic, Game Pass | online-only (an MMO) (known) |
| Diablo IV PC | unknown | Game Pass | online-only (known) |
| Enlisted | Dagor Engine | Game Pass | online-only (known) |
| Enlisted Xbox One | Dagor Engine | Game Pass | online-only (known) |
| Fallout 76 (PC) | Creation Engine | Game Pass | online-only (known) |
| Heroes of the Storm | unknown | Game Pass | online-only (known) |
| Once Human | NeoX | Game Pass | online-only (known) |
| Riders Republic Standard Edition | AnvilNext | Game Pass | online-only (known) |
| The Crew 2 | unknown | Game Pass | online-only (known) |
| The Crew Motorfest | unknown | Game Pass | online-only (known) |
| World of Tanks Modern Armor | unknown | Game Pass | online-only (known) |
| World of Tanks: HEAT | unknown | Game Pass | online-only (known) |
| World of Warships | Bigworld | Epic, Game Pass | online-only (known) |
| World of Warships: Legends | unknown | Game Pass | online-only (known) |
| Call of Duty: Warzone | IW engine | Game Pass | online-only, online play under Ricochet, Activision's kernel anti-cheat (known) |
| Overwatch | unknown | Game Pass | online-only, under Blizzard's anti-cheat (known) |
| Dead by Daylight | Unreal 5 | Epic, Game Pass | online-only, under Easy Anti-Cheat (known) |
| Sea of Thieves | Unreal 4 | Steam | online-only, under Easy Anti-Cheat (known) |
| Sea of Thieves: 2026 Edition | Unreal Engine 4 | Game Pass | online-only, under Easy Anti-Cheat (known) |
| SMITE 2 (Game Preview) | Unreal Engine 5 | Game Pass | online-only, under Easy Anti-Cheat (known) |
| Warhammer 40,000: Darktide | Autodesk Stingray Engine | Game Pass | online-only, under Easy Anti-Cheat (known) |
| Fortnite | Unreal 5 | Epic, Game Pass | online-only, under Easy Anti-Cheat and BattlEye (known) |
| League of Legends | Hextech Engine | Game Pass | online-only, under Riot Vanguard (a kernel anti-cheat) (known) |
| Teamfight Tactics | Hextech Engine | Game Pass | online-only, under Riot Vanguard (a kernel anti-cheat) (known) |
| Halo Infinite | Slipspace Engine | Game Pass | under Easy Anti-Cheat (known) |
| Garry's Mod | Source | Steam | DX9 |
| Men of War: Assault Squad 2 | GEM 2 | Steam | DX9 |
| Torchlight II | OGRE | Steam, Epic | DX9 |
| Planetary Annihilation: TITANS | Uber | Steam | OpenGL |
| Wolfenstein: The New Order | id Tech 5 | Steam, Epic, Game Pass | OpenGL |
| Mortal Kombat 11 | Unreal 3, NetherRealm fork | Steam | Unreal 3 |
| Mass Effect Legendary Edition | Unreal 3, modified | Steam | Unreal 3 (UE4SS cannot attach) |
| Borderlands 2 | Unreal 3 | Steam, Epic | Unreal 3, DX9 |
| Assassin's Creed Chronicles - Russia | Unreal Engine 3 | Game Pass | Unreal 3: UE4SS, the Unreal rig's loader, starts at Unreal 4.12 |
| Assassin's Creed Chronicles : China | Unreal Engine 3 | Game Pass | Unreal 3: UE4SS, the Unreal rig's loader, starts at Unreal 4.12 |
| Batman: Arkham Knight | Unreal Engine 3 | Game Pass | Unreal 3: UE4SS, the Unreal rig's loader, starts at Unreal 4.12 |
| Dishonored - Definitive Edition | Unreal Engine 3 | Epic, Game Pass | Unreal 3: UE4SS, the Unreal rig's loader, starts at Unreal 4.12 |
| DOOM Eternal | id Tech 7 | Steam, Game Pass | Vulkan, and no way into the engine |
| Among Us | Unity IL2CPP (measured) | Steam, Game Pass | 2D |
| Blasphemous 2 | Unity IL2CPP (measured) | Steam | 2D |
| Children of Morta | Unity | Steam | 2D |
| Death's Gambit - Afterlife | GameMaker | Epic | 2D |
| Hades | Supergiant's own | Steam, Game Pass | 2D |
| Helltaker | Unity | Steam | 2D |
| Hollow Knight: Silksong | Unity Mono (measured) | Steam, Game Pass | 2D |
| Loop Hero | GameMaker | Epic | 2D |
| Minit | GameMaker | Epic | 2D |
| Stardew Valley | MonoGame | Steam, Game Pass | 2D |
| Terraria | XNA / FNA | Steam | 2D |
| The Textorcist | GameMaker | Epic | 2D |
| Undertale | GameMaker (measured) | Steam | 2D |
| River City Melee Mach!! | ? | Steam | 2D (believed) |
| 30XX | own | Steam | 2D (save folder only on C) |
| River City Ransom: Underground | own | Steam | 2D (save folder only on C) |
| TowerFall Ascension | MonoGame | Steam | 2D (save folder only on C) |
| Gears Tactics | Unreal Engine 4 | Game Pass | ends itself when files are added (measured) |
| Need for Speed Heat | Frostbite | Steam | Frostbite with no extra view to reuse; sweeps ruled out (measured) |
| 3D Aim Trainer | Unity? | Steam | not planned (GAMES.md gives no reason) |
| MultiVersus | Unreal 5 | Steam | nothing to run |
| Gears of War: Ultimate Edition for Windows 10 | Unreal Engine 3 | Game Pass | old Store (UWP) package |
| Gears of War 4 | Unreal Engine 4 | Game Pass | old Store (UWP) package, locked folder (measured) |

## Engine not known (151)

Look at the install (`DEVELOPER_GUIDE.md`, *Before touching it*) to place these.

33 Immortals; A Plague Tale: Innocence; Alien: Isolation; Amnesia: A Machine for Pigs; Amnesia: Rebirth; Ancient Enemy; Antstream Arcade; Arx Fatalis; Ascend to ZERO; Baldur's Gate and Baldur's Gate II: Enhanced Editions; Battle vs Chess; Beatdown City Survivors; BioShock 2 Remastered; BioShock Infinite: Complete Edition; BioShock Remastered; Black Jacket; BlazBlue Entropy Effect X; BLiNX: The Time Sweeper; Blizzard Arcade Collection PC; Blood Dungeon; Borderlands: The Pre-Sequel; Box Maze 2: Agent Cubert; Brews & Bastards; Brothers - A Tale of Two Sons; Cassette Beasts; Castlevania Anniversary Collection; Cave Story+; Chained Beasts; Conker: Live and Reloaded; Crimson Skies: High Road to Revenge; Dark Side of War; DEPLOYMENT; Destiny 2; Deus Ex - Mankind Divided; Diablo; Diablo II: Resurrected PC; Diablo III: Reaper of Souls - Ultimate Evil Edition PC; Dice A Million; Dodo Duckie; Dragon Age: Inquisition - Game of the Year Edition; DRAGON BALL XENOVERSE 2; DreamWorks Gabby's Dollhouse: Ready to Party; Dungeon of Elements; Dying Light; Echo Generation 2; Echo Weaver; Evoland Legendary Edition; Faster Than Light; FINAL FANTASY V; FINAL FANTASY VI; Firefighting Simulator - The Squad; FixForce; Frostpunk; Galactic Civilizations III; Galactic Civilizations III (Test branch); Geneforge 1: Mutagen; Grim Fandango Remastered; Guacamelee! 2; Guacamelee! Super Turbo Championship Edition; Halo: Spartan Assault; Halo: Spartan Strike; Heroes of Might and Magic 3 - Complete Edition; Heroes of Might and Magic II - Gold Edition; HITMAN; HITMAN 3; Hyper Light Drifter; Into The Breach; Junkster; Kabuto Park; KILL IT WITH FIRE! 2; Killing Floor 2; KillingFloor2Beta; Legend of Mana; Limbo; Mad King Redemption; MAVRIX by Matt Jones; Metro Last Light Redux; Microsoft Flight Simulator (2020) Standard 40th Anniversary Edition; Microsoft Mahjong; Microsoft Solitaire Collection; Minecraft Legends for Windows + Launcher; Minecraft Preview for Windows; MIO: Memories in Orbit; MONOPOLY Madness; Monster Train 2; Monsters are Coming!; NBA 2K21; Neon Abyss 2; NEW MONOPOLY; Ni no Kuni II: Revenant Kingdom PRINCE'S EDITION; Ni no Kuni Wrath of the White Witch Remastered; Nioh: The Complete Edition; No Man's Sky; Paladins; Paradigm; Pathway; PAYDAY 2; PBA Pro Bowling 2026; Psychonauts (Windows 10); Rebel Galaxy; Retro Classics; Rise of the Tomb Raider: 20 Year Celebration; Rising Storm 2: Vietnam; RollerCoaster Tycoon 3: Complete Edition; RPG in a Box; Rush: A DisneyPixar Adventure; Saints Row IV Re-Elected; Saints Row The Third Remastered; Sandustry (Game Preview); Second Extinction; Shadow of the Tomb Raider: Definitive Edition; shapez; Sniper Ghost Warrior Contracts; Sonic Mania; StarCraft II Campaign Collection; StarCraft Remastered; Super Meat Boy Forever; Supermarket Simulator; Tamashika; Tears of Metal (Game Preview); TerraTech Legion; The Bard's Tale ARPG : Remastered and Resnarkled; The End is Nigh; The Escapists; The Jackbox Party Pack 4; The Royal Writ; Tiny Bookshop; Tiny Tina's Assault on Dragon Keep: A Wonderlands One-shot Adventure; Tomb Raider GAME OF THE YEAR EDITION; Tomb Raider I-III Remastered Starring Lara Croft; Total War: THREE KINGDOMS; Total War: WARHAMMER; Total War: WARHAMMER III; Trackmania Starter Access; Trials Fusion; Trials Rising; Valley of the Ancient; Warcraft I: Remastered; Warcraft II: Remastered; Warcraft III: Reforged; Warface: Clutch; Warframe; Watch Dogs 2; WATCH_DOGS; Winds of Arcana: Ruination; Winter Burrow; Wonder Boy The Dragons Trap; World War Z; Yoku's Island Express; Young Suns; ZOMBI
