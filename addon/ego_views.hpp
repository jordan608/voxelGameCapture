// ego_views.hpp - F1 2020 (engine=ego; Codemasters' EGO, D3D12): the cube's faces drawn by the game's own reflection
// cubemap, in the same frame as the player's view, from cameras the add-on puts in its views.
//
// F1 2020 build 759689 (F1_2020_dx12.exe; traced 2026-10-23):
//   * With Vehicle Reflections at Ultra High (hardware_settings_config.xml: cubeMapEnabled="true" cubeMapScale="2.0"
//     cubeMapUpdateMode="all") the engine renders a cubemap round the car every frame - in a race and in the showroom:
//     six views, 512 px square, each the full deferred pipeline (G-buffer, lighting, its own depth; GPU markers
//     "Cubemap_0_-X0GbufferExport", "Cubemap_0_+Z0Forward+0").
//   * Every view's camera lives in a view object V, whose camera record is at V+0x10 (this frame) and V+0x160 (the
//     last), 0x150 bytes each (constructor exe+0x1541AA0, copy exe+0x1541B30). A record, from its start: near +0x2C
//     (default 0.2) and far +0x30 (1000); the lens's extents at distance 2, across +0x34 and up +0x38 (2, 2 for 90
//     degrees); 2 over the field of view in radians +0x68 (-1 unset); then 4x4 matrices, row vectors (p' = p M): the
//     projection +0x70 (reversed depth: [sx 0 0 0 / 0 sy 0 0 / 0 0 n/(f-n) -1 / 0 0 nf/(f-n) 0]), world to view +0xB0,
//     and view to world +0xF0 - rows right, up, back (the camera looks along minus the third), place.
//   * The renderer replays, once a frame on its own thread, the commands the game sent it (exe+0x155A0E0 from the
//     per-frame renderer exe+0x15573D0); a view's camera arrives as a lens command and a transform command, whose
//     handler, a virtual function (exe+0x1497AE0: handler, command, payload, extra), finds V through the handler (+0x10
//     -> +8, the game's own lookup) and merges the command into the record. Everything that uses a view's camera reads
//     the record after the replay - the culling, the per-view GPU constants (exe+0x148CEB0), the passes. Both functions
//     are hooked (a 14-byte jump over their first instructions, moved into a trampoline, put in only when they read as
//     expected): the handler to learn which views are the cube's faces and where the car is, the replay to give those
//     views the rig's cameras once it returns to the per-frame renderer.
//   * The cube's targets (voxon_capture_ue.cpp, EgoBind): its colour `cubemap_target_0` (R11G11B10, six layers, nine
//     levels) and the depth every face is drawn with in turn, `cubemap_gbuffer_depth_stencil_0` (R24G8), known by their
//     D3D12 debug names. A face's lighting pass binds its slice with that depth, which then holds the face whole.
//   * A cube face's record, as the game makes it: near 0.001 m, far 200 m, 2 / fov = 4 / pi, a 90-degree square lens,
//     a world matrix along the world's axes, centred on the car (the showroom's: on the turntable car). Its look along
//     the axes says which of the six it is. Once the rig's camera is in it, it no longer reads as one.
// Axes: EGO's world is Frostbite's - metres, right-handed, Y up, a camera's rows right, up, back, place - so
// frostbite_views.hpp's FbRows / FbCamera carry cameras across, and the faces stand where Red3Place stands them.
#pragma once

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

#include "frostbite_views.hpp"
#include "red3_native.hpp"

namespace vxcap
{
    constexpr int kEgoFaces = kRed4Faces;   // REDengine's five places: four sides and the top
    constexpr uint32_t kEgoViewCmdRva = 0x1497AE0;
    // mov [rsp+18h],rsi / mov [rsp+20h],rdi / push rbp / lea rbp,[rsp-510h]: whole instructions, none RIP-relative.
    constexpr uint8_t kEgoViewCmdHead[19] = { 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
                                              0x48, 0x8D, 0xAC, 0x24, 0xF0, 0xFA, 0xFF, 0xFF };
    constexpr size_t kEgoRecords[2] = { 0x10, 0x160 };   // a view's camera records, this frame's and the last
    constexpr size_t kEgoNear = 0x2C, kEgoFar = 0x30, kEgoExtentX = 0x34, kEgoExtentY = 0x38, kEgoFovCode = 0x68,
                     kEgoProj = 0x70, kEgoView = 0xB0, kEgoWorld = 0xF0;
    constexpr float kEgoCubeNear = 0.001f, kEgoCubeFar = 200.0f;

    using EgoViewCmdFn = uint64_t (*)(void* handler, void* command, void* payload, void* extra);

