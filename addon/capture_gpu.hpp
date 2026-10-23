// capture_gpu.hpp - the GPU half of the re-hosted VLED capture (docs/ARCHITECTURE.md).
//
// Plain D3D11 and nothing else, so the ReShade add-on (voxon_capture_ue.cpp) and the no-game
// self-test (selftest_gpu.cpp) run EXACTLY the same code. The two compute passes are Voxel
// Photonics' (shaders/*.hlsl, see PORTED.md): pack encodes each face's lit colour + linear depth,
// compact turns every pixel into a voxel relative to the volume centre, in world metres, with
// display +Z = physically down. Only the voxels come back, through a ring of staging copies mapped
// without waiting -- a frame late, never a stall (the plugin's FRHIGPUTextureReadback ring, here).
//
// The camera half (rig.lua) and this file derive the same geometry from the same settings file.
// Derive() below and geometry() in rig.lua MUST agree exactly: the pack shader decodes depth as
// (depth - CamDist), so a stand-off that differs between the two is a whole-cloud offset that
// looks like a calibration error.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "obj/pack_cs.h"      // g_packCS     - fxc /T cs_5_0, from shaders/pack.hlsl
#include "obj/pack_depth_cs.h" // g_packDepthCS - the same, /D DEPTH_BUFFER: depth from its own texture (Unity)
#include "obj/pack_frostbite_cs.h" // g_packFrostbiteCS - DEPTH_BUFFER and /D FROSTBITE: the player by where they stand
#include "obj/pack_surface_cs.h" // g_packSurfaceCS - the same, /D SURFACE_COLOUR: the surfaces' colours mixed in
#include "obj/compact_cs.h"   // g_compactCS  - fxc /T cs_5_0, from shaders/compact.hlsl

namespace vxcap
{
    // ---- settings: voxon_capture_ue.ini, the SAME keys rig.lua reads ---------------------------
    struct Settings
    {
        int   on = 1;
        float radiusM = 12.0f, feetZ = 1.5f, boundR = 4.0f, boundZ = 2.0f;
        float feetX = 0.0f, feetY = 0.0f;   // where the feet stand in the display (REDENGINE3: the script places the volume)
        bool  faceOn[6] = { true, true, false, false, true, false };
        int   faceRes = 512;
        float tiltDeg = 30.0f;
        int   hz = 30;
        float camDist = 1.0f;       // camera stand-off, as a multiple of the volume's edge: 1 cuts the level away
        float yawDeg = 0.0f;        // the whole rig turned about the vertical (the panel's Rig rotation)
        bool  turnWithPlayer = false;  // the rig also turns with the player's view, and the display with it (DisplayTurnDeg)
        float lighting = 1.0f;         // 1: the lit capture alone; below, the surface-colour capture with this much light
        float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;   // world along the rig's X, Y and up, as multiples of radiusM's
        int   colorFloor = 0, colorCeiling = 255;            // the brightness 0 and 255 are drawn as (the app's Colour range)
        char  pipe[64] = "voxon_n64";
        float exposureComp = 0.0f;
        // LOOK: the world's gamma and saturation (1, 1: as captured). PLAYER LOOK (an Unreal rig): each camera also
        // captures the player alone (rig.lua, playerLook), and the player's pixels take a look of their own - stops of
        // exposure on top of the world's, a gamma, a saturation - and are drawn playerDensity voxels deep (1: a shell).
        float gamma = 1.0f, saturation = 1.0f;
        bool  playerLook = false;
        float playerExposure = 0.0f, playerGamma = 1.0f, playerSaturation = 1.0f;
        int   playerDensity = 1;
        // SOLID SURFACES (an Unreal or Unity rig): a see-through material writes no depth, so a camera puts its colour on what is
        // behind it. With some chosen (solidMaterials, the app's list) or all (solidAll), each camera also captures the
        // scene's depth with those made opaque (rig.lua, the solid capture), and the decode takes its depth.
        bool  solidAll = false, solidChosen = false;
        // ...only where they are at least this opaque, 0-1 (an Unreal rig; 0: wherever the opaque stand-in was drawn):
        // each camera also captures them as the game draws them (rig.lua, the glow capture), its alpha the light they let
        // through. A lightsaber's mesh is wider than its glowing core, which its material fades out toward the edge.
        float solidOpacity = 0.0f;
        // The automatic exposure's range, in stops from the picture as captured (the app's Auto-exposure lowest and
        // highest): it never darkens by more than the first or brightens by more than the second. 0 and 0: off.
        float exposureMinEV = -10.0f, exposureMaxEV = 8.0f;
        bool  showCameras = false;     // debug: a spoke from the volume's centre toward each camera that is on
        float mergePerUnit = 56.0f;    // one point per display voxel at this many a unit (VX2-XL: 448 across); 0 = off
        int   perspective = 1;         // 1: perspective lenses (UE4 drops dynamic lights orthographic); 0: the plugin's ortho
        char  controlFile[260] = "";   // the app's control file: its Cameras tab (the ue* keys) wins over this ini
        char  hostGame[32] = "ascent"; // ...when the file says game= this
        int   engine = 0;              // 0 Unreal (rig/unreal), 1 Unity (rig/unity), 2 REDengine 3 (rig/redengine3),
                                       // 3 REDengine 4 (rig/redengine4), 4 Frostbite (no rig module: frostbite_views.hpp),
                                       // 5 The Sims 4 (no rig module: sims4_views.hpp)
        float chaseAhead = 2.0f;       // FROSTBITE: the player stands this far ahead of the game's camera, level (m)...
        float chaseLeft = 0.0f;        // ...this far to its left (an over-the-shoulder camera)...
        float chaseDown = 1.5f;        // ...and this far below it
        int   ce2Flags = 0x307;        // DIAG: CRYENGINE2 faces' RenderWorld flags
        int   ce2Diag = 0;             // DIAG: CRYENGINE2 per-frame brightness and sharpness
        int   ce2Dump = 0;             // DIAG: CRYENGINE2 a change dumps one frame's pictures
        int   red3Native = 1;          // REDENGINE3: 1 the faces rendered as extra views (red3_native.hpp); 0 the camera swept
    };

    constexpr int kEngineUnreal = 0, kEngineUnity = 1, kEngineRed3 = 2, kEngineRed4 = 3, kEngineFrostbite = 4, kEngineSims4 = 5,
                  kEngineCryEngine2 = 6, kEngineEgo = 7;

