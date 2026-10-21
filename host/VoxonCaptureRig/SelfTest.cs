using System.Buffers.Binary;
using System.IO.Compression;
using System.IO.Pipes;
using System.Security.Cryptography;
using System.Text;

namespace VoxonCaptureRig;

/// <summary>VoxonCaptureRig.exe --selftest: the installer against throwaway game folders, and the pipe against a fake
/// add-on, with no game, no display and nothing outside a temporary folder touched. Writes selftest.txt beside the exe
/// and exits 0 when every check passes.
///
/// <para>The installer's promise is that Remove puts a game's folder back EXACTLY as it was, whatever was there
/// before - nothing, or a player's own UE4SS and ReShade and an older rig. So each case snapshots the folder (every
/// file and its hash), installs, checks, removes, and compares.</para></summary>
internal static class SelfTest
{
    private static int _fail;
    private static readonly StringBuilder Out = new();

    public static int Run()
    {
        string root = Path.Combine(Path.GetTempPath(), "VoxonCaptureRig-selftest-" + Environment.ProcessId);
        try
        {
            EmptyGame(Path.Combine(root, "empty"));
            PlayerModdedGame(Path.Combine(root, "modded"));
            ForeignDxgi(Path.Combine(root, "foreign"));
            OwnUe4ssGame(Path.Combine(root, "own-ue4ss"));
            EmptyUnityGame(Path.Combine(root, "unity"));
            PlayerModdedUnityGame(Path.Combine(root, "unity-modded"));
            ForeignWinhttp(Path.Combine(root, "unity-foreign"));
            Red3Game(Path.Combine(root, "witcher3"));
            Red3Refusals(Path.Combine(root, "witcher3-refused"));
            Red4Game(Path.Combine(root, "cyberpunk"), playerLoaders: false);
            Red4Game(Path.Combine(root, "cyberpunk-modded"), playerLoaders: true);
            Red4Refusals(Path.Combine(root, "cyberpunk-refused"));
            AddonOnlyGame(Path.Combine(root, "bf2"), Frostbite, "13. A Frostbite game");
            AddonOnlyGame(Path.Combine(root, "sims4"), Sims4, "14. The Sims 4");
            AddonOnlyGame(Path.Combine(root, "crysis"), Crysis, "15. Crysis");
            Il2CppUnityGame(Path.Combine(root, "unity-il2cpp"), Path.Combine(root, "unity-il2cpp-bepinex5"));
            Feed();
            GameSettings();
            Launching(Path.Combine(root, "launch"));
        }
        catch (Exception ex) { Check(false, $"unexpected: {ex}"); }
        finally
        {
            // Kept when anything failed, so the folders can be looked at.
            if (_fail == 0) { try { Directory.Delete(root, true); } catch (Exception) { } }
            else Say($"\nthe test folders are kept in {root}");
        }

        Say(_fail == 0 ? "\nALL CHECKS PASSED" : $"\n{_fail} CHECK(S) FAILED");
        File.WriteAllText(Path.Combine(AppPaths.Root, "selftest.txt"), Out.ToString());
        return _fail == 0 ? 0 : 1;
    }

    private static readonly GameDef Game = new() { Id = "ascent", Name = "Test game", Profile = "ascent" };

    // ── A game with nothing in it: everything is ours, and all of it goes ────────────────────────────

    private static void EmptyGame(string dir)
    {
        Say("\n1. A game folder with nothing in it");
        Directory.CreateDirectory(dir);
        File.WriteAllText(Path.Combine(dir, "Test-Win64-Shipping.exe"), "not a game");
        var before = Snapshot(dir);

        RigInstaller.Install(Game, dir);
        Check(RigInstaller.StateOf(Game, dir) == RigInstaller.State.Installed, "installed, and says so");
        foreach (string f in new[] { "dwmapi.dll", "UE4SS.dll", "dxgi.dll", "voxon_capture_ue.addon64", "ReShade.ini",
                                     @"Mods\VoxonTapUE\Scripts\rig.lua", @"Mods\VoxonTapUE\enabled.txt", "voxon_capture_ue.ini" })
            Check(File.Exists(Path.Combine(dir, f)), $"{f} is there");
        string mods = File.ReadAllText(Path.Combine(dir, "Mods", "mods.txt"));
        int rig = mods.IndexOf("VoxonTapUE : 1", StringComparison.Ordinal), keys = mods.IndexOf("; Built-in keybinds", StringComparison.Ordinal);
        Check(rig >= 0 && keys > rig, "mods.txt lists the rig, before the keybinds line");
        Check(mods.Contains("ConsoleEnablerMod : 0", StringComparison.Ordinal), "UE4SS's bundled mods are off");
        Check(File.ReadAllBytes(Path.Combine(dir, "Mods", "mods.txt")).AsSpan(0, 3).SequenceEqual(new byte[] { 0xEF, 0xBB, 0xBF }),
              "mods.txt keeps its BOM");
        string settings = File.ReadAllText(Path.Combine(dir, "UE4SS-settings.ini"));
        Check(settings.Contains("bUseUObjectArrayCache = false", StringComparison.Ordinal), "UE4SS's object cache is off");
        string cap = File.ReadAllText(Path.Combine(dir, "voxon_capture_ue.ini"));
        Check(cap.Contains($"controlFile={AppPaths.ControlFile}", StringComparison.Ordinal), "the game's settings name this app's control file");
        Check(cap.Contains($"pipe={AppPaths.PipeName}", StringComparison.Ordinal), "...and this app's pipe");
        Check(cap.Contains("hostGame=ascent", StringComparison.Ordinal), "...and the game's id");
        Check(File.ReadAllText(Path.Combine(dir, "ReShade.ini")).Contains($"AddonPath={dir}", StringComparison.Ordinal),
              "ReShade looks for add-ons beside the game");

        // The install record is left out: it notes when the last install was, which is the one thing that does change.
        var once = Snapshot(dir);
        once.Remove(RigInstaller.RecordName);
        RigInstaller.Install(Game, dir);
        var twice = Snapshot(dir);
        twice.Remove(RigInstaller.RecordName);
        Check(Same(once, twice), "installing again changes nothing");
        Diff(once, twice);

        // What a run of the game writes, as if it had been played: the loader's, ReShade's and the add-on's logs.
        foreach (string log in new[] { "UE4SS.log", "ReShade.log", "voxon_capture_ue.log" })
            File.WriteAllText(Path.Combine(dir, log), "a run");
        RigInstaller.Remove(Game, dir);
        var after = Snapshot(dir);
        Check(Same(before, after), "Remove leaves the folder exactly as it was, after a run too");
        Diff(before, after);
        Check(RigInstaller.StateOf(Game, dir) == RigInstaller.State.NotInstalled, "...and says the rig is not installed");
    }

