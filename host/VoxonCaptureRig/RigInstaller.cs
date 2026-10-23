using System.Diagnostics;
using System.IO.Compression;
using System.Text;
using System.Text.Json;

namespace VoxonCaptureRig;

/// <summary>Puts the camera rig into a game and takes it out again, with nothing for the player to edit. Ported from
/// VoxelSteamTap's tools/install-ue-tap.ps1, whose lessons it keeps:
/// <list type="bullet">
/// <item>UE4SS loads as dwmapi.dll, BepInEx as winhttp.dll and ReShade as dxgi.dll, so the proxies never collide.</item>
/// <item>UE4SS's object-array cache crashed The Ascent twice, its bundled mods are not ours to run, and hot reload
///   deadlocked on the rig's loop: all off, when UE4SS is ours to configure.</item>
/// <item>mods.txt is UTF-8 with a BOM and order matters: the rig goes in before the keybinds line, which says
///   nothing may follow it.</item>
/// <item>The engine-version override is applied only to the games that need it (Wukong); a wrong version is worse
///   than none.</item>
/// </list>
/// EVERYTHING it adds or changes is recorded in <see cref="RecordName"/> in the game's folder, so Remove takes out
/// exactly that and puts back what it replaced: a UE4SS or ReShade the player already had stays, settings and all.
/// </summary>
internal static class RigInstaller
{
    public const string RecordName = "VoxonCaptureRig.install.json";
    private const string AddonName = "voxon_capture_ue.addon64";
    private const string AddonLog = "voxon_capture_ue.log";        // the add-on writes it beside the game's exe
    private const string CaptureIni = "voxon_capture_ue.ini";

    private sealed class Record
    {
        public string Game { get; set; } = "";
        public DateTime When { get; set; }
        public List<string> Files { get; set; } = [];          // ours: deleted on Remove
        public bool Ue4ssOurs { get; set; }
        public bool BepInExOurs { get; set; }
        public bool ReShadeOurs { get; set; }
        public bool CetOurs { get; set; }                       // REDENGINE4: Cyber Engine Tweaks, RED4ext, Codeware
        public bool Red4extOurs { get; set; }
        public bool CodewareOurs { get; set; }
        public bool ReShadeIniOurs { get; set; }
        public string? CaptureIniBackup { get; set; }           // an existing voxon_capture_ue.ini, put back on Remove
        // Lines changed in the player's own files, kept WHOLE and put back verbatim (UE4SS ships "MajorVersion = " but
        // "MinorVersion =", and a value alone loses the difference). Null with the flag set: there was no such line,
        // and Remove takes ours out.
        public bool AddonPathSet { get; set; }
        public string? OldAddonPath { get; set; }
        public bool EngineSet { get; set; }
        public string? OldEngineMajor { get; set; }
        public string? OldEngineMinor { get; set; }
    }

    public enum State { NotFound, NotInstalled, Installed, Foreign }

    private const string PluginFolder = "VoxonCaptureRig";        // BepInEx\plugins\<this>, the Unity rig

    private static bool IsUnity(GameDef g) => g.Engine == "unity";
    // A Unity game compiled to native code (IL2CPP: GameAssembly.dll, no Mono) takes BepInEx 6 and the rig's IL2CPP
    // build (rig/unity/il2cpp); a Mono one BepInEx 5 and the rig's own.
    private static bool IsIl2Cpp(string exeDir) => File.Exists(Path.Combine(exeDir, "GameAssembly.dll"));
    private static string BepInExCore(string exeDir) =>
        Path.Combine(exeDir, "BepInEx", "core", IsIl2Cpp(exeDir) ? "BepInEx.Unity.IL2CPP.dll" : "BepInEx.dll");
    private static bool IsRed3(GameDef g) => g.Engine == "redengine3";
    private static bool IsRed4(GameDef g) => g.Engine == "redengine4";
    // No loader and no script: the add-on itself has the engine draw the faces (Frostbite, The Sims 4, CryEngine 2, EGO),
    // or a Blueprint Loader mod brings the cameras in as game content (BLUEPRINT, below).
    private static bool AddonOnly(GameDef g) => g.Engine is "frostbite" or "sims4" or "cryengine2" or "ego" or "blueprint";

    // BLUEPRINT (Minecraft Dungeons II): the cameras are a Blueprint Loader mod of ours (tools/mcdungeons2-mod), its pak
    // files in the game's ~mods folder; Blueprint Loader itself is the player's to get (Nexus Mods), and must be there
    // first. The app writes the mod's settings beside the game (BlueprintBridge), as the control file is to other rigs.
    private static bool IsBlueprint(GameDef g) => g.Engine == "blueprint";
    private const string BlueprintModFolder = "VoxonRig";
    private static readonly string[] BlueprintLoaderPak = ["BlueprintLoader", "BlueprintLoader_P.pak"];
    private static string ModsDir(GameDef g, string exeDir) =>
        Path.GetFullPath(Path.Combine(exeDir, g.ModsDir ?? Path.Combine("..", "..", "Content", "Paks", "~mods")));
    private static string BlueprintMod(GameDef g, string exeDir) => Path.Combine(ModsDir(g, exeDir), BlueprintModFolder);

    private const string Red3Mod = "modVoxonRig";                     // the Witcher 3 script mod's folder under mods\

