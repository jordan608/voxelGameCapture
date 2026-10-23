using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Xml.Linq;

namespace VoxonCaptureRig;

/// <summary>Starts a game the way the store it came from starts it, so that store's own checks and the player's launch
/// options apply: a Steam copy through Steam, an Epic copy through the Epic Games launcher, an Xbox app copy as its
/// package; any other copy (the EA app's, or one the player pointed the app at) by its exe, from its own folder.
/// games.json's "launch" wins over all of them: a link, or an exe in the game's folder (Crysis: the c1-launcher the
/// player downloads, without which the game does not start on a current PC).</summary>
internal static class GameLauncher
{
    /// <summary>What Launch starts for the game whose exe is in exeDir, and through what; a null target, and why, when
    /// there is nothing to start.</summary>
    public static (string? Target, string How) Target(GameDef g, string exeDir)
    {
        if (!string.IsNullOrEmpty(g.Launch))
        {
            if (g.Launch.Contains("://", StringComparison.Ordinal)) return (g.Launch, "the link games.json gives it");
            string exe = Path.Combine(exeDir, g.Launch);
            return File.Exists(exe) ? (exe, g.Launch)
                                    : (null, $"{g.Launch} is not in the game's folder: the game's notes say where it comes from");
        }
        if (g.SteamAppId is int app && Games.SteamInstall(app) is string steamDir && IsUnder(exeDir, steamDir))
            return ($"steam://rungameid/{app}", "Steam");
        if (Games.EpicLinkFor(exeDir) is string epic) return (epic, "the Epic Games launcher");
        if (XboxAppLink(exeDir) is string xbox) return (xbox, "the Xbox app");
        return Games.GameExe(g, exeDir) is string own ? (own, Path.GetFileName(own)) : (null, "no game exe in its folder");
    }

    /// <summary>Starts what Target chose: a link through Windows, an exe from the game's folder.</summary>
    public static void Start(string target, string exeDir) =>
        Process.Start(new ProcessStartInfo(target) { UseShellExecute = true, WorkingDirectory = exeDir })?.Dispose();

    /// <summary>An Xbox app (GDK) copy's package, from the appxmanifest.xml beside its exe or up to three folders up
    /// (an Unreal game's exe is in Content\&lt;Game&gt;\Binaries\WinStore):
    /// shell:AppsFolder\{name}_{publisher id}!{application id}; null when there is none.</summary>
    public static string? XboxAppLink(string exeDir)
    {
        string? dir = exeDir;
        for (int up = 0; up < 4 && dir != null; up++, dir = Path.GetDirectoryName(dir))
        {
            string manifest = Path.Combine(dir, "appxmanifest.xml");
            if (!File.Exists(manifest)) continue;
            try
            {
                XElement root = XDocument.Load(manifest).Root!;
                XElement? identity = root.Elements().FirstOrDefault(e => e.Name.LocalName == "Identity");
                XElement? application = root.Descendants().FirstOrDefault(e => e.Name.LocalName == "Application");
                string? name = identity?.Attribute("Name")?.Value, publisher = identity?.Attribute("Publisher")?.Value;
                string? id = application?.Attribute("Id")?.Value;
                if (name != null && publisher != null && id != null) return $@"shell:AppsFolder\{name}_{PublisherId(publisher)}!{id}";
            }
            catch (Exception) { /* not a manifest this understands: not launched as a package */ }
            return null;
        }
        return null;
    }

    /// <summary>A package's publisher id, as Windows makes it: the first 8 bytes of the SHA-256 of the publisher's
    /// name (UTF-16), one zero bit added, written 5 bits a letter in Crockford's base 32.</summary>
    public static string PublisherId(string publisher)
    {
        byte[] hash = SHA256.HashData(Encoding.Unicode.GetBytes(publisher));
        var bits = new StringBuilder();
        for (int i = 0; i < 8; i++) bits.Append(Convert.ToString(hash[i], 2).PadLeft(8, '0'));
        bits.Append('0');
        const string letters = "0123456789abcdefghjkmnpqrstvwxyz";
        var id = new StringBuilder();
        for (int i = 0; i < bits.Length; i += 5) id.Append(letters[Convert.ToInt32(bits.ToString(i, 5), 2)]);
        return id.ToString();
    }

    public static bool IsUnder(string path, string folder)
    {
        string p = Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        string f = Path.GetFullPath(folder).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        return p.StartsWith(f, StringComparison.OrdinalIgnoreCase);
    }
}
