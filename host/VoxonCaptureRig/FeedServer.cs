using System.Buffers.Binary;
using System.IO.Pipes;
using Voxon.GameKit.Voxelizer;

namespace VoxonCaptureRig;

/// <summary>Receives finished volumes from the capture add-on in the game, over a named pipe.
///
/// <para>The add-on sends two of GameKit's message types and nothing else: Reset (8) when it connects, and VoxelBatch
/// (9) once a volume - display-space voxels, already transformed and merged on the GPU, 15 bytes each. Each message is a
/// little-endian u32 length, then type, version and payload (VoxelSteamTap s1-unreal/VoxonTapUE/Feed.cpp). Anything
/// else is skipped, so a newer add-on cannot break this host.</para>
///
/// <para>NOTHING IS ALLOCATED PER VOLUME. Volumes are 6-27 MB, 20-30 a second; a fresh buffer each was a full garbage
/// collection 5-7 times a second in GameKit's reader and 18 ms to take one in (fixed there 2026-10-11, GameKit
/// fix/feed-buffer-reuse). Here the message buffer is reused, and the voxels pass between this thread and the display
/// through three frames that change hands without copying: one being filled, one ready, one being drawn.</para>
///
/// <para>The newest volume wins. If the display has not taken the last one when the next is ready, the next replaces
/// it - a volume nobody saw is worth nothing.</para></summary>
internal sealed class FeedServer : IDisposable
{
    public sealed class Frame
    {
        public Voxel[] Voxels = [];
        public int Count;
        public long Seq;
    }

    // The largest volume the add-on can send: its 8 M-voxel capacity at 15 bytes, and the header. Not a limit of its
    // own - only the guard against a corrupt length.
    private const int MaxMessageBytes = 8 * 1024 * 1024 * 15 + 64;
    private const int PerVoxel = 15;

    private readonly string _pipeName;
    private readonly CancellationTokenSource _stop = new();
    private readonly object _gate = new();
    private Frame _filling = new(), _ready = new();
    private long _seq;
    private Task? _task;

    public volatile bool Connected;
    public long VolumesIn;          // Interlocked
    public int LastCount;

    public FeedServer(string pipeName) => _pipeName = pipeName;

    public void Start() => _task = Task.Run(() => RunAsync(_stop.Token));

    /// <summary>Swaps the newest volume into <paramref name="front"/> when there is one it has not had yet.</summary>
    public bool TryTake(ref Frame front)
    {
        lock (_gate)
        {
            // Newer only: after a swap the ready slot holds the frame the display just gave back.
            if (_ready.Seq <= front.Seq) return false;
            (front, _ready) = (_ready, front);
            return true;
        }
    }

    private void Publish()
    {
        lock (_gate)
        {
            _filling.Seq = ++_seq;
            (_filling, _ready) = (_ready, _filling);
        }
        Interlocked.Increment(ref VolumesIn);
    }

    private async Task RunAsync(CancellationToken ct)
    {
        byte[] body = new byte[1 << 20];
        byte[] len = new byte[4];
        while (!ct.IsCancellationRequested)
        {
            try
            {
                using var pipe = new NamedPipeServerStream(_pipeName, PipeDirection.In, 1, PipeTransmissionMode.Byte,
                                                           PipeOptions.Asynchronous, 512 * 1024, 0);
                await pipe.WaitForConnectionAsync(ct).ConfigureAwait(false);
                Connected = true;
                Log.Write("feed: the game connected");
                while (!ct.IsCancellationRequested)
                {
                    if (!await ReadExactAsync(pipe, len, 4, ct).ConfigureAwait(false)) break;
                    uint n = BinaryPrimitives.ReadUInt32LittleEndian(len);
                    if (n < 2 || n > MaxMessageBytes) { Log.Write($"feed: bad message length {n}; reconnecting"); break; }
                    if (body.Length < n) body = new byte[Math.Max(n, body.Length + body.Length / 2)];
                    if (!await ReadExactAsync(pipe, body, (int)n, ct).ConfigureAwait(false)) break;
                    Handle(body.AsSpan(0, (int)n));
                }
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex) { Log.Write($"feed: {ex.Message}"); }
            finally
            {
                if (Connected) Log.Write("feed: the game disconnected");
                Connected = false;
            }
        }
    }

    private void Handle(ReadOnlySpan<byte> m)
    {
        byte type = m[0];
        if (type == 8)                                   // Reset: the add-on has (re)connected - show nothing until it sends
        {
            _filling.Count = 0;
            Publish();
            return;
        }
        if (type != 9 || m.Length < 6) return;           // VoxelBatch only; anything else is not ours to read
        int count = BinaryPrimitives.ReadInt32LittleEndian(m[2..]);
        if (count < 0 || (long)count * PerVoxel > m.Length - 6) { Log.Write($"feed: VoxelBatch of {count} overruns its message"); return; }

        var f = _filling;
        if (f.Voxels.Length < count) f.Voxels = new Voxel[Math.Max(count, f.Voxels.Length + f.Voxels.Length / 2)];
        ReadOnlySpan<byte> src = m.Slice(6, count * PerVoxel);
        Voxel[] dst = f.Voxels;
        for (int i = 0, o = 0; i < count; i++, o += PerVoxel)
            dst[i] = new Voxel(BinaryPrimitives.ReadSingleLittleEndian(src[o..]),
                               BinaryPrimitives.ReadSingleLittleEndian(src[(o + 4)..]),
                               BinaryPrimitives.ReadSingleLittleEndian(src[(o + 8)..]),
                               src[o + 12], src[o + 13], src[o + 14], 255);
        f.Count = count;
        LastCount = count;
        Publish();
    }

    private static async Task<bool> ReadExactAsync(Stream s, byte[] buf, int count, CancellationToken ct)
    {
        int off = 0;
        while (off < count)
        {
            int n = await s.ReadAsync(buf.AsMemory(off, count - off), ct).ConfigureAwait(false);
            if (n == 0) return false;
            off += n;
        }
        return true;
    }

    public void Dispose()
    {
        _stop.Cancel();
        try { _task?.Wait(1000); } catch (Exception) { /* shutting down */ }
        _stop.Dispose();
    }
}
