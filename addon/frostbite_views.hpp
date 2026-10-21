// frostbite_views.hpp - FROSTBITE (engine=frostbite; Star Wars Battlefront II): the cube's faces rendered by the engine
// as extra views, in the same frame as the player's.
//
// Frostbite gives a mod no camera and no script, but its renderer draws every view on a list it keeps (traced on
// Battlefront II 1.1.8.16175, 2026-10-16): the frame builder (buildGameRenderViewParams) walks nodes headed at
// exe+0x3FFCD88, { u16 id; static state*; dynamic state*; next; prev; u8 linked }, and renders each in full - its own
// G-buffer, lighting, shadows and exposure - into targets of its viewport's size, rounded up to a multiple of 4. A view
// that is not the player's root view is not put on the screen. So the add-on links a node of its own per face: a copy
// of the root's state with its own id, viewport, lens and near and far planes. Its camera is set where the frame builder
// has each view's camera rows in xmm2-5 and its node in rdi (exe+0x1524B12): a hardware execute breakpoint, so the
// game's code is not changed. The same breakpoint reads the player's camera from the root view's node.
//   * The faces' nodes are on the list only while the frame builder runs: linked as it starts (exe+0x1524910), each a
//     fresh copy of the root's state, and unlinked where all its ways out meet (exe+0x1524EEF). Left on the list, they
//     crashed the game as a level loaded: something else walks the list then and takes a node for more than these are.
//   * The engine takes four views in all. In play it appends a 16-byte record a view, each frame, to a table of four
//     inside a renderer object ([exe+0x3FFBA20] + 0xCC250, its count at + 0xCC24C, unchecked: exe+0x7C3AE05); a fifth
//     view's record lands on the list pointer after the table, and the game crashed a second or two later walking it
//     (exe+0x7C41E40; caught with a write watch, 2026-10-16). In the menu the table is not used, and there a fifth view
//     only dropped the frame rate to about 5 a second. So at most kFbMaxFaces faces are drawn, fewer while the game
//     has views of its own beyond the player's: each frame they take only the room the game's leave.
//   * Each face is drawn at its own size, FbFaceSize, 8 texels apart: views of one size share their targets, and the last
//     one's would be all that is left at present. Of a face's targets, the add-on takes the scene depth (D32 S8,
//     R32G8X24) and the lit colour (R11G11B10, HDR, before exposure and the HUD) bound with it, both whole at present.
//     The lit colour is written whole by the lighting and never cleared; another R11G11B10 of the face's size is
//     cleared to black each frame and bound with the depth too (in play before the lit one, in the menu after).
//   * The depth is reversed, 1 at the near plane and 0 at the far one, with the near and far planes the add-on sets in
//     the view's state (measured looking down at a floor from four heights: a near plane of 0.06 m left alone, and 0.5
//     m set, each put the floor at the same height to a millimetre).
// Axes. Frostbite's world: metres, right-handed, Y up; a camera's rows are its right, its up, its back (it looks along
// minus the third row) and its place. Carried into REDengine's frame (Z up) by F(w) = (x, -z, y), a turn, so the faces
// stand where sweep_red3.hpp's Red3Place stands them and decode as red4_rtt.hpp's do, in the rig's own frame.
#pragma once

#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <new>
#include <thread>

#include "red4_rtt.hpp"

