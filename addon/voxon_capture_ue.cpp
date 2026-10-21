// voxon_capture_ue.cpp - the ReShade shell round capture_gpu.hpp: the GPU half of the re-hosted
// VLED capture (docs/ARCHITECTURE.md). Engine-generic: nothing here knows any one game.
//
//   1. FIND the camera half's render targets. rig.lua clears each one, once, to a face code;
//      every new RGBA16F render target of the configured face size is a candidate, and one texel is
//      read back to see whether it carries a code. No engine offsets, no signatures.
//   2. NOTICE a fresh capture: Unreal draws each capture's result into its target, so a target
//      bound as a render target (or a render pass, or a copy's destination) has been written. When
//      any enabled face has, they are decoded. An engine that writes them some way none of those
//      shows -- a compute pass -- gets a timer at the rig's rate instead (FacesReady).
//   3. DECODE at present, on the game's immediate context, with the game's compute state saved and
//      restored: the plugin's pack + compact passes, then a staging copy into a ring.
//   4. READ the oldest finished copy without waiting -- a frame late, never a stall -- meter the
//      exposure the way the plugin does, and hand the voxels to a worker thread.
//   5. SEND from the worker: display units, VoxelBatch, the overlapped pipe (Feed.cpp). If the host
//      is still reading the last volume, this one is dropped rather than queued.
//
// REDENGINE3 (engine=redengine3, The Witcher 3) has no cameras to find: the rig's script sweeps the game's own camera
// round the cube, a face a frame. Each frame's camera is read from the constant buffer the game binds for its draws
// (sweep_red3.hpp); a face frame's picture and depth are copied at present, and the latest of each face are decoded
// together. The settings reach the script through red3_link.hpp.
//
// REDENGINE4 (engine=redengine4, Cyberpunk 2077) has cameras of its own that render into a texture: the rig's script
// gives V five, one a face, each writing a picture of its own size (red4_rtt.hpp). A face's depth is copied out of the
// cameras' shared depth buffer as its picture is bound for drawing, on the game's own command list; its picture at
// present; the five are decoded together. The settings reach the script as a text file beside it, rig.txt.
//
// FROSTBITE (engine=frostbite, Star Wars Battlefront II) has no rig module: the add-on itself puts the faces on the
// renderer's list of views and sets their cameras (frostbite_views.hpp). Each face's scene depth and lit colour are
// known by their size as they are bound together, copied at present, and decoded together.
//
// THE SIMS 4 (engine=sims4) has no rig module either: the add-on hooks the game's per-frame render job and has the
// game's renderer draw the world again from a camera of the add-on's own for each face, into targets of the add-on's
// own (sims4_views.hpp), which are decoded at present as they are.
//
// No frame data ever touches disk. The settings file and the log are the only files.
#include <reshade.hpp>

#include <windows.h>
#include <DirectXPackedVector.h>
#include <d3d10_1.h>
#include <share.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include "capture_gpu.hpp"
#include "capture_d3d12.hpp"
#include "sweep_red3.hpp"
#include "red3_link.hpp"
#include "red3_native.hpp"
#include "red4_rtt.hpp"
#include "frostbite_views.hpp"
#include "sims4_views.hpp"
#include "cryengine2_views.hpp"
#include "Feed.h"

using namespace reshade::api;

extern "C" __declspec(dllexport) const char* NAME = "Voxon capture (Unreal)";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "The GPU half of Voxel Photonics' VLED volume capture, re-hosted in a shipped Unreal game: "
    "decodes the camera half's scene captures into voxels for the Voxon Capture Rig app.";

namespace
{
    // The handshake rig.lua writes (rig.lua: MARK_R, MARK_A, CODE_BASE).
    constexpr float kMarkR = -1234.0f, kMarkA = -4321.0f, kCodeBase = 1000.0f;
    constexpr float kSurfaceCodeBase = 2000.0f;    // a face's surface-colour target (rig.lua: SURFACE_CODE_BASE)
    constexpr float kSolidCodeBase = 1250.0f;      // a face's solid capture (rig.lua: SOLID_CODE_BASE): below 2048, exact in fp16
    constexpr float kPlayerCodeBase = 1500.0f;     // a face's player capture (rig.lua: PLAYER_CODE_BASE): below 2048, exact in fp16
    constexpr float kGlowCodeBase = 1750.0f;       // a face's glow capture (rig.lua: GLOW_CODE_BASE): below 2048, exact in fp16
    constexpr int kHandshakeAttempts = 600;

    char g_dir[MAX_PATH] = {};
    FILE* g_log = nullptr;