    // ── A game the player has already modded: theirs stays, ours goes, what we changed comes back ────

    private static void PlayerModdedGame(string dir)
    {
        Say("\n2. A game with the player's own UE4SS, ReShade and an older rig's settings");
        Directory.CreateDirectory(Path.Combine(dir, "Mods", "TheirMod", "Scripts"));
        File.WriteAllText(Path.Combine(dir, "Test-Win64-Shipping.exe"), "not a game");
        using (var zip = ZipFile.OpenRead(Path.Combine(AppPaths.Redist, "UE4SS_v3.0.1.zip")))
        {
            zip.GetEntry("UE4SS-settings.ini")!.ExtractToFile(Path.Combine(dir, "UE4SS-settings.ini"));
            zip.GetEntry("Mods/mods.txt")!.ExtractToFile(Path.Combine(dir, "Mods", "mods.txt"));
        }
        File.WriteAllText(Path.Combine(dir, "UE4SS.dll"), "the player's UE4SS");
        File.WriteAllText(Path.Combine(dir, "dwmapi.dll"), "the player's UE4SS proxy");
        File.WriteAllText(Path.Combine(dir, "Mods", "TheirMod", "Scripts", "main.lua"), "-- theirs");
        File.Copy(Path.Combine(AppPaths.Redist, "ReShade64.dll"), Path.Combine(dir, "dxgi.dll"));
        File.WriteAllText(Path.Combine(dir, "ReShade.ini"), "[ADDON]\r\nAddonPath=D:\\somewhere\\else\r\n\r\n[GENERAL]\r\nPerformanceMode=1\r\n");
        File.WriteAllText(Path.Combine(dir, "voxon_capture_ue.ini"), "; an older rig's settings\r\nradiusM=8\r\n");
        var before = Snapshot(dir);

        var wukongish = new GameDef { Id = "wukong", Name = "Test game", Profile = "wukong", EngineMajor = 5, EngineMinor = 0 };
        Check(RigInstaller.StateOf(Game, dir) == RigInstaller.State.Foreign, "an older rig is recognised as not ours");
        RigInstaller.Install(wukongish, dir);
        Check(File.ReadAllText(Path.Combine(dir, "UE4SS.dll")) == "the player's UE4SS", "the player's UE4SS is kept");
        Check(File.ReadAllText(Path.Combine(dir, "UE4SS-settings.ini")).Contains("MajorVersion = 5", StringComparison.Ordinal),
              "the engine override is applied for the game that needs it");
        Check(File.ReadAllText(Path.Combine(dir, "ReShade.ini")).Contains("PerformanceMode=1", StringComparison.Ordinal),
              "the player's ReShade settings are kept");
        Check(File.ReadAllText(Path.Combine(dir, "voxon_capture_ue.ini")).Contains("hostGame=wukong", StringComparison.Ordinal),
              "the older rig's settings are replaced");
        Check(File.ReadAllText(Path.Combine(dir, "Mods", "mods.txt")).Contains("CheatManagerEnablerMod : 1", StringComparison.Ordinal),
              "the player's mods.txt choices are left alone");

        RigInstaller.Remove(wukongish, dir);
        var after = Snapshot(dir);
        Check(Same(before, after), "Remove puts back their ReShade.ini, UE4SS settings and the older rig's settings, exactly");
        Diff(before, after);
    }

    // ── A dxgi.dll that is some other mod: refuse, and touch nothing ─────────────────────────────────

    private static void ForeignDxgi(string dir)
    {
        Say("\n3. A game whose dxgi.dll belongs to another mod");
        Directory.CreateDirectory(dir);
        File.WriteAllText(Path.Combine(dir, "Test-Win64-Shipping.exe"), "not a game");
        File.WriteAllText(Path.Combine(dir, "dxgi.dll"), "some other mod's proxy");
        var before = Snapshot(dir);
        bool refused = false;
        try { RigInstaller.Install(Game, dir); } catch (InvalidOperationException) { refused = true; }
        Check(refused, "Install refuses");
        Check(Same(before, Snapshot(dir)), "...before changing anything");
    }

    // ── Unity: BepInEx and the plugin, in and out ───────────────────────────────────────────────────

    // ── A game that needs a UE4SS of its own: Mortal Kombat 1's build, hooks and vtable layout ─────────

    private static readonly GameDef Mk1 = new() { Id = "mk1", Name = "Test game (own UE4SS)", Profile = "mk1",
                                                  Ue4ss = "UE4SS_v3.0.1-1165-g12ec45a0.zip", Ue4ssHooks = ["EngineTick"] };

