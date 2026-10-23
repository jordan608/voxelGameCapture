# PC Game Pass games by engine

Every game in the PC Game Pass catalogue (the Xbox app on Windows), 515 games, as Microsoft listed them on 2026-10-22 for the Australian store, grouped by engine. The rig's two engine modules are Unreal (`rig/unreal`, UE4SS: Unreal 4 and 5) and Unity (`rig/unity`: Mono through BepInEx 5, IL2CPP through BepInEx 6, the built-in renderer, URP and HDRP).

**Where this comes from.** The catalogue: Microsoft's Game Pass list (catalog.gamepass.com, "All PC Games") and its store catalogue for the names. The engines: each game's PCGamingWiki page, matched by name, or, where that names none, its Wikipedia infobox (marked *(Wikipedia)*); the developer is the store's. A name match can be wrong, and a page can be out of date. Nothing here says a game has no anti-cheat or copy protection that stops the rig: **check the install before anything is attached** (`docs/DEVELOPER_GUIDE.md`, *Adding a game*), as Gears 5 (Easy Anti-Cheat) and Gears Tactics (ends itself when files are added) showed.

**Counts:** Unreal Engine 6: 2, Unreal Engine 5: 65, Unreal Engine 4: 50, Unreal Engine 3: 6, Unity: 132, other: 173, unknown: 87.

## Unreal Engine