    // key=value lines, ';' comments, surrounding blanks trimmed.
    template <class Apply>
    inline void ParseKeyValues(const std::string& raw, Apply apply)
    {
        size_t pos = 0;
        while (pos < raw.size())
        {
            size_t end = raw.find_first_of("\r\n", pos);
            if (end == std::string::npos) end = raw.size();
            std::string line = raw.substr(pos, end - pos);
            pos = end + 1;
            const size_t semi = line.find(';');
            if (semi != std::string::npos) line.resize(semi);
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            auto trim = [](std::string v) {
                const size_t a = v.find_first_not_of(" \t"), b = v.find_last_not_of(" \t");
                return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
            };
            apply(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
        }
    }

    inline void SetFaces(Settings& c, const std::string& v)
    {
        for (bool& b : c.faceOn) b = false;
        for (char ch : v) if (ch >= '0' && ch <= '5') c.faceOn[ch - '0'] = true;
    }

    // Shared for reading, writing and replacing: the app replaces its control file many times a second while the rig's
    // scale keys are held, and a reader that held it closed to that would make it miss a step.
    inline bool ReadFile(const char* path, std::string& out)
    {
        if (!path[0]) return false;
        HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        out.clear();
        char buf[4096];
        DWORD n = 0;
        while (::ReadFile(f, buf, sizeof buf, &n, nullptr) && n > 0) out.append(buf, n);
        CloseHandle(f);
        return true;
    }

    // The settings' text without the rig's scale (radiusM, ueRadius): what tells a change of scale alone, which the
    // scale keys make many times a second, from any other.
    inline std::string WithoutScale(const std::string& raw)
    {
        std::string out;
        size_t pos = 0;
        while (pos < raw.size())
        {
            size_t end = raw.find('\n', pos);
            if (end == std::string::npos) end = raw.size();
            size_t k = raw.find_first_not_of(" \t", pos);
            const bool scale = k < end && (raw.compare(k, 7, "radiusM") == 0 || raw.compare(k, 8, "ueRadius") == 0);
            if (!scale) out.append(raw, pos, end - pos);
            pos = end + 1;
        }
        return out;
    }

    // The ini, then -- when it names one and the app is on this game -- the app's control file over it, so the
    // panel's cameras, resolution and rig win. rig.lua does the same, key for key. `raw` is both files, so a change
    // to either is seen.
    inline bool LoadSettings(const char* path, Settings& s, std::string& raw)
    {
        if (!ReadFile(path, raw)) return false;
        Settings c;
        ParseKeyValues(raw, [&c](const std::string& k, const std::string& v) {
            if (k == "on") c.on = atoi(v.c_str());
            else if (k == "radiusM") c.radiusM = (float)atof(v.c_str());
            else if (k == "feetZ") c.feetZ = (float)atof(v.c_str());
            else if (k == "feetX") c.feetX = (float)atof(v.c_str());
            else if (k == "feetY") c.feetY = (float)atof(v.c_str());
            else if (k == "boundR") c.boundR = (float)atof(v.c_str());
            else if (k == "boundZ") c.boundZ = (float)atof(v.c_str());
            else if (k == "faceRes") c.faceRes = atoi(v.c_str());
            else if (k == "tiltDeg") c.tiltDeg = (float)atof(v.c_str());
            else if (k == "hz") c.hz = atoi(v.c_str());
            else if (k == "camDist") c.camDist = (float)atof(v.c_str());
            else if (k == "yawDeg") c.yawDeg = (float)atof(v.c_str());
            else if (k == "turnWithPlayer") c.turnWithPlayer = atoi(v.c_str()) != 0;
            else if (k == "lighting") c.lighting = (float)atof(v.c_str());
            else if (k == "scaleX") c.scaleX = (float)atof(v.c_str());
            else if (k == "scaleY") c.scaleY = (float)atof(v.c_str());
            else if (k == "scaleZ") c.scaleZ = (float)atof(v.c_str());
            else if (k == "exposureComp") c.exposureComp = (float)atof(v.c_str());
            else if (k == "gamma") c.gamma = (float)atof(v.c_str());
            else if (k == "saturation") c.saturation = (float)atof(v.c_str());
            else if (k == "playerLook") c.playerLook = atoi(v.c_str()) != 0;
            else if (k == "playerExposure") c.playerExposure = (float)atof(v.c_str());
            else if (k == "playerGamma") c.playerGamma = (float)atof(v.c_str());
            else if (k == "playerSaturation") c.playerSaturation = (float)atof(v.c_str());
            else if (k == "playerDensity") c.playerDensity = atoi(v.c_str());
            else if (k == "solidAll") c.solidAll = atoi(v.c_str()) != 0;
            else if (k == "solidMaterials") c.solidChosen = !v.empty();
            else if (k == "solidOpacity") c.solidOpacity = (float)atof(v.c_str());
            else if (k == "exposureMin") c.exposureMinEV = (float)atof(v.c_str());
            else if (k == "exposureMax") c.exposureMaxEV = (float)atof(v.c_str());
            else if (k == "colorFloor") c.colorFloor = atoi(v.c_str());
            else if (k == "colorCeiling") c.colorCeiling = atoi(v.c_str());
            else if (k == "pipe") strncpy_s(c.pipe, v.c_str(), _TRUNCATE);
            else if (k == "controlFile") strncpy_s(c.controlFile, v.c_str(), _TRUNCATE);
            else if (k == "hostGame") strncpy_s(c.hostGame, v.c_str(), _TRUNCATE);
            else if (k == "faces") SetFaces(c, v);
            else if (k == "showCameras") c.showCameras = atoi(v.c_str()) != 0;
            else if (k == "mergePerUnit") c.mergePerUnit = (float)atof(v.c_str());
            else if (k == "perspective") c.perspective = atoi(v.c_str());
            else if (k == "engine") c.engine = v == "unity" ? kEngineUnity : v == "redengine3" ? kEngineRed3
                                             : v == "redengine4" ? kEngineRed4 : v == "frostbite" ? kEngineFrostbite
                                             : v == "sims4" ? kEngineSims4 : v == "cryengine2" ? kEngineCryEngine2
                                             : v == "ego" ? kEngineEgo : kEngineUnreal;
            else if (k == "chaseAhead") c.chaseAhead = (float)atof(v.c_str());
            else if (k == "chaseLeft") c.chaseLeft = (float)atof(v.c_str());
            else if (k == "chaseDown") c.chaseDown = (float)atof(v.c_str());

            else if (k == "red3Native") c.red3Native = atoi(v.c_str());
            else if (k == "ce2Flags") c.ce2Flags = (int)strtol(v.c_str(), nullptr, 0);
            else if (k == "ce2Diag") c.ce2Diag = atoi(v.c_str());
            else if (k == "ce2Dump") c.ce2Dump = atoi(v.c_str());
        });

        std::string host;
        if (ReadFile(c.controlFile, host))
        {
            bool ours = false;
            ParseKeyValues(host, [&](const std::string& k, const std::string& v) { if (k == "game") ours = v == c.hostGame; });
            if (ours)
            {
                ParseKeyValues(host, [&c](const std::string& k, const std::string& v) {
                    if (k == "ueFaces") SetFaces(c, v);
                    else if (k == "ueFaceRes") c.faceRes = (int)atof(v.c_str());
                    else if (k == "ueRadius") c.radiusM = (float)atof(v.c_str());
                    else if (k == "ueFeetX") c.feetX = (float)atof(v.c_str());
                    else if (k == "ueFeetY") c.feetY = (float)atof(v.c_str());
                    else if (k == "ueFeetZ") c.feetZ = (float)atof(v.c_str());
                    else if (k == "ueCamDist") c.camDist = (float)atof(v.c_str());
                    else if (k == "ueYaw") c.yawDeg = (float)atof(v.c_str());
                    else if (k == "ueTurn") c.turnWithPlayer = atoi(v.c_str()) != 0;
                    else if (k == "ueLighting") c.lighting = (float)atof(v.c_str());
                    else if (k == "ueScaleX") c.scaleX = (float)atof(v.c_str());
                    else if (k == "ueScaleY") c.scaleY = (float)atof(v.c_str());
                    else if (k == "ueScaleZ") c.scaleZ = (float)atof(v.c_str());
                    else if (k == "ueBoundR") c.boundR = (float)atof(v.c_str());   // the app's Display
                    else if (k == "ueBoundZ") c.boundZ = (float)atof(v.c_str());
                    else if (k == "ueExposure") c.exposureComp = (float)atof(v.c_str());
                    else if (k == "ueGamma") c.gamma = (float)atof(v.c_str());
                    else if (k == "ueSaturation") c.saturation = (float)atof(v.c_str());
                    else if (k == "uePlayerLook") c.playerLook = atoi(v.c_str()) != 0;
                    else if (k == "uePlayerExposure") c.playerExposure = (float)atof(v.c_str());
                    else if (k == "uePlayerGamma") c.playerGamma = (float)atof(v.c_str());
                    else if (k == "uePlayerSaturation") c.playerSaturation = (float)atof(v.c_str());
                    else if (k == "uePlayerDensity") c.playerDensity = atoi(v.c_str());
                    else if (k == "ueSolidAll") c.solidAll = atoi(v.c_str()) != 0;
                    else if (k == "ueSolidMaterials") c.solidChosen = !v.empty();
                    else if (k == "ueSolidOpacity") c.solidOpacity = (float)atof(v.c_str());
                    else if (k == "ueExposureMin") c.exposureMinEV = (float)atof(v.c_str());
                    else if (k == "ueExposureMax") c.exposureMaxEV = (float)atof(v.c_str());
                    else if (k == "ueColorFloor") c.colorFloor = atoi(v.c_str());
                    else if (k == "ueColorCeiling") c.colorCeiling = atoi(v.c_str());
                    else if (k == "ueTilt") c.tiltDeg = (float)atof(v.c_str());
                    else if (k == "ueShowCameras") c.showCameras = atoi(v.c_str()) != 0;
                    else if (k == "uePerspective") c.perspective = atoi(v.c_str());
                });
                raw += host;
            }
        }
        c.faceRes = (std::clamp(c.faceRes, 64, 2560) / 2) * 2;   // rig.lua's limit: six faces stay inside D3D's 16384
        c.camDist = std::clamp(c.camDist, 1.0f, 8.0f);
        c.radiusM = std::clamp(c.radiusM, 0.5f, 1000.0f);
        c.lighting = std::clamp(c.lighting, 0.0f, 1.0f);
        c.gamma = std::clamp(c.gamma, 0.2f, 5.0f); c.playerGamma = std::clamp(c.playerGamma, 0.2f, 5.0f);
        c.saturation = std::clamp(c.saturation, 0.0f, 3.0f); c.playerSaturation = std::clamp(c.playerSaturation, 0.0f, 3.0f);
        c.playerExposure = std::clamp(c.playerExposure, -8.0f, 8.0f);
        c.playerDensity = std::clamp(c.playerDensity, 1, 8);
        c.colorFloor = std::clamp(c.colorFloor, 0, 255);
        c.colorCeiling = std::clamp(c.colorCeiling, 0, 255);
        c.scaleX = std::max(c.scaleX, 0.05f); c.scaleY = std::max(c.scaleY, 0.05f); c.scaleZ = std::max(c.scaleZ, 0.05f);
        if (c.boundR <= 0) c.boundR = 4.0f;
        if (c.boundZ <= 0) c.boundZ = 2.0f;
        s = c;
        return true;
    }

    // ---- geometry: rig.lua's geometry(), term for term -----------------------------------------
    // SCALE: the cameras capture a box of half-extents ex, ey, ez along the rig's X, Y and up - the display's R, R and Z
    // times the settings' scales - and the compact pass scales each axis back into the display: above 1 more world is
    // squashed in along that axis, below 1 less is stretched out. Each side camera frames its own box: faces 0 and 2
    // look along X (through ex, across ey), 1 and 3 along Y. With every scale 1 these are the original formulas.
    // Perspective lenses only: orthographic captures keep 1.
    struct Geometry
    {
        float s;            // world metres per display unit
        float R, Z;         // the display's box in world metres (BoundR, BoundZ in the shaders), unscaled
        float ex, ey, ez;   // SCALE: the captured box's half-extents along the rig's X, Y and up (metres)
        float tilt, ct, st;
        int   sideH;        // side faces' height: half their width on a display twice as wide as tall (VX2-XL), else square
        float halfDepth[4]; // each side face: its slab's half-extent along the tilted look
        float vertHalf[4];
        float halfWin[4];   // ... the half-width its picture spans on the volume's near face
        float sideDist[4];  // ... camera stand-off (metres) = the pack's CamDist
        float topDist;      // ... for top and bottom
        // Perspective lenses: the near clip plane sits on the volume's near face, nSide / nTop in front of the
        // camera, where the lens just spans the volume's cross-section: tan(half FOV) = 1 / camDist, 90 degrees at 1.
        bool  persp;
        float nSide[4], nTop;   // near-face distances (metres)
        float tanP;         // tan of half the horizontal field of view, every face
    };

    // A side face's height: half its width on a display at least twice as wide as tall (VX2-XL, 8 x 4), square on one
    // as tall as wide (VX2, 4 x 4) - a picture the shape of what it frames.
    inline int SideHeight(const Settings& c) { return 2.0f * c.boundZ <= c.boundR * 1.001f ? c.faceRes / 2 : c.faceRes; }

    inline Geometry Derive(const Settings& c)
    {
        Geometry g{};
        g.s = c.radiusM / c.boundR;
        g.R = c.boundR * g.s;
        g.Z = c.boundZ * g.s;
        g.persp = c.perspective != 0;
        g.ex = g.R * (g.persp ? c.scaleX : 1.0f);
        g.ey = g.R * (g.persp ? c.scaleY : 1.0f);
        g.ez = g.Z * (g.persp ? c.scaleZ : 1.0f);
        g.tilt = std::clamp(c.tiltDeg, 0.0f, 90.0f) * 3.14159265358979f / 180.0f;
        g.ct = std::cos(g.tilt);
        g.st = std::sin(g.tilt);
        g.sideH = SideHeight(c);
        const float k = std::max(1.0f, c.camDist);
        if (g.persp) g.tanP = 1.0f / k;
        for (int v = 0; v < 4; v++)
        {
            const float along = (v % 2 == 0) ? g.ex : g.ey, across = (v % 2 == 0) ? g.ey : g.ex;
            g.halfDepth[v] = along * g.ct + g.ez * g.st;
            g.vertHalf[v] = along * g.st + g.ez * g.ct;
            g.halfWin[v] = std::max(across, g.vertHalf[v] * (float)c.faceRes / (float)g.sideH);
            g.sideDist[v] = g.halfDepth[v] * c.camDist;
            if (g.persp)
            {
                g.nSide[v] = g.halfWin[v] * k;
                g.sideDist[v] = g.halfDepth[v] + g.nSide[v];
            }
        }
        g.topDist = g.ez * c.camDist;
        if (g.persp)
        {
            g.nTop = std::max(g.ex, g.ey) * k;
            g.topDist = g.ez + g.nTop;
        }
        return g;
    }

    // How far a face's camera may see past the centre and still be inside the box: its far reach, for the far plane.
    inline float Reach(const Geometry& g, int v)
    {
        const float slab = v < 4 ? g.halfDepth[v] : std::max(g.halfDepth[0], g.halfDepth[1]);
        return std::max(std::max(g.ex, g.ey), std::max(g.ez, slab));
    }

    // A face's camera in the compact pass's frame - metres from the volume's centre, display axes (+Z down), the rig's
    // yaw left out (DISPLAY turns it back). rig.lua's place() term for term with yaw 0, then Unreal's rotator -> axes
    // (FRotationMatrix, roll 0): image +x along Right, image rows down along -Up.
    struct FaceFrame { float fwd[3], right[3], up[3]; float camD, tanH, tanV; };
    inline FaceFrame FrameFor(int v, const Settings& c, const Geometry& g)
    {
        static const double L[4][2] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };
        const double rad = 3.14159265358979 / 180.0;
        double pitch, yaw;
        FaceFrame f{};
        if (v >= 4)
        {
            const double s = v == 4 ? 1 : -1;
            pitch = -90 * s * rad; yaw = 90 * s * rad;
            f.camD = g.topDist; f.tanH = f.tanV = g.tanP;
        }
        else
        {
            const double lx = L[v][0] * g.ct, ly = L[v][1] * g.ct, lz = -g.st;
            pitch = std::asin(lz); yaw = std::atan2(ly, lx);
            f.camD = g.sideDist[v]; f.tanH = g.tanP; f.tanV = g.tanP * (float)g.sideH / (float)c.faceRes;   // as tall as the picture
        }
        const double cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
        const double F[3] = { cp * cy, cp * sy, sp }, R[3] = { -sy, cy, 0 }, U[3] = { -sp * cy, -sp * sy, cp };
        for (int i = 0; i < 3; i++)
        {
            const double flip = i == 2 ? -1.0 : 1.0;   // Unreal's up is display -Z
            f.fwd[i] = (float)(F[i] * flip); f.right[i] = (float)(R[i] * flip); f.up[i] = (float)(U[i] * flip);
        }
        return f;
    }