    private static void OwnUe4ssGame(string dir)
    {
        Say("\n4. A game that needs its own UE4SS: a newer build, one hook, a vtable layout beside it");
        Directory.CreateDirectory(dir);
        File.WriteAllText(Path.Combine(dir, "Test-Win64-Shipping.exe"), "not a game");
        File.WriteAllText(Path.Combine(dir, "VTableLayout.ini"), "the player's own");
        var theirs = Snapshot(dir);
        bool refused = false;
        try { RigInstaller.Install(Mk1, dir); } catch (InvalidOperationException) { refused = true; }
        Check(refused, "a VTableLayout.ini that is not ours: Install refuses");
        Check(Same(theirs, Snapshot(dir)), "...before changing anything");
        File.Delete(Path.Combine(dir, "VTableLayout.ini"));
        var before = Snapshot(dir);

        RigInstaller.Install(Mk1, dir);
        using (var zip = ZipFile.OpenRead(Path.Combine(AppPaths.Redist, Mk1.Ue4ss)))
            Check(new FileInfo(Path.Combine(dir, "UE4SS.dll")).Length == zip.GetEntry("ue4ss/UE4SS.dll")!.Length,
                  "its UE4SS.dll, out of the zip's ue4ss folder and beside the exe");
        string settings = File.ReadAllText(Path.Combine(dir, "UE4SS-settings.ini"));
        Check(System.Text.RegularExpressions.Regex.IsMatch(settings, @"(?m)^HookEngineTick[ \t]*=[ \t]*1"), "the engine-tick hook on");
        Check(!System.Text.RegularExpressions.Regex.IsMatch(settings, @"(?m)^Hook(?!EngineTick\b)\w+[ \t]*=[ \t]*1"), "every other hook off");
        Check(File.Exists(Path.Combine(dir, "VTableLayout.ini")), "its vtable layout beside UE4SS");
        Check(!File.ReadAllText(Path.Combine(dir, "Mods", "mods.json")).Contains("\"mod_enabled\": true", StringComparison.Ordinal)
              || File.ReadAllText(Path.Combine(dir, "Mods", "mods.json")).Split("\"mod_enabled\": true").Length == 2,
              "UE4SS's bundled mods off in mods.json too (Keybinds stays)");
        Check(File.Exists(Path.Combine(dir, "Mods", "VoxonTapUE", "Scripts", "rig.lua")), "the rig is in");

        foreach (string log in new[] { "UE4SS.log", "ReShade.log", "voxon_capture_ue.log" })
            File.WriteAllText(Path.Combine(dir, log), "a run");
        RigInstaller.Remove(Mk1, dir);
        var after = Snapshot(dir);
        Check(Same(before, after), "Remove leaves the folder exactly as it was, after a run too");
        Diff(before, after);
    }

    private static readonly GameDef UnityGame = new() { Id = "ror2", Name = "Test Unity game", Profile = "ror2", Engine = "unity" };

    private static void UnityFolder(string dir)
    {
        Directory.CreateDirectory(Path.Combine(dir, "Test_Data"));
        File.WriteAllText(Path.Combine(dir, "Test.exe"), "not a game");
        File.WriteAllText(Path.Combine(dir, "Test_Data", "level0"), "not a level");
    }

    private static void EmptyUnityGame(string dir)
    {
        Say("\n5. A Unity game with nothing in it");
        UnityFolder(dir);
        var before = Snapshot(dir);
        Check(Games.ExeDirUnder(UnityGame, dir) == dir, "the game is found by its exe beside its _Data folder");

        RigInstaller.Install(UnityGame, dir);
        Check(RigInstaller.StateOf(UnityGame, dir) == RigInstaller.State.Installed, "installed, and says so");
        foreach (string f in new[] { "winhttp.dll", "doorstop_config.ini", @"BepInEx\core\BepInEx.dll",
                                     @"BepInEx\plugins\VoxonCaptureRig\VoxonCaptureRig.Unity.dll",
                                     "dxgi.dll", "voxon_capture_ue.addon64", "ReShade.ini", "voxon_capture_ue.ini" })
            Check(File.Exists(Path.Combine(dir, f)), $"{f} is there");
        string cap = File.ReadAllText(Path.Combine(dir, "voxon_capture_ue.ini"));
        Check(cap.Contains("engine=unity", StringComparison.Ordinal), "the game's settings tell the add-on it is Unity");
        Check(cap.Contains("hostGame=ror2", StringComparison.Ordinal), "...and name the game");

        var once = Snapshot(dir);
        once.Remove(RigInstaller.RecordName);
        RigInstaller.Install(UnityGame, dir);
        var twice = Snapshot(dir);
        twice.Remove(RigInstaller.RecordName);
        Check(Same(once, twice), "installing again changes nothing");
        Diff(once, twice);

        // What BepInEx writes on its first run, as if the game had been played.
        Directory.CreateDirectory(Path.Combine(dir, "BepInEx", "cache"));
        Directory.CreateDirectory(Path.Combine(dir, "BepInEx", "config"));
        File.WriteAllText(Path.Combine(dir, "BepInEx", "LogOutput.log"), "a run");
        File.WriteAllText(Path.Combine(dir, "BepInEx", "config", "BepInEx.cfg"), "[x]");
        File.WriteAllText(Path.Combine(dir, "BepInEx", "cache", "chainloader_typeloader.dat"), "cache");
        File.WriteAllText(Path.Combine(dir, "ReShade.log"), "a run");
        File.WriteAllText(Path.Combine(dir, "voxon_capture_ue.log"), "a run");
        RigInstaller.Remove(UnityGame, dir);
        var after = Snapshot(dir);
        Check(Same(before, after), "Remove leaves the folder exactly as it was, after a run too");
        Diff(before, after);
        Check(!Directory.Exists(Path.Combine(dir, "BepInEx")), "...BepInEx's own folder included");
    }