    // The six looks of the game's cube, as the index its record is known by: +X, -X, +Y, -Y, +Z, -Z (EGO's world).
    // The rig's five faces take five of them; the one looking up is left to the game's reflections.
    constexpr int kEgoSlotOfFace[kEgoFaces] = { 0, 1, 4, 5, 3 };

    // The rig face whose camera is in a cube slot (the slot's slice of the cube's colour holds its picture), or -1: the
    // game's own view. The slice is taken to be the slot - the cube's faces in D3D's order, +X -X +Y -Y +Z -Z.
    inline int EgoFaceOfSlot(uint32_t slot, const Settings& c)
    {
        for (int k = 0; k < kEgoFaces; k++) if (kEgoSlotOfFace[k] == (int)slot && c.faceOn[k]) return k;
        return -1;
    }

    // The pack pass's constants for a face: its camera relative to the volume's centre at the rig's yaw 0 (the camera
    // the record was given, FbWorldCamera, less the centre and the turn), the picture its slice of the cube (size
    // square), the lit colour HDR and before the game's exposure.
    inline PackConstants EgoFaceConstants(int face, const Settings& c, const Geometry& g, float exposure, int size)
    {
        const float centre[3] = { 0, 0, 0 };
        PackConstants p = Red3FaceConstants(face, Red4Camera(face, g), centre, 0.0, c, g, size, size, 1, exposure);
        p.DisplayColour = 0;
        return p;
    }

    template <typename T> inline bool EgoRead(const void* at, T& out)
    {
        __try { memcpy(&out, at, sizeof(T)); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // A cube face's record as the game made it: which of the six looks it has, or -1.
    inline int EgoCubeSlot(const uint8_t* rec)
    {
        float r[0x150 / 4];
        if (!EgoRead(rec, r)) return -1;
        auto f = [&](size_t off) { return r[off / 4]; };
        auto is = [](float a, float b, float eps) { return std::fabs(a - b) <= eps; };
        if (!is(f(kEgoNear), kEgoCubeNear, 1e-6f) || !is(f(kEgoFar), kEgoCubeFar, 1e-3f)) return -1;
        if (!is(f(kEgoFovCode), 4.0f / 3.14159265f, 1e-4f)) return -1;
        if (!is(f(kEgoProj), 1, 1e-4f) || !is(f(kEgoProj + 0x14), 1, 1e-4f) || !is(f(kEgoProj + 0x2C), -1, 1e-6f)) return -1;
        const float* back = &r[(kEgoWorld + 0x20) / 4];
        int axis = -1;
        for (int i = 0; i < 3; i++)
        {
            if (is(back[i], 1, 1e-4f) || is(back[i], -1, 1e-4f)) axis = i;
            else if (!is(back[i], 0, 1e-4f)) return -1;
        }
        if (axis < 0) return -1;
        const bool lookPlus = back[axis] < 0;   // the look is minus the back
        return axis * 2 + (lookPlus ? 0 : 1);
    }

    // The place a record's camera stands (EGO's world).
    inline bool EgoRecordPlace(const uint8_t* rec, float out[3])
    {
        float p[4];
        if (!EgoRead(rec + kEgoWorld + 0x30, p)) return false;
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
        return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
    }

    // A camera (REDengine's frame) into a record: its matrices, its lens and its planes; reversed depth, as the game's.
    inline void EgoWriteRecord(uint8_t* rec, const Red3Camera& cam)
    {
        float w[16];
        FbRows(cam, w);   // view to world: right, up, back, place
        const float* r = w; const float* u = w + 4; const float* b = w + 8; const float* p = w + 12;
        auto dot = [](const float* a, const float* c) { return a[0] * c[0] + a[1] * c[1] + a[2] * c[2]; };
        const float v[16] = { r[0], u[0], b[0], 0, r[1], u[1], b[1], 0, r[2], u[2], b[2], 0, -dot(p, r), -dot(p, u), -dot(p, b), 1 };
        const double n = cam.nearM, fa = cam.farM;
        const float P[16] = { cam.sx, 0, 0, 0, 0, cam.sy, 0, 0, 0, 0, (float)(n / (fa - n)), -1, 0, 0, (float)(n * fa / (fa - n)), 0 };
        const float nearFar[2] = { (float)n, (float)fa };
        const float extents[2] = { 2.0f / cam.sx, 2.0f / cam.sy };
        memcpy(rec + kEgoNear, nearFar, sizeof nearFar);
        memcpy(rec + kEgoExtentX, extents, sizeof extents);
        memcpy(rec + kEgoProj, P, sizeof P);
        memcpy(rec + kEgoView, v, sizeof v);
        memcpy(rec + kEgoWorld, w, sizeof w);
    }

    // A function's head turned into a 14-byte jump to a detour; the instructions it covered moved into a trampoline,
    // which Original() runs. Put in only when the head reads as expected; taken out when it is still this hook's.
    class EgoHook
    {
    public:
        bool Install(uint32_t rva, const uint8_t* head, size_t n, void* detour, std::string& err)
        {
            uint8_t* target = (uint8_t*)GetModuleHandleA(nullptr) + rva;
            if (memcmp(target, head, n) != 0) { err = "not where this build has it (another game version?)"; return false; }
            uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!tramp) { err = "no memory for the trampoline"; return false; }
            memcpy(tramp, target, n);
            Red3AbsJump(tramp + n, target + n);
            FlushInstructionCache(GetCurrentProcess(), tramp, 64);
            uint8_t patch[32];
            memset(patch, 0xCC, n);
            Red3AbsJump(patch, detour);
            if (!Red3Write(target, patch, n)) { err = "could not be made writable"; return false; }
            memcpy(_mine, patch, n);
            memcpy(_was, head, n);
            _rva = rva; _n = n; _original = tramp;
            return true;
        }
        void Uninstall()
        {
            if (!_original) return;
            uint8_t* target = (uint8_t*)GetModuleHandleA(nullptr) + _rva;
            if (memcmp(target, _mine, _n) == 0) Red3Write(target, _was, _n);
            _original = nullptr;
        }
        void* Original() const { return _original; }
    private:
        void* _original = nullptr;
        uint32_t _rva = 0;
        size_t _n = 0;
        uint8_t _mine[32] = {}, _was[32] = {};
    };

    // The renderer's replay of the game's commands (exe+0x155A0E0: renderer, queue), and where the per-frame renderer
    // calls it (the calls return to these): every camera command of a frame is in by the time it returns there.
    constexpr uint32_t kEgoReplayRva = 0x155A0E0;
    constexpr uint32_t kEgoReplayReturns[2] = { 0x15579C6, 0x1557A76 };
    // mov rax,rsp / mov [rax+8],rcx / push r13 / push r15 / sub rsp,58h
    constexpr uint8_t kEgoReplayHead[15] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x41, 0x55, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x58 };
    using EgoReplayFn = uint64_t (*)(void* renderer, void* queue);