    /// <summary>REDENGINE3: the game's own folder - the exe is in bin\x64_dx12 - where content\ and mods\ are. REDENGINE4
    /// the same, from bin\x64.</summary>
    private static string Red3Root(string exeDir) => Path.GetFullPath(Path.Combine(exeDir, "..", ".."));

    /// <summary>REDENGINE4: Cyber Engine Tweaks' folder beside the exe, and the rig's mod in it.</summary>
    private static string CetDir(string exeDir) => Path.Combine(exeDir, "plugins", "cyber_engine_tweaks");
    private static string Red4Mod(string exeDir) => Path.Combine(CetDir(exeDir), "mods", "voxon_rig");

    /// <summary>Installed: this app put the rig here. Foreign: a rig is present that this app did not install (an
    /// older install, by hand or by script); Install takes it over and Remove then restores it.</summary>
    public static State StateOf(GameDef g, string? exeDir)
    {
        if (exeDir == null || !Directory.Exists(exeDir)) return State.NotFound;
        if (File.Exists(Path.Combine(exeDir, RecordName))) return State.Installed;
        bool rig = IsUnity(g) ? Directory.Exists(Path.Combine(exeDir, "BepInEx", "plugins", PluginFolder))
                 : IsRed3(g) ? Directory.Exists(Path.Combine(Red3Root(exeDir), "mods", Red3Mod))
                 : IsRed4(g) ? Directory.Exists(Red4Mod(exeDir))
                 : IsBlueprint(g) ? Directory.Exists(BlueprintMod(g, exeDir))
                 : AddonOnly(g) ? File.Exists(Path.Combine(exeDir, AddonName))
                 : Directory.Exists(Path.Combine(exeDir, "Mods", "VoxonTapUE"));
        if (rig || File.Exists(Path.Combine(exeDir, CaptureIni))) return State.Foreign;
        return State.NotInstalled;
    }

    public static bool IsRunning(GameDef g, string exeDir)
    {
        string? exe = Games.GameExe(g, exeDir);
        if (exe == null) return false;
        return Process.GetProcessesByName(Path.GetFileNameWithoutExtension(exe)).Length > 0;
    }

    /// <summary>The rig into a game: its engine's loader and the rig (UE4SS and the Lua rig for Unreal, BepInEx and
    /// the plugin for Unity), then what every engine shares - ReShade, the add-on, and the game's settings.</summary>
    public static void Install(GameDef g, string exeDir)
    {
        // EVERY REFUSAL BEFORE ANY CHANGE: a refusal halfway would leave a half-installed rig with no record to undo.
        if (IsRunning(g, exeDir)) throw new InvalidOperationException($"{g.Name} is running. Close it first.");
        var rec = LoadRecord(exeDir) ?? new Record { Game = g.Id };
        rec.When = DateTime.Now;
        RefuseForeign(exeDir, "dxgi.dll", "ReShade", rec.ReShadeOurs);
        if (IsUnity(g)) RefuseForeign(exeDir, "winhttp.dll", "BepInEx", rec.BepInExOurs);
        if (IsUnity(g) && !rec.BepInExOurs && Directory.Exists(Path.Combine(exeDir, "BepInEx", "core")) && !File.Exists(BepInExCore(exeDir)))
            throw new InvalidOperationException(IsIl2Cpp(exeDir)
                ? "This game already has a BepInEx for Mono games, and it is an IL2CPP game: the rig needs BepInEx 6 for IL2CPP. Remove the other BepInEx first."
                : "This game already has a BepInEx that is not BepInEx 5, which the rig needs. Remove the other BepInEx first.");
        if (IsRed3(g)) RefuseRed3(exeDir);
        if (IsRed4(g)) RefuseRed4(rec, exeDir);
        if (IsBlueprint(g) && !File.Exists(Path.Combine([ModsDir(g, exeDir), .. BlueprintLoaderPak])))
            throw new InvalidOperationException($"{g.Name} needs Blueprint Loader, which loads the rig's mod: get it from Nexus Mods "
                + $"({g.Name}, Blueprint Loader), put its BlueprintLoader folder in {ModsDir(g, exeDir)}, then Install rig again.");
        foreach (string f in GameFiles(g))
        {
            string name = Path.GetFileName(f);
            if (File.Exists(Path.Combine(exeDir, name)) && !rec.Files.Contains(name))
                throw new InvalidOperationException($"This game already has its own {name}. The rig brings one for {g.Name}; move the other aside first.");
        }
        Log.Write($"install: {g.Name} -> {exeDir}");

        if (IsUnity(g)) InstallUnityRig(rec, exeDir);
        else if (IsRed3(g)) InstallRed3Rig(rec, exeDir);
        else if (IsRed4(g)) InstallRed4Rig(rec, exeDir);
        else if (IsBlueprint(g)) InstallBlueprintMod(g, rec, exeDir);
        else if (AddonOnly(g)) Log.Write("install: no loader or script for this game: the add-on adds the faces itself");
        else InstallUnrealRig(g, rec, exeDir);
        InstallReShade(rec, exeDir);
        InstallSettings(g, rec, exeDir);
        if (IsBlueprint(g))
        {
            // The mod's settings, until the app writes them for the display it finds (a VX2-XL's size meanwhile).
            BlueprintBridge.Write(RigSettings.For(g), 4f, 2f, exeDir);
            Own(rec, exeDir, BlueprintBridge.PathFor(exeDir));
        }

        SaveRecord(exeDir, rec);
        Log.Write($"install: done. Start {g.Name}; the rig arms itself and F7 stops and starts it.");
    }