    // The plugin's FaceTable / CamAxisSign (VledRenderVolume.cpp), copied not re-derived.
    static const uint32_t kFlipX[6] = { 1, 0, 0, 1, 1, 0 };
    static const uint32_t kFlipY[6] = { 1, 1, 1, 1, 1, 1 };
    static const float kDepthSign[6] = { +1.f, +1.f, -1.f, -1.f, -1.f, +1.f };
    static const float kCamAxisSign[6] = { +1.f, +1.f, -1.f, -1.f, +1.f, -1.f };

    inline int FaceHeight(int face, const Settings& c) { return face >= 4 ? c.faceRes : SideHeight(c); }

    // ---- constant buffers: the layouts of the cbuffers in shaders/*.hlsl ----------------------
    struct PackConstants
    {
        uint32_t FaceIndex, FaceRes, ActiveH, MeterEnable;
        float CamAxisPos, LookSign, Bound, DepthSign;
        float D_min, D_scale, CamDist, HalfDepth;
        float EmptyCutoff, Exposure;
        uint32_t FlipX, FlipY;
        uint32_t Perspective;
        float CamD, TanH, TanV;
        float Fwd[3], QuantX;       // SCALE: the packed position's range along each of the rig's axes
        float Right[3], QuantZ;
        float Up[3], QuantY;
        // UNITY (rig/unity): depth from the camera's own depth buffer, which is reversed-Z device depth, turned back
        // into distance with the near and far planes the rig set (DepthPlanes); and rows stored bottom-up, as Unity
        // renders into render textures on D3D11.
        uint32_t DepthFromBuffer, BottomUp;
        float DepthNear, DepthFar;
        // SURFACE COLOUR: how much of the lit colour goes over the surfaces' own, and their gain (the app's Brightness).
        float Lighting, SurfaceGain;
        // COLOUR RANGE: the brightness 0 and 1 are drawn as, 0-1.
        float ColorFloor, ColorCeiling;
        // REDENGINE3: the camera's place from the volume's centre (-CamD x Fwd on a face's axis); the colour's kind, the
        // texel step (pack.hlsl). REDENGINE4: the picture's own size when it is not FaceRes.
        float CamRel[3];
        uint32_t DisplayColour, SrcStep, SrcSize;
        // PLAYER LOOK: bit 0, the face's player capture is read (t3) and its pixels take PlayerGain on the exposure and
        // the player's gamma and saturation. SOLID SURFACES: bit 1, the depth comes from the face's solid capture (t4);
        // bit 2, only where their glow capture (t5) says they are at least SolidOpacity opaque. LOOK: gamma and
        // saturation kept so that 0 leaves the colour alone - a block made without them changes nothing: an InvGamma of 0
        // is taken for 1, and the saturations are offsets from 1.
        uint32_t PlayerMask;
        float PlayerGain;
        float InvGamma, SatDelta, PlayerInvGamma, PlayerSatDelta;
        float SolidOpacity;
        uint32_t SrcDepthScaled;   // FROSTBITE: the depth read at the picture's nearest texel too (SrcSize)
        uint32_t DepthLinear;      // CRYENGINE2: the depth is linear, view depth over DepthFar (pack.hlsl)
        float Pad2;
    };
    static_assert(sizeof(PackConstants) == 224, "PackConstants must match the HLSL cbuffer");

