using System.Runtime.InteropServices;
using Voxon;

namespace Voxon.GameKit.Display;

/// <summary>
/// A pinned BGRA pixel buffer the SDK's 2D renderer can draw the volume into, for showing the
/// simulator inside an application's own window.
///
/// Owning the buffer is what makes this work without the SDK's LedWin window being visible: LedWin
/// stays 1x1 and off-screen (its framebuffer would otherwise be projected into the volume as a flat
/// sheet, and it would steal keyboard focus), while <c>vxl_rend2d</c> writes here instead.
///
/// ── Why the buffer is bigger than the picture ───────────────────────────────────────────────────
/// Native <c>Rend2D</c> writes slightly PAST <c>pitch * height</c>. The buffer is pinned, so those
/// bytes land on whatever managed object happens to follow it — in a captured dump from the sibling
/// gen1recomp project, another object's method-table pointer. The GC then access-violates reading
/// that pointer during a background sweep and escalates to a fatal
/// <c>ExecutionEngineException</c> with no managed stack, at an unpredictable moment far from the
/// actual corruption.
///
/// The fix is to over-allocate by <see cref="SlackBytes"/> and keep the tile describing the true
/// w/h/pitch, so the overshoot lands in memory we own and the rendered output is unchanged. The
/// slack is filled with <see cref="Canary"/> and re-checked every frame, so the overshoot is
/// MEASURED rather than assumed and a future SDK change that overshoots further cannot quietly go
/// back to corrupting the heap.
///
/// Two rules follow, and both are easy to undo by accident:
///   * <see cref="Clear"/> clears only the USED region. Clearing <c>_pixels.Length</c> would wipe
///     the canary and the check would never fire again.
///   * <see cref="CheckOverrun"/> must run after every <c>Rend2D</c>, and it refills the canary.
///
/// Full writeup, including the dump evidence: <c>docs/VOXON_PORTABLE_FIXES.md</c> §1 in
/// Gen1RecompVoxon.
/// </summary>
public sealed class PreviewSurface : IDisposable
{
    /// <summary>Spare bytes past the picture for <c>Rend2D</c> to overshoot into.</summary>
    public const int SlackBytes = 64 * 1024;

    private const byte Canary = 0xA5;

    private byte[] _pixels = [];
    private GCHandle _pin;
    private tiletype _tile;
    private int _used;
    private int _maxOverrun;
    private bool _slackWarned;

    public int Width { get; private set; }
    public int Height { get; private set; }

    /// <summary>Bytes of <see cref="Pixels"/> that are actually the picture; the rest is slack.</summary>
    public int UsedBytes => _used;

    /// <summary>Largest overshoot past <see cref="UsedBytes"/> measured so far, in bytes.</summary>
    public int MaxOverrun => _maxOverrun;

    /// <summary>The BGRA pixels of the last render. Overwritten each frame — copy before handing
    /// it to another thread. Longer than <c>Width*Height*4</c>; see <see cref="UsedBytes"/>.</summary>
    public byte[] Pixels => _pixels;

    /// <summary>Reallocates if the requested size differs. Call on the render thread.</summary>
    public void Resize(int width, int height)
    {
        width = Math.Max(64, width);
        height = Math.Max(64, height);
        if (width == Width && height == Height && _pin.IsAllocated) return;

        Free();
        Width = width;
        Height = height;
        _used = width * height * 4;
        _pixels = new byte[_used + SlackBytes];
        FillCanary();
        _pin = GCHandle.Alloc(_pixels, GCHandleType.Pinned);
        _tile = new tiletype
        {
            first_pixel = _pin.AddrOfPinnedObject(),
            pitch = (nint)(width * 4),
            width = (nint)width,
            height = (nint)height,
        };
    }

    internal ref tiletype Tile => ref _tile;
    internal bool Ready => _pin.IsAllocated;

    /// <summary>Clears the picture. Deliberately NOT the whole buffer — see the type comment.</summary>
    internal void Clear() => Array.Clear(_pixels, 0, _used);

    /// <summary>
    /// Measures how far past the picture the last <c>Rend2D</c> wrote, then refills the canary.
    /// Scans from the far end, so a clean frame costs one comparison.
    /// </summary>
    internal void CheckOverrun()
    {
        int last = -1;
        for (int i = _pixels.Length - 1; i >= _used; i--)
        {
            if (_pixels[i] != Canary) { last = i; break; }
        }

        if (last >= 0)
        {
            int overrun = last - _used + 1;
            if (overrun > _maxOverrun)
            {
                _maxOverrun = overrun;
                Console.Error.WriteLine(
                    $"[PreviewSurface] Rend2D wrote {overrun} byte(s) past the {_used}-byte picture " +
                    $"(slack {SlackBytes}). Contained; without the slack this corrupts the next " +
                    "managed object and kills the GC.");
            }
            if (overrun > SlackBytes / 2 && !_slackWarned)
            {
                _slackWarned = true;
                Console.Error.WriteLine(
                    $"[PreviewSurface] *** overrun {overrun} exceeds half the slack — RAISE SlackBytes. ***");
            }
            FillCanary();
        }
    }

    private void FillCanary()
    {
        if (_used < _pixels.Length)
            Array.Fill(_pixels, Canary, _used, _pixels.Length - _used);
    }

    private void Free()
    {
        if (_pin.IsAllocated) _pin.Free();
    }

    public void Dispose() => Free();
}