    void Log(const char* fmt, ...)
    {
        if (!g_log) return;
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(g_log, "%02d:%02d:%02d.%03d  ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
        fputc('\n', g_log);
        fflush(g_log);
    }

    // ---- settings ----
    vxcap::Settings g_cfg;
    vxcap::Geometry g_geo;
    std::string g_cfgRaw;
    ULONGLONG g_cfgAt = 0;
    bool g_dropFaces = false;            // set by a resolution change, acted on in Handshake

    // PLAYER LOOK, for the settings line: off, or the player's exposure, gamma, saturation and density.
    std::string PlayerLookText(const vxcap::Settings& c)
    {
        if (!c.playerLook) return "off";
        char s[96];
        sprintf_s(s, "%+.2f stops, gamma %.2f, saturation %.2f, %d voxels deep", c.playerExposure, c.playerGamma,
                  c.playerSaturation, c.playerDensity);
        return s;
    }

    // The settings files' last writes. Read again as soon as either changes - the app's rig-scale keys change the
    // radius many times a second while held, and the volume follows each step - and at least once a second.
    FILETIME g_cfgStamp[2] = {};
    ULONGLONG g_scaleAt = 0;                     // a change of scale alone not yet logged: when it was made
    bool SettingsTouched(const char* ini)
    {
        const char* paths[2] = { ini, g_cfg.controlFile };
        bool changed = false;
        for (int i = 0; i < 2; i++)
        {
            WIN32_FILE_ATTRIBUTE_DATA a{};
            if (!paths[i][0] || !GetFileAttributesExA(paths[i], GetFileExInfoStandard, &a)) continue;
            if (CompareFileTime(&a.ftLastWriteTime, &g_cfgStamp[i]) != 0) { g_cfgStamp[i] = a.ftLastWriteTime; changed = true; }
        }
        return changed;
    }

    void ReloadSettings(bool force)
    {
        const ULONGLONG now = GetTickCount64();
        char path[MAX_PATH];
        sprintf_s(path, "%s\\voxon_capture_ue.ini", g_dir);
        if (g_scaleAt && now - g_scaleAt >= 1000)
        {   // the scale has stayed put a second: its last value, once
            g_scaleAt = 0;
            Log("rig scale: radius %.2f m (%.3f m/unit)", g_cfg.radiusM, g_geo.s);
        }
        if (!SettingsTouched(path) && !force && now - g_cfgAt < 1000) return;
        g_cfgAt = now;
        vxcap::Settings c;
        std::string raw;
        if (!vxcap::LoadSettings(path, c, raw)) { if (force) Log("no %s -- defaults", path); return; }
        if (raw == g_cfgRaw && !force) return;
        if (!force && !g_cfgRaw.empty() && vxcap::WithoutScale(raw) == vxcap::WithoutScale(g_cfgRaw))
        {   // the scale alone: taken at once, logged once it settles
            g_cfg = c; g_cfgRaw = raw;
            g_geo = vxcap::Derive(c);
            g_scaleAt = now;
            return;
        }
        g_scaleAt = 0;
        // A new resolution means new targets: the old ones are the wrong size for the pack pass. Dropped on the
        // present thread, which owns them; rig.lua rebuilds and hands over new codes - which may already have been
        // read, since rig.lua can see the change first (SyncCandidates keeps a face of the new size).
        const bool resChanged = c.faceRes != g_cfg.faceRes || vxcap::SideHeight(c) != vxcap::SideHeight(g_cfg);
        g_cfg = c; g_cfgRaw = raw;
        g_geo = vxcap::Derive(c);
        if (resChanged) g_dropFaces = true;
        Log("settings: on %d, radius %.1f m (%.3f m/unit), feet z %.2f, faces %d%d%d%d%d%d, %d px, %s, tilt %.0f, yaw %.0f%s, %d Hz, camera distance %.2f, merge %.0f/unit, lighting %.2f, auto-exposure %+.2f to %+.2f stops, brightness %+.2f, gamma %.2f, saturation %.2f, player look %s, solid see-through surfaces %s (from %.0f%% opaque), colour range %d-%d, scale %.2f/%.2f/%.2f, display %.1f x %.1f, pipe %s%s",
            c.on, c.radiusM, g_geo.s, c.feetZ, c.faceOn[0], c.faceOn[1], c.faceOn[2], c.faceOn[3], c.faceOn[4], c.faceOn[5],
            c.faceRes, c.perspective ? "perspective" : "orthographic", c.tiltDeg, c.yawDeg, c.turnWithPlayer ? " turning with the player" : "", c.hz, c.camDist, c.mergePerUnit, c.lighting, c.exposureMinEV, c.exposureMaxEV, c.exposureComp, c.gamma, c.saturation, PlayerLookText(c).c_str(), c.solidAll ? "all" : c.solidChosen ? "chosen" : "off", c.solidOpacity * 100.0f, c.colorFloor, c.colorCeiling, c.scaleX, c.scaleY, c.scaleZ, c.boundR, c.boundZ, c.pipe,
            c.controlFile[0] ? " (host panel over the ini)" : "");
    }

    // ---- the camera half's targets ----
    struct Candidate
    {
        uint64_t handle = 0;                // referenced by this add-on from init_resource until it is dropped (Unhold)
        uint32_t w = 0;                     // from init_resource: probed only while it is the configured faceRes
        ULONGLONG born = 0;                 // a remembered target of another size is let go after kRememberMs
        DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
        ID3D11Texture2D* probe = nullptr;   // 1x1 staging: one texel, to read the face code
        bool pending = false;
        int attempts = 0;
    };
    // CANDIDATES ARE HELD: a reference is taken on each in init_resource and given back only here. Remembering them by
    // pointer alone crashed The Ascent on D3D12 at launch (2026-10-11): a target the engine freed between frames was
    // still a candidate, and a shader view made on it read freed memory in the driver. Released never under g_candMtx:
    // the last release fires destroy_resource, which takes it.
    constexpr ULONGLONG kRememberMs = 5000, kOffGraceMs = 5000;
    void Unhold(uint64_t h) { if (h) reinterpret_cast<IUnknown*>(h)->Release(); }

    std::mutex g_candMtx;
    struct NewCand { uint64_t handle; uint32_t w; ULONGLONG born; };
    std::vector<NewCand> g_newCands;        // from init_resource, any thread, each already referenced
    std::vector<uint64_t> g_deadRes;        // from destroy_resource, any thread
    std::vector<Candidate> g_cands;         // present thread only

    struct Face
    {
        std::atomic<uint64_t> res{ 0 };     // read by bind_render_targets on the render thread; held, as a candidate was
        int w = 0;                          // the target's width: decoded only while it is the configured faceRes
        int h = 0;                          // ... and its height, while it is the display's shape (FaceHeight)
        ULONGLONG offSince = 0;             // turned off in the settings since; let go after kOffGraceMs
        std::atomic<bool> dirty{ false };
        ID3D11ShaderResourceView* srv = nullptr;
        // UNITY: the depth buffer drawn with this face's target, from the bind event; held, as the target is. Its view
        // is made on the present thread when it changes.
        std::atomic<uint64_t> depthRes{ 0 };
        uint64_t depthViewOf = 0;
        ID3D11ShaderResourceView* depthSrv = nullptr;
        // SURFACE COLOUR: the face's base-colour target (lighting below 1), found by its own code; held, as res is.
        std::atomic<uint64_t> surfaceRes{ 0 };
        int surfaceW = 0, surfaceH = 0;
        ID3D11ShaderResourceView* surfaceSrv = nullptr;
        // PLAYER LOOK: the face's player capture (playerLook), found by its own code; held, as res is. UNITY: the depth
        // buffer drawn with it, which the decode reads instead, found and held as the face's own depth is.
        std::atomic<uint64_t> playerRes{ 0 };
        int playerW = 0, playerH = 0;
        ID3D11ShaderResourceView* playerSrv = nullptr;
        std::atomic<uint64_t> playerDepthRes{ 0 };
        uint64_t playerDepthViewOf = 0;
        ID3D11ShaderResourceView* playerDepthSrv = nullptr;
        // SOLID SURFACES: the face's solid capture (solidMaterials / solidAll), found by its own code; held, as res is.
        // UNITY: the depth buffer drawn with it, which the decode reads instead, as the player capture's.
        std::atomic<uint64_t> solidRes{ 0 };
        int solidW = 0, solidH = 0;
        ID3D11ShaderResourceView* solidSrv = nullptr;
        std::atomic<uint64_t> solidDepthRes{ 0 };
        uint64_t solidDepthViewOf = 0;
        ID3D11ShaderResourceView* solidDepthSrv = nullptr;
        // ...and their glow capture (solidOpacity above 0): how opaque they are, by its own code; held, as res is.
        std::atomic<uint64_t> glowRes{ 0 };
        int glowW = 0, glowH = 0;
        ID3D11ShaderResourceView* glowSrv = nullptr;
    };
    Face g_face[6];
    std::atomic<bool> g_anyFace{ false };
    std::atomic<uint64_t> g_writes[6];          // how often each face's target was written: the capture rate per face
    std::atomic<uint64_t> g_faceDraws[6];       // draws made into each face's target: zero while the engine only clears it
    thread_local int t_drawFace = -1;           // the face whose target the calling thread has bound for drawing, or -1
    ULONGLONG g_foundAt = 0, g_timedAt = 0;     // the first face's handshake; the timer's last decode
    bool g_timedLogged = false;

    // ---- the decode: one of these, by the game's API ----
    vxcap::Decoder g_dec;            // D3D11
    ID3D11Device* g_dev = nullptr;
    vxcap::Decoder12 g_dec12;        // D3D12 (Black Myth: Wukong, Oblivion Remastered; The Ascent can run either)
    ID3D12Device* g_dev12 = nullptr;
    bool g_decOk = false;
    uint64_t g_probeCand[vxcap::Decoder12::kProbeMax] = {};   // which candidates the D3D12 probe in flight is reading
    float g_autoExp = 1.0f;
    ULONGLONG g_expAt = 0;
    std::vector<vxcap::Voxel> g_read;

    // ---- the worker ----
    std::mutex g_wMtx;
    std::condition_variable g_wCv;
    std::vector<vxcap::Voxel> g_wBuf;
    vxcap::Settings g_wCfg;
    vxcap::Geometry g_wGeo;
    bool g_wHas = false, g_wQuit = false;
    std::thread g_worker;
    std::atomic<bool> g_pipeUp{ false };
    constexpr unsigned kHostWaitMs = 30;     // longer than a host that keeps up ever needs; a stuck one costs a drop

    // ---- REDENGINE3: the game's own camera, swept round the cube ----
    struct Red3Face
    {
        resource colour = { 0 }, depth = { 0 };   // this add-on's copies: copy destinations between decodes
        vxcap::Red3Camera cam{};
        bool have = false;                        // both copied, for the camera in cam
    };
    Red3Face g_r3[5];
    std::mutex g_r3Mtx;                           // the camera of the frame being drawn: found on any recording thread
    vxcap::Red3Camera g_r3Cam{};
    bool g_r3Found = false;
    int g_r3Checked = 0;                          // constant buffers looked into this frame, before the camera was found
    // The scene's depth: of the depth buffers drawn into at the back buffer's size, the one bound most in a frame (the game
    // has others that size); and the state the game's last barrier left each of them in.
    std::mutex g_r3DepthMtx;
    std::map<uint64_t, int> g_r3DepthBinds;       // this frame's
    std::map<uint64_t, uint32_t> g_r3DepthStates;
    uint64_t g_r3Depth = 0;                       // chosen at present, from the frame just drawn
    bool g_r3Fresh = false;                       // a face completed since the last decode
    int g_r3Latest = -1;                          // the face completed last: the volume is placed round its centre
    uint32_t g_r3W = 0, g_r3H = 0;               // the game's frame (its back buffer)
    format g_r3ColourFmt = format::unknown;
    resource g_r3Player = { 0 };                  // the player's last frame: shown again in place of each face frame
    LONGLONG g_r3DecDue = 0;                      // when the next decode is due (performance counter)
    std::atomic<uint64_t> g_r3Frames{ 0 }, g_r3Faces{ 0 }, g_r3Depths{ 0 }, g_r3NoCamera{ 0 }, g_r3Disagree{ 0 };
    vxcap::Red3Link g_link;

    // REDENGINE3, native views (red3_native.hpp, red3Native=1, the default): the renderer's render-frame hooked. Each
    // world frame is rendered again from each face's camera before the player's own - the frame's two camera sets
    // rewritten, put back afterwards - so the cube's faces come in the same frame as the player's view and the game's
    // camera never moves (the sweep moved it: the world streamed in and out behind it, and the player lost the controls).
    // The script reports the volume's centre and the rig's yaw, and that it wants the faces. Every render presents: the
    // present path takes an extra view's frame as the face g_r3nView names. The lens is the player's; the far plane just
    // past the box. Not hooked (another build of the game), the script sweeps as before.
    vxcap::Red3RenderFrameHook g_r3Native;
    bool g_r3NativeOn = false;
    std::atomic<int> g_r3nView{ -1 };                     // the face being rendered as an extra view; -1 the player's
    std::atomic<uint64_t> g_r3nWorld{ 0 }, g_r3nExtra{ 0 };

    // REDENGINE3, native views: the HUD kept out of the faces. The game draws its HUD into a layer of its own - the last
    // window-sized target drawn into, with a depth buffer, before the frame's one draw onto the back buffer, which puts
    // the scene and that layer together (traced 2026-10-13). The layer is learnt from the player's own renders, and
    // while a face renders every draw into it is skipped: the face composites without the HUD, the player's frame keeps
    // it. The target bound, and the last one drawn into, are kept per recording thread (draws are thousands a frame).
    swapchain* g_r3Sc = nullptr;
    std::atomic<uint64_t> g_r3HudLayer{ 0 };
    std::atomic<uint64_t> g_r3BackBuffer{ 0 };     // the back buffer the player's render draws onto
    struct Red3Recording { command_list* cmd = nullptr; uint64_t rtv0 = 0; bool depth = false; uint64_t drawnRtv0 = 0; bool drawnDepth = false; };
    thread_local Red3Recording t_r3Rec;

    // A draw: skipped when it is into the HUD layer during a face's render; otherwise noted as the last one drawn.
    bool Red3Draw(command_list* cmd)
    {
        if (!g_r3NativeOn) return false;
        Red3Recording& rec = t_r3Rec;
        if (rec.cmd != cmd) return false;
        if (g_r3nView >= 0 && rec.rtv0 && rec.rtv0 == g_r3HudLayer) return true;
        rec.drawnRtv0 = rec.rtv0;
        rec.drawnDepth = rec.depth;
        return false;
    }
    void CountFaceDraw() { if (t_drawFace >= 0) ++g_faceDraws[t_drawFace]; }
    bool on_draw(command_list* cmd, uint32_t, uint32_t, uint32_t, uint32_t) { CountFaceDraw(); return Red3Draw(cmd); }
    bool on_draw_indexed(command_list* cmd, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) { CountFaceDraw(); return Red3Draw(cmd); }

    // Render targets bound: noted for the draws; the back buffer bound in the player's render names the HUD layer.
    void Red3Bind(command_list* cmd, uint32_t count, const resource_view* rtvs, resource_view dsv)
    {
        if (!g_r3NativeOn) return;
        device* d = cmd->get_device();
        const uint64_t rt0 = count && rtvs[0].handle ? d->get_resource_from_view(rtvs[0]).handle : 0;
        Red3Recording& rec = t_r3Rec;
        if (rec.cmd != cmd) rec = Red3Recording{};
        rec.cmd = cmd;
        if (g_r3nView < 0 && rt0 && rt0 == g_r3BackBuffer && rec.drawnRtv0 && rec.drawnDepth && rec.drawnRtv0 != rt0)
        {
            const resource_desc hd = d->get_resource_desc({ rec.drawnRtv0 }), bd = d->get_resource_desc({ rt0 });
            if (hd.texture.width == bd.texture.width && hd.texture.height == bd.texture.height && g_r3HudLayer != rec.drawnRtv0)
            {
                g_r3HudLayer = rec.drawnRtv0;
                Log("redengine3 native: the HUD layer is %llx (%ux%u)", (unsigned long long)rec.drawnRtv0, hd.texture.width, hd.texture.height);
            }
        }
        rec.rtv0 = rt0;
        rec.depth = dsv.handle != 0;
    }

    uint64_t Red3RenderFrameDetour(void* renderer, void* frame, void* scene)
    {
        float rep[vxcap::kLinkEnd - vxcap::kLinkViewsOn];
        if (scene && g_cfg.red3Native && g_link.Report(rep) && rep[0] > 0.5f)
        {
            ++g_r3nWorld;
            uint8_t* fr = (uint8_t*)frame;
            static uint8_t saved[vxcap::kRed3FrameCameraBytes];
            const size_t from = vxcap::kRed3FrameCameraSet[0], bytes = vxcap::kRed3FrameCameraBytes - from;
            memcpy(saved, fr + from, bytes);
            float sx, sy;
            memcpy(&sx, fr + vxcap::kRed3FrameCameraSet[0] + 0x180, 4);   // the player's projection: its 1 / tan
            memcpy(&sy, fr + vxcap::kRed3FrameCameraSet[0] + 0x194, 4);
            const float centre[3] = { rep[1], rep[2], rep[3] };
            for (int k = 0; k < 5; k++)
            {
                if (!g_cfg.faceOn[k] || !(sx > 0.05f) || !(sy > 0.05f)) continue;
                vxcap::Red3Camera cam = vxcap::Red3Place(k, centre, rep[4], g_geo, 1.0f / sx, 1.0f / sy, g_geo.tilt);
                float d2 = 0;
                for (int i = 0; i < 3; i++) d2 += (cam.pos[i] - centre[i]) * (cam.pos[i] - centre[i]);
                cam.farM = (std::sqrt(d2) + 2.0f * vxcap::Reach(g_geo, k)) * 1.5f;
                vxcap::Red3WriteFrameCamera(fr, cam);
                g_r3nView = k;
                ++g_r3nExtra;
                g_r3Native.Original()(renderer, frame, scene);
            }
            g_r3nView = -1;
            memcpy(fr + from, saved, bytes);
            if (g_r3Sc) g_r3BackBuffer = g_r3Sc->get_current_back_buffer().handle;   // the player's render draws onto it
        }
        return g_r3Native.Original()(renderer, frame, scene);
    }

    // ---- stats, logged every few seconds ----
    std::atomic<uint64_t> g_runs{ 0 }, g_volumes{ 0 }, g_sent{ 0 }, g_dropHost{ 0 }, g_dropWorker{ 0 }, g_lastCount{ 0 };
    std::atomic<uint64_t> g_mergeIn{ 0 }, g_mergeOut{ 0 };   // points into the per-voxel merge, and removed by it
    uint64_t g_presents = 0;    // the game's own frame rate, beside the volume rate it pays for
    double g_presentMs = 0;
    ULONGLONG g_statAt = 0;

    void Worker()
    {
        voxon::FeedPipe pipe;
        voxon::FeedWriter w;
        std::vector<voxon::VoxelPoint> pts;
        std::vector<vxcap::Voxel> buf;
        std::vector<vxcap::GizmoPoint> gizmo;
        ULONGLONG tried = 0;
        for (;;)
        {
            vxcap::Settings c; vxcap::Geometry g;
            {
                std::unique_lock<std::mutex> lk(g_wMtx);
                g_wCv.wait(lk, [] { return g_wHas || g_wQuit; });
                if (g_wQuit) return;
                buf.swap(g_wBuf);
                c = g_wCfg; g = g_wGeo;
                g_wHas = false;
            }
            if (!pipe.IsConnected())
            {
                if (GetTickCount64() - tried < 1000) continue;
                tried = GetTickCount64();
                if (!pipe.Connect(c.pipe)) continue;
                Log("pipe: connected to \\\\.\\pipe\\%s", c.pipe);
                g_pipeUp = true;
                w.Clear(); w.Reset();
                pipe.Write(w.Data(), w.Size());
            }
            // A SHORT WAIT FOR THE HOST, not a drop on the first Busy(). Volumes come off the readback ring unevenly -
            // two can land a few ms apart - and the host takes about 5 ms to read and store one (2026-10-11, after
            // GameKit stopped allocating per message), so dropping at once threw away about one volume in five that
            // the host would have taken a moment later: decoded 23-24/s, sent 17-21. This is the worker's own thread,
            // so the wait costs the game nothing. A volume that arrives meanwhile supersedes this one: newest wins.
            if (!pipe.WaitIdle(kHostWaitMs)) { ++g_dropHost; continue; }
            {
                std::lock_guard<std::mutex> lk(g_wMtx);
                if (g_wHas) { ++g_dropHost; continue; }
            }

            // Already display units, merged: the compact pass did both on the GPU. This only reformats for the wire.
            pts.resize(buf.size());
            for (size_t i = 0; i < buf.size(); i++)
            {
                const vxcap::Voxel& v = buf[i];
                pts[i] = { v.x, v.y, v.z, (uint8_t)(v.rgb >> 16), (uint8_t)(v.rgb >> 8), (uint8_t)v.rgb };
            }
            if (c.showCameras)
            {
                gizmo.clear();
                vxcap::CameraGizmo(c, gizmo);
                for (const auto& q : gizmo) pts.push_back({ q.x, q.y, q.z, q.r, q.g, q.b });
            }
            w.Clear();
            w.VoxelBatch(pts, 0, pts.size());
            if (!pipe.Write(w.Data(), w.Size()))
            {
                Log("pipe: broke -- retrying");
                pipe.Disconnect();
                g_pipeUp = false;
                continue;
            }
            ++g_sent;
        }
    }

    // Is (r, g, a) a face code rig.lua wrote? The face index 0-5, 6-11 for a face's surface-colour target, 12-17 for its
    // player capture, 18-23 its solid capture, 24-29 its glow capture, or -1.
    int FaceCode(float r, float g, float a)
    {
        if (std::fabs(r - kMarkR) >= 1 || std::fabs(a - kMarkA) >= 2) return -1;
        const int face = (int)std::lround(g - kCodeBase), surface = (int)std::lround(g - kSurfaceCodeBase);
        const int player = (int)std::lround(g - kPlayerCodeBase), solid = (int)std::lround(g - kSolidCodeBase);
        const int glow = (int)std::lround(g - kGlowCodeBase);
        return (face >= 0 && face < 6) ? face : (surface >= 0 && surface < 6) ? 6 + surface
             : (player >= 0 && player < 6) ? 12 + player : (solid >= 0 && solid < 6) ? 18 + solid
             : (glow >= 0 && glow < 6) ? 24 + glow : -1;
    }

    void DropGlow(Face& f)
    {
        if (f.glowSrv) { f.glowSrv->Release(); f.glowSrv = nullptr; }
        f.glowW = 0; f.glowH = 0;
        Unhold(f.glowRes.exchange(0));
    }

    void DropSolid(Face& f)
    {
        if (f.solidSrv) { f.solidSrv->Release(); f.solidSrv = nullptr; }
        if (f.solidDepthSrv) { f.solidDepthSrv->Release(); f.solidDepthSrv = nullptr; }
        f.solidDepthViewOf = 0;
        Unhold(f.solidDepthRes.exchange(0));
        f.solidW = 0; f.solidH = 0;
        Unhold(f.solidRes.exchange(0));
    }

    void DropPlayer(Face& f)
    {
        if (f.playerSrv) { f.playerSrv->Release(); f.playerSrv = nullptr; }
        if (f.playerDepthSrv) { f.playerDepthSrv->Release(); f.playerDepthSrv = nullptr; }
        f.playerDepthViewOf = 0;
        Unhold(f.playerDepthRes.exchange(0));
        f.playerW = 0; f.playerH = 0;
        Unhold(f.playerRes.exchange(0));
    }

    void DropSurface(Face& f)
    {
        if (f.surfaceSrv) { f.surfaceSrv->Release(); f.surfaceSrv = nullptr; }
        f.surfaceW = 0; f.surfaceH = 0;
        Unhold(f.surfaceRes.exchange(0));
    }

    void DropFace(Face& f)
    {
        const uint64_t h = f.res.exchange(0);
        f.dirty = false;
        if (f.srv) { f.srv->Release(); f.srv = nullptr; }
        if (f.depthSrv) { f.depthSrv->Release(); f.depthSrv = nullptr; }
        f.depthViewOf = 0;
        Unhold(f.depthRes.exchange(0));
        Unhold(h);
        DropSurface(f);
        DropPlayer(f);
        DropSolid(f);
        DropGlow(f);
    }

    // UNITY: a shader view on a face's depth buffer (or its player capture's: what "player"), made again whenever the
    // bind event has seen a new one. The buffer is typeless; the view reads its depth channel. False while there is
    // none, or none can be made.
    bool DepthView11(ID3D11Device* dev, int v, const std::atomic<uint64_t>& from, uint64_t& viewOf,
                     ID3D11ShaderResourceView*& srv, const char* what)
    {
        const uint64_t res = from;
        if (res == viewOf) return srv != nullptr;
        if (srv) { srv->Release(); srv = nullptr; }
        viewOf = res;
        if (!res) return false;
        auto* tex = reinterpret_cast<ID3D11Texture2D*>(res);
        D3D11_TEXTURE2D_DESC td{};
        tex->GetDesc(&td);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        switch (td.Format)
        {
        case DXGI_FORMAT_R32_TYPELESS:      sd.Format = DXGI_FORMAT_R32_FLOAT; break;
        case DXGI_FORMAT_R24G8_TYPELESS:    sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
        case DXGI_FORMAT_R32G8X24_TYPELESS: sd.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
        case DXGI_FORMAT_R16_TYPELESS:      sd.Format = DXGI_FORMAT_R16_UNORM; break;
        default:
            Log("face %d: its %sdepth buffer (format %d) cannot be read by a shader", v, what, (int)td.Format);
            return false;
        }
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (!(td.BindFlags & D3D11_BIND_SHADER_RESOURCE) || FAILED(dev->CreateShaderResourceView(tex, &sd, &srv)))
        {
            Log("face %d: no shader view on its %sdepth buffer (format %d, bind flags %u)", v, what, (int)td.Format, td.BindFlags);
            srv = nullptr;
            return false;
        }
        Log("face %d: %sdepth buffer found, %ux%u (format %d)", v, what, td.Width, td.Height, (int)td.Format);
        return true;
    }

    // A handshake's match: the candidate's reference becomes the face's, and the face it replaces is let go.
    void TakeFace(Face& f, uint64_t h)
    {
        const uint64_t old = f.res.exchange(h);
        if (old && old != h) Unhold(old);
    }

    // New and destroyed targets, a resolution change: the bookkeeping both APIs share, on the present thread.
    void SyncCandidates()
    {
        if (g_dropFaces)
        {
            g_dropFaces = false;
            for (int v = 0; v < 6; v++)
            {
                Face& f = g_face[v];
                const int fh = vxcap::FaceHeight(v, g_cfg);
                if (f.res && (f.w != g_cfg.faceRes || f.h != fh)) DropFace(f);
                if (f.surfaceRes && (f.surfaceW != g_cfg.faceRes || f.surfaceH != fh)) DropSurface(f);
                if (f.playerRes && (f.playerW != g_cfg.faceRes || f.playerH != fh)) DropPlayer(f);
                if (f.solidRes && (f.solidW != g_cfg.faceRes || f.solidH != fh)) DropSolid(f);
                if (f.glowRes && (f.glowW != g_cfg.faceRes || f.glowH != fh)) DropGlow(f);
            }
            Log("faces of another size dropped: new capture resolution %d", g_cfg.faceRes);
        }
        // Turned off: let go once it has stayed off a while. At once lost a camera when the settings flipped off and on
        // (2026-10-11): rig.lua had already rebuilt with it, its face code was gone, and it could not be found again.
        const ULONGLONG tNow = GetTickCount64();
        for (int v = 0; v < 6; v++)
        {
            Face& f = g_face[v];
            if (g_cfg.faceOn[v] || !f.res) { f.offSince = 0; continue; }
            if (!f.offSince) f.offSince = tNow;
            else if (tNow - f.offSince > kOffGraceMs) { DropFace(f); f.offSince = 0; }
        }
        std::vector<uint64_t> letGo;
        std::vector<IUnknown*> drop;    // released once g_candMtx is let go: no D3D call under it (FbReleaseLetGo)
        auto later = [&drop](auto*& p) { if (p) { drop.push_back(p); p = nullptr; } };
        {
            std::lock_guard<std::mutex> lk(g_candMtx);
            for (uint64_t h : g_deadRes)
            {
                for (auto it = g_cands.begin(); it != g_cands.end();)
                    if (it->handle == h) { later(it->probe); it = g_cands.erase(it); } else ++it;
                for (auto& f : g_face)
                {
                    if (f.res == h) { f.res = 0; later(f.srv); }
                    if (f.surfaceRes == h) { f.surfaceRes = 0; f.surfaceW = 0; later(f.surfaceSrv); }
                    if (f.playerRes == h) { f.playerRes = 0; f.playerW = 0; later(f.playerSrv); }
                    if (f.solidRes == h) { f.solidRes = 0; f.solidW = 0; later(f.solidSrv); }
                    if (f.glowRes == h) { f.glowRes = 0; f.glowW = 0; later(f.glowSrv); }
                }
            }
            g_deadRes.clear();
            for (const auto& n : g_newCands) { Candidate c; c.handle = n.handle; c.w = n.w; c.born = n.born; g_cands.push_back(c); }
            g_newCands.clear();
        }
        for (IUnknown* q : drop) q->Release();
        // EVERY candidate not matched goes after kRememberMs - of any size: long enough for a new resolution to reach this
        // add-on, and for a face's code (held 2 s by rig.lua) to be read. Only other sizes went at first, and the engine's
        // own targets of the camera size piled up held: 7.6 GB of an 8 GB card used, the game at 11 fps, and the rig's
        // new cameras never probed behind them (2026-10-11).
        const ULONGLONG now = GetTickCount64();
        for (auto it = g_cands.begin(); it != g_cands.end();)
            if (now - it->born > kRememberMs)
            { if (it->probe) it->probe->Release(); letGo.push_back(it->handle); it = g_cands.erase(it); }
            else ++it;
        for (uint64_t h : letGo) Unhold(h);
    }

    void Handshake11(ID3D11Device* dev, ID3D11DeviceContext* ctx)
    {
        SyncCandidates();
        for (auto it = g_cands.begin(); it != g_cands.end();)
        {
            Candidate& c = *it;
            if ((int)c.w != g_cfg.faceRes) { ++it; continue; }    // remembered, not probed
            auto* tex = reinterpret_cast<ID3D11Texture2D*>(c.handle);
            if (!c.probe)
            {
                D3D11_TEXTURE2D_DESC d{};
                tex->GetDesc(&d);
                c.fmt = d.Format;
                d.Width = d.Height = 1; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc = { 1, 0 };
                d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
                if (FAILED(dev->CreateTexture2D(&d, nullptr, &c.probe))) { const uint64_t h = c.handle; it = g_cands.erase(it); Unhold(h); continue; }
            }
            if (c.pending)
            {
                D3D11_MAPPED_SUBRESOURCE m{};
                const HRESULT hr = ctx->Map(c.probe, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
                if (hr == DXGI_ERROR_WAS_STILL_DRAWING) { ++it; continue; }
                c.pending = false;
                if (SUCCEEDED(hr))
                {
                    using DirectX::PackedVector::XMConvertHalfToFloat;
                    const auto* h = (const DirectX::PackedVector::HALF*)m.pData;
                    const float r = XMConvertHalfToFloat(h[0]), g = XMConvertHalfToFloat(h[1]), a = XMConvertHalfToFloat(h[3]);
                    ctx->Unmap(c.probe, 0);
                    const int face = FaceCode(r, g, a);
                    if (face >= 24)
                    {
                        // SOLID SURFACES' GLOW: the face's glow capture. A view on it, and it is held as the face's.
                        Face& f = g_face[face - 24];
                        ID3D11ShaderResourceView* view = nullptr;
                        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                        sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                        sd.Texture2D.MipLevels = 1;
                        const uint64_t h = c.handle;
                        if (SUCCEEDED(dev->CreateShaderResourceView(tex, &sd, &view)))
                        {
                            D3D11_TEXTURE2D_DESC td{};
                            tex->GetDesc(&td);
                            DropGlow(f);
                            f.glowSrv = view;
                            f.glowRes = h;
                            f.glowW = (int)td.Width; f.glowH = (int)td.Height;
                            Log("face %d: glow capture found, %ux%u, after %d look(s)", face - 24, td.Width, td.Height, c.attempts + 1);
                        }
                        else { Log("face %d: glow capture found, but no shader view on it", face - 24); Unhold(h); }
                        c.probe->Release();
                        it = g_cands.erase(it);
                        continue;
                    }
                    if (face >= 18)
                    {
                        // SOLID SURFACES: the face's solid capture. A view on it, and it is held as the face's.
                        Face& f = g_face[face - 18];
                        ID3D11ShaderResourceView* view = nullptr;
                        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                        sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                        sd.Texture2D.MipLevels = 1;
                        const uint64_t h = c.handle;
                        if (SUCCEEDED(dev->CreateShaderResourceView(tex, &sd, &view)))
                        {
                            D3D11_TEXTURE2D_DESC td{};
                            tex->GetDesc(&td);
                            DropSolid(f);
                            f.solidSrv = view;
                            f.solidRes = h;
                            f.solidW = (int)td.Width; f.solidH = (int)td.Height;
                            Log("face %d: solid capture found, %ux%u, after %d look(s)", face - 18, td.Width, td.Height, c.attempts + 1);
                        }
                        else { Log("face %d: solid capture found, but no shader view on it", face - 18); Unhold(h); }
                        c.probe->Release();
                        it = g_cands.erase(it);
                        continue;
                    }
                    if (face >= 12)
                    {
                        // PLAYER LOOK: the face's player capture. A view on it, and it is held as the face's.
                        Face& f = g_face[face - 12];
                        ID3D11ShaderResourceView* view = nullptr;
                        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                        sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                        sd.Texture2D.MipLevels = 1;
                        const uint64_t h = c.handle;
                        if (SUCCEEDED(dev->CreateShaderResourceView(tex, &sd, &view)))
                        {
                            D3D11_TEXTURE2D_DESC td{};
                            tex->GetDesc(&td);
                            DropPlayer(f);
                            f.playerSrv = view;
                            f.playerRes = h;
                            f.playerW = (int)td.Width; f.playerH = (int)td.Height;
                            Log("face %d: player capture found, %ux%u, after %d look(s)", face - 12, td.Width, td.Height, c.attempts + 1);
                        }
                        else { Log("face %d: player capture found, but no shader view on it", face - 12); Unhold(h); }
                        c.probe->Release();
                        it = g_cands.erase(it);
                        continue;
                    }
                    if (face >= 6)
                    {
                        // SURFACE COLOUR: the face's base-colour target. A view on it, and it is held as the face's.
                        Face& f = g_face[face - 6];
                        ID3D11ShaderResourceView* view = nullptr;
                        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                        sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                        sd.Texture2D.MipLevels = 1;
                        const uint64_t h = c.handle;
                        if (SUCCEEDED(dev->CreateShaderResourceView(tex, &sd, &view)))
                        {
                            D3D11_TEXTURE2D_DESC td{};
                            tex->GetDesc(&td);
                            DropSurface(f);
                            f.surfaceSrv = view;
                            f.surfaceRes = h;
                            f.surfaceW = (int)td.Width; f.surfaceH = (int)td.Height;
                            Log("face %d: surface colour found, %ux%u, after %d look(s)", face - 6, td.Width, td.Height, c.attempts + 1);
                        }
                        else { Log("face %d: surface colour found, but no shader view on it", face - 6); Unhold(h); }
                        c.probe->Release();
                        it = g_cands.erase(it);
                        continue;
                    }
                    if (face >= 0)
                    {
                        Face& f = g_face[face];
                        if (f.srv) { f.srv->Release(); f.srv = nullptr; }
                        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                        sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                        sd.Texture2D.MipLevels = 1;
                        const uint64_t h = c.handle;
                        if (FAILED(dev->CreateShaderResourceView(tex, &sd, &f.srv)))
                        {
                            Log("face %d: found, but no shader view on it", face);
                            c.probe->Release();
                            it = g_cands.erase(it);
                            Unhold(h);
                            continue;
                        }
                        else
                        {
                            D3D11_TEXTURE2D_DESC td{};
                            tex->GetDesc(&td);
                            TakeFace(f, h);
                            f.w = (int)td.Width; f.h = (int)td.Height;
                            f.dirty = false;
                            if (!g_anyFace) g_foundAt = GetTickCount64();
                            g_anyFace = true;
                            Log("face %d: found, %ux%u (format %d), after %d look(s)", face, td.Width, td.Height, (int)c.fmt, c.attempts + 1);
                        }
                        c.probe->Release();
                        it = g_cands.erase(it);
                        continue;
                    }
                }
                if (++c.attempts >= kHandshakeAttempts) { const uint64_t h = c.handle; c.probe->Release(); it = g_cands.erase(it); Unhold(h); continue; }
            }
            if (!c.pending)
            {
                const D3D11_BOX one{ 0, 0, 0, 1, 1, 1 };
                ctx->CopySubresourceRegion(c.probe, 0, 0, 0, 0, tex, 0, &one);
                c.pending = true;
            }
            ++it;
        }
    }

    // D3D12: the probe reads texel (0,0) of up to 16 candidates at once on the GPU; the result arrives a frame or two
    // later. The target itself is the face -- the decoder makes its own views of it every run.
    void Handshake12()
    {
        SyncCandidates();
        float vals[vxcap::Decoder12::kProbeMax][4];
        int n = 0;
        if (g_dec12.ProbeResult(vals, n))
        {
            for (int i = 0; i < n; i++)
            {
                const uint64_t h = g_probeCand[i];
                auto it = std::find_if(g_cands.begin(), g_cands.end(), [h](const Candidate& c) { return c.handle == h; });
                if (it == g_cands.end()) continue;                       // let go while the probe was in flight
                const int face = FaceCode(vals[i][0], vals[i][1], vals[i][3]);
                if (face >= 24)
                {
                    // SOLID SURFACES' GLOW: the face's glow capture, held as the face's is; the decoder makes its views.
                    Face& f = g_face[face - 24];
                    const D3D12_RESOURCE_DESC rd = reinterpret_cast<ID3D12Resource*>(h)->GetDesc();
                    DropGlow(f);
                    f.glowRes = h;
                    f.glowW = (int)rd.Width; f.glowH = (int)rd.Height;
                    Log("face %d: glow capture found on D3D12, %llux%u, after %d look(s)", face - 24, (unsigned long long)rd.Width, rd.Height, it->attempts + 1);
                    g_cands.erase(it);
                }
                else if (face >= 18)
                {
                    // SOLID SURFACES: the face's solid capture, held as the face's is; the decoder makes its views.
                    Face& f = g_face[face - 18];
                    const D3D12_RESOURCE_DESC rd = reinterpret_cast<ID3D12Resource*>(h)->GetDesc();
                    DropSolid(f);
                    f.solidRes = h;
                    f.solidW = (int)rd.Width; f.solidH = (int)rd.Height;
                    Log("face %d: solid capture found on D3D12, %llux%u, after %d look(s)", face - 18, (unsigned long long)rd.Width, rd.Height, it->attempts + 1);
                    g_cands.erase(it);
                }
                else if (face >= 12)
                {
                    // PLAYER LOOK: the face's player capture, held as the face's is; the decoder makes its views.
                    Face& f = g_face[face - 12];
                    const D3D12_RESOURCE_DESC rd = reinterpret_cast<ID3D12Resource*>(h)->GetDesc();
                    DropPlayer(f);
                    f.playerRes = h;
                    f.playerW = (int)rd.Width; f.playerH = (int)rd.Height;
                    Log("face %d: player capture found on D3D12, %llux%u, after %d look(s)", face - 12, (unsigned long long)rd.Width, rd.Height, it->attempts + 1);
                    g_cands.erase(it);
                }
                else if (face >= 6)
                {
                    // SURFACE COLOUR: the face's base-colour target, held as the face's is; the decoder makes its views.
                    Face& f = g_face[face - 6];
                    const D3D12_RESOURCE_DESC rd = reinterpret_cast<ID3D12Resource*>(h)->GetDesc();
                    DropSurface(f);
                    f.surfaceRes = h;
                    f.surfaceW = (int)rd.Width; f.surfaceH = (int)rd.Height;
                    Log("face %d: surface colour found on D3D12, %llux%u, after %d look(s)", face - 6, (unsigned long long)rd.Width, rd.Height, it->attempts + 1);
                    g_cands.erase(it);
                }
                else if (face >= 0)
                {
                    Face& f = g_face[face];
                    const D3D12_RESOURCE_DESC rd = reinterpret_cast<ID3D12Resource*>(h)->GetDesc();
                    TakeFace(f, h);
                    f.w = (int)rd.Width; f.h = (int)rd.Height;
                    f.dirty = false;
                    if (!g_anyFace) g_foundAt = GetTickCount64();
                    g_anyFace = true;
                    Log("face %d: found on D3D12, %llux%u, after %d look(s)", face, (unsigned long long)rd.Width, rd.Height, it->attempts + 1);
                    g_cands.erase(it);
                }
                else if (++it->attempts >= kHandshakeAttempts) { g_cands.erase(it); Unhold(h); }
            }
        }
        if (!g_dec12.ProbeBusy() && !g_cands.empty())
        {
            ID3D12Resource* res[vxcap::Decoder12::kProbeMax];
            int m = 0;
            for (auto& c : g_cands)
            {
                if (m == vxcap::Decoder12::kProbeMax) break;
                if ((int)c.w != g_cfg.faceRes) continue;            // remembered, not probed
                g_probeCand[m] = c.handle;
                res[m++] = reinterpret_cast<ID3D12Resource*>(c.handle);
            }
            if (g_dec12.ProbeSubmit(res, m))
            {
                // Round robin: those just probed go to the back, so the next batch reaches the rest. Always the first
                // sixteen, and the rig's cameras could wait behind candidates that would never match.
                auto probed = [m](const Candidate& c) { for (int i = 0; i < m; i++) if (g_probeCand[i] == c.handle) return true; return false; };
                std::stable_partition(g_cands.begin(), g_cands.end(), [&](const Candidate& c) { return !probed(c); });
            }
        }
    }

    // ---- REDENGINE4: the render-to-texture cameras' faces (red4_rtt.hpp) ----
    struct Red4Face
    {
        resource colour = { 0 }, depth = { 0 };   // this add-on's copies: copy destinations between decodes
        uint64_t game = 0;                        // the game's picture for the face, its texture of the face's size
        uint32_t gameState = (uint32_t)resource_usage::render_target;   // ... and the state the game left it in
        uint64_t depths = 0;                      // depth copies made
    };
    Red4Face g_r4[vxcap::kRed4Faces];
    std::mutex g_r4Mtx;
    // The cameras' shared scene depth buffer (faceRes square, D32 S8), copied each time a pass of theirs finishes
    // writing it - its state then the barrier's own - into this add-on's carry, which is only ever moved between states
    // by this add-on. In the GPU's order the carry then holds what the shared buffer held at its last such barrier: when
    // a face's picture is drawn, that face's depth (red4_rtt.hpp).
    resource g_r4Carry = { 0 };
    std::atomic<bool> g_r4Carried{ false };
    // Every square texture's size and format, from its creation: barriers come by the thousand a frame, and each is
    // checked against the faces' sizes without asking the device.
    struct Red4Desc { uint32_t w; format fmt; };
    std::unordered_map<uint64_t, Red4Desc> g_r4Desc;
    std::shared_mutex g_r4DescMtx;
    std::vector<std::pair<resource, ULONGLONG>> g_r4Dead;   // copies replaced: destroyed once the GPU is surely done
    std::atomic<uint64_t> g_r4Frames{ 0 }, g_r4Depths{ 0 }, g_r4Pictures{ 0 };
    std::string g_r4RigText;
    ULONGLONG g_r4RigAt = 0;

    bool Red4Depth(format f) { return f == format::r32_g8_typeless || f == format::d32_float_s8_uint; }
    bool Red4Colour(format f)
    {
        return f == format::r8g8b8a8_unorm_srgb || f == format::r8g8b8a8_typeless || f == format::r8g8b8a8_unorm;
    }

    // FROSTBITE: its faces' lit colour, R11G11B10.
    bool FbColour(format f) { return f == format::r11g11b10_float; }

    void Red4Remember(const resource_desc& d, resource r)
    {
        const bool fb = g_cfg.engine == vxcap::kEngineFrostbite;
        if ((g_cfg.engine != vxcap::kEngineRed4 && !fb) || d.type != resource_type::texture_2d || d.texture.width != d.texture.height) return;
        if (!Red4Depth(d.texture.format) && !(fb ? FbColour(d.texture.format) : Red4Colour(d.texture.format))) return;
        std::unique_lock<std::shared_mutex> lk(g_r4DescMtx);
        g_r4Desc[r.handle] = { (uint32_t)d.texture.width, d.texture.format };
    }

    void FbForget(uint64_t h);

    void Red4Forget(uint64_t h)
    {
        FbForget(h);
        if (g_cfg.engine != vxcap::kEngineRed4 && g_cfg.engine != vxcap::kEngineFrostbite) return;
        {
            std::unique_lock<std::shared_mutex> lk(g_r4DescMtx);
            g_r4Desc.erase(h);
        }
        if (g_cfg.engine != vxcap::kEngineRed4) return;
        std::lock_guard<std::mutex> lk(g_r4Mtx);
        for (auto& f : g_r4) if (f.game == h) f.game = 0;
    }

    // The face a texture is (by its size), or -1; and its format.
    int Red4FaceOfTexture(uint64_t h, format& fmt)
    {
        std::shared_lock<std::shared_mutex> lk(g_r4DescMtx);
        const auto it = g_r4Desc.find(h);
        if (it == g_r4Desc.end()) return -1;
        fmt = it->second.fmt;
        return vxcap::Red4FaceOf(g_cfg, it->second.w, it->second.w);
    }

    // A copy of the game's texture, made like it (copy destination and shader resource), or the one there if it matches.
    resource Red4CopyOf(device* dev, resource& have, uint32_t w, format fmt)
    {
        if (have.handle)
        {
            const resource_desc d = dev->get_resource_desc(have);
            if (d.texture.width == w && d.texture.format == fmt) return have;
            g_r4Dead.push_back({ have, GetTickCount64() });
            have = { 0 };
        }
        resource_desc d(w, w, 1, 1, fmt, 1, memory_heap::gpu_only, resource_usage::copy_dest | resource_usage::shader_resource);
        if (!dev->create_resource(d, nullptr, resource_usage::copy_dest, &have))
        {
            Log("redengine4: could not make a %ux%u copy (format %u)", w, w, (unsigned)fmt);
            have = { 0 };
        }
        return have;
    }

    // A face's picture bound for drawing: that texture is the face's picture, and the carry, in the GPU's order, holds
    // this face's depth, complete - copied on into the face's own, on this command list.
    void Red4Bind(command_list* cmd, uint32_t count, const resource_view* rtvs, device* dev)
    {
        if (g_cfg.engine != vxcap::kEngineRed4) return;
        for (uint32_t i = 0; i < count; i++)
        {
            if (!rtvs[i].handle) continue;
            const uint64_t r = dev->get_resource_from_view(rtvs[i]).handle;
            format fmt;
            const int k = Red4FaceOfTexture(r, fmt);
            if (k < 0 || !Red4Colour(fmt)) continue;
            resource carry = { 0 }, copy = { 0 };
            {
                std::lock_guard<std::mutex> lk(g_r4Mtx);
                if (g_r4[k].game != r) { g_r4[k].game = r; g_r4[k].gameState = (uint32_t)resource_usage::render_target; }
                if (g_r4Carry.handle && g_r4Carried)
                {
                    const resource_desc cd = dev->get_resource_desc(g_r4Carry);
                    copy = Red4CopyOf(dev, g_r4[k].depth, cd.texture.width, cd.texture.format);
                    if (copy.handle) { carry = g_r4Carry; ++g_r4[k].depths; }
                }
            }
            if (!copy.handle) continue;
            cmd->barrier(carry, resource_usage::copy_dest, resource_usage::copy_source);
            cmd->copy_texture_region(carry, 0, nullptr, copy, 0, nullptr);
            cmd->barrier(carry, resource_usage::copy_source, resource_usage::copy_dest);
            ++g_r4Depths;
        }
    }

    // The cameras' shared depth buffer, known by its size (faceRes) and format: each time a pass finishes writing it, it
    // goes into the carry - out of the state the game's own barrier has just put it in, and back. A face's picture: the
    // state it is left in.
    void Red4Barrier(command_list* cmd, uint32_t count, const resource* res, const resource_usage* from, const resource_usage* to)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            format fmt;
            uint32_t w = 0;
            {
                std::shared_lock<std::shared_mutex> lk(g_r4DescMtx);
                const auto it = g_r4Desc.find(res[i].handle);
                if (it == g_r4Desc.end()) continue;
                fmt = it->second.fmt; w = it->second.w;
            }
            if (Red4Depth(fmt))
            {
                if ((int)w != g_cfg.faceRes) continue;
                const uint32_t was = (uint32_t)from[i], now = (uint32_t)to[i];
                const uint32_t writes = (uint32_t)(resource_usage::depth_stencil_write | resource_usage::render_target
                                                   | resource_usage::unordered_access | resource_usage::copy_dest);
                if ((was & (uint32_t)resource_usage::depth_stencil_write) == 0 || (now & writes) != 0) continue;
                resource carry;
                {
                    std::lock_guard<std::mutex> lk(g_r4Mtx);
                    carry = Red4CopyOf(cmd->get_device(), g_r4Carry, w, fmt);
                }
                if (!carry.handle) continue;
                cmd->barrier(res[i], to[i], resource_usage::copy_source);
                cmd->copy_texture_region(res[i], 0, nullptr, carry, 0, nullptr);
                cmd->barrier(res[i], resource_usage::copy_source, to[i]);
                g_r4Carried = true;
                continue;
            }
            const int k = vxcap::Red4FaceOf(g_cfg, w, w);
            if (k < 0) continue;
            std::lock_guard<std::mutex> lk(g_r4Mtx);
            if (g_r4[k].game == res[i].handle) g_r4[k].gameState = (uint32_t)to[i];
        }
    }