    // FROSTBITE PLAYER LOOK, the pack pass's own (PlayerMask bit 3; b1, read only by the FROSTBITE build of pack.hlsl - the
    // D3D12 root signature has no room for more in b0, and Frostbite here is D3D11): the player is what stands within
    // radius of their feet (at: metres from the volume's centre, the pack pass's frame, +Z down) and up to height above.
    struct PlayerShape
    {
        float at[3];
        float radius;
        float height;
        float pad[3];
    };
    static_assert(sizeof(PlayerShape) == 32, "PlayerShape must match the HLSL cbuffer");

    // LOOK and PLAYER LOOK, into a face's pack constants: every way of making them ends here.
    inline void ApplyLook(PackConstants& p, const Settings& c)
    {
        p.InvGamma = 1.0f / std::max(0.2f, c.gamma);
        p.SatDelta = c.saturation - 1.0f;
        p.PlayerGain = std::pow(2.0f, c.playerExposure);
        p.PlayerInvGamma = 1.0f / std::max(0.2f, c.playerGamma);
        p.PlayerSatDelta = c.playerSaturation - 1.0f;
    }

    // The surfaces' own colours are wanted: lighting below 1, and an Unreal rig (rig/unity makes no second capture).
    inline bool UseSurface(const Settings& c) { return c.engine == kEngineUnreal && c.lighting < 0.999f; }

    // The player's own capture is wanted: Player look on, and an Unreal rig (the only one that makes it).
    inline bool UsePlayer(const Settings& c) { return (c.engine == kEngineUnreal || c.engine == kEngineUnity) && c.playerLook; }

    // The solid capture is wanted: see-through materials chosen to be made opaque, or all of them; an Unreal or Unity rig.
    inline bool UseSolid(const Settings& c) { return (c.engine == kEngineUnreal || c.engine == kEngineUnity) && (c.solidAll || c.solidChosen); }

    // ...and their glow capture, to keep them solid only where opaque enough: an Unreal rig (the only one that makes it).
    inline bool UseGlow(const Settings& c) { return c.engine == kEngineUnreal && UseSolid(c) && c.solidOpacity > 0.0f; }

    struct CompactConstants
    {
        uint32_t FaceRes;
        float BoundR, BoundZ;
        uint32_t VX2XL;
        uint32_t MaxVoxels;
        float KeepFraction, SideTiltRad;
        uint32_t MergeGrid;
        float DispXY, DispZ, YawCos, YawSin;
        float MergePer, MergeOffXY, MergeOffZ;
        uint32_t MergeGridZ;
        uint32_t Perspective;
        float QuantX, QuantZ, QuantY;
        float ScaleX, ScaleY, ScaleZ;   // SCALE: each axis back into the display's box (R / ex, R / ey, Z / ez)
        uint32_t ActiveRows;            // REDENGINE3: every face this many rows (a swept game frame); 0: the display's shape
        // PLAYER LOOK: each of the player's voxels drawn PlayerDepth deep (1: as it is), a display cell (CellStep display
        // units) at a time along its face's line of sight (PlayerDir, display units), into the player's body.
        uint32_t PlayerDepth;
        float CellStep;
        uint32_t PadC0, PadC1;
        float PlayerDir[6][4];
    };
    static_assert(sizeof(CompactConstants) == 208, "CompactConstants must match the HLSL cbuffer");

    // The merge's bit grid: one bit per display cell, 2048 words a row. Made for 64 cells a unit over a VX2-XL's box
    // (513 x 513 x 257 bits, 8.5 MB) and grown when a finer grid is asked for - a denser display than today's, tried
    // ahead of having one. Up to kMergeMaxRows (128 a unit on a VX2-XL, 67 MB); finer than that runs unmerged.
    constexpr int kMergeW = 2048, kMergeRows = 1032, kMergeMaxRows = 8212;

    // The compact output's capacity: every pixel of the six faces (the sides are half height on a VX2-XL, so 4 faceRes^2),
    // but never more than this - 128 MB on the GPU and as much in each readback slot. With the merge on, a volume holds
    // at most one voxel per display cell a surface crosses, far below it; with it off at the largest faces, the excess
    // is dropped by the compact pass and reported as cut short.
    constexpr uint32_t kMaxVoxels = 8u << 20;

    inline int CompactRows(int faceRes, int width)
    {
        const uint64_t want = std::min<uint64_t>(4ull * faceRes * faceRes, kMaxVoxels);
        return (int)((want + width - 1) / width);
    }

