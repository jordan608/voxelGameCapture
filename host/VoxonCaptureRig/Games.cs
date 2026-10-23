using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Win32;

namespace VoxonCaptureRig;

/// <summary>One supported game, as games.json describes it.</summary>
internal sealed class GameDef
{
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("profile")] public string Profile { get; set; } = "";
    /// <summary>"unreal" (rig/unreal, UE4SS), "unity" (rig/unity, BepInEx), "redengine3" (rig/redengine3, a script mod),
    /// "redengine4" (rig/redengine4, a Cyber Engine Tweaks mod), "frostbite" (no rig module: the add-on adds the faces
    /// as views of the engine's own), "sims4" (no rig module: the add-on has the game's renderer draw the faces from
    /// cameras of its own), "cryengine2" (no rig module: the add-on has the game's 3D engine draw the faces from
    /// cameras of its own), "ego" (Codemasters' EGO engine; no rig module, the add-on works in the game's renderer) or
    /// "blueprint" (an Unreal game whose cameras come in as a Blueprint Loader mod of ours, tools/mcdungeons2-mod):
    /// which rig module, and installer, it takes.</summary>
    [JsonPropertyName("engine")] public string Engine { get; set; } = "unreal";
    [JsonPropertyName("steamAppId")] public int? SteamAppId { get; set; }
    [JsonPropertyName("epicAppName")] public string? EpicAppName { get; set; }
    /// <summary>A registry value holding the install folder, for a game another launcher installs (the EA app, for
    /// the EA games sold on Epic): its key, in full (HKEY_LOCAL_MACHINE\...), and the value's name.</summary>
    [JsonPropertyName("installRegistryKey")] public string? InstallRegistryKey { get; set; }
    [JsonPropertyName("installRegistryValue")] public string? InstallRegistryValue { get; set; }
    /// <summary>The game's folder name in an Xbox app library (XboxGames\<this>\Content), for a PC Game Pass or Microsoft
    /// Store copy installed by the Xbox app.</summary>
    [JsonPropertyName("xboxFolder")] public string? XboxFolder { get; set; }
    [JsonPropertyName("exeDir")] public string ExeDir { get; set; } = "";
    [JsonPropertyName("exe")] public string Exe { get; set; } = "";             // when it is not *-Win64-Shipping.exe
    /// <summary>BLUEPRINT games: the game's ~mods folder, relative to the exe's (default ..\..\Content\Paks\~mods).</summary>
    [JsonPropertyName("modsDir")] public string? ModsDir { get; set; }
    /// <summary>How the app's Launch game starts it, when not the way its store does (GameLauncher): a link, or an exe in
    /// the game's folder.</summary>
    [JsonPropertyName("launch")] public string? Launch { get; set; }
    [JsonPropertyName("ue4ss")] public string Ue4ss { get; set; } = "UE4SS_v3.0.1.zip";   // the UE4SS in redist/ it needs
    [JsonPropertyName("ue4ssHooks")] public string[]? Ue4ssHooks { get; set; }          // only these UE4SS hooks on; null: its defaults
    [JsonPropertyName("engineMajor")] public int? EngineMajor { get; set; }
    [JsonPropertyName("engineMinor")] public int? EngineMinor { get; set; }
    [JsonPropertyName("status")] public string Status { get; set; } = "";
    [JsonPropertyName("graphics")] public string Graphics { get; set; } = "";
    [JsonPropertyName("notes")] public string Notes { get; set; } = "";
}

/// <summary>The supported games, and where each one is installed on this PC.</summary>
internal static class Games
{
    private sealed class GamesFile { [JsonPropertyName("games")] public List<GameDef> List { get; set; } = []; }

    public static IReadOnlyList<GameDef> Load(string path)
    {
        var f = JsonSerializer.Deserialize<GamesFile>(File.ReadAllText(path));
        return f?.List ?? [];
    }

