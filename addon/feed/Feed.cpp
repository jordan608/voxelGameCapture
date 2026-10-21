#include "Feed.h"

#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace voxon
{
    // Message types and versions, from PROTOCOL.md. Kept as constants so a mismatch is one edit.
    static const uint8_t kTypeReset = 8, kTypeVoxelBatch = 9;
    static const uint8_t kVerReset = 1, kVerVoxelBatch = 1;

    // ── encoding ─────────────────────────────────────────────────────────────────────────────────

    void FeedWriter::PutU32(uint32_t v)
    {
        _buf.push_back((uint8_t)v);
        _buf.push_back((uint8_t)(v >> 8));
        _buf.push_back((uint8_t)(v >> 16));
        _buf.push_back((uint8_t)(v >> 24));
    }

    void FeedWriter::PutF32(float v)
    {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);          // not a cast: type-punning through a pointer is UB
        PutU32(bits);
    }

    void FeedWriter::Begin(uint8_t type, uint8_t version)
    {
        // Messages ACCUMULATE into the frame buffer rather than each writing itself out. The wire
        // format is unchanged - a frame is still a sequence of length-prefixed messages - but the
        // pipe sees one write per frame instead of one per message, and that write can be
        // asynchronous because nothing after it needs the buffer back.
        _msgStart = _buf.size();
        PutU32(0);                          // length placeholder, back-filled in End()
        PutByte(type);
        PutByte(version);
    }

    void FeedWriter::End()
    {
        // The length counts the bytes that FOLLOW it - type + version + body.
        const uint32_t payload = (uint32_t)(_buf.size() - _msgStart - 4);
        _buf[_msgStart + 0] = (uint8_t)payload;
        _buf[_msgStart + 1] = (uint8_t)(payload >> 8);
        _buf[_msgStart + 2] = (uint8_t)(payload >> 16);
        _buf[_msgStart + 3] = (uint8_t)(payload >> 24);
    }

    void FeedWriter::VoxelBatch(const std::vector<VoxelPoint>& points, size_t start, size_t count)
    {
        Begin(kTypeVoxelBatch, kVerVoxelBatch);
        PutI32((int32_t)count);
        // Written in place, not appended a byte at a time: at 400,000 points and 30 volumes a second the per-byte
        // push_back was the worker's largest cost. The same bytes - floats little-endian, as PutF32 writes them on x64.
        const size_t at = _buf.size();
        _buf.resize(at + count * 15);
        uint8_t* d = _buf.data() + at;
        for (size_t i = start; i < start + count; i++, d += 15)
        {
            const VoxelPoint& p = points[i];
            std::memcpy(d, &p.X, 4); std::memcpy(d + 4, &p.Y, 4); std::memcpy(d + 8, &p.Z, 4);
            d[12] = p.R; d[13] = p.G; d[14] = p.B;
        }
        End();
    }


    void FeedWriter::Reset()
    {
        Begin(kTypeReset, kVerReset);
        End();
    }

    // ── transport ────────────────────────────────────────────────────────────────────────────────

    FeedPipe::~FeedPipe() { Disconnect(); }

    bool FeedPipe::Connect(const std::string& pipeName)
    {
        Disconnect();
        const std::string path = "\\\\.\\pipe\\" + pipeName;

        // FILE_FLAG_OVERLAPPED, so the write really is asynchronous. Without it the OS opens the
        // handle in synchronous mode and every write blocks until the host drains it - which is the
        // exact cost this class exists to avoid.
        HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;

        OVERLAPPED* ov = new OVERLAPPED();
        std::memset(ov, 0, sizeof(OVERLAPPED));
        ov->hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (ov->hEvent == nullptr) { CloseHandle(h); delete ov; return false; }

        _handle = h;
        _overlapped = ov;
        _pending = false;
        return true;
    }

    void FeedPipe::Disconnect()
    {
        if (_pending) WaitForPending();
        if (_overlapped)
        {
            OVERLAPPED* ov = (OVERLAPPED*)_overlapped;
            if (ov->hEvent) CloseHandle(ov->hEvent);
            delete ov;
            _overlapped = nullptr;
        }
        if (_handle)
        {
            CloseHandle((HANDLE)_handle);
            _handle = nullptr;
        }
        _pending = false;
    }

    bool FeedPipe::WaitForPending()
    {
        if (!_pending) return true;
        DWORD written = 0;
        const BOOL ok = GetOverlappedResult((HANDLE)_handle, (OVERLAPPED*)_overlapped, &written, TRUE);
        _pending = false;
        return ok != 0;
    }

    bool FeedPipe::Busy()
    {
        if (!_pending) return false;
        DWORD written = 0;
        if (GetOverlappedResult((HANDLE)_handle, (OVERLAPPED*)_overlapped, &written, FALSE))
        {
            _pending = false;
            return false;
        }
        return GetLastError() == ERROR_IO_INCOMPLETE;
    }

    bool FeedPipe::WaitIdle(unsigned ms)
    {
        if (!_pending) return true;
        WaitForSingleObject(((OVERLAPPED*)_overlapped)->hEvent, ms);
        return !Busy();
    }

    bool FeedPipe::Write(const uint8_t* data, size_t size)
    {
        if (!_handle || size == 0) return _handle != nullptr;
        if (_pending && !WaitForPending()) return false;

        // The buffer is COPIED rather than referenced, because the caller's vector is refilled next
        // frame while the OS is still reading these bytes. That is the same reason the Unity tap
        // swaps two buffers; a copy of a few hundred kB is cheaper than the bug.
        _inFlight.assign(data, data + size);

        OVERLAPPED* ov = (OVERLAPPED*)_overlapped;
        const HANDLE ev = ov->hEvent;
        std::memset(ov, 0, sizeof(OVERLAPPED));
        ov->hEvent = ev;
        ResetEvent(ev);

        DWORD written = 0;
        if (WriteFile((HANDLE)_handle, _inFlight.data(), (DWORD)_inFlight.size(), &written, ov))
        {
            _pending = false;
            return true;
        }
        if (GetLastError() == ERROR_IO_PENDING)
        {
            _pending = true;
            return true;
        }
        return false;
    }
}