    // ── Unreal: UE4SS, and the rig as its Lua mod ──────────────────────────────────────────────────

    private static void InstallUnrealRig(GameDef g, Record rec, string exeDir)
    {
        string ue4ssDll = Path.Combine(exeDir, "UE4SS.dll");
        if (!File.Exists(ue4ssDll))
        {
            using var zip = ZipFile.OpenRead(Path.Combine(AppPaths.Redist, g.Ue4ss));
            foreach (var e in zip.Entries)
            {
                // A newer UE4SS keeps its files in a ue4ss folder. Here they go beside the exe, the layout it still
                // reads when its dll is there, and the one every other game's UE4SS uses.
                string name = e.FullName.StartsWith("ue4ss/", StringComparison.Ordinal) ? e.FullName[6..] : e.FullName;
                bool wanted = name is "dwmapi.dll" or "UE4SS.dll" or "UE4SS-settings.ini" || name.StartsWith("Mods/", StringComparison.Ordinal);
                if (!wanted || name.EndsWith('/')) continue;
                Own(rec, exeDir, Extract(e, exeDir, name));
            }
            rec.Ue4ssOurs = true;
            Log.Write($"install: UE4SS {g.Ue4ss.Replace("UE4SS_v", "").Replace(".zip", "")} added (dwmapi.dll)");
        }
        else Log.Write("install: UE4SS already here - kept, with its settings");

        string settings = Path.Combine(exeDir, "UE4SS-settings.ini");
        string modsTxt = Path.Combine(exeDir, "Mods", "mods.txt");
        if (rec.Ue4ssOurs)
        {
            Rewrite(settings, t => SetLine(SetLine(t, "bUseUObjectArrayCache", "false"), "EnableHotReloadSystem", "0"));
            string[] bundled = ["CheatManagerEnablerMod", "ActorDumperMod", "ConsoleCommandsMod", "ConsoleEnablerMod",
                                "SplitScreenMod", "LineTraceMod", "BPModLoaderMod", "BPML_GenericFunctions", "jsbLuaProfilerMod"];
            Rewrite(modsTxt, m =>
            {
                foreach (string mod in bundled)
                    m = System.Text.RegularExpressions.Regex.Replace(m, $@"(?m)^{mod}[ \t]*:[ \t]*1", $"{mod} : 0");
                return m;
            });
            string modsJson = Path.Combine(exeDir, "Mods", "mods.json");        // a newer UE4SS lists them here as well
            if (File.Exists(modsJson))
                Rewrite(modsJson, m =>
                {
                    foreach (string mod in bundled)
                        m = System.Text.RegularExpressions.Regex.Replace(m, $@"(""mod_name""\s*:\s*""{mod}"",\s*""mod_enabled""\s*:\s*)true", "$1false");
                    return m;
                });
            Log.Write("install: UE4SS object cache off, hot reload off, its bundled mods off");
            if (g.Ue4ssHooks is string[] on)
            {
                // A game whose engine some of UE4SS's hooks do not fit (Mortal Kombat 1 crashes with them): only the
                // ones named stay on. The rig itself needs EngineTick, for its game-thread calls.
                Rewrite(settings, t => System.Text.RegularExpressions.Regex.Replace(t, @"(?m)^Hook(\w+)([ \t]*)=[ \t]*\d",
                    h => $"Hook{h.Groups[1].Value}{h.Groups[2].Value}= {(on.Contains(h.Groups[1].Value) ? 1 : 0)}"));
                Log.Write($"install: UE4SS hooks on: {string.Join(", ", on)} only");
            }
        }
        if (g.EngineMajor is int major && g.EngineMinor is int minor && File.Exists(settings))
        {
            Rewrite(settings, t =>
            {
                if (!rec.EngineSet)
                {
                    rec.EngineSet = true;
                    rec.OldEngineMajor = RawLine(t, "MajorVersion");
                    rec.OldEngineMinor = RawLine(t, "MinorVersion");
                }
                return SetLine(SetLine(t, "MajorVersion", major.ToString()), "MinorVersion", minor.ToString());
            });
            Log.Write($"install: UE4SS engine version set to {major}.{minor} (this game's scan needs it)");
        }

        string modDir = Path.Combine(exeDir, "Mods", "VoxonTapUE");
        string scripts = Path.Combine(modDir, "Scripts");
        Directory.CreateDirectory(scripts);
        foreach (string lua in Directory.EnumerateFiles(AppPaths.RigScripts, "*.lua"))
            Own(rec, exeDir, CopyInto(lua, scripts));
        File.WriteAllText(Path.Combine(modDir, "enabled.txt"), "");
        Own(rec, exeDir, Path.Combine(modDir, "enabled.txt"));
        if (File.Exists(modsTxt))
            Rewrite(modsTxt, m =>
            {
                if (m.Contains("VoxonTapUE", StringComparison.Ordinal)) return m;
                int at = m.IndexOf("; Built-in keybinds", StringComparison.Ordinal);
                return at >= 0 ? m.Insert(at, "VoxonTapUE : 1\r\n\r\n") : m.TrimEnd() + "\r\nVoxonTapUE : 1\r\n";
            });
        Log.Write("install: camera rig scripts in Mods\\VoxonTapUE");

        // What this game's UE4SS needs beside it (Mortal Kombat 1: a VTableLayout.ini), from rig/unreal/games/<id>/.
        if (rec.Ue4ssOurs)
            foreach (string f in GameFiles(g))
            {
                Own(rec, exeDir, CopyInto(f, exeDir));
                Log.Write($"install: {Path.GetFileName(f)} for this game's UE4SS");
            }
    }

