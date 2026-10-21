// red4_rtt.hpp - REDENGINE4 (rig/redengine4, Cyberpunk 2077): the cube's faces rendered by the engine's own
// render-to-texture cameras, in the same frame as the player's view.
//
// The rig, a Cyber Engine Tweaks mod (rig/redengine4/voxon_rig/init.lua), gives V five entRenderToTextureCameraComponents
// and stands them where Red3Place stands The Witcher 3's faces (sweep_red3.hpp), a square lens of 90 degrees each, the
// near plane on the box's face. Each renders faceRes texels square and writes its picture into a spare render texture of
// the game's, which the rig sizes faceRes + 1 + face texels square: the add-on knows a face's picture by its size.
// Traced on 2.31 (2026-10-13/14):
//   * cameras on one entity that share a virtualCameraName - unset, all of them "Component" - all render the view of
//     the one added last (five on V: five pictures of the top face's view, every depth pass the same). The script gives
//     each its own name;
//   * the cameras render one after another, each in full, in the order they were added - and every one of them at the
//     largest camera's resolution, into the same transient buffers: one scene depth buffer, D32 S8 (R32G8X24), for all;
//   * a camera's last pass turns that depth buffer from depth-write to read, and the next command it runs on the GPU is
//     the one draw of its picture (R8G8B8A8 sRGB, left a render target after); straight after, the render graph hands the
//     buffer to the next camera. So the buffer is copied into a carry of the add-on's own each time a pass finishes
//     writing it - out of the state the game's barrier has just set, never an assumed one - and the carry into the
//     face's own copy as its picture is bound for drawing: in the GPU's order, the face's depth, complete. (Copied
//     straight out of the shared buffer at that point instead, in the state the trace showed, the GPU faulted once in
//     the main menu.) The picture is copied at present.
//   * the depth is reversed, 1 at the near plane and 0 at the far one; unset, a camera's far plane is about a kilometre
//     away (measured 2026-10-14: decoded as infinite, the top face put the ground 12 cm below where the sides did). The
//     rig sets each camera's own just past the box (Red4FarM), so the decode knows it, and nothing beyond is drawn.
// The decode then runs as REDengine 3's native views' does, in the rig's own frame: the faces' cameras are known from the
// settings alone, so nothing passes from the script but the settings (rig.txt, which this add-on writes beside it).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "sweep_red3.hpp"

namespace vxcap
{
    constexpr int kRed4Faces = 5;

    // A face's picture: this many texels square. Its depth is faceRes square.
    inline int Red4FaceSize(const Settings& c, int face) { return c.faceRes + 1 + face; }

    // The face a picture of this size is, or -1.
    inline int Red4FaceOf(const Settings& c, uint32_t w, uint32_t h)
    {
        if (w != h) return -1;
        const int k = (int)w - c.faceRes - 1;
        return k >= 0 && k < kRed4Faces ? k : -1;
    }

    // A face camera's far plane (the script sets the same, farPlaneOverride): the box's farthest corner from the camera,
    // and half as much again, whatever the tilt.
    inline float Red4FarM(float camD, const Geometry& g)
    {
        return (camD + std::sqrt(g.ex * g.ex + g.ey * g.ey + g.ez * g.ez)) * 1.5f;
    }

    // A face's camera round the volume's centre, in the rig's own frame (yaw 0): the script turns the whole rig, and the
    // decode places each face from its centre, so the world's place and yaw never reach here.
    inline Red3Camera Red4Camera(int face, const Geometry& g)
    {
        const float centre[3] = { 0, 0, 0 };
        Red3Camera cam = Red3Place(face, centre, 0.0, g, 1.0f, 1.0f, g.tilt);
        cam.farM = Red4FarM(std::sqrt(cam.pos[0] * cam.pos[0] + cam.pos[1] * cam.pos[1] + cam.pos[2] * cam.pos[2]), g);
        return cam;
    }

    // The pack pass's constants for a face: its depth read texel for texel, its picture at the nearest texel.
    inline PackConstants Red4FaceConstants(int face, const Settings& c, const Geometry& g, float exposure)
    {
        const float centre[3] = { 0, 0, 0 };
        PackConstants p = Red3FaceConstants(face, Red4Camera(face, g), centre, 0.0, c, g, c.faceRes, c.faceRes, 1, exposure);
        p.SrcSize = (uint32_t)Red4FaceSize(c, face);
        return p;
    }

    // rig.txt, for the script: on while the app is taking volumes, and the settings it places the cameras by.
    inline std::string Red4RigText(const Settings& c, bool on)
    {
        int mask = 0;
        for (int v = 0; v < kRed4Faces; v++) if (c.faceOn[v]) mask |= 1 << v;
        char s[512];
        sprintf_s(s, "on=%d\nradius=%g\nboundR=%g\nboundZ=%g\nscaleX=%g\nscaleY=%g\nscaleZ=%g\nfeetX=%g\nfeetY=%g\nfeetZ=%g\n"
                     "yaw=%g\nturn=%d\nfaces=%d\nfaceRes=%d\ntilt=%g\n",
                  on ? 1 : 0, c.radiusM, c.boundR, c.boundZ, c.scaleX, c.scaleY, c.scaleZ, c.feetX, c.feetY, c.feetZ,
                  c.yawDeg, c.turnWithPlayer ? 1 : 0, mask, c.faceRes, std::clamp(c.tiltDeg, 0.0f, 90.0f));
        return s;
    }
}
