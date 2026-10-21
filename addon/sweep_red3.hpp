// sweep_red3.hpp - REDENGINE3 (rig/redengine3, The Witcher 3): the cube swept by the game's own camera.
//
// REDengine 3 has no way to render a second camera, so the rig's script (rig/redengine3/voxonRig.ws) stands the
// engine's free camera on each face of the box in turn, one face a frame and then one frame of the player's own, with
// the world's near plane on that face and its far plane set to a code for the face (kRed3FarCode + kRed3FarStep x face).
// The camera of every frame reaches the GPU in a constant buffer, which Red3ReadCamera reads: a face frame is known by
// its far plane and placed by its own camera, so nothing has to be timed. The add-on copies a face frame's colour and
// scene depth at present, keeps the latest of each face, and decodes them together round the latest face's centre
// (Red3FaceConstants). The depth buffer is reversed - 1 at the near plane, the sky at 0 - though the projection in the
// block runs forward (measured: read forward, the top face put the ground 20 m below where the side faces did).
//
// Axes. The world is REDengine's: metres, right-handed, Z up. The display's frame (+X right, +Y toward the viewer,
// +Z down) is the world with Y and Z turned over, D(w) = (x, -y, -z): two turns, so nothing is mirrored. Unreal's
// left-handed world goes over with Z alone (FrameFor), to the same display. The rig's yaw rho is an angle in the
// display's frame, as the compact pass's turn is: the rig's +X axis is D-1 of (cos rho, sin rho).
#pragma once

#include <cmath>

#include "capture_gpu.hpp"

namespace vxcap
{
    // A frame's camera, as REDengine hands it to the GPU.
    struct Red3Camera
    {
        float pos[3], right[3], up[3], fwd[3];   // world: the camera's place, and its image's right, up and forward
        float sx, sy;                            // the projection's 1 / tan of the half fields of view, across and up
        float nearM, farM;
    };

    // The scene's camera block in The Witcher 3 5.0 (measured 2026-10-13, bound at root parameter 5 for the scene's
    // draws): camera-to-world rows - right, up, forward, position - at byte 400, the projection (row-major, w = z,
    // A + B / z, 0 at the near plane: the depth buffer holds 1 minus that) at 464, and (-1, 1, near, far) at 608.
    constexpr size_t kRed3CameraBytes = 624;
    constexpr float kRed3FarCode = 5000.0f, kRed3FarStep = 100.0f;   // a face frame's far plane: code + step x face
    constexpr float kRed3NearShare = 0.98f;                          // the near plane: this much of the way to the face

    // The block, or false when these bytes are not one: a perspective projection where it should be, the near and far
    // after it agreeing with it, an orthonormal camera.
    inline bool Red3ReadCamera(const float* f, size_t count, Red3Camera& cam)
    {
        if (count < kRed3CameraBytes / 4) return false;
        const float* P = f + 464 / 4;
        const float* C = f + 400 / 4;
        const float* nf = f + 608 / 4;
        auto is = [](float a, float b, float eps) { return std::fabs(a - b) <= eps; };
        if (!(P[0] > 0.05f && P[0] < 20.f && P[5] > 0.05f && P[5] < 20.f)) return false;
        if (!is(P[11], 1, 1e-4f) || !is(P[15], 0, 1e-4f) || !is(P[1], 0, 1e-4f) || !is(P[4], 0, 1e-4f)) return false;
        if (!is(nf[0], -1, 1e-4f) || !is(nf[1], 1, 1e-4f)) return false;
        const float n = nf[2], fa = nf[3];
        if (!(n > 0.001f && fa > n) || std::fabs(P[10]) < 1e-6f || !is(-P[14] / P[10], n, n * 1e-3f + 1e-4f)) return false;
        for (int i = 0; i < 3; i++) { cam.right[i] = C[i]; cam.up[i] = C[4 + i]; cam.fwd[i] = C[8 + i]; cam.pos[i] = C[12 + i]; }
        auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        if (!is(dot(cam.right, cam.right), 1, 1e-3f) || !is(dot(cam.up, cam.up), 1, 1e-3f) || !is(dot(cam.fwd, cam.fwd), 1, 1e-3f)
            || !is(dot(cam.right, cam.fwd), 0, 1e-3f) || !is(dot(cam.up, cam.fwd), 0, 1e-3f)) return false;
        cam.sx = P[0]; cam.sy = P[5]; cam.nearM = n; cam.farM = fa;
        return true;
    }

    // Which face a frame was drawn for, by its far plane; -1 for the player's own frame.
    inline int Red3FaceOf(const Red3Camera& cam)
    {
        const float k = (cam.farM - kRed3FarCode) / kRed3FarStep;
        const long ki = std::lround(k);
        if (ki < 0 || ki > 4 || std::fabs(k - (float)ki) > 0.01f || cam.nearM < 0.05f) return -1;
        return (int)ki;
    }

    // Each face's half-extent along its camera's look: faces 0 and 2 look along the rig's X, 1 and 3 along Y, 4 down.
    inline float Red3Along(int face, const Geometry& g) { return face >= 4 ? g.ez : (face % 2 == 0 ? g.ex : g.ey); }

    // The rig's yaw a face frame was drawn with (radians, the display's frame): side face k stands on the rig's axis
    // k x 90 degrees round from +X and looks back along it; the top one's image right is the rig's -X.
    inline double Red3RigYaw(int face, const Red3Camera& cam)
    {
        if (face >= 4) return std::atan2((double)cam.right[1], -(double)cam.right[0]);
        return std::atan2((double)cam.fwd[1], -(double)cam.fwd[0]) - face * 1.5707963267948966;
    }

