using System.Globalization;
using System.Text;
using System.Text.Json;

namespace VoxonCaptureRig;

/// <summary>One game's camera settings: what the Rig tab edits and the control file carries to the game. Both
/// halves of the rig in the game - rig.lua (the cameras) and the ReShade add-on (the decode) - read the control file
/// about once a second and take these over the values in the game's voxon_capture_ue.ini.</summary>
internal sealed class RigSettings
{
    /// <summary>Faces on: 0 +X side, 1 +Y side, 2 -X side, 3 -Y side, 4 top, 5 bottom (rig.lua's numbering).</summary>
    public bool[] Faces { get; set; } = [true, true, false, false, true, false];
    public int FaceRes { get; set; } = 512;
    public float RadiusM { get; set; } = 12f;
    public float CamDist { get; set; } = 1f;
    public float YawDeg { get; set; } = 0f;
    public float TiltDeg { get; set; } = 30f;
    /// <summary>Where the player's feet stand in the display, in display units: x left to right, y back to front (display
    /// +Y is toward the viewer), z top to bottom (+Z is down). A VX2-XL is -4 to 4 across and -2 to 2 high.</summary>
    public float FeetX { get; set; } = 0f;
    public float FeetY { get; set; } = 0f;
    public float FeetZ { get; set; } = 1.5f;
    /// <summary>The volume turns with the player's view, which points to the display's back (display -Y). Off, the
    /// world keeps its own orientation and the player turns in it.</summary>
    public bool TurnWithPlayer { get; set; }
    /// <summary>Brightness: EV stops on top of the add-on's auto-exposure (exposureComp in the profile). Sent to the game.</summary>
    public float Exposure { get; set; } = -1f;
    /// <summary>The auto-exposure's range, in stops from the picture as captured: it never darkens by more than
    /// ExposureMin or brightens by more than ExposureMax; both 0 turns it off (exposureMin and exposureMax in the
    /// profile; -10 and +8 where it has none). NaN until known: settings saved before these existed take the profile's.
    /// Sent to the game.</summary>
    public float ExposureMin { get; set; } = float.NaN;
    public float ExposureMax { get; set; } = float.NaN;
    private const float DefaultExposureMin = -10f, DefaultExposureMax = 8f;
    /// <summary>How much of the game's lighting the volume shows, 0-1. Below 1 each camera also captures the surfaces' own
    /// colours, and the add-on mixes the light over them: 0 is the surface colours alone. Sent to the game.</summary>
    public float Lighting { get; set; } = 1f;
    /// <summary>The world's gamma (above 1 lifts the middle tones) and saturation (0 grey, 1 as captured). Sent to the game.</summary>
    public float Gamma { get; set; } = 1f;
    public float Saturation { get; set; } = 1f;
    /// <summary>PLAYER LOOK (Unreal and Unity games): each camera also captures the player alone, so the player can have a look of
    /// its own - stops of brightness on top of the world's, a gamma, a saturation, and a density: how many voxels deep
    /// it is drawn (1: a shell, as the world). Off, no extra captures are made. Sent to the game.</summary>
    public bool PlayerLook { get; set; }
    public float PlayerExposure { get; set; } = 0f;
    public float PlayerGamma { get; set; } = 1f;
    public float PlayerSaturation { get; set; } = 1f;
    public int PlayerDensity { get; set; } = 1;
    /// <summary>SEE-THROUGH SURFACES: a see-through (translucent) material writes no depth, so the cameras put its colour
    /// on whatever is behind it - a lightsaber's blade on the wall, a glass door nowhere. SeeList: the rig lists the ones
    /// it meets (SeeThrough.Read). SolidMaterials: those made opaque for the cameras' depth, by path; SolidAll: every
    /// one. Either makes one more capture a camera. Sent to the game. SolidMaterials is null only until For fills it:
    /// settings saved before it existed take the profile's (a game's known see-through parts, Jedi: Fallen Order's blade).</summary>
    public bool SeeList { get; set; }
    public bool SolidAll { get; set; }
    public List<string> SolidMaterials { get; set; } = null!;
    /// <summary>...solid only where at least this opaque, 0-1 (Unreal games): a lightsaber's mesh is wider than its glowing
    /// core, which its material fades out toward the edge. 0: wherever the mesh is. Above 0, one more capture a camera.
    /// NaN until known: settings saved before it existed take the profile's (solidOpacity; 0 where it has none). Sent to
    /// the game.</summary>
    public float SolidOpacity { get; set; } = float.NaN;
    /// <summary>How much world each of the rig's axes holds, as a multiple of World radius: above 1 more is squashed into
    /// the display along it, below 1 less is stretched. X, Y are the world's at Rig rotation 0. Sent to the game.</summary>
    public float ScaleX { get; set; } = 1f;
    public float ScaleY { get; set; } = 1f;
    public float ScaleZ { get; set; } = 1f;
    /// <summary>The colour range: a voxel's brightness 0-255 drawn as Floor to Ceiling, its hue and saturation kept.
    /// Applied by the add-on, on the GPU, before the voxels are made. 0 and 255 leave it alone. Sent to the game.</summary>
    public int ColorFloor { get; set; } = 0;
    public int ColorCeiling { get; set; } = 255;
    public bool Perspective { get; set; } = true;
    public bool ShowCameras { get; set; }

