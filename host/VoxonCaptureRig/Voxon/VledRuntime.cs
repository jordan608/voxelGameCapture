using System.Reflection;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using Voxon;

namespace Voxon.GameKit.Display;

/// <summary>
/// Finds the VLED runtime DLLs (<c>ledhost.dll</c>, <c>LedWin.dll</c>), and — when it cannot —
/// reports every place it looked.
/// </summary>
/// <remarks>
/// The SDK ships its own resolver, <c>VLED_CS_Utils.GetVLEDFilePath</c>, and this asks it FIRST so
/// that a machine where it works behaves exactly as it always has. What this adds is the two things
/// that resolver does not do:
///
/// <b>1. It returns an empty string instead of failing.</b> The caller then hands "" to
/// <c>LoadLedHostCS</c>, which calls <c>Path.GetFullPath("")</c>, which throws
/// <c>ArgumentException: The path is empty</c> from inside the play loop. Observed on a second PC:
/// the game loaded its map, its music and its voxel models, and then died with a stack trace about
/// a path parameter — which says nothing about the actual problem, that the display runtime is not
/// installed for this user.
///
/// <b>2. It only looks where the runtime registers itself PER USER.</b> Measured on a working
/// install:
/// <code>
///   HKCU\SOFTWARE\Voxon\VLED   VLED_RUNTIME_PATH = C:\VLED\System\Runtime\    (trailing slash)
///   HKLM\SOFTWARE\Voxon        VLED_RUNTIME_PATH = C:\VLED\System\Runtime     (none)
///   user PATH                  C:\VLED\System\Runtime\
///   machine PATH               C:\VLED\System\Runtime
/// </code>
/// The SDK checks the working directory, then both PATHs, then HKCU only. Its own registry branch
/// concatenates the value and the filename with no separator, so it depends on that trailing
/// backslash being there — HKCU has one, HKLM does not, which is presumably why HKLM was left out.
///
/// The VLED environment installer sets the <i>user</i> PATH and HKCU, and both of those are per
/// account. So a machine where VLED is plainly installed — <c>C:\VLED</c> on disk, HKLM filled in,
/// machine PATH set — still tells a second Windows account that there is no runtime at all. That is
/// the case this class exists for, and the fallbacks below are not guesses: every one of them was
/// read off a real install.
///
/// Nothing here writes to the registry or the environment. Finding a DLL is not a reason to start
/// repairing someone's machine.
/// </remarks>
public static class VledRuntime
{
    /// <summary>Where the installer puts it when nobody chooses otherwise.</summary>
    private const string DefaultRuntime = @"C:\VLED\System\Runtime";

    /// <summary>Registry keys that carry a VLED path, nearest-authority first.</summary>
    /// <remarks>
    /// Both hives and both shapes: HKCU keeps the paths under a <c>VLED</c> subkey, HKLM keeps them
    /// on the <c>Voxon</c> key itself. Read from a real install rather than assumed, and cheap
    /// enough that trying all four costs nothing.
    /// </remarks>
    private static readonly (RegistryKey Hive, string Path)[] Keys =
    [
        (Registry.CurrentUser,  @"SOFTWARE\Voxon\VLED"),
        (Registry.LocalMachine, @"SOFTWARE\Voxon\VLED"),
        (Registry.CurrentUser,  @"SOFTWARE\Voxon"),
        (Registry.LocalMachine, @"SOFTWARE\Voxon"),
    ];