    // The cube's faces. Their camera commands (the view-camera handler, hooked) say which view objects they are and where
    // the car is: a lens command and a transform command each frame, each merged into the view's record (measured
    // 2026-10-23: written after the handler, the rig's camera was half undone by the next command), so a record holds
    // the game's whole camera only once both are in. The rig's cameras are written once the renderer's replay of the
    // frame's commands has returned to the per-frame renderer - after every command, before anything reads them.
    class EgoViews
    {
    public:
        bool Install(void* viewDetour, void* replayDetour, std::string& err)
        {
            std::string e;
            if (!_view.Install(kEgoViewCmdRva, kEgoViewCmdHead, sizeof kEgoViewCmdHead, viewDetour, e)) { err = "the view-camera handler: " + e; return false; }
            if (!_replay.Install(kEgoReplayRva, kEgoReplayHead, sizeof kEgoReplayHead, replayDetour, e))
            { _view.Uninstall(); err = "the command replay: " + e; return false; }
            const uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
            for (int i = 0; i < 2; i++) _returns[i] = base + kEgoReplayReturns[i];
            return true;
        }
        void Uninstall() { _replay.Uninstall(); _view.Uninstall(); }
        EgoViewCmdFn ViewOriginal() const { return (EgoViewCmdFn)_view.Original(); }
        EgoReplayFn ReplayOriginal() const { return (EgoReplayFn)_replay.Original(); }
        bool IsFrameReplay(void* returnAddress) const
        { return (uintptr_t)returnAddress == _returns[0] || (uintptr_t)returnAddress == _returns[1]; }

        // At present: the settings the next frame's faces are placed by, and whether they are wanted.
        void Plan(const Settings& c, const Geometry& g, bool on, float aspect)
        {
            std::lock_guard<std::mutex> lk(_mtx);
            _plan.c = c; _plan.g = g; _plan.on = on; _aspect = aspect;
        }