    /// <summary>The folder holding the game's exe - where the loader and ReShade go - or null. A folder the user
    /// picked wins; then Steam, then the Epic launcher, then the registry value games.json names, then the Xbox app's
    /// libraries.</summary>
    public static string? FindExeDir(GameDef g, string? chosenFolder)
    {
        if (chosenFolder != null && ExeDirUnder(g, chosenFolder) is string picked) return picked;
        foreach (string root in InstallRoots(g))
            if (ExeDirUnder(g, root) is string found) return found;
        return null;
    }

    /// <summary>The game exe's folder under an install root, or the root itself when it already is that folder.
    /// Unreal: games.json names it when it is known (checked first: Mortal Kombat 1 has a launcher of the same name in
    /// its root); otherwise the game's exe under a Binaries\Win64, the engine's own folder excluded. Unity: the folder
    /// with an exe beside its own "_Data" folder.</summary>
    public static string? ExeDirUnder(GameDef g, string root)
    {
        if (!Directory.Exists(root)) return null;
        if (g.Engine != "unity" && g.ExeDir.Length > 0)
        {
            string d = Path.Combine(root, g.ExeDir.Replace('/', Path.DirectorySeparatorChar));
            if (GameExe(g, d) != null) return d;
        }
        if (GameExe(g, root) != null) return root;
        if (g.Engine == "unity") return null;
        try
        {
            foreach (string sub in Directory.EnumerateDirectories(root))
            {
                if (Path.GetFileName(sub).Equals("Engine", StringComparison.OrdinalIgnoreCase)) continue;
                string d = Path.Combine(sub, "Binaries", "Win64");
                if (GameExe(g, d) != null) return d;
            }
        }
        catch (Exception) { /* an unreadable folder is just not this game */ }
        return null;
    }

    /// <summary>The game's exe in a folder, or null: an Unreal game's (games.json's "exe", else its
    /// *-Win64-Shipping.exe), or a Unity exe beside its "_Data" folder.</summary>
    public static string? GameExe(GameDef g, string dir)
    {
        if (g.Engine != "unity" && g.Exe.Length > 0)
        {
            string exe = Path.Combine(dir, g.Exe);
            return File.Exists(exe) ? exe : null;
        }
        if (g.Engine != "unity") return ShippingExe(dir);
        if (!Directory.Exists(dir)) return null;
        try
        {
            return Directory.EnumerateFiles(dir, "*.exe").FirstOrDefault(exe =>
                !Path.GetFileName(exe).StartsWith("UnityCrashHandler", StringComparison.OrdinalIgnoreCase)
                && Directory.Exists(Path.Combine(dir, Path.GetFileNameWithoutExtension(exe) + "_Data")));
        }
        catch (Exception) { return null; }
    }

    public static string? ShippingExe(string dir)
    {
        if (!Directory.Exists(dir)) return null;
        try { return Directory.EnumerateFiles(dir, "*-Win64-Shipping.exe").FirstOrDefault(); }
        catch (Exception) { return null; }
    }

    private static IEnumerable<string> InstallRoots(GameDef g)
    {
        if (g.SteamAppId is int app && SteamInstall(app) is string steam) yield return steam;
        if (g.EpicAppName is string epic)
            foreach (string loc in EpicInstalls(epic))
                yield return loc;
        if (g.InstallRegistryKey is string key && g.InstallRegistryValue is string value)
        {
            string? dir = null;
            try { dir = Registry.GetValue(key, value, null) as string; }
            catch (Exception) { /* no such key: not installed this way */ }
            if (!string.IsNullOrEmpty(dir)) yield return dir;
        }
        if (g.XboxFolder is string xbox)
            foreach (string lib in XboxLibraries())
                yield return Path.Combine(lib, xbox, "Content");
    }