    public static readonly int[] Resolutions = [256, 512, 640, 768, 1024, 1280, 1536, 1792, 2048, 2560];
    public static readonly string[] FaceNames = ["+X side", "+Y side", "-X side", "-Y side", "Top", "Bottom"];


    /// <summary>The settings saved for a game, else its profile's starting values.</summary>
    public static RigSettings For(GameDef g)
    {
        string saved = Path.Combine(AppPaths.Settings, g.Id + ".json");
        if (File.Exists(saved))
        {
            try
            {
                var s = JsonSerializer.Deserialize<RigSettings>(File.ReadAllText(saved));
                if (s != null && s.Faces.Length == 6)
                {
                    if (float.IsNaN(s.ExposureMin) || float.IsNaN(s.ExposureMax) || s.SolidMaterials == null || float.IsNaN(s.SolidOpacity))
                    {
                        var p = Defaults(g);
                        if (float.IsNaN(s.ExposureMin)) s.ExposureMin = p.ExposureMin;
                        if (float.IsNaN(s.ExposureMax)) s.ExposureMax = p.ExposureMax;
                        s.SolidMaterials ??= p.SolidMaterials;
                        if (float.IsNaN(s.SolidOpacity)) s.SolidOpacity = p.SolidOpacity;
                    }
                    return s;
                }
            }
            catch (Exception ex) { Log.Write($"settings: {saved} unreadable ({ex.Message}); using the profile"); }
        }
        return Defaults(g);
    }

    /// <summary>A game's starting values: its profile's, every setting the profile leaves out at its default.</summary>
    public static RigSettings Defaults(GameDef g) => FromProfile(Path.Combine(AppPaths.Profiles, g.Profile + ".ini"));