    // Rows of the merge grid a constant set needs (0: no merge).
    inline int MergeRows(uint32_t grid, uint32_t gridZ)
    {
        if (!grid) return 0;
        const uint64_t words = ((uint64_t)grid * grid * gridZ + 31) / 32;
        return (int)((words + kMergeW - 1) / kMergeW);
    }

    // How far the compact pass turns the rig's frame to give the display's, in degrees. The rig's cameras stand turned
    // by yawDeg, and with turnWithPlayer by the player's view as well - which only the rig knows, frame by frame.
    // Turned back by yawDeg alone, the world keeps its own orientation (off), or turns with the player's view (on),
    // whose forward then lands on display -Y: away from a viewer at the front, as display +Y is toward them (GameKit's
    // WorldRotates, the same convention). rig.lua and rig/unity place the volume's centre by the same angle.
    inline float DisplayTurnDeg(const Settings& c) { return c.yawDeg - (c.turnWithPlayer ? 90.0f : 0.0f); }

    // The compact pass's constants. Its output is in DISPLAY units: x, y turned back by the rig's yaw and scaled by
    // boundR / R, z by boundZ / Z - the plugin's PushFrame scale, which for one scale s is 1/s on every axis. The merge's
    // cells are mergePerUnit a display unit, counted from the box's edge.
    inline CompactConstants CompactFor(const Settings& c, const Geometry& g, uint32_t capacity, float keep)
    {
        CompactConstants q{};
        q.FaceRes = (uint32_t)c.faceRes;
        q.BoundR = g.R;
        q.BoundZ = g.Z;
        q.VX2XL = g.sideH < c.faceRes ? 1u : 0u;   // the side faces are faceRes x faceRes/2 (a VX2-XL's shape)
        q.MaxVoxels = capacity;
        q.KeepFraction = keep;
        q.SideTiltRad = g.tilt;
        // The compact pass works in the RIG's frame; a turned rig is turned back into the world's here. Display x, y
        // are the world's x, y (no mirror: Unreal's map already flips chirality once), so it is the same rotation.
        const float a = DisplayTurnDeg(c) * 3.14159265358979f / 180.0f;
        q.DispXY = c.boundR / g.R; q.DispZ = c.boundZ / g.Z;
        q.YawCos = std::cos(a); q.YawSin = std::sin(a);
        const float per = c.mergePerUnit;
        if (per > 0)
        {
            const uint32_t nxy = (uint32_t)std::ceil(2 * c.boundR * per) + 1, nz = (uint32_t)std::ceil(2 * c.boundZ * per) + 1;
            if (MergeRows(nxy, nz) <= kMergeMaxRows)
            {
                q.MergeGrid = nxy; q.MergeGridZ = nz;
                q.MergePer = per; q.MergeOffXY = c.boundR * per; q.MergeOffZ = c.boundZ * per;
            }
        }
        q.Perspective = g.persp ? 1u : 0u;
        q.QuantX = g.ex * 1.05f; q.QuantY = g.ey * 1.05f; q.QuantZ = g.ez * 1.05f;   // the compact pass's own 5% margin
        q.ScaleX = g.R / g.ex; q.ScaleY = g.R / g.ey; q.ScaleZ = g.Z / g.ez;
        q.PlayerDepth = UsePlayer(c) || (c.engine == kEngineFrostbite && c.playerLook) ? (uint32_t)c.playerDensity : 1u;
        q.CellStep = 1.0f / (per > 0 ? per : 56.0f);
        if (q.PlayerDepth > 1)
            for (int v = 0; v < 6; v++)
            {
                // The face's look, taken through the compact pass's own map: each axis's scale, the turn, display units.
                const FaceFrame f = FrameFor(v, c, g);
                const float sx = f.fwd[0] * q.ScaleX, sy = f.fwd[1] * q.ScaleY, sz = f.fwd[2] * q.ScaleZ;
                float d[3] = { (sx * q.YawCos - sy * q.YawSin) * q.DispXY, (sx * q.YawSin + sy * q.YawCos) * q.DispXY, sz * q.DispZ };
                const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                for (int i = 0; i < 3; i++) q.PlayerDir[v][i] = len > 0 ? d[i] / len : 0.0f;
            }
        return q;
    }

    // Rows of the compact output to copy back: the last volume's count and a quarter more, never the whole capacity -
    // which at six faces of 1024 px is 100 MB a volume. A volume that outgrows it is cut short once; the next is sized
    // from its full count.
    inline int RowsToCopy(bool haveCount, uint32_t lastCount, int rows, int width)
    {
        if (!haveCount) return rows;
        const uint64_t want = (uint64_t)lastCount + lastCount / 4 + 65536;
        return (int)std::min<uint64_t>((uint64_t)rows, (want + width - 1) / width);
    }

    // A Unity face camera's near and far clip planes, in metres. rig/unity sets its cameras from this same rule
    // (Geometry.cs, DepthPlanes), and the decode turns their depth buffer back into distance with it: the two must agree.
    // Near: the volume's near face less 2%, as rig.lua's lens; orthographic, just in front of the camera. Far: past the
    // volume's far side with a margin, so anything beyond reads as empty.
    inline void DepthPlanes(int v, const Settings& c, const Geometry& g, float& nearM, float& farM)
    {
        const FaceFrame f = FrameFor(v, c, g);
        const float camD = g.persp ? f.camD : (v >= 4 ? g.topDist : g.sideDist[v]);
        nearM = g.persp ? (v >= 4 ? g.nTop : g.nSide[v]) * 0.98f : 0.05f;
        farM = (camD + 2.0f * Reach(g, v)) * 1.5f;
    }

    // The plugin's DispatchPackCS, one face. The one change: AxisDist is the camera's real
    // stand-off (camDist x the volume's edge), not a fixed 4 x Bound.
    inline PackConstants FaceConstants(int v, const Settings& c, const Geometry& g, float exposure, bool meter)
    {
        const bool zAxis = v >= 4;
        const float bound = zAxis ? g.Z : g.R;
        const float axisDist = zAxis ? g.topDist : g.sideDist[v];
        const float camAxisPos = kCamAxisSign[v] * axisDist;
        float depthBound = bound;
        if (!zAxis && g.tilt > 0.0001f) depthBound = g.R * g.ct + g.Z * g.st;

        PackConstants p{};
        p.FaceIndex = (uint32_t)v;
        p.FaceRes = (uint32_t)c.faceRes;
        p.ActiveH = (uint32_t)FaceHeight(v, c);
        p.MeterEnable = meter ? 1u : 0u;
        p.CamAxisPos = camAxisPos;
        p.LookSign = camAxisPos > 0.f ? 1.f : -1.f;
        p.Bound = bound;
        p.DepthSign = kDepthSign[v];
        p.D_min = -depthBound;
        p.D_scale = 65535.0f / (2.0f * depthBound);
        p.CamDist = axisDist;
        p.HalfDepth = depthBound;
        p.EmptyCutoff = std::max(g.R, g.Z) * 8.0f;
        p.Exposure = exposure;
        p.FlipX = kFlipX[v];
        p.FlipY = kFlipY[v];
        p.Lighting = c.lighting;
        p.SurfaceGain = std::pow(2.0f, c.exposureComp);
        p.ColorFloor = c.colorFloor / 255.0f;
        p.ColorCeiling = c.colorCeiling / 255.0f;
        if (g.persp)
        {
            const FaceFrame f = FrameFor(v, c, g);
            p.Perspective = 1;
            p.CamD = f.camD; p.TanH = f.tanH; p.TanV = f.tanV;
            for (int i = 0; i < 3; i++) { p.Fwd[i] = f.fwd[i]; p.Right[i] = f.right[i]; p.Up[i] = f.up[i]; p.CamRel[i] = -f.camD * f.fwd[i]; }
            p.QuantX = g.ex * 1.05f; p.QuantY = g.ey * 1.05f; p.QuantZ = g.ez * 1.05f;
            // The far side of the volume from this camera, and a margin: sky and anything beyond are empty.
            p.EmptyCutoff = (f.camD + 2.0f * Reach(g, v)) * 1.5f;
        }
        if (c.engine == kEngineUnity)
        {
            p.DepthFromBuffer = 1;
            p.BottomUp = 1;
            DepthPlanes(v, c, g, p.DepthNear, p.DepthFar);
            p.EmptyCutoff = p.DepthFar * 0.999f;   // the far plane is the cleared depth: empty
        }
        ApplyLook(p, c);
        return p;
    }