    private static void PlayerModdedUnityGame(string dir)
    {
        Say("\n6. A Unity game with the player's own BepInEx and another plugin");
        UnityFolder(dir);
        Directory.CreateDirectory(Path.Combine(dir, "BepInEx", "core"));
        Directory.CreateDirectory(Path.Combine(dir, "BepInEx", "plugins", "TheirPlugin"));
        File.WriteAllText(Path.Combine(dir, "winhttp.dll"), "their doorstop");
        File.WriteAllText(Path.Combine(dir, "BepInEx", "core", "BepInEx.dll"), "their BepInEx");
        File.WriteAllText(Path.Combine(dir, "BepInEx", "plugins", "TheirPlugin", "Theirs.dll"), "their plugin");
        var before = Snapshot(dir);

        RigInstaller.Install(UnityGame, dir);
        Check(File.ReadAllText(Path.Combine(dir, "BepInEx", "core", "BepInEx.dll")) == "their BepInEx", "the player's BepInEx is kept");
        Check(File.Exists(Path.Combine(dir, "BepInEx", "plugins", "VoxonCaptureRig", "VoxonCaptureRig.Unity.dll")),
              "the rig goes in beside their plugin");
        RigInstaller.Remove(UnityGame, dir);
        var after = Snapshot(dir);
        Check(Same(before, after), "Remove takes out only the rig");
        Diff(before, after);
    }