    /// <summary>
    /// The full path to one of the runtime DLLs, or an empty string. <paramref name="looked"/> is
    /// every folder that was tried, in order, so a failure can say where rather than just no.
    /// </summary>
    public static string Find(string fileName, out List<string> looked)
    {
        looked = new List<string>();

        // An escape hatch, for the case where the INSTALLED runtime is the problem: an old version,
        // a half-finished install, or a machine being used to reproduce what a machine without VLED
        // does. Set VLED_USE_BUNDLED=1 and the copy inside the exe is tried first instead of last.
        if (Environment.GetEnvironmentVariable("VLED_USE_BUNDLED") == "1")
        {
            string first = Bundled();
            if (first.Length > 0)
            {
                string candidate = Path.Combine(first, fileName);
                looked.Add($"{candidate}   (bundled in the exe, forced by VLED_USE_BUNDLED)");
                if (File.Exists(candidate)) return Path.GetFullPath(candidate);
            }
        }

        // The SDK's own resolver first, so nothing changes on a machine where it already works.
        // Its answer is still checked: it can return a path it has not verified.
        try
        {
            string sdk = VLED_CS_Utils.GetVLEDFilePath(fileName);
            if (!string.IsNullOrWhiteSpace(sdk))
            {
                looked.Add($"{sdk}   (from the SDK's own resolver)");
                if (File.Exists(sdk)) return Path.GetFullPath(sdk);
            }
        }
        catch (Exception ex)
        {
            // A null PATH variable makes its foreach throw. Not fatal - we have our own places.
            looked.Add($"(the SDK's own resolver failed: {ex.GetType().Name})");
        }

        // Sources are reported even when they are EMPTY, which is the point. A machine with no
        // VLED at all and a machine where VLED belongs to another Windows account both end up
        // here, and the registry is what tells them apart: if HKLM names a runtime folder then it
        // is installed and something else is wrong, and if nothing names one then it is not.
        foreach (var (folder, source) in Folders())
        {
            if (string.IsNullOrWhiteSpace(folder))
            {
                looked.Add($"(nothing set)   {source}");
                continue;
            }

            string candidate;
            try { candidate = Path.Combine(folder.Trim(), fileName); }
            catch { continue; }                      // a malformed registry value is just "no"

            // The same folder arrives from several sources on a normal install, so each candidate
            // is named once and collects the sources that offered it.
            int at = looked.FindIndex(
                l => l.StartsWith(candidate + "   ", StringComparison.OrdinalIgnoreCase));
            if (at >= 0) looked[at] += $", {source}";
            else looked.Add($"{candidate}   {source}");

            if (File.Exists(candidate)) return Path.GetFullPath(candidate);
        }

        return "";
    }

    /// <summary>Folders that might hold the runtime, each with where it came from.</summary>
    private static IEnumerable<(string Folder, string Source)> Folders()
    {
        // Beside our own executable, which is how someone overrides the installed runtime: drop the
        // pair of DLLs next to the exe and they win. The SDK's resolver has the same idea but tests
        // a RELATIVE name, so it finds them only when the working directory happens to be the
        // exe's - which it is not for a shortcut, a scheduled task, or a drag-and-drop.
        yield return (AppContext.BaseDirectory, "(beside the exe)");

        foreach (var target in new[]
                 {
                     EnvironmentVariableTarget.Process,
                     EnvironmentVariableTarget.User,
                     EnvironmentVariableTarget.Machine,
                 })
        {
            // No nullable context in this project, so no annotation: the variable simply starts
            // empty and stays that way if the read is denied.
            string env = "";
            try { env = Environment.GetEnvironmentVariable("VLED_RUNTIME_PATH", target) ?? ""; }
            catch { /* reading the machine block can be denied; that is just "no" */ }
            yield return (env, $"VLED_RUNTIME_PATH ({target})");
        }

        foreach (var (hive, path) in Keys)
        {
            string hiveName = hive == Registry.LocalMachine ? "HKLM" : "HKCU";
            yield return (ReadKey(hive, path, "VLED_RUNTIME_PATH"),
                          $"{hiveName}\\{path} VLED_RUNTIME_PATH");

            // The install root, plus the layout the installer uses under it. Worth having because
            // VLED_INSTALL_PATH is the one value present in every shape of the key.
            string install = ReadKey(hive, path, "VLED_INSTALL_PATH");
            yield return (string.IsNullOrWhiteSpace(install)
                              ? ""
                              : Path.Combine(install.Trim(), "System", "Runtime"),
                          $"{hiveName}\\{path} VLED_INSTALL_PATH");
        }

        yield return (DefaultRuntime, "(the default install location)");

        // LAST, and last on purpose. A machine with VLED installed should use ITS OWN runtime:
        // that is the version matching its ledhost.ini and its display's firmware, and preferring
        // a copy baked in months ago would quietly override an update. The bundle answers "this
        // machine has no VLED at all", it is not an improvement on a machine that has one.
        yield return (Bundled(), "(bundled in the exe)");
    }