    struct Voxel { float x, y, z; uint32_t rgb; };   // the compact's float4: display units + asfloat(0x00RRGGBB)

    // ---- the decoder ---------------------------------------------------------------------------
    class Decoder
    {
    public:
        static constexpr int kRing = 3;
        static constexpr int kCompactW = 2048;

        bool Init(ID3D11Device* dev, std::string& err)
        {
            Release();
            _dev = dev;
            if (FAILED(dev->CreateComputeShader(g_packCS, sizeof(g_packCS), nullptr, &_packCS))) { err = "pack shader"; return false; }
            if (FAILED(dev->CreateComputeShader(g_packDepthCS, sizeof(g_packDepthCS), nullptr, &_packDepthCS))) { err = "pack shader (depth buffer)"; return false; }
            if (FAILED(dev->CreateComputeShader(g_packFrostbiteCS, sizeof(g_packFrostbiteCS), nullptr, &_packFrostbiteCS))) { err = "pack shader (Frostbite)"; return false; }
            if (FAILED(dev->CreateComputeShader(g_packSurfaceCS, sizeof(g_packSurfaceCS), nullptr, &_packSurfaceCS))) { err = "pack shader (surface colour)"; return false; }
            if (FAILED(dev->CreateComputeShader(g_compactCS, sizeof(g_compactCS), nullptr, &_compactCS))) { err = "compact shader"; return false; }
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = sizeof(PackConstants);
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (FAILED(dev->CreateBuffer(&bd, nullptr, &_packCB))) { err = "pack cb"; return false; }
            bd.ByteWidth = sizeof(CompactConstants);
            if (FAILED(dev->CreateBuffer(&bd, nullptr, &_compactCB))) { err = "compact cb"; return false; }
            bd.ByteWidth = sizeof(PlayerShape);
            if (FAILED(dev->CreateBuffer(&bd, nullptr, &_shapeCB))) { err = "player shape cb"; return false; }
            // 4x1 R32_UINT: [0] voxel count, [1] sum of fixed-point log2 luminance, [2] samples.
            if (!Tex(4, 1, DXGI_FORMAT_R32_UINT, D3D11_BIND_UNORDERED_ACCESS, &_counter, err)) return false;
            if (FAILED(dev->CreateUnorderedAccessView(_counter, nullptr, &_counterUAV))) { err = "counter uav"; return false; }
            for (auto& s : _ring)
                if (!Staging(4, 1, DXGI_FORMAT_R32_UINT, &s.counter, err)) return false;
            return MergeGrid(kMergeRows, err);
        }

        // The merge grid, at least this many rows: made at Init, remade larger when a finer grid is asked for.
        bool MergeGrid(int rows, std::string& err)
        {
            if (rows <= _mergeRows && _occupied) return true;
            SafeRelease(_occupiedUAV); SafeRelease(_occupied); _mergeRows = 0;
            if (!Tex(kMergeW, rows, DXGI_FORMAT_R32_UINT, D3D11_BIND_UNORDERED_ACCESS, &_occupied, err)) return false;
            if (FAILED(_dev->CreateUnorderedAccessView(_occupied, nullptr, &_occupiedUAV))) { err = "merge uav"; return false; }
            _mergeRows = rows;
            return true;
        }

        // Sized by face resolution; called again whenever it changes.
        bool Configure(int faceRes, std::string& err)
        {
            if (faceRes == _faceRes && _pack) return true;
            ReleaseSized();
            _faceRes = faceRes;
            // The plugin's pack texture: all six face columns, colour over depth.
            if (!Tex(6 * faceRes, 2 * faceRes, DXGI_FORMAT_R32_UINT,
                     D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, &_pack, err)) return false;
            if (FAILED(_dev->CreateUnorderedAccessView(_pack, nullptr, &_packUAV))) { err = "pack uav"; return false; }
            if (FAILED(_dev->CreateShaderResourceView(_pack, nullptr, &_packSRV))) { err = "pack srv"; return false; }
            // Capacity: every pixel of the faces could be a voxel, up to kMaxVoxels; the compact pass never writes past it.
            _rows = CompactRows(faceRes, kCompactW);
            if (!Tex(kCompactW, _rows, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS, &_compact, err)) return false;
            if (FAILED(_dev->CreateUnorderedAccessView(_compact, nullptr, &_compactUAV))) { err = "compact uav"; return false; }
            for (auto& s : _ring)
                if (!Staging(kCompactW, _rows, DXGI_FORMAT_R32G32B32A32_FLOAT, &s.compact, err)) return false;
            return true;
        }

        uint32_t Capacity() const { return (uint32_t)(kCompactW * _rows); }

        // FROSTBITE: faces drawn by the game's own cameras, as Decoder12's Frames - each face's depth (its view reading
        // the depth plane), with its pack constants made by the caller from the camera it was drawn with
        // (frostbite_views.hpp), every face activeRows tall. The faces' colour views are faceSRV.
        struct Frames
        {
            ID3D11ShaderResourceView* depth[6] = {};
            PackConstants pack[6] = {};
            int activeRows = 0;
            PlayerShape player{};       // FROSTBITE PLAYER LOOK: read where a face's PlayerMask has bit 3
        };