namespace vxcap
{
    constexpr int kFbMaxViews = 4, kFbMaxFaces = kFbMaxViews - 1;   // the engine's views in all; the player's is one
    constexpr uint32_t kFbListRva = 0x3FFCD88, kFbListReadRva = 0x1524A5E, kFbBreakRva = 0x1524B12;
    constexpr uint32_t kFbStartRva = 0x1524910, kFbEndRva = 0x1524EEF;
    // The code at each, in the build the add-on knows: the frame builder's read of the list's head, the instruction after
    // each view's camera rows are loaded, the frame builder's first instruction, and the one its ways out meet at.
    constexpr uint8_t kFbListRead[7] = { 0x48, 0x8B, 0x3D, 0x23, 0x83, 0xAD, 0x02 };
    constexpr uint8_t kFbBreakBytes[9] = { 0x0F, 0x28, 0x45, 0xD0, 0x0F, 0x29, 0x44, 0x24, 0x70 };
    constexpr uint8_t kFbStartBytes[12] = { 0x48, 0x8B, 0xC4, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
    constexpr uint8_t kFbEndBytes[7] = { 0x4C, 0x8B, 0x8D, 0x38, 0x01, 0x00, 0x00 };
    // The view state: the dynamic state's near and far planes, aspect, vertical field of view (radians), viewport, and
    // whether its lens is a physical camera's (focal length and sensor) - the menu's is, and then the field of view is
    // not the one given (measured: a quarter of the 90 degrees asked for).
    constexpr size_t kFbNear = 0x90, kFbFar = 0x94, kFbAspect = 0x98, kFbFov = 0x9C, kFbViewport = 0xD8, kFbPhysical = 0xC9;
    // The player's soldier: the game keeps its local players' soldiers in a list in its own data ({ begin, end } at
    // exe+0x3A93190; the first is the player's), each a WSClientSoldierEntity whose aim entity (+0xA20, a
    // ClientAimEntity) holds its feet (+0x230) - where its transform puts them, measured against the camera as the
    // player walked, stood against a wall and was pushed in by it (2026-10-16). Each object known by its vtable.
    constexpr uint32_t kFbSoldiersRva = 0x3A93190, kFbSoldierVtRva = 0x322CB68, kFbAimVtRva = 0x3140A08;
    constexpr size_t kFbSoldierAim = 0xA20, kFbAimFeet = 0x230;
    constexpr float kFbSoldierNearM = 50.0f;   // a soldier farther from the camera than this is not the one it follows

    inline void FbToR3(const float w[3], float out[3]) { out[0] = w[0]; out[1] = -w[2]; out[2] = w[1]; }
    inline void FbFromR3(const float r[3], float out[3]) { out[0] = r[0]; out[1] = r[2]; out[2] = -r[1]; }

    // A face's size: its views and targets this many texels square (multiples of 4, the engine's rounding).
    inline int FbBaseSize(const Settings& c) { return (c.faceRes + 3) / 4 * 4; }
    inline int FbFaceSize(const Settings& c, int face) { return FbBaseSize(c) + 8 * face; }
    inline int FbFaceOf(const Settings& c, uint32_t w, uint32_t h)
    {
        if (w != h) return -1;
        const int d = (int)w - FbBaseSize(c);
        return d >= 0 && d % 8 == 0 && d / 8 < kRed4Faces ? d / 8 : -1;
    }

    // The faces drawn: the first kFbMaxFaces of those the app has on, of the five REDengine places (four sides, the top).
    // Not more taking turns: a face left out of a frame and put back the next made the game four times slower (four
    // faces in turn, 40 fps; three every frame, 150; 2026-10-16) - the engine draws a view cheaply only while it stays.
    inline int FbFaces(const Settings& c, int out[kFbMaxFaces])
    {
        int n = 0;
        for (int v = 0; v < kRed4Faces && n < kFbMaxFaces; v++) if (c.faceOn[v]) out[n++] = v;
        return n;
    }

    // The player's camera, from a view's rows (right, up, back, place), in REDengine's frame; false when they are none.
    inline bool FbCamera(const float rows[16], Red3Camera& cam)
    {
        const float back[3] = { rows[8], rows[9], rows[10] }, fwd[3] = { -back[0], -back[1], -back[2] };
        FbToR3(rows, cam.right); FbToR3(rows + 4, cam.up); FbToR3(fwd, cam.fwd); FbToR3(rows + 12, cam.pos);
        auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        auto is = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
        return is(dot(cam.right, cam.right), 1) && is(dot(cam.up, cam.up), 1) && is(dot(cam.fwd, cam.fwd), 1)
               && is(dot(cam.right, cam.fwd), 0) && is(dot(cam.up, cam.fwd), 0);
    }

    // A camera into a view's rows (right, up, back, place; w lanes 0, 1 for the place).
    inline void FbRows(const Red3Camera& cam, float rows[16])
    {
        float r[3], u[3], f[3], p[3];
        FbFromR3(cam.right, r); FbFromR3(cam.up, u); FbFromR3(cam.fwd, f); FbFromR3(cam.pos, p);
        const float m[16] = { r[0], r[1], r[2], 0, u[0], u[1], u[2], 0, -f[0], -f[1], -f[2], 0, p[0], p[1], p[2], 1 };
        memcpy(rows, m, sizeof m);
    }

    // The player when its soldier cannot be read (deploying, dead, in a vehicle): the game's camera's place, ahead along
    // its level look, to its left and down (FROSTBITE settings).
    inline void FbFeet(const Red3Camera& player, const Settings& c, float out[3])
    {
        auto level = [](const float* v, float& x, float& y)
        {
            const float l = std::sqrt(v[0] * v[0] + v[1] * v[1]);
            if (l > 1e-4f) { x = v[0] / l; y = v[1] / l; } else { x = 0; y = 0; }
        };
        float ax, ay, rx, ry;
        level(player.fwd, ax, ay);
        level(player.right, rx, ry);
        out[0] = player.pos[0] + ax * c.chaseAhead - rx * c.chaseLeft;
        out[1] = player.pos[1] + ay * c.chaseAhead - ry * c.chaseLeft;
        out[2] = player.pos[2] - c.chaseDown;
    }

    // The rig's yaw (radians, the display's frame): its own, and the player's view when turning with it - the Witcher 3
    // script's Rho (rig/redengine3/voxonRig.ws), the player's view read from its camera. last: kept when looking straight
    // up or down.
    inline double FbRho(const Settings& c, const Red3Camera& player, double& lastView)
    {
        if (std::fabs(player.fwd[0]) + std::fabs(player.fwd[1]) > 0.01f) lastView = std::atan2(-(double)player.fwd[1], (double)player.fwd[0]);
        const double yaw = c.yawDeg * 3.14159265358979 / 180.0;
        return c.turnWithPlayer ? lastView + yaw : yaw;
    }

    // The volume's centre round the feet: where they stand in the display undone - the script's Centre, term for term.
    inline void FbCentre(const Settings& c, const float feet[3], double rho, float out[3])
    {
        const double s = c.radiusM / c.boundR, ex = c.radiusM * c.scaleX, ey = c.radiusM * c.scaleY;
        const double t = (c.yawDeg - (c.turnWithPlayer ? 90.0 : 0.0)) * 3.14159265358979 / 180.0;
        const double qx = (c.feetX * std::cos(t) + c.feetY * std::sin(t)) * ex / c.radiusM * s;
        const double qy = (-c.feetX * std::sin(t) + c.feetY * std::cos(t)) * ey / c.radiusM * s;
        const double qz = c.feetZ * c.scaleZ * s;
        const double rx = qx * std::cos(rho) - qy * std::sin(rho), ry = qx * std::sin(rho) + qy * std::cos(rho);
        out[0] = (float)(feet[0] - rx); out[1] = (float)(feet[1] + ry); out[2] = (float)(feet[2] + qz);
    }

    // A face's camera in the world: Red3Place's, round the centre at the rig's yaw, a square lens of 90 degrees, its far
    // plane just past the box (Red4FarM) - the same camera, relative to the centre, as the decode's (FbFaceConstants).
    inline Red3Camera FbWorldCamera(int face, const float centre[3], double rho, const Geometry& g)
    {
        Red3Camera cam = Red3Place(face, centre, rho, g, 1.0f, 1.0f, g.tilt);
        const float rel[3] = { cam.pos[0] - centre[0], cam.pos[1] - centre[1], cam.pos[2] - centre[2] };
        cam.farM = Red4FarM(std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]), g);
        return cam;
    }

    // PLAYER LOOK: the player's body, as the pack pass marks it - a standing cylinder on their feet (m). Wide enough for a
    // soldier holding a rifle out, tall enough for the tallest heroes.
    constexpr float kFbPlayerRadiusM = 0.6f, kFbPlayerHeightM = 2.4f;

    // The player's feet in the pack pass's frame (metres from the volume's centre, +Z down): where the display stands them
    // (feetX, feetY, feetZ), taken back through the compact pass's map - its turn, its display units, each axis's scale.
    inline void FbPlayerAt(const Settings& c, const Geometry& g, float out[3])
    {
        const float a = DisplayTurnDeg(c) * 3.14159265358979f / 180.0f, ca = std::cos(a), sa = std::sin(a);
        const float dispXY = c.boundR / g.R, dispZ = c.boundZ / g.Z;
        out[0] = (c.feetX * ca + c.feetY * sa) / dispXY * (g.ex / g.R);
        out[1] = (-c.feetX * sa + c.feetY * ca) / dispXY * (g.ey / g.R);
        out[2] = c.feetZ / dispZ * (g.ez / g.Z);
    }

    // The pack pass's constants for a face: Cyberpunk's (red4_rtt.hpp), in the rig's own frame, with the picture and the
    // depth both at the face's own size, and the colour HDR. PLAYER LOOK: the player marked by where they stand
    // (FbPlayerShape).
    inline PackConstants FbFaceConstants(int face, const Settings& c, const Geometry& g, float exposure)
    {
        PackConstants p = Red4FaceConstants(face, c, g, exposure);
        p.SrcSize = (uint32_t)FbFaceSize(c, face);
        p.SrcDepthScaled = 1;
        p.DisplayColour = 0;
        if (c.playerLook) p.PlayerMask |= 8u;
        return p;
    }

    // PLAYER LOOK: the player's cylinder, for the pack pass (the decoder's Frames).
    inline PlayerShape FbPlayerShape(const Settings& c, const Geometry& g)
    {
        PlayerShape s{};
        FbPlayerAt(c, g, s.at);
        s.radius = kFbPlayerRadiusM;
        s.height = kFbPlayerHeightM;
        return s;
    }

    // The views, their cameras and the breakpoints. One per process.
    class FbViews
    {
    public:
        struct Node { uint16_t id; uint8_t pad[6]; uint8_t* st; uint8_t* dy; Node* next; Node* prev; uint8_t linked; uint8_t pad2[7]; };
        static_assert(sizeof(Node) == 0x30, "the engine's view node");

        // The game's code checked (this build?), the views made, the handler put on; false and why when not. The
        // debug registers go on with Arm, once the game's threads are running.
        bool Install(std::string& why)
        {
            _exe = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
            auto is = [this](uint32_t rva, const uint8_t* bytes, size_t n) { return memcmp(reinterpret_cast<const void*>(_exe + rva), bytes, n) == 0; };
            if (!is(kFbListReadRva, kFbListRead, sizeof kFbListRead) || !is(kFbBreakRva, kFbBreakBytes, sizeof kFbBreakBytes)
                || !is(kFbStartRva, kFbStartBytes, sizeof kFbStartBytes) || !is(kFbEndRva, kFbEndBytes, sizeof kFbEndBytes))
            {
                why = "not the game build this add-on knows (Battlefront II 1.1.8.16175): its views are not where they were";
                return false;
            }
            _views = static_cast<View*>(VirtualAlloc(nullptr, sizeof(View) * kRed4Faces, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!_views) { why = "no memory for the views"; return false; }
            for (int v = 0; v < kRed4Faces; v++) new (&_views[v]) View();
            s_self = this;
            _veh = AddVectoredExceptionHandler(1, &FbViews::OnException);
            return _veh != nullptr || (why = "no exception handler", false);
        }

        // The breakpoints on every thread of the game's but the caller's (a thread of its own sets them). Again whenever
        // the root view has not been seen for a while: a thread made since has none.
        void Arm()
        {
            const uint64_t at[3] = { _exe + kFbStartRva, _exe + kFbBreakRva, _exe + kFbEndRva };
            std::thread t([at, this] { _armed = SetDebugRegisters(at); });
            t.join();
        }

        // Each frame, from the present thread: the faces wanted and their cameras round the player, for the frame builder
        // to take (it links them itself). Returns the faces wanted (their order in faces[]).
        int Update(const Settings& c, const Geometry& g, bool on, int faces[kFbMaxFaces])
        {
            int n = on ? FbFaces(c, faces) : 0;
            Red3Camera player{};
            if (n && !PlayerCamera(player)) n = 0;
            float centre[3] = {};
            if (n)
            {
                float feet[3], w[3];
                bool soldier = SoldierFeet(w);
                if (soldier)
                {
                    FbToR3(w, feet);
                    const float d[3] = { feet[0] - player.pos[0], feet[1] - player.pos[1], feet[2] - player.pos[2] };
                    soldier = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < kFbSoldierNearM * kFbSoldierNearM;
                }
                if (!soldier) FbFeet(player, c, feet);
                ++(soldier ? _feetSoldier : _feetCamera);
                _rho = FbRho(c, player, _lastView);
                FbCentre(c, feet, _rho, centre);
            }
            for (int v = 0; v < kRed4Faces; v++)
            {
                bool want = false;
                for (int i = 0; i < n; i++) if (faces[i] == v) want = true;
                View& w = _views[v];
                if (!want) { w.wanted.store(false, std::memory_order_release); continue; }
                const Red3Camera cam = FbWorldCamera(v, centre, _rho, g);
                const int next = 1 - w.live.load(std::memory_order_relaxed);
                Shot& sh = w.shot[next];
                FbRows(cam, sh.rows);
                const uint16_t size = (uint16_t)FbFaceSize(c, v);
                sh.vp[0] = 0; sh.vp[1] = 0; sh.vp[2] = size; sh.vp[3] = size;
                sh.nearM = cam.nearM; sh.farM = cam.farM;
                w.live.store(next, std::memory_order_release);
                w.wanted.store(true, std::memory_order_release);
            }
            return n;
        }

        // The breakpoints off and the handler gone (the faces are never left on the list).
        void Remove()
        {
            const uint64_t none[3] = {};
            std::thread t([&none] { SetDebugRegisters(none); });
            t.join();
            if (_veh) { RemoveVectoredExceptionHandler(_veh); _veh = nullptr; }
        }

        // The game's own views on the list as the frame builder last started, and the faces it had room for.
        int GameViewCount() const { return _gameViews.load(std::memory_order_relaxed); }
        int FacesDrawn() const { return _facesDrawn.load(std::memory_order_relaxed); }

        // Frames the player's feet came from its soldier, and from the camera (the chase settings).
        uint64_t FeetFromSoldier() const { return _feetSoldier; }
        uint64_t FeetFromCamera() const { return _feetCamera; }
        uint64_t RootSeen() const { return _rootSeen; }
        uint64_t FacesSeen() const { return _facesSeen; }
        uint64_t Builds() const { return _builds; }
        int Armed() const { return _armed; }
        double Rho() const { return _rho; }

    private:
        // What the present thread hands the frame builder for a face, double-buffered: its camera, viewport and planes.
        struct Shot { alignas(16) float rows[16]; uint16_t vp[4]; float nearM, farM; };
        struct View
        {
            Node node{};
            uint8_t st[0x20] = {};
            alignas(16) uint8_t dy[0xE0] = {};
            Shot shot[2] = {};
            std::atomic<int> live{ 0 };
            std::atomic<bool> wanted{ false };
        };

        Node** Head() const { return reinterpret_cast<Node**>(_exe + kFbListRva); }

        bool Ours(const Node* n) const
        {
            return _views && reinterpret_cast<const uint8_t*>(n) >= reinterpret_cast<const uint8_t*>(_views)
                   && reinterpret_cast<const uint8_t*>(n) < reinterpret_cast<const uint8_t*>(_views + kRed4Faces);
        }

        // The player's root view: the game's node with id 0.
        Node* Root() const
        {
            Node* n = *Head();
            for (int i = 0; n && i < 64; i++, n = n->next) if (!Ours(n) && n->id == 0) return n;
            return nullptr;
        }

        // As the frame builder starts: each face wanted, as many as the game's own views leave room for, a fresh copy of
        // the root's state with the face's own, onto the list's head. Never a face already on it: linked again, it would
        // point at itself, and the frame builder would walk round it adding views to a list on its stack until that
        // overran (the game stops itself then: 0xC0000409).
        void LinkFaces()
        {
            for (int v = 0; v < kRed4Faces; v++) if (_views[v].node.linked) return;
            Node* root = Root();
            if (!root || !root->st || !root->dy) return;
            int game = 0, top = 0;
            for (Node* g = *Head(); g && game < 64; g = g->next)
                if (!Ours(g)) { game++; top = std::max(top, (int)g->id); }
            _gameViews.store(game, std::memory_order_relaxed);
            const int room = kFbMaxViews - game;
            int drawn = 0;
            const int base = top + 1;          // the faces' ids: just past the game's own (the root's is 0)
            for (int v = 0; v < kRed4Faces && drawn < room; v++)
            {
                View& w = _views[v];
                if (!w.wanted.load(std::memory_order_acquire)) continue;
                drawn++;
                const Shot& sh = w.shot[w.live.load(std::memory_order_acquire)];
                memcpy(w.st, root->st, sizeof w.st);
                memcpy(w.dy, root->dy, sizeof w.dy);
                const float aspect = 1.0f, fov = 1.5707963f;
                memcpy(w.dy + kFbViewport, sh.vp, sizeof sh.vp);
                memcpy(w.dy + kFbNear, &sh.nearM, 4); memcpy(w.dy + kFbFar, &sh.farM, 4);
                memcpy(w.dy + kFbAspect, &aspect, 4); memcpy(w.dy + kFbFov, &fov, 4);
                w.dy[kFbPhysical] = 0;
                Node& n = w.node;
                n.id = (uint16_t)(base + v); n.st = w.st; n.dy = w.dy;
                Node* h = *Head();
                n.next = h; n.prev = nullptr; n.linked = 1;
                if (h) h->prev = &n;
                *Head() = &n;
            }
            _facesDrawn.store(drawn, std::memory_order_relaxed);
        }

        // Where the frame builder's ways out meet: every face off the list again.
        void UnlinkFaces()
        {
            for (int v = 0; v < kRed4Faces; v++)
            {
                Node* n = &_views[v].node;
                if (!n->linked) continue;
                if (n->prev) n->prev->next = n->next; else if (*Head() == n) *Head() = n->next;
                if (n->next) n->next->prev = n->prev;
                n->next = n->prev = nullptr; n->linked = 0;
            }
        }

        // The player's soldier's feet, in Frostbite's world; false when there is none to read. Every read guarded: the
        // soldier goes when the player dies, and a pointer read just then may already be stale.
        bool SoldierFeet(float out[3]) const
        {
            __try
            {
                const uint64_t* list = reinterpret_cast<const uint64_t*>(_exe + kFbSoldiersRva);
                if (!list[0] || list[1] <= list[0]) return false;
                const uint64_t s = *reinterpret_cast<const uint64_t*>(list[0]);
                if (!s || *reinterpret_cast<const uint64_t*>(s) != _exe + kFbSoldierVtRva) return false;
                const uint64_t aim = *reinterpret_cast<const uint64_t*>(s + kFbSoldierAim);
                if (!aim || *reinterpret_cast<const uint64_t*>(aim) != _exe + kFbAimVtRva) return false;
                memcpy(out, reinterpret_cast<const void*>(aim + kFbAimFeet), 3 * sizeof(float));
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
        }

        // The player's camera as the frame builder last had it.
        bool PlayerCamera(Red3Camera& cam)
        {
            float rows[16];
            for (int tries = 0; tries < 4; tries++)
            {
                const uint32_t s0 = _rootSeq.load(std::memory_order_acquire);
                if (s0 & 1) continue;
                memcpy(rows, _rootRows, sizeof rows);
                if (_rootSeq.load(std::memory_order_acquire) == s0) return s0 != 0 && FbCamera(rows, cam);
            }
            return false;
        }

        // DR0-2 at these addresses (0: off), on execute; the other debug registers left as they were.
        static int SetDebugRegisters(const uint64_t at[3])
        {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snap == INVALID_HANDLE_VALUE) return 0;
            THREADENTRY32 te{ sizeof te };
            const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
            int n = 0;
            for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
            {
                if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
                HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (!t) continue;
                if (SuspendThread(t) != (DWORD)-1)
                {
                    CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                    if (GetThreadContext(t, &c))
                    {
                        c.Dr0 = at[0]; c.Dr1 = at[1]; c.Dr2 = at[2]; c.Dr6 = 0;
                        uint64_t dr7 = c.Dr7 & ~0xFFF003Full;                    // DR0-2: enables, kinds and lengths
                        for (int i = 0; i < 3; i++) if (at[i]) dr7 |= 1ull << (2 * i);   // local, on execute
                        c.Dr7 = dr7;
                        if (SetThreadContext(t, &c)) n++;
                    }
                    ResumeThread(t);
                }
                CloseHandle(t);
            }
            CloseHandle(snap);
            return n;
        }

        // At the breakpoints: the faces linked as the frame builder starts, a face's camera into xmm2-5 (or the root's
        // read) as each view's is loaded, the faces unlinked as it finishes.
        static LONG CALLBACK OnException(EXCEPTION_POINTERS* ep)
        {
            FbViews* self = s_self;
            if (!self || ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
            CONTEXT* c = ep->ContextRecord;
            const uint64_t rip = c->Rip;
            if ((c->Dr6 & 7) == 0) return EXCEPTION_CONTINUE_SEARCH;
            if (rip == self->_exe + kFbStartRva)
            {
                // Only a build that is the only one running links the faces, and only it unlinks them.
                if (self->_active.fetch_add(1) == 0) { self->LinkFaces(); self->_linker = GetCurrentThreadId(); }
                ++self->_builds;
            }
            else if (rip == self->_exe + kFbEndRva)
            {
                if (self->_linker == GetCurrentThreadId()) { self->UnlinkFaces(); self->_linker = 0; }
                self->_active.fetch_sub(1);
            }
            else if (rip == self->_exe + kFbBreakRva)
            {
                const Node* n = reinterpret_cast<const Node*>(c->Rdi);
                if (self->Ours(n))
                {
                    const View* w = reinterpret_cast<const View*>(n);
                    const float* r = w->shot[w->live.load(std::memory_order_acquire)].rows;
                    memcpy(&c->Xmm2, r, 16); memcpy(&c->Xmm3, r + 4, 16); memcpy(&c->Xmm4, r + 8, 16); memcpy(&c->Xmm5, r + 12, 16);
                    ++self->_facesSeen;
                }
                else if (n && n->id == 0)
                {
                    self->_rootSeq.fetch_add(1, std::memory_order_acq_rel);
                    memcpy(self->_rootRows, &c->Xmm2, 16); memcpy(self->_rootRows + 4, &c->Xmm3, 16);
                    memcpy(self->_rootRows + 8, &c->Xmm4, 16); memcpy(self->_rootRows + 12, &c->Xmm5, 16);
                    self->_rootSeq.fetch_add(1, std::memory_order_acq_rel);
                    ++self->_rootSeen;
                }
            }
            else return EXCEPTION_CONTINUE_SEARCH;
            c->Dr6 = 0;
            c->EFlags |= 0x10000;                // resume: run the instruction without breaking on it again
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        static inline FbViews* s_self = nullptr;
        uintptr_t _exe = 0;
        View* _views = nullptr;
        PVOID _veh = nullptr;
        std::atomic<uint32_t> _rootSeq{ 0 };
        float _rootRows[16] = {};
        std::atomic<uint64_t> _rootSeen{ 0 }, _facesSeen{ 0 }, _builds{ 0 };
        std::atomic<int> _gameViews{ 0 }, _facesDrawn{ 0 };
        uint64_t _feetSoldier = 0, _feetCamera = 0;     // present thread only
        std::atomic<int> _active{ 0 };                  // frame builds running now
        std::atomic<DWORD> _linker{ 0 };                // the thread whose build linked the faces
        int _armed = 0;
        double _rho = 0, _lastView = 0;
    };
}