    /// <summary>
    /// The three runtime DLLs carried inside this assembly, unpacked to a cache folder - or an
    /// empty string when this build has none.
    /// </summary>
    /// <remarks>
    /// Unpacked to disk rather than loaded from memory because <c>ledhost.dll</c> imports
    /// <c>FTD3XXWU.dll</c> in its IMPORT TABLE: Windows resolves that as ledhost loads, out of
    /// ledhost's own folder, and a static import cannot be satisfied from a byte array. All three
    /// therefore have to sit in one real folder together.
    ///
    /// <c>ledhost.ini</c> is NOT bundled - it is the machine's hardware description rather than a
    /// default; see this project's .csproj for what is in it. An ini placed beside the EXE is copied
    /// in, which is how a machine with no VLED install but a real display attached can still be told
    /// what that display is.
    /// </remarks>
    private static string Bundled()
    {
        var asm = Assembly.GetExecutingAssembly();

        // ledhost and LedWin are required; FTD3XXWU is OPTIONAL and off by default, because it is
        // FTDI's to license rather than Voxon's (see BundleFtdiRuntime in the csproj). Requiring
        // all three would have made leaving it out silently disable the whole fallback instead of
        // shipping two of the three, which is not the same decision.
        //
        // ledhost imports FTD3XXWU statically, so without it in the cache folder the load succeeds
        // only if Windows finds one elsewhere - System32, or PATH, which a Voxon install puts its
        // runtime folder on. That is the trade this default accepts.
        string[] required = ["vled/ledhost.dll", "vled/LedWin.dll"];
        string[] optional = ["vled/FTD3XXWU.dll"];

        long total = 0;
        var names = new List<string>();
        foreach (string n in required)
        {
            using var probe = asm.GetManifestResourceStream(n);
            if (probe == null) return "";                // this build did not bundle them
            total += probe.Length;
            names.Add(n);
        }
        foreach (string n in optional)
        {
            using var probe = asm.GetManifestResourceStream(n);
            if (probe == null) continue;
            total += probe.Length;
            names.Add(n);
        }

        // Keyed by assembly version AND total size, so a rebuilt or updated bundle unpacks afresh
        // rather than the old DLLs being served out of the cache for ever.
        string key = $"{asm.GetName().Version}-{total:x}";
        string target = Path.Combine(CacheRoot(), key);
        string done = Path.Combine(target, ".complete");

        try
        {
            if (!File.Exists(done))
            {
                Directory.CreateDirectory(target);
                foreach (string n in names)
                {
                    using var src = asm.GetManifestResourceStream(n)!;
                    using var dst = File.Create(Path.Combine(target, n[(n.IndexOf('/') + 1)..]));
                    src.CopyTo(dst);
                }
                // Written last, so a half-finished unpack is never taken for a finished one.
                File.WriteAllText(done, key);
            }

            // A configuration beside the exe wins, and is refreshed every run so that editing it
            // takes effect without anyone having to know the cache exists.
            string beside = Path.Combine(AppContext.BaseDirectory, "ledhost.ini");
            if (File.Exists(beside))
                File.Copy(beside, Path.Combine(target, "ledhost.ini"), overwrite: true);
        }
        catch
        {
            return "";                                   // cannot unpack it, so we do not have it
        }

        return target;
    }

