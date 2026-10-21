// cryengine2_views.hpp - CRYSIS (engine=cryengine2): the cube's faces rendered by the game's own 3D engine, in the same
// frame as the player's view, from cameras the add-on makes.
//
// Crysis 1.2.1 (build 6156, 64-bit, D3D10; traced 2026-10-20). Interface layouts are those of the Crysis Mod SDK's
// CryCommon headers, as kept by the CryMP project (github.com/crymp-net/client-server, Code/CryCommon; read, not copied):
//   * The engine's global environment (SSystemGlobalEnvironment) is found by its shape: a pointer, in the renderer's own
//     data, to a block holding the system (+0x00), the renderer (+0x18) and the 3D engine (+0x40), each an object whose
//     vtable lies in that module's image. The game is at +0x08.
//   * Each frame CSystem::Render calls I3DEngine::RenderWorld (slot 2: IProcess's Init, Update, RenderWorld) once, on the
//     game's main thread, with flags 0x327 and the name "CSystem:Render I3DEngine"; the water's reflection calls it too,
//     from inside that call. The renderer has no thread of its own in this build: a call returns with its view drawn. The
//     slot is replaced in the 3D engine's vtable - no code is patched - and before the game's own view, each face is drawn
//     by the same call with a camera of the add-on's and flags 0x307 (HDR, sorting, the Z pass, ambient occlusion and
//     water; no post-processing, so no motion blur from the player's last view). Drawn that way, a face renders its own
//     sun shadow maps and leaves its finished picture in the back buffer and its depth in the engine's linear depth
//     target ($ZTarget: R32_FLOAT, the game's size, view depth over the far plane), which the add-on then copies out.
//     Measured: about 1.2 ms of the main thread a face; the game's own view after it is untouched.
//   * A camera (CCamera, 536 bytes) is a copy of the game's, given the face's matrix and lens and its frustum worked out
//     again as the engine's own inline code does (Ce2SetCamera). Its matrix is row-major 3x4, its columns the camera's
//     right, forward (it looks along +Y) and up, and its place: REDengine's frame (X right, Y forward, Z up), so
//     red3_native.hpp's and frostbite_views.hpp's face placement is used as it is.
//   * The lens is the game's picture (it draws at its own size) with a vertical angle of 90 degrees: the square in its
//     middle, as many texels a side as the picture is tall, is then exactly 90 degrees each way - the face (Ce2Square).
//   * The player's feet: the entity system (+0x70) finds the player's entity by its name, "Dude" (IEntitySystem slot 9,
//     FindEntityByName), and its world matrix (IEntity slot 22, GetWorldTM) holds its place: a character's origin is its
//     feet. The entity system is the game's own CryEntitySystem.dll; CryMP's game and action layers are its own work and
//     their headers did not match the game's (the framework's client actor, tried first, never answered: 2026-10-20).
#pragma once

