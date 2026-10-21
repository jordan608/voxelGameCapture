// sims4_views.hpp - THE SIMS 4 (engine=sims4): the cube's faces rendered by the game's own renderer, in the same frame
// as the player's view, from cameras the add-on makes.
//
// The Sims 4 1.128.90.1030 (D3D11; traced 2026-10-18 from TS4_x64.exe's RTTI and the frames' draws):
//   * In a lot, each frame the scene manager (EA::Sims4::Renderer::SceneManager, known by its vtable) hands its render
//     job - exe+0x11C4910, a member function taking only the manager - to a worker thread, which records the frame's
//     scene into a D3D11 deferred context. The job is hooked: a 14-byte jump over its first four instructions (18 bytes,
//     none RIP-relative, moved into a trampoline), put in only when those bytes read as expected.
//   * The thumbnail renderer draws the world from a camera of its own with exe+0x11C7CD0 (manager, scene, camera, 0, -1,
//     false, null): every pass, about 1600 draws, recorded into the same deferred context. It draws into the frame's
//     main colour (B8G8R8A8) and depth (D24 S8) - the targets bound with a depth buffer - and clears neither; between
//     its passes it copies the picture so far into targets of its own, setting their viewports and putting back the one
//     it read before. Called after the game's own job, its draws reach the GPU before the frame's own scene, which clears
//     those targets and draws over them: the player's picture is untouched (measured with a face turned half round).
//     So each face is drawn by that routine, with the manager's scene at +0x40 and the flags and mask the manager draws
//     its own view with (+0x710, +0x714: without them the roofs and the levels above the one shown are drawn too), and
//     its binds of the main targets are turned to the face's own (Ts4Bind in voxon_capture_ue.cpp), cleared first.
//     Those are the size of the game's, so every viewport and scissor it sets - and reads back - stays the game's own;
//     only the targets change.
//   * A face's camera is a Sims3::Cameras::Camera (0x700 bytes, 16-aligned, as the game allocates it): a copy of the
//     manager's main camera (+0x60), made each frame in memory of the add-on's own, given the face's place and planes,
//     then rebuilt by the camera's own update (exe+0x1208EB0; false: keep the planes given; null: the view from the rows),
//     which recomputes its projection, view, world to screen and frustum. Its lens is as wide and tall as the game's
//     picture at the face's scale and shifted (Ts4Lens), so that the square at its top left is the face's lens of 90
//     degrees: the decode reads that square (SrcSize). The camera's fields: its kind at +0x10 (0 perspective); the lens
//     as extents at distance 1 - width +0x14, height +0x18 - the horizontal angle +0x1C, the aspect +0x20; near and far
//     +0x24, +0x28; rows right, up, back (it looks along minus the third) and place at +0x30..+0x6F; the lens's centre,
//     off the axis, at +0x1F0 (x, y, at distance 1); and the point it orbits, on the ground it looks at, +0x200. A count
//     at +0x08 (the game's constructor zeroes it atomically) is set high in the copies, so nothing that takes and lets
//     go of a reference can ever free them.
//   * The depth runs forward: 0 at the near plane, 1 at the far one (cleared). The decode reads depth reversed; handed
//     the planes the other way round (Ts4FaceConstants), its formula is the forward one.
// Axes. The game's world: metres, right-handed, Y up, a camera's rows right, up, back and place - Frostbite's frame, so
// frostbite_views.hpp's helpers carry it to REDengine's (Z up) and place the faces as Battlefront II's are placed.
// The volume follows the main camera: the point it orbits stands where the app puts the feet (the ground at a set height
// in the display), and with Turn the volume with the player on, it turns as the camera does.
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
    constexpr int kTs4Faces = kRed4Faces;   // REDengine's five places: four sides and the top
    constexpr uint32_t kTs4LotJobRva = 0x11C4910, kTs4SceneFromCameraRva = 0x11C7CD0, kTs4CameraUpdateRva = 0x1208EB0,
                       kTs4SceneManagerVtRva = 0x2049C58;
    // mov rax,rsp / push rbp / lea rbp,[rax-248h] / sub rsp,340h
    constexpr uint8_t kTs4LotJobHead[18] = { 0x48, 0x8B, 0xC4, 0x55, 0x48, 0x8D, 0xA8, 0xB8, 0xFD, 0xFF, 0xFF,
                                             0x48, 0x81, 0xEC, 0x40, 0x03, 0x00, 0x00 };
    // The scene manager's: the scene the faces are drawn from, its main camera, and the flags and mask of its own view.
    constexpr size_t kTs4ManagerScene = 0x40, kTs4ManagerCamera = 0x60, kTs4ManagerFlags = 0x710, kTs4ManagerMask = 0x714;
    constexpr size_t kTs4CameraBytes = 0x700, kTs4CamCount = 0x08, kTs4CamLens = 0x14, kTs4CamRows = 0x30,
                     kTs4CamCentre = 0x1F0, kTs4CamTarget = 0x200;

    using Ts4JobFn = void (*)(void* manager);
    using Ts4SceneFromCameraFn = void (*)(void* manager, void* scene, void* camera, uint32_t flags, int mask, bool a6, void* a7);
    using Ts4CameraUpdateFn = void (*)(void* camera, bool fromDistance, const float* view);

    // A face's square: the side of the largest square in the game's picture (w x h), its top left corner.
    inline uint32_t Ts4Square(uint32_t w, uint32_t h) { return std::max(1u, std::min(w, h)); }

    // A face camera's lens for a picture of w x h: its width and height at distance 1, and its centre off the axis, so that
    // the square at the picture's top left spans -1 to 1 both ways - a lens of 90 degrees - with the rest beyond it.
    struct Ts4LensShape { float width, height, cx, cy; };
    inline Ts4LensShape Ts4Lens(uint32_t w, uint32_t h)
    {
        const float s = (float)Ts4Square(w, h), ws = w / s, hs = h / s;
        return { 2.0f * ws, 2.0f * hs, ws - 1.0f, 1.0f - hs };
    }

    // The pack pass's constants for a face: the camera Battlefront II's faces have round the centre (Red4Camera), read
    // from the square of the picture (square texels a side; its depth there too), the colour the game's finished picture,
    // the planes swapped for forward depth.
    inline PackConstants Ts4FaceConstants(int face, const Settings& c, const Geometry& g, float exposure, uint32_t square)
    {
        const float centre[3] = { 0, 0, 0 };
        PackConstants p = Red3FaceConstants(face, Red4Camera(face, g), centre, 0.0, c, g, c.faceRes, c.faceRes, 1, exposure);
        p.SrcSize = square;
        p.SrcDepthScaled = 1;
        std::swap(p.DepthNear, p.DepthFar);
        return p;
    }

    // Each face's targets, made by the add-on at present, the size of the game's picture; a set is never changed once
    // handed over, only replaced.
    struct Ts4Targets
    {
        uint64_t rtv[kTs4Faces] = {}, dsv[kTs4Faces] = {};   // the D3D11 views (ReShade's handles are the native pointers)
        uint32_t w = 0, h = 0;
    };

    class Ts4Faces
    {
    public:
        // The game's code checked, the render job hooked; false and why when not.
        bool Install(void* detour, std::string& why)
        {
            _exe = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
            uint8_t* job = reinterpret_cast<uint8_t*>(_exe + kTs4LotJobRva);
            if (memcmp(job, kTs4LotJobHead, sizeof kTs4LotJobHead) != 0)
            {
                why = "not the game build this add-on knows (The Sims 4 1.128.90.1030): its render job is not where it was";
                return false;
            }
            uint8_t* tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            _cams = static_cast<uint8_t*>(VirtualAlloc(nullptr, kTs4CameraBytes * kTs4Faces, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!tramp || !_cams) { why = "no memory for the trampoline or the cameras"; return false; }
            memcpy(tramp, job, sizeof kTs4LotJobHead);
            Red3AbsJump(tramp + sizeof kTs4LotJobHead, job + sizeof kTs4LotJobHead);
            FlushInstructionCache(GetCurrentProcess(), tramp, 64);
            uint8_t patch[sizeof kTs4LotJobHead];
            memset(patch, 0x90, sizeof patch);   // the jump, then nops to the next whole instruction
            Red3AbsJump(patch, detour);
            _original = reinterpret_cast<Ts4JobFn>(tramp);
            if (!Red3Write(job, patch, sizeof patch)) { _original = nullptr; why = "the render job could not be made writable"; return false; }
            memcpy(_mine, patch, sizeof _mine);
            return true;
        }

        // Puts the job's own first bytes back, if the head is still this hook's jump.
        void Uninstall()
        {
            if (!_original) return;
            uint8_t* job = reinterpret_cast<uint8_t*>(_exe + kTs4LotJobRva);
            if (memcmp(job, _mine, sizeof _mine) == 0) Red3Write(job, kTs4LotJobHead, sizeof kTs4LotJobHead);
            _original = nullptr;
        }

        Ts4JobFn Original() const { return _original; }

        // Present thread, each frame: the settings the next faces are drawn by, and whether they are wanted. Returns the
        // frame count the faces drawn from now on are stamped with.
        uint64_t Plan(const Settings& c, const Geometry& g, bool on)
        {
            {
                std::lock_guard<std::mutex> lk(_planMtx);   // no D3D call under it
                _plan.c = c; _plan.g = g; _plan.on = on;
            }
            return _frame.fetch_add(1, std::memory_order_acq_rel) + 1;
        }

        // Present thread: the faces' targets to draw into from now on (null: none). The set handed over before stays
        // alive until the job can no longer be using it - the caller keeps it a few frames.
        void SetTargets(const Ts4Targets* t) { _targets.store(t, std::memory_order_release); }

        // The job thread, after the game's own job: each face that is on, drawn from its camera round the main camera's
        // ground point.
        void DrawFaces(void* manager)
        {
            Planned p;
            {
                std::lock_guard<std::mutex> lk(_planMtx);
                p = _plan;
            }
            ++_jobs;
            const uint64_t frame = _frame.load(std::memory_order_acquire);
            if (!p.on || !_cams || *reinterpret_cast<const uintptr_t*>(manager) != _exe + kTs4SceneManagerVtRva) return;
            uint8_t* m = static_cast<uint8_t*>(manager);
            const uint8_t* main = *reinterpret_cast<uint8_t**>(m + kTs4ManagerCamera);
            void* scene = *reinterpret_cast<void**>(m + kTs4ManagerScene);
            Red3Camera player{};
            if (!main || !scene || !FbCamera(reinterpret_cast<const float*>(main + kTs4CamRows), player)) return;
            float feet[3], centre[3];
            FbToR3(reinterpret_cast<const float*>(main + kTs4CamTarget), feet);
            _rho = FbRho(p.c, player, _lastView);
            FbCentre(p.c, feet, _rho, centre);
            t_targets = _targets.load(std::memory_order_acquire);
            // The lens for the targets' size; with none yet, the game's (drawn into its own targets, which its frame
            // then clears).
            const uint32_t w = t_targets ? t_targets->w : _gameW.load(std::memory_order_relaxed);
            const uint32_t h = t_targets ? t_targets->h : _gameH.load(std::memory_order_relaxed);
            const Ts4LensShape lens = Ts4Lens(w ? w : 1, h ? h : 1);
            for (int v = 0; v < kTs4Faces; v++)
            {
                if (!p.c.faceOn[v]) continue;
                const Red3Camera cam = FbWorldCamera(v, centre, _rho, p.g);
                if (DrawFace(manager, scene, main, v, cam, lens)) { ++_drawn; _drawnAt[v].store(frame, std::memory_order_release); }
                else ++_faults;
            }
            t_targets = nullptr;
        }

        // The job thread, while a face's scene is drawn: the face (-1: none) and the targets it goes into (null: none
        // yet, and it goes into the game's own, which its frame then clears and draws over), and whether this is its
        // first bind of the scene's own targets (they are cleared then).
        static int Face() { return t_face; }
        static const Ts4Targets* FaceTargets() { return t_targets; }
        static bool FirstBind() { const bool first = !t_bound; t_bound = true; return first; }

        // The job thread: the game's picture as a face's scene binds it - its size, and its colour and depth formats
        // (DXGI's) - for the targets the present makes; and a bind turned to a face's targets, counted.
        void Learn(uint32_t w, uint32_t h, uint32_t colourFormat, uint32_t depthFormat)
        {
            _gameW.store(w, std::memory_order_relaxed); _gameH.store(h, std::memory_order_relaxed);
            _colourFormat.store(colourFormat, std::memory_order_relaxed); _depthFormat.store(depthFormat, std::memory_order_relaxed);
        }
        void Redirected() { ++_redirects; }

        // The game's picture as the faces' scenes bind it (0 until a face has been drawn).
        uint32_t GameWidth() const { return _gameW.load(std::memory_order_relaxed); }
        uint32_t GameHeight() const { return _gameH.load(std::memory_order_relaxed); }
        uint32_t ColourFormat() const { return _colourFormat.load(std::memory_order_relaxed); }
        uint32_t DepthFormat() const { return _depthFormat.load(std::memory_order_relaxed); }

        // Render jobs run, the frame count, and the frame that last drew face v (0: never).
        uint64_t Jobs() const { return _jobs.load(std::memory_order_relaxed); }
        uint64_t Frame() const { return _frame.load(std::memory_order_acquire); }
        uint64_t DrawnAt(int v) const { return _drawnAt[v].load(std::memory_order_acquire); }
        uint64_t Drawn() const { return _drawn; }
        uint64_t Faults() const { return _faults; }
        uint64_t Redirects() const { return _redirects; }
        double Rho() const { return _rho; }

    private:
        struct Planned { Settings c; Geometry g; bool on = false; };

        // One face: its camera made from the main one, then the scene drawn from it. Every read and call guarded: a lot
        // being left may take the manager's camera or scene away under it.
        bool DrawFace(void* manager, void* scene, const uint8_t* main, int v, const Red3Camera& cam, const Ts4LensShape& lens)
        {
            uint8_t* fc = _cams + kTs4CameraBytes * v;
            float rows[16];
            FbRows(cam, rows);
            // width, height, horizontal angle, aspect, near, far
            const float angle = std::atan(lens.cx + lens.width / 2) - std::atan(lens.cx - lens.width / 2);
            const float shape[6] = { lens.width, lens.height, angle, lens.width / lens.height, cam.nearM, cam.farM };
            const float off[2] = { lens.cx, lens.cy };
            __try
            {
                memcpy(fc, main, kTs4CameraBytes);
                const uint32_t many = 1u << 24;
                memcpy(fc + kTs4CamCount, &many, sizeof many);
                memcpy(fc + kTs4CamLens, shape, sizeof shape);
                memcpy(fc + kTs4CamRows, rows, sizeof rows);
                memcpy(fc + kTs4CamCentre, off, sizeof off);
                reinterpret_cast<Ts4CameraUpdateFn>(_exe + kTs4CameraUpdateRva)(fc, false, nullptr);
                t_face = v; t_bound = false;
                const int mask = *reinterpret_cast<const int*>(static_cast<const uint8_t*>(manager) + kTs4ManagerMask);
                const uint32_t flags = *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(manager) + kTs4ManagerFlags);
                reinterpret_cast<Ts4SceneFromCameraFn>(_exe + kTs4SceneFromCameraRva)(manager, scene, fc, flags, mask, false, nullptr);
                t_face = -1;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { t_face = -1; return false; }
        }

        // The face the job thread is drawing (-1: none), its targets, and whether it has bound the scene's targets yet.
        static inline thread_local int t_face = -1;
        static inline thread_local bool t_bound = false;
        static inline thread_local const Ts4Targets* t_targets = nullptr;

        uintptr_t _exe = 0;
        Ts4JobFn _original = nullptr;
        uint8_t _mine[sizeof kTs4LotJobHead] = {};
        uint8_t* _cams = nullptr;
        std::mutex _planMtx;
        Planned _plan;
        std::atomic<const Ts4Targets*> _targets{ nullptr };
        std::atomic<uint32_t> _gameW{ 0 }, _gameH{ 0 }, _colourFormat{ 0 }, _depthFormat{ 0 };
        std::atomic<uint64_t> _jobs{ 0 }, _frame{ 1 }, _drawnAt[kTs4Faces] = {};
        uint64_t _drawn = 0, _faults = 0;   // job thread only (read for the log)
        std::atomic<uint64_t> _redirects{ 0 };
        double _rho = 0, _lastView = 0;
    };
}