    /// <summary>The files a game's UE4SS needs beside it: rig/unreal/games/&lt;id&gt;/, none for most games.</summary>
    private static IEnumerable<string> GameFiles(GameDef g)
    {
        string dir = Path.Combine(AppPaths.RigScripts, "games", g.Id);
        return !IsUnity(g) && !AddonOnly(g) && Directory.Exists(dir) ? Directory.EnumerateFiles(dir) : [];
    }

    // ── Blueprint: the rig's own Blueprint Loader mod, into the game's ~mods folder ──────────────────────────────────

    private static void InstallBlueprintMod(GameDef g, Record rec, string exeDir)
    {
        string mod = BlueprintMod(g, exeDir);
        Directory.CreateDirectory(mod);
        foreach (string f in Directory.EnumerateFiles(Path.Combine(AppPaths.Redist, "blueprint", g.Id)))
            Own(rec, exeDir, CopyInto(f, mod));
        Log.Write($"install: the rig's Blueprint Loader mod in {mod}");
    }

    // ── Unity: BepInEx (5 for Mono, 6 for IL2CPP), and the rig as its plugin ───────────────────────

    private static void InstallUnityRig(Record rec, string exeDir)
    {
        bool il2cpp = IsIl2Cpp(exeDir);
        if (!File.Exists(BepInExCore(exeDir)))
        {
            string zipPath = il2cpp ? AppPaths.BepInExIl2CppZip : AppPaths.BepInExZip;
            using var zip = ZipFile.OpenRead(zipPath);
            foreach (var e in zip.Entries)
            {
                if (e.FullName.EndsWith('/') || e.FullName == "changelog.txt") continue;
                Own(rec, exeDir, Extract(e, exeDir));
            }
            rec.BepInExOurs = true;
            Log.Write($"install: {Path.GetFileNameWithoutExtension(zipPath)} added (winhttp.dll)" +
                      (il2cpp ? " - its first start builds the game's interop assemblies, a minute or two, once" : ""));
        }
        else Log.Write("install: BepInEx already here - kept, with its plugins");

        string plugins = Path.Combine(exeDir, "BepInEx", "plugins", PluginFolder);
        Directory.CreateDirectory(plugins);
        Own(rec, exeDir, CopyInto(il2cpp ? AppPaths.UnityIl2CppPlugin : AppPaths.UnityPlugin, plugins));
        Log.Write($"install: camera rig plugin{(il2cpp ? " (IL2CPP)" : "")} in BepInEx\\plugins\\{PluginFolder}");
    }

    // ── REDengine 3 (The Witcher 3): a script mod, and the game's player script with three lines added ──────────────

    /// <summary>The player script, patched (rig/redengine3/r4Player.patch.json), or why it cannot be: the game's file
    /// is read from the player's own install, so no game file ships with the app.</summary>
    private sealed class Red3Patch
    {
        public string File { get; set; } = "";
        public List<Red3Insert> Inserts { get; set; } = [];
    }
    private sealed class Red3Insert
    {
        public string Before { get; set; } = "";
        public string Text { get; set; } = "";
    }