    // ---- FROSTBITE ----------------------------------------------------------------------------------------------------
    // Each face's scene depth and lit colour, the game's, as first bound together each frame (their sizes say which
    // face), and this add-on's copies of them, which the decode reads. The lit colour is written whole by the lighting,
    // never cleared; another R11G11B10 target of the face's size is cleared to black each frame and is mostly left so -
    // in play it is bound with the face's depth just before the lit one (in the menu, after) - so a target cleared this
    // frame is not taken.
    struct FbFace
    {
        uint64_t gameColour = 0, gameDepth = 0;   // HELD: a reference taken by FbBind, given back when they change
        uint64_t frame = 0;                       // the frame they were bound in
        ID3D11Texture2D* colour = nullptr; ID3D11Texture2D* depth = nullptr;
        ID3D11ShaderResourceView* colourSrv = nullptr; ID3D11ShaderResourceView* depthSrv = nullptr;
    };
    FbFace g_fb[vxcap::kRed4Faces];
    std::unordered_map<uint64_t, uint64_t> g_fbCleared;   // the faces' R11G11B10 targets: the frame each was last cleared in
    std::mutex g_fbMtx;
    vxcap::FbViews g_fbViews;
    bool g_fbInstalled = false, g_fbFailed = false;
    ULONGLONG g_fbArmedAt = 0, g_fbRootAt = 0;
    uint64_t g_fbRootLast = 0;
    int g_fbLinked = 0;
    int g_fbGameViews = -1;                       // the game's own views, as last logged
    std::atomic<uint64_t> g_fbBinds{ 0 }, g_fbPictures{ 0 };
    uint64_t g_fbFrame = 1;                       // counted at present

    // NO D3D CALL UNDER g_fbMtx. Any call can end in ReShade's destroy_resource event on the calling thread - D3D11
    // finishes destroying what the game let go of during the next call it makes, whoever makes it - and its handler
    // (FbForget) takes g_fbMtx: taken again by the thread that holds it, std::mutex throws, the exception leaves through
    // ReShade and the game stops itself (0xC0000409, a fail-fast of FAST_FAIL_FATAL_APP_EXIT; caught twice with a
    // debugger's dump, 2026-10-16: once letting a copy go, once in CopyResource). So the faces' game textures are held - a
    // reference each, taken where they are found - and the present copies them from a list made under the lock, with
    // the lock let go; what is let go of under it waits here.
    std::vector<IUnknown*> g_fbLetGo;               // under g_fbMtx
    template <class T> void FbRelease(T*& p) { if (p) { g_fbLetGo.push_back(p); p = nullptr; } }
    void FbLetGoHeld(uint64_t& h) { if (h) { g_fbLetGo.push_back(reinterpret_cast<IUnknown*>(h)); h = 0; } }
    void FbReleaseLetGo()                           // with g_fbMtx not held
    {
        std::vector<IUnknown*> go;
        {
            std::lock_guard<std::mutex> lk(g_fbMtx);
            go.swap(g_fbLetGo);
        }
        for (IUnknown* p : go) p->Release();
    }

    void FbForget(uint64_t h)
    {
        if (g_cfg.engine != vxcap::kEngineFrostbite) return;
        std::lock_guard<std::mutex> lk(g_fbMtx);
        for (auto& f : g_fb) { if (f.gameColour == h) f.gameColour = 0; if (f.gameDepth == h) f.gameDepth = 0; }
        g_fbCleared.erase(h);
    }

    // The face's size and format a texture has (its creation's, remembered), or -1.
    int FbFaceOfTexture(uint64_t h, format& fmt, uint32_t& w)
    {
        std::shared_lock<std::shared_mutex> lk(g_r4DescMtx);
        const auto it = g_r4Desc.find(h);
        if (it == g_r4Desc.end()) return -1;
        fmt = it->second.fmt; w = it->second.w;
        return vxcap::FbFaceOf(g_cfg, w, w);
    }

    // A face's R11G11B10 target cleared: not its lit colour, this frame.
    bool on_clear_render_target_view(command_list* cmd, resource_view rtv, const float[4], uint32_t, const rect*)
    {
        if (g_cfg.engine != vxcap::kEngineFrostbite || !rtv.handle) return false;
        const uint64_t r = cmd->get_device()->get_resource_from_view(rtv).handle;
        format fmt; uint32_t w;
        if (FbFaceOfTexture(r, fmt, w) < 0 || !FbColour(fmt)) return false;
        std::lock_guard<std::mutex> lk(g_fbMtx);
        g_fbCleared[r] = g_fbFrame;
        return false;
    }