#include <windows.h>
#include <psapi.h>

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
    constexpr int kCe2Faces = kRed4Faces;   // REDengine's five places: four sides and the top
    constexpr size_t kCe2EnvSystem = 0x00, kCe2EnvRenderer = 0x18, kCe2Env3DEngine = 0x40, kCe2EnvEntitySystem = 0x70;
    constexpr int kCe2RenderWorldSlot = 2, kCe2FindEntityByNameSlot = 9, kCe2WorldTmSlot = 22;
    constexpr char kCe2PlayerName[] = "Dude";
    constexpr int kCe2FaceFlags = 0x307;
    constexpr char kCe2MainView[] = "CSystem:Render I3DEngine";

    // CCamera's fields (bytes).
    constexpr size_t kCe2CameraBytes = 536, kCe2CamFov = 48, kCe2CamWidth = 52, kCe2CamHeight = 56, kCe2CamRatio = 60,
                     kCe2CamAngleRes = 64, kCe2CamEdgeN = 68, kCe2CamEdgeP = 80, kCe2CamEdgeF = 92, kCe2CamCorners = 104,
                     kCe2CamPlanes = 248, kCe2CamIdx = 344, kCe2CamOccPos = 520;

    using Ce2RenderWorldFn = void(__fastcall*)(void* engine, int flags, const void* camera, const char* name, int drawFlags,
                                               int filterFlags);

    // The face's square: the middle of the game's picture, as many texels a side as it is tall.
    inline uint32_t Ce2Square(uint32_t w, uint32_t h) { return std::max(1u, std::min(w, h)); }

    // A camera in the game's world from a CCamera's matrix: its columns right, forward, up and its place.
    inline void Ce2FromMatrix(const float m[12], Red3Camera& cam)
    {
        for (int i = 0; i < 3; i++)
        {
            cam.right[i] = m[4 * i + 0]; cam.fwd[i] = m[4 * i + 1]; cam.up[i] = m[4 * i + 2]; cam.pos[i] = m[4 * i + 3];
        }
    }

    // A face's camera into a CCamera: a copy of the game's (whatever this layout leaves out stays the game's), given the
    // face's matrix and a lens of the game's picture size (w x h) with a vertical angle of 90 degrees, and its frustum -
    // the corners in camera space and the six planes in the world - worked out as CCamera::SetFrustum and UpdateFrustum do.
    inline void Ce2SetCamera(uint8_t* out, const uint8_t* game, const Red3Camera& cam, int w, int h)
    {
        memcpy(out, game, kCe2CameraBytes);
        float m[12];
        for (int i = 0; i < 3; i++)
        {
            m[4 * i + 0] = cam.right[i]; m[4 * i + 1] = cam.fwd[i]; m[4 * i + 2] = cam.up[i]; m[4 * i + 3] = cam.pos[i];
        }
        memcpy(out, m, sizeof m);
        const float fov = 3.14159265358979f / 2.0f, nearM = cam.nearM, farM = cam.farM;
        const float ratio = (float)w / (float)h, angleRes = (float)w / fov;
        memcpy(out + kCe2CamFov, &fov, 4); memcpy(out + kCe2CamWidth, &w, 4); memcpy(out + kCe2CamHeight, &h, 4);
        memcpy(out + kCe2CamRatio, &ratio, 4); memcpy(out + kCe2CamAngleRes, &angleRes, 4);
        // The left top corner of the projection plane (x left, y ahead, z up), and of the near and far planes.
        const float p[3] = { -w * 0.5f, (float)(1.0 / std::tan(fov * 0.5) * (h * 0.5)), h * 0.5f };
        const float n[3] = { nearM * p[0] / p[1], nearM, nearM * p[2] / p[1] };
        const float f[3] = { p[0] * (farM / p[1]), farM, p[2] * (farM / p[1]) };
        memcpy(out + kCe2CamEdgeN, n, 12); memcpy(out + kCe2CamEdgeP, p, 12); memcpy(out + kCe2CamEdgeF, f, 12);
        // The four corners of each plane turned into the world's directions: left top, right top, left bottom, right bottom.
        auto turn = [&](float x, float y, float z, float o[3])
        {
            for (int i = 0; i < 3; i++) o[i] = m[4 * i + 0] * x + m[4 * i + 1] * y + m[4 * i + 2] * z;
        };
        float c[12][3];
        const float* e[3] = { p, n, f };
        for (int k = 0; k < 3; k++)
        {
            turn(+e[k][0], e[k][1], +e[k][2], c[4 * k + 0]);
            turn(-e[k][0], e[k][1], +e[k][2], c[4 * k + 1]);
            turn(+e[k][0], e[k][1], -e[k][2], c[4 * k + 2]);
            turn(-e[k][0], e[k][1], -e[k][2], c[4 * k + 3]);
        }
        memcpy(out + kCe2CamCorners, c, sizeof c);
        // The planes (normal, d with n.x + d = 0), through three points each, in the engine's order: near, far, right,
        // left, top, bottom. The matrix is right-handed (right x forward = up), so none is turned round.
        const float* pos = cam.pos;
        auto at = [&](const float* v, float o[3]) { for (int i = 0; i < 3; i++) o[i] = v[i] + pos[i]; };
        auto plane = [](const float a[3], const float b[3], const float d[3], float o[4])
        {
            const float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { d[0] - a[0], d[1] - a[1], d[2] - a[2] };
            float nn[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
            const float l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            for (int i = 0; i < 3; i++) nn[i] /= (l > 0 ? l : 1);
            o[0] = nn[0]; o[1] = nn[1]; o[2] = nn[2]; o[3] = -(nn[0] * a[0] + nn[1] * a[1] + nn[2] * a[2]);
        };
        float ltn[3], rtn[3], rbn[3], ltf[3], rtf[3], lbf[3], rbf[3];
        at(c[4], ltn); at(c[5], rtn); at(c[7], rbn); at(c[8], ltf); at(c[9], rtf); at(c[10], lbf); at(c[11], rbf);
        float planes[6][4];
        plane(rtn, ltn, rbn, planes[0]);   // near
        plane(rtf, rbf, ltf, planes[1]);   // far
        plane(rbf, rtf, pos, planes[2]);   // right
        plane(ltf, lbf, pos, planes[3]);   // left
        plane(rtf, ltf, pos, planes[4]);   // top
        plane(lbf, rbf, pos, planes[5]);   // bottom
        memcpy(out + kCe2CamPlanes, planes, sizeof planes);
        // For each plane, the box corner its normal points away from and toward (the engine's AABB test): per axis,
        // 3 when the normal's component is negative, then the other.
        uint32_t idx[6][6];
        for (int i = 0; i < 6; i++)
            for (int a = 0; a < 3; a++)
            {
                const uint32_t neg = planes[i][a] < 0 ? 1u : 0u;
                idx[a][i] = neg * 3 + a;
                idx[3 + a][i] = (1 - neg) * 3 + a;
            }
        memcpy(out + kCe2CamIdx, idx, sizeof idx);
        memcpy(out + kCe2CamOccPos, pos, 12);
    }

    // The pack pass's constants for a face: the camera Battlefront II's faces have round the centre (Red4Camera), read
    // from its square (square texels a side, its depth there too), the colour the game's finished picture, the depth
    // linear: the view depth over the face's far plane.
    inline PackConstants Ce2FaceConstants(int face, const Settings& c, const Geometry& g, float exposure, uint32_t square)
    {
        const float centre[3] = { 0, 0, 0 };
        PackConstants p = Red3FaceConstants(face, Red4Camera(face, g), centre, 0.0, c, g, c.faceRes, c.faceRes, 1, exposure);
        p.SrcSize = square;
        p.SrcDepthScaled = 1;
        p.DepthLinear = 1;
        return p;
    }

    struct Ce2Module { uintptr_t base = 0, end = 0; };

    class Ce2Faces
    {
    public:
        // The engine found and its RenderWorld slot taken; false and why when not. Main thread, once the game's device and
        // swap chain exist (never at load: ReShade loads and drops the add-on while the game tries devices out).
        bool Install(void* detour, std::string& why)
        {
            _sys = Mod("CrySystem.dll"); _3d = Mod("Cry3DEngine.dll"); _rend = Mod("CryRenderD3D10.dll");
            if (!_sys.base || !_3d.base || !_rend.base) { why = "not CryEngine 2 on D3D10 (CrySystem, Cry3DEngine, CryRenderD3D10)"; return false; }
            if (!_cams) _cams = static_cast<uint8_t*>(VirtualAlloc(nullptr, kCe2CameraBytes * kCe2Faces, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!_cams) { why = "no memory for the cameras"; return false; }
            _env = FindEnv(_rend);
            if (!_env) { why = "the engine's global environment was not found"; return false; }
            uintptr_t engine = 0, vt = 0, fn = 0;
            if (!Read(_env + kCe2Env3DEngine, engine) || !Read(engine, vt)) { why = "no 3D engine"; return false; }
            _slot = reinterpret_cast<uintptr_t*>(vt + 8 * kCe2RenderWorldSlot);
            if (!Read((uintptr_t)_slot, fn) || !In(_3d, fn)) { why = "the 3D engine's RenderWorld slot is not its own"; _slot = nullptr; return false; }
            _original = reinterpret_cast<Ce2RenderWorldFn>(fn);
            if (!WriteSlot((uintptr_t)detour)) { why = "the 3D engine's vtable could not be made writable"; _original = nullptr; _slot = nullptr; return false; }
            _detour = (uintptr_t)detour;
            return true;
        }

        // The slot given back, if it is still this hook's.
        void Uninstall()
        {
            if (!_slot || !_original) return;
            uintptr_t now = 0;
            if (Read((uintptr_t)_slot, now) && now == _detour) WriteSlot((uintptr_t)_original);
            _original = nullptr; _slot = nullptr;
        }

        Ce2RenderWorldFn Original() const { return _original; }
        bool Installed() const { return _original != nullptr; }

        // Main thread, at present: the settings the next frame's faces are drawn by, and whether that frame draws them.
        void Plan(const Settings& c, const Geometry& g, bool draw) { _plan.c = c; _plan.g = g; _plan.draw = draw; }

        // Main thread, in the game's RenderWorld: whether this call is the game's own view of the frame.
        static bool IsMainView(const char* name)
        {
            __try { return name && strcmp(name, kCe2MainView) == 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Main thread, before the game's own view: each face that is on, drawn from its camera round the player, then
        // handed to drawn(face) while its picture and depth are still the engine's latest. The picture's size (w x h)
        // is the game's. Returns the faces drawn.
        template <typename Drawn>
        int DrawFaces(void* engine, const void* gameCamera, uint32_t w, uint32_t h, Drawn&& drawn)
        {
            if (!_plan.draw || !_original || !w || !h) return 0;
            _plan.draw = false;   // one frame's worth
            const Settings& c = _plan.c;
            const Geometry& g = _plan.g;
            Red3Camera player{};
            float feet[3];
            if (!GameCamera(gameCamera, player)) { ++_faults; return 0; }
            if (Feet(feet)) ++_fromPlayer;
            else { feet[0] = player.pos[0]; feet[1] = player.pos[1]; feet[2] = player.pos[2] - kCe2EyeM; ++_noFeet; }
            memcpy(_feet, feet, sizeof feet);
            _eye = player.pos[2] - feet[2];
            _rho = FbRho(c, player, _lastView);
            float centre[3];
            FbCentre(c, feet, _rho, centre);
            int n = 0;
            for (int v = 0; v < kCe2Faces; v++)
            {
                if (!c.faceOn[v]) continue;
                const Red3Camera cam = FbWorldCamera(v, centre, _rho, g);
                uint8_t* fc = _cams + kCe2CameraBytes * v;
                if (!DrawFace(engine, gameCamera, fc, cam, w, h, c.ce2Flags)) { ++_faults; continue; }
                drawn(v);
                ++n;
            }
            _drawn += n;
            return n;
        }

        double Rho() const { return _rho; }
        const float* LastFeet() const { return _feet; }
        uint64_t Drawn() const { return _drawn; }
        uint64_t Faults() const { return _faults; }
        uint64_t NoFeet() const { return _noFeet; }
        uint64_t FeetFromPlayer() const { return _fromPlayer; }
        float EyeAboveFeet() const { return _eye; }

    private:
        static constexpr float kCe2EyeM = 1.7f;   // when the player cannot be read: the camera this far above the feet

        static Ce2Module Mod(const char* name)
        {
            Ce2Module m; HMODULE h = GetModuleHandleA(name); MODULEINFO mi{};
            if (h && GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof mi))
            {
                m.base = (uintptr_t)mi.lpBaseOfDll; m.end = m.base + mi.SizeOfImage;
            }
            return m;
        }
        static bool In(const Ce2Module& m, uintptr_t a) { return a >= m.base && a < m.end; }
        template <typename T> static bool Read(uintptr_t at, T& out)
        {
            __try { memcpy(&out, (const void*)at, sizeof(T)); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool VtIn(uintptr_t obj, const Ce2Module& m) const { uintptr_t vt = 0; return obj && Read(obj, vt) && In(m, vt); }

        uintptr_t FindEnv(const Ce2Module& where) const
        {
            const auto* dos = (const IMAGE_DOS_HEADER*)where.base;
            const auto* nt = (const IMAGE_NT_HEADERS64*)(where.base + dos->e_lfanew);
            const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
            for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
            {
                if (!(sec->Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
                for (uintptr_t a = where.base + sec->VirtualAddress, e = a + sec->Misc.VirtualSize; a + 8 <= e; a += 8)
                {
                    const uintptr_t p = *(const uintptr_t*)a;
                    uintptr_t sys = 0, ren = 0, eng = 0;
                    if (p < 0x10000 || !Read(p + kCe2EnvSystem, sys) || !Read(p + kCe2EnvRenderer, ren) || !Read(p + kCe2Env3DEngine, eng)) continue;
                    if (VtIn(sys, _sys) && VtIn(ren, _rend) && VtIn(eng, _3d)) return p;
                }
            }
            return 0;
        }

        bool WriteSlot(uintptr_t value)
        {
            DWORD old = 0;
            if (!VirtualProtect(_slot, 8, PAGE_READWRITE, &old)) return false;
            *_slot = value;
            VirtualProtect(_slot, 8, old, &old);
            return true;
        }

        static bool GameCamera(const void* cam, Red3Camera& out)
        {
            float m[12];
            if (!Read((uintptr_t)cam, m)) return false;
            Ce2FromMatrix(m, out);
            auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
            auto is = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
            return is(dot(out.right, out.right), 1) && is(dot(out.fwd, out.fwd), 1) && is(dot(out.up, out.up), 1) &&
                   is(dot(out.right, out.fwd), 0) && is(dot(out.up, out.fwd), 0);
        }

        // The player's feet: their entity's place. Every step guarded: in menus and loads there is none.
        bool Feet(float out[3]) const
        {
            __try
            {
                void* entities = *(void**)(_env + kCe2EnvEntitySystem);
                if (!entities) return false;
                void* entity = reinterpret_cast<void*(__fastcall*)(void*, const char*)>((*(void***)entities)[kCe2FindEntityByNameSlot])(entities, kCe2PlayerName);
                if (!entity) return false;
                const float* tm = reinterpret_cast<const float*(__fastcall*)(void*)>((*(void***)entity)[kCe2WorldTmSlot])(entity);
                if (!tm) return false;
                out[0] = tm[3]; out[1] = tm[7]; out[2] = tm[11];
                return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool DrawFace(void* engine, const void* gameCamera, uint8_t* fc, const Red3Camera& cam, uint32_t w, uint32_t h, int flags)
        {
            __try
            {
                Ce2SetCamera(fc, (const uint8_t*)gameCamera, cam, (int)w, (int)h);
                _original(engine, flags, fc, "VoxonCaptureRig face", -1, -1);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        struct Planned { Settings c; Geometry g; bool draw = false; };

        Ce2Module _sys, _3d, _rend;
        uintptr_t _env = 0, _detour = 0;
        uintptr_t* _slot = nullptr;
        Ce2RenderWorldFn _original = nullptr;
        Planned _plan;   // main thread only: written at present, read in RenderWorld
        uint8_t* _cams = nullptr;   // the faces' cameras, page-aligned memory of the add-on's own
        float _feet[3] = {};
        double _rho = 0, _lastView = 0;
        uint64_t _drawn = 0, _faults = 0, _noFeet = 0, _fromPlayer = 0;
        float _eye = 0;   // the game's camera above the feet (m), last frame drawn
    };
}