        // Pack each face whose SRV is given, compact, and copy into the next free ring slot.
        // The caller's compute state is saved and restored: the game's renderer caches what it
        // bound and must find it unchanged. surfaceSRV: each face's surface-colour capture, used when every face given
        // has one and UseSurface; otherwise the lit colour alone, as before.
        bool Run(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* const faceSRV[6], const Settings& c,
                 const Geometry& g, float exposure, float keep, ID3D11ShaderResourceView* const depthSRV[6] = nullptr,
                 ID3D11ShaderResourceView* const surfaceSRV[6] = nullptr, ID3D11ShaderResourceView* const playerSRV[6] = nullptr,
                 ID3D11ShaderResourceView* const solidSRV[6] = nullptr, ID3D11ShaderResourceView* const glowSRV[6] = nullptr,
                 const Frames* frames = nullptr)
        {
            Slot* slot = nullptr;
            for (auto& s : _ring) if (s.state == 0) { slot = &s; break; }
            if (!slot) { ++dropped; return false; }      // the CPU has not caught up: skip, never wait

            Saved sv; Save(ctx, sv);

            const UINT zero[4] = { 0, 0, 0, 0 };
            ctx->ClearUnorderedAccessViewUint(_packUAV, zero);
            ctx->ClearUnorderedAccessViewUint(_counterUAV, zero);

            const bool depthTex = c.engine == kEngineUnity || frames;   // Unity, Frostbite: depth from a buffer of its own (t1)
            bool surface = UseSurface(c) && surfaceSRV && !frames;      // the surfaces' colours (t2), when every face has them
            for (int v = 0; v < 6; v++) if (surface && c.faceOn[v] && faceSRV[v] && !surfaceSRV[v]) surface = false;
            bool player = UsePlayer(c) && playerSRV && !frames; // PLAYER LOOK: the player's captures (t3), every face's
            for (int v = 0; v < 6; v++) if (player && c.faceOn[v] && faceSRV[v] && !playerSRV[v]) player = false;
            bool solid = UseSolid(c) && solidSRV && !frames;    // SOLID SURFACES: the solid captures (t4), every face's
            for (int v = 0; v < 6; v++) if (solid && c.faceOn[v] && faceSRV[v] && !solidSRV[v]) solid = false;
            bool glow = solid && UseGlow(c) && glowSRV;         // ...only where opaque enough: their glow captures (t5)
            for (int v = 0; v < 6; v++) if (glow && c.faceOn[v] && faceSRV[v] && !glowSRV[v]) glow = false;
            ctx->CSSetShader(frames ? _packFrostbiteCS : depthTex ? _packDepthCS : surface ? _packSurfaceCS : _packCS, nullptr, 0);
            if (frames)
            {
                ctx->UpdateSubresource(_shapeCB, 0, nullptr, &frames->player, 0, 0);
                ctx->CSSetConstantBuffers(1, 1, &_shapeCB);
            }
            ID3D11UnorderedAccessView* packUAVs[2] = { _packUAV, _counterUAV };
            const UINT keepCounts[2] = { (UINT)-1, (UINT)-1 };
            for (int v = 0; v < 6; v++)
            {
                if (!c.faceOn[v] || !faceSRV[v]) continue;
                ID3D11ShaderResourceView* const depthIn = frames ? frames->depth[v] : (depthSRV ? depthSRV[v] : nullptr);
                if (depthTex && !depthIn) continue;
                PackConstants p = frames ? frames->pack[v] : FaceConstants(v, c, g, exposure, true);
                if (!frames) p.PlayerMask = (player ? 1u : 0u) | (solid ? 2u : 0u) | (glow ? 4u : 0u);
                p.SolidOpacity = c.solidOpacity;
                ctx->UpdateSubresource(_packCB, 0, nullptr, &p, 0, 0);
                ctx->CSSetConstantBuffers(0, 1, &_packCB);
                ID3D11ShaderResourceView* in[6] = { faceSRV[v], depthTex ? depthIn : nullptr, surface ? surfaceSRV[v] : nullptr,
                                                    player ? playerSRV[v] : nullptr, solid ? solidSRV[v] : nullptr,
                                                    glow ? glowSRV[v] : nullptr };
                ctx->CSSetShaderResources(0, 6, in);
                ctx->CSSetUnorderedAccessViews(0, 2, packUAVs, keepCounts);
                ctx->Dispatch((p.FaceRes + 7) / 8, (p.ActiveH + 7) / 8, 1);
            }
            // Unbind before the pack texture is read as an SRV: D3D11 refuses a resource bound as
            // both, and silently nulls the input.
            ID3D11UnorderedAccessView* nullUAV[2] = { nullptr, nullptr };
            ID3D11ShaderResourceView* nullSRV[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
            ctx->CSSetUnorderedAccessViews(0, 2, nullUAV, keepCounts);
            ctx->CSSetShaderResources(0, 6, nullSRV);

            CompactConstants q = CompactFor(c, g, Capacity(), keep);
            if (frames) q.ActiveRows = (uint32_t)frames->activeRows;
            std::string gridErr;
            if (q.MergeGrid && !MergeGrid(MergeRows(q.MergeGrid, q.MergeGridZ), gridErr)) q.MergeGrid = 0;   // unmerged, not stopped
            if (q.MergeGrid) ctx->ClearUnorderedAccessViewUint(_occupiedUAV, zero);
            ctx->UpdateSubresource(_compactCB, 0, nullptr, &q, 0, 0);
            ctx->CSSetShader(_compactCS, nullptr, 0);
            ctx->CSSetConstantBuffers(0, 1, &_compactCB);
            ctx->CSSetShaderResources(0, 1, &_packSRV);
            ID3D11UnorderedAccessView* compactUAVs[3] = { _compactUAV, _counterUAV, _occupiedUAV };
            const UINT keepCounts3[3] = { (UINT)-1, (UINT)-1, (UINT)-1 };
            ID3D11UnorderedAccessView* nullUAV3[3] = { nullptr, nullptr, nullptr };
            ctx->CSSetUnorderedAccessViews(0, 3, compactUAVs, keepCounts3);
            ctx->Dispatch((6 * c.faceRes + 7) / 8, (c.faceRes + 7) / 8, 1);
            ctx->CSSetUnorderedAccessViews(0, 3, nullUAV3, keepCounts3);
            ctx->CSSetShaderResources(0, 1, nullSRV);

            ctx->CopyResource(slot->counter, _counter);
            slot->rows = RowsToCopy(_haveCount, _lastCount, _rows, kCompactW);
            const D3D11_BOX box{ 0, 0, 0, (UINT)kCompactW, (UINT)slot->rows, 1 };
            ctx->CopySubresourceRegion(slot->compact, 0, 0, 0, 0, _compact, 0, &box);
            slot->state = 1;
            slot->seq = ++_seq;

            Restore(ctx, sv);
            return true;
        }

        // The oldest copy, if the GPU has finished it -- never waits. Returns the voxel count, or
        // -1 when nothing is ready. meanLum is the metered geometric-mean luminance (0: none).
        // merged: how many the compact pass merged into a cell another face had already lit.
        int Poll(ID3D11DeviceContext* ctx, std::vector<Voxel>& out, float& meanLum, uint32_t& merged)
        {
            Slot* slot = nullptr;
            for (auto& s : _ring)
                if (s.state == 1 && (!slot || s.seq < slot->seq)) slot = &s;
            if (!slot) return -1;

            D3D11_MAPPED_SUBRESOURCE m{};
            const HRESULT hr = ctx->Map(slot->counter, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return -1;
            if (FAILED(hr)) { slot->state = 0; return -1; }
            const uint32_t* ctr = (const uint32_t*)m.pData;
            _lastCount = std::min(ctr[0], Capacity()); _haveCount = true;
            const uint32_t count = std::min(_lastCount, (uint32_t)(slot->rows * kCompactW));
            if (count < _lastCount) ++truncated;
            meanLum = ctr[2] ? std::pow(2.0f, (float)ctr[1] / (float)ctr[2] / 64.0f - 16.0f) : 0.0f;
            merged = ctr[3];
            ctx->Unmap(slot->counter, 0);

            out.resize(count);
            if (count)
            {
                // Same submission as the counter, so finished too; DO_NOT_WAIT keeps that honest.
                if (FAILED(ctx->Map(slot->compact, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m)))
                    return -1;
                for (uint32_t base = 0; base < count; base += kCompactW)
                {
                    const uint32_t n = std::min<uint32_t>(kCompactW, count - base);
                    memcpy(&out[base], (const uint8_t*)m.pData + (size_t)(base / kCompactW) * m.RowPitch, n * sizeof(Voxel));
                }
                ctx->Unmap(slot->compact, 0);
            }
            slot->state = 0;
            return (int)count;
        }

        void Release()
        {
            ReleaseSized();
            for (auto& s : _ring) SafeRelease(s.counter);
            SafeRelease(_packCS); SafeRelease(_packDepthCS); SafeRelease(_packSurfaceCS); SafeRelease(_packFrostbiteCS);
            SafeRelease(_compactCS);
            SafeRelease(_packCB); SafeRelease(_compactCB); SafeRelease(_shapeCB);
            SafeRelease(_counterUAV); SafeRelease(_counter);
            SafeRelease(_occupiedUAV); SafeRelease(_occupied); _mergeRows = 0;
            _dev = nullptr; _haveCount = false;
        }

        ~Decoder() { Release(); }

        uint64_t dropped = 0, truncated = 0;

    private:
        struct Slot { ID3D11Texture2D* compact = nullptr; ID3D11Texture2D* counter = nullptr; int state = 0, rows = 0; uint64_t seq = 0; };
        struct Saved
        {
            ID3D11ComputeShader* cs = nullptr;   // UE4 uses no class instances, so none are saved
            ID3D11Buffer* cb[2] = {}; ID3D11ShaderResourceView* srv[6] = {}; ID3D11UnorderedAccessView* uav[2] = {};
        };

        ID3D11Device* _dev = nullptr;
        ID3D11ComputeShader* _packCS = nullptr; ID3D11ComputeShader* _packDepthCS = nullptr; ID3D11ComputeShader* _compactCS = nullptr;
        ID3D11ComputeShader* _packSurfaceCS = nullptr; ID3D11ComputeShader* _packFrostbiteCS = nullptr;
        ID3D11Buffer* _packCB = nullptr; ID3D11Buffer* _compactCB = nullptr; ID3D11Buffer* _shapeCB = nullptr;
        ID3D11Texture2D* _counter = nullptr; ID3D11UnorderedAccessView* _counterUAV = nullptr;
        ID3D11Texture2D* _pack = nullptr; ID3D11UnorderedAccessView* _packUAV = nullptr; ID3D11ShaderResourceView* _packSRV = nullptr;
        ID3D11Texture2D* _compact = nullptr; ID3D11UnorderedAccessView* _compactUAV = nullptr;
        ID3D11Texture2D* _occupied = nullptr; ID3D11UnorderedAccessView* _occupiedUAV = nullptr;
        int _mergeRows = 0;
        uint32_t _lastCount = 0; bool _haveCount = false;
        Slot _ring[kRing];
        int _faceRes = 0, _rows = 0;
        uint64_t _seq = 0;

        template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

        void ReleaseSized()
        {
            for (auto& s : _ring) { SafeRelease(s.compact); s.state = 0; }
            SafeRelease(_packUAV); SafeRelease(_packSRV); SafeRelease(_pack);
            SafeRelease(_compactUAV); SafeRelease(_compact);
            _faceRes = 0; _haveCount = false;
        }

        bool Tex(int w, int h, DXGI_FORMAT f, UINT bind, ID3D11Texture2D** out, std::string& err)
        {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = (UINT)w; d.Height = (UINT)h; d.MipLevels = 1; d.ArraySize = 1; d.Format = f;
            d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = bind;
            if (FAILED(_dev->CreateTexture2D(&d, nullptr, out))) { err = "texture " + std::to_string(w) + "x" + std::to_string(h); return false; }
            return true;
        }

        bool Staging(int w, int h, DXGI_FORMAT f, ID3D11Texture2D** out, std::string& err)
        {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = (UINT)w; d.Height = (UINT)h; d.MipLevels = 1; d.ArraySize = 1; d.Format = f;
            d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(_dev->CreateTexture2D(&d, nullptr, out))) { err = "staging " + std::to_string(w) + "x" + std::to_string(h); return false; }
            return true;
        }

        static void Save(ID3D11DeviceContext* ctx, Saved& s)
        {
            ctx->CSGetShader(&s.cs, nullptr, nullptr);
            ctx->CSGetConstantBuffers(0, 2, s.cb);    // b0, and b1 (the Frostbite pack's player shape)
            ctx->CSGetShaderResources(0, 6, s.srv);   // t0-t5: the pack pass binds all six
            ctx->CSGetUnorderedAccessViews(0, 2, s.uav);
        }

        static void Restore(ID3D11DeviceContext* ctx, Saved& s)
        {
            const UINT keepCounts[2] = { (UINT)-1, (UINT)-1 };
            ctx->CSSetShader(s.cs, nullptr, 0);
            ctx->CSSetConstantBuffers(0, 2, s.cb);
            ctx->CSSetShaderResources(0, 6, s.srv);
            ctx->CSSetUnorderedAccessViews(0, 2, s.uav, keepCounts);
            SafeRelease(s.cs); SafeRelease(s.cb[0]); SafeRelease(s.cb[1]); for (auto& v : s.srv) SafeRelease(v);
            SafeRelease(s.uav[0]); SafeRelease(s.uav[1]);
        }
    };

