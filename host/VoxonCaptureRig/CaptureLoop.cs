using System.Diagnostics;
using Voxon.GameKit.Display;
using Voxon.GameKit.Voxelizer;

namespace VoxonCaptureRig;

/// <summary>What the window and the display thread share. The window writes the camera and reads the status; the
/// display thread does the opposite. Plain fields, each written by one side only.</summary>
internal sealed class AppState
{
    public volatile bool Closing;
    public float HAng, VAng, Dist;
    public float HomeH, HomeV, HomeD;          // the SDK's default view, for a double-click to return to
    public volatile bool CameraChanged;
    public volatile bool CameraReady;          // the SDK's default camera is known
    public Action<byte[], int, int>? OnPreviewFrame;
    public volatile string Status = "Starting the display...";
    public int? MaxVps;                         // --max-vps; null: 60 on the simulator, no cap with hardware
    public int Rpm = 600;                       // --no-motor sets -1
    // The display's volume as the Voxon runtime reports it once running (vxl_state_t boundr, boundz): its radius - the
    // half-extent along x and y, for the volume a spinning panel sweeps is round - and its half-height, in display units.
    // 0 until it has; BoundsVersion counts each change.
    public volatile float BoundR, BoundZ;
    public volatile int BoundsVersion;

    // The preview is a FIXED size and the window scales it. Resizing the SDK's preview buffer under it crashed
    // Voxel64.Host inside coreclr (an access violation no catch receives), so it is never resized here.
    public const int PreviewSize = 800;
}

/// <summary>The display thread: the Voxon SDK, the newest volume from the game, and the preview. The SDK is COM STA
/// and must own its thread, so this runs on a background STA thread of its own and the window never touches it.</summary>
internal static class CaptureLoop
{
    public static int Run(AppState s)
    {
        using var feed = new FeedServer(AppPaths.PipeName);
        feed.Start();

        using var display = new VoxonDisplay { Rpm = s.Rpm };
        if (!display.Initialise("Voxon Capture Rig"))
        {
            Log.Write($"display: {display.Error}");
            s.Status = "The Voxon display or simulator could not start - see the Log tab.";
            return 1;
        }
        display.DrawBorder = true;
        Log.Write($"display: LedHost {display.Version}, volume radius {display.BoundRadius:0.00}, half-height {display.BoundHalfZ:0.00}");
        s.HAng = s.HomeH = display.DefaultHAng;
        s.VAng = s.HomeV = display.DefaultVAng;
        s.Dist = s.HomeD = display.DefaultDist;
        s.CameraReady = true;

        // THE DRAW RATE IS CAPPED on the simulator: at most maxVps volumes a second, a new volume drawn as soon as the
        // cap allows, an unchanged one again only IdleRedrawHz times a second. Uncapped, the loop redraws as fast as
        // the simulator goes - each of the game's 30 volumes 3-6 times, 3-3.5 cores of the PC the game is running
        // on: six cameras of 1024 px ran The Ascent at 178 fps beside it and 254-264 without it (2026-10-11). On
        // hardware the device paces the loop and it is left uncapped: whether a unit holds a frame it is not sent
        // again is unchecked. (Voxel64.Host 5f507b3, the same rule.)
        const int IdleRedrawHz = 10;
        var hardware = VoxonHardwareCheck.Check();
        int maxVps = s.MaxVps ?? (hardware.Error == null && hardware.Found == 0 ? 60 : 0);
        long minTicks = maxVps > 0 ? Stopwatch.Frequency / maxVps : 0;
        long idleTicks = Stopwatch.Frequency / IdleRedrawHz;
        Log.Write(maxVps > 0 ? $"display: draws capped at {maxVps} a second ({hardware.Summary})"
                             : $"display: draws not capped ({hardware.Summary})");

        using var pace = new PaceTimer();
        using var preview = new PreviewSurface();
        preview.Resize(AppState.PreviewSize, AppState.PreviewSize);
        var previewClock = Stopwatch.StartNew();
        var report = Stopwatch.StartNew();
        var front = new FeedServer.Frame();
        long lastPresent = 0, volumesAtReport = 0;
        bool pending = false;
        int draws = 0;

        while (!s.Closing && display.Breath())
        {
            pending |= feed.TryTake(ref front) | s.CameraChanged;
            long now = Stopwatch.GetTimestamp();
            long since = now - lastPresent;
            bool redraw = minTicks == 0 || (since >= minTicks && (pending || since >= idleTicks));
            if (redraw)
            {
                display.Present(new ArraySegment<Voxel>(front.Voxels, 0, front.Count));
                lastPresent = now;
                pending = false;
                draws++;
            }

            if (previewClock.ElapsedMilliseconds >= 33 && s.OnPreviewFrame != null)
            {
                previewClock.Restart();
                if (s.CameraChanged)
                {
                    s.CameraChanged = false;
                    display.SetCamera(s.HAng, s.VAng, s.Dist);
                }
                display.RenderPreview(preview);
                s.OnPreviewFrame(preview.Pixels, preview.Width, preview.Height);
            }

            if (report.Elapsed.TotalSeconds >= 1)
            {
                // The display's volume, read every second: the runtime reports it once it has drawn, and again when the
                // simulator's display model is changed.
                float br = display.BoundRadius, bz = display.BoundHalfZ;
                if (br > 0.1f && bz > 0.1f && (br != s.BoundR || bz != s.BoundZ))
                {
                    s.BoundR = br; s.BoundZ = bz;
                    s.BoundsVersion++;
                    Log.Write($"display: the volume is x and y {-br:0.###} to {br:0.###}, z {-bz:0.###} to {bz:0.###} (the runtime's boundr and boundz; boundr2 {display.BoundRadius2:0.###})");
                }
                double secs = report.Elapsed.TotalSeconds;
                long vols = Interlocked.Read(ref feed.VolumesIn);
                double inPerSec = (vols - volumesAtReport) / secs;
                volumesAtReport = vols;
                s.Status = feed.Connected
                    ? $"Game connected   {inPerSec:0.0} volumes/s in   {front.Count:N0} voxels   {draws / secs:0} drawn/s"
                    : $"Waiting for a game   ({draws / secs:0} drawn/s)";
                draws = 0;
                report.Restart();
            }

            if (!redraw) pace.Sleep(1);
        }
        return 0;
    }
}