    // The volume's centre a face frame was drawn round (world): the script stood its camera kRed3NearShare of the way
    // from the near plane to the box's face, so the camera is near / share + along in front of it.
    inline void Red3Centre(int face, const Red3Camera& cam, const Geometry& g, float out[3])
    {
        const float d = cam.nearM / kRed3NearShare + Red3Along(face, g);
        for (int i = 0; i < 3; i++) out[i] = cam.pos[i] + cam.fwd[i] * d;
    }

    // A world vector in the rig's frame of yaw rho: D, then the yaw turned back.
    inline void Red3ToRig(const float w[3], double rho, float out[3])
    {
        const double x = w[0], y = -(double)w[1], z = -(double)w[2], c = std::cos(rho), s = std::sin(rho);
        out[0] = (float)(x * c + y * s); out[1] = (float)(-x * s + y * c); out[2] = (float)z;
    }

    // The pack pass's constants for one face frame (w x h, every step-th texel), placed round the centre and rig yaw
    // of the latest face.
    inline PackConstants Red3FaceConstants(int face, const Red3Camera& cam, const float centre[3], double rho,
                                           const Settings& c, const Geometry& g, int w, int h, int step, float exposure)
    {
        PackConstants p{};
        p.FaceIndex = (uint32_t)face;
        p.FaceRes = (uint32_t)(w / step);
        p.ActiveH = (uint32_t)(h / step);
        p.SrcStep = (uint32_t)step;
        p.MeterEnable = 1;
        p.Exposure = exposure;
        p.Perspective = 1;
        p.DepthFromBuffer = 1;
        p.DisplayColour = 1;
        p.DepthNear = cam.nearM; p.DepthFar = cam.farM;
        p.TanH = 1.0f / cam.sx; p.TanV = 1.0f / cam.sy;
        Red3ToRig(cam.fwd, rho, p.Fwd);
        Red3ToRig(cam.right, rho, p.Right);
        Red3ToRig(cam.up, rho, p.Up);
        const float rel[3] = { cam.pos[0] - centre[0], cam.pos[1] - centre[1], cam.pos[2] - centre[2] };
        Red3ToRig(rel, rho, p.CamRel);
        p.CamD = std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
        p.QuantX = g.ex * 1.05f; p.QuantY = g.ey * 1.05f; p.QuantZ = g.ez * 1.05f;
        p.EmptyCutoff = (p.CamD + 2.0f * Reach(g, face)) * 1.5f;
        p.Lighting = 1; p.SurfaceGain = 1;
        p.ColorFloor = c.colorFloor / 255.0f; p.ColorCeiling = c.colorCeiling / 255.0f;
        ApplyLook(p, c);
        return p;
    }

    // Where the script stands the camera for a face (rig/redengine3/voxonRig.ws, MoveFace, term for term): the rig's
    // box round centre, turned by rho, seen through a lens of tanH x tanV. Each face is the one the app names, as for
    // Unreal and Unity (rig/unreal/rig.lua, FaceTable; capture_gpu.hpp, CameraGizmo): side face 0 stands on the rig's +X
    // side, 1 on +Y, 2 on -X, 3 on -Y, looking in, back past the centre; it stands back from its face until the face
    // fits the picture. A side camera tilted (radians) is raised and looks down at the centre by that much, as
    // Unreal's do (capture_gpu.hpp, Derive): the box then reaches along * cos + ez * sin along its look, and along * sin
    // + ez * cos up its picture. The top one looks straight down, its image's top toward the display's front (the rig's
    // +Y). For the self-test, which draws the faces this way, and as the scripts' reference (the Witcher 3 script's
    // swept camera stays level: tilt 0).
    inline Red3Camera Red3Place(int face, const float centre[3], double rho, const Geometry& g, float tanH, float tanV,
                                float tilt = 0.0f)
    {
        Red3Camera cam{};
        const double cr = std::cos(rho), sr = std::sin(rho);
        const float ax[3] = { (float)cr, (float)-sr, 0 }, ay[3] = { (float)-sr, (float)-cr, 0 };   // D-1 of the rig's X, Y
        float look[3], s, dist;
        if (face < 4)
        {
            const float* a = face % 2 == 0 ? ax : ay;
            const float sign = face < 2 ? -1.0f : 1.0f;   // faces 0 and 1 on the + side, looking toward -
            for (int i = 0; i < 3; i++) look[i] = a[i] * sign;   // level
            const float ct = std::cos(tilt), st = std::sin(tilt), along = Red3Along(face, g);
            const float across = face % 2 == 0 ? g.ey : g.ex;
            s = std::max(across / tanH, (along * st + g.ez * ct) / tanV);
            dist = along * ct + g.ez * st + s;
            for (int i = 0; i < 3; i++)
            {
                cam.fwd[i] = look[i] * ct + (i == 2 ? -st : 0.0f);   // tilted down
                cam.up[i] = look[i] * st + (i == 2 ? ct : 0.0f);     // the picture's top tips toward the look
                cam.pos[i] = centre[i] - cam.fwd[i] * dist;
            }
            cam.right[0] = look[1]; cam.right[1] = -look[0]; cam.right[2] = 0;   // the level look turned clockwise
        }
        else
        {
            s = std::max(g.ex / tanH, g.ey / tanV);
            dist = g.ez + s;
            for (int i = 0; i < 3; i++) { cam.right[i] = -ax[i]; cam.up[i] = ay[i]; cam.pos[i] = centre[i]; }
            cam.pos[2] += dist;
            cam.fwd[0] = 0; cam.fwd[1] = 0; cam.fwd[2] = -1;
        }
        cam.sx = 1.0f / tanH; cam.sy = 1.0f / tanV;
        cam.nearM = s * kRed3NearShare;
        cam.farM = kRed3FarCode + kRed3FarStep * face;
        return cam;
    }
}
