using System.Management;

namespace Voxon.GameKit.Display;

// ═══════════════════════════════════════════════════════════════════════════════════════════════
//  Is the Voxon hardware actually plugged in?
//
//  Ported from the EDes project (Jordan/EDes/Core/VoxonHardwareCheck.cs), which took it in turn
//  from VoxonPlyPlayback. Kept here because every Voxon application wants the same answer before it
//  touches the SDK, and because the device identifiers below are hard-won facts rather than
//  something to rediscover per project.
//
//  It asks Windows Plug-and-Play what is connected BEFORE anything calls LedHostInit or spins the
//  motor, so the SDK never has to cope with a half-connected or absent unit. Self-contained: no
//  settings, no logging, no UI - the caller decides what to do with the verdict.
//
//  A board is considered present if its friendly name OR its hardware id contains any of the
//  board's match strings. The hardware id is the reliable one: "USB\VID_xxxx&PID_yyyy" identifies
//  the chipset exactly, while friendly names vary with the installed driver.
// ═══════════════════════════════════════════════════════════════════════════════════════════════

/// <summary>One physical USB board that has to be present for a Voxon unit to work.</summary>
public sealed record VoxonBoard(
    string DisplayName,
    IReadOnlyList<string> NameMatches,
    IReadOnlyList<string> HardwareIdMatches);

/// <summary>What a check found.</summary>
public sealed class HardwareCheckResult
{
    /// <summary>True only when EVERY board was found.</summary>
    public bool Ok { get; init; }
    public int Found { get; init; }
    public int Required { get; init; }
    /// <summary>Boards that were found, by display name.</summary>
    public IReadOnlyList<string> PresentBoards { get; init; } = [];
    /// <summary>Boards that are missing. Empty when <see cref="Ok"/>.</summary>
    public IReadOnlyList<string> MissingBoards { get; init; } = [];
    /// <summary>Non-null if the PnP query itself failed, which is rare and not the same as
    /// "no hardware" — it means we could not tell either way.</summary>
    public string? Error { get; init; }

    /// <summary>One line for a status display.</summary>
    public string Summary =>
        Error != null ? $"could not query devices: {Error}"
        : Ok ? "Voxon hardware present"
        : $"{Found} of {Required} boards — missing {string.Join(", ", MissingBoards)}";
}

public static class VoxonHardwareCheck
{
    /// <summary>
    /// The FTDI USB 3.0 bridge that carries frame data to the display.
    /// </summary>
    public static readonly VoxonBoard FtdiBridge = new(
        "FTDI USB 3.0 data bridge (FT600)",
        ["FTDI SuperSpeed-FIFO Bridge", "FTDI FT600"],
        ["VID_0403&PID_601E"]);

    /// <summary>The Voxon controller board, which drives the motor and the optics.</summary>
    public static readonly VoxonBoard Controller = new(
        "Voxon Photonics controller board",
        [],
        ["VID_BE3D&PID_FB32"]);

    /// <summary>
    /// Both boards. The VX2 and the VX2-XL expose exactly the SAME two boards, so this check can
    /// confirm a Voxon unit is attached but CANNOT tell you which model it is — which is why the
    /// motor speed is asked of the operator rather than inferred.
    /// </summary>
    public static readonly VoxonBoard[] RequiredBoards = [FtdiBridge, Controller];

    /// <summary>
    /// A single enumeration of connected devices. Costs roughly half a second to a second and a
    /// half on its first call, which is why the caller should run it off the UI thread.
    ///
    /// Each board is tested independently so a partial connection — one cable in — reports WHICH
    /// board is missing rather than a bare failure.
    /// </summary>
    public static HardwareCheckResult Check()
    {
        var devices = new List<(string Name, string Id)>();
        try
        {
            using var searcher = new ManagementObjectSearcher(
                "SELECT Name, DeviceID FROM Win32_PnPEntity");
            using var found = searcher.Get();
            foreach (var o in found)
            {
                string name = (o["Name"]?.ToString() ?? "").Trim();
                string id = (o["DeviceID"]?.ToString() ?? "").Trim();
                if (name.Length > 0 || id.Length > 0) devices.Add((name, id));
            }
        }
        catch (Exception ex)
        {
            // Not the same as "absent": we failed to ask. The caller should say so rather than
            // claiming the hardware is missing.
            return new HardwareCheckResult
            {
                Ok = false, Found = 0, Required = RequiredBoards.Length, Error = ex.Message,
            };
        }

        var present = new List<string>();
        var missing = new List<string>();
        foreach (var board in RequiredBoards)
        {
            if (devices.Any(d => Matches(d.Name, d.Id, board))) present.Add(board.DisplayName);
            else missing.Add(board.DisplayName);
        }

        return new HardwareCheckResult
        {
            Ok = missing.Count == 0,
            Found = present.Count,
            Required = RequiredBoards.Length,
            PresentBoards = present,
            MissingBoards = missing,
        };
    }

    private static bool Matches(string name, string id, VoxonBoard board)
    {
        foreach (string s in board.NameMatches)
            if (s.Length > 0 && (name.Contains(s, StringComparison.OrdinalIgnoreCase) ||
                                 id.Contains(s, StringComparison.OrdinalIgnoreCase)))
                return true;

        foreach (string s in board.HardwareIdMatches)
            if (s.Length > 0 && id.Contains(s, StringComparison.OrdinalIgnoreCase)) return true;

        return false;
    }
}