        // The renderer's thread, after a camera command: a view whose record holds a cube face of the game's is that
        // slot's (and the cube's centre, the car, is read from it); a view known as a slot's is marked as updated.
        void AfterViewCommand(void* handler)
        {
            uint8_t* view = nullptr;
            uintptr_t h = 0;
            if (!EgoRead((uint8_t*)handler + 0x10, h) || !h || !EgoRead((uint8_t*)h + 8, view) || !view) return;
            uint8_t* rec = view + kEgoRecords[0];
            const int slot = EgoCubeSlot(rec);
            float place[3];
            const bool placed = slot >= 0 && EgoRecordPlace(rec, place);
            std::lock_guard<std::mutex> lk(_mtx);
            if (slot >= 0)
            {
                _views[slot] = view;
                if (placed) { memcpy(_centreEgo, place, sizeof place); _haveCentre = true; }
                _fresh[slot] = true;
                ++_seen[slot];
                return;
            }
            for (int s = 0; s < 6; s++) if (_views[s] == view) { _fresh[s] = true; return; }
            // The player's view: a lens the screen's shape (the mirrors' are wider, the shadows' square), a near plane
            // under the default's, its field of view set; of those, the widest (a close-up camera's is narrower).
            float r[0x150 / 4];
            if (!EgoRead(rec, r) || _aspect <= 0) return;
            const float p00 = r[kEgoProj / 4], p11 = r[(kEgoProj + 0x14) / 4], n = r[kEgoNear / 4], code = r[kEgoFovCode / 4];
            if (!(p00 > 0.1f) || !(p11 > 0.1f) || std::fabs(p11 / p00 / _aspect - 1.0f) > 0.03f || !(n > 0 && n < 0.15f) || !(code > 0)) return;
            if (_havePlayer && p11 >= _playerP11) return;
            const float* back = &r[(kEgoWorld + 0x20) / 4];
            const float look[3] = { -back[0], -back[1], -back[2] };
            FbToR3(look, _player.fwd);
            _playerP11 = p11;
            _havePlayer = true;
        }

        // The renderer's thread, once the frame's commands are in: each rig face's view, updated this frame, given the
        // rig's camera in both its records.
        void AfterCommands()
        {
            std::lock_guard<std::mutex> lk(_mtx);
            if (_plan.on && _haveCentre)
                for (int s = 0; s < 6; s++)
                {
                    if (!_fresh[s] || !_views[s]) continue;
                    const int face = EgoFaceOfSlot((uint32_t)s, _plan.c);
                    if (face < 0) continue;
                    const Red3Camera cam = FaceCamera(face, _centreEgo);
                    if (WriteBoth(_views[s], cam)) ++_placed[s]; else { ++_faults; _views[s] = nullptr; }
                }
            for (bool& f : _fresh) f = false;
            if (_havePlayer) ++_playerSeen;
            _havePlayer = false;
        }

        static bool WriteBoth(uint8_t* view, const Red3Camera& cam)
        {
            __try { for (size_t off : kEgoRecords) EgoWriteRecord(view + off, cam); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // A face's camera round the car: the cube's centre is the car's; the ground under it is chaseDown below.
        Red3Camera FaceCamera(int face, const float placeEgo[3])
        {
            float place[3], feet[3], centre[3];
            FbToR3(placeEgo, place);
            feet[0] = place[0]; feet[1] = place[1]; feet[2] = place[2] - _plan.c.chaseDown;
            // Turning with the player: the player's view's heading (kept while it looks straight up or down, or is not
            // seen this frame); otherwise the rig's own yaw alone.
            const double rho = _havePlayer ? FbRho(_plan.c, _player, _lastView)
                                           : (_plan.c.turnWithPlayer ? _lastView : 0.0) + _plan.c.yawDeg * 3.14159265358979 / 180.0;
            FbCentre(_plan.c, feet, rho, centre);
            memcpy(_feet, feet, sizeof feet);
            memcpy(_centre, centre, sizeof centre);
            _rho = rho;
            return FbWorldCamera(face, centre, rho, _plan.g);
        }

        // For the stats line and the decode: what was seen and placed.
        void Counts(uint64_t seen[6], uint64_t placed[6], uint64_t& faults)
        {
            std::lock_guard<std::mutex> lk(_mtx);
            memcpy(seen, _seen, sizeof _seen); memcpy(placed, _placed, sizeof _placed); faults = _faults;
        }
        void Placement(float centre[3], double& rho, uint64_t& playerSeen)
        {
            std::lock_guard<std::mutex> lk(_mtx);
            memcpy(centre, _centre, sizeof _centre); rho = _rho; playerSeen = _playerSeen;
        }

    private:
        struct Planned { Settings c; Geometry g; bool on = false; };
        EgoHook _view, _replay;
        uintptr_t _returns[2] = {};
        std::mutex _mtx;
        Planned _plan;
        uint8_t* _views[6] = {};
        bool _fresh[6] = {}, _haveCentre = false;
        uint64_t _seen[6] = {}, _placed[6] = {}, _faults = 0;
        float _centreEgo[3] = {}, _feet[3] = {}, _centre[3] = {};
        double _rho = 0, _lastView = 0;
        float _aspect = 0;                      // the screen's width over its height
        Red3Camera _player{};                   // the player's view this frame (its look), when _havePlayer
        float _playerP11 = 0;
        bool _havePlayer = false;
        uint64_t _playerSeen = 0;
    };
}