    private static void Il2CppUnityGame(string dir, string withBepInEx5)
    {
        Say("\n16. A Unity IL2CPP game (GameAssembly.dll): BepInEx 6 and the rig's IL2CPP build");
        UnityFolder(dir);
        File.WriteAllText(Path.Combine(dir, "GameAssembly.dll"), "not a game");
        var before = Snapshot(dir);

        RigInstaller.Install(UnityGame, dir);
        Check(RigInstaller.StateOf(UnityGame, dir) == RigInstaller.State.Installed, "installed, and says so");
        foreach (string f in new[] { "winhttp.dll", "doorstop_config.ini", @"BepInEx\core\BepInEx.Unity.IL2CPP.dll", @"dotnet\coreclr.dll",
                                     @"BepInEx\plugins\VoxonCaptureRig\VoxonCaptureRig.Unity.IL2CPP.dll",
                                     "dxgi.dll", "voxon_capture_ue.addon64", "ReShade.ini", "voxon_capture_ue.ini" })
            Check(File.Exists(Path.Combine(dir, f)), $"{f} is there");
        Check(!File.Exists(Path.Combine(dir, "BepInEx", "core", "BepInEx.dll"))
              && !File.Exists(Path.Combine(dir, "BepInEx", "plugins", "VoxonCaptureRig", "VoxonCaptureRig.Unity.dll")),
              "not BepInEx 5, nor the rig's Mono build");

        var once = Snapshot(dir);
        once.Remove(RigInstaller.RecordName);
        RigInstaller.Install(UnityGame, dir);
        var twice = Snapshot(dir);
        twice.Remove(RigInstaller.RecordName);
        Check(Same(once, twice), "installing again changes nothing");
        Diff(once, twice);

        // What BepInEx 6 writes on its first run: the game's interop assemblies, the Unity libraries they were made from.
        string b = Path.Combine(dir, "BepInEx");
        foreach (string f in new[] { @"cache\chainloader_typeloader.dat", @"config\BepInEx.cfg", @"interop\Assembly-CSharp.dll",
                                     @"unity-libs\2021.3.33.zip", "LogOutput.log", "ErrorLog.log" })
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(b, f))!);
            File.WriteAllText(Path.Combine(b, f), "a run");
        }
        File.WriteAllText(Path.Combine(dir, "ReShade.log"), "a run");
        File.WriteAllText(Path.Combine(dir, "voxon_capture_ue.log"), "a run");
        RigInstaller.Remove(UnityGame, dir);
        var after = Snapshot(dir);
        Check(Same(before, after), "Remove leaves the folder exactly as it was, after a run too");
        Diff(before, after);
        Check(!Directory.Exists(b) && !Directory.Exists(Path.Combine(dir, "dotnet")), "...BepInEx's folder and its .NET included");

        // An IL2CPP game with a BepInEx for Mono games in it: refused, as nothing could load the rig.
        UnityFolder(withBepInEx5);
        File.WriteAllText(Path.Combine(withBepInEx5, "GameAssembly.dll"), "not a game");
        Directory.CreateDirectory(Path.Combine(withBepInEx5, "BepInEx", "core"));
        File.WriteAllText(Path.Combine(withBepInEx5, "winhttp.dll"), "their doorstop");
        File.WriteAllText(Path.Combine(withBepInEx5, "BepInEx", "core", "BepInEx.dll"), "their BepInEx 5");
        var theirs = Snapshot(withBepInEx5);
        bool refused = false;
        try { RigInstaller.Install(UnityGame, withBepInEx5); } catch (InvalidOperationException) { refused = true; }
        Check(refused, "with a BepInEx for Mono games in it: Install refuses");
        Check(Same(theirs, Snapshot(withBepInEx5)), "...before changing anything");
    }

    private static void ForeignWinhttp(string dir)
    {
        Say("\n7. A Unity game whose winhttp.dll belongs to another mod");
        UnityFolder(dir);
        File.WriteAllText(Path.Combine(dir, "winhttp.dll"), "some other mod's proxy");
        var before = Snapshot(dir);
        bool refused = false;
        try { RigInstaller.Install(UnityGame, dir); } catch (InvalidOperationException) { refused = true; }
        Check(refused, "Install refuses");
        Check(Same(before, Snapshot(dir)), "...before changing anything");
    }

    // ── The pipe, against a fake add-on ─────────────────────────────────────────────────────────────

    // ── Every game's starting settings: they save and read back, every value a number ───────────────

    private static void GameSettings()
    {
        Say("\n18. Every game's starting settings save and read back");
        foreach (GameDef g in Games.Load(AppPaths.GamesJson))
        {
            try
            {
                RigSettings s = RigSettings.Defaults(g);
                var back = System.Text.Json.JsonSerializer.Deserialize<RigSettings>(s.ToJson());
                Check(back != null && back.ExposureMin == s.ExposureMin && back.ExposureMax == s.ExposureMax
                      && !float.IsNaN(s.ExposureMin) && !float.IsNaN(s.ExposureMax) && s.ExposureMin <= s.ExposureMax,
                      $"{g.Name}: auto-exposure {s.ExposureMin:+0.##;-0.##;0} to {s.ExposureMax:+0.##;-0.##;0} stops");
                Check(back != null && back.SolidOpacity == s.SolidOpacity && s.SolidOpacity >= 0f && s.SolidOpacity <= 1f,
                      $"{g.Name}: see-through surfaces solid from {s.SolidOpacity:0%} opaque");
            }
            catch (Exception ex) { Check(false, $"{g.Name}: {ex.Message}"); }
        }
    }

    private static void Feed()
    {
        Say("\n17. The pipe: volumes from a fake add-on");
        const string pipe = "voxon_capture_rig_selftest";
        using var server = new FeedServer(pipe);
        server.Start();
        using var client = new NamedPipeClientStream(".", pipe, PipeDirection.Out);
        client.Connect(5000);

        var front = new FeedServer.Frame();
        Send(client, 8, []);                                         // Reset
        Send(client, 9, Batch(3, 10));
        Send(client, 42, [1, 2, 3]);                                 // a message type this host does not know
        Send(client, 9, Batch(5, 20));
        Check(WaitTake(server, ref front, 5), "the newest volume arrives whole (5 voxels), an unknown message skipped");
        Check(front.Count == 5 && front.Voxels[4].X == 24f && front.Voxels[4].R == 24, "...with its positions and colours");
        Check(!server.TryTake(ref front), "the same volume is not handed over twice");
        Send(client, 9, Batch(2, 30));
        Check(WaitTake(server, ref front, 2) && front.Voxels[1].X == 31f, "the next one follows");
        Check(!server.TryTake(ref front), "and the volume handed back is never handed out again");
    }

    private static byte[] Batch(int count, int first)
    {
        var b = new byte[4 + count * 15];
        BinaryPrimitives.WriteInt32LittleEndian(b, count);
        for (int i = 0; i < count; i++)
        {
            int o = 4 + i * 15;
            BinaryPrimitives.WriteSingleLittleEndian(b.AsSpan(o), first + i);
            BinaryPrimitives.WriteSingleLittleEndian(b.AsSpan(o + 4), -(first + i));
            BinaryPrimitives.WriteSingleLittleEndian(b.AsSpan(o + 8), 0.5f);
            b[o + 12] = (byte)(first + i); b[o + 13] = 1; b[o + 14] = 2;
        }
        return b;
    }

    private static void Send(Stream s, byte type, byte[] payload)
    {
        var m = new byte[4 + 2 + payload.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(m, (uint)(2 + payload.Length));
        m[4] = type; m[5] = 1;
        payload.CopyTo(m, 6);
        s.Write(m);
        s.Flush();
    }

    private static bool WaitTake(FeedServer server, ref FeedServer.Frame front, int count)
    {
        for (int t = 0; t < 200; t++)
        {
            if (server.TryTake(ref front) && front.Count == count) return true;
            Thread.Sleep(10);
        }
        return false;
    }

    // ── REDengine 3 (The Witcher 3): a script mod in mods\, the player script patched from the game's own ─────────────

    private static readonly GameDef Red3 = new() { Id = "witcher3", Name = "Test game (REDengine 3)", Profile = "witcher3",
                                                   Engine = "redengine3", ExeDir = "bin/x64_dx12", Exe = "witcher3.exe" };

    // The game's own player script, as far as the patch cares: the three lines it inserts before, in the game's CRLF.
    private const string Red3Player =
        "statemachine abstract import class CR4Player extends CPlayer\r\n{ \r\n\t\r\n\tprotected \t\tvar pcGamePlayInitialized\t\t\t: bool;\r\n" +
        "\tevent OnSpawned( spawnData : SEntitySpawnData )\r\n\t{\r\n\t\tAddAnimEventCallback('ThrowHoldTest',\t\t\t'OnAnimEvent_ThrowHoldTest');\r\n\t}\r\n" +
        "\ttimer function UnmountCrossbowTimer( dt : float, id : int )\r\n\t{\r\n\t}\r\n}\r\n";

    private static string Red3Folder(string root)
    {
        string exeDir = Path.Combine(root, "bin", "x64_dx12");
        Directory.CreateDirectory(exeDir);
        File.WriteAllText(Path.Combine(exeDir, "witcher3.exe"), "not a game");
        string scripts = Path.Combine(root, "content", "content0", "scripts", "game", "player");
        Directory.CreateDirectory(scripts);
        File.WriteAllText(Path.Combine(scripts, "r4Player.ws"), Red3Player);
        return exeDir;
    }

    private static void Red3Game(string root)
    {
        Say("\n8. A REDengine 3 game with nothing in it");
        string exeDir = Red3Folder(root);
        var before = Snapshot(root);
        Check(Games.ExeDirUnder(Red3, root) == exeDir, @"the game is found by its exe in bin\x64_dx12");

        RigInstaller.Install(Red3, exeDir);
        Check(RigInstaller.StateOf(Red3, exeDir) == RigInstaller.State.Installed, "installed, and says so");
        string mod = Path.Combine(root, "mods", "modVoxonRig", "content", "scripts");
        foreach (string f in new[] { Path.Combine(exeDir, "dxgi.dll"), Path.Combine(exeDir, "voxon_capture_ue.addon64"),
                                     Path.Combine(exeDir, "ReShade.ini"), Path.Combine(exeDir, "voxon_capture_ue.ini"),
                                     Path.Combine(mod, "local", "voxonRig.ws"), Path.Combine(mod, "game", "player", "r4Player.ws") })
            Check(File.Exists(f), $"{Path.GetRelativePath(root, f)} is there");
        string player = File.ReadAllText(Path.Combine(mod, "game", "player", "r4Player.ws"));
        Check(player.Split("VOXON RIG").Length == 4, "the player script has the rig's three lines");
        Check(!player.Replace("\r\n", "").Contains('\n'), "...in the game's own line endings");
        Check(player.IndexOf("timer function VxRigTimer", StringComparison.Ordinal) is int t && t >= 0
              && t < player.IndexOf("timer function UnmountCrossbowTimer", StringComparison.Ordinal),
              "...its timer before the timer it is placed by");
        string cap = File.ReadAllText(Path.Combine(exeDir, "voxon_capture_ue.ini"));
        Check(cap.Contains("engine=redengine3", StringComparison.Ordinal), "the game's settings tell the add-on it is REDengine 3");
        Check(cap.Contains("hostGame=witcher3", StringComparison.Ordinal), "...and name the game");

        string record = Path.Combine("bin", "x64_dx12", RigInstaller.RecordName);
        var once = Snapshot(root);
        once.Remove(record);
        RigInstaller.Install(Red3, exeDir);
        var twice = Snapshot(root);
        twice.Remove(record);
        Check(Same(once, twice), "installing again changes nothing");
        Diff(once, twice);

        foreach (string log in new[] { "ReShade.log", "voxon_capture_ue.log" }) File.WriteAllText(Path.Combine(exeDir, log), "a run");
        RigInstaller.Remove(Red3, exeDir);
        var after = Snapshot(root);
        Check(Same(before, after), "Remove leaves the game's folder exactly as it was, after a run too");
        Diff(before, after);
        Check(!Directory.Exists(Path.Combine(root, "mods")), "...its mods folder gone, as it was not there before");
    }

    private static void Red3Refusals(string root)
    {
        Say("\n9. A REDengine 3 game the rig cannot go into as it stands");
        string exeDir = Red3Folder(root);
        string theirs = Path.Combine(root, "mods", "modTheirs", "content", "scripts", "game", "player");
        Directory.CreateDirectory(theirs);
        File.WriteAllText(Path.Combine(theirs, "r4Player.ws"), "their player script");
        var before = Snapshot(root);
        bool refused = false;
        try { RigInstaller.Install(Red3, exeDir); } catch (InvalidOperationException) { refused = true; }
        Check(refused && Same(before, Snapshot(root)), "another mod replacing the player script: Install refuses, before changing anything");

        Directory.Delete(Path.Combine(root, "mods"), true);
        File.WriteAllText(Path.Combine(root, "content", "content0", "scripts", "game", "player", "r4Player.ws"), "an updated game's script");
        before = Snapshot(root);
        refused = false;
        try { RigInstaller.Install(Red3, exeDir); } catch (InvalidOperationException) { refused = true; }
        Check(refused && Same(before, Snapshot(root)), "a player script the patch does not fit: Install refuses, before changing anything");
    }

    // ── REDengine 4 (Cyberpunk 2077): the loaders, the rig as a Cyber Engine Tweaks mod ─────────────────────────────

    private static readonly GameDef Red4 = new() { Id = "cyberpunk2077", Name = "Test game (REDengine 4)", Profile = "cyberpunk2077",
                                                   Engine = "redengine4", ExeDir = "bin/x64", Exe = "Cyberpunk2077.exe" };

    private static string Red4Folder(string root)
    {
        string exeDir = Path.Combine(root, "bin", "x64");
        Directory.CreateDirectory(exeDir);
        File.WriteAllText(Path.Combine(exeDir, "Cyberpunk2077.exe"), "not a game");
        Directory.CreateDirectory(Path.Combine(root, "archive", "pc", "content"));
        File.WriteAllText(Path.Combine(root, "archive", "pc", "content", "basegame_1_engine.archive"), "the game's own");
        return exeDir;
    }

    /// <summary>A Cyberpunk folder with nothing in it, or with the player's own loaders and a mod of theirs (kept, and
    /// left exactly as they were).</summary>
    private static void Red4Game(string root, bool playerLoaders)
    {
        Say(playerLoaders ? "\n11. A REDengine 4 game with the player's own Cyber Engine Tweaks, RED4ext and Codeware"
                          : "\n10. A REDengine 4 game with nothing in it");
        string exeDir = Red4Folder(root);
        string cet = Path.Combine(exeDir, "plugins", "cyber_engine_tweaks");
        if (playerLoaders)
        {
            Directory.CreateDirectory(Path.Combine(cet, "mods", "theirs"));
            File.WriteAllText(Path.Combine(exeDir, "plugins", "cyber_engine_tweaks.asi"), "their CET");
            File.WriteAllText(Path.Combine(exeDir, "version.dll"), "their CET's loader");
            File.WriteAllText(Path.Combine(cet, "mods", "theirs", "init.lua"), "-- their mod");
            File.WriteAllText(Path.Combine(cet, "config.json"), "{ \"theirs\": true }");
            Directory.CreateDirectory(Path.Combine(root, "red4ext", "plugins", "Codeware"));
            File.WriteAllText(Path.Combine(exeDir, "winmm.dll"), "their RED4ext's loader");
            File.WriteAllText(Path.Combine(root, "red4ext", "RED4ext.dll"), "their RED4ext");
            File.WriteAllText(Path.Combine(root, "red4ext", "plugins", "Codeware", "Codeware.dll"), "their Codeware");
        }
        var before = Snapshot(root);
        Check(Games.ExeDirUnder(Red4, root) == exeDir, @"the game is found by its exe in bin\x64");

        RigInstaller.Install(Red4, exeDir);
        Check(RigInstaller.StateOf(Red4, exeDir) == RigInstaller.State.Installed, "installed, and says so");
        foreach (string f in new[] { Path.Combine(exeDir, "dxgi.dll"), Path.Combine(exeDir, "voxon_capture_ue.addon64"),
                                     Path.Combine(exeDir, "ReShade.ini"), Path.Combine(exeDir, "voxon_capture_ue.ini"),
                                     Path.Combine(cet, "mods", "voxon_rig", "init.lua"),
                                     Path.Combine(exeDir, "version.dll"), Path.Combine(exeDir, "plugins", "cyber_engine_tweaks.asi"),
                                     Path.Combine(exeDir, "winmm.dll"), Path.Combine(root, "red4ext", "RED4ext.dll"),
                                     Path.Combine(root, "red4ext", "plugins", "Codeware", "Codeware.dll") })
            Check(File.Exists(f), $"{Path.GetRelativePath(root, f)} is there");
        if (playerLoaders)
            Check(File.ReadAllText(Path.Combine(exeDir, "plugins", "cyber_engine_tweaks.asi")) == "their CET"
                  && File.ReadAllText(Path.Combine(root, "red4ext", "RED4ext.dll")) == "their RED4ext"
                  && File.ReadAllText(Path.Combine(root, "red4ext", "plugins", "Codeware", "Codeware.dll")) == "their Codeware",
                  "the player's own loaders kept as they were");
        string cap = File.ReadAllText(Path.Combine(exeDir, "voxon_capture_ue.ini"));
        Check(cap.Contains("engine=redengine4", StringComparison.Ordinal), "the game's settings tell the add-on it is REDengine 4");
        Check(cap.Contains("hostGame=cyberpunk2077", StringComparison.Ordinal), "...and name the game");

        string record = Path.Combine("bin", "x64", RigInstaller.RecordName);
        var once = Snapshot(root);
        once.Remove(record);
        RigInstaller.Install(Red4, exeDir);
        var twice = Snapshot(root);
        twice.Remove(record);
        Check(Same(once, twice), "installing again changes nothing");
        Diff(once, twice);

        // A run: what the add-on, ReShade, the loaders and Cyber Engine Tweaks write while the game plays.
        foreach (string log in new[] { "ReShade.log", "voxon_capture_ue.log" }) File.WriteAllText(Path.Combine(exeDir, log), "a run");
        string mod = Path.Combine(cet, "mods", "voxon_rig");
        foreach (string f in new[] { "rig.txt", "db.sqlite3", "voxon_rig.log" }) File.WriteAllText(Path.Combine(mod, f), "a run");
        if (!playerLoaders)
        {
            foreach (string f in new[] { "bindings.json", "config.json", "cyber_engine_tweaks.log", "layout.ini", "scripting.log" })
                File.WriteAllText(Path.Combine(cet, f), "a run");
            Directory.CreateDirectory(Path.Combine(exeDir, "plugins", "address_library"));
            File.WriteAllText(Path.Combine(exeDir, "plugins", "address_library", "version-1.bin"), "a run");
            Directory.CreateDirectory(Path.Combine(root, "red4ext", "logs"));
            File.WriteAllText(Path.Combine(root, "red4ext", "logs", "red4ext-1.log"), "a run");
            File.WriteAllText(Path.Combine(root, "red4ext", "config.ini"), "a run");
            File.WriteAllText(Path.Combine(root, "red4ext", "plugins", "Codeware", "Codeware-1.log"), "a run");
            Directory.CreateDirectory(Path.Combine(root, "red4ext", "plugins", "Codeware", "Persistent"));
            File.WriteAllText(Path.Combine(root, "red4ext", "plugins", "Codeware", "Persistent", "a.dat"), "a run");
        }
        RigInstaller.Remove(Red4, exeDir);
        var after = Snapshot(root);
        Check(Same(before, after), "Remove leaves the game's folder exactly as it was, after a run too");
        Diff(before, after);
        if (!playerLoaders)
            Check(!Directory.Exists(Path.Combine(exeDir, "plugins")) && !Directory.Exists(Path.Combine(root, "red4ext")),
                  "...the loaders' folders gone, as they were not there before");
    }

    // -- Frostbite (Star Wars Battlefront II), The Sims 4 and Crysis: no loader, the add-on alone --------------------

    // Not the games' own exes: they may be running.
    private static readonly GameDef Frostbite = new() { Id = "bf2", Name = "Test game (Frostbite)", Profile = "bf2",
                                                        Engine = "frostbite", Exe = "frostbite_test_game.exe" };
    private static readonly GameDef Sims4 = new() { Id = "sims4", Name = "Test game (The Sims 4)", Profile = "sims4",
                                                    Engine = "sims4", Exe = "sims4_test_game.exe" };
    private static readonly GameDef Crysis = new() { Id = "crysis", Name = "Test game (Crysis)", Profile = "crysis",
                                                     Engine = "cryengine2", Exe = "crysis_test_game.exe" };

    private static void AddonOnlyGame(string dir, GameDef g, string title)
    {
        Say($"\n{title}: ReShade, the add-on and its settings, nothing else");
        Directory.CreateDirectory(Path.Combine(dir, "Data"));
        File.WriteAllText(Path.Combine(dir, g.Exe), "not a game");
        File.WriteAllText(Path.Combine(dir, "Data", "layout.toc"), "the game's own");
        var before = Snapshot(dir);
        Check(Games.ExeDirUnder(g, dir) == dir, "the game is found by its exe");
        Check(RigInstaller.StateOf(g, dir) == RigInstaller.State.NotInstalled, "not installed, and says so");

        RigInstaller.Install(g, dir);
        Check(RigInstaller.StateOf(g, dir) == RigInstaller.State.Installed, "installed, and says so");
        foreach (string f in new[] { "dxgi.dll", "voxon_capture_ue.addon64", "ReShade.ini", "voxon_capture_ue.ini" })
            Check(File.Exists(Path.Combine(dir, f)), $"{f} is there");
        Check(!File.Exists(Path.Combine(dir, "dwmapi.dll")) && !Directory.Exists(Path.Combine(dir, "Mods")), "no loader put in");
        string cap = File.ReadAllText(Path.Combine(dir, "voxon_capture_ue.ini"));
        Check(cap.Contains($"engine={g.Engine}", StringComparison.Ordinal), $"the game's settings tell the add-on its engine ({g.Engine})");
        Check(cap.Contains($"hostGame={g.Id}", StringComparison.Ordinal), "...and name the game");

        var once = Snapshot(dir);
        once.Remove(RigInstaller.RecordName);
        RigInstaller.Install(g, dir);
        var twice = Snapshot(dir);
        twice.Remove(RigInstaller.RecordName);
        Check(Same(once, twice), "installing again changes nothing");
        Diff(once, twice);

        foreach (string log in new[] { "ReShade.log", "voxon_capture_ue.log" }) File.WriteAllText(Path.Combine(dir, log), "a run");
        RigInstaller.Remove(g, dir);
        var after = Snapshot(dir);
        Check(Same(before, after), "Remove leaves the game's folder exactly as it was, after a run too");
        Diff(before, after);
    }

    private static void Red4Refusals(string root)
    {
        Say("\n12. A REDengine 4 game whose loader slots are taken");
        string exeDir = Red4Folder(root);
        File.WriteAllText(Path.Combine(exeDir, "version.dll"), "some other mod");
        var before = Snapshot(root);
        bool refused = false;
        try { RigInstaller.Install(Red4, exeDir); } catch (InvalidOperationException) { refused = true; }
        Check(refused && Same(before, Snapshot(root)), "a version.dll that is not Cyber Engine Tweaks: Install refuses, before changing anything");

        File.Delete(Path.Combine(exeDir, "version.dll"));
        File.WriteAllText(Path.Combine(exeDir, "winmm.dll"), "some other mod");
        before = Snapshot(root);
        refused = false;
        try { RigInstaller.Install(Red4, exeDir); } catch (InvalidOperationException) { refused = true; }
        Check(refused && Same(before, Snapshot(root)), "a winmm.dll that is not RED4ext: Install refuses, before changing anything");
    }

    // ── helpers ─────────────────────────────────────────────────────────────────────────────────────

    private static Dictionary<string, string> Snapshot(string dir) =>
        Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories).ToDictionary(
            f => Path.GetRelativePath(dir, f),
            f => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(f))));

    private static bool Same(Dictionary<string, string> a, Dictionary<string, string> b) =>
        a.Count == b.Count && a.All(kv => b.TryGetValue(kv.Key, out var v) && v == kv.Value);

    private static void Diff(Dictionary<string, string> before, Dictionary<string, string> after)
    {
        foreach (var (k, v) in before) if (!after.TryGetValue(k, out var a) || a != v) Say($"     changed or gone: {k}");
        foreach (var k in after.Keys) if (!before.ContainsKey(k)) Say($"     left behind: {k}");
    }

    // -- Launch game ---------------------------------------------------------------------------------------------------

    private static void Launching(string root)
    {
        Say("\n19. Launch game: what it starts, by where the game came from (nothing is started here)");
        // An Xbox app copy: its package, named from its manifest. Gang Beasts' publisher, whose id Windows gives as
        // s9zt93y1rpe5a (its package family DoubleFineProductionsInc.GangBeasts_s9zt93y1rpe5a).
        const string publisher = "CN=BC286920-E2CE-4A6C-B95D-5A1FA51016AD";
        Check(GameLauncher.PublisherId(publisher) == "s9zt93y1rpe5a", "a package's publisher id, as Windows makes it");
        string xbox = Path.Combine(root, "XboxGames", "Test Game", "Content");
        UnityFolder(xbox);
        File.WriteAllText(Path.Combine(xbox, "appxmanifest.xml"),
            "<?xml version=\"1.0\" encoding=\"utf-8\"?><Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\">" +
            $"<Identity Name=\"Test.Game\" Publisher=\"{publisher}\" Version=\"1.0.0.0\" />" +
            "<Applications><Application Id=\"Game\" Executable=\"GameLaunchHelper.exe\" /></Applications></Package>");
        var (target, how) = GameLauncher.Target(UnityGame, xbox);
        Check(target == @"shell:AppsFolder\Test.Game_s9zt93y1rpe5a!Game", $"an Xbox app copy starts as its package ({target}, through {how})");

        // Any other copy: its exe. One games.json gives a launcher of its own: that, when the player has it.
        string plain = Path.Combine(root, "plain");
        UnityFolder(plain);
        (target, _) = GameLauncher.Target(UnityGame, plain);
        Check(target == Games.GameExe(UnityGame, plain), "a copy no store started: its exe");
        var withLauncher = new GameDef { Id = "test", Name = "Test game", Engine = "unity", Launch = "TestLauncher.exe" };
        (target, how) = GameLauncher.Target(withLauncher, plain);
        Check(target == null && how.Contains("TestLauncher.exe"), "games.json's launcher missing: nothing started, and says why");
        File.WriteAllText(Path.Combine(plain, "TestLauncher.exe"), "not a launcher");
        (target, _) = GameLauncher.Target(withLauncher, plain);
        Check(target == Path.Combine(plain, "TestLauncher.exe"), "games.json's launcher there: that");
        withLauncher.Launch = "steam://rungameid/1";
        (target, _) = GameLauncher.Target(withLauncher, plain);
        Check(target == "steam://rungameid/1", "games.json's link: that");
    }

    private static void Check(bool ok, string what)
    {
        if (!ok) _fail++;
        Say($"  [{(ok ? "PASS" : "FAIL")}] {what}");
    }

    private static void Say(string line) => Out.AppendLine(line);
}