    private static string Red3Patched(string exeDir, out string relPath)
    {
        var patch = JsonSerializer.Deserialize<Red3Patch>(File.ReadAllText(Path.Combine(AppPaths.Red3Rig, "r4Player.patch.json")),
                                                          new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!;
        relPath = patch.File.Replace('/', Path.DirectorySeparatorChar);
        string game = Path.Combine(Red3Root(exeDir), "content", "content0", "scripts", relPath);
        if (!File.Exists(game)) throw new InvalidOperationException($"This game has no {patch.File} where The Witcher 3 keeps it: is this its folder?");
        string src = File.ReadAllText(game);
        string nl = src.Contains("\r\n", StringComparison.Ordinal) ? "\r\n" : "\n";
        foreach (var ins in patch.Inserts)
        {
            int at = src.IndexOf(ins.Before, StringComparison.Ordinal);
            if (at < 0 || src.IndexOf(ins.Before, at + 1, StringComparison.Ordinal) >= 0)
                throw new InvalidOperationException($"This version of the game's {patch.File} is not the one the rig was made for " +
                                                    "(a game update?): the rig cannot go in until it is updated for it.");
            src = src.Insert(at, ins.Text.Replace("\n", nl));
        }
        return src;
    }

    /// <summary>Another mod replacing the same player script would lose one of the two: refused, before any change.</summary>
    private static void RefuseRed3(string exeDir)
    {
        Red3Patched(exeDir, out string rel);   // the game's file is there and can be patched
        string mods = Path.Combine(Red3Root(exeDir), "mods");
        if (!Directory.Exists(mods)) return;
        foreach (string mod in Directory.EnumerateDirectories(mods))
        {
            if (Path.GetFileName(mod).Equals(Red3Mod, StringComparison.OrdinalIgnoreCase)) continue;
            if (File.Exists(Path.Combine(mod, "content", "scripts", rel)))
                throw new InvalidOperationException($"The mod {Path.GetFileName(mod)} replaces the game's player script, which the rig " +
                                                    "adds three lines to. Merge the two with Script Merger, or remove that mod first.");
        }
    }

    private static void InstallRed3Rig(Record rec, string exeDir)
    {
        string scripts = Path.Combine(Red3Root(exeDir), "mods", Red3Mod, "content", "scripts");
        string local = Path.Combine(scripts, "local");
        Directory.CreateDirectory(local);
        Own(rec, exeDir, CopyInto(Path.Combine(AppPaths.Red3Rig, "voxonRig.ws"), local));
        string patched = Red3Patched(exeDir, out string rel);
        string player = Path.Combine(scripts, rel);
        Directory.CreateDirectory(Path.GetDirectoryName(player)!);
        File.WriteAllText(player, patched, new UTF8Encoding(false));
        Own(rec, exeDir, player);
        Log.Write($"install: camera rig script mod in mods\\{Red3Mod}, the player script patched from this game's own");
    }

    // ── REDengine 4 (Cyberpunk 2077): Cyber Engine Tweaks, RED4ext and Codeware, and the rig as a CET mod ──────────

    /// <summary>The loaders' proxy slots taken by something else: refused, before any change. Cyber Engine Tweaks loads
    /// as version.dll, RED4ext as winmm.dll; each is told by its own folder beside it.</summary>
    private static void RefuseRed4(Record rec, string exeDir)
    {
        string root = Red3Root(exeDir);
        if (!rec.CetOurs && File.Exists(Path.Combine(exeDir, "version.dll")) && !File.Exists(Path.Combine(exeDir, "plugins", "cyber_engine_tweaks.asi")))
            throw new InvalidOperationException("This game already has a version.dll that is not Cyber Engine Tweaks. The rig needs that slot for it; remove the other mod first.");
        if (!rec.Red4extOurs && File.Exists(Path.Combine(exeDir, "winmm.dll")) && !File.Exists(Path.Combine(root, "red4ext", "RED4ext.dll")))
            throw new InvalidOperationException("This game already has a winmm.dll that is not RED4ext. The rig needs that slot for it; remove the other mod first.");
    }

    /// <summary>The three loaders, each only if the game has none (a player's own stays, with its mods), then the rig's
    /// mod. The rig adds its cameras while V is assembled, so a save is loaded once after the install.</summary>
    private static void InstallRed4Rig(Record rec, string exeDir)
    {
        string root = Red3Root(exeDir);
        (string zip, string have, string name, Action ours)[] loaders =
        [
            (AppPaths.CetZip, Path.Combine(exeDir, "plugins", "cyber_engine_tweaks.asi"), "Cyber Engine Tweaks", () => rec.CetOurs = true),
            (AppPaths.Red4extZip, Path.Combine(root, "red4ext", "RED4ext.dll"), "RED4ext", () => rec.Red4extOurs = true),
            (AppPaths.CodewareZip, Path.Combine(root, "red4ext", "plugins", "Codeware", "Codeware.dll"), "Codeware", () => rec.CodewareOurs = true),
        ];
        foreach (var (zip, have, name, ours) in loaders)
        {
            if (File.Exists(have) && !rec.Files.Contains(Path.GetRelativePath(exeDir, have)))
            {
                Log.Write($"install: {name} already here - kept, with its mods");
                continue;
            }
            using var z = ZipFile.OpenRead(zip);
            foreach (var e in z.Entries)
                if (!e.FullName.EndsWith('/')) Own(rec, exeDir, Extract(e, root));
            ours();
            Log.Write($"install: {Path.GetFileNameWithoutExtension(zip)} added");
        }
        // Cyber Engine Tweaks' first start asks for a key for its overlay and takes the next one pressed (Alt, from a
        // window being brought to the front, measured) - and the overlay then holds the keyboard. Ours gets ~ instead
        // (VK_OEM_3, in the top 16 bits as it stores a key), so the game starts straight into its menu.
        string bindings = Path.Combine(CetDir(exeDir), "bindings.json");
        if (rec.CetOurs && !File.Exists(bindings))
        {
            File.WriteAllText(bindings, "{\n    \"cet\": {\n        \"overlay_key\": 54043195528445952\n    }\n}\n");
            Own(rec, exeDir, bindings);
        }
        string mod = Red4Mod(exeDir);
        Directory.CreateDirectory(mod);
        Own(rec, exeDir, CopyInto(Path.Combine(AppPaths.Red4Rig, "voxon_rig", "init.lua"), mod));
        Log.Write(@"install: camera rig in bin\x64\plugins\cyber_engine_tweaks\mods\voxon_rig - load a save once to arm it");
    }

    /// <summary>What the loaders and the rig write while the game runs, taken out with them: the rig's mod's own files
    /// always (its settings from the add-on, Cyber Engine Tweaks' per-mod store and log), the loaders' when ours.</summary>
    private static void RemoveRed4Runtime(Record? rec, string exeDir)
    {
        string root = Red3Root(exeDir), cet = CetDir(exeDir), mod = Red4Mod(exeDir);
        foreach (string f in new[] { "rig.txt", "rig.txt.tmp", "db.sqlite3", "voxon_rig.log" }) TryDelete(Path.Combine(mod, f));
        if (rec?.CetOurs == true)
        {
            foreach (string f in new[] { "bindings.json", "config.json", "cyber_engine_tweaks.log", "gamelog.log", "layout.ini",
                                         "persistent.json", "scripting.log" })
                TryDelete(Path.Combine(cet, f));
            TryDeleteDir(Path.Combine(exeDir, "plugins", "address_library"));
        }
        if (rec?.Red4extOurs == true)
        {
            TryDelete(Path.Combine(root, "red4ext", "config.ini"));
            TryDeleteDir(Path.Combine(root, "red4ext", "logs"));
        }
        if (rec?.CodewareOurs == true)
        {
            string cw = Path.Combine(root, "red4ext", "plugins", "Codeware");
            if (Directory.Exists(cw)) foreach (string f in Directory.EnumerateFiles(cw, "Codeware-*.log")) TryDelete(f);
            TryDeleteDir(Path.Combine(cw, "Persistent"));
        }
        DeleteEmptyDirs(mod);
        DeleteEmptyDirs(Path.Combine(exeDir, "plugins"));
        DeleteEmptyDirs(Path.Combine(root, "red4ext"));
    }

    // ── Every engine: ReShade, the add-on, the game's settings ─────────────────────────────────────

    private static void InstallReShade(Record rec, string exeDir)
    {
        string dxgi = Path.Combine(exeDir, "dxgi.dll");
        if (!File.Exists(dxgi))
        {
            File.Copy(Path.Combine(AppPaths.Redist, "ReShade64.dll"), dxgi);
            Own(rec, exeDir, dxgi);
            rec.ReShadeOurs = true;
            Log.Write("install: ReShade 6.7.3 (add-on build) added as dxgi.dll");
        }
        else if (!rec.ReShadeOurs)
            Log.Write("install: ReShade already here - kept (it must be the build with add-on support)");
        Own(rec, exeDir, CopyInto(Path.Combine(AppPaths.Redist, AddonName), exeDir));

        string reshadeIni = Path.Combine(exeDir, "ReShade.ini");
        if (!File.Exists(reshadeIni))
        {
            // No effects: the add-on is the only thing ReShade is here for. TutorialProgress=4 skips the first-run
            // overlay, which otherwise sits over the game until dismissed.
            File.WriteAllText(reshadeIni,
                $"[ADDON]\r\nAddonPath={exeDir}\r\n\r\n[GENERAL]\r\nEffectSearchPaths=\r\nTextureSearchPaths=\r\n" +
                "PresetPath=.\\ReShadePreset.ini\r\nNoReloadOnInit=1\r\n\r\n[OVERLAY]\r\nTutorialProgress=4\r\n");
            rec.ReShadeIniOurs = true;
            Own(rec, exeDir, reshadeIni);
        }
        else if (!rec.ReShadeIniOurs)
            Rewrite(reshadeIni, t =>
            {
                if (string.Equals(LineValue(t, "AddonPath"), exeDir, StringComparison.OrdinalIgnoreCase)) return t;
                if (!rec.AddonPathSet)
                {
                    rec.AddonPathSet = true;
                    rec.OldAddonPath = RawLine(t, "AddonPath");      // null: there was none, and Remove takes ours out
                }
                return SetLine(t, "AddonPath", exeDir, section: "[ADDON]");
            });
        Log.Write("install: capture add-on beside the game, ReShade pointed at it");
    }

    private static void InstallSettings(GameDef g, Record rec, string exeDir)
    {
        string capIni = Path.Combine(exeDir, CaptureIni);
        if (File.Exists(capIni) && !rec.Files.Contains(CaptureIni) && rec.CaptureIniBackup == null)
        {
            rec.CaptureIniBackup = CaptureIni + ".before-VoxonCaptureRig";
            File.Copy(capIni, Path.Combine(exeDir, rec.CaptureIniBackup), overwrite: true);
        }
        string profile = File.ReadAllText(Path.Combine(AppPaths.Profiles, g.Profile + ".ini"));
        profile = Ini.Set(profile, "controlFile", AppPaths.ControlFile);
        profile = Ini.Set(profile, "hostGame", g.Id);
        profile = Ini.Set(profile, "pipe", AppPaths.PipeName);
        profile = Ini.Set(profile, "engine", g.Engine);          // the add-on reads depth by it
        File.WriteAllText(capIni, profile, Encoding.ASCII);
        Own(rec, exeDir, capIni);
        Log.Write($"install: settings ({g.Profile}) -> {CaptureIni}, driven by this app");
    }

    /// <summary>The rig out of a game, and everything it changed put back.</summary>
    public static void Remove(GameDef g, string exeDir)
    {
        if (IsRunning(g, exeDir)) throw new InvalidOperationException($"{g.Name} is running. Close it first.");
        var rec = LoadRecord(exeDir);
        Log.Write($"remove: {g.Name} <- {exeDir}");
        string pluginDir = Path.Combine(exeDir, "BepInEx", "plugins", PluginFolder);
        if (rec == null)
        {
            // Not ours: take out only the rig itself, and leave the loader and ReShade as they are.
            if (IsUnity(g)) TryDeleteDir(pluginDir);
            else if (IsRed3(g)) { TryDeleteDir(Path.Combine(Red3Root(exeDir), "mods", Red3Mod)); DeleteEmptyDirs(Path.Combine(Red3Root(exeDir), "mods")); }
            else if (IsRed4(g)) { TryDeleteDir(Red4Mod(exeDir)); RemoveRed4Runtime(null, exeDir); }
            else if (IsBlueprint(g)) { TryDeleteDir(BlueprintMod(g, exeDir)); TryDelete(BlueprintBridge.PathFor(exeDir)); }
            else if (!AddonOnly(g)) { TryDeleteDir(Path.Combine(exeDir, "Mods", "VoxonTapUE")); DropModsLine(exeDir); }
            TryDelete(Path.Combine(exeDir, AddonName));
            TryDelete(Path.Combine(exeDir, AddonLog));
            TryDelete(Path.Combine(exeDir, CaptureIni));
            Log.Write("remove: rig, add-on and settings removed; the loader and ReShade left as they were");
            return;
        }

        if (!IsUnity(g) && !IsRed3(g) && !IsRed4(g) && !AddonOnly(g)) DropModsLine(exeDir);
        foreach (string rel in rec.Files.OrderByDescending(f => f.Length))
            TryDelete(Path.Combine(exeDir, rel));
        if (IsRed4(g)) RemoveRed4Runtime(rec, exeDir);
        else if (IsBlueprint(g))
        {
            TryDelete(BlueprintBridge.PathFor(exeDir) + ".tmp");
            DeleteEmptyDirs(BlueprintMod(g, exeDir));
        }
        else if (IsRed3(g))
        {
            // The mod's folders, and mods\ itself when nothing else is in it.
            DeleteEmptyDirs(Path.Combine(Red3Root(exeDir), "mods", Red3Mod));
            DeleteEmptyDirs(Path.Combine(Red3Root(exeDir), "mods"));
        }
        else if (IsUnity(g))
        {
            TryDeleteDir(pluginDir);
            if (rec.BepInExOurs)
            {
                // What BepInEx writes on its first run; a plugin the player added since stays, and its folders. BepInEx
                // 6 for IL2CPP also writes the game's interop assemblies and the Unity libraries it made them from.
                string b = Path.Combine(exeDir, "BepInEx");
                TryDelete(Path.Combine(b, "LogOutput.log"));
                TryDelete(Path.Combine(b, "ErrorLog.log"));
                TryDelete(Path.Combine(b, "config", "BepInEx.cfg"));
                TryDeleteDir(Path.Combine(b, "cache"));
                TryDeleteDir(Path.Combine(b, "interop"));
                TryDeleteDir(Path.Combine(b, "unity-libs"));
                DeleteEmptyDirs(b);
                DeleteEmptyDirs(Path.Combine(exeDir, "dotnet"));       // BepInEx 6's own .NET
            }
        }
        else if (!AddonOnly(g))
        {
            TryDeleteDir(Path.Combine(exeDir, "Mods", "VoxonTapUE"));
            if (rec.Ue4ssOurs)
            {
                TryDelete(Path.Combine(exeDir, "UE4SS.log"));
                DeleteEmptyDirs(Path.Combine(exeDir, "Mods"));
            }
        }
        TryDelete(Path.Combine(exeDir, AddonLog));
        if (rec.ReShadeOurs)
        {
            TryDelete(Path.Combine(exeDir, "ReShade.log"));
            TryDelete(Path.Combine(exeDir, "ReShadePreset.ini"));
            // ReShade's numbered settings and logs (ReShade2.ini): written when a second process of the game's loads it
            // while the first holds ReShade.ini (Undisputed, 2026-10-23)
            foreach (string f in Directory.EnumerateFiles(exeDir, "ReShade*.*"))
                if (System.Text.RegularExpressions.Regex.IsMatch(Path.GetFileName(f), @"^ReShade\d+\.(ini|log)$",
                                                                 System.Text.RegularExpressions.RegexOptions.IgnoreCase)) TryDelete(f);
        }
        string reshadeIni = Path.Combine(exeDir, "ReShade.ini");
        if (rec.AddonPathSet && !rec.ReShadeIniOurs && File.Exists(reshadeIni))
            Rewrite(reshadeIni, t => PutBack(t, "AddonPath", rec.OldAddonPath));
        string settings = Path.Combine(exeDir, "UE4SS-settings.ini");
        if (rec.EngineSet && !rec.Ue4ssOurs && File.Exists(settings))
            Rewrite(settings, t => PutBack(PutBack(t, "MajorVersion", rec.OldEngineMajor), "MinorVersion", rec.OldEngineMinor));
        if (rec.CaptureIniBackup != null && File.Exists(Path.Combine(exeDir, rec.CaptureIniBackup)))
            File.Move(Path.Combine(exeDir, rec.CaptureIniBackup), Path.Combine(exeDir, CaptureIni), overwrite: true);
        TryDelete(Path.Combine(exeDir, RecordName));
        Log.Write("remove: done - the game's folder is as it was before the rig went in");
    }

    /// <summary>A proxy DLL slot taken by something else: refuse before changing anything. ReShade's dxgi.dll is
    /// told by its product name, BepInEx's winhttp.dll by the BepInEx folder beside it.</summary>
    private static void RefuseForeign(string exeDir, string dll, string want, bool ours)
    {
        string path = Path.Combine(exeDir, dll);
        if (ours || !File.Exists(path)) return;
        bool isIt = want == "BepInEx"
            ? File.Exists(Path.Combine(exeDir, "BepInEx", "core", "BepInEx.dll")) || File.Exists(Path.Combine(exeDir, "BepInEx", "core", "BepInEx.Core.dll"))
            : (FileVersionInfo.GetVersionInfo(path).ProductName ?? "").Contains(want, StringComparison.OrdinalIgnoreCase);
        if (!isIt)
            throw new InvalidOperationException(
                $"This game already has a {dll} that is not {want}. The rig needs that slot for {want}; remove the other mod first.");
    }

    private static string Extract(ZipArchiveEntry e, string root, string? name = null)
    {
        string dest = Path.Combine(root, (name ?? e.FullName).Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
        e.ExtractToFile(dest, overwrite: true);
        return dest;
    }

    // ── helpers ─────────────────────────────────────────────────────────────────────────────────────

    private static void Own(Record rec, string exeDir, string path)
    {
        string rel = Path.GetRelativePath(exeDir, path);
        if (!rec.Files.Contains(rel)) rec.Files.Add(rel);
    }

    private static string CopyInto(string file, string dir)
    {
        string dest = Path.Combine(dir, Path.GetFileName(file));
        File.Copy(file, dest, overwrite: true);
        return dest;
    }

    private static void DropModsLine(string exeDir)
    {
        string modsTxt = Path.Combine(exeDir, "Mods", "mods.txt");
        if (File.Exists(modsTxt))
            Rewrite(modsTxt, m => System.Text.RegularExpressions.Regex.Replace(m, @"(?m)^VoxonTapUE[ \t]*:[ \t]*\d\r?\n(\r?\n)?", ""));
    }

    /// <summary>Reads, changes and writes back a text file in the encoding it came in: UE4SS's files start with a
    /// UTF-8 BOM and a plain WriteAllText drops it, so Remove could not have put them back as they were.</summary>
    private static void Rewrite(string path, Func<string, string> change)
    {
        byte[] raw = File.ReadAllBytes(path);
        bool bom = raw.Length >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF;
        string before = Encoding.UTF8.GetString(raw, bom ? 3 : 0, raw.Length - (bom ? 3 : 0));
        string after = change(before);
        if (after != before) File.WriteAllText(path, after, new UTF8Encoding(bom));
    }

    /// <summary>Puts a recorded line back as it was, or takes ours out when there had been none.</summary>
    private static string PutBack(string text, string key, string? original) =>
        original != null ? PutRawLine(text, key, original)
                         : new System.Text.RegularExpressions.Regex(LineRx(key) + @"\r?\n?").Replace(text, "", 1);

    /// <summary>Sets "key = value" (UE4SS) or "key=value" (ReShade) in place; inside <paramref name="section"/> when
    /// given, appending it there if absent.</summary>
    private static string SetLine(string text, string key, string value, string? section = null)
    {
        // [ \t], never \s: in multiline mode \s* at ^ reaches back across blank lines and the replacement eats them.
        var rx = new System.Text.RegularExpressions.Regex($@"(?m)^([ \t]*{key}[ \t]*=)[^\r\n]*");
        if (rx.IsMatch(text))
        {
            bool spaced = rx.Match(text).Groups[1].Value.Contains(" =");
            return rx.Replace(text, spaced ? $"{key} = {value}" : $"{key}={value}", 1);
        }
        if (section != null)
        {
            int at = text.IndexOf(section, StringComparison.OrdinalIgnoreCase);
            if (at >= 0)
            {
                int eol = text.IndexOf('\n', at);
                return text.Insert(eol < 0 ? text.Length : eol + 1, $"{key}={value}\r\n");
            }
            return text.TrimEnd() + $"\r\n\r\n{section}\r\n{key}={value}\r\n";
        }
        return text.TrimEnd() + $"\r\n{key}={value}\r\n";
    }

    private static string? RawLine(string text, string key)
    {
        var m = System.Text.RegularExpressions.Regex.Match(text, LineRx(key));
        return m.Success ? m.Value : null;
    }

    private static string PutRawLine(string text, string key, string raw) =>
        new System.Text.RegularExpressions.Regex(LineRx(key)).Replace(text, raw.Replace("$", "$$"), 1);

    // The whole "key = value" line, without its line break.
    private static string LineRx(string key) => "(?m)^[ \t]*" + key + "[ \t]*=[^\r\n]*";

    private static string? LineValue(string text, string key)
    {
        var m = System.Text.RegularExpressions.Regex.Match(text, $@"(?m)^[ \t]*{key}[ \t]*=([^\r\n]*)");
        return m.Success ? m.Groups[1].Value.Trim() : null;
    }

    private static Record? LoadRecord(string exeDir)
    {
        string p = Path.Combine(exeDir, RecordName);
        if (!File.Exists(p)) return null;
        try { return JsonSerializer.Deserialize<Record>(File.ReadAllText(p)); }
        catch (Exception) { return null; }
    }

    private static void SaveRecord(string exeDir, Record rec) =>
        File.WriteAllText(Path.Combine(exeDir, RecordName),
                          JsonSerializer.Serialize(rec, new JsonSerializerOptions { WriteIndented = true }));

    private static void TryDelete(string path)
    {
        try { if (File.Exists(path)) File.Delete(path); }
        catch (Exception ex) { Log.Write($"remove: could not delete {path}: {ex.Message}"); }
    }

    private static void TryDeleteDir(string dir)
    {
        try { if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true); }
        catch (Exception ex) { Log.Write($"remove: could not delete {dir}: {ex.Message}"); }
    }

    private static void DeleteEmptyDirs(string dir)
    {
        if (!Directory.Exists(dir)) return;
        foreach (string sub in Directory.EnumerateDirectories(dir)) DeleteEmptyDirs(sub);
        try { if (!Directory.EnumerateFileSystemEntries(dir).Any()) Directory.Delete(dir); }
        catch (Exception) { /* left as it is */ }
    }
}