    /// <summary>
    /// Where the bundled runtime is unpacked. Must be WRITABLE: ledhost writes its log beside
    /// itself - <c>logfile=ledhost.log</c> in its ini - so a read-only folder would not do.
    /// </summary>
    private static string CacheRoot()
    {
        string bas = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrEmpty(bas)) bas = Path.GetTempPath();
        return Path.Combine(bas, "Voxon.GameKit", "runtime");
    }

    private static string ReadKey(RegistryKey hive, string path, string name)
    {
        try
        {
            using var key = hive.OpenSubKey(path);
            return key?.GetValue(name) as string ?? "";
        }
        catch
        {
            return "";                               // no access is the same as not there
        }
    }

    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode,
               EntryPoint = "LoadLibraryW")]
    private static extern IntPtr LoadLibraryW(string path);

    /// <summary>
    /// Loads the runtime's own dependencies, by full path, out of the folder the runtime was found
    /// in. Call before anything loads ledhost. Returns a reason on failure, or an empty string.
    /// </summary>
    /// <remarks>
    /// <b>ledhost.dll imports FTD3XXWU.dll statically</b> - it is in its import table, so the
    /// loader must satisfy it as ledhost loads. Handing <c>LoadLibrary</c> a full path does NOT
    /// make Windows look for that import beside the DLL it is loading: the dependency search uses
    /// the standard order, which is the process's own directory, System32 and PATH. So a ledhost
    /// sitting anywhere other than a folder already on PATH fails to load, with nothing but
    /// "module not found" to say why.
    ///
    /// Loading the dependency FIRST, by full path, fixes it: the loader keys loaded modules by base
    /// name, so ledhost's import of "FTD3XXWU.dll" matches the copy already in the process
    /// regardless of where it came from.
    ///
    /// This is not only about the bundled copy. A machine whose registry names the runtime folder
    /// but whose PATH does not include it - which is most of the reason
    /// <see cref="Find(string, out List{string})"/> looks in the registry at all - would fail in
    /// exactly the same way. Preloading from whichever folder the runtime was actually found in
    /// makes every route to it work.
    ///
    /// It was missed because the obvious test passed for the wrong reason: a LoadLibrary of the
    /// unpacked ledhost.dll succeeded while an installed runtime was still on PATH, which is where
    /// the dependency was quietly coming from. It only failed once the install was renamed away -
    /// i.e. in the one case the bundle exists to serve.
    /// </remarks>
    public static string PreloadDependencies(string runtimeFolder)
    {
        if (string.IsNullOrEmpty(runtimeFolder)) return "";

        // Named rather than globbed: these are the imports read off the DLLs themselves, and a
        // folder-wide preload would also drag in whatever else an install happens to keep there.
        foreach (string dep in new[] { "FTD3XXWU.dll" })
        {
            string at = Path.Combine(runtimeFolder, dep);
            if (!File.Exists(at)) continue;              // an install may satisfy it another way

            if (LoadLibraryW(at) == IntPtr.Zero)
                return $"{dep} in {runtimeFolder} would not load " +
                       $"(win32 error {Marshal.GetLastWin32Error()})";
        }
        return "";
    }

    /// <summary>What to tell someone when the runtime is not installed.</summary>
    public static string NotFoundMessage(List<string> looked)
    {
        var lines = new List<string>
        {
            "Voxon VLED runtime not found, so the display cannot be driven.",
            "",
            "  This build did not bundle a copy of it, so one has to be installed: get it from",
            "  https://voxon.co/downloads. If it IS installed, it may have been",
            "  installed by a different Windows user - the installer sets the per-user PATH and",
            "  HKCU, so another account sees nothing. Either re-run it as this user, or set",
            "  VLED_RUNTIME_PATH to the folder holding ledhost.dll.",
            "",
            "  The game itself still runs without it: the window, the settings and a PLY snapshot",
            "  of the last frame all work. There is just no volume to send it to.",
            "",
            "  Looked in:",
        };
        lines.AddRange(looked.Select(l => "    " + l));
        return string.Join(Environment.NewLine, lines);
    }
}