    /// <summary>Every Xbox app library: each drive's .GamingRoot names its folders ("RGBX", a count, then UTF-16
    /// paths from the drive's root, each ending in a zero).</summary>
    public static IEnumerable<string> XboxLibraries()
    {
        foreach (DriveInfo d in DriveInfo.GetDrives())
        {
            byte[] bytes;
            try
            {
                if (d.DriveType != DriveType.Fixed || !d.IsReady) continue;
                string file = Path.Combine(d.RootDirectory.FullName, ".GamingRoot");
                if (!File.Exists(file)) continue;
                bytes = File.ReadAllBytes(file);
            }
            catch (Exception) { continue; }       // a drive that cannot be read holds no library
            if (bytes.Length < 8 || bytes[0] != 'R' || bytes[1] != 'G' || bytes[2] != 'B' || bytes[3] != 'X') continue;
            foreach (string rel in System.Text.Encoding.Unicode.GetString(bytes, 8, (bytes.Length - 8) & ~1).Split('\0'))
                if (rel.Length > 0) yield return Path.Combine(d.RootDirectory.FullName, rel);
        }
    }

    /// <summary>Where Steam installed the app, by its manifest in whichever library has it; null when not installed.</summary>
    public static string? SteamInstall(int app)
    {
        foreach (string lib in SteamLibraries())
        {
            string manifest = Path.Combine(lib, "steamapps", $"appmanifest_{app}.acf");
            if (!File.Exists(manifest)) continue;
            string? dir = VdfValue(File.ReadAllText(manifest), "installdir");
            if (dir != null) return Path.Combine(lib, "steamapps", "common", dir);
        }
        return null;
    }

    /// <summary>Every Steam library: the install folder, and each "path" in its libraryfolders.vdf.</summary>
    public static IEnumerable<string> SteamLibraries()
    {
        string? steam = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string
                     ?? Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath", null) as string;
        if (steam == null) yield break;
        steam = steam.Replace('/', Path.DirectorySeparatorChar);
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { steam };
        yield return steam;
        string vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
        if (!File.Exists(vdf)) yield break;
        foreach (string line in File.ReadAllLines(vdf))
        {
            string t = line.Trim();
            if (!t.StartsWith("\"path\"", StringComparison.OrdinalIgnoreCase)) continue;
            string? p = Quoted(t, 1);
            if (p == null) continue;
            p = p.Replace(@"\\", @"\");
            if (seen.Add(p)) yield return p;
        }
    }

    private static IEnumerable<string> EpicInstalls(string appName) =>
        EpicManifests().Where(m => m.AppName == appName).Select(m => m.Location);

    /// <summary>The Epic Games launcher's link that starts the Epic copy installed where exeDir is, or null.</summary>
    public static string? EpicLinkFor(string exeDir)
    {
        foreach (var m in EpicManifests())
            if (GameLauncher.IsUnder(exeDir, m.Location))
                return $"com.epicgames.launcher://apps/{Uri.EscapeDataString($"{m.Namespace}:{m.Item}:{m.AppName}")}?action=launch&silent=true";
        return null;
    }

    // The Epic Games launcher's install manifests: where each game is, and the ids its launch link names.
    private static IEnumerable<(string Location, string AppName, string Namespace, string Item)> EpicManifests()
    {
        string manifests = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                                        "Epic", "EpicGamesLauncher", "Data", "Manifests");
        if (!Directory.Exists(manifests)) yield break;
        foreach (string item in Directory.EnumerateFiles(manifests, "*.item"))
        {
            (string, string, string, string)? m = null;
            try
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(item));
                var root = doc.RootElement;
                string Text(string key) => root.TryGetProperty(key, out var v) ? v.GetString() ?? "" : "";
                if (Text("InstallLocation").Length > 0 && Text("AppName").Length > 0)
                    m = (Text("InstallLocation"), Text("AppName"), Text("CatalogNamespace"), Text("CatalogItemId"));
            }
            catch (Exception) { /* not a manifest this understands */ }
            if (m is { } found) yield return found;
        }
    }

    // "key"  "value" lines, as Steam's .acf and .vdf files have them.
    private static string? VdfValue(string text, string key)
    {
        foreach (string line in text.Split('\n'))
        {
            string t = line.Trim();
            if (t.StartsWith($"\"{key}\"", StringComparison.OrdinalIgnoreCase)) return Quoted(t, 1);
        }
        return null;
    }

    private static string? Quoted(string line, int index)
    {
        var parts = line.Split('"');
        int want = index * 2 + 1;
        return parts.Length > want ? parts[want] : null;
    }
}