    void FbDrop(FbFace& f)
    {
        FbRelease(f.colourSrv); FbRelease(f.depthSrv); FbRelease(f.colour); FbRelease(f.depth);
    }

    // A face's depth bound with its lit colour: both of the face's size, the colour not cleared this frame, the first
    // such this frame.
    void FbBind(command_list* cmd, uint32_t count, const resource_view* rtvs, resource_view dsv)
    {
        if (g_cfg.engine != vxcap::kEngineFrostbite || !dsv.handle || !count || !rtvs[0].handle) return;
        device* dev = cmd->get_device();
        const uint64_t d = dev->get_resource_from_view(dsv).handle, c = dev->get_resource_from_view(rtvs[0]).handle;
        uint32_t dw = 0, cw = 0;
        format df = format::unknown, cf = format::unknown;
        const int kd = FbFaceOfTexture(d, df, dw);
        if (kd < 0 || FbFaceOfTexture(c, cf, cw) != kd || !Red4Depth(df) || !FbColour(cf)) return;
        std::lock_guard<std::mutex> lk(g_fbMtx);
        FbFace& f = g_fb[kd];
        if (f.frame == g_fbFrame) return;
        if (const auto it = g_fbCleared.find(c); it != g_fbCleared.end() && it->second == g_fbFrame) return;
        ++g_fbBinds;
        // Held: bound for drawing just now, so alive; a reference each keeps them so until the present has copied them.
        if (f.gameColour != c) { FbLetGoHeld(f.gameColour); reinterpret_cast<IUnknown*>(c)->AddRef(); f.gameColour = c; }
        if (f.gameDepth != d) { FbLetGoHeld(f.gameDepth); reinterpret_cast<IUnknown*>(d)->AddRef(); f.gameDepth = d; }
        f.frame = g_fbFrame;
    }

