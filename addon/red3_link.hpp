// red3_link.hpp - REDENGINE3 (rig/redengine3): the app's settings, handed to the rig's script in the game.
//
// WitcherScript cannot read a file, so the script keeps one object of floats (CVoxonRigLink, voxonRig.ws) whose first
// three are marker values, and counts its own ticks into the fourth. This finds that object in the game's memory - the
// markers in a row, in committed private read-write memory, on a thread of its own - picks the one whose tick count
// moves (a stale copy of the markers does not), and writes the settings after them, then a serial number the script
// watches. Every read and write of the game's memory is guarded: a freed object costs a search, never the game. The
// search reads ordinary memory only, and only the pages already in it: never write-combined or uncached pages, which
// are the GPU's, and never a page the game committed but has not touched - reading one makes Windows give it RAM, and
// the game commits gigabytes it never uses: the first searches grew it by 3 GB and it stalled, paging. The live object
// is in use every frame, so it is always in memory. Only while the game is in play.
#pragma once

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vxcap
{
    constexpr float kLinkMark[3] = { 739147.25f, -1234.0625f, 98765.5f };

    // The object's floats, in the script's declaration order (voxonRig.ws, CVoxonRigLink).
    enum LinkField
    {
        kLinkAlive = 3,     // the script: its ticks
        kLinkSerial,        // this: raised after every write of what follows
        kLinkOn, kLinkRadius, kLinkBoundR, kLinkBoundZ, kLinkScaleX, kLinkScaleY, kLinkScaleZ,
        kLinkFeetX, kLinkFeetY, kLinkFeetZ, kLinkYaw, kLinkTurn, kLinkFaces, kLinkTanH, kLinkTanV,
        kLinkNative,        // 1: this renders the faces itself (red3_native.hpp); the script only reports
        kLinkCount,         // this writes kLinkOn up to here
        kLinkViewsOn = kLinkCount,   // the script reports from here: 1 while it wants the faces (native)
        kLinkCentreX, kLinkCentreY, kLinkCentreZ, kLinkRho,
        kLinkEnd
    };

    // A guarded copy out of, or into, the game's memory: false where the memory has gone.
    inline bool LinkRead(const float* at, float* out, int n)
    {
        __try { memcpy(out, at, n * sizeof(float)); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline bool LinkWrite(float* at, const float* in, int n)
    {
        __try { memcpy(at, in, n * sizeof(float)); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // Every place the three markers stand in a row, 4-byte aligned, in the resident pages of committed private
    // read-write memory. alone: where the first marker stands without the others after it - the script's object laid
    // out some other way. resident, read: the pages looked at, and of them the ones read (in memory).
    inline void FindLinks(std::vector<float*>& out, std::vector<float*>* alone = nullptr, size_t maxHits = 16,
                          uint64_t* resident = nullptr, uint64_t* read = nullptr)
    {
        out.clear();
        if (alone) alone->clear();
        uint32_t m0;
        memcpy(&m0, &kLinkMark[0], 4);
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        const SIZE_T page = si.dwPageSize;
        uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
        MEMORY_BASIC_INFORMATION mbi;
        constexpr SIZE_T kBatch = 1024;                           // pages asked about at once
        std::vector<PSAPI_WORKING_SET_EX_INFORMATION> ws(kBatch);
        std::vector<uint32_t> buf(page / 4);                      // one page at a time, copied out under the guard
        uint64_t looked = 0, got = 0;
        while (p < (uint8_t*)si.lpMaximumApplicationAddress && VirtualQuery(p, &mbi, sizeof mbi) == sizeof mbi)
        {
            uint8_t* base = (uint8_t*)mbi.BaseAddress;
            const SIZE_T size = mbi.RegionSize;
            const DWORD prot = mbi.Protect & 0xFF;
            const bool rw = prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE;
            const bool plain = !(mbi.Protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE));
            if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && rw && plain)
            {
                for (SIZE_T first = 0; first < size; first += kBatch * page)
                {
                    const SIZE_T pages = std::min<SIZE_T>(kBatch, (size - first) / page);
                    for (SIZE_T k = 0; k < pages; k++) ws[k].VirtualAddress = base + first + k * page;
                    if (!QueryWorkingSetEx(GetCurrentProcess(), ws.data(), (DWORD)(pages * sizeof(ws[0])))) continue;
                    for (SIZE_T k = 0; k < pages; k++)
                    {
                        ++looked;
                        if (!ws[k].VirtualAttributes.Valid) continue;     // not in memory: reading it would put it there
                        const uint8_t* from = base + first + k * page;
                        if (from == (const uint8_t*)buf.data()) continue;  // this search's own buffer
                        if (!LinkRead((const float*)from, (float*)buf.data(), (int)(page / 4))) continue;
                        ++got;
                        for (SIZE_T i = 0; i + 2 < page / 4; i++)
                        {
                            if (buf[i] != m0) continue;
                            float f[3];
                            memcpy(f, &buf[i], 12);
                            if (f[1] == kLinkMark[1] && f[2] == kLinkMark[2])
                            {
                                out.push_back((float*)from + i);
                                if (out.size() >= maxHits) { if (resident) *resident = looked; if (read) *read = got; return; }
                            }
                            else if (alone && alone->size() < 4) alone->push_back((float*)from + i);
                        }
                    }
                }
            }
            p = base + size;
        }
        if (resident) *resident = looked;
        if (read) *read = got;
    }

    class Red3Link
    {
    public:
        // The settings to hand over, kLinkOn onward; written when they change, or when the object is found again.
        void Set(const float values[kLinkCount])
        {
            std::lock_guard<std::mutex> lk(_mtx);
            if (memcmp(values + kLinkOn, _want + kLinkOn, (kLinkCount - kLinkOn) * sizeof(float)) == 0) return;
            memcpy(_want, values, sizeof _want);
            _dirty = true;
        }

        void Start()
        {
            if (_thread.joinable()) return;
            _quit = false;
            _thread = std::thread([this] { Loop(); });
        }
        void Stop()
        {
            _quit = true;
            if (_thread.joinable()) _thread.detach();   // DllMain may call this: never joined there
        }

        bool Found() const { return _at.load() != nullptr; }

        // What the script reports, kLinkViewsOn onward; false with no live link.
        bool Report(float out[kLinkEnd - kLinkViewsOn])
        {
            float* at = _at.load();
            return at && LinkRead(at + kLinkViewsOn, out, kLinkEnd - kLinkViewsOn);
        }

        // Searching is allowed only while the game is in play (a frame's camera was found): not at start-up or loading.
        void AllowSearch(bool allow) { _allow = allow; }
        std::string Status()
        {
            std::lock_guard<std::mutex> lk(_mtx);
            return _status;
        }

    private:
        void Say(const std::string& s) { std::lock_guard<std::mutex> lk(_mtx); _status = s; }

        void Loop()
        {
            std::vector<float*> hits, alone;
            while (!_quit)
            {
                float* at = _at.load();
                if (!at && !_allow)
                {
                    Say("waiting for the game to be in play before searching for the script's link object");
                    Sleep(200);
                    continue;
                }
                if (!at)
                {
                    Say("searching for the script's link object");
                    uint64_t looked = 0, got = 0;
                    FindLinks(hits, &alone, 16, &looked, &got);
                    _pagesLooked = looked; _pagesRead = got;
                    // The live one is the one whose tick count moves.
                    std::vector<float> before(hits.size(), 0.0f);
                    for (size_t i = 0; i < hits.size(); i++) LinkRead(hits[i] + kLinkAlive, &before[i], 1);
                    Sleep(300);
                    for (size_t i = 0; i < hits.size() && !at; i++)
                    {
                        float now = 0;
                        if (LinkRead(hits[i] + kLinkAlive, &now, 1) && now != before[i]) at = hits[i];
                    }
                    if (!at)
                    {
                        std::string s = hits.empty() ? "the script's link object is not in memory (is the game in play, the rig's script installed?)"
                                                     : std::to_string(hits.size()) + " link markers found, none ticking (game paused?)";
                        s += " [read " + std::to_string(got) + " of " + std::to_string(looked) + " committed pages: the rest not in memory]";
                        // The first marker alone: the floats round it, to see how the script's object is laid out.
                        for (float* a : alone)
                        {
                            float around[12];
                            if (!LinkRead(a, around, 12)) continue;
                            char line[256];
                            int len = sprintf_s(line, " | first marker alone at %p, then:", (void*)a);
                            for (int j = 1; j < 12 && len > 0 && len < 230; j++) len += sprintf_s(line + len, sizeof line - len, " %g", around[j]);
                            s += line;
                        }
                        Say(s);
                        for (int i = 0; i < 30 && !_quit; i++) Sleep(100);
                        continue;
                    }
                    _at = at;
                    char s[96];
                    sprintf_s(s, "linked to the script at %p", (void*)at);
                    Say(s);
                    std::lock_guard<std::mutex> lk(_mtx);
                    _dirty = true;
                }
                // Still there? Then write what changed, the serial last.
                float mark[3];
                if (!LinkRead(at, mark, 3) || memcmp(mark, kLinkMark, sizeof mark) != 0) { _at = nullptr; continue; }
                float want[kLinkCount];
                bool dirty;
                {
                    std::lock_guard<std::mutex> lk(_mtx);
                    dirty = _dirty;
                    memcpy(want, _want, sizeof want);
                    _dirty = false;
                }
                if (dirty)
                {
                    float serial = 0;
                    LinkRead(at + kLinkSerial, &serial, 1);
                    serial += 1;
                    if (!LinkWrite(at + kLinkOn, want + kLinkOn, kLinkCount - kLinkOn) || !LinkWrite(at + kLinkSerial, &serial, 1))
                        { _at = nullptr; continue; }
                }
                Sleep(15);          // about a frame: the script follows the app's rig-scale keys while they are held
            }
        }

        std::mutex _mtx;
        float _want[kLinkCount] = {};
        bool _dirty = true;
        std::string _status = "not started";
        std::atomic<float*> _at{ nullptr };
        std::atomic<bool> _quit{ false }, _allow{ false };
        std::atomic<uint64_t> _pagesLooked{ 0 }, _pagesRead{ 0 };
        std::thread _thread;
    };
}
