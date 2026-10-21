#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace voxon
{
    /// <summary>One voxel, in DISPLAY space, with its colour: what the decode produces and VoxelBatch carries.</summary>
    struct VoxelPoint
    {
        float X, Y, Z;
        unsigned char R, G, B;
    };

    /// <summary>
    /// The two messages of GameKit's wire protocol (GameKit/PROTOCOL.md) the rig sends: Reset when it connects, and
    /// VoxelBatch once a volume. The app reads exactly these (host/VoxonCaptureRig/FeedServer.cs).
    ///
    /// Little-endian throughout. A frame is a sequence of messages, each u32 length (counting the
    /// bytes that FOLLOW it) then type, version, body.
    /// </summary>
    class FeedWriter
    {
    public:
        /// <summary>Type 9. Finished display-space voxels, which the host draws without voxelising
        /// anything - the right message for a camera rig for the same reason MeshChunk is the right
        /// one for a mesh walk: it matches what the producer actually has. A rig samples surfaces on
        /// a grid and gets POINTS; there is no triangle to hand over, and inventing one would mean
        /// the host re-deriving a surface that was never there. 15 bytes each.</summary>
        void VoxelBatch(const std::vector<VoxelPoint>& points, size_t start, size_t count);


        /// <summary>Type 8. Drop cached geometry - level change, teleport, fast travel.</summary>
        void Reset();

        const uint8_t* Data() const { return _buf.data(); }
        size_t Size() const { return _buf.size(); }
        void Clear() { _buf.clear(); }

    private:
        std::vector<uint8_t> _buf;
        size_t _msgStart = 0;

        void Begin(uint8_t type, uint8_t version);
        void End();
        void PutByte(uint8_t v) { _buf.push_back(v); }
        void PutU32(uint32_t v);
        void PutI32(int32_t v) { PutU32((uint32_t)v); }
        void PutF32(float v);
    };

    /// <summary>
    /// The named pipe, written asynchronously.
    ///
    /// **A synchronous write makes the GAME wait for the HOST, which is the one thing the
    /// architecture forbids.** Measured in the Unity tap on Overcooked! 2: `tap 52.55ms = 16.46
    /// collect + 36.09 send` - two thirds of the cost was the pipe refusing more bytes until the
    /// host had drained the last ones, so the game's frame rate was pinned to the voxeliser's.
    /// `ARCHITECTURE.md` §2 is explicit that the game pays almost nothing, and a blocking write
    /// quietly undoes that.
    ///
    /// So the frame is accumulated whole, handed to an overlapped write, and the tap moves on. Two
    /// buffers, swapped, because the next frame must not overwrite bytes the pipe is still reading.
    /// When the host has not finished the previous frame the tap skips this one entirely - see
    /// <see cref="Busy"/>. Dropping a frame of geometry on a display showing the same scene is
    /// invisible; stalling the game for 36ms is not.
    /// </summary>
    class FeedPipe
    {
    public:
        ~FeedPipe();

        bool Connect(const std::string& pipeName);
        void Disconnect();
        bool IsConnected() const { return _handle != nullptr; }

        /// <summary>True while the host is still reading the previous frame. The caller skips the
        /// whole frame - capture included - rather than queueing work that would only wait.</summary>
        bool Busy();

        /// <summary>Waits up to <paramref name="ms"/> for the previous frame to finish; true once nothing is in
        /// flight. For a caller on its own thread, never the game's: see the add-on's worker.</summary>
        bool WaitIdle(unsigned ms);

        /// <summary>Hand the bytes to the pipe and return. Returns false when the pipe broke, which
        /// the caller treats as a disconnect and retries on its own clock.</summary>
        bool Write(const uint8_t* data, size_t size);

    private:
        void* _handle = nullptr;            //< HANDLE, kept opaque so this header stays windows-free
        void* _overlapped = nullptr;        //< OVERLAPPED*, allocated once
        std::vector<uint8_t> _inFlight;     //< the bytes the OS is reading right now
        bool _pending = false;

        bool WaitForPending();
    };
}