Unreal 4 and 5 are what `rig/unreal` covers (UE4SS). Unreal 3 is not: UE4SS starts at 4.12. A studio fork may need a UE4SS of its own (Mortal Kombat 1) or may be out of reach (Gears Tactics: The Coalition's 4.11 branch).

### Unreal Engine 6 (2)

| Game | Developer | Engine | Tried |
|---|---|---|---|
| Fortnite | Epic Games Inc. | Unreal Engine 4, Unreal Engine 5, Unreal Engine 6 |  |
| Rocket League | PSYONIX LLC | Unreal Engine 3, Unreal Engine 6 |  |

### Unreal Engine 5 (65)

| Game | Developer | Engine | Tried |
|---|---|---|---|
| A Game About Digging A Hole | DoubleBee, Mi'pu'mi Games | Unreal Engine 5 |  |
| Abiotic Factor | Deep Field Games | Unreal Engine 5 |  |
| Abyssus | DoubleMoose Games | Unreal Engine 5 |  |
| Aphelion | DON'T NOD | Unreal Engine 5 |  |
| ARK: Survival Ascended | Studio Wildcard, Grove Street Games | Unreal Engine 5 |  |
| Avowed Standard Edition | Obsidian Entertainment | Unreal Engine 5 |  |
| Banishers: Ghosts of New Eden | DON'T NOD | Unreal Engine 5 |  |
| Beast of Reincarnation | GAME FREAK inc. | Unreal Engine 5 |  |
| Beastro | Timberline Studios | Unreal Engine 5 *(Wikipedia)* |  |
| Bluey's Happy Snaps | Gameloft | Unreal Engine 5 *(Wikipedia)* |  |
| Call of the Elder Gods | Out of the Blue Games | Unreal Engine 5 |  |
| Corsair Cove | Limbic Entertainment | Unreal Engine 5 |  |
| Dead by Daylight Windows | Behaviour Interactive Inc. | Unreal Engine 5, Unreal Engine 4 |  |
| Denshattack! | Undercoders | Unreal Engine 5 |  |
| Drop Duchy - Complete Edition | Sleepy Mill Studio | Unreal Engine 5 |  |
| Dune: Awakening | Funcom | Unreal Engine 5 |  |
| Eternal Strands | Yellow Brick Games | Unreal Engine 5 |  |
| FragPunk | Bad Guitar Studio | Unreal Engine 5 |  |
| Gears of War: E-Day | The Coalition | Unreal Engine 5 | reported to use Easy Anti-Cheat |
| GOALS | GOALS  | Unreal Engine 5 |  |
| Grounded 2 (Game Preview) | Obsidian Entertainment / Eidos Montréal | Unreal Engine 5 |  |
| Halo: Campaign Evolved - Standard Edition | Halo Studios | Unreal Engine 5, Halo Engine |  |
| Hell is Us | Rogue Factor | Unreal Engine 5 |  |
| Hellblade II: Senua's Saga | Ninja Theory | Unreal Engine 5 |  |
| High On Life 2 | Squanch Games, Inc. | Unreal Engine 5 |  |
| Keeper | Double Fine Productions | Unreal Engine 5 |  |
| Kernel Hearts | Ephemera Games | Unreal Engine 5 |  |
| Kiln | Double Fine Productions | Unreal Engine 5 *(Wikipedia)* |  |
| Lost Records: Bloom & Rage | DON'T NOD | Unreal Engine 5 |  |
| MechWarrior 5: Clans | Piranha Games Inc. | Unreal Engine 5 |  |
| Minecraft Dungeons II | Mojang Studios / ‪Double Eleven Limited | Unreal Engine 5 *(Wikipedia)* | **working** through Blueprint Loader: the rig as a Blueprint mod (Unreal 5.6.1, measured; its code protected, so no UE4SS) |
| Mistfall Hunter | Bellring Games | Unreal Engine 5 |  |
| Mixtape | Beethoven & Dinosaur | Unreal Engine 5 |  |
| MOTORSLICE | Regular Studio | Unreal Engine 5 |  |
| NINJA GAIDEN 2 Black | KOEI TECMO GAMES | Unreal Engine 5 |  |
| Palworld | Pocketpair | Unreal Engine 5 |  |
| Pax Dei | Mainframe Industries | Unreal Engine 5 |  |
| Pigeon Simulator | tinyBuild Riga | Unreal Engine 5 |  |
| Quarantine Zone: The Last Check | Brigada Games | Unreal Engine 5 |  |
| Relooted | Nyamakop | Unreal Engine 5 |  |
| REMATCH | Sloclap | Unreal Engine 5 | not supported: Easy Anti-Cheat on every launch (`start_protected_game.exe`, measured) |
| Remnant II - Standard Edition | Gunfire Games | Unreal Engine 5 |  |
| RoboCop: Rogue City | Teyon | Unreal Engine 5 |  |
| ROUTINE | Lunar Software | Unreal Engine 5 |  |
| RuneScape: Dragonwilds | Jagex: The RuneScape Company | Unreal Engine 5 |  |
| RV There Yet? | Nuggets Entertainment | Unreal Engine 5 |  |
| SMITE 2 (Game Preview) | Titan Forge Games | Unreal Engine 5 |  |
| Solarpunk | Cyberwave, Mi'pu'mi Games | Unreal Engine 5 |  |
| STARSEEKER: Astroneer Expeditions (Game Preview) | System Era Softworks | Unreal Engine 5 *(Wikipedia)* |  |
| Subnautica 2 (Game Preview) | Unknown Worlds Entertainment, Inc. | Unreal Engine 5 |  |
| Super Meat Boy 3D | Team Meat, Sluggerfly | Unreal Engine 5 |  |
| The Alters | 11 bit studios | Unreal Engine 5 |  |
| The Elder Scrolls IV: Oblivion Remastered | Bethesda Game Studios, Virtuos | Unreal Engine 5, Gamebryo (TES Engine) |  |
| THE FINALS | Embark Studios AB | Unreal Engine 5 |  |
| The First Descendant | NEXON Games Co., Ltd. | Unreal Engine 5 |  |
| The Outer Worlds 2 | Obsidian Entertainment | Unreal Engine 5 |  |
| The Talos Principle 2 | Croteam | Unreal Engine 5 |  |
| The Thaumaturge | Fool's Theory | Unreal Engine 5 |  |
| to a T | uvula | Unreal Engine 5 |  |
| Towerborne | Stoic  | Unreal Engine 5 |  |
| Valor Mortis | One More Level | Unreal Engine 5 |  |
| VALORANT PC | Riot Games | Unreal Engine 4, Unreal Engine 5 |  |
| VOID/BREAKER (Game Preview) | Stubby Games | Unreal Engine 5 |  |
| Wildgate | Moonshot Games | Unreal Engine 5 |  |
| WUCHANG: Fallen Feathers | Leenzee | Unreal Engine 5 |  |

### Unreal Engine 4 (50)

| Game | Developer | Engine | Tried |
|---|---|---|---|
| Aliens: Dark Descent (Windows) | Tindalos Interactive | Unreal Engine 4 |  |
| ARK: Ultimate Survivor Edition | Studio Wildcard | Unreal Engine 4 |  |
| ASTRONEER | System Era Softworks | Unreal Engine 4 |  |
| Bleeding Edge | Ninja Theory Ltd | Unreal Engine 4 |  |
| Chivalry 2 | Tripwire Interactive LLC | Unreal Engine 4 |  |
| Crackdown 3 | Sumo Digital Ltd / Elbow Rocket | Unreal Engine 4 |  |
| Crash Bandicoot 4: It's About Time (PC) | Toys for Bob | Unreal Engine 4 |  |
| Dead Island 2 (Windows) | Deep Silver Dambuster Studios | Unreal Engine 4 |  |
| Dead Static Drive | Reuben Games | Unreal Engine 4 |  |
| Deep Rock Galactic | Ghost Ship Games | Unreal Engine 4 |  |
| Escape the Backrooms | Fancy Games, Blackbird Interactive | Unreal Engine 4 |  |
| Gears 5 Game of the Year Edition | The Coalition | Unreal Engine 4 | not supported: Easy Anti-Cheat on every launch |
| Gears of War 4 | The Coalition | Unreal Engine 4 | not supported: old Store (UWP) package |
| Gears Tactics | Splash Damage / The Coalition | Unreal Engine 4 | not supported: ends itself when files are added |
| Genesis Noir: Windows Edition | Feral Cat Den | Unreal Engine 4 |  |
| Ghostwire: Tokyo | Tango Gameworks | Unreal Engine 4 |  |
| Goat Simulator 3: Windows Edition | Coffee Stain North AB | Unreal Engine 4 |  |
| Grounded | Obsidian Entertainment | Unreal Engine 4 |  |
| Halo: The Master Chief Collection | 343 Industries | Halo Engine, Saber3D Engine, Unreal Engine 4 |  |
| Hellblade: Senua's Sacrifice | Ninja Theory Ltd | Unreal Engine 4 |  |
| Hi-Fi RUSH | Tango Gameworks | Unreal Engine 4 |  |
| High On Life | Squanch Games, Inc. | Unreal Engine 4 |  |
| Hogwarts Legacy | Avalanche Software | Unreal Engine 4 |  |
| INDIKA | Odd Meter | Unreal Engine 4 |  |
| Little Nightmares Enhanced Edition | Tarsier Studios | Unreal Engine 4 |  |
| Luna Abyss | Kwalee Labs | Unreal Engine 4 |  |
| Manor Lords (Game Preview) | Slavic Magic | Unreal Engine 4 |  |
| MechWarrior 5: Mercenaries | Piranha Games Inc. | Unreal Engine 4 |  |
| Medieval Dynasty | Render Cube | Unreal Engine 4 |  |
| Minecraft Dungeons for Windows + Launcher | Mojang Studios/Double Eleven Limited | Unreal Engine 4 | not supported: protected exe, UE4SS cannot start (4.22.3, measured) |
| Mortal Kombat 1 | NetherRealm Studios  | Unreal Engine 4 |  |
| Police Simulator: Patrol Officers | Aesir Interactive | Unreal Engine 4 |  |
| Psychonauts 2 | Double Fine Productions | Unreal Engine 4 |  |
| Redfall | Arkane Studios | Unreal Engine 4 |  |
| Roboquest | RyseUp Studios | Unreal Engine 4 |  |
| Sea of Thieves: 2026 Edition | Rare Ltd | Unreal Engine 4 |  |
| South of Midnight | Compulsion Games | Unreal Engine 4 |  |
| Spyro Reignited Trilogy | Activision Publishing Inc. | Unreal Engine 4 |  |
| State of Decay 2: Juggernaut Edition | Undead Labs | Unreal Engine 4 |  |
| Tell Me Why: Chapters 1-3 | DONTNOD Entertainment  | Unreal Engine 4 |  |
| The Bard's Tale IV: Director's Cut | inXile entertainment, inc.  | Unreal Engine 4 |  |
| The Gunk | Image & Form Games | Unreal Engine 4 |  |
| The Outer Worlds: Spacer's Choice Edition | Obsidian Entertainment | Unreal Engine 4 |  |
| Tony Hawk's Pro Skater 3 + 4 - Cross-Gen Edition | Activision Publishing Inc. | Unreal Engine 4 |  |
| Trepang2 | Trepang Studios | Unreal Engine 4 |  |
| TRIALS of MANA | SQUARE ENIX | Unreal Engine 4 |  |
| Visions of Mana | SQUARE ENIX | Unreal Engine 4 |  |
| Voidtrain | HypeTrain Digital | Unreal Engine 4 |  |
| We Happy Few | Compulsion Games Inc | Unreal Engine 4 |  |
| Wuthering Waves | KURO GAMES | Unreal Engine 4 |  |

### Unreal Engine 3 (6)

| Game | Developer | Engine | Tried |
|---|---|---|---|
| Assassin's Creed Chronicles : China | Ubisoft Entertainment | Unreal Engine 3 |  |
| Assassin's Creed Chronicles – Russia | Climax Studio | Unreal Engine 3 |  |
| Batman: Arkham Knight | Rocksteady Studios | Unreal Engine 3 |  |
| Dishonored Definitive Edition (PC) | Arkane Studios | Unreal Engine 3 |  |
| Gears of War: Reloaded | The Coalition | Unreal Engine 3 | reported to use Easy Anti-Cheat |
| Gears of War: Ultimate Edition for Windows 10 | Microsoft Studios | Unreal Engine 3 | not supported: old Store (UWP) package |

## Unity (132)

`rig/unity`: Mono (`<Name>_Data/Managed`) through BepInEx 5, IL2CPP (`GameAssembly.dll`) through BepInEx 6; the built-in renderer, URP (`Urp.cs`) and HDRP (`Hdrp.cs`). Unity on D3D12 is not decoded yet.

| Game | Developer | Engine | Tried |
|---|---|---|---|
| 1000xRESIST | sunset visitor 斜陽過客 | Unity |  |
| 9 Kings (Game Preview) | Sad Socket | Unity |  |
| Aerial_Knight's DropShot | Aerial_Knight | Unity |  |
| Albion Online | Sandbox Interactive GmbH | Unity |  |
| Among Us | Innersloth | Unity, Unity, Unity |  |
| Aniimo | Pawprint Studio | Unity |  |
| ANNO : Mutationem | ThinkingStars | Unity |  |
| As Dusk Falls | INTERIOR NIGHT | Unity |  |
| BALL x PIT | Kenny Sun | Unity |  |
| Barbie Horse Trails | PHL Collective | Unity |  |
| BATTLETECH | Harebrained Schemes | Unity, Unity |  |
| Battletoads | Dlala Studios | Unity |  |
| Ben 10: Power Trip | PHL Collective | Unity |  |
| Beyond These Stars | Balancing Monkey Games | Unity |  |
| Blue Prince | Dogubomb | Unity |  |
| Bluey: The Videogame | Artax Games and Casual Brothers Ltd. | Unity |  |
| Bounty Star | DINOGOD | Unity |  |
| Buckshot Roulette | Mike Klubnika | Godot, Unity |  |
| Car Mechanic Simulator 2021 | Red Dot Games | Unity |  |
| Cities: Skylines - Windows 10 Edition | Tantalus Media | Unity |  |
| Cities: Skylines II - PC Edition | Iceflake Studios, Colossal Order | Unity | in progress (Unity HDRP: depth through HDRP) |
| Clone Drone in the Danger Zone | Doborog Games | Unity, Unity |  |
| CloverPit | Panik Arcade | Unity |  |
| Construction Simulator | weltenbauer. Software Entwicklung GmbH | Unity |  |
| Cooking Simulator Windows | Big Cheese Studio | Unity |  |
| Crashout Crew | Aggro Crab | Unity |  |
| Date Everything! | Sassy Chap Games and Team17 | Unity |  |
| Death Howl | The Outer Zone | Unity |  |
| Deep Dish Dungeon | Behold Studios | Unity |  |
| Descenders | RageSquid | Unity |  |
| Descenders Next (Game Preview) | RageSquid | Unity |  |
| Disco Elysium - The Final Cut | ZA/UM | Unity |  |
| Disney Dreamlight Valley | Gameloft SE | Unity |  |
| DREDGE | Black Salt Games | Unity |  |
| Easy Delivery Co. | Sam C | Unity |  |
| Egging On | Egobounds | Unity |  |
| Escape Simulator | Pine Studio | Unity |  |
| FINAL FANTASY | SQUARE ENIX | Unity |  |
| FINAL FANTASY II | SQUARE ENIX | Unity |  |
| FINAL FANTASY III | SQUARE ENIX | Unity |  |
| FINAL FANTASY IV | SQUARE ENIX | Unity |  |
| Football Manager 26 | Sports Interactive | Unity |  |
| Football Manager 26 Console | Sports Interactive | Unity |  |
| For The King II | IronOak Games Inc. | Unity |  |
| Forever Ago | Third Shift | Unity |  |
| Frog Sqwad | Panic Stations | Unity |  |
| Gang Beasts | Boneloaf, Rocket Science | Unity | working (Unity IL2CPP rig) |
| Genshin Impact | Shanghai miHoYo TianMing Technology Co., Ltd. | Unity |  |
| Great God Grove | LimboLane | Unity |  |
| Herdling | Okomotive | Unity |  |
| Heroes of Might and Magic: Olden Era (Game Preview) | Unfrozen | Unity |  |
| Hollow Knight: Silksong | Team Cherry | Unity |  |
| Hollow Knight: Voidheart Edition | Team Cherry | Unity, Unity, Unity, Unity |  |
| House Flipper | Frozen District | Unity, Unity |  |
| Human Fall Flat | No Brakes Games | Unity *(Wikipedia)* |  |
| Hungry Shark World | Future Games of London, Ubisoft Da Nang | Unity |  |
| inKONBINI: One Store. Many Stories | Nagai Industries | Unity |  |
| Inscryption | Daniel Mullins Games | Unity |  |
| INSIDE | Playdead | Unity, Unity |  |
| Kingdom Two Crowns | Fury Studios, Coatsink | Unity *(Wikipedia)* |  |
| Legends of Runeterra | Riot Games | Unity |  |
| Little Kitty, Big City | Double Dagger Studio | Unity |  |
| Microsoft Bubble | Xbox Game Studios | Unity |  |
| Microsoft Jewel | Microsoft Studios | Unity |  |
| Minishoot' Adventures | SoulGame Studio, Headbang Club | Unity *(Wikipedia)* |  |
| Monster Sanctuary (Windows Version) | Moi Rai Games | Unity |  |
| Moonlighter | Digital Sun | Unity, Unity |  |
| Moonlighter 2: The Endless Vault |  Digital Sun | Unity |  |
| Moving Out 2 | SMG Studio & DEVM Games | Unity |  |
| My Friend Peppa Pig | Petoons Studio Ltd | Unity |  |
| NARAKA: BLADEPOINT | NetEase | Unity |  |
| New Super Lucky's Tale | Playful Studios | Unity |  |
| Nickelodeon All-Star Brawl 2 | Fair Play Labs | Unity |  |
| Nine Sols | Red Candle Games | Unity |  |
| NINJA GAIDEN: Ragebound | The Game Kitchen | Unity |  |
| OddBallers | Ubisoft | Unity |  |
| Ori and the Blind Forest: Definitive Edition | Moon Studios | Unity |  |
| Ori and the Will of the Wisps | Moon Studios | Unity |  |
| Outbound | Square Glade Games | Unity |  |
| Overcooked! 2 | Ghost Town Games and Team17 Digital Ltd | Unity, Unity |  |
| Overthrown | Brimstone Games | Unity |  |
| Pentiment | Obsidian Entertainment | Unity |  |
| Phoenix Point | Snapshot Games Inc | Unity |  |
| Pillars of Eternity 2: Deadfire - Ultimate Edition (PC) | Obsidian Entertainment | Unity *(Wikipedia)* |  |
| Pillars of Eternity: Definitive Edition | Obsidian Entertainment | Unity, Unity |  |
| Planet of Lana II | Wishfully | Unity |  |
| PlateUp! | It's happening | Unity |  |
| PowerWash Simulator 2 | FuturLab Limited | Unity |  |
| Prince of Persia The Lost Crown | Ubisoft | Unity *(Wikipedia)* |  |
| ReCore: Definitive Edition | Comcept Inc., Armature Studio LLC | Unity |  |
| Relic Hunters Legend | Rogue Snail | Unity |  |
| REPLACED | Sad Cat Studios | Unity |  |
| Roadside Research (Game Preview) | Cybernetic Walrus | Unity |  |
| Shadowrun Returns PC | Harebrained Schemes, Codeglue | Unity |  |
| Shadowrun: Dragonfall - Director's Cut PC | Harebrained Schemes, Codeglue | Unity |  |
| Shadowrun: Hong Kong - Extended Edition PC | Harebrained Schemes, Codeglue | Unity |  |
| Shape of Dreams | Lizard Smoothie | Unity |  |
| Shift At Midnight | Bun Muen | Unity |  |
| Sledding Game | The Sledding Corporation | Unity |  |
| SpeedRunners 2: King of Speed | tinyBuild Games | Unity |  |
| SpongeBob SquarePants: The Patrick Star Game | PHL Collective | Unity |  |
| Star Trucker | Monster and Monster | Unity |  |
| Starsand Island | Seed Sparkle Lab | Unity |  |
| Super Lucky's Tale | Playful Corp. | Unity |  |
| SUPERHOT: MIND CONTROL DELETE | SUPERHOT Team | Unity |  |
| Symphonia | Sunny Peak | Unity |  |
| TCG Card Shop Simulator | OPNeon Games Sdn Bhd | Unity |  |
| Tempopo | Witch Beam | Unity |  |
| The Bard's Tale Trilogy | Krome Studios | Unity |  |
| The Long Dark | Hinterland Studio Inc | Unity, Unity, Unity, Unity |  |
| The Planet Crafter | Miju Games | Unity |  |
| The Rogue Prince of Persia | Ubisoft | Unity |  |
| Torment: Tides of Numenera | inXile Entertainment | Unity |  |
| Total Chaos | Trigger Happy Interactive, Black Sheep Studio | Unity |  |
| Totally Accurate Battle Simulator | Landfall and 24 Bit Games | Unity | working (Unity rig, follows the view) |
| Totally Reliable Delivery Service | We’re Five Games | Unity |  |
| Trailmakers | Flashbulb Games | Unity |  |
| Transference | Ubisoft Entertainment | Unity |  |
| TUNIC | ISOMETRICORP Games Ltd | Unity |  |
| Ultimate Sheep Raccoon | Clever Endeavour Games | Unity |  |
| Undisputed | Steel City Interactive  | Unity | **working** in a fight: Unity 2022.3 IL2CPP + HDRP (measured), the IL2CPP rig with HDRP's depth; the ring and both boxers |
| UNO | Ubisoft | Unity |  |
| Vampire Crawlers: The Turbo Wildcard from Vampire Survivors | Poncle Limited, Nosebleed Interactive | Unity *(Wikipedia)* |  |
| Vampire Survivors | Poncle | Phaser, Electron, Unity, Unity |  |
| Vapor World: Over The Mind (Game Preview) | ALIVE Inc. | Unity |  |
| Warhammer 40,000: Rogue Trader | Owlcat Games | Unity |  |
| Wasteland 2: Director's Cut | InXile Entertainment | Unity, Unity |  |
| Wasteland 3 (PC) | inXile entertainment, Inc. | Unity |  |
| Wasteland Remastered | inXile entertainment | Unity |  |
| Wheel World | Messhof | Unity |  |
| Wobbly Life | RubberBandGames | Unity |  |
| Zenless Zone Zero | Shanghai JiaoZhai Technology Co.,Ltd. | Unity |  |

## Other engines (173)

Not the rig's Unreal or Unity modules: each would need route 3, its renderer hooked (`docs/GAMES.md`).

- **AnvilNext**: Assassin's Creed Freedom Cry - Standalone; Assassin's Creed III Remastered; Assassin's Creed IV Black Flag - Gold Edition; Assassin's Creed Origins; Assassin's Creed Rogue; Assassin's Creed Syndicate; Assassin's Creed Unity; For Honor Standard; Riders Republic Standard Edition; STEEP; Tom Clancy's Ghost Recon Breakpoint; Tom Clancy's Ghost Recon Wildlands; Tom Clancy's Rainbow Six Extraction; Tom Clancy's Rainbow Six Siege - Elite Edition
- **IW engine**: Call of Duty: Black Ops 6 - Standard Edition (Windows); Call of Duty: Black Ops 7 - Cross-Gen Bundle; Call of Duty: Black Ops Cold War (Windows); Call of Duty: Modern Warfare - Digital Standard Edition (Windows); Call of Duty: Modern Warfare II - Standard Edition (Windows); Call of Duty: Modern Warfare III - Standard Edition (Windows); Call of Duty: Vanguard - Standard Edition; Call of Duty: Warzone; Call of Duty: WWII (Windows)
- **Snowdrop**: Avatar: Frontiers of Pandora; Rabbids: Party of Legends; South Park: The Fractured but Whole; Star Wars Outlaws; Starlink: Battle for Atlas; The Settlers: New Allies; Tom Clancy's The Division; Tom Clancy's The Division 2
- **Dunia 2**: FAR CRY 3; FAR CRY 3 BLOOD DRAGON; FAR CRY 4; Far Cry 5; Far Cry 6; Far Cry New Dawn; FAR CRY PRIMAL
- **Apex Engine**: Call of the Wild: The Angler; Generation Zero; RAGE 2 (PC); theHunter: Call of the Wild
- **Cobra**: Disneyland Adventures; Jurassic World Evolution 3; Planet Coaster 2; Zoo Tycoon: Ultimate Animal Collection
- **Gamebryo (TES Engine)**: Fallout 3: Game of the Year Edition; Fallout: New Vegas Ultimate Edition; The Elder Scrolls III: Morrowind Game of the Year Edition (PC); The Elder Scrolls IV: Oblivion Game of the Year Edition (PC) – 2009
- **Godot**: Brotato; Dome Keeper; Fogpiercer; Halls of Torment
- **RE Engine**: RESIDENT EVIL 2 *(Wikipedia)*; RESIDENT EVIL 3 for Windows; RESIDENT EVIL 7 biohazard *(Wikipedia)*; Resident Evil Village
- **Swarm Engine**: RoadCraft; SnowRunner - Windows; Warhammer 40,000: Space Marine 2; World War Z: Aftermath
- **Ubisoft Anvil**: Assassin's Creed Mirage; Assassin's Creed Valhalla; Immortals Fenyx Rising; Skull and Bones
- **Anvil**: Assassin's Creed 2; Assassin's Creed Brotherhood; Assassin's Creed Revelations
- **Creation Engine**: Fallout 4; Fallout 76 (PC); The Elder Scrolls V: Skyrim Special Edition (PC)
- **Dagor Engine**: Enlisted; Enlisted Xbox One; War Thunder
- **Dragon Engine**: Like a Dragon: Infinite Wealth; Like a Dragon: Pirate Yakuza in Hawaii; Virtua Fighter 5 R.E.V.O. World Stage
- **ForzaTech**: Forza Horizon 5 Standard Edition; Forza Horizon 6 Standard Edition; Forza Motorsport Standard Edition
- **id Tech 4**: DOOM 3; Prey; Quake 4
- **Kex Engine**: DOOM + DOOM II; DOOM 64; Heretic + Hexen
- **Northlight**: Control Ultimate Edition; FBC: Firebreak; Quantum Break
- **UbiArt Framework**: Child of Light; RAYMAN LEGENDS; Valiant Hearts: The Great War
- **Void Engine**: DEATHLOOP; Dishonored 2; Dishonored: Death of the Outsider (PC)
- **Adobe AIR**: Brawlhalla; Brawlhalla
- **BANG!**: Age of Empires III: Definitive Edition; Age of Mythology: Retold
- **Fallout Engine**: Fallout; Fallout 2
- **GameMaker**: Shelldiver; Well Dweller
- **Genie**: Age of Empires Definitive Edition; Age of Empires II: Definitive Edition
- **Hextech Engine**: League of Legends; Teamfight Tactics
- **id Tech 3**: Quake III Arena; Return to Castle Wolfenstein
- **id Tech 5**: The Evil Within (PC); Wolfenstein: The New Order (PC)
- **Id Tech 6**: Wolfenstein II: Standard Edition; Wolfenstein: Youngblood (PC)
- **Phoenix (Ensemble Studios)**: Halo Wars 2: Standard Edition; Halo Wars: Definitive Edition
- **SCUMM**: Day of the Tentacle Remastered; Full Throttle Remastered
- **XnGine**: An Elder Scrolls Legend: Battlespire; The Elder Scrolls Adventures: Redguard
- **Zouna**: Microsoft Flight Simulator 2024 - Standard Edition; Resonance: A Plague Tale Legacy
- **Asura**: Zombie Army 4: Dead War
- **Autodesk Stingray Engine**: Warhammer 40,000: Darktide
- **Big Huge Engine**: Rise of Nations: Extended Edition
- **Bigworld**: World of Warships
- **BlitzTech**: Fuzion Frenzy
- **Creation Engine 2**: Starfield
- **CryEngine (4th generation)**: Kingdom Come: Deliverance
- **CryEngine V**: Kingdom Come: Deliverance II
- **Crystal Engine**: Lara Croft and the Temple of Osiris
- **Decima**: DEATH STRANDING DIRECTOR'S CUT
- **Disrupt**: Watch Dogs: Legion
- **Enfusion, Real Virtuality**: DayZ
- **Essence Engine**: Age of Empires IV: Anniversary Edition
- **FNA**: MARVEL Cosmic Invasion
- **GameBox**: Trackmania Turbo
- **GFD**: Persona 5 Royal
- **GIANTS Engine 10**: Farming Simulator 25 (PC)
- **Heaps**: Wartales
- **HeroEngine**: The Elder Scrolls Online: Standard Edition
- **id Tech 2**: Quake II
- **Id Tech 5**: Wolfenstein: The Old Blood (PC)
- **id Tech 7**: DOOM Eternal Standard Edition (PC)
- **id Tech 8**: DOOM: The Dark Ages
- **In-House Engine**: Cricket 26 - The Official Game of the Ashes *(Wikipedia)*
- **Insomniac Engine**: Sunset Overdrive
- **Jet Engine**: Asphalt Legends
- **Katana Engine**: Wo Long: Fallen Dynasty
- **LWJGL, Java**: Minecraft: Java & Bedrock Edition for PC
- **LÖVE**: Balatro
- **Messiah Engine**: Where Winds Meet
- **Moai**: Broken Age
- **MonoGame**: Stardew Valley
- **MonoGame, The Forge**: Hades
- **Motor**: Indiana Jones and the Great Circle
- **NeoX**: Once Human
- **Nitrous Engine**: Ara History Untold: Anniversary Edition *(Wikipedia)*
- **nTT**: LEGO Star Wars: The Skywalker Saga
- **Onrush engine**: DIRT 5
- **Onyx**: South Park: The Stick of Truth
- **Phoenix (Micro Forté)**: Fallout Tactics
- **Platinum Engine**: NINJA GAIDEN 4 Standard Edition
- **Quake engine**: Quake
- **RD3D**: Anno 1800
- **ROMU Engine**: Wreckfest
- **Slayer**: Killer Instinct: Anniversary Edition
- **Slipspace Engine**: Halo Infinite
- **STEM Engine**: The Evil Within 2 (PC)
- **The Forge**: Hades II
- **Vicarious Visions Alchemy**: Crash Bandicoot N. Sane Trilogy
- **Wolfenstein 3D engine**: Wolfenstein 3D
- **XNA**: Absolum

## Engine not known (87)

No PCGamingWiki page matched by name, or the page names no engine, and no Wikipedia infobox does either.

33 Immortals; Arx Fatalis; Ascend to ZERO; Baldur's Gate and Baldur's Gate II: Enhanced Editions; Beatdown City Survivors; Black Jacket; BlazBlue Entropy Effect X; BLiNX: The Time Sweeper; Blizzard Arcade Collection PC; Blood Dungeon; Brews & Bastards; Chained Beasts; Conker: Live and Reloaded; Crimson Skies: High Road to Revenge; Diablo; Diablo II: Resurrected PC; Diablo III: Reaper of Souls – Ultimate Evil Edition PC; Diablo IV PC; Dice A Million; Dodo Duckie; DRAGON BALL XENOVERSE 2; DreamWorks Gabby's Dollhouse: Ready to Party; Echo Generation 2; Echo Weaver; FINAL FANTASY V; FINAL FANTASY VI; Firefighting Simulator - The Squad; FixForce; Grim Fandango Remastered; Halo: Spartan Assault; Halo: Spartan Strike; Heroes of Might and Magic 3 - Complete Edition; Heroes of Might and Magic II - Gold Edition; Heroes of the Storm; Junkster; Kabuto Park; KILL IT WITH FIRE! 2; Legend of Mana; LIMBO; Mad King Redemption; MAVRIX by Matt Jones; Microsoft Flight Simulator (2020) Standard 40th Anniversary Edition; Microsoft Mahjong; Microsoft Solitaire Collection; Minecraft Legends for Windows + Launcher; Minecraft Preview for Windows; MIO: Memories in Orbit; MONOPOLY Madness; Monster Train 2; Monsters are Coming!; Neon Abyss 2; NEW MONOPOLY; Ni no Kuni II: Revenant Kingdom PRINCE'S EDITION; Ni no Kuni Wrath of the White Witch Remastered; No Man's Sky; Overwatch; Overwatch PC; PBA Pro Bowling 2026; Psychonauts (Windows 10); Retro Classics; Rush: A DisneyPixar Adventure; Sandustry (Game Preview); StarCraft II Campaign Collection; StarCraft Remastered; Supermarket Simulator; Tamashika; Tears of Metal (Game Preview); TerraTech Legion; The Bard's Tale ARPG : Remastered and Resnarkled; The Crew 2; The Crew Motorfest; The Royal Writ; Tiny Bookshop; Trials Fusion; Trials Rising; Warcraft I: Remastered; Warcraft II: Remastered; Warcraft III: Reforged; Watch Dogs2; WATCH_DOGS; Winds of Arcana: Ruination; Winter Burrow; World of Tanks Modern Armor; World of Tanks: HEAT; World of Warships: Legends; Young Suns; ZOMBI