    public string ToJson() => JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true });

    public void Save(GameDef g)
    {
        Directory.CreateDirectory(AppPaths.Settings);
        File.WriteAllText(Path.Combine(AppPaths.Settings, g.Id + ".json"), ToJson());
    }

    private static RigSettings FromProfile(string ini)
    {
        var s = new RigSettings { ExposureMin = DefaultExposureMin, ExposureMax = DefaultExposureMax, SolidMaterials = [], SolidOpacity = 0f };
        if (!File.Exists(ini)) return s;
        foreach (var (k, v) in Ini.Read(File.ReadAllText(ini)))
        {
            float f = float.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out float x) ? x : float.NaN;
            switch (k)
            {
                case "faces":
                    s.Faces = new bool[6];
                    foreach (char c in v) if (c >= '0' && c <= '5') s.Faces[c - '0'] = true;
                    break;
                case "faceRes" when !float.IsNaN(f): s.FaceRes = (int)f; break;
                case "radiusM" when !float.IsNaN(f): s.RadiusM = f; break;
                case "camDist" when !float.IsNaN(f): s.CamDist = f; break;
                case "yawDeg" when !float.IsNaN(f): s.YawDeg = f; break;
                case "tiltDeg" when !float.IsNaN(f): s.TiltDeg = f; break;
                case "feetX" when !float.IsNaN(f): s.FeetX = f; break;
                case "feetY" when !float.IsNaN(f): s.FeetY = f; break;
                case "feetZ" when !float.IsNaN(f): s.FeetZ = f; break;
                case "turnWithPlayer" when !float.IsNaN(f): s.TurnWithPlayer = f != 0; break;
                case "exposureComp" when !float.IsNaN(f): s.Exposure = f; break;
                case "exposureMin" when !float.IsNaN(f): s.ExposureMin = f; break;
                case "exposureMax" when !float.IsNaN(f): s.ExposureMax = f; break;
                case "lighting" when !float.IsNaN(f): s.Lighting = Math.Clamp(f, 0f, 1f); break;
                case "gamma" when !float.IsNaN(f): s.Gamma = f; break;
                case "saturation" when !float.IsNaN(f): s.Saturation = f; break;
                case "playerLook" when !float.IsNaN(f): s.PlayerLook = f != 0; break;
                case "playerExposure" when !float.IsNaN(f): s.PlayerExposure = f; break;
                case "playerGamma" when !float.IsNaN(f): s.PlayerGamma = f; break;
                case "playerSaturation" when !float.IsNaN(f): s.PlayerSaturation = f; break;
                case "playerDensity" when !float.IsNaN(f): s.PlayerDensity = (int)f; break;
                case "seeList" when !float.IsNaN(f): s.SeeList = f != 0; break;
                case "solidAll" when !float.IsNaN(f): s.SolidAll = f != 0; break;
                case "solidMaterials": s.SolidMaterials = [.. v.Split('|', StringSplitOptions.RemoveEmptyEntries)]; break;
                case "solidOpacity" when !float.IsNaN(f): s.SolidOpacity = Math.Clamp(f, 0f, 1f); break;
                case "scaleX" when !float.IsNaN(f): s.ScaleX = f; break;
                case "scaleY" when !float.IsNaN(f): s.ScaleY = f; break;
                case "scaleZ" when !float.IsNaN(f): s.ScaleZ = f; break;
                case "colorFloor" when !float.IsNaN(f): s.ColorFloor = (int)f; break;
                case "colorCeiling" when !float.IsNaN(f): s.ColorCeiling = (int)f; break;
                case "perspective" when !float.IsNaN(f): s.Perspective = f != 0; break;
            }
        }
        return s;
    }

    /// <summary>Writes the control file for the game being served. Replaced whole, through a temporary file, so the
    /// game never reads half of one. boundR, boundZ: the display's volume - its radius and half-height in display units,
    /// as the Voxon runtime reports it - which the cameras are framed to.</summary>
    public void WriteControlFile(GameDef g, float boundR, float boundZ, string? exeDir = null)
    {
        var sb = new StringBuilder();
        sb.Append("game=").Append(g.Id).Append('\n');
        sb.Append("ueFaces=");
        for (int i = 0; i < 6; i++) if (Faces[i]) sb.Append(i);
        sb.Append('\n');
        sb.Append(CultureInfo.InvariantCulture, $"ueFaceRes={FaceRes}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueRadius={RadiusM:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueCamDist={CamDist:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueYaw={YawDeg:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueTilt={TiltDeg:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueFeetX={FeetX:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueFeetY={FeetY:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueFeetZ={FeetZ:0.###}\n");
        sb.Append("ueTurn=").Append(TurnWithPlayer ? 1 : 0).Append('\n');
        sb.Append(CultureInfo.InvariantCulture, $"ueExposure={Exposure:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueExposureMin={(float.IsNaN(ExposureMin) ? DefaultExposureMin : ExposureMin):0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueExposureMax={(float.IsNaN(ExposureMax) ? DefaultExposureMax : ExposureMax):0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueLighting={Lighting:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueGamma={Gamma:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueSaturation={Saturation:0.###}\n");
        sb.Append("uePlayerLook=").Append(PlayerLook ? 1 : 0).Append('\n');
        sb.Append(CultureInfo.InvariantCulture, $"uePlayerExposure={PlayerExposure:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"uePlayerGamma={PlayerGamma:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"uePlayerSaturation={PlayerSaturation:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"uePlayerDensity={Math.Clamp(PlayerDensity, 1, 8)}\n");
        sb.Append("ueSeeList=").Append(SeeList ? 1 : 0).Append('\n');
        sb.Append("ueSolidAll=").Append(SolidAll ? 1 : 0).Append('\n');
        sb.Append("ueSolidMaterials=").Append(string.Join('|', SolidMaterials)).Append('\n');
        sb.Append(CultureInfo.InvariantCulture, $"ueSolidOpacity={(float.IsNaN(SolidOpacity) ? 0f : Math.Clamp(SolidOpacity, 0f, 1f)):0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueColorFloor={Math.Clamp(ColorFloor, 0, 255)}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueColorCeiling={Math.Clamp(ColorCeiling, 0, 255)}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueScaleX={ScaleX:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueScaleY={ScaleY:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueScaleZ={ScaleZ:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueBoundR={boundR:0.###}\n");
        sb.Append(CultureInfo.InvariantCulture, $"ueBoundZ={boundZ:0.###}\n");
        sb.Append("ueShowCameras=").Append(ShowCameras ? 1 : 0).Append('\n');
        sb.Append("uePerspective=").Append(Perspective ? 1 : 0).Append('\n');
        string tmp = AppPaths.ControlFile + ".tmp";
        File.WriteAllText(tmp, sb.ToString());
        File.Move(tmp, AppPaths.ControlFile, overwrite: true);
        // BLUEPRINT games: the mod in the game cannot read this file, so it gets the rig worked out, beside the game
        BlueprintBridge.Update(g, this, boundR, boundZ, exeDir);
    }
}

/// <summary>SEE-THROUGH SURFACES: the materials the rig in the game has found, from the file it writes beside the control
/// file (see_through_[game].txt): path, mesh or particle, the actors carrying it, how many components have it now.</summary>
internal sealed record SeeThroughMaterial(string Path, string Kind, string Owners, int Count)
{
    /// <summary>The asset's own name: "/Game/FX/MI_Blade.MI_Blade" -> "MI_Blade".</summary>
    public string Name => Path[(Path.LastIndexOf('.') + 1)..];

    /// <summary>Its folder: "/Game/FX/MI_Blade.MI_Blade" -> "/Game/FX".</summary>
    public string Folder => Path.LastIndexOf('/') > 0 ? Path[..Path.LastIndexOf('/')] : "";

    public static string FileFor(GameDef g) =>
        System.IO.Path.Combine(System.IO.Path.GetDirectoryName(AppPaths.ControlFile) ?? ".", $"see_through_{g.Id}.txt");

    public static List<SeeThroughMaterial> Read(GameDef g)
    {
        var list = new List<SeeThroughMaterial>();
        try
        {
            foreach (string line in File.ReadAllLines(FileFor(g)))
            {
                if (line.StartsWith(';') || line.Length == 0) continue;
                string[] p = line.Split('\t');
                if (p.Length >= 4) list.Add(new(p[0], p[1], p[2], int.TryParse(p[3], out int n) ? n : 0));
            }
        }
        catch (IOException) { }
        return list;
    }
}

/// <summary>key=value lines with ';' comments, the format of the rig's ini and the control file.</summary>
internal static class Ini
{
    public static IEnumerable<(string Key, string Value)> Read(string text)
    {
        foreach (string raw in text.Split('\n'))
        {
            string line = raw;
            int semi = line.IndexOf(';');
            if (semi >= 0) line = line[..semi];
            int eq = line.IndexOf('=');
            if (eq <= 0) continue;
            yield return (line[..eq].Trim(), line[(eq + 1)..].Trim());
        }
    }

    /// <summary>Sets key=value in ini text, keeping the line's comment, or appends it when absent.</summary>
    public static string Set(string text, string key, string value)
    {
        var lines = text.Replace("\r\n", "\n").Split('\n').ToList();
        for (int i = 0; i < lines.Count; i++)
        {
            string t = lines[i].TrimStart();
            if (!t.StartsWith(key + "=", StringComparison.Ordinal) && !t.StartsWith(key + " =", StringComparison.Ordinal)) continue;
            int semi = lines[i].IndexOf(';');
            string comment = semi >= 0 ? "    " + lines[i][semi..] : "";
            lines[i] = $"{key}={value}{comment}";
            return string.Join("\r\n", lines);
        }
        while (lines.Count > 0 && lines[^1].Length == 0) lines.RemoveAt(lines.Count - 1);
        lines.Add($"{key}={value}");
        return string.Join("\r\n", lines) + "\r\n";
    }
}

/// <summary>Where the app keeps things: all beside the exe, so the folder is the whole product and can be moved.</summary>
internal static class AppPaths
{
    public static readonly string Root = AppContext.BaseDirectory;
    public static string Payload => Path.Combine(Root, "payload");
    public static string Profiles => Path.Combine(Payload, "rig", "profiles");
    public static string RigScripts => Path.Combine(Payload, "rig", "unreal");      // the UE4SS mod's Lua
    public static string Red3Rig => Path.Combine(Payload, "rig", "redengine3");     // the Witcher 3 script mod and its patch
    public static string Red4Rig => Path.Combine(Payload, "rig", "redengine4");     // the Cyberpunk 2077 Cyber Engine Tweaks mod
    public static string Redist => Path.Combine(Payload, "redist");
    public static string UnityPlugin => Path.Combine(Redist, "VoxonCaptureRig.Unity.dll");    // rig/unity, built
    public static string BepInExZip => Directory.EnumerateFiles(Redist, "BepInEx_win_x64_*.zip").First();
    // Unity IL2CPP games: BepInEx 6 (LGPL-2.1, with the .NET it runs on) and rig/unity/il2cpp, built.
    public static string UnityIl2CppPlugin => Path.Combine(Redist, "VoxonCaptureRig.Unity.IL2CPP.dll");
    public static string BepInExIl2CppZip => Directory.EnumerateFiles(Redist, "BepInEx-Unity.IL2CPP-win-x64-*.zip").First();
    // REDENGINE4's loaders, each rooted at the game's own folder (all MIT): Cyber Engine Tweaks runs the rig's Lua,
    // RED4ext loads Codeware, whose AddComponent the rig adds its cameras with.
    public static string CetZip => Directory.EnumerateFiles(Redist, "cet_*.zip").First();
    public static string Red4extZip => Directory.EnumerateFiles(Redist, "red4ext-*.zip").First();
    public static string CodewareZip => Directory.EnumerateFiles(Redist, "Codeware-*.zip").First();
    // MINECRAFT: rig/minecraft, built (a Fabric mod), and the decode host it starts beside the game (addon/minecraft_host.cpp).
    public static string MinecraftMod => Path.Combine(Redist, "VoxonCaptureRig.Minecraft.jar");
    public static string MinecraftHost => Path.Combine(Redist, "voxon_capture_mc.exe");
    public static string Settings => Path.Combine(Root, "settings");
    public static string ControlFile => Path.Combine(Root, "control.txt");
    public static string GamesJson => Path.Combine(Root, "games.json");
    public static string LogFile => Path.Combine(Root, "VoxonCaptureRig.log");

    /// <summary>The pipe the add-on sends volumes down. Its own name, not Voxel64.Host's voxon_n64, so the two
    /// hosts can never take each other's games.</summary>
    public const string PipeName = "voxon_capture_rig";
}