    // This add-on's copy of a game texture (made like it, read as shaderRead), and its view.
    bool FbCopyOf(ID3D11Device* dev, ID3D11Texture2D* game, ID3D11Texture2D*& have, ID3D11ShaderResourceView*& srv, DXGI_FORMAT read)
    {
        D3D11_TEXTURE2D_DESC gd{};
        game->GetDesc(&gd);
        if (have)
        {
            D3D11_TEXTURE2D_DESC hd{};
            have->GetDesc(&hd);
            if (hd.Width == gd.Width && hd.Height == gd.Height && hd.Format == gd.Format) return true;
            srv->Release(); srv = nullptr; have->Release(); have = nullptr;   // g_fbMtx not held: let go at once
        }
        D3D11_TEXTURE2D_DESC d = gd;
        d.MipLevels = 1; d.ArraySize = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.CPUAccessFlags = 0; d.MiscFlags = 0;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &have))) { Log("frostbite: could not make a %ux%u copy (format %u)", gd.Width, gd.Height, (unsigned)gd.Format); return false; }
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = read; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        if (FAILED(dev->CreateShaderResourceView(have, &sd, &srv))) { have->Release(); have = nullptr; Log("frostbite: could not read a copy (format %u)", (unsigned)read); return false; }
        return true;
    }

    // THE SIMS 4: the faces' targets, this add-on's own, made at present once the first face drawn has shown the game's
    // picture - its size and formats - and again when that changes. The job thread may still be drawing into a set that
    // has been replaced, so a replaced set is let go only kTs4Keep frames later.
    struct Ts4Set
    {
        vxcap::Ts4Targets t;                    // the render target and depth views the job thread binds
        ID3D11Texture2D* tex[2 * vxcap::kTs4Faces] = {};
        ID3D11ShaderResourceView* colourSrv[vxcap::kTs4Faces] = {};
        ID3D11ShaderResourceView* depthSrv[vxcap::kTs4Faces] = {};
        uint64_t retiredAt = 0;                 // the present it was replaced at
    };
    constexpr uint64_t kTs4Keep = 8;
    vxcap::Ts4Faces g_ts4;
    bool g_ts4Hooked = false, g_ts4Failed = false;
    Ts4Set* g_ts4Set = nullptr;                 // present thread
    std::vector<Ts4Set*> g_ts4Old;
    uint64_t g_ts4Unmade = 0;                   // a size the targets could not be made at (not tried again): w << 32 | h
    uint64_t g_ts4Pictures = 0;

    void Ts4JobDetour(void* manager)
    {
        g_ts4.Original()(manager);
        g_ts4.DrawFaces(manager);
    }

    void Ts4Free(Ts4Set* s)
    {
        for (int v = 0; v < vxcap::kTs4Faces; v++)
        {
            if (s->t.rtv[v]) reinterpret_cast<IUnknown*>(s->t.rtv[v])->Release();
            if (s->t.dsv[v]) reinterpret_cast<IUnknown*>(s->t.dsv[v])->Release();
            if (s->colourSrv[v]) s->colourSrv[v]->Release();
            if (s->depthSrv[v]) s->depthSrv[v]->Release();
        }
        for (ID3D11Texture2D* t : s->tex) if (t) t->Release();
        delete s;
    }

    // A depth format's texture (typeless, so it can be read too) and its depth-stencil view.
    bool Ts4DepthFormats(format f, DXGI_FORMAT& tex, DXGI_FORMAT& dsv)
    {
        switch ((DXGI_FORMAT)f)
        {
        case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: tex = DXGI_FORMAT_R24G8_TYPELESS; dsv = DXGI_FORMAT_D24_UNORM_S8_UINT; return true;
        case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT: tex = DXGI_FORMAT_R32_TYPELESS; dsv = DXGI_FORMAT_D32_FLOAT; return true;
        case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: tex = DXGI_FORMAT_R32G8X24_TYPELESS; dsv = DXGI_FORMAT_D32_FLOAT_S8X24_UINT; return true;
        case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM: tex = DXGI_FORMAT_R16_TYPELESS; dsv = DXGI_FORMAT_D16_UNORM; return true;
        default: return false;
        }
    }

    DXGI_FORMAT ColourView(format f);
    DXGI_FORMAT DepthView(format f);

    // A face's colour and depth, w x h like the game's picture, in its formats.
    Ts4Set* Ts4Make(ID3D11Device* dev, uint32_t w, uint32_t h, format colour, format depth)
    {
        DXGI_FORMAT dt, dv;
        if (!Ts4DepthFormats(depth, dt, dv)) { Log("sims4: the game's depth format %u is not one this add-on reads", (unsigned)depth); return nullptr; }
        Ts4Set* s = new Ts4Set;
        s->t.w = w; s->t.h = h;
        for (int v = 0; v < vxcap::kTs4Faces; v++)
        {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = w; d.Height = h; d.MipLevels = d.ArraySize = 1; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
            d.Format = ColourView(colour); d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            ID3D11Texture2D*& c = s->tex[2 * v];
            ID3D11Texture2D*& z = s->tex[2 * v + 1];
            ID3D11RenderTargetView* rtv = nullptr;
            ID3D11DepthStencilView* dsv = nullptr;
            bool ok = SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &c)) && SUCCEEDED(dev->CreateRenderTargetView(c, nullptr, &rtv))
                      && SUCCEEDED(dev->CreateShaderResourceView(c, nullptr, &s->colourSrv[v]));
            s->t.rtv[v] = (uint64_t)rtv;
            d.Format = dt; d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
            dd.Format = dv; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = DepthView(depth); sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
            ok = ok && SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &z)) && SUCCEEDED(dev->CreateDepthStencilView(z, &dd, &dsv))
                 && SUCCEEDED(dev->CreateShaderResourceView(z, &sd, &s->depthSrv[v]));
            s->t.dsv[v] = (uint64_t)dsv;
            if (!ok)
            {
                Log("sims4: could not make face %d's %ux%u targets (colour %u, depth %u)", v, w, h, (unsigned)colour, (unsigned)depth);
                Ts4Free(s);
                return nullptr;
            }
        }
        return s;
    }

    // THE SIMS 4: a face's scene binds targets. With a depth buffer, the scene's own: turned to the face's (cleared the
    // first time); the first time, the game's picture is learnt from them, for the targets the present makes. Without
    // one, a pass of its own (a copy of the picture so far): left alone.
    void Ts4Bind(command_list* cmd, uint32_t count, const resource_view* rtvs, resource_view dsv)
    {
        const int v = vxcap::Ts4Faces::Face();
        if (v < 0 || !dsv.handle || !count || !rtvs[0].handle) return;
        const bool first = vxcap::Ts4Faces::FirstBind();
        if (first)
        {
            device* dev = cmd->get_device();
            const resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(dsv));
            const resource_desc c = dev->get_resource_desc(dev->get_resource_from_view(rtvs[0]));
            g_ts4.Learn(d.texture.width, d.texture.height, (uint32_t)c.texture.format, (uint32_t)d.texture.format);
        }
        const vxcap::Ts4Targets* t = vxcap::Ts4Faces::FaceTargets();
        if (!t || !t->rtv[v] || !t->dsv[v]) return;
        const resource_view rtv{ t->rtv[v] }, dv{ t->dsv[v] };
        if (first)
        {
            const float black[4] = {}, far1 = 1.0f;   // the depth runs forward: 1 is the far plane
            const uint8_t zero = 0;
            cmd->clear_render_target_view(rtv, black);
            cmd->clear_depth_stencil_view(dv, &far1, &zero);
        }
        cmd->bind_render_targets_and_depth_stencil(1, &rtv, dv);
        g_ts4.Redirected();
    }

    // CRYSIS (engine=cryengine2): the faces drawn by the game's own 3D engine from the add-on's cameras, in its
    // RenderWorld (cryengine2_views.hpp), each face's picture and depth copied out the moment it is drawn: the middle
    // square of the back buffer and of the engine's linear depth target. The game is D3D10, which has no compute shaders,
    // so the decode runs on a D3D11 device of the add-on's own on the same GPU, and the copies go into textures the two
    // devices share, each with a keyed mutex (key 0: D3D10's to write; 1: D3D11's to read). All of it runs on the game's
    // main thread, which the game's device - made single-threaded - requires: the copies in RenderWorld, the decode at
    // present.
    struct Ce2Set
    {
        uint32_t w = 0, h = 0, side = 0;        // the game's picture, and the faces' squares (its height)
        ID3D11Texture2D* tex11[2 * vxcap::kCe2Faces] = {};   // per face: colour, depth
        IDXGIKeyedMutex* km11[2 * vxcap::kCe2Faces] = {};
        ID3D11ShaderResourceView* colourSrv[vxcap::kCe2Faces] = {};
        ID3D11ShaderResourceView* depthSrv[vxcap::kCe2Faces] = {};
        ID3D10Texture2D* tex10[2 * vxcap::kCe2Faces] = {};
        IDXGIKeyedMutex* km10[2 * vxcap::kCe2Faces] = {};
        ID3D10Texture2D* resolved = nullptr;    // a multisampled picture, resolved before its square is copied
        bool written[vxcap::kCe2Faces] = {};    // copied, not yet decoded: the D3D11 side has key 1 to take
    };
    vxcap::Ce2Faces g_ce2;
    bool g_ce2Hooked = false, g_ce2Failed = false;
    ID3D10Device* g_ce2Dev10 = nullptr;         // the game's
    ID3D11Device* g_ce2Dev11 = nullptr;         // the add-on's own, for the decode
    ID3D11DeviceContext* g_ce2Ctx11 = nullptr;
    IDXGISwapChain* g_ce2Sc = nullptr;
    Ce2Set* g_ce2Set = nullptr;
    ID3D10Texture2D* g_ce2Z = nullptr;           // the engine's linear depth target, as a face being drawn binds it
    bool g_ce2InFaces = false;                  // the faces are being drawn: their binds are watched for it
    uint64_t g_ce2Copied = 0, g_ce2NoDepth = 0, g_ce2Busy = 0, g_ce2Pictures = 0;

    DXGI_FORMAT ColourView(format f);

    void Ce2Free(Ce2Set* s)
    {
        if (!s) return;
        for (int i = 0; i < 2 * vxcap::kCe2Faces; i++)
        {
            if (s->km10[i]) s->km10[i]->Release();
            if (s->tex10[i]) s->tex10[i]->Release();
            if (s->km11[i]) s->km11[i]->Release();
            if (s->tex11[i]) s->tex11[i]->Release();
        }
        for (auto* v : s->colourSrv) if (v) v->Release();
        for (auto* v : s->depthSrv) if (v) v->Release();
        if (s->resolved) s->resolved->Release();
        delete s;
    }

    // A texture made on the add-on's D3D11 device, shared with the game's D3D10 one; its keyed mutexes on both sides.
    bool Ce2Shared(uint32_t side, DXGI_FORMAT f, ID3D11Texture2D*& t11, IDXGIKeyedMutex*& k11, ID3D10Texture2D*& t10,
                   IDXGIKeyedMutex*& k10)
    {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = d.Height = side; d.MipLevels = d.ArraySize = 1; d.Format = f; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        d.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        if (FAILED(g_ce2Dev11->CreateTexture2D(&d, nullptr, &t11))) return false;
        IDXGIResource* r = nullptr;
        HANDLE shared = nullptr;
        const bool ok = SUCCEEDED(t11->QueryInterface(__uuidof(IDXGIResource), (void**)&r)) && SUCCEEDED(r->GetSharedHandle(&shared))
                        && SUCCEEDED(g_ce2Dev10->OpenSharedResource(shared, __uuidof(ID3D10Texture2D), (void**)&t10))
                        && SUCCEEDED(t11->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&k11))
                        && SUCCEEDED(t10->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&k10));
        if (r) r->Release();
        return ok;
    }

    // The faces' textures for a picture of w x h in this format (the back buffer's): squares as tall as it is.
    Ce2Set* Ce2Make(uint32_t w, uint32_t h, DXGI_FORMAT colour, bool multisampled)
    {
        Ce2Set* s = new Ce2Set;
        s->w = w; s->h = h; s->side = vxcap::Ce2Square(w, h);
        for (int v = 0; v < vxcap::kCe2Faces; v++)
        {
            bool ok = Ce2Shared(s->side, colour, s->tex11[2 * v], s->km11[2 * v], s->tex10[2 * v], s->km10[2 * v])
                      && Ce2Shared(s->side, DXGI_FORMAT_R32_FLOAT, s->tex11[2 * v + 1], s->km11[2 * v + 1], s->tex10[2 * v + 1], s->km10[2 * v + 1]);
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = ColourView((format)colour); sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
            ok = ok && SUCCEEDED(g_ce2Dev11->CreateShaderResourceView(s->tex11[2 * v], &sd, &s->colourSrv[v]))
                    && SUCCEEDED(g_ce2Dev11->CreateShaderResourceView(s->tex11[2 * v + 1], nullptr, &s->depthSrv[v]));
            if (!ok) { Log("cryengine2: could not make face %d's shared %ux%u textures (colour %u)", v, s->side, s->side, (unsigned)colour); Ce2Free(s); return nullptr; }
        }
        if (multisampled)
        {
            D3D10_TEXTURE2D_DESC d{};
            d.Width = w; d.Height = h; d.MipLevels = d.ArraySize = 1; d.Format = colour; d.SampleDesc.Count = 1;
            d.Usage = D3D10_USAGE_DEFAULT; d.BindFlags = D3D10_BIND_SHADER_RESOURCE;
            if (FAILED(g_ce2Dev10->CreateTexture2D(&d, nullptr, &s->resolved))) { Log("cryengine2: could not make the resolve target"); Ce2Free(s); return nullptr; }
        }
        return s;
    }

    // DIAG (temporary): the back buffer to a BMP beside the exe.
    bool g_ce2Dumping = false;
    int g_ce2DumpSeen = 0;
    void Ce2DumpBb(const char* what)
    {
        ID3D10Texture2D* bb = nullptr;
        if (FAILED(g_ce2Sc->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)&bb))) return;
        D3D10_TEXTURE2D_DESC d; bb->GetDesc(&d);
        D3D10_TEXTURE2D_DESC s = d; s.Usage = D3D10_USAGE_STAGING; s.BindFlags = 0; s.CPUAccessFlags = D3D10_CPU_ACCESS_READ; s.MiscFlags = 0;
        ID3D10Texture2D* st = nullptr;
        if (SUCCEEDED(g_ce2Dev10->CreateTexture2D(&s, nullptr, &st)))
        {
            g_ce2Dev10->CopyResource(st, bb);
            D3D10_MAPPED_TEXTURE2D m;
            if (SUCCEEDED(st->Map(0, D3D10_MAP_READ, 0, &m)))
            {
                static int k = 0;
                char path[MAX_PATH]; sprintf_s(path, "%s\\ce2dump_%02d_%s.bmp", g_dir, k++, what);
                FILE* f = nullptr; fopen_s(&f, path, "wb");
                if (f)
                {
                    const int w = d.Width, h = d.Height, row = w * 3, pad = (4 - row % 4) % 4;
                    BITMAPFILEHEADER fh{ 0x4D42, (DWORD)(54 + (row + pad) * h), 0, 0, 54 };
                    BITMAPINFOHEADER ih{ 40, w, h, 1, 24 };
                    fwrite(&fh, sizeof fh, 1, f); fwrite(&ih, sizeof ih, 1, f);
                    for (int y = h - 1; y >= 0; y--)
                    {
                        const uint8_t* r = (const uint8_t*)m.pData + (size_t)y * m.RowPitch;
                        for (int x = 0; x < w; x++) { const uint8_t px[3] = { r[x * 4 + 2], r[x * 4 + 1], r[x * 4] }; fwrite(px, 3, 1, f); }
                        const uint8_t z[3] = {}; fwrite(z, 1, pad, f);
                    }
                    fclose(f);
                    Log("DIAG dump %s -> %s", what, path);
                }
                st->Unmap(0);
            }
            st->Release();
        }
        bb->Release();
    }

    // Main thread, the moment face v is drawn: its square of the back buffer and of the depth target into its textures.
    void Ce2Copy(int v)
    {
        if (g_ce2Dumping) { char w[32]; sprintf_s(w, "face%d", v); Ce2DumpBb(w); }
        ID3D10Texture2D* z = g_ce2Z;
        g_ce2Z = nullptr;                       // the next face's binds name it again
        Ce2Set* s = g_ce2Set;
        if (!s || !g_ce2Sc) return;
        if (!z) { ++g_ce2NoDepth; return; }
        IDXGIKeyedMutex* kc = s->km10[2 * v];
        IDXGIKeyedMutex* kz = s->km10[2 * v + 1];
        if (kc->AcquireSync(0, 0) != S_OK) { ++g_ce2Busy; return; }
        if (kz->AcquireSync(0, 0) != S_OK) { kc->ReleaseSync(0); ++g_ce2Busy; return; }
        ID3D10Texture2D* bb = nullptr;
        if (SUCCEEDED(g_ce2Sc->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)&bb)))
        {
            D3D10_BOX box{ (s->w - s->side) / 2, (s->h - s->side) / 2, 0, 0, 0, 1 };
            box.right = box.left + s->side; box.bottom = box.top + s->side;
            ID3D10Texture2D* src = bb;
            if (s->resolved)
            {
                D3D10_TEXTURE2D_DESC d; bb->GetDesc(&d);
                g_ce2Dev10->ResolveSubresource(s->resolved, 0, bb, 0, d.Format);
                src = s->resolved;
            }
            g_ce2Dev10->CopySubresourceRegion(s->tex10[2 * v], 0, 0, 0, 0, src, 0, &box);
            g_ce2Dev10->CopySubresourceRegion(s->tex10[2 * v + 1], 0, 0, 0, 0, z, 0, &box);
            bb->Release();
            s->written[v] = true;
            ++g_ce2Copied;
            kz->ReleaseSync(1); kc->ReleaseSync(1);
            return;
        }
        kz->ReleaseSync(0); kc->ReleaseSync(0);
    }

    // The game's RenderWorld, taken over: before its own view of the frame, the faces.
    void __fastcall Ce2RenderWorldDetour(void* engine, int flags, const void* camera, const char* name, int drawFlags, int filterFlags)
    {
        const vxcap::Ce2RenderWorldFn original = g_ce2.Original();
        if (g_ce2Set && vxcap::Ce2Faces::IsMainView(name))
        {
            g_ce2InFaces = true; g_ce2Z = nullptr;
            const uint64_t before = g_ce2.Drawn();
            g_ce2Dumping = g_cfg.ce2Dump != g_ce2DumpSeen;
            if (g_ce2Dumping) Ce2DumpBb("before");
            g_ce2.DrawFaces(engine, camera, g_ce2Set->w, g_ce2Set->h, [](int v) { Ce2Copy(v); });
            g_ce2InFaces = false; g_ce2Z = nullptr;
            original(engine, flags, camera, name, drawFlags, filterFlags);
            if (g_ce2Dumping && g_ce2.Drawn() != before) { Ce2DumpBb("main"); g_ce2DumpSeen = g_cfg.ce2Dump; }
            g_ce2Dumping = false;
            return;
        }
        original(engine, flags, camera, name, drawFlags, filterFlags);
    }

    // A face being drawn binds the engine's linear depth target: the game's size, one float a texel.
    void Ce2Bind(command_list* cmd, uint32_t count, const resource_view* rtvs)
    {
        if (!g_ce2InFaces || g_ce2Z || !g_ce2Set) return;
        device* dev = cmd->get_device();
        for (uint32_t i = 0; i < count; i++)
        {
            if (!rtvs[i].handle) continue;
            const resource r = dev->get_resource_from_view(rtvs[i]);
            const resource_desc d = dev->get_resource_desc(r);
            if (d.texture.format == format::r32_float && d.texture.width == g_ce2Set->w && d.texture.height == g_ce2Set->h)
            {
                g_ce2Z = reinterpret_cast<ID3D10Texture2D*>(r.handle);
                return;
            }
        }
    }

    // The add-on's own device and everything made on it let go, and the game's RenderWorld given back: the game's device
    // is going (or another has come).
    void Ce2Release()
    {
        g_ce2.Uninstall();
        g_ce2Hooked = false;
        g_ce2Failed = false;
        if (g_ce2Dev11) { g_dec.Release(); g_decOk = false; }
        Ce2Free(g_ce2Set);
        g_ce2Set = nullptr;
        if (g_ce2Ctx11) { g_ce2Ctx11->Release(); g_ce2Ctx11 = nullptr; }
        if (g_ce2Dev11) { g_ce2Dev11->Release(); g_ce2Dev11 = nullptr; }
        g_ce2Dev10 = nullptr; g_ce2Sc = nullptr; g_ce2Z = nullptr;
    }

    void MarkWritten(uint64_t res);

    void on_init_resource(device* dev, const resource_desc& d, const subresource_data*, resource_usage, resource r)
    {
        if ((dev->get_api() != device_api::d3d11 && dev->get_api() != device_api::d3d12) || d.type != resource_type::texture_2d)
            return;
        Red4Remember(d, r);
        if (d.texture.format != format::r16g16b16a16_float && d.texture.format != format::r16g16b16a16_typeless) return;
        if ((d.usage & resource_usage::render_target) == 0 || (d.usage & resource_usage::shader_resource) == 0) return;
        // A render target texture (UTextureRenderTarget2D, bCanCreateUAV off) is never a compute target; much of the
        // engine's own scene texturing is, so this keeps most of it out of the candidates.
        if ((d.usage & resource_usage::unordered_access) != 0) return;
        // Every plausible face is REMEMBERED, not only the configured size: rig.lua can build the cameras for a new
        // resolution before this add-on has read it. Only the configured size is PROBED (the handshakes): probing every
        // such target hung the GPU on D3D12 while the game loaded (2026-10-11, DXGI_ERROR_DEVICE_HUNG within 7 s).
        const uint32_t w = d.texture.width, h = d.texture.height;
        if (w < 64 || w > 4096 || (h != w && h != w / 2) || d.texture.depth_or_layers != 1 || d.texture.samples > 1) return;
        reinterpret_cast<IUnknown*>(r.handle)->AddRef();
        std::lock_guard<std::mutex> lk(g_candMtx);
        g_newCands.push_back({ r.handle, w, GetTickCount64() });
    }

    void on_destroy_resource(device*, resource r)
    {
        Red4Forget(r.handle);
        std::lock_guard<std::mutex> lk(g_candMtx);
        for (const auto& n : g_newCands) if (n.handle == r.handle) { g_deadRes.push_back(r.handle); return; }
        for (auto& c : g_cands) if (c.handle == r.handle) { g_deadRes.push_back(r.handle); return; }
        for (auto& f : g_face) if (f.res == r.handle || f.surfaceRes == r.handle || f.playerRes == r.handle || f.solidRes == r.handle || f.glowRes == r.handle) { g_deadRes.push_back(r.handle); return; }
    }

    // A target bound for drawing has just been written: Unreal draws each capture's result into it. UNITY: the depth
    // buffer bound with it is that face's depth, which its decode reads (rig/unity renders depth into its own buffer).
    void on_bind_render_targets(command_list* cmd, uint32_t count, const resource_view* rtvs, resource_view dsv)
    {
        Red3Bind(cmd, count, rtvs, dsv);
        Red4Bind(cmd, count, rtvs, cmd->get_device());   // before anything else: the copy goes ahead of the face's draw
        FbBind(cmd, count, rtvs, dsv);
        if (g_cfg.engine == vxcap::kEngineSims4) Ts4Bind(cmd, count, rtvs, dsv);   // a face being drawn: its own targets
        if (g_cfg.engine == vxcap::kEngineCryEngine2) Ce2Bind(cmd, count, rtvs);  // a face being drawn: its depth target
        // REDENGINE3: the depth buffers drawn into at the back buffer's size, counted: the scene's is the one bound most.
        if (g_cfg.engine == vxcap::kEngineRed3 && dsv.handle && g_r3W)
        {
            device* dev = cmd->get_device();
            const resource r = dev->get_resource_from_view(dsv);
            std::lock_guard<std::mutex> lk(g_r3DepthMtx);
            auto it = g_r3DepthBinds.find(r.handle);
            if (it != g_r3DepthBinds.end()) ++it->second;
            else
            {
                const resource_desc d = dev->get_resource_desc(r);
                if (d.texture.width == g_r3W && d.texture.height == g_r3H)
                {
                    g_r3DepthBinds[r.handle] = 1;
                    if (!g_r3DepthStates.count(r.handle)) g_r3DepthStates[r.handle] = (uint32_t)resource_usage::depth_stencil_write;
                }
            }
        }
        t_drawFace = -1;
        if (!g_anyFace || count == 0) return;
        device* dev = cmd->get_device();
        if (rtvs[0].handle)
        {
            const uint64_t first = dev->get_resource_from_view(rtvs[0]).handle;
            for (int v = 0; v < 6; v++) if (g_face[v].res == first) t_drawFace = v;
        }
        const uint64_t depth = (g_cfg.engine == vxcap::kEngineUnity && dsv.handle) ? dev->get_resource_from_view(dsv).handle : 0;
        for (uint32_t i = 0; i < count; i++)
        {
            if (rtvs[i].handle == 0) continue;
            const uint64_t res = dev->get_resource_from_view(rtvs[i]).handle;
            MarkWritten(res);
            if (!depth) continue;
            for (auto& f : g_face)
            {
                // The face's target, PLAYER LOOK's or SOLID SURFACES': each keeps the depth buffer drawn with it.
                std::atomic<uint64_t>* keep = f.res == res ? &f.depthRes : f.playerRes == res ? &f.playerDepthRes
                                            : f.solidRes == res ? &f.solidDepthRes : nullptr;
                if (!keep || *keep == depth) continue;
                reinterpret_cast<IUnknown*>(depth)->AddRef();
                Unhold(keep->exchange(depth));
            }
        }
    }

    // The same through a D3D12 render pass (ID3D12GraphicsCommandList4::BeginRenderPass), which binds no targets.
    void on_begin_render_pass(command_list* cmd, uint32_t count, const render_pass_render_target_desc* rts,
                              const render_pass_depth_stencil_desc*)
    {
        if (!g_anyFace || count == 0) return;
        device* dev = cmd->get_device();
        for (uint32_t i = 0; i < count; i++)
        {
            if (rts[i].view.handle == 0) continue;
            MarkWritten(dev->get_resource_from_view(rts[i].view).handle);
        }
    }

    // A target that is the DESTINATION of a copy or resolve has been written too: Unreal may hand a capture's result
    // over that way rather than by drawing into it.
    void MarkWritten(uint64_t res)
    {
        if (!g_anyFace || !res) return;
        for (int v = 0; v < 6; v++) if (g_face[v].res == res) { g_face[v].dirty = true; ++g_writes[v]; }
    }
    bool on_copy_resource(command_list*, resource, resource dest) { MarkWritten(dest.handle); return false; }
    bool on_copy_texture_region(command_list*, resource, uint32_t, const subresource_box*, resource dest, uint32_t,
                                const subresource_box*, filter_mode) { MarkWritten(dest.handle); return false; }
    bool on_resolve_texture_region(command_list*, resource, uint32_t, const subresource_box*, resource dest, uint32_t,
                                   uint32_t, uint32_t, uint32_t, format) { MarkWritten(dest.handle); return false; }

    // REDENGINE3, when the game's swapchain is made - its own device, after Streamline's probing, before the first
    // frame: a dead hook left on the renderer's render-frame put right (red3_native.hpp), then the native hook, if on.
    void on_init_swapchain(swapchain* sc, bool resize)
    {
        g_r3Sc = sc;
        if (g_cfg.engine != vxcap::kEngineRed3 || resize) return;
        if (vxcap::Red3HealRenderFrame())
            Log("redengine3: a dead hook on the renderer's render-frame put right (Streamline's frame generation, unloaded)");
        if (g_cfg.red3Native && !g_r3NativeOn)
        {
            std::string err;
            g_r3NativeOn = g_r3Native.Install((void*)&Red3RenderFrameDetour, err);
            if (g_r3NativeOn) Log("redengine3 native: the renderer's render-frame hooked%s", g_r3Native.Chained() ? " (after another live hook)" : "");
            else Log("redengine3 native: NOT hooked: %s", err.c_str());
        }
    }

    void on_destroy_device(device* dev)
    {
        const uint64_t native = dev->get_native();
        if (g_ce2Dev10 && native == (uint64_t)g_ce2Dev10) { Ce2Release(); return; }   // CRYSIS: the game's D3D10 device
        if (native != (uint64_t)g_dev && native != (uint64_t)g_dev12) return;
        g_dec.Release();      // first: in-flight GPU work is waited for before anything it reads is let go
        g_dec12.Release();
        if (g_r3Player.handle) { dev->destroy_resource(g_r3Player); g_r3Player = { 0 }; }
        for (auto& f : g_r3)   // REDENGINE3: this add-on's copies
        {
            if (f.colour.handle) dev->destroy_resource(f.colour);
            if (f.depth.handle) dev->destroy_resource(f.depth);
            f = Red3Face{};
        }
        {
            std::vector<resource> go;                  // REDENGINE4: this add-on's copies, destroyed with g_r4Mtx let go
            {
                std::lock_guard<std::mutex> lk(g_r4Mtx);
                for (auto& f : g_r4)
                {
                    if (f.colour.handle) go.push_back(f.colour);
                    if (f.depth.handle) go.push_back(f.depth);
                    f = Red4Face{};
                }
                for (auto& d : g_r4Dead) go.push_back(d.first);
                g_r4Dead.clear();
                if (g_r4Carry.handle) go.push_back(g_r4Carry);
                g_r4Carry = { 0 };
                g_r4Carried = false;
            }
            for (const resource& r : go) dev->destroy_resource(r);
        }
        {
            std::lock_guard<std::mutex> lk(g_fbMtx);   // FROSTBITE: this add-on's copies, and the game textures held
            for (auto& f : g_fb) { FbDrop(f); FbLetGoHeld(f.gameColour); FbLetGoHeld(f.gameDepth); f = FbFace{}; }
        }
        FbReleaseLetGo();
        g_ts4.SetTargets(nullptr);                     // THE SIMS 4: the faces' targets
        for (Ts4Set* o : g_ts4Old) Ts4Free(o);
        g_ts4Old.clear();
        if (g_ts4Set) { Ts4Free(g_ts4Set); g_ts4Set = nullptr; }
        g_ts4Unmade = 0;
        for (auto& f : g_face) DropFace(f);
        std::vector<uint64_t> letGo;
        for (auto& c : g_cands) { if (c.probe) c.probe->Release(); letGo.push_back(c.handle); }
        g_cands.clear();
        {
            std::lock_guard<std::mutex> lk(g_candMtx);
            for (const auto& n : g_newCands) letGo.push_back(n.handle);
            g_newCands.clear();
        }
        for (uint64_t h : letGo) Unhold(h);
        g_dev = nullptr; g_dev12 = nullptr; g_decOk = false; g_anyFace = false;
    }

    // A finished volume: the plugin's auto-exposure, then hand it to the worker. The same for both APIs.
    void Deliver(int n, float lum)
    {
        ++g_volumes;
        g_lastCount = (uint64_t)n;
        // Ease toward the exposure that puts the metered luminance on middle grey, in log space, at
        // AutoExposureSpeed 3 - never past the app's lowest and highest (stops).
        const ULONGLONG now = GetTickCount64();
        const float dt = g_expAt ? std::min(0.5f, (now - g_expAt) / 1000.0f) : 0.033f;
        g_expAt = now;
        if (lum > 0)
        {
            const float lo = std::min(g_cfg.exposureMinEV, g_cfg.exposureMaxEV), hi = std::max(g_cfg.exposureMinEV, g_cfg.exposureMaxEV);
            const float target = std::clamp(0.18f / lum, std::exp2(lo), std::exp2(hi));
            const float a = 1.0f - std::exp(-3.0f * dt);
            const float cur = std::max(g_autoExp, 1e-9f);
            g_autoExp = std::pow(2.0f, std::log2(cur) + (std::log2(target) - std::log2(cur)) * a);
        }
        std::lock_guard<std::mutex> lk(g_wMtx);
        if (g_wHas) ++g_dropWorker;      // the worker has not taken the last one: replace it
        g_wBuf.swap(g_read);
        g_wCfg = g_cfg; g_wGeo = g_geo;
        g_wHas = true;
        g_wCv.notify_one();
    }

    // Decode when ANY enabled face is fresh, from every face's latest capture. Waiting for all of them held the
    // rate to about a third of the captures (measured 2026-10-10: ~10/s decoded against 30/s captured).
    //
    // Unless no write to any face has been seen 3 s after the handshake: an engine that fills its capture targets
    // from a compute pass binds nothing this add-on can see. Then the decode runs at the rig's own rate. Unseen in
    // UE 4.26 (The Ascent draws every face), written for the UE5 titles nobody has run this on yet.
    bool FacesReady()
    {
        bool any = false, fresh = false;
        uint64_t writes = 0;
        for (int v = 0; v < 6; v++)
        {
            if (!g_cfg.faceOn[v]) continue;
            any = true;
            if (!g_face[v].res || g_face[v].w != g_cfg.faceRes || g_face[v].h != vxcap::FaceHeight(v, g_cfg)) return false;
            if (g_cfg.engine == vxcap::kEngineUnity && !g_face[v].depthSrv) return false;   // UNITY: and its depth
            if (vxcap::UseSurface(g_cfg) && (!g_face[v].surfaceRes || g_face[v].surfaceW != g_cfg.faceRes
                                             || g_face[v].surfaceH != vxcap::FaceHeight(v, g_cfg))) return false;   // and its colours
            if (vxcap::UsePlayer(g_cfg) && (!g_face[v].playerRes || g_face[v].playerW != g_cfg.faceRes
                                            || g_face[v].playerH != vxcap::FaceHeight(v, g_cfg))) return false;    // and its player
            if (vxcap::UsePlayer(g_cfg) && g_cfg.engine == vxcap::kEngineUnity && !g_face[v].playerDepthSrv) return false;
            if (vxcap::UseSolid(g_cfg) && (!g_face[v].solidRes || g_face[v].solidW != g_cfg.faceRes
                                           || g_face[v].solidH != vxcap::FaceHeight(v, g_cfg))) return false;     // and its solid depth
            if (vxcap::UseSolid(g_cfg) && g_cfg.engine == vxcap::kEngineUnity && !g_face[v].solidDepthSrv) return false;
            if (vxcap::UseGlow(g_cfg) && (!g_face[v].glowRes || g_face[v].glowW != g_cfg.faceRes
                                          || g_face[v].glowH != vxcap::FaceHeight(v, g_cfg))) return false;      // and their glow
            fresh = fresh || g_face[v].dirty;
            writes += g_writes[v];
        }
        if (!any) return false;
        if (fresh) return true;
        const ULONGLONG now = GetTickCount64();
        if (writes != 0 || now - g_foundAt < 3000) return false;
        if (!g_timedLogged)
        {
            g_timedLogged = true;
            Log("no write to a face target seen in 3 s: decoding on a timer at %d Hz instead", g_cfg.hz);
        }
        if (now - g_timedAt < (ULONGLONG)(1000 / std::max(1, g_cfg.hz))) return false;
        g_timedAt = now;
        return true;
    }

    // REDENGINE3: the frame's camera, from the first constant buffer bound this frame that holds the game's camera block
    // and is drawn at the back buffer's shape (a shadow's or a reflection's camera is not).
    void on_push_descriptors(command_list* cmd, shader_stage, pipeline_layout, uint32_t, const descriptor_table_update& u)
    {
        if (g_cfg.engine != vxcap::kEngineRed3 || u.type != descriptor_type::constant_buffer) return;
        {
            std::lock_guard<std::mutex> lk(g_r3Mtx);
            if (g_r3Found || g_r3Checked > 256) return;
        }
        device* dev = cmd->get_device();
        const buffer_range* r = static_cast<const buffer_range*>(u.descriptors);
        for (uint32_t i = 0; i < u.count; i++)
        {
            if (r[i].buffer.handle == 0) continue;
            const resource_desc d = dev->get_resource_desc(r[i].buffer);
            if (d.type != resource_type::buffer || d.heap == memory_heap::gpu_only) continue;
            const uint64_t size = r[i].size == UINT64_MAX ? d.buffer.size - r[i].offset : r[i].size;
            if (size < vxcap::kRed3CameraBytes) continue;
            float f[vxcap::kRed3CameraBytes / 4];
            void* p = nullptr;
            if (!dev->map_buffer_region(r[i].buffer, r[i].offset, vxcap::kRed3CameraBytes, map_access::read_only, &p) || !p) continue;
            memcpy(f, p, sizeof f);
            dev->unmap_buffer_region(r[i].buffer);
            vxcap::Red3Camera cam;
            const bool ok = vxcap::Red3ReadCamera(f, sizeof f / 4, cam)
                            && (!g_r3W || std::fabs(cam.sy / cam.sx - (float)g_r3W / g_r3H) < 0.02f * g_r3W / g_r3H);
            std::lock_guard<std::mutex> lk(g_r3Mtx);
            ++g_r3Checked;
            if (ok && !g_r3Found) { g_r3Cam = cam; g_r3Found = true; }
        }
    }

    resource Red3Copy(device* dev, const resource_desc& like)
    {
        resource_desc d(like.texture.width, like.texture.height, 1, 1, like.texture.format, 1, memory_heap::gpu_only,
                        resource_usage::copy_dest | resource_usage::shader_resource);
        resource r = { 0 };
        if (!dev->create_resource(d, nullptr, resource_usage::copy_dest, &r)) Log("redengine3: could not make a %ux%u copy (format %u)",
            like.texture.width, like.texture.height, (unsigned)like.texture.format);
        return r;
    }

    // REDENGINE3: the state the game leaves its scene depth in, from its own barriers - the face frame's depth is copied
    // at present, out of whatever state the frame ended in, and handed back in it.
    void on_barrier(command_list* cmd, uint32_t count, const resource* res, const resource_usage* from, const resource_usage* to)
    {
        if (g_cfg.engine == vxcap::kEngineRed4) { Red4Barrier(cmd, count, res, from, to); return; }
        if (g_cfg.engine != vxcap::kEngineRed3) return;
        std::lock_guard<std::mutex> lk(g_r3DepthMtx);
        if (g_r3DepthStates.empty()) return;
        for (uint32_t i = 0; i < count; i++)
        {
            auto it = g_r3DepthStates.find(res[i].handle);
            if (it != g_r3DepthStates.end()) it->second = (uint32_t)to[i];
        }
    }

    // A copy's format, as a shader reads it: the colour's typed view, the depth's depth plane.
    DXGI_FORMAT ColourView(format f)
    {
        switch ((DXGI_FORMAT)f)
        {
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return (DXGI_FORMAT)f;
        }
    }
    DXGI_FORMAT DepthView(format f)
    {
        switch ((DXGI_FORMAT)f)
        {
        case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    }

    // The app is there to take volumes when its pipe is: connected already, or waiting (busy counts; no such pipe does
    // not). Asked once a second, as the worker only connects once there is a volume to send.
    bool AppThere()
    {
        static ULONGLONG appAt = 0;
        static bool appThere = false;
        if (GetTickCount64() - appAt >= 1000)
        {
            appAt = GetTickCount64();
            char pipePath[96];
            sprintf_s(pipePath, "\\\\.\\pipe\\%s", g_cfg.pipe);
            appThere = g_pipeUp || WaitNamedPipeA(pipePath, 1) || GetLastError() == ERROR_SEM_TIMEOUT;
        }
        return appThere;
    }

    // F7, while the game is in front, stops and starts the rig (the Unreal and Unity rigs bind it themselves). startOff:
    // the rig waits for the first F7 (FROSTBITE: the faces are put in only once the player is in play).
    bool UserOff(swapchain* sc, const char* what, bool startOff = false)
    {
        static bool f7Down = false, userOff = startOff;
        const bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        if (f7 && !f7Down && GetForegroundWindow() == (HWND)sc->get_hwnd())
        {
            userOff = !userOff;
            Log("%s %s", what, userOff ? "stopped" : "started");
        }
        f7Down = f7;
        return userOff;
    }

    // REDENGINE3 at present: the settings to the script, this frame's picture if it was a face's, and a decode when a
    // face has completed.
    void Present12Red3(ID3D12Device* dev12, command_queue* queue, swapchain* sc)
    {
        device* dev = queue->get_device();
        const resource bb = sc->get_current_back_buffer();
        const resource_desc bd = dev->get_resource_desc(bb);
        g_r3W = bd.texture.width; g_r3H = bd.texture.height; g_r3ColourFmt = bd.texture.format;
        ++g_r3Frames;

        vxcap::Red3Camera cam;
        bool found;
        {
            std::lock_guard<std::mutex> lk(g_r3Mtx);
            cam = g_r3Cam; found = g_r3Found;
            g_r3Found = false; g_r3Checked = 0;
        }
        if (!found) ++g_r3NoCamera;
        uint32_t depthState = 0;
        {
            std::lock_guard<std::mutex> lk(g_r3DepthMtx);
            int most = 0;
            for (const auto& kv : g_r3DepthBinds) if (kv.second > most) { most = kv.second; g_r3Depth = kv.first; }
            g_r3DepthBinds.clear();
            if (g_r3Depth) depthState = g_r3DepthStates[g_r3Depth];
        }

        // The settings, to the script: on only while the app is there to take volumes - the sweep costs the player frames.
        const bool appThere = AppThere();
        float link[vxcap::kLinkCount] = {};
        const float tanV = found && vxcap::Red3FaceOf(cam) >= 0 ? 1.0f / cam.sy : 0.48773f;   // the free camera's (measured)
        const bool userOff = UserOff(sc, "redengine3: F7, the sweep");
        link[vxcap::kLinkOn] = g_cfg.on && appThere && !userOff ? 1.0f : 0.0f;
        link[vxcap::kLinkNative] = g_r3NativeOn && g_cfg.red3Native ? 1.0f : 0.0f;
        link[vxcap::kLinkRadius] = g_cfg.radiusM;
        link[vxcap::kLinkBoundR] = g_cfg.boundR; link[vxcap::kLinkBoundZ] = g_cfg.boundZ;
        link[vxcap::kLinkScaleX] = g_cfg.scaleX; link[vxcap::kLinkScaleY] = g_cfg.scaleY; link[vxcap::kLinkScaleZ] = g_cfg.scaleZ;
        link[vxcap::kLinkFeetX] = g_cfg.feetX; link[vxcap::kLinkFeetY] = g_cfg.feetY; link[vxcap::kLinkFeetZ] = g_cfg.feetZ;
        link[vxcap::kLinkYaw] = g_cfg.yawDeg; link[vxcap::kLinkTurn] = g_cfg.turnWithPlayer ? 1.0f : 0.0f;
        int mask = 0;
        for (int v = 0; v < 5; v++) if (g_cfg.faceOn[v]) mask |= 1 << v;
        link[vxcap::kLinkFaces] = (float)mask;
        link[vxcap::kLinkTanV] = tanV;
        link[vxcap::kLinkTanH] = tanV * (float)g_r3W / (float)std::max(1u, g_r3H);
        g_link.Set(link);
        g_link.AllowSearch(found);
        g_link.Start();

        if (dev12 != g_dev12)
        {
            std::string err;
            g_dev12 = dev12;
            g_decOk = g_dec12.Init(dev12, (ID3D12CommandQueue*)queue->get_native(), err);
            Log(g_decOk ? "decoder: ready on the game's D3D12 device (REDengine 3)" : "decoder: FAILED %s", err.c_str());
        }

        // The player's last frame, kept while the rig is wanted, to show again over every face frame: the monitor shows
        // the player's view, never the sweep.
        // The face this frame is: an extra view's (red3_native.hpp) by the one being rendered, a swept frame's by its far plane.
        const int view = g_r3nView;
        int k = view >= 0 ? view : found ? vxcap::Red3FaceOf(cam) : -1;
        const bool wanted = g_cfg.on && appThere;
        if (g_r3Player.handle)
        {
            const resource_desc have = dev->get_resource_desc(g_r3Player);
            if (have.texture.width != bd.texture.width || have.texture.height != bd.texture.height || have.texture.format != bd.texture.format)
            { dev->destroy_resource(g_r3Player); g_r3Player = { 0 }; }
        }
        if (wanted && k == -1)
        {
            if (!g_r3Player.handle) g_r3Player = Red3Copy(dev, bd);
            if (g_r3Player.handle)
            {
                command_list* cmd = queue->get_immediate_command_list();
                cmd->barrier(bb, resource_usage::present, resource_usage::copy_source);
                cmd->copy_resource(bb, g_r3Player);
                cmd->barrier(bb, resource_usage::copy_source, resource_usage::present);
                queue->flush_immediate_command_list();
            }
        }

        // A face frame whose camera disagrees with its code (a side face's must look level, the top's straight down) is
        // left alone: the script and the engine out of step for a frame.
        if (k >= 0 && (k < 4 ? std::fabs(cam.fwd[2]) > 0.05f : cam.fwd[2] > -0.95f)) { ++g_r3Disagree; k = -2; }

        // A face frame: its picture and its depth, now; and the player's last frame shown in its place.
        if (g_cfg.on && k >= 0 && g_cfg.faceOn[k])
        {
            Red3Face& f = g_r3[k];
            if (f.colour.handle)
            {
                const resource_desc have = dev->get_resource_desc(f.colour);
                if (have.texture.width != bd.texture.width || have.texture.height != bd.texture.height || have.texture.format != bd.texture.format)
                { dev->destroy_resource(f.colour); f.colour = { 0 }; f.have = false; }
            }
            if (!f.colour.handle) f.colour = Red3Copy(dev, bd);
            if (f.colour.handle)
            {
                // The depth: this frame's, from the scene's depth buffer, out of the state the game's last barrier left
                // it in and back into it.
                const resource depthRes = { g_r3Depth };
                if (depthRes.handle)
                {
                    const resource_desc dd = dev->get_resource_desc(depthRes);
                    if (f.depth.handle)
                    {
                        const resource_desc have = dev->get_resource_desc(f.depth);
                        if (have.texture.width != dd.texture.width || have.texture.height != dd.texture.height || have.texture.format != dd.texture.format)
                        { dev->destroy_resource(f.depth); f.depth = { 0 }; }
                    }
                    if (!f.depth.handle) f.depth = Red3Copy(dev, dd);
                }
                command_list* cmd = queue->get_immediate_command_list();
                const bool withDepth = depthRes.handle && f.depth.handle;
                if (withDepth)
                {
                    const resource_usage was = (resource_usage)depthState;
                    cmd->barrier(depthRes, was, resource_usage::copy_source);
                    cmd->copy_resource(depthRes, f.depth);
                    cmd->barrier(depthRes, resource_usage::copy_source, was);
                }
                cmd->barrier(bb, resource_usage::present, resource_usage::copy_source);
                cmd->copy_resource(bb, f.colour);
                if (g_r3Player.handle)
                {
                    cmd->barrier(bb, resource_usage::copy_source, resource_usage::copy_dest);
                    cmd->barrier(g_r3Player, resource_usage::copy_dest, resource_usage::copy_source);
                    cmd->copy_resource(g_r3Player, bb);
                    cmd->barrier(g_r3Player, resource_usage::copy_source, resource_usage::copy_dest);
                    cmd->barrier(bb, resource_usage::copy_dest, resource_usage::present);
                }
                else cmd->barrier(bb, resource_usage::copy_source, resource_usage::present);
                queue->flush_immediate_command_list();
                std::lock_guard<std::mutex> lk(g_r3Mtx);
                ++g_r3Faces;
                if (withDepth)
                {
                    f.cam = cam;
                    f.have = true;
                    g_r3Fresh = true;
                    g_r3Latest = k;
                    ++g_r3Depths;
                }
            }
        }

        // A decode, when a face has completed: every face held, round the latest face's centre and yaw.
        bool fresh;
        int latest;
        {
            std::lock_guard<std::mutex> lk(g_r3Mtx);
            fresh = g_r3Fresh; latest = g_r3Latest;
        }
        // Paced by the performance counter, each due time one period after the last: GetTickCount64's 15.6 ms steps
        // made 30 Hz's 33 ms into 47 (21 volumes a second, measured).
        LARGE_INTEGER qpc, qpf;
        QueryPerformanceCounter(&qpc);
        QueryPerformanceFrequency(&qpf);
        const LONGLONG period = qpf.QuadPart / std::max(1, g_cfg.hz);
        if (g_decOk && g_cfg.on && fresh && latest >= 0 && qpc.QuadPart >= g_r3DecDue)
        {
            g_r3DecDue = std::max(g_r3DecDue + period, qpc.QuadPart - period);
            const int step = (int)((6ull * g_r3W + 16383) / 16384);   // six frames across must fit a 16384-wide texture
            vxcap::Settings cr = g_cfg;
            cr.faceRes = (int)(g_r3W / step);
            std::string err;
            if (!g_dec12.Configure(cr.faceRes, err)) { Log("decoder: configure FAILED %s", err.c_str()); g_decOk = false; return; }
            vxcap::Decoder12::Frames fr;
            ID3D12Resource* colour[6] = {};
            float centre[3];
            vxcap::Red3Camera latestCam;
            Red3Face faces[5];
            {
                std::lock_guard<std::mutex> lk(g_r3Mtx);
                for (int v = 0; v < 5; v++) faces[v] = g_r3[v];
                latestCam = g_r3[latest].cam;
                g_r3Fresh = false;
            }
            vxcap::Red3Centre(latest, latestCam, g_geo, centre);
            const double rho = vxcap::Red3RigYaw(latest, latestCam);
            const float exposure = g_autoExp * std::pow(2.0f, g_cfg.exposureComp);
            fr.colourFormat = ColourView(g_r3ColourFmt);
            fr.activeRows = (int)(g_r3H / step);
            for (int v = 0; v < 6; v++)
            {
                cr.faceOn[v] = v < 5 && g_cfg.faceOn[v] && faces[v].have && faces[v].colour.handle && faces[v].depth.handle;
                if (!cr.faceOn[v]) continue;
                colour[v] = reinterpret_cast<ID3D12Resource*>(faces[v].colour.handle);
                fr.depth[v] = reinterpret_cast<ID3D12Resource*>(faces[v].depth.handle);
                fr.depthFormat[v] = DepthView(dev->get_resource_desc(faces[v].depth).texture.format);
                fr.pack[v] = vxcap::Red3FaceConstants(v, faces[v].cam, centre, rho, g_cfg, g_geo, (int)g_r3W, (int)g_r3H, step, exposure);
            }
            if (g_dec12.Run(colour, cr, g_geo, exposure, 1.0f, nullptr, &fr)) ++g_runs;
        }
        if (g_decOk)
        {
            float lum = 0;
            uint32_t merged = 0;
            const int n = g_dec12.Poll(g_read, lum, merged);
            if (n >= 0) { g_mergeIn += (uint64_t)n + merged; g_mergeOut += merged; Deliver(n, lum); }
        }
    }

    // REDENGINE4 at present: the settings to the script, the faces' pictures copied, and a decode when one is due.
    void Present12Red4(ID3D12Device* dev12, command_queue* queue, swapchain* sc)
    {
        device* dev = queue->get_device();
        ++g_r4Frames;
        const bool appThere = AppThere();
        const bool userOff = UserOff(sc, "redengine4: F7, the rig");
        const bool wanted = g_cfg.on && appThere && !userOff;

        // rig.txt beside the script (bin\x64\plugins\cyber_engine_tweaks\mods\voxon_rig), written when it changes - up to
        // 30 times a second, so the rig follows the app's scale keys - and whole: written beside it, then put in its
        // place, so the script never reads half of one. Put in place again next time if the script had it open.
        if (GetTickCount64() - g_r4RigAt >= 33)
        {
            g_r4RigAt = GetTickCount64();
            const std::string text = vxcap::Red4RigText(g_cfg, wanted);
            if (text != g_r4RigText)
            {
                char path[MAX_PATH], tmp[MAX_PATH];
                sprintf_s(path, "%s\\plugins\\cyber_engine_tweaks\\mods\\voxon_rig\\rig.txt", g_dir);
                sprintf_s(tmp, "%s.tmp", path);
                if (FILE* f = nullptr; fopen_s(&f, tmp, "wb") == 0 && f)
                {
                    fwrite(text.data(), 1, text.size(), f);
                    fclose(f);
                    if (MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) g_r4RigText = text;
                }
                else Log("redengine4: could not write %s", tmp);
            }
        }

        // Copies replaced a few seconds ago: the GPU is long done with them. Destroyed with g_r4Mtx not held: destroying
        // one may fire destroy_resource on this thread, whose handler (Red4Forget) takes g_r4Mtx (FbReleaseLetGo).
        {
            std::vector<resource> go;
            {
                std::lock_guard<std::mutex> lk(g_r4Mtx);
                for (size_t i = 0; i < g_r4Dead.size();)
                {
                    if (GetTickCount64() - g_r4Dead[i].second < 3000) { i++; continue; }
                    go.push_back(g_r4Dead[i].first);
                    g_r4Dead.erase(g_r4Dead.begin() + i);
                }
            }
            for (const resource& r : go) dev->destroy_resource(r);
        }

        if (dev12 != g_dev12)
        {
            std::string err;
            g_dev12 = dev12;
            g_decOk = g_dec12.Init(dev12, (ID3D12CommandQueue*)queue->get_native(), err);
            Log(g_decOk ? "decoder: ready on the game's D3D12 device (REDengine 4)" : "decoder: FAILED %s", err.c_str());
        }

        LARGE_INTEGER qpc, qpf;
        QueryPerformanceCounter(&qpc);
        QueryPerformanceFrequency(&qpf);
        const LONGLONG period = qpf.QuadPart / std::max(1, g_cfg.hz);
        if (g_decOk && wanted && qpc.QuadPart >= g_r3DecDue)
        {
            // Each face's picture copied now, out of the state the game left it in and back; the depths are this frame's
            // or the last.
            vxcap::Settings cr = g_cfg;
            vxcap::Decoder12::Frames fr;
            ID3D12Resource* colour[6] = {};
            format colourFmt = format::unknown;
            int faces = 0;
            command_list* cmd = queue->get_immediate_command_list();
            {
                std::lock_guard<std::mutex> lk(g_r4Mtx);
                for (int v = 0; v < 6; v++)
                {
                    cr.faceOn[v] = false;
                    if (v >= vxcap::kRed4Faces || !g_cfg.faceOn[v]) continue;
                    Red4Face& f = g_r4[v];
                    if (!f.game || !f.depth.handle || !f.depths) continue;
                    const resource game = { f.game };
                    const resource_desc gd = dev->get_resource_desc(game);
                    if ((int)gd.texture.width != vxcap::Red4FaceSize(g_cfg, v)) continue;
                    if (!Red4CopyOf(dev, f.colour, gd.texture.width, gd.texture.format).handle) continue;
                    const resource_usage was = (resource_usage)f.gameState;
                    cmd->barrier(game, was, resource_usage::copy_source);
                    cmd->copy_resource(game, f.colour);
                    cmd->barrier(game, resource_usage::copy_source, was);
                    colour[v] = reinterpret_cast<ID3D12Resource*>(f.colour.handle);
                    fr.depth[v] = reinterpret_cast<ID3D12Resource*>(f.depth.handle);
                    fr.depthFormat[v] = DepthView(dev->get_resource_desc(f.depth).texture.format);
                    colourFmt = gd.texture.format;
                    cr.faceOn[v] = true;
                    ++faces;
                }
            }
            queue->flush_immediate_command_list();
            g_r4Pictures += faces;
            if (faces)
            {
                g_r3DecDue = std::max(g_r3DecDue + period, qpc.QuadPart - period);
                std::string err;
                if (!g_dec12.Configure(cr.faceRes, err)) { Log("decoder: configure FAILED %s", err.c_str()); g_decOk = false; return; }
                const float exposure = g_autoExp * std::pow(2.0f, g_cfg.exposureComp);
                fr.colourFormat = ColourView(colourFmt);
                fr.activeRows = cr.faceRes;
                for (int v = 0; v < vxcap::kRed4Faces; v++)
                    if (cr.faceOn[v]) fr.pack[v] = vxcap::Red4FaceConstants(v, g_cfg, g_geo, exposure);
                if (g_dec12.Run(colour, cr, g_geo, exposure, 1.0f, nullptr, &fr)) ++g_runs;
            }
        }
        if (g_decOk)
        {
            float lum = 0;
            uint32_t merged = 0;
            const int n = g_dec12.Poll(g_read, lum, merged);
            if (n >= 0) { g_mergeIn += (uint64_t)n + merged; g_mergeOut += merged; Deliver(n, lum); }
        }
    }

    void Present11(ID3D11Device* dev)
    {
        ID3D11DeviceContext* ctx = nullptr;
        dev->GetImmediateContext(&ctx);
        if (dev != g_dev)
        {
            std::string err;
            g_dev = dev;
            g_decOk = g_dec.Init(dev, err);
            Log(g_decOk ? "decoder: ready on the game's D3D11 device" : "decoder: FAILED %s", err.c_str());
        }
        if (g_decOk)
        {
            std::string err;
            if (!g_dec.Configure(g_cfg.faceRes, err)) { Log("decoder: configure FAILED %s", err.c_str()); g_decOk = false; }
        }
        Handshake11(dev, ctx);
        if (g_cfg.engine == vxcap::kEngineUnity)
            for (int v = 0; v < 6; v++)
            {
                Face& f = g_face[v];
                if (g_cfg.faceOn[v] && f.res) DepthView11(dev, v, f.depthRes, f.depthViewOf, f.depthSrv, "");
                if (g_cfg.faceOn[v] && f.playerRes) DepthView11(dev, v, f.playerDepthRes, f.playerDepthViewOf, f.playerDepthSrv, "player capture's ");
                if (g_cfg.faceOn[v] && f.solidRes) DepthView11(dev, v, f.solidDepthRes, f.solidDepthViewOf, f.solidDepthSrv, "solid capture's ");
            }
        if (g_decOk && g_cfg.on)
        {
            if (FacesReady())
            {
                ID3D11ShaderResourceView* srv[6] = {};
                ID3D11ShaderResourceView* depth[6] = {};
                ID3D11ShaderResourceView* surface[6] = {};
                ID3D11ShaderResourceView* player[6] = {};
                ID3D11ShaderResourceView* solid[6] = {};
                ID3D11ShaderResourceView* glow[6] = {};
                for (int v = 0; v < 6; v++)
                    if (g_cfg.faceOn[v])
                    {
                        glow[v] = g_face[v].glowSrv;
                        solid[v] = g_cfg.engine == vxcap::kEngineUnity ? g_face[v].solidDepthSrv : g_face[v].solidSrv;
                        srv[v] = g_face[v].srv; depth[v] = g_face[v].depthSrv; surface[v] = g_face[v].surfaceSrv;
                        // UNITY: the player capture's depth buffer; Unreal's capture holds its depth in the colour
                        player[v] = g_cfg.engine == vxcap::kEngineUnity ? g_face[v].playerDepthSrv : g_face[v].playerSrv;
                    }
                const float exposure = g_autoExp * std::pow(2.0f, g_cfg.exposureComp);
                if (g_dec.Run(ctx, srv, g_cfg, g_geo, exposure, 1.0f, depth, surface, player, solid, glow)) ++g_runs;
                for (auto& f : g_face) f.dirty = false;
            }
            float lum = 0;
            uint32_t merged = 0;
            const int n = g_dec.Poll(ctx, g_read, lum, merged);
            if (n >= 0) { g_mergeIn += (uint64_t)n + merged; g_mergeOut += merged; Deliver(n, lum); }
        }
        ctx->Release();
    }

    // FROSTBITE at present: the views kept on the renderer's list round the player, and when a decode is due each face's
    // depth and colour copied and decoded.
    void Present11Frostbite(ID3D11Device* dev, swapchain* sc)
    {
        ID3D11DeviceContext* ctx = nullptr;
        dev->GetImmediateContext(&ctx);
        if (dev != g_dev)
        {
            std::string err;
            g_dev = dev;
            g_decOk = g_dec.Init(dev, err);
            Log(g_decOk ? "decoder: ready on the game's D3D11 device (Frostbite)" : "decoder: FAILED %s", err.c_str());
        }
        if (g_decOk)
        {
            std::string err;
            if (!g_dec.Configure(g_cfg.faceRes, err)) { Log("decoder: configure FAILED %s", err.c_str()); g_decOk = false; }
        }
        const bool wanted = g_cfg.on && AppThere() && !UserOff(sc, "frostbite: F7, the rig", true);
        const ULONGLONG now = GetTickCount64();

        // The views: put in once the game has run a while (its threads made); the breakpoint set again when the player's
        // view has not been seen for a few seconds (a thread made since).
        if (!g_fbInstalled && !g_fbFailed && g_presents > 300)
        {
            std::string why;
            g_fbInstalled = g_fbViews.Install(why);
            g_fbFailed = !g_fbInstalled;
            if (g_fbInstalled) { g_fbViews.Arm(); g_fbArmedAt = g_fbRootAt = now; Log("frostbite: views in, the breakpoints on %d threads", g_fbViews.Armed()); }
            else Log("frostbite: NOT in: %s", why.c_str());
        }
        int faces[vxcap::kFbMaxFaces] = {};
        if (g_fbInstalled)
        {
            const uint64_t seen = g_fbViews.RootSeen();
            if (seen != g_fbRootLast) { g_fbRootLast = seen; g_fbRootAt = now; }
            else if (now - g_fbRootAt > 3000 && now - g_fbArmedAt > 3000)
            {
                g_fbViews.Arm(); g_fbArmedAt = now;
                Log("frostbite: the player's view not seen for 3 s: the breakpoints set again, on %d threads", g_fbViews.Armed());
            }
            static bool wasWanted = false;    // the breakpoints set again as the faces go on: threads made since the menu
            if (wanted && !wasWanted) { g_fbViews.Arm(); g_fbArmedAt = now; }
            wasWanted = wanted;
            g_fbLinked = g_fbViews.Update(g_cfg, g_geo, wanted, faces);
            if (!g_fbLinked)
            {   // no faces drawn: the game textures held for them let go (a level's targets are not kept past it)
                std::lock_guard<std::mutex> lk(g_fbMtx);
                for (auto& f : g_fb) { FbLetGoHeld(f.gameColour); FbLetGoHeld(f.gameDepth); }
            }
            const int gv = g_fbViews.GameViewCount();
            if (gv != g_fbGameViews)
            {
                if (g_fbGameViews >= 0)
                    Log("frostbite: the game has %d view%s on the list: room for %d faces", gv, gv == 1 ? "" : "s",
                        std::max(0, vxcap::kFbMaxViews - gv));
                g_fbGameViews = gv;
            }
        }

        LARGE_INTEGER qpc, qpf;
        QueryPerformanceCounter(&qpc);
        QueryPerformanceFrequency(&qpf);
        const LONGLONG period = qpf.QuadPart / std::max(1, g_cfg.hz);
        if (g_decOk && wanted && g_fbLinked && qpc.QuadPart >= g_r3DecDue)
        {
            vxcap::Settings cr = g_cfg;
            vxcap::Decoder::Frames fr;
            ID3D11ShaderResourceView* colour[6] = {};
            int got = 0;
            for (int v = 0; v < 6; v++) cr.faceOn[v] = false;
            // The faces to copy, and a reference more on each game texture for the copy: taken under the lock, used
            // without it (NO D3D CALL UNDER g_fbMtx, above). Their copies are this thread's alone.
            struct Copy { int v; ID3D11Texture2D* gc; ID3D11Texture2D* gd; };
            Copy todo[vxcap::kFbMaxFaces];
            int n = 0;
            {
                std::lock_guard<std::mutex> lk(g_fbMtx);
                for (int i = 0; i < g_fbLinked; i++)
                {
                    const FbFace& f = g_fb[faces[i]];
                    // drawn this frame or the one before (a face the game's own views left no room for is not)
                    if (!f.gameColour || !f.gameDepth || g_fbFrame - f.frame > 1) continue;
                    todo[n] = { faces[i], reinterpret_cast<ID3D11Texture2D*>(f.gameColour), reinterpret_cast<ID3D11Texture2D*>(f.gameDepth) };
                    todo[n].gc->AddRef(); todo[n].gd->AddRef();
                    n++;
                }
            }
            for (int i = 0; i < n; i++)
            {
                const int v = todo[i].v;
                FbFace& f = g_fb[v];
                D3D11_TEXTURE2D_DESC dd{};
                todo[i].gd->GetDesc(&dd);
                if (FbCopyOf(dev, todo[i].gc, f.colour, f.colourSrv, DXGI_FORMAT_R11G11B10_FLOAT)
                    && FbCopyOf(dev, todo[i].gd, f.depth, f.depthSrv, DepthView((format)dd.Format)))
                {
                    ctx->CopyResource(f.colour, todo[i].gc);
                    ctx->CopyResource(f.depth, todo[i].gd);
                    colour[v] = f.colourSrv;
                    fr.depth[v] = f.depthSrv;
                    cr.faceOn[v] = true;
                    ++got;
                }
                todo[i].gc->Release(); todo[i].gd->Release();
            }
            g_fbPictures += got;
            if (got)
            {
                g_r3DecDue = std::max(g_r3DecDue + period, qpc.QuadPart - period);
                const float exposure = g_autoExp * std::pow(2.0f, g_cfg.exposureComp);
                fr.activeRows = cr.faceRes;
                for (int v = 0; v < 6; v++)
                    if (cr.faceOn[v]) fr.pack[v] = vxcap::FbFaceConstants(v, g_cfg, g_geo, exposure);
                fr.player = vxcap::FbPlayerShape(g_cfg, g_geo);
                if (g_dec.Run(ctx, colour, cr, g_geo, exposure, 1.0f, nullptr, nullptr, nullptr, nullptr, nullptr, &fr)) ++g_runs;
            }
        }
        {
            std::lock_guard<std::mutex> lk(g_fbMtx);
            ++g_fbFrame;                              // the next frame's binds
        }
        FbReleaseLetGo();                             // copies made again this frame: the old ones, now
        if (g_decOk)
        {
            float lum = 0;
            uint32_t merged = 0;
            const int n = g_dec.Poll(ctx, g_read, lum, merged);
            if (n >= 0) { g_mergeIn += (uint64_t)n + merged; g_mergeOut += merged; Deliver(n, lum); }
        }
        ctx->Release();
    }

    // THE SIMS 4 at present: the render job hooked (the first frame: no lot is drawn yet), the settings handed to it, the
    // faces' targets made, and when a decode is due the faces it drew lately decoded from them.
    void Present11Sims4(ID3D11Device* dev, swapchain* sc)
    {
        ID3D11DeviceContext* ctx = nullptr;
        dev->GetImmediateContext(&ctx);
        if (dev != g_dev)
        {
            std::string err;
            g_dev = dev;
            g_decOk = g_dec.Init(dev, err);
            Log(g_decOk ? "decoder: ready on the game's D3D11 device (The Sims 4)" : "decoder: FAILED %s", err.c_str());
        }
        if (g_decOk)
        {
            std::string err;
            if (!g_dec.Configure(g_cfg.faceRes, err)) { Log("decoder: configure FAILED %s", err.c_str()); g_decOk = false; }
        }
        if (!g_ts4Hooked && !g_ts4Failed)
        {
            std::string why;
            g_ts4Hooked = g_ts4.Install((void*)&Ts4JobDetour, why);
            g_ts4Failed = !g_ts4Hooked;
            if (g_ts4Hooked) Log("sims4: the render job hooked");
            else Log("sims4: NOT hooked: %s", why.c_str());
        }
        const bool wanted = g_cfg.on && AppThere() && !UserOff(sc, "sims4: F7, the rig") && g_ts4Hooked;
        const uint64_t frame = g_ts4.Plan(g_cfg, g_geo, wanted);

        const format cf = (format)g_ts4.ColourFormat(), df = (format)g_ts4.DepthFormat();
        const uint32_t w = g_ts4.GameWidth(), h = g_ts4.GameHeight();
        const uint64_t wh = (uint64_t)w << 32 | h;
        if (cf != format::unknown && w && h && (!g_ts4Set || g_ts4Set->t.w != w || g_ts4Set->t.h != h) && g_ts4Unmade != wh)
        {
            if (Ts4Set* s = Ts4Make(dev, w, h, cf, df))
            {
                if (g_ts4Set) { g_ts4Set->retiredAt = g_presents; g_ts4Old.push_back(g_ts4Set); }
                g_ts4Set = s;
                g_ts4.SetTargets(&s->t);
                Log("sims4: the faces' targets made, %ux%u like the game's picture (its colour format %u, depth %u); each face is its %u-texel square",
                    w, h, (unsigned)cf, (unsigned)df, vxcap::Ts4Square(w, h));
            }
            else g_ts4Unmade = wh;
        }
        for (size_t i = 0; i < g_ts4Old.size();)
        {
            if (g_presents - g_ts4Old[i]->retiredAt > kTs4Keep) { Ts4Free(g_ts4Old[i]); g_ts4Old.erase(g_ts4Old.begin() + i); }
            else i++;
        }

        LARGE_INTEGER qpc, qpf;
        QueryPerformanceCounter(&qpc);
        QueryPerformanceFrequency(&qpf);
        const LONGLONG period = qpf.QuadPart / std::max(1, g_cfg.hz);
        if (g_decOk && wanted && g_ts4Set && qpc.QuadPart >= g_r3DecDue)
        {
            vxcap::Settings cr = g_cfg;
            vxcap::Decoder::Frames fr;
            ID3D11ShaderResourceView* colour[6] = {};
            int got = 0;
            for (int v = 0; v < 6; v++)
            {
                // drawn in the last few frames (a lot left, or a face just turned on, is not)
                const uint64_t at = v < vxcap::kTs4Faces ? g_ts4.DrawnAt(v) : 0;
                cr.faceOn[v] = g_cfg.faceOn[v] && at && frame - at <= 3;
                if (!cr.faceOn[v]) continue;
                colour[v] = g_ts4Set->colourSrv[v];
                fr.depth[v] = g_ts4Set->depthSrv[v];
                ++got;
            }
            g_ts4Pictures += got;
            if (got)
            {
                g_r3DecDue = std::max(g_r3DecDue + period, qpc.QuadPart - period);
                const float exposure = g_autoExp * std::pow(2.0f, g_cfg.exposureComp);
                fr.activeRows = cr.faceRes;
                for (int v = 0; v < 6; v++)
                    if (cr.faceOn[v]) fr.pack[v] = vxcap::Ts4FaceConstants(v, g_cfg, g_geo, exposure, vxcap::Ts4Square(g_ts4Set->t.w, g_ts4Set->t.h));
                if (g_dec.Run(ctx, colour, cr, g_geo, exposure, 1.0f, nullptr, nullptr, nullptr, nullptr, nullptr, &fr)) ++g_runs;
            }
        }
        if (g_decOk)
        {
            float lum = 0;
            uint32_t merged = 0;
            const int n = g_dec.Poll(ctx, g_read, lum, merged);
            if (n >= 0) { g_mergeIn += (uint64_t)n + merged; g_mergeOut += merged; Deliver(n, lum); }
        }
        ctx->Release();
    }

    // DIAG (temporary): the presented frame's mean brightness and sharpness (mean step between neighbours), sorted into
    // frames with faces drawn in them, the frame after, and the rest; logged every 5 s.
    void Ce2Diag()
    {
        static ID3D10Texture2D* st = nullptr;
        static uint64_t lastDrawn = 0;
        static bool lastWasFaces = false;
        static double sum[3][2] = {}; static uint64_t n[3] = {};
        static ULONGLONG at = GetTickCount64();
        ID3D10Texture2D* bb = nullptr;
        if (FAILED(g_ce2Sc->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)&bb))) return;
        D3D10_TEXTURE2D_DESC d; bb->GetDesc(&d);
        if (!st)
        {
            D3D10_TEXTURE2D_DESC s = d; s.Usage = D3D10_USAGE_STAGING; s.BindFlags = 0; s.CPUAccessFlags = D3D10_CPU_ACCESS_READ; s.MiscFlags = 0;
            s.SampleDesc.Count = 1; s.SampleDesc.Quality = 0;
            g_ce2Dev10->CreateTexture2D(&s, nullptr, &st);
        }
        if (st && d.SampleDesc.Count == 1)
        {
            g_ce2Dev10->CopyResource(st, bb);
            D3D10_MAPPED_TEXTURE2D m;
            if (SUCCEEDED(st->Map(0, D3D10_MAP_READ, 0, &m)))
            {
                double lum = 0, step = 0; uint64_t k = 0;
                for (uint32_t y = 8; y + 8 < d.Height; y += 8)
                {
                    const uint8_t* r = (const uint8_t*)m.pData + (size_t)y * m.RowPitch;
                    for (uint32_t x = 8; x + 8 < d.Width; x += 8)
                    {
                        const int a = r[x * 4] + r[x * 4 + 1] + r[x * 4 + 2], b = r[x * 4 + 4] + r[x * 4 + 5] + r[x * 4 + 6];
                        lum += a; step += std::abs(a - b); ++k;
                    }
                }
                st->Unmap(0);
                const bool faces = g_ce2.Drawn() != lastDrawn;
                lastDrawn = g_ce2.Drawn();
                const int kind = faces ? 0 : lastWasFaces ? 1 : 2;
                lastWasFaces = faces;
                if (k) { sum[kind][0] += lum / k / 3.0; sum[kind][1] += step / k / 3.0; ++n[kind]; }
            }
        }
        bb->Release();
        if (GetTickCount64() - at >= 5000)
        {
            at = GetTickCount64();
            auto avg = [&](int i, int j) { return n[i] ? sum[i][j] / n[i] : 0.0; };
            Log("DIAG cryengine2 flags 0x%x | frames with faces %llu: brightness %.2f sharpness %.3f | the frame after %llu: %.2f %.3f | the rest %llu: %.2f %.3f",
                g_cfg.ce2Flags, (unsigned long long)n[0], avg(0, 0), avg(0, 1), (unsigned long long)n[1], avg(1, 0), avg(1, 1),
                (unsigned long long)n[2], avg(2, 0), avg(2, 1));
            memset(sum, 0, sizeof sum); memset(n, 0, sizeof n);
        }
    }

    // CRYSIS at present (D3D10, the game's main thread): the add-on's own D3D11 device and decoder the first time; the
    // faces' shared textures for the game's picture; the faces copied since the last present decoded; and whether the next
    // frame draws faces - when a volume is due and the last ones are decoded.
    void Present10Crysis(ID3D10Device* dev10, swapchain* sc)
    {
        if (dev10 != g_ce2Dev10)
        {
            Ce2Release();
            g_ce2Dev10 = dev10;
            IDXGIDevice* dx = nullptr;
            IDXGIAdapter* ad = nullptr;
            const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
            if (SUCCEEDED(dev10->QueryInterface(__uuidof(IDXGIDevice), (void**)&dx)) && SUCCEEDED(dx->GetAdapter(&ad))
                && SUCCEEDED(D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_ce2Dev11, nullptr, &g_ce2Ctx11)))
            {
                std::string err;
                g_decOk = g_dec.Init(g_ce2Dev11, err);
                Log(g_decOk ? "decoder: ready on a D3D11 device of its own beside the game's D3D10 one (Crysis)" : "decoder: FAILED %s", err.c_str());
            }
            else Log("cryengine2: no D3D11 device on the game's GPU: nothing can be decoded");
            if (ad) ad->Release();
            if (dx) dx->Release();
        }
        if (!g_ce2Dev11) return;
        g_ce2Sc = reinterpret_cast<IDXGISwapChain*>(sc->get_native());
        if (g_decOk)
        {
            std::string err;
            if (!g_dec.Configure(g_cfg.faceRes, err)) { Log("decoder: configure FAILED %s", err.c_str()); g_decOk = false; }
        }
        if (!g_ce2Hooked && !g_ce2Failed)
        {
            std::string why;
            g_ce2Hooked = g_ce2.Install((void*)&Ce2RenderWorldDetour, why);
            g_ce2Failed = !g_ce2Hooked;
            if (g_ce2Hooked) Log("cryengine2: the 3D engine's RenderWorld taken (its vtable, slot %d)", vxcap::kCe2RenderWorldSlot);
            else Log("cryengine2: NOT hooked: %s", why.c_str());
        }

        // The faces' textures, for the picture as it is now (made again when it changes, once nothing is waiting in them).
        ID3D10Texture2D* bb = nullptr;
        if (SUCCEEDED(g_ce2Sc->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)&bb)))
        {
            D3D10_TEXTURE2D_DESC d;
            bb->GetDesc(&d);
            bb->Release();
            bool pending = false;
            if (g_ce2Set) for (bool w : g_ce2Set->written) pending = pending || w;
            if ((!g_ce2Set || g_ce2Set->w != d.Width || g_ce2Set->h != d.Height) && !pending)
            {
                Ce2Free(g_ce2Set);
                g_ce2Set = Ce2Make(d.Width, d.Height, d.Format, d.SampleDesc.Count > 1);
                if (g_ce2Set) Log("cryengine2: the faces' shared textures made: the middle %u-texel square of the game's %ux%u picture (format %u%s)",
                                  g_ce2Set->side, d.Width, d.Height, (unsigned)d.Format, d.SampleDesc.Count > 1 ? ", multisampled: resolved first" : "");
            }
        }

        // The faces copied since the last present, decoded: D3D11 takes key 1, gives key 0 back.
        Ce2Set* s = g_ce2Set;
        if (g_decOk && s)
        {
            vxcap::Settings cr = g_cfg;
            vxcap::Decoder::Frames fr;
            ID3D11ShaderResourceView* colour[6] = {};
            int got = 0;
            for (int v = 0; v < 6; v++)
            {
                cr.faceOn[v] = false;
                if (v >= vxcap::kCe2Faces || !s->written[v]) continue;
                if (s->km11[2 * v]->AcquireSync(1, 0) != S_OK) continue;
                if (s->km11[2 * v + 1]->AcquireSync(1, 0) != S_OK) { s->km11[2 * v]->ReleaseSync(1); continue; }
                cr.faceOn[v] = true;
                colour[v] = s->colourSrv[v];
                fr.depth[v] = s->depthSrv[v];
                ++got;
            }
            if (got)
            {
                const float exposure = g_autoExp * std::pow(2.0f, g_cfg.exposureComp);
                fr.activeRows = cr.faceRes;
                for (int v = 0; v < 6; v++)
                    if (cr.faceOn[v]) fr.pack[v] = vxcap::Ce2FaceConstants(v, g_cfg, g_geo, exposure, s->side);
                if (g_dec.Run(g_ce2Ctx11, colour, cr, g_geo, exposure, 1.0f, nullptr, nullptr, nullptr, nullptr, nullptr, &fr)) ++g_runs;
                for (int v = 0; v < vxcap::kCe2Faces; v++)
                {
                    if (!cr.faceOn[v]) continue;
                    s->km11[2 * v + 1]->ReleaseSync(0);
                    s->km11[2 * v]->ReleaseSync(0);
                    s->written[v] = false;
                }
                g_ce2Pictures += got;
            }
        }

        // The next frame's faces: when a volume is due, and nothing is waiting to be decoded.
        LARGE_INTEGER qpc, qpf;
        QueryPerformanceCounter(&qpc);
        QueryPerformanceFrequency(&qpf);
        const LONGLONG period = qpf.QuadPart / std::max(1, g_cfg.hz);
        bool pending = false;
        if (s) for (bool w : s->written) pending = pending || w;
        const bool wanted = g_cfg.on && AppThere() && !UserOff(sc, "cryengine2: F7, the rig") && g_ce2Hooked && g_decOk && s;
        const bool draw = wanted && !pending && qpc.QuadPart >= g_r3DecDue;
        if (draw) g_r3DecDue = std::max(g_r3DecDue + period, qpc.QuadPart - period);
        g_ce2.Plan(g_cfg, g_geo, draw);
        if (g_cfg.ce2Diag) Ce2Diag();

        if (g_decOk)
        {
            float lum = 0;
            uint32_t merged = 0;
            const int n = g_dec.Poll(g_ce2Ctx11, g_read, lum, merged);
            if (n >= 0) { g_mergeIn += (uint64_t)n + merged; g_mergeOut += merged; Deliver(n, lum); }
        }
    }

    void Present12(ID3D12Device* dev, ID3D12CommandQueue* q)
    {
        if (dev != g_dev12)
        {
            std::string err;
            g_dev12 = dev;
            g_decOk = g_dec12.Init(dev, q, err);
            Log(g_decOk ? "decoder: ready on the game's D3D12 device" : "decoder: FAILED %s", err.c_str());
        }
        if (g_decOk)
        {
            std::string err;
            if (!g_dec12.Configure(g_cfg.faceRes, err)) { Log("decoder: configure FAILED %s", err.c_str()); g_decOk = false; }
        }
        Handshake12();
        if (g_cfg.engine == vxcap::kEngineUnity)
        {
            static bool said = false;
            if (!said) { said = true; Log("Unity on D3D12 is not supported yet: run the game on D3D11"); }
            return;
        }
        if (g_decOk && g_cfg.on)
        {
            if (FacesReady())
            {
                ID3D12Resource* res[6] = {};
                ID3D12Resource* surface[6] = {};
                ID3D12Resource* player[6] = {};
                ID3D12Resource* solid[6] = {};
                ID3D12Resource* glow[6] = {};
                for (int v = 0; v < 6; v++)
                    if (g_cfg.faceOn[v])
                    {
                        glow[v] = reinterpret_cast<ID3D12Resource*>(g_face[v].glowRes.load());
                        solid[v] = reinterpret_cast<ID3D12Resource*>(g_face[v].solidRes.load());
                        res[v] = reinterpret_cast<ID3D12Resource*>(g_face[v].res.load());
                        surface[v] = reinterpret_cast<ID3D12Resource*>(g_face[v].surfaceRes.load());
                        player[v] = reinterpret_cast<ID3D12Resource*>(g_face[v].playerRes.load());
                    }
                const float exposure = g_autoExp * std::pow(2.0f, g_cfg.exposureComp);
                if (g_dec12.Run(res, g_cfg, g_geo, exposure, 1.0f, surface, nullptr, player, solid, glow)) ++g_runs;
                for (auto& f : g_face) f.dirty = false;
            }
            float lum = 0;
            uint32_t merged = 0;
            const int n = g_dec12.Poll(g_read, lum, merged);
            if (n >= 0) { g_mergeIn += (uint64_t)n + merged; g_mergeOut += merged; Deliver(n, lum); }
        }
    }

    void on_present(command_queue* queue, swapchain* sc, const rect*, const rect*, uint32_t, const rect*)
    {
        device* d = queue->get_device();
        const device_api api = d->get_api();
        const bool crysis = api == device_api::d3d10 && g_cfg.engine == vxcap::kEngineCryEngine2;
        if (api != device_api::d3d11 && api != device_api::d3d12 && !crysis) return;
        ++g_presents;
        LARGE_INTEGER t0, t1, fq;
        QueryPerformanceCounter(&t0);

        if (!g_worker.joinable()) g_worker = std::thread(Worker);
        ReloadSettings(false);
        if (crysis) Present10Crysis((ID3D10Device*)d->get_native(), sc);
        else if (api == device_api::d3d11 && g_cfg.engine == vxcap::kEngineFrostbite) Present11Frostbite((ID3D11Device*)d->get_native(), sc);
        else if (api == device_api::d3d11 && g_cfg.engine == vxcap::kEngineSims4) Present11Sims4((ID3D11Device*)d->get_native(), sc);
        else if (api == device_api::d3d11) Present11((ID3D11Device*)d->get_native());
        else if (g_cfg.engine == vxcap::kEngineRed3) Present12Red3((ID3D12Device*)d->get_native(), queue, sc);
        else if (g_cfg.engine == vxcap::kEngineRed4) Present12Red4((ID3D12Device*)d->get_native(), queue, sc);
        else Present12((ID3D12Device*)d->get_native(), (ID3D12CommandQueue*)queue->get_native());

        QueryPerformanceCounter(&t1);
        QueryPerformanceFrequency(&fq);
        g_presentMs = std::max(g_presentMs, (t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart);
        const ULONGLONG now = GetTickCount64();
        if (now - g_statAt >= 5000)
        {
            const double secs = g_statAt ? (now - g_statAt) / 1000.0 : 5.0;
            static uint64_t runs0, vols0, sent0, pres0, w0[6], mIn0, mOut0;
            char writes[64];
            sprintf_s(writes, "%.0f/%.0f/%.0f/%.0f/%.0f/%.0f",
                (g_writes[0] - w0[0]) / secs, (g_writes[1] - w0[1]) / secs, (g_writes[2] - w0[2]) / secs,
                (g_writes[3] - w0[3]) / secs, (g_writes[4] - w0[4]) / secs, (g_writes[5] - w0[5]) / secs);
            for (int v = 0; v < 6; v++) w0[v] = g_writes[v];
            static uint64_t d0[6];
            char drawn[96];
            sprintf_s(drawn, "%.0f/%.0f/%.0f/%.0f/%.0f/%.0f",
                (g_faceDraws[0] - d0[0]) / secs, (g_faceDraws[1] - d0[1]) / secs, (g_faceDraws[2] - d0[2]) / secs,
                (g_faceDraws[3] - d0[3]) / secs, (g_faceDraws[4] - d0[4]) / secs, (g_faceDraws[5] - d0[5]) / secs);
            for (int v = 0; v < 6; v++) d0[v] = g_faceDraws[v];
            const uint64_t mIn = g_mergeIn - mIn0, mOut = g_mergeOut - mOut0;
            mIn0 = g_mergeIn; mOut0 = g_mergeOut;
            Log("game %.1f fps | faces %s%s%s%s%s%s written %s/s, drawn into %s/s | decoded %.1f/s, read %.1f/s, sent %.1f/s | last %llu voxels, %.1f%% merged into shared voxels | dropped host %llu, worker %llu, ring %llu, cut short %llu | candidates %zu | worst present %.2f ms | auto-exposure %+.2f stops | pipe %s",
                (g_presents - pres0) / secs,
                g_face[0].res ? "0" : "-", g_face[1].res ? "1" : "-", g_face[2].res ? "2" : "-",
                g_face[3].res ? "3" : "-", g_face[4].res ? "4" : "-", g_face[5].res ? "5" : "-", writes, drawn,
                (g_runs - runs0) / secs, (g_volumes - vols0) / secs, (g_sent - sent0) / secs,
                (unsigned long long)g_lastCount.load(), mIn ? 100.0 * (double)mOut / (double)mIn : 0.0, (unsigned long long)g_dropHost.load(),
                (unsigned long long)g_dropWorker.load(), (unsigned long long)(g_dec.dropped + g_dec12.dropped), (unsigned long long)(g_dec.truncated + g_dec12.truncated), g_cands.size(), g_presentMs, std::log2(std::max(g_autoExp, 1e-9f)),
                g_pipeUp ? "up" : "down");
            if (g_r3NativeOn)
            {
                static uint64_t w0, x0;
                Log("redengine3 native views: %.1f world frames/s with faces, %.1f face views/s",
                    (g_r3nWorld - w0) / secs, (g_r3nExtra - x0) / secs);
                w0 = g_r3nWorld; x0 = g_r3nExtra;
            }
            if (g_cfg.engine == vxcap::kEngineRed3)
            {
                static uint64_t fr0, fa0, de0, nc0, di0;
                Log("redengine3: %.1f frames/s, %.1f face pictures/s, %.1f face depths/s, %.1f frames with no camera found/s, %.1f face frames whose camera disagreed/s | %ux%u | link: %s",
                    (g_r3Frames - fr0) / secs, (g_r3Faces - fa0) / secs, (g_r3Depths - de0) / secs, (g_r3NoCamera - nc0) / secs,
                    (g_r3Disagree - di0) / secs, g_r3W, g_r3H, g_link.Status().c_str());
                fr0 = g_r3Frames; fa0 = g_r3Faces; de0 = g_r3Depths; nc0 = g_r3NoCamera; di0 = g_r3Disagree;
            }
            if (g_cfg.engine == vxcap::kEngineRed4)
            {
                static uint64_t fr0, de0, pi0;
                std::string seen;
                {
                    std::lock_guard<std::mutex> lk(g_r4Mtx);
                    for (const auto& f : g_r4) seen += f.game ? (f.depth.handle ? "C" : "c") : "-";
                }
                Log("redengine4: %.1f frames/s, %.1f face depths copied/s, %.1f face pictures copied/s | faces seen %s (C: picture and depth) | shared depth %s",
                    (g_r4Frames - fr0) / secs, (g_r4Depths - de0) / secs, (g_r4Pictures - pi0) / secs, seen.c_str(),
                    g_r4Carried ? "carried" : "not seen");
                fr0 = g_r4Frames; de0 = g_r4Depths; pi0 = g_r4Pictures;
            }
            if (g_cfg.engine == vxcap::kEngineFrostbite)
            {
                static uint64_t ro0, fa0, bi0, pi0, bu0, fs0, fc0;
                std::string seen;
                {
                    std::lock_guard<std::mutex> lk(g_fbMtx);
                    for (const auto& f : g_fb) seen += f.gameColour && f.gameDepth ? "C" : "-";
                }
                Log("frostbite: the game's own views %d | %d faces wanted | the player's feet from the soldier %.1f/s, the camera %.1f/s | frames built %.1f/s, the player's view %.1f/s, faces' cameras set %.1f/s | face targets found %.1f/s, copied %.1f/s | faces seen %s (C: depth and colour) | rig yaw %.1f deg",
                    g_fbViews.GameViewCount(), g_fbLinked, (g_fbViews.FeetFromSoldier() - fs0) / secs,
                    (g_fbViews.FeetFromCamera() - fc0) / secs, (g_fbViews.Builds() - bu0) / secs, (g_fbViews.RootSeen() - ro0) / secs, (g_fbViews.FacesSeen() - fa0) / secs, (g_fbBinds - bi0) / secs,
                    (g_fbPictures - pi0) / secs, seen.c_str(), g_fbViews.Rho() * 57.29578);
                ro0 = g_fbViews.RootSeen(); fa0 = g_fbViews.FacesSeen(); bu0 = g_fbViews.Builds(); bi0 = g_fbBinds; pi0 = g_fbPictures;
                fs0 = g_fbViews.FeetFromSoldier(); fc0 = g_fbViews.FeetFromCamera();
            }
            if (g_cfg.engine == vxcap::kEngineSims4)
            {
                static uint64_t jo0, dr0, re0, pi0;
                std::string seen;
                for (int v = 0; v < vxcap::kTs4Faces; v++)
                {
                    const uint64_t at = g_ts4.DrawnAt(v);
                    seen += at && g_ts4.Frame() - at <= 4 ? (char)('0' + v) : '-';
                }
                Log("sims4: render jobs %.1f/s, faces drawn %.1f/s, their binds turned to the face's targets %.1f/s, faults %llu | faces drawn lately %s | decoded %.1f faces/s | targets %s | rig yaw %.1f deg",
                    (g_ts4.Jobs() - jo0) / secs, (g_ts4.Drawn() - dr0) / secs, (g_ts4.Redirects() - re0) / secs,
                    (unsigned long long)g_ts4.Faults(), seen.c_str(), (g_ts4Pictures - pi0) / secs,
                    g_ts4Set ? "made" : "not yet", g_ts4.Rho() * 57.29578);
                jo0 = g_ts4.Jobs(); dr0 = g_ts4.Drawn(); re0 = g_ts4.Redirects(); pi0 = g_ts4Pictures;
            }
            if (g_cfg.engine == vxcap::kEngineCryEngine2)
            {
                static uint64_t dr0, co0, pi0, nd0, bu0, fp0, fc0;
                const float* f = g_ce2.LastFeet();
                Log("cryengine2: %s | faces drawn %.1f/s, copied %.1f/s, decoded %.1f/s | no depth target %llu, textures busy %llu, faults %llu | the feet from the player %.1f/s, from the camera %.1f/s: (%.2f %.2f %.2f), the camera %.2f m above | rig yaw %.1f deg",
                    g_ce2Hooked ? "hooked" : "not hooked", (g_ce2.Drawn() - dr0) / secs, (g_ce2Copied - co0) / secs, (g_ce2Pictures - pi0) / secs,
                    (unsigned long long)(g_ce2NoDepth - nd0), (unsigned long long)(g_ce2Busy - bu0), (unsigned long long)g_ce2.Faults(),
                    (g_ce2.FeetFromPlayer() - fp0) / secs, (g_ce2.NoFeet() - fc0) / secs, f[0], f[1], f[2], g_ce2.EyeAboveFeet(), g_ce2.Rho() * 57.29578);
                dr0 = g_ce2.Drawn(); co0 = g_ce2Copied; pi0 = g_ce2Pictures; nd0 = g_ce2NoDepth; bu0 = g_ce2Busy;
                fp0 = g_ce2.FeetFromPlayer(); fc0 = g_ce2.NoFeet();
            }
            runs0 = g_runs; vols0 = g_volumes; sent0 = g_sent; pres0 = g_presents;
            g_presentMs = 0;
            g_statAt = now;
        }
    }
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        if (!reshade::register_addon(mod)) return FALSE;
        GetModuleFileNameA(nullptr, g_dir, MAX_PATH);   // the GAME's exe: the settings live beside it
        if (char* s = strrchr(g_dir, '\\')) *s = 0;
        char path[MAX_PATH];
        sprintf_s(path, "%s\\voxon_capture_ue.log", g_dir);
        // _fsopen with _SH_DENYNO, not fopen_s: fopen_s opens files unshared, so nothing can read the
        // log while the game runs -- which is exactly when it is wanted.
        g_log = _fsopen(path, "w", _SH_DENYNO);
        Log("voxon_capture_ue loaded; settings from %s\\voxon_capture_ue.ini", g_dir);
        ReloadSettings(true);
        reshade::register_event<reshade::addon_event::init_resource>(&on_init_resource);
        reshade::register_event<reshade::addon_event::destroy_resource>(&on_destroy_resource);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&on_bind_render_targets);
        reshade::register_event<reshade::addon_event::clear_render_target_view>(&on_clear_render_target_view);  // FROSTBITE
        reshade::register_event<reshade::addon_event::begin_render_pass>(&on_begin_render_pass);
        reshade::register_event<reshade::addon_event::destroy_device>(&on_destroy_device);
        reshade::register_event<reshade::addon_event::copy_resource>(&on_copy_resource);
        reshade::register_event<reshade::addon_event::copy_texture_region>(&on_copy_texture_region);
        reshade::register_event<reshade::addon_event::resolve_texture_region>(&on_resolve_texture_region);
        reshade::register_event<reshade::addon_event::present>(&on_present);
        reshade::register_event<reshade::addon_event::push_descriptors>(&on_push_descriptors);              // REDENGINE3
        reshade::register_event<reshade::addon_event::barrier>(&on_barrier);
        reshade::register_event<reshade::addon_event::init_swapchain>(&on_init_swapchain);               // REDENGINE3
        reshade::register_event<reshade::addon_event::draw>(&on_draw);
        reshade::register_event<reshade::addon_event::draw_indexed>(&on_draw_indexed);
        break;
    }
    case DLL_PROCESS_DETACH:
        reshade::unregister_event<reshade::addon_event::init_resource>(&on_init_resource);
        reshade::unregister_event<reshade::addon_event::destroy_resource>(&on_destroy_resource);
        reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&on_bind_render_targets);
        reshade::unregister_event<reshade::addon_event::clear_render_target_view>(&on_clear_render_target_view);
        reshade::unregister_event<reshade::addon_event::begin_render_pass>(&on_begin_render_pass);
        reshade::unregister_event<reshade::addon_event::destroy_device>(&on_destroy_device);
        reshade::unregister_event<reshade::addon_event::copy_resource>(&on_copy_resource);
        reshade::unregister_event<reshade::addon_event::copy_texture_region>(&on_copy_texture_region);
        reshade::unregister_event<reshade::addon_event::resolve_texture_region>(&on_resolve_texture_region);
        reshade::unregister_event<reshade::addon_event::present>(&on_present);
        reshade::unregister_event<reshade::addon_event::push_descriptors>(&on_push_descriptors);
        reshade::unregister_event<reshade::addon_event::barrier>(&on_barrier);
        reshade::unregister_event<reshade::addon_event::init_swapchain>(&on_init_swapchain);
        reshade::unregister_event<reshade::addon_event::draw>(&on_draw);
        reshade::unregister_event<reshade::addon_event::draw_indexed>(&on_draw_indexed);
        g_r3Native.Uninstall();
        g_ts4.Uninstall();
        g_ce2.Uninstall();
        g_link.Stop();
        reshade::unregister_addon(mod);
        {
            std::lock_guard<std::mutex> lk(g_wMtx);
            g_wQuit = true;
        }
        g_wCv.notify_one();
        // Not joined: this is DllMain, where waiting on a thread can deadlock (the Tiny Glade note).
        if (g_worker.joinable()) g_worker.detach();
        if (g_log) fclose(g_log);
        break;
    }
    return TRUE;
}
