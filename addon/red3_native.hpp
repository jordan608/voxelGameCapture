// red3_native.hpp - REDENGINE3, native views (red3Native=1, the default): the renderer's own "render this frame" hooked,
// so the world is rendered again from each face's camera in the same frame as the player's view, the way Unreal's scene
// captures are - instead of sweeping the game's one camera (sweep_red3.hpp, the fallback).
//
// The Witcher 3 5.0.0.1048522 (measured 2026-10-13, from witcher3.exe's RTTI): CRenderCommand_RenderScene::Execute
// (0x141D0C570) calls 0x141C1A210(renderer, CRenderFrame*, CRenderSceneEx*) on the render thread - the whole world
// pass of one frame. The hook is a 14-byte jump over that function's first five instructions, which are copied into a
// trampoline; it goes in only when those bytes read as expected, so another build of the game is left alone.
#pragma once

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "sweep_red3.hpp"

namespace vxcap
{
    constexpr uintptr_t kRed3RenderFrameRva = 0x1C1A210;
    // mov rax,rsp / mov [rax+10h],rdx / push rbp / push rbx / push rsi / push r12 / push r13: whole instructions, none
    // RIP-relative, so they run unchanged from the trampoline.
    constexpr uint8_t kRed3RenderFrameHead[14] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x55, 0x53, 0x56, 0x41, 0x54, 0x41, 0x55 };

    using Red3RenderFrameFn = uint64_t (*)(void* renderer, void* frame, void* scene);


    // jmp qword ptr [rip+0] followed by the address: 14 bytes, no register touched.
    inline void Red3AbsJump(uint8_t* at, const void* to)
    {
        at[0] = 0xFF; at[1] = 0x25; at[2] = at[3] = at[4] = at[5] = 0;
        const uint64_t a = (uint64_t)to;
        memcpy(at + 6, &a, 8);
    }

    inline uint8_t* Red3RenderFrameAddress() { return (uint8_t*)GetModuleHandleA(nullptr) + kRed3RenderFrameRva; }

    inline bool Red3Write(uint8_t* at, const uint8_t* bytes, size_t n)
    {
        DWORD old = 0;
        if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
        memcpy(at, bytes, n);
        VirtualProtect(at, n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, n);
        return true;
    }

    // Where a 14-byte absolute jump at p goes, or null when p does not start with one.
    inline const uint8_t* Red3JumpTarget(const uint8_t* p)
    {
        if (p[0] != 0xFF || p[1] != 0x25 || p[2] || p[3] || p[4] || p[5]) return nullptr;
        uint64_t a;
        memcpy(&a, p + 6, 8);
        return (const uint8_t*)a;
    }

    inline bool Red3IsLiveCode(const void* p)
    {
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery(p, &m, sizeof m) != sizeof m || m.State != MEM_COMMIT) return false;
        return (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    }

    // NVIDIA Streamline's frame-generation plug-in (sl.dlss_g.dll) hooks this same function while the game starts, and
    // on a GPU without frame generation unloads again WITHOUT taking its hook out: the game's first world frame then
    // jumps into freed memory (measured 2026-10-13: "sl.dlss_g.dll_unloaded" at +0x3f2f0, every launch for a while). A
    // jump at the function's head into memory that is no longer code is put back to the function's own first bytes.
    // Returns true when it had to.
    inline bool Red3HealRenderFrame()
    {
        uint8_t* target = Red3RenderFrameAddress();
        const uint8_t* to = Red3JumpTarget(target);
        if (!to || Red3IsLiveCode(to)) return false;
        return Red3Write(target, kRed3RenderFrameHead, sizeof kRed3RenderFrameHead);
    }

    // The hook: installed once the game's own device is made (not while Streamline probes the GPU: ReShade loads and
    // unloads the add-on then), taken out when the add-on unloads. Original() runs the game's function - through
    // another live hook on it, if there is one (Streamline's, on a GPU with frame generation): its jump is copied.
    class Red3RenderFrameHook
    {
    public:
        bool Install(void* detour, std::string& err)
        {
            uint8_t* target = Red3RenderFrameAddress();
            const uint8_t* other = Red3JumpTarget(target);
            if (other && !Red3IsLiveCode(other)) { err = "a dead hook is on the render-frame function"; return false; }
            if (!other && memcmp(target, kRed3RenderFrameHead, sizeof kRed3RenderFrameHead) != 0)
            { err = "the render-frame function is not where this build has it (another game version?)"; return false; }
            uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!tramp) { err = "no memory for the trampoline"; return false; }
            memcpy(tramp, target, sizeof kRed3RenderFrameHead);   // the function's first instructions, or the other jump
            if (!other) Red3AbsJump(tramp + sizeof kRed3RenderFrameHead, target + sizeof kRed3RenderFrameHead);
            FlushInstructionCache(GetCurrentProcess(), tramp, 64);
            memcpy(_was, target, sizeof _was);
            uint8_t patch[sizeof kRed3RenderFrameHead];
            Red3AbsJump(patch, detour);
            if (!Red3Write(target, patch, sizeof patch)) { err = "the render-frame function could not be made writable"; return false; }
            memcpy(_mine, patch, sizeof _mine);
            _original = (Red3RenderFrameFn)tramp;
            _chained = other != nullptr;
            return true;
        }

        // Puts back what was there, if the head is still this hook's jump.
        void Uninstall()
        {
            if (!_original) return;
            uint8_t* target = Red3RenderFrameAddress();
            if (memcmp(target, _mine, sizeof _mine) == 0) Red3Write(target, _was, sizeof _was);
            _original = nullptr;
        }

        Red3RenderFrameFn Original() const { return _original; }
        bool Chained() const { return _chained; }

    private:
        Red3RenderFrameFn _original = nullptr;
        bool _chained = false;
        uint8_t _was[sizeof kRed3RenderFrameHead] = {}, _mine[sizeof kRed3RenderFrameHead] = {};
    };

    // ---- A CRenderFrame's camera (measured 2026-10-13 on frame dumps; every matrix rebuilt from the basics to 1e-7) ----
    // The frame holds two identical camera sets, at +0x20 and +0x600 (kRed3CameraSetBytes apart). In a set (offsets from
    // its start): the position at +0x00, (roll, pitch, yaw) in degrees at +0x10, the vertical field of view at +0x1C,
    // near and far at +0x30 / +0x34, then 4x4 matrices, row vectors (p' = p M), the view's axes x right, y up, z forward
    // unless named: world to view +0x40; world to view in (right, forward, up) +0x80; view to world in (right, forward,
    // up) +0xC0; view to world +0x100; forward-depth projection's inverse +0x140; the projection +0x180 and +0x1C0;
    // world to screen +0x200 and its inverse +0x240; the rotation's rows (right, forward, up) +0x280; reversed-depth
    // world to screen +0x2C0 and +0x300; the reversed projection +0x340 and +0x380; its world to screen's inverse +0x3C0;
    // the projection again +0x440 and +0x480; four points round the camera +0x4E0 (kept where they are relative to it);
    // and the culling camera +0x540: position, angles, world to view, a forward projection with near 0.05 further out.
    constexpr size_t kRed3FrameCameraSet[2] = { 0x20, 0x600 };
    constexpr size_t kRed3CameraSetBytes = 0x5E0;
    constexpr size_t kRed3FrameCameraBytes = 0xBE0;   // frame bytes the cameras cover, saved round the extra views

    struct Red3M4 { double m[4][4]; };

    inline Red3M4 Red3Mul(const Red3M4& a, const Red3M4& b)
    {
        Red3M4 r{};
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
        { double s = 0; for (int k = 0; k < 4; k++) s += a.m[i][k] * b.m[k][j]; r.m[i][j] = s; }
        return r;
    }

    // A general 4x4 inverse (Gauss-Jordan with pivoting).
    inline Red3M4 Red3Inverse(const Red3M4& in)
    {
        double a[4][8];
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { a[i][j] = in.m[i][j]; a[i][j + 4] = i == j ? 1.0 : 0.0; }
        for (int c = 0; c < 4; c++)
        {
            int p = c;
            for (int r = c + 1; r < 4; r++) if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
            if (p != c) for (int j = 0; j < 8; j++) std::swap(a[p][j], a[c][j]);
            const double d = a[c][c] != 0 ? a[c][c] : 1e-30;
            for (int j = 0; j < 8; j++) a[c][j] /= d;
            for (int r = 0; r < 4; r++)
            {
                if (r == c) continue;
                const double f = a[r][c];
                for (int j = 0; j < 8; j++) a[r][j] -= f * a[c][j];
            }
        }
        Red3M4 r{};
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r.m[i][j] = a[i][j + 4];
        return r;
    }

    inline void Red3PutM4(uint8_t* at, const Red3M4& m)
    {
        float f[16];
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) f[i * 4 + j] = (float)m.m[i][j];
        memcpy(at, f, sizeof f);
    }

    inline void Red3PutRow(uint8_t* at, const double v[3], double w)
    {
        const float f[4] = { (float)v[0], (float)v[1], (float)v[2], (float)w };
        memcpy(at, f, sizeof f);
    }

    // Writes cam (position, axes, near, far; its lens sx x sy) into both of the frame's camera sets.
    inline void Red3WriteFrameCamera(uint8_t* frame, const Red3Camera& cam)
    {
        const double P[3] = { cam.pos[0], cam.pos[1], cam.pos[2] }, R[3] = { cam.right[0], cam.right[1], cam.right[2] },
                     U[3] = { cam.up[0], cam.up[1], cam.up[2] }, F[3] = { cam.fwd[0], cam.fwd[1], cam.fwd[2] };
        const double n = cam.nearM, f = cam.farM, sx = cam.sx, sy = cam.sy;
        auto dot = [](const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        auto w2v = [&](const double* a, const double* b, const double* c) {
            Red3M4 m{};
            for (int i = 0; i < 3; i++) { m.m[i][0] = a[i]; m.m[i][1] = b[i]; m.m[i][2] = c[i]; }
            m.m[3][0] = -dot(P, a); m.m[3][1] = -dot(P, b); m.m[3][2] = -dot(P, c); m.m[3][3] = 1;
            return m;
        };
        auto v2w = [&](const double* a, const double* b, const double* c) {
            Red3M4 m{};
            for (int i = 0; i < 3; i++) { m.m[0][i] = a[i]; m.m[1][i] = b[i]; m.m[2][i] = c[i]; m.m[3][i] = P[i]; }
            m.m[3][3] = 1;
            return m;
        };
        auto proj = [&](double a, double b) {
            Red3M4 m{};
            m.m[0][0] = sx; m.m[1][1] = sy; m.m[2][2] = a; m.m[2][3] = 1; m.m[3][2] = b;
            return m;
        };
        const Red3M4 W2V = w2v(R, U, F);
        const Red3M4 Pf = proj(f / (f - n), -n * f / (f - n)), Pr = proj(-n / (f - n), n * f / (f - n));
        const Red3M4 WSf = Red3Mul(W2V, Pf), WSr = Red3Mul(W2V, Pr);
        const double pitch = std::asin(std::max(-1.0, std::min(1.0, F[2]))) * 57.29577951308232;
        double yaw = std::atan2(R[1], R[0]) * 57.29577951308232;
        if (yaw < 0) yaw += 360;
        const float angles[3] = { 0.0f, (float)(pitch < 0 ? pitch + 360 : pitch), (float)yaw };
        for (size_t s : kRed3FrameCameraSet)
        {
            uint8_t* c = frame + s;
            // the four points, where they were relative to the old camera
            float old[16];
            memcpy(old, c + 0x40, sizeof old);   // the old world to view (x right, y up, z forward)
            Red3M4 oldW2V{};
            for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) oldW2V.m[i][j] = old[i * 4 + j];
            for (int k = 0; k < 4; k++)
            {
                float pt[4];
                memcpy(pt, c + 0x4E0 + 16 * k, sizeof pt);
                double v[3] = {};
                for (int j = 0; j < 3; j++) v[j] = pt[0] * oldW2V.m[0][j] + pt[1] * oldW2V.m[1][j] + pt[2] * oldW2V.m[2][j] + oldW2V.m[3][j];
                const double w[3] = { P[0] + v[0] * R[0] + v[1] * U[0] + v[2] * F[0], P[1] + v[0] * R[1] + v[1] * U[1] + v[2] * F[1],
                                      P[2] + v[0] * R[2] + v[1] * U[2] + v[2] * F[2] };
                Red3PutRow(c + 0x4E0 + 16 * k, w, pt[3]);
            }
            { const float p3[3] = { (float)P[0], (float)P[1], (float)P[2] }; memcpy(c, p3, sizeof p3); }
            memcpy(c + 0x10, angles, sizeof angles);
            { const float nf[2] = { (float)n, (float)f }; memcpy(c + 0x30, nf, sizeof nf); }
            Red3PutM4(c + 0x40, W2V);
            Red3PutM4(c + 0x80, w2v(R, F, U));
            Red3PutM4(c + 0xC0, v2w(R, F, U));
            Red3PutM4(c + 0x100, v2w(R, U, F));
            Red3PutM4(c + 0x140, Red3Inverse(Pf));
            Red3PutM4(c + 0x180, Pf);
            Red3PutM4(c + 0x1C0, Pf);
            Red3PutM4(c + 0x200, WSf);
            Red3PutM4(c + 0x240, Red3Inverse(WSf));
            Red3PutRow(c + 0x280, R, 0); Red3PutRow(c + 0x290, F, 0); Red3PutRow(c + 0x2A0, U, 0);
            Red3PutM4(c + 0x2C0, WSr);
            Red3PutM4(c + 0x300, WSr);
            Red3PutM4(c + 0x340, Pr);
            Red3PutM4(c + 0x380, Pr);
            Red3PutM4(c + 0x3C0, Red3Inverse(WSr));
            Red3PutM4(c + 0x440, Pf);
            Red3PutM4(c + 0x480, Pf);
            // the culling camera: its own far kept, its near 0.05 beyond the camera's
            uint8_t* o = c + 0x540;
            float oz[2];
            memcpy(oz, o + 0x60 + 0x28, 4);        // its projection's A = f / (f - n)
            memcpy(oz + 1, o + 0x60 + 0x38, 4);    // B = -n f / (f - n), so f = -B / (A - 1)
            const double fo = (oz[0] > 1.0f && oz[1] < 0.0f) ? -oz[1] / (oz[0] - 1.0) : f;
            const double nO = n + 0.05, fO = std::max(fo, nO + 1.0);
            { const float p3[3] = { (float)P[0], (float)P[1], (float)P[2] }; memcpy(o, p3, sizeof p3); }
            memcpy(o + 0x10, angles, sizeof angles);
            Red3PutM4(o + 0x20, W2V);
            const Red3M4 Po = [&] { Red3M4 m{}; m.m[0][0] = sx; m.m[1][1] = sy; m.m[2][2] = fO / (fO - nO); m.m[2][3] = 1; m.m[3][2] = -nO * fO / (fO - nO); return m; }();
            Red3PutM4(o + 0x60, Po);
        }
    }
}