    // DEBUG: where each camera that is on stands and looks (asked 2026-10-10: "a debug toggle that allows us to
    // visualize where each camera that is enabled is angled"). In display units, the volume's centre at the origin,
    // +Z down: a spoke from the centre toward each camera, ending at the volume's edge in a square standing across
    // its line of sight with a tick toward its image's top -- so the rig's yaw, the side cameras' tilt and which faces
    // are on can all be read off the display. Colour by face, the same as the panel's order.
    struct GizmoPoint { float x, y, z; uint8_t r, g, b; };

    inline void CameraGizmo(const Settings& c, std::vector<GizmoPoint>& out)
    {
        static const uint8_t col[6][3] = { { 255, 60, 60 }, { 60, 255, 60 }, { 255, 60, 255 },
                                           { 60, 255, 255 }, { 120, 140, 255 }, { 255, 230, 60 } };
        const float step = 1.0f / 40.0f;
        const float a = DisplayTurnDeg(c) * 3.14159265358979f / 180.0f, ca = std::cos(a), sa = std::sin(a);
        // The Witcher 3's swept camera looks level whatever the tilt (rig/redengine3/voxonRig.ws).
        const float tiltDeg = (c.engine == kEngineRed3 && !c.red3Native) ? 0.0f : c.tiltDeg;
        const float t = std::clamp(tiltDeg, 0.0f, 90.0f) * 3.14159265358979f / 180.0f, ct = std::cos(t), st = std::sin(t);
        static const float L[4][2] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };
        auto put = [&](float x, float y, float z, int f) {
            for (int k = -1; k <= 1; k++)   // a plus-shaped section: one voxel wide is a faint trail on this display
            {
                out.push_back({ x + k * step, y, z, col[f][0], col[f][1], col[f][2] });
                out.push_back({ x, y + k * step, z, col[f][0], col[f][1], col[f][2] });
                out.push_back({ x, y, z + k * step, col[f][0], col[f][1], col[f][2] });
            }
        };
        for (int f = 0; f < 6; f++)
        {
            if (!c.faceOn[f]) continue;
            // u: from the centre toward the camera. v: the camera image's up. Display +Z is down, so "up" is -Z.
            float ux, uy, uz, vx, vy, vz;
            if (f < 4)
            {
                const float bx = L[f][0] * ca - L[f][1] * sa, by = L[f][0] * sa + L[f][1] * ca;   // base look, turned
                ux = -bx * ct; uy = -by * ct; uz = -st;            // the camera is above, looking down at the centre
                vx = bx * st;  vy = by * st;  vz = -ct;           // image up: toward the look as it tilts
            }
            else
            {
                const float s = f == 4 ? 1.0f : -1.0f;
                ux = 0; uy = 0; uz = -s;
                vx = -sa; vy = ca; vz = 0;                         // image up along the turned +Y
            }
            // To the volume's edge: the cylinder's side or its cap, whichever comes first, a little inside.
            const float hxy = std::sqrt(ux * ux + uy * uy);
            float reach = 0.95f * std::min(hxy > 1e-4f ? c.boundR / hxy : 1e9f,
                                           std::fabs(uz) > 1e-4f ? c.boundZ / std::fabs(uz) : 1e9f);
            for (float d = 0; d <= reach; d += step) put(ux * d, uy * d, uz * d, f);
            // The square across the line of sight at the end, with its right axis r = v x u.
            const float rx = vy * uz - vz * uy, ry = vz * ux - vx * uz, rz = vx * uy - vy * ux;
            const float cx = ux * reach, cy = uy * reach, cz = uz * reach, h = 0.35f;
            for (float e = -h; e <= h; e += step)
            {
                put(cx + rx * e + vx * h, cy + ry * e + vy * h, cz + rz * e + vz * h, f);
                put(cx + rx * e - vx * h, cy + ry * e - vy * h, cz + rz * e - vz * h, f);
                put(cx + vx * e + rx * h, cy + vy * e + ry * h, cz + vz * e + rz * h, f);
                put(cx + vx * e - rx * h, cy + vy * e - ry * h, cz + vz * e - rz * h, f);
            }
            for (float e = 0; e <= 0.25f; e += step)                 // the tick: which way is the image's top
                put(cx + vx * (h + e), cy + vy * (h + e), cz + vz * (h + e), f);
        }
    }

}
