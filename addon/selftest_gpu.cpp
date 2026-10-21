// selftest_gpu.cpp - the GPU half, checked with no game, no Unreal and no host (CLAUDE.md §11: a
// console self-test, no framework). A WARP device runs the SAME compiled shaders and the SAME
// capture_gpu.hpp as the add-on, on faces rendered here exactly as Unreal renders an orthographic
// SceneCapture2D placed the way rig.lua places it: image columns along the camera's right (its
// rotation's Y axis), rows down its up (Z axis), depth = distance along its forward in cm.
//
// What it asserts, each a way this has gone wrong elsewhere (CLAUDE.md §4.5, §9):
//   * every voxel lies on a surface that is really there, to within the capture's pixel size;
//   * -Z is up: the top of a box sits above the floor in the display;
//   * nothing is mirrored: a box placed +X and -Y of the player arrives +X and -Y;
//   * nothing outside the volume;
//   * stand-offs: on the volume's edge (camDist 1) and further out (camDist 3);
//   * a turned, steeper rig (yaw 45, tilt 45), decoded in the rig's frame and turned back;
//   * all of it twice: the D3D11 decoder and the D3D12 one (capture_d3d12.hpp), on WARP each.
#include "capture_gpu.hpp"
#include "capture_d3d12.hpp"
#include "sweep_red3.hpp"
#include "red3_link.hpp"
#include "red3_native.hpp"
#include "red4_rtt.hpp"
#include "frostbite_views.hpp"
#include "sims4_views.hpp"
#include "cryengine2_views.hpp"

#include <DirectXPackedVector.h>
#include <cstdio>
#include <set>
#include <windows.h>

using namespace vxcap;

static int g_fail = 0;
static void check(bool ok, const char* what, double a = 0, double b = 0)
{
    printf("  %s %s", ok ? "pass" : "FAIL", what);
    if (a != 0 || b != 0) printf("  (%.4g, %.4g)", a, b);
    printf("\n");
    if (!ok) ++g_fail;
}

struct V3 { double x, y, z; };
static V3 add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V3 mul(V3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }

// The scene, in Unreal cm: a floor at z = 0 under the player's feet, and a box off to +X / -Y.
static const V3 kFeet = { 1000, -2000, 0 };
static const V3 kBoxMin = { 1150, -2300, 0 }, kBoxMax = { 1450, -2100, 250 };

// tmin: nothing nearer is drawn - a perspective camera's near clip plane, along this ray. floor false: the box alone
// (PLAYER LOOK: the box stands for the player, and the player capture draws nothing else). box false: the floor alone
// (SOLID SURFACES: the box see-through, writing no depth).
static bool hitScene(V3 o, V3 d, double& t, int& what, double tmin = 0, bool floor = true, bool boxOn = true)
{
    t = 1e30; what = 0;
    if (floor && d.z < -1e-9) { const double tf = (0 - o.z) / d.z; if (tf > tmin && tf < t) { t = tf; what = 1; } }
    double t0 = 0, t1 = 1e30;   // the box, by slabs; t >= 0 is the camera's near plane
    const double o3[3] = { o.x, o.y, o.z }, d3[3] = { d.x, d.y, d.z };
    const double mn[3] = { kBoxMin.x, kBoxMin.y, kBoxMin.z }, mx[3] = { kBoxMax.x, kBoxMax.y, kBoxMax.z };
    bool box = boxOn;
    for (int i = 0; i < 3 && box; i++)
    {
        if (std::fabs(d3[i]) < 1e-12) { if (o3[i] < mn[i] || o3[i] > mx[i]) box = false; continue; }
        double a = (mn[i] - o3[i]) / d3[i], b = (mx[i] - o3[i]) / d3[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a); t1 = std::min(t1, b);
        if (t0 > t1) box = false;
    }
    if (box && t0 > tmin && t0 < t) { t = t0; what = 2; }
    return what != 0;
}

// Distance (cm) from a point to the nearest real surface.
static double surfaceDistance(V3 p)
{
    double best = std::fabs(p.z);   // the floor
    const double cx = std::clamp(p.x, kBoxMin.x, kBoxMax.x), cy = std::clamp(p.y, kBoxMin.y, kBoxMax.y),
                 cz = std::clamp(p.z, kBoxMin.z, kBoxMax.z);
    const bool inside = cx == p.x && cy == p.y && cz == p.z;
    if (inside)
        best = std::min({ best, p.x - kBoxMin.x, kBoxMax.x - p.x, p.y - kBoxMin.y, kBoxMax.y - p.y, kBoxMax.z - p.z });
    else
        best = std::min(best, std::sqrt((p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) + (p.z - cz) * (p.z - cz)));
    return best;
}

struct Cam { V3 pos, fwd, right, up; double orthoW; int w, h; bool persp; double tanH, tanV, nearCm; };

// The volume's centre, as the rigs put it: the feet less feetZ display units of height, in world metres along the up
// axis (SCALE: the up axis's scale stretches it too).
static V3 CentreFor(const Settings& c, const Geometry& g)
{
    return { kFeet.x, kFeet.y, kFeet.z + c.feetZ * g.s * (g.ez / g.Z) * 100 };
}

// rig.lua's place(), term for term; then Unreal's rotator -> axes (FRotationMatrix, roll 0). headingDeg is the player's
// view, which turns the rig as well when turnWithPlayer is on.
static Cam placeFace(int face, const Settings& c, const Geometry& g, V3 centre, double headingDeg = 0)
{
    double pitch, yaw; V3 pos; double ortho;
    const double rigYaw = c.yawDeg + (c.turnWithPlayer ? headingDeg : 0);
    if (face >= 4)
    {
        const double sign = face == 4 ? 1 : -1;
        pos = { centre.x, centre.y, centre.z + sign * g.topDist * 100 };
        pitch = -90 * sign; yaw = 90 * sign + rigYaw; ortho = 2 * g.R * 100;
    }
    else
    {
        const double L[4][2] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };
        const double a = rigYaw * 3.14159265358979 / 180, ca = std::cos(a), sa = std::sin(a);
        const double bx = L[face][0] * ca - L[face][1] * sa, by = L[face][0] * sa + L[face][1] * ca;
        const double lx = bx * g.ct, ly = by * g.ct, lz = -g.st;
        pos = { centre.x - lx * g.sideDist[face] * 100, centre.y - ly * g.sideDist[face] * 100, centre.z - lz * g.sideDist[face] * 100 };
        pitch = std::asin(lz) * 180 / 3.14159265358979; yaw = std::atan2(ly, lx) * 180 / 3.14159265358979;
        ortho = 2 * g.halfWin[face] * 100;
    }
    const double P = pitch * 3.14159265358979 / 180, Y = yaw * 3.14159265358979 / 180;
    Cam cam;
    cam.pos = pos;
    cam.fwd = { std::cos(P) * std::cos(Y), std::cos(P) * std::sin(Y), std::sin(P) };
    cam.right = { -std::sin(Y), std::cos(Y), 0 };
    cam.up = { -std::sin(P) * std::cos(Y), -std::sin(P) * std::sin(Y), std::cos(P) };
    cam.orthoW = ortho;
    cam.w = c.faceRes; cam.h = FaceHeight(face, c);
    // A perspective lens: rig.lua's FOV and near clip plane (the volume's near face, less 2%).
    cam.persp = g.persp;
    cam.tanH = g.tanP; cam.tanV = g.tanP * cam.h / cam.w;
    cam.nearCm = (face >= 4 ? g.nTop : g.nSide[face]) * 100 * 0.98;
    return cam;
}

// What Unreal would have drawn into one face: lit colour in RGB, linear depth (cm) in A, half floats. surface: what the
// base-colour capture draws instead - the surfaces' own colours (box magenta, floor cyan), no depth.
// kind: 0 the lit capture (colour, depth in alpha); 1 the surface-colour capture; 2 PLAYER LOOK's player capture - the
// box alone, its scene depth (cm) in red, as Unreal's SCS_SceneDepth writes it, far where nothing was drawn; 3 the lit
// capture with the box see-through (SOLID SURFACES): its colour drawn, its depth not - the floor's behind it in alpha;
// 4 the solid capture: the chosen see-through surfaces alone - the box, made opaque - as the player capture draws; 5 their
// glow capture: the box alone as the game draws it, see-through (SCS_SceneColorHDR) - its colour scaled by its opacity, and
// in alpha the light it lets through, 1 where nothing was drawn. Its upper half is opaque, its lower half 20% (a blade's
// core and its fading glow).
constexpr float kGlowCoreCm = 125.0f, kGlowEdgeOpacity = 0.2f;
static std::vector<DirectX::PackedVector::HALF> renderPixels(const Cam& cam, int kind = 0)
{
    using namespace DirectX::PackedVector;
    const bool surface = kind == 1, player = kind == 2, seeThrough = kind == 3, solid = kind == 4, glow = kind == 5;
    std::vector<HALF> px((size_t)cam.w * cam.h * 4);
    const double orthoH = cam.orthoW * cam.h / cam.w;
    for (int i = 0; i < cam.h; i++)
        for (int j = 0; j < cam.w; j++)
        {
            V3 o = cam.pos, d = cam.fwd;
            double toDepth = 1, tmin = 0;
            if (cam.persp)
            {
                // Scene depth is view-space z: the hit's distance along the ray times the ray's cosine to forward.
                const double nx = (j + 0.5) / cam.w * 2 - 1, ny = (i + 0.5) / cam.h * 2 - 1;
                d = add(cam.fwd, add(mul(cam.right, nx * cam.tanH), mul(cam.up, -ny * cam.tanV)));
                const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                d = mul(d, 1 / len);
                toDepth = d.x * cam.fwd.x + d.y * cam.fwd.y + d.z * cam.fwd.z;
                tmin = cam.nearCm / toDepth;
            }
            else
                o = add(cam.pos, add(mul(cam.right, ((j + 0.5) / cam.w - 0.5) * cam.orthoW),
                                     mul(cam.up, (0.5 - (i + 0.5) / cam.h) * orthoH)));
            double t; int what;
            float r = 0, gg = 0, b = 0, depth = 60000.0f;              // sky: far, like Unreal's
            if (glow)
            {
                HALF* q = &px[((size_t)i * cam.w + j) * 4];
                float op = 0.0f;
                if (hitScene(o, d, t, what, tmin, false)) op = (o.z + d.z * t) >= kGlowCoreCm ? 1.0f : kGlowEdgeOpacity;
                q[0] = XMConvertFloatToHalf(0.70f * op); q[1] = XMConvertFloatToHalf(0.08f * op);
                q[2] = XMConvertFloatToHalf(0.08f * op); q[3] = XMConvertFloatToHalf(1.0f - op);
                continue;
            }
            if (player || solid)
            {
                const float pd = hitScene(o, d, t, what, tmin, false) ? (float)(t * toDepth) : 60000.0f;
                HALF* q = &px[((size_t)i * cam.w + j) * 4];
                q[0] = XMConvertFloatToHalf(pd); q[1] = q[2] = q[3] = XMConvertFloatToHalf(0.0f);
                continue;
            }
            if (hitScene(o, d, t, what, tmin))
            {
                depth = (float)(t * toDepth);
                if (what == 1) { r = 0.15f; gg = 0.45f; b = 0.15f; }    // green floor
                else { r = 0.70f; gg = 0.08f; b = 0.08f; }             // red box
                if (surface && what == 1) { r = 0.10f; gg = 0.80f; b = 0.90f; }   // the floor's own colour: cyan
                else if (surface) { r = 0.90f; gg = 0.10f; b = 0.80f; }            // the box's: magenta
                if (seeThrough && what == 2)                                       // no depth of its own: what is behind
                {
                    double t2; int w2;
                    depth = hitScene(o, d, t2, w2, tmin, true, false) ? (float)(t2 * toDepth) : 60000.0f;
                }
            }
            HALF* q = &px[((size_t)i * cam.w + j) * 4];
            q[0] = XMConvertFloatToHalf(r); q[1] = XMConvertFloatToHalf(gg);
            q[2] = XMConvertFloatToHalf(b); q[3] = XMConvertFloatToHalf(depth);
        }
    return px;
}

static ID3D11ShaderResourceView* renderFace11(ID3D11Device* dev, const Cam& cam, int kind = 0)
{
    auto px = renderPixels(cam, kind);
    D3D11_TEXTURE2D_DESC d{};
    d.Width = cam.w; d.Height = cam.h; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA init{ px.data(), (UINT)(cam.w * 8), 0 };
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, &init, &tex))) return nullptr;
    dev->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    return srv;
}

// The same face as a D3D12 texture, uploaded and left in a shader-resource state -- the state the add-on
// assumes Unreal leaves a capture target in.
// UNITY: the same scene as rig/unity's cameras leave it - colour in an RGBA16F target whose rows run bottom-up, and the
// depth in a texture of its own, as reversed-Z device depth from the near and far planes the rig sets (DepthPlanes).
// kind 2: PLAYER LOOK's player camera - the box alone, its depth buffer far (0) wherever the box is not; kind 4 SOLID
// SURFACES' solid camera, the same; kind 3 the face with the box see-through (its colour, the floor's depth).
static void renderFaceUnity11(ID3D11Device* dev, const Cam& cam, float nearM, float farM,
                              ID3D11ShaderResourceView** colour, ID3D11ShaderResourceView** depth, int kind = 0)
{
    using namespace DirectX::PackedVector;
    auto px = renderPixels(cam, kind);
    std::vector<HALF> up(px.size());
    std::vector<float> d((size_t)cam.w * cam.h);
    for (int m = 0; m < cam.h; m++)                      // memory row m holds the view's row h-1-m
        for (int j = 0; j < cam.w; j++)
        {
            const HALF* s = &px[((size_t)(cam.h - 1 - m) * cam.w + j) * 4];
            HALF* q = &up[((size_t)m * cam.w + j) * 4];
            q[0] = s[0]; q[1] = s[1]; q[2] = s[2]; q[3] = XMConvertFloatToHalf(1.0f);
            const double z = XMConvertHalfToFloat(s[(kind == 2 || kind == 4) ? 0 : 3]) / 100.0;   // view-space z, metres
            double dev01 = 0;                                               // sky: the far plane
            if (z < 500.0)
                dev01 = cam.persp ? nearM * (farM / z - 1) / (farM - nearM) : (farM - z) / (farM - nearM);
            d[(size_t)m * cam.w + j] = (float)std::clamp(dev01, 0.0, 1.0);
        }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = cam.w; td.Height = cam.h; td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    D3D11_SUBRESOURCE_DATA ci{ up.data(), (UINT)(cam.w * 8), 0 };
    ID3D11Texture2D* tex = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&td, &ci, &tex))) { dev->CreateShaderResourceView(tex, nullptr, colour); tex->Release(); }
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA di{ d.data(), (UINT)(cam.w * 4), 0 };
    if (SUCCEEDED(dev->CreateTexture2D(&td, &di, &tex))) { dev->CreateShaderResourceView(tex, nullptr, depth); tex->Release(); }
}

static ID3D12Resource* renderFace12(ID3D12Device* dev, ID3D12CommandQueue* q, const Cam& cam, int kind = 0)
{
    auto px = renderPixels(cam, kind);
    const UINT rowPitch = (UINT)((cam.w * 8 + 255) / 256 * 256);
    D3D12_HEAP_PROPERTIES hpUp{ D3D12_HEAP_TYPE_UPLOAD }, hpDef{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = (UINT64)rowPitch * cam.h; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* up = nullptr; ID3D12Resource* tex = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up))))
        return nullptr;
    uint8_t* map = nullptr;
    up->Map(0, nullptr, (void**)&map);
    for (int i = 0; i < cam.h; i++) memcpy(map + (size_t)i * rowPitch, &px[(size_t)i * cam.w * 4], (size_t)cam.w * 8);
    up->Unmap(0, nullptr);
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = (UINT64)cam.w; td.Height = (UINT)cam.h;
    td.DepthOrArraySize = 1; td.MipLevels = 1; td.Format = DXGI_FORMAT_R16G16B16A16_TYPELESS;   // as a typeless RT
    td.SampleDesc.Count = 1; td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex))))
        { up->Release(); return nullptr; }
    ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList* l = nullptr; ID3D12Fence* f = nullptr;
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&al));
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, IID_PPV_ARGS(&l));
    D3D12_TEXTURE_COPY_LOCATION s{}, dl{};
    s.pResource = up; s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    s.PlacedFootprint.Footprint = { DXGI_FORMAT_R16G16B16A16_FLOAT, (UINT)cam.w, (UINT)cam.h, 1, rowPitch };
    dl.pResource = tex; dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    l->CopyTextureRegion(&dl, 0, 0, 0, &s, nullptr);
    D3D12_RESOURCE_BARRIER br{};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    br.Transition = { tex, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST,
                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
    l->ResourceBarrier(1, &br);
    l->Close();
    ID3D12CommandList* lists[] = { l };
    q->ExecuteCommandLists(1, lists);
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f));
    q->Signal(f, 1);
    for (int t = 0; t < 5000 && f->GetCompletedValue() < 1; t++) Sleep(1);
    f->Release(); l->Release(); al->Release(); up->Release();
    return tex;
}

// Any pixels into a D3D12 texture, left as a copy destination - the state the REDENGINE3 path keeps its copies in.
static ID3D12Resource* upload12(ID3D12Device* dev, ID3D12CommandQueue* q, int w, int h, DXGI_FORMAT f, int bpp, const void* px)
{
    const UINT rowPitch = (UINT)((w * bpp + 255) / 256 * 256);
    D3D12_HEAP_PROPERTIES hpUp{ D3D12_HEAP_TYPE_UPLOAD }, hpDef{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = (UINT64)rowPitch * h; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* up = nullptr; ID3D12Resource* tex = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up))))
        return nullptr;
    uint8_t* map = nullptr;
    up->Map(0, nullptr, (void**)&map);
    for (int i = 0; i < h; i++) memcpy(map + (size_t)i * rowPitch, (const uint8_t*)px + (size_t)i * w * bpp, (size_t)w * bpp);
    up->Unmap(0, nullptr);
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = (UINT64)w; td.Height = (UINT)h;
    td.DepthOrArraySize = 1; td.MipLevels = 1; td.Format = f; td.SampleDesc.Count = 1;
    if (FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex))))
        { up->Release(); return nullptr; }
    ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList* l = nullptr; ID3D12Fence* fe = nullptr;
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&al));
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, IID_PPV_ARGS(&l));
    D3D12_TEXTURE_COPY_LOCATION s{}, dl{};
    s.pResource = up; s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    s.PlacedFootprint.Footprint = { f, (UINT)w, (UINT)h, 1, rowPitch };
    dl.pResource = tex; dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    l->CopyTextureRegion(&dl, 0, 0, 0, &s, nullptr);
    l->Close();
    ID3D12CommandList* lists[] = { l };
    q->ExecuteCommandLists(1, lists);
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fe));
    q->Signal(fe, 1);
    for (int t = 0; t < 5000 && fe->GetCompletedValue() < 1; t++) Sleep(1);
    fe->Release(); l->Release(); al->Release(); up->Release();
    return tex;
}

// REDENGINE3: a face frame as The Witcher 3 leaves it - its finished picture (8-bit here) and its reversed depth (1 at
// the near plane, the sky at 0, though the projection runs forward) - drawn by the camera the script placed. REDengine's world is Unreal's with Y turned
// over, in metres: the scene below is Unreal's, so the camera goes across the same way to be drawn.
static void renderRed3(const Red3Camera& cam, int w, int h, std::vector<uint32_t>& colour, std::vector<float>& depth)
{
    Cam uc{};
    uc.pos = { cam.pos[0] * 100.0, -cam.pos[1] * 100.0, cam.pos[2] * 100.0 };
    uc.fwd = { cam.fwd[0], -cam.fwd[1], cam.fwd[2] };
    uc.right = { cam.right[0], -cam.right[1], cam.right[2] };
    uc.up = { cam.up[0], -cam.up[1], cam.up[2] };
    uc.w = w; uc.h = h; uc.persp = true; uc.tanH = 1.0 / cam.sx; uc.tanV = 1.0 / cam.sy; uc.nearCm = cam.nearM * 100.0;
    const auto px = renderPixels(uc);
    const double n = cam.nearM, f = cam.farM, A = f / (f - n), B = -n * f / (f - n);
    colour.assign((size_t)w * h, 0);
    depth.assign((size_t)w * h, 0.0f);
    for (size_t i = 0; i < (size_t)w * h; i++)
    {
        auto byte = [](float v) { return (uint32_t)(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f); };
        const float r = DirectX::PackedVector::XMConvertHalfToFloat(px[i * 4 + 0]);
        const float gg = DirectX::PackedVector::XMConvertHalfToFloat(px[i * 4 + 1]);
        const float b = DirectX::PackedVector::XMConvertHalfToFloat(px[i * 4 + 2]);
        const float zcm = DirectX::PackedVector::XMConvertHalfToFloat(px[i * 4 + 3]);
        colour[i] = byte(r) | (byte(gg) << 8) | (byte(b) << 16) | 0xFF000000u;
        if (zcm < 59000.0f) depth[i] = (float)(1.0 - (A + B / (zcm / 100.0)));
    }
}

// REDENGINE3: the camera block as the game binds it (sweep_red3.hpp), to read back.
static std::vector<float> red3Block(const Red3Camera& cam)
{
    std::vector<float> f(kRed3CameraBytes / 4 + 32, 0.0f);
    float* C = &f[400 / 4];
    for (int i = 0; i < 3; i++) { C[i] = cam.right[i]; C[4 + i] = cam.up[i]; C[8 + i] = cam.fwd[i]; C[12 + i] = cam.pos[i]; }
    C[15] = 1;
    const float n = cam.nearM, fa = cam.farM;
    float* P = &f[464 / 4];
    P[0] = cam.sx; P[5] = cam.sy; P[10] = fa / (fa - n); P[11] = 1; P[14] = -n * fa / (fa - n);
    float* nf = &f[608 / 4];
    nf[0] = -1; nf[1] = 1; nf[2] = n; nf[3] = fa;
    return f;
}

// The checks, the same for both backends.
static void checkVoxels(const std::vector<Voxel>& vox, int n, float lum, uint32_t merged, const Settings& c, const Geometry& g,
                        V3 centre, double headingDeg = 0)
{
    // Display to the world, the compact pass undone: the display turn back to the rig's frame (DisplayTurnDeg), each
    // axis's SCALE back to world metres, then the rig's own yaw (and the player's view, turning with them) to the
    // world's axes. With no scale and no turn this is the identity.
    const double pi = 3.14159265358979;
    const double T = DisplayTurnDeg(c) * pi / 180, Yr = (c.yawDeg + (c.turnWithPlayer ? headingDeg : 0)) * pi / 180;
    const double cT = std::cos(T), sT = std::sin(T), cY = std::cos(Yr), sY = std::sin(Yr);
    const double kx = g.ex / g.R, ky = g.ey / g.R, kz = g.ez / g.Z;
    check(n > 1000, "voxels came back without a stall", n);
    check(lum > 0.0f, "the exposure meter read the faces", lum);
    if (n <= 0) return;

    const double pxSide = 2 * std::max(g.halfWin[0], g.halfWin[1]) * 100 / c.faceRes,
                 pxTop = 2 * (g.persp ? std::max(g.ex, g.ey) : g.R) * 100 / c.faceRes;
    const double tol = 1.5 * std::max(pxSide, pxTop), worst = 3 * std::max(pxSide, pxTop);
    int within = 0, outside = 0, red = 0, green = 0;
    double maxd = 0, redX = 0, redY = 0, redZ = 0, greenZ = 0;
    for (const Voxel& v : vox)
    {
        const uint8_t r = (uint8_t)(v.rgb >> 16), gg = (uint8_t)(v.rgb >> 8);
        if (std::sqrt(v.x * v.x + v.y * v.y) > c.boundR * 1.05f * 1.4143f || std::fabs(v.z) > c.boundZ * 1.05f) ++outside;
        const double rx = (v.x * cT + v.y * sT) * kx, ry = (-v.x * sT + v.y * cT) * ky;      // the rig's frame, display units
        const float x = (float)(rx * cY - ry * sY), y = (float)(rx * sY + ry * cY);          // ...the world's axes
        const float z = (float)(v.z * kz);
        const V3 w = { centre.x + x * g.s * 100, centre.y + y * g.s * 100, centre.z - z * g.s * 100 };   // display +Z is down
        const double d = surfaceDistance(w);
        maxd = std::max(maxd, d);
        if (d <= tol) ++within;
        if (r > gg) { ++red; redX += x; redY += y; redZ += z; } else { ++green; greenZ += z; }
    }
    check(within >= (int)(0.99 * n), "99% of voxels within 1.5 px of a real surface", 100.0 * within / n, tol);
    check(maxd <= worst, "no voxel more than 3 px off a surface (cm)", maxd, worst);
    check(outside == 0, "nothing outside the volume's box", outside);
    check(red > 100 && green > 100, "both the box and the floor arrived", red, green);
    if (red && green)
    {
        check(redZ / red < greenZ / green, "-Z is up: the box sits above the floor", redZ / red, greenZ / green);
        // The box's centre is 3.0 m +X and 2.0 m -Y of the feet (+1.0 and -0.67 display units on a VX2-XL at 12 m):
        // checked in metres, which every display and scale share.
        check(redX / red * g.s > 1.5 && redY / red * g.s < -0.9, "not mirrored: the box arrives +X and -Y (m)",
              redX / red * g.s, redY / red * g.s);
    }

    // COLOUR RANGE: every voxel's brightest channel between the floor and the ceiling. A voxel merged from two colours
    // can sit a little under the floor; none can pass the ceiling.
    if (c.colorFloor != 0 || c.colorCeiling != 255)
    {
        int over = 0, inBand = 0;
        for (const Voxel& v : vox)
        {
            const int hi = std::max({ (int)(v.rgb >> 16 & 255), (int)(v.rgb >> 8 & 255), (int)(v.rgb & 255) });
            if (hi > c.colorCeiling + 1) ++over;
            if (hi >= c.colorFloor - 1 && hi <= c.colorCeiling + 1) ++inBand;
        }
        check(over == 0 && inBand >= (int)(0.99 * n), "colour range: every voxel's brightness within it (above it, % within)",
              over, 100.0 * inBand / n);
    }

    // The compact pass's merge: the floor is seen by the top camera and by the tilted sides, so with it on some points
    // must have gone, and no display voxel inside the box may hold two of what came back.
    // Cells by the shader's own formula and constants: points on a cell's edge round either way, so a recomputation by
    // another route disagrees about a few of them (at yaw 0 the top face's pixel grid lands on the cell grid).
    Settings cm = c;
    if (cm.mergePerUnit <= 0) cm.mergePerUnit = 56;
    const CompactConstants q = CompactFor(cm, g, 1, 1.0f);
    std::set<long long> cells;
    int sharedN = 0;
    for (const Voxel& v : vox)
    {
        const float cx = v.x * q.MergePer + q.MergeOffXY;
        const float cy = v.y * q.MergePer + q.MergeOffXY;
        const float cz = v.z * q.MergePer + q.MergeOffZ;
        if (cx < 0 || cy < 0 || cz < 0 || cx >= q.MergeGrid || cy >= q.MergeGrid || cz >= q.MergeGridZ) continue;   // passed through
        if (!cells.insert(((long long)cz * 1000 + (long long)cy) * 1000 + (long long)cx).second) ++sharedN;
    }
    const bool shared = sharedN > 0;
    if (c.mergePerUnit > 0)
    {
        check(merged > 0, "overlapping cameras merged on the GPU", merged, 100.0 * merged / (n + merged));
        check(sharedN <= n / 2000, "no display voxel holds two points (but for float ties on a cell's edge)", sharedN, n / 2000);
    }
    else
    {
        check(merged == 0, "mergePerUnit=0: nothing merged", merged);
        check(shared, "...and without it display voxels do hold two points");
    }
}

// SCALE and the display's shape: the scales along the rig's X, Y and up, and the display's box (VX2-XL 4 x 2, VX2 2 x 2).
// COLOUR RANGE: the brightness 0 and 255 are drawn as.
struct Look { float sx = 1, sy = 1, sz = 1, boundR = 4, boundZ = 2; int floor = 0, ceiling = 255; };

static const char* LookTitle(const Look& k)
{
    static char buf[96];
    buf[0] = 0;
    if (k.boundR != 4 || k.boundZ != 2) snprintf(buf, sizeof buf, ", display %.0f x %.0f", k.boundR, k.boundZ);
    if (k.sx != 1 || k.sy != 1 || k.sz != 1)
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), ", scale %.2f / %.2f / %.2f", k.sx, k.sy, k.sz);
    if (k.floor != 0 || k.ceiling != 255)
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), ", colour range %d-%d", k.floor, k.ceiling);
    return buf;
}

static void ApplyLook(Settings& c, const Look& k)
{
    c.scaleX = k.sx; c.scaleY = k.sy; c.scaleZ = k.sz;
    c.boundR = k.boundR; c.boundZ = k.boundZ;
    c.colorFloor = k.floor; c.colorCeiling = k.ceiling;
}

// REDENGINE3: the settings link (red3_link.hpp) finds a script object laid out as voxonRig.ws's CVoxonRigLink - here a
// heap block of floats - and not a block with only the first marker.
// REDENGINE3, native views: a face's camera written into a CRenderFrame's camera sets (red3_native.hpp) - the matrices
// agreeing with each other, a point straight ahead on the screen's middle at its reversed depth, both sets alike, and
// the four points round the camera kept where they were relative to it.
static void runCaseRed3Frame()
{
    printf("[REDENGINE3: a face's camera written into the renderer's frame]\n");
    std::vector<uint8_t> frame(vxcap::kRed3FrameCameraBytes + 0x40, 0);
    auto F = [&](size_t off) { float v; memcpy(&v, &frame[off], 4); return (double)v; };
    for (size_t s : vxcap::kRed3FrameCameraSet)
    {
        const float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        memcpy(&frame[s + 0x40], id, sizeof id);                       // an old camera at the origin, looking +Z
        const float pt[4] = { 1.0f, 2.0f, 3.0f, 1.0f };                // a point 3 ahead, 1 right, 2 up of it
        for (int k = 0; k < 4; k++) memcpy(&frame[s + 0x4E0 + 16 * k], pt, sizeof pt);
        const float cull[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1.00025f, 1, 0, 0, -0.25006f, 0 };   // near 0.25, far ~1000
        memcpy(&frame[s + 0x540 + 0x60], cull, sizeof cull);
    }
    vxcap::Red3Camera cam{};
    cam.pos[0] = 10; cam.pos[1] = 20; cam.pos[2] = 5;
    cam.fwd[0] = -1; cam.right[1] = 1; cam.up[2] = 1;                  // looking -X, level: right = forward x up = +Y
    cam.sx = 0.944f; cam.sy = 1.511f; cam.nearM = 4; cam.farM = 40;
    vxcap::Red3WriteFrameCamera(frame.data(), cam);
    const size_t s = vxcap::kRed3FrameCameraSet[0];
    // world to view, then view to world: the identity
    double worst = 0;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
    {
        double v = 0;
        for (int k = 0; k < 4; k++) v += F(s + 0x40 + (i * 4 + k) * 4) * F(s + 0x100 + (k * 4 + j) * 4);
        worst = std::max(worst, std::fabs(v - (i == j ? 1.0 : 0.0)));
    }
    check(worst < 1e-4, "world to view and view to world undo each other", worst);
    // a point 10 ahead through the reversed world to screen: the middle, at depth n (f - d) / (d (f - n))
    const double p[4] = { 0, 20, 5, 1 }, want = 4.0 * (40.0 - 10.0) / (10.0 * (40.0 - 4.0));
    double sc[4] = {};
    for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) sc[j] += p[k] * F(s + 0x2C0 + (k * 4 + j) * 4);
    check(std::fabs(sc[0] / sc[3]) < 1e-5 && std::fabs(sc[1] / sc[3]) < 1e-5, "a point straight ahead lands on the screen's middle", sc[0] / sc[3], sc[1] / sc[3]);
    check(std::fabs(sc[2] / sc[3] - want) < 1e-5, "...at its reversed depth", sc[2] / sc[3], want);
    check(F(s + 0x30) == 4.0 && F(s + 0x34) == 40.0 && F(s) == 10.0, "near, far and position written", F(s + 0x30), F(s + 0x34));
    const size_t s2 = vxcap::kRed3FrameCameraSet[1];
    check(memcmp(&frame[s], &frame[s2], vxcap::kRed3CameraSetBytes) == 0, "both camera sets alike");
    // the point that was (1, 2, 3) from the old camera is (1, 2, 3) from the new: right, up, ahead
    const double q[3] = { F(s + 0x4E0), F(s + 0x4E4), F(s + 0x4E8) };
    const double wantQ[3] = { 10 - 3, 20 + 1, 5 + 2 };
    check(std::fabs(q[0] - wantQ[0]) + std::fabs(q[1] - wantQ[1]) + std::fabs(q[2] - wantQ[2]) < 1e-4, "the points round the camera moved with it", q[0], q[1]);
    check(std::fabs(F(s + 0x540 + 0x60 + 0x38) - (-4.05 * (1000.2 / (1000.2 - 4.05)))) < 0.01, "the culling camera's near just beyond the camera's", F(s + 0x540 + 0x60 + 0x38));
}

static void runCaseLink()
{
    printf("[REDENGINE3: the settings link finds the script's object]\n");
    std::vector<float> obj(kLinkCount + 4, 0.0f), decoy(16, 0.0f);
    memcpy(obj.data() + 2, kLinkMark, sizeof kLinkMark);
    decoy[5] = kLinkMark[0]; decoy[6] = 1.0f;
    std::vector<float*> hits, alone;
    FindLinks(hits, &alone);
    bool mine = false;
    for (float* h : hits) mine = mine || h == obj.data() + 2;
    bool decoyHit = false;
    for (float* h : hits) decoyHit = decoyHit || h == decoy.data() + 5;
    check(mine && !decoyHit, "the markers found in a row, where they were put", (double)hits.size());
}

// ", turning with a player looking 30 degrees": the cases' titles.
static const char* TurnTitle(double heading)
{
    static char buf[64];
    snprintf(buf, sizeof buf, ", turning with a player looking %.0f degrees", heading);
    return buf;
}

static Settings caseSettings(float camDist, float yawDeg, float tiltDeg, float merge, int persp)
{
    Settings c;               // the ini's defaults: radius 12 m, faces 0,1,4, 512 px
    c.perspective = persp;
    c.camDist = camDist;
    c.yawDeg = yawDeg;
    c.tiltDeg = tiltDeg;
    c.mergePerUnit = merge;
    return c;
}

// The same faces decoded a second time by the same decoder: the merge's grid must have been cleared (same count, same
// merged), and the copy back - sized from the first volume's count now, not the whole capacity - must still hold it all.
static void checkRepeat(int n1, uint32_t m1, int n2, uint32_t m2, uint64_t truncated)
{
    check(n2 == n1 && m2 == m1, "a second volume: the same count, so the merge grid was cleared", n2, n1);
    check(truncated == 0, "the trimmed copy back held every voxel", (double)truncated);
}

static void runCase11(ID3D11Device* dev, ID3D11DeviceContext* ctx, float camDist, float yawDeg, float tiltDeg, float merge = 56,
                      int persp = 0, bool turn = false, double heading = 0, const Look& look = Look())
{
    printf("[D3D11: %s, camera distance %.0f, rig yaw %.0f, tilt %.0f, merge %.0f/unit%s%s]\n", persp ? "perspective" : "orthographic",
           camDist, yawDeg, tiltDeg, merge, turn ? TurnTitle(heading) : "", LookTitle(look));
    Settings c = caseSettings(camDist, yawDeg, tiltDeg, merge, persp);
    c.turnWithPlayer = turn;
    ApplyLook(c, look);
    const Geometry g = Derive(c);
    const V3 centre = CentreFor(c, g);

    ID3D11ShaderResourceView* srv[6] = {};
    for (int v = 0; v < 6; v++) if (c.faceOn[v]) srv[v] = renderFace11(dev, placeFace(v, c, g, centre, heading));

    Decoder dec;
    std::string err;
    if (!dec.Init(dev, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }
    check(dec.Run(ctx, srv, c, g, 1.0f, 1.0f), "pack + compact dispatched");

    std::vector<Voxel> vox;
    float lum = 0;
    uint32_t merged = 0, merged2 = 0;
    int n = -1, n2 = -1;
    for (int tries = 0; tries < 2000 && n < 0; tries++) { ctx->Flush(); Sleep(1); n = dec.Poll(ctx, vox, lum, merged); }
    checkVoxels(vox, n, lum, merged, c, g, centre, heading);
    std::vector<Voxel> vox2;
    dec.Run(ctx, srv, c, g, 1.0f, 1.0f);
    for (int tries = 0; tries < 2000 && n2 < 0; tries++) { ctx->Flush(); Sleep(1); n2 = dec.Poll(ctx, vox2, lum, merged2); }
    checkRepeat(n, merged, n2, merged2, dec.truncated);
    for (auto* s : srv) if (s) s->Release();
}

// UNITY (rig/unity): faces with bottom-up rows and depth in buffers of their own, decoded on D3D11, must land on the
// same surfaces as Unreal's.
static void runCaseUnity11(ID3D11Device* dev, ID3D11DeviceContext* ctx, float camDist, float yawDeg, float tiltDeg, int persp,
                           bool turn = false, double heading = 0, const Look& look = Look())
{
    printf("[D3D11, UNITY: %s, camera distance %.0f, rig yaw %.0f, tilt %.0f%s%s]\n", persp ? "perspective" : "orthographic",
           camDist, yawDeg, tiltDeg, turn ? TurnTitle(heading) : "", LookTitle(look));
    Settings c = caseSettings(camDist, yawDeg, tiltDeg, 56, persp);
    c.turnWithPlayer = turn;
    ApplyLook(c, look);
    c.engine = kEngineUnity;
    const Geometry g = Derive(c);
    const V3 centre = CentreFor(c, g);

    ID3D11ShaderResourceView* srv[6] = {};
    ID3D11ShaderResourceView* depth[6] = {};
    for (int v = 0; v < 6; v++)
    {
        if (!c.faceOn[v]) continue;
        float n = 0, f = 0;
        DepthPlanes(v, c, g, n, f);
        Cam cam = placeFace(v, c, g, centre, heading);
        cam.nearCm = n * 100;      // the Unity camera's own near plane, which the rig sets from DepthPlanes
        renderFaceUnity11(dev, cam, n, f, &srv[v], &depth[v]);
    }

    Decoder dec;
    std::string err;
    if (!dec.Init(dev, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }
    check(dec.Run(ctx, srv, c, g, 1.0f, 1.0f, depth), "pack (depth-buffer variant) + compact dispatched");
    std::vector<Voxel> vox;
    float lum = 0;
    uint32_t merged = 0;
    int n = -1;
    for (int tries = 0; tries < 2000 && n < 0; tries++) { ctx->Flush(); Sleep(1); n = dec.Poll(ctx, vox, lum, merged); }
    checkVoxels(vox, n, lum, merged, c, g, centre, heading);
    for (auto* s : srv) if (s) s->Release();
    for (auto* s : depth) if (s) s->Release();
}

// REDENGINE3: the game's own camera swept round the cube (sweep_red3.hpp). Each face is a whole frame of the free camera's
// lens, placed as the script places it; the decode knows each frame only by the camera block it was drawn with - which
// face from the far plane, the volume's centre and the rig's yaw from the latest face's camera alone.
static void runCaseRed3(ID3D12Device* dev, ID3D12CommandQueue* q, float yawDeg, const Look& look = Look())
{
    printf("[D3D12, REDENGINE3: the game's camera swept, rig yaw %.0f%s]\n", yawDeg, LookTitle(look));
    Settings c = caseSettings(1, yawDeg, 0, 56, 1);
    ApplyLook(c, look);
    c.engine = kEngineRed3;
    for (int v = 0; v < 6; v++) c.faceOn[v] = v < 5;
    const Geometry g = Derive(c);
    const V3 cu = CentreFor(c, g);
    const float centre[3] = { (float)(cu.x / 100.0), (float)(-cu.y / 100.0), (float)(cu.z / 100.0) };
    const int w = 640, h = 400;
    const float tanV = 1.0f / 2.0503f, tanH = tanV * w / h;   // the free camera's lens, measured in the game; this shape
    const double rho = yawDeg * 3.14159265358979 / 180.0;

    Red3Camera cams[5];
    ID3D12Resource* colour[6] = {};
    Decoder12::Frames fr;
    fr.colourFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    for (auto& f : fr.depthFormat) f = DXGI_FORMAT_R32_FLOAT;
    fr.activeRows = h;
    bool faces = true, blocks = true;
    for (int k = 0; k < 5; k++)
    {
        cams[k] = Red3Place(k, centre, rho, g, tanH, tanV);
        faces = faces && Red3FaceOf(cams[k]) == k;
        Red3Camera back{};
        const auto blk = red3Block(cams[k]);
        blocks = blocks && Red3ReadCamera(blk.data(), blk.size(), back) && std::fabs(back.nearM - cams[k].nearM) < 1e-4f
                 && std::fabs(back.pos[0] - cams[k].pos[0]) < 1e-4f && std::fabs(back.fwd[2] - cams[k].fwd[2]) < 1e-6f;
        std::vector<uint32_t> col; std::vector<float> dep;
        renderRed3(cams[k], w, h, col, dep);
        colour[k] = upload12(dev, q, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 4, col.data());
        fr.depth[k] = upload12(dev, q, w, h, DXGI_FORMAT_R32_FLOAT, 4, dep.data());
    }
    check(faces, "each face frame known by its far plane");
    check(blocks, "the camera block read back as it was bound");
    std::vector<float> notCam(kRed3CameraBytes / 4 + 32, 0.5f);
    Red3Camera none{};
    check(!Red3ReadCamera(notCam.data(), notCam.size(), none), "a block that is no camera is not read as one");
    Red3Camera player = cams[0];
    player.farM = 6500; player.nearM = 0.2f;
    check(Red3FaceOf(player) == -1, "the player's own frame is no face");

    float cL[3];
    Red3Centre(4, cams[4], g, cL);
    const double rhoL = Red3RigYaw(4, cams[4]), rho0 = Red3RigYaw(0, cams[0]), rho3 = Red3RigYaw(3, cams[3]);
    const double off = std::fabs(cL[0] - centre[0]) + std::fabs(cL[1] - centre[1]) + std::fabs(cL[2] - centre[2]);
    auto angle = [](double a) { return std::fabs(std::remainder(a, 6.283185307179586)); };
    check(off < 1e-3 && angle(rhoL - rho) < 1e-4 && angle(rho0 - rho) < 1e-4 && angle(rho3 - rho) < 1e-4,
          "the centre and the rig's yaw read back from the cameras alone (m, rad)", off, angle(rhoL - rho));
    for (int k = 0; k < 5; k++) fr.pack[k] = Red3FaceConstants(k, cams[k], cL, rhoL, c, g, w, h, 1, 1.0f);

    Settings cr = c;
    cr.faceRes = w;      // the pack texture's slots: one game frame wide
    Decoder12 dec;
    std::string err;
    if (!dec.Init(dev, q, err) || !dec.Configure(cr.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }
    check(dec.Run(colour, cr, g, 1.0f, 1.0f, nullptr, &fr), "pack + compact executed on whole frames");
    std::vector<Voxel> vox;
    float lum = 0;
    uint32_t merged = 0;
    int n = -1;
    for (int tries = 0; tries < 5000 && n < 0; tries++) { Sleep(1); n = dec.Poll(vox, lum, merged); }
    checkVoxels(vox, n, lum, merged, cr, g, cu);
    for (auto* t : colour) if (t) t->Release();
    for (auto* t : fr.depth) if (t) t->Release();
}

// FROSTBITE (frostbite_views.hpp): the engine's own extra views, one a face, which the add-on places. Each face's camera
// stands where Red3Place stands it, round the volume's centre at the rig's yaw, carried into Frostbite's axes (Y up, the
// camera looking along minus its third row) and back; it is drawn at the face's own size (FbFaceSize), its lit colour HDR
// and its depth reversed between the near and far planes the add-on gives the view, and decoded through the D3D11
// decoder's frames in the rig's own frame, as Cyberpunk's are.
static ID3D11ShaderResourceView* upload11(ID3D11Device* dev, int w, int h, DXGI_FORMAT f, int bpp, const void* data)
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1; td.Format = f; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA si{ data, (UINT)(w * bpp), 0 };
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&td, &si, &t))) { dev->CreateShaderResourceView(t, nullptr, &v); t->Release(); }
    return v;
}

static bool frostbiteFrames(ID3D11Device* dev, const Settings& c, const Geometry& g, const float centre[3], double rho,
                            ID3D11ShaderResourceView* colour[6], Decoder::Frames& fr);

static void runCaseFrostbite11(ID3D11Device* dev, ID3D11DeviceContext* ctx, float yawDeg, float tiltDeg)
{
    printf("[D3D11, FROSTBITE: the engine's own extra views, rig yaw %.0f, tilt %.0f]\n", yawDeg, tiltDeg);
    Settings c = caseSettings(1, yawDeg, tiltDeg, 56, 1);
    c.engine = kEngineFrostbite;
    c.faceRes = 320;
    for (int v = 0; v < 6; v++) c.faceOn[v] = v == 0 || v == 1 || v == 4;   // two sides and the top: three, the engine's most
    const Geometry g = Derive(c);
    const V3 cu = CentreFor(c, g);
    const float centre[3] = { (float)(cu.x / 100.0), (float)(-cu.y / 100.0), (float)(cu.z / 100.0) };
    const double rho = yawDeg * 3.14159265358979 / 180.0;

    bool sizes = true;
    for (int k = 0; k < 5; k++) sizes = sizes && FbFaceOf(c, FbFaceSize(c, k), FbFaceSize(c, k)) == k && FbFaceSize(c, k) % 4 == 0;
    for (int k = 1; k < 8; k++) sizes = sizes && FbFaceOf(c, FbBaseSize(c) + k, FbBaseSize(c) + k) == -1;
    sizes = sizes && FbFaceOf(c, FbBaseSize(c), FbBaseSize(c) + 8) == -1;
    check(sizes, "each face known by its views' size, multiples of 4 and 8 apart, and nothing else taken for one");
    int faces[kFbMaxFaces];
    Settings all = c;
    for (bool& b : all.faceOn) b = true;
    check(FbFaces(c, faces) == 3 && faces[0] == 0 && faces[1] == 1 && faces[2] == 4 && FbFaces(all, faces) == kFbMaxFaces,
          "the faces on, three at most: the engine's limit");

    ID3D11ShaderResourceView* colour[6] = {};
    Decoder::Frames fr;
    const bool rows = frostbiteFrames(dev, c, g, centre, rho, colour, fr);
    check(rows, "each face's camera into Frostbite's rows (looking along minus the third, Y up) and back as it was");

    Decoder dec;
    std::string err;
    if (!dec.Init(dev, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }
    check(dec.Run(ctx, colour, c, g, 1.0f, 1.0f, nullptr, nullptr, nullptr, nullptr, nullptr, &fr),
          "pack + compact dispatched on the three views' pictures, each at its own size (D3D11)");
    std::vector<Voxel> vox;
    float lum = 0;
    uint32_t merged = 0;
    int nv = -1;
    for (int tries = 0; tries < 2000 && nv < 0; tries++) { ctx->Flush(); Sleep(1); nv = dec.Poll(ctx, vox, lum, merged); }
    checkVoxels(vox, nv, lum, merged, c, g, cu);
    for (auto* t : colour) if (t) t->Release();
    for (auto* t : fr.depth) if (t) t->Release();
}

// FROSTBITE: each face drawn as the add-on places the engine's views - its camera through Frostbite's rows and back (rows
// ok: as it was), its picture and reversed depth at the face's own size - into colour (the caller releases them) and fr.
static bool frostbiteFrames(ID3D11Device* dev, const Settings& c, const Geometry& g, const float centre[3], double rho,
                            ID3D11ShaderResourceView* colour[6], Decoder::Frames& fr)
{
    fr.activeRows = c.faceRes;
    fr.player = FbPlayerShape(c, g);
    bool rows = true;
    for (int k = 0; k < 5; k++)
    {
        if (!c.faceOn[k]) continue;
        // The camera the views are given, through Frostbite's rows and back: as the breakpoint sets it, as it reads the
        // player's.
        const Red3Camera cam = FbWorldCamera(k, centre, rho, g);
        float m[16];
        FbRows(cam, m);
        Red3Camera back{};
        float err = FbCamera(m, back) ? 0.0f : 1.0f;
        for (int i = 0; i < 3; i++)
            err = std::max({ err, std::fabs(back.pos[i] - cam.pos[i]), std::fabs(back.fwd[i] - cam.fwd[i]), std::fabs(back.right[i] - cam.right[i]),
                             std::fabs(back.up[i] - cam.up[i]) });
        float fwdFb[3]; FbFromR3(cam.fwd, fwdFb);
        rows = rows && err < 1e-5f && std::fabs(m[8] + fwdFb[0]) < 1e-6f && std::fabs(m[9] + fwdFb[1]) < 1e-6f && std::fabs(m[10] + fwdFb[2]) < 1e-6f
               && std::fabs(m[5] - (k == 4 ? 0.0f : std::cos(g.tilt))) < 1e-4f;   // Frostbite's Y is up: a side camera's up leans by the tilt
        Cam uc{};
        uc.pos = { cam.pos[0] * 100.0, -cam.pos[1] * 100.0, cam.pos[2] * 100.0 };
        uc.fwd = { cam.fwd[0], -cam.fwd[1], cam.fwd[2] };
        uc.right = { cam.right[0], -cam.right[1], cam.right[2] };
        uc.up = { cam.up[0], -cam.up[1], cam.up[2] };
        uc.persp = true; uc.tanH = 1.0; uc.tanV = 1.0; uc.nearCm = cam.nearM * 100.0;
        const int w = FbFaceSize(c, k);
        uc.w = w; uc.h = w;
        const auto px = renderPixels(uc);
        std::vector<DirectX::PackedVector::HALF> col((size_t)w * w * 4);
        std::vector<float> dep((size_t)w * w, 0.0f);
        const double n = cam.nearM, fa = cam.farM;
        for (size_t i = 0; i < (size_t)w * w; i++)
        {
            for (int ch = 0; ch < 3; ch++) col[i * 4 + ch] = px[i * 4 + ch];   // the lit colour, HDR as the game's
            col[i * 4 + 3] = DirectX::PackedVector::XMConvertFloatToHalf(1.0f);
            const double z = DirectX::PackedVector::XMConvertHalfToFloat(px[i * 4 + 3]) / 100.0;
            if (z < 590.0 && z < fa) dep[i] = (float)(n * (fa - z) / (z * (fa - n)));
        }
        colour[k] = upload11(dev, w, w, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, col.data());
        fr.depth[k] = upload11(dev, w, w, DXGI_FORMAT_R32_FLOAT, 4, dep.data());
        fr.pack[k] = FbFaceConstants(k, c, g, 1.0f);
    }
    return rows;
}

// THE SIMS 4 (sims4_views.hpp): the game's renderer draws each face from the add-on's camera - placed as Battlefront II's
// are - into targets the size of the game's picture, through a lens as wide and tall as that picture at the face's scale
// and shifted, so that the square at its top left is the face's lens of 90 degrees. Past the square is more of the world,
// here something near and bright, which the decode must never read. The colour is the game's finished picture (8 bits),
// the depth runs forward (0 at the near plane, 1 far, as cleared), and the decode is handed the planes swapped.
static void runCaseSims4(ID3D11Device* dev, ID3D11DeviceContext* ctx, float yawDeg, float tiltDeg, int w, int h)
{
    printf("[D3D11, THE SIMS 4: the game's renderer drawing the add-on's cameras, rig yaw %.0f, tilt %.0f, a %dx%d picture]\n",
           yawDeg, tiltDeg, w, h);
    Settings c = caseSettings(1, yawDeg, tiltDeg, 56, 1);
    c.engine = kEngineSims4;
    c.faceRes = 320;
    for (int v = 0; v < 6; v++) c.faceOn[v] = v < kTs4Faces;
    const Geometry g = Derive(c);
    const V3 cu = CentreFor(c, g);
    const float centre[3] = { (float)(cu.x / 100.0), (float)(-cu.y / 100.0), (float)(cu.z / 100.0) };
    const double rho = yawDeg * 3.14159265358979 / 180.0;

    const int s = (int)Ts4Square((uint32_t)w, (uint32_t)h);
    const Ts4LensShape lens = Ts4Lens((uint32_t)w, (uint32_t)h);
    const float left = lens.cx - lens.width / 2, top = lens.cy + lens.height / 2;
    check(std::fabs(left + 1) < 1e-6f && std::fabs(top - 1) < 1e-6f && std::fabs(lens.width / w - 2.0f / s) < 1e-6f
          && std::fabs(lens.height / h - 2.0f / s) < 1e-6f,
          "the lens: the picture's top-left square spans 90 degrees both ways, every texel square");

    ID3D11ShaderResourceView* colour[6] = {};
    Decoder::Frames fr;
    fr.activeRows = c.faceRes;
    for (int k = 0; k < kTs4Faces; k++)
    {
        const Red3Camera cam = FbWorldCamera(k, centre, rho, g);
        Cam uc{};
        uc.pos = { cam.pos[0] * 100.0, -cam.pos[1] * 100.0, cam.pos[2] * 100.0 };
        uc.fwd = { cam.fwd[0], -cam.fwd[1], cam.fwd[2] };
        uc.right = { cam.right[0], -cam.right[1], cam.right[2] };
        uc.up = { cam.up[0], -cam.up[1], cam.up[2] };
        uc.persp = true; uc.tanH = 1.0; uc.tanV = 1.0; uc.nearCm = cam.nearM * 100.0;
        uc.w = s; uc.h = s;
        const auto px = renderPixels(uc);
        std::vector<uint8_t> col((size_t)w * h * 4);
        std::vector<float> dep((size_t)w * h, 0.02f);   // past the square: near...
        for (size_t i = 0; i < col.size(); i += 4) { col[i] = 0; col[i + 1] = 0; col[i + 2] = 255; col[i + 3] = 255; }   // ...and red (BGRA)
        const double n = cam.nearM, fa = cam.farM;
        for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++)
            {
                const size_t i = (size_t)y * s + x, o = (size_t)y * w + x;
                for (int ch = 0; ch < 3; ch++)   // the finished picture: display-encoded, BGRA
                {
                    const float v = DirectX::PackedVector::XMConvertHalfToFloat(px[i * 4 + ch]);
                    col[o * 4 + 2 - ch] = (uint8_t)std::lround(std::pow(std::clamp(v, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f);
                }
                const double z = DirectX::PackedVector::XMConvertHalfToFloat(px[i * 4 + 3]) / 100.0;
                dep[o] = z < 590.0 && z < fa ? (float)(fa * (z - n) / (z * (fa - n))) : 1.0f;
            }
        colour[k] = upload11(dev, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 4, col.data());
        fr.depth[k] = upload11(dev, w, h, DXGI_FORMAT_R32_FLOAT, 4, dep.data());
        fr.pack[k] = Ts4FaceConstants(k, c, g, 1.0f, (uint32_t)s);
    }

    Decoder dec;
    std::string err;
    if (!dec.Init(dev, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }
    check(dec.Run(ctx, colour, c, g, 1.0f, 1.0f, nullptr, nullptr, nullptr, nullptr, nullptr, &fr),
          "pack + compact dispatched on the five pictures' squares (D3D11)");
    std::vector<Voxel> vox;
    float lum = 0;
    uint32_t merged = 0;
    int nv = -1;
    for (int tries = 0; tries < 2000 && nv < 0; tries++) { ctx->Flush(); Sleep(1); nv = dec.Poll(ctx, vox, lum, merged); }
    checkVoxels(vox, nv, lum, merged, c, g, cu);
    for (auto* t : colour) if (t) t->Release();
    for (auto* t : fr.depth) if (t) t->Release();
}

// CRYSIS (cryengine2_views.hpp): the game's 3D engine drawing the add-on's cameras. A face camera is a copy of the game's
// CCamera with the face's matrix, a 90-degree vertical lens at the game's picture size and its frustum worked out again
// (Ce2SetCamera): checked against the camera's own geometry - a point ahead between the planes is inside all six, a
// point past each side outside it. Then each face's middle square - what the add-on copies out of the back buffer and
// the linear depth target - is decoded: colour display-encoded RGBA, depth the view depth over the face's far plane (1
// for the sky), and every voxel must land on the scene.
static void checkCe2Camera(float yawDeg, float tiltDeg, int w, int h)
{
    Settings c = caseSettings(1, yawDeg, tiltDeg, 56, 1);
    const Geometry g = Derive(c);
    const float centre[3] = { 3.0f, -7.0f, 1.5f };
    const double rho = yawDeg * 3.14159265358979 / 180.0;
    uint8_t game[kCe2CameraBytes] = {}, cam[kCe2CameraBytes];
    for (int k = 0; k < kCe2Faces; k++)
    {
        const Red3Camera rc = FbWorldCamera(k, centre, rho, g);
        Ce2SetCamera(cam, game, rc, w, h);
        float planes[6][4];
        memcpy(planes, cam + kCe2CamPlanes, sizeof planes);
        auto dist = [&](int i, const float p[3]) { return planes[i][0] * p[0] + planes[i][1] * p[1] + planes[i][2] * p[2] + planes[i][3]; };
        auto along = [&](float ahead, float across, float upward, float out[3])
        {
            for (int i = 0; i < 3; i++) out[i] = rc.pos[i] + rc.fwd[i] * ahead + rc.right[i] * across + rc.up[i] * upward;
        };
        const float mid = (rc.nearM + rc.farM) / 2;
        float p[3];
        along(mid, 0, 0, p);
        bool inside = true;
        for (int i = 0; i < 6; i++) inside = inside && dist(i, p) < 0;
        // Past each side: behind the near plane, beyond the far one, right, left, above, below - each its own plane.
        const float halfH = mid * (float)w / (float)h, halfV = mid;
        const float out[6][3] = { { rc.nearM * 0.5f, 0, 0 }, { rc.farM * 1.5f, 0, 0 }, { mid, halfH * 1.2f, 0 },
                                  { mid, -halfH * 1.2f, 0 }, { mid, 0, halfV * 1.2f }, { mid, 0, -halfV * 1.2f } };
        bool outside = true;
        for (int i = 0; i < 6; i++) { along(out[i][0], out[i][1], out[i][2], p); outside = outside && dist(i, p) > 0; }
        // The lens: 90 degrees up and down; its middle square as tall as the picture is 90 degrees across.
        float fov = 0, edge[3];
        memcpy(&fov, cam + kCe2CamFov, 4);
        memcpy(edge, cam + kCe2CamEdgeP, 12);
        const bool lens = std::fabs(fov - 1.5707963f) < 1e-6f && std::fabs(edge[1] - edge[2]) < 1e-3f
                          && std::fabs(-edge[0] - edge[2] * (float)w / (float)h) < 1e-3f;
        char what[160];
        sprintf_s(what, "face %d's camera: a point ahead inside all six planes, one past each side outside it; a 90-degree lens", k);
        check(inside && outside && lens, what);
    }
}

static void runCaseCryEngine2(ID3D11Device* dev, ID3D11DeviceContext* ctx, float yawDeg, float tiltDeg, int s)
{
    printf("[D3D11, CRYSIS: the game's 3D engine drawing the add-on's cameras, rig yaw %.0f, tilt %.0f, %d-texel squares]\n",
           yawDeg, tiltDeg, s);
    checkCe2Camera(yawDeg, tiltDeg, s * 16 / 9, s);
    Settings c = caseSettings(1, yawDeg, tiltDeg, 56, 1);
    c.engine = kEngineCryEngine2;
    c.faceRes = 320;
    for (int v = 0; v < 6; v++) c.faceOn[v] = v < kCe2Faces;
    const Geometry g = Derive(c);
    const V3 cu = CentreFor(c, g);
    const float centre[3] = { (float)(cu.x / 100.0), (float)(-cu.y / 100.0), (float)(cu.z / 100.0) };
    const double rho = yawDeg * 3.14159265358979 / 180.0;

    ID3D11ShaderResourceView* colour[6] = {};
    Decoder::Frames fr;
    fr.activeRows = c.faceRes;
    for (int k = 0; k < kCe2Faces; k++)
    {
        const Red3Camera cam = FbWorldCamera(k, centre, rho, g);
        Cam uc{};
        uc.pos = { cam.pos[0] * 100.0, -cam.pos[1] * 100.0, cam.pos[2] * 100.0 };
        uc.fwd = { cam.fwd[0], -cam.fwd[1], cam.fwd[2] };
        uc.right = { cam.right[0], -cam.right[1], cam.right[2] };
        uc.up = { cam.up[0], -cam.up[1], cam.up[2] };
        uc.persp = true; uc.tanH = 1.0; uc.tanV = 1.0; uc.nearCm = cam.nearM * 100.0;
        uc.w = s; uc.h = s;
        const auto px = renderPixels(uc);
        std::vector<uint8_t> col((size_t)s * s * 4);
        std::vector<float> dep((size_t)s * s, 1.0f);
        for (size_t i = 0; i < (size_t)s * s; i++)
        {
            for (int ch = 0; ch < 3; ch++)   // the finished picture: display-encoded, RGBA
            {
                const float v = DirectX::PackedVector::XMConvertHalfToFloat(px[i * 4 + ch]);
                col[i * 4 + ch] = (uint8_t)std::lround(std::pow(std::clamp(v, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f);
            }
            col[i * 4 + 3] = 255;
            const double z = DirectX::PackedVector::XMConvertHalfToFloat(px[i * 4 + 3]) / 100.0;
            dep[i] = z < 590.0 && z < cam.farM ? (float)(z / cam.farM) : 1.0f;   // linear: view depth over the far plane
        }
        colour[k] = upload11(dev, s, s, DXGI_FORMAT_R8G8B8A8_UNORM, 4, col.data());
        fr.depth[k] = upload11(dev, s, s, DXGI_FORMAT_R32_FLOAT, 4, dep.data());
        fr.pack[k] = Ce2FaceConstants(k, c, g, 1.0f, (uint32_t)s);
    }

    Decoder dec;
    std::string err;
    if (!dec.Init(dev, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }
    check(dec.Run(ctx, colour, c, g, 1.0f, 1.0f, nullptr, nullptr, nullptr, nullptr, nullptr, &fr),
          "pack + compact dispatched on the five faces' squares, their depth linear (D3D11)");
    std::vector<Voxel> vox;
    float lum = 0;
    uint32_t merged = 0;
    int nv = -1;
    for (int tries = 0; tries < 2000 && nv < 0; tries++) { ctx->Flush(); Sleep(1); nv = dec.Poll(ctx, vox, lum, merged); }
    checkVoxels(vox, nv, lum, merged, c, g, cu);
    for (auto* t : colour) if (t) t->Release();
    for (auto* t : fr.depth) if (t) t->Release();
}

struct LookStats;
static LookStats lookStats(const std::vector<Voxel>& vox, int n);

// FROSTBITE PLAYER LOOK: no capture of the player alone - the player is what stands in a cylinder on their feet. The box
// stands in for the player: decoded once plainly, then with a cylinder over it (its feet where the box arrived) and the
// player's +2 stops, then density 3 as well: the box brighter and three deep, the floor as it was.
static void checkFrostbitePlayerLook(ID3D11Device* dev, ID3D11DeviceContext* ctx);

// REDENGINE4: the game's render-to-texture cameras (red4_rtt.hpp). Each face is drawn by a camera the script stands where
// Red3Place does, with a square lens of 90 degrees, the near plane on the box's face and the far plane the script sets
// (Red4FarM): its depth at faceRes, reversed (1 at the near plane, 0 at the far one and beyond), its picture at the
// face's own size (faceRes + 1 + face). The decode knows the faces from the settings alone.
static void runCaseRed4(ID3D12Device* dev, ID3D12CommandQueue* q, float yawDeg, const Look& look = Look(), float tiltDeg = 0)
{
    printf("[D3D12, REDENGINE4: the game's render-to-texture cameras, rig yaw %.0f, tilt %.0f%s]\n", yawDeg, tiltDeg, LookTitle(look));
    Settings c = caseSettings(1, yawDeg, tiltDeg, 56, 1);
    ApplyLook(c, look);
    c.engine = kEngineRed4;
    c.faceRes = 320;
    for (int v = 0; v < 6; v++) c.faceOn[v] = v < 5;
    const Geometry g = Derive(c);
    const V3 cu = CentreFor(c, g);
    const float centre[3] = { (float)(cu.x / 100.0), (float)(-cu.y / 100.0), (float)(cu.z / 100.0) };
    const double rho = yawDeg * 3.14159265358979 / 180.0;

    bool sizes = true;
    for (int k = 0; k < 5; k++) sizes = sizes && Red4FaceOf(c, Red4FaceSize(c, k), Red4FaceSize(c, k)) == k;
    sizes = sizes && Red4FaceOf(c, c.faceRes, c.faceRes) == -1 && Red4FaceOf(c, c.faceRes + 6, c.faceRes + 6) == -1
            && Red4FaceOf(c, c.faceRes + 1, c.faceRes + 2) == -1;
    check(sizes, "each face known by its picture's size, and nothing else taken for one");
    const std::string text = Red4RigText(c, true);
    check(text.find("on=1\n") != std::string::npos && text.find("faces=31\n") != std::string::npos
          && text.find("faceRes=320\n") != std::string::npos, "the script's settings written out");

    ID3D12Resource* colour[6] = {};
    Decoder12::Frames fr;
    fr.colourFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    for (auto& f : fr.depthFormat) f = DXGI_FORMAT_R32_FLOAT;
    fr.activeRows = c.faceRes;
    for (int k = 0; k < 5; k++)
    {
        // The world as the script stands the cameras in it: the rig turned by its yaw, round the volume's centre.
        Red3Camera cam = Red3Place(k, centre, rho, g, 1.0f, 1.0f, g.tilt);
        Cam uc{};
        uc.pos = { cam.pos[0] * 100.0, -cam.pos[1] * 100.0, cam.pos[2] * 100.0 };
        uc.fwd = { cam.fwd[0], -cam.fwd[1], cam.fwd[2] };
        uc.right = { cam.right[0], -cam.right[1], cam.right[2] };
        uc.up = { cam.up[0], -cam.up[1], cam.up[2] };
        uc.persp = true; uc.tanH = 1.0; uc.tanV = 1.0; uc.nearCm = cam.nearM * 100.0;
        auto byte = [](float v) { return (uint32_t)(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f); };
        const int w = Red4FaceSize(c, k), d = c.faceRes;
        uc.w = w; uc.h = w;
        const auto pc = renderPixels(uc);   // the picture, at its own size
        std::vector<uint32_t> col((size_t)w * w, 0);
        for (size_t i = 0; i < (size_t)w * w; i++)
            col[i] = byte(DirectX::PackedVector::XMConvertHalfToFloat(pc[i * 4 + 0]))
                     | (byte(DirectX::PackedVector::XMConvertHalfToFloat(pc[i * 4 + 1])) << 8)
                     | (byte(DirectX::PackedVector::XMConvertHalfToFloat(pc[i * 4 + 2])) << 16) | 0xFF000000u;
        uc.w = d; uc.h = d;
        const auto pd = renderPixels(uc);   // the depth, at faceRes
        std::vector<float> dep((size_t)d * d, 0.0f);
        const double camD = std::sqrt(std::pow(cam.pos[0] - centre[0], 2) + std::pow(cam.pos[1] - centre[1], 2)
                                      + std::pow(cam.pos[2] - centre[2], 2));
        const double n = cam.nearM, fa = Red4FarM((float)camD, g);
        for (size_t i = 0; i < (size_t)d * d; i++)
        {
            const double z = DirectX::PackedVector::XMConvertHalfToFloat(pd[i * 4 + 3]) / 100.0;
            if (z < 590.0 && z < fa) dep[i] = (float)(n * (fa - z) / (z * (fa - n)));
        }
        colour[k] = upload12(dev, q, w, w, DXGI_FORMAT_R8G8B8A8_UNORM, 4, col.data());
        fr.depth[k] = upload12(dev, q, d, d, DXGI_FORMAT_R32_FLOAT, 4, dep.data());
        fr.pack[k] = Red4FaceConstants(k, c, g, 1.0f);
    }

    Decoder12 dec;
    std::string err;
    if (!dec.Init(dev, q, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }
    check(dec.Run(colour, c, g, 1.0f, 1.0f, nullptr, &fr), "pack + compact executed on the five pictures");
    std::vector<Voxel> vox;
    float lum = 0;
    uint32_t merged = 0;
    int n = -1;
    for (int tries = 0; tries < 5000 && n < 0; tries++) { Sleep(1); n = dec.Poll(vox, lum, merged); }
    checkVoxels(vox, n, lum, merged, c, g, cu);
    for (auto* t : colour) if (t) t->Release();
    for (auto* t : fr.depth) if (t) t->Release();
}

static void runCase12(ID3D12Device* dev, ID3D12CommandQueue* q, float camDist, float yawDeg, float tiltDeg, float merge = 56,
                      int persp = 0, bool turn = false, double heading = 0, const Look& look = Look())
{
    printf("[D3D12: %s, camera distance %.0f, rig yaw %.0f, tilt %.0f, merge %.0f/unit%s%s]\n", persp ? "perspective" : "orthographic",
           camDist, yawDeg, tiltDeg, merge, turn ? TurnTitle(heading) : "", LookTitle(look));
    Settings c = caseSettings(camDist, yawDeg, tiltDeg, merge, persp);
    c.turnWithPlayer = turn;
    ApplyLook(c, look);
    const Geometry g = Derive(c);
    const V3 centre = CentreFor(c, g);

    ID3D12Resource* tex[6] = {};
    for (int v = 0; v < 6; v++) if (c.faceOn[v]) tex[v] = renderFace12(dev, q, placeFace(v, c, g, centre, heading));

    Decoder12 dec;
    std::string err;
    if (!dec.Init(dev, q, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }

    // The handshake path as well: each face carries no code here, so this checks the probe reads back the texel it
    // was pointed at. The top face's corner sees the floor, straight down: depth = the camera's height above it.
    if (!persp)
    {
        ID3D12Resource* cand[1] = { tex[4] };
        check(dec.ProbeSubmit(cand, 1), "probe submitted");
        float vals[Decoder12::kProbeMax][4] = {};
        int got = 0;
        bool done = false;
        for (int t = 0; t < 5000 && !done; t++) { Sleep(1); done = dec.ProbeResult(vals, got); }
        const float expect = (g.topDist + c.feetZ * g.s) * 100.0f;
        check(done && got == 1 && std::fabs(vals[0][3] - expect) < 2.0f, "probe read the target's corner texel (cm)",
              vals[0][3], expect);
    }

    check(dec.Run(tex, c, g, 1.0f, 1.0f), "pack + compact executed");
    std::vector<Voxel> vox;
    float lum = 0;
    uint32_t merged = 0, merged2 = 0;
    int n = -1, n2 = -1;
    for (int tries = 0; tries < 5000 && n < 0; tries++) { Sleep(1); n = dec.Poll(vox, lum, merged); }
    checkVoxels(vox, n, lum, merged, c, g, centre, heading);
    std::vector<Voxel> vox2;
    dec.Run(tex, c, g, 1.0f, 1.0f);
    for (int tries = 0; tries < 5000 && n2 < 0; tries++) { Sleep(1); n2 = dec.Poll(vox2, lum, merged2); }
    checkRepeat(n, merged, n2, merged2, dec.truncated);
    for (auto* t : tex) if (t) t->Release();
}

// SURFACE COLOUR: the voxels' blue, which the lit faces have almost none of and the surfaces' own colours plenty
// (box 0.8, floor 0.9). At lighting 0 each voxel must carry its surface's own colour; at 0.5, about half of it.
// COLOUR RANGE: at lighting 0 with a range, each surface's own colour scaled whole, so its brightest channel (0.9 for
// both) lands at floor + (ceiling - floor) x 0.9 and the others keep their share of it: not lifted to the floor.
static void checkSurface(const std::vector<Voxel>& vox, int n, float lighting, int rangeFloor = 0, int rangeCeiling = 255)
{
    if (n <= 0) return;
    double boxC[3] = {}, floorC[3] = {};
    int box = 0, floor = 0;
    for (const Voxel& v : vox)
    {
        const int r = (v.rgb >> 16) & 255, gg = (v.rgb >> 8) & 255, b = v.rgb & 255;
        double* sum = r > gg ? boxC : floorC;
        sum[0] += r; sum[1] += gg; sum[2] += b;
        ++(r > gg ? box : floor);
    }
    for (double& x : boxC) x /= std::max(box, 1);
    for (double& x : floorC) x /= std::max(floor, 1);
    const double boxB = boxC[2], floorB = floorC[2];
    if (rangeFloor != 0 || rangeCeiling != 255)
    {
        const double ownBox[3] = { 0.90, 0.10, 0.80 }, ownFloor[3] = { 0.10, 0.80, 0.90 };   // renderPixels' surface colours
        const double k = (rangeFloor + (rangeCeiling - rangeFloor) * 0.90) / 0.90;
        double worst = 0;
        for (int i = 0; i < 3; i++)
            worst = std::max({ worst, std::fabs(boxC[i] - ownBox[i] * k), std::fabs(floorC[i] - ownFloor[i] * k) });
        check(worst < 8, "colour range: each surface's colour scaled whole into it, hue and saturation kept (box green, worst error)",
              boxC[1], worst);
    }
    else if (lighting <= 0.0f)
        check(std::fabs(boxB - 0.8 * 255) < 8 && std::fabs(floorB - 0.9 * 255) < 8,
              "lighting 0: every voxel has its surface's own colour (blue: box, floor)", boxB, floorB);
    else
        check(boxB > 0.3 * 0.8 * 255 && boxB < 0.7 * 0.8 * 255 + 25 && floorB > 0.3 * 0.9 * 255 && floorB < 0.7 * 0.9 * 255 + 25,
              "lighting 0.5: about half of the surfaces' colour (blue: box, floor)", boxB, floorB);
}

static void runCaseSurface11(ID3D11Device* dev, ID3D11DeviceContext* ctx, float lighting, int floor = 0, int ceiling = 255)
{
    printf("[D3D11: SURFACE COLOUR, perspective, lighting %.1f, colour range %d-%d]\n", lighting, floor, ceiling);
    Settings c = caseSettings(1, 0, 30, 56, 1);
    c.lighting = lighting;
    c.colorFloor = floor; c.colorCeiling = ceiling;
    const Geometry g = Derive(c);
    const V3 centre = CentreFor(c, g);
    ID3D11ShaderResourceView* srv[6] = {};
    ID3D11ShaderResourceView* surf[6] = {};
    for (int v = 0; v < 6; v++)
        if (c.faceOn[v]) { const Cam cam = placeFace(v, c, g, centre); srv[v] = renderFace11(dev, cam); surf[v] = renderFace11(dev, cam, true); }
    Decoder dec;
    std::string err;
    if (!dec.Init(dev, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }
    check(dec.Run(ctx, srv, c, g, 1.0f, 1.0f, nullptr, surf), "pack (surface colour) + compact dispatched");
    std::vector<Voxel> vox;
    float lum = 0;
    uint32_t merged = 0;
    int n = -1;
    for (int tries = 0; tries < 2000 && n < 0; tries++) { ctx->Flush(); Sleep(1); n = dec.Poll(ctx, vox, lum, merged); }
    checkVoxels(vox, n, lum, merged, c, g, centre);
    checkSurface(vox, n, lighting, floor, ceiling);
    for (auto* x : srv) if (x) x->Release();
    for (auto* x : surf) if (x) x->Release();
}

static void runCaseSurface12(ID3D12Device* dev, ID3D12CommandQueue* q, float lighting, int floor = 0, int ceiling = 255)
{
    printf("[D3D12: SURFACE COLOUR, perspective, lighting %.1f, colour range %d-%d]\n", lighting, floor, ceiling);
    Settings c = caseSettings(1, 0, 30, 56, 1);
    c.lighting = lighting;
    c.colorFloor = floor; c.colorCeiling = ceiling;
    const Geometry g = Derive(c);
    const V3 centre = CentreFor(c, g);
    ID3D12Resource* tex[6] = {};
    ID3D12Resource* surf[6] = {};
    for (int v = 0; v < 6; v++)
        if (c.faceOn[v]) { const Cam cam = placeFace(v, c, g, centre); tex[v] = renderFace12(dev, q, cam); surf[v] = renderFace12(dev, q, cam, true); }
    Decoder12 dec;
    std::string err;
    if (!dec.Init(dev, q, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }
    check(dec.Run(tex, c, g, 1.0f, 1.0f, surf), "pack (surface colour) + compact executed");
    std::vector<Voxel> vox;
    float lum = 0;
    uint32_t merged = 0;
    int n = -1;
    for (int tries = 0; tries < 5000 && n < 0; tries++) { Sleep(1); n = dec.Poll(vox, lum, merged); }
    checkVoxels(vox, n, lum, merged, c, g, centre);
    checkSurface(vox, n, lighting, floor, ceiling);
    for (auto* t : tex) if (t) t->Release();
    for (auto* t : surf) if (t) t->Release();
}

// PLAYER LOOK: the box stands for the player and has a player capture (renderPixels kind 2). Against the same scene
// decoded with Player look off: the player's pixels take its own exposure while the floor keeps the world's; a player
// density above 1 keeps more of its voxels; and the world's gamma and saturation reach the floor.
struct LookStats { int n = 0, box = 0, floor = 0; double boxR = 0, floorG = 0, floorSpread = 0; };

static LookStats lookStats(const std::vector<Voxel>& vox, int n)
{
    LookStats s;
    s.n = n;
    for (int i = 0; i < n && i < (int)vox.size(); i++)
    {
        const int r = (vox[i].rgb >> 16) & 255, gg = (vox[i].rgb >> 8) & 255, b = vox[i].rgb & 255;
        if (r > gg) { ++s.box; s.boxR += r; }
        else { ++s.floor; s.floorG += gg; s.floorSpread += std::max({ r, gg, b }) - std::min({ r, gg, b }); }
    }
    if (s.box) s.boxR /= s.box;
    if (s.floor) { s.floorG /= s.floor; s.floorSpread /= s.floor; }
    return s;
}

static void checkFrostbitePlayerLook(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    printf("[D3D11, FROSTBITE: player look, the player known by where they stand]\n");
    Settings base = caseSettings(1, 0, 30, 56, 1);
    base.engine = kEngineFrostbite;
    base.faceRes = 320;
    for (int v = 0; v < 6; v++) base.faceOn[v] = v == 0 || v == 1 || v == 4;
    const Geometry g = Derive(base);
    const V3 cu = CentreFor(base, g);
    const float centre[3] = { (float)(cu.x / 100.0), (float)(-cu.y / 100.0), (float)(cu.z / 100.0) };
    Decoder dec;
    std::string err;
    if (!dec.Init(dev, err) || !dec.Configure(base.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return; }
    auto decode = [&](const Settings& c, const PlayerShape* shape, std::vector<Voxel>& vox)
    {
        ID3D11ShaderResourceView* colour[6] = {};
        Decoder::Frames fr;
        frostbiteFrames(dev, c, g, centre, 0.0, colour, fr);
        if (shape) fr.player = *shape;
        int n = -1;
        float lum = 0;
        uint32_t merged = 0;
        if (dec.Run(ctx, colour, c, g, 1.0f, 1.0f, nullptr, nullptr, nullptr, nullptr, nullptr, &fr))
            for (int tries = 0; tries < 2000 && n < 0; tries++) { ctx->Flush(); Sleep(1); n = dec.Poll(ctx, vox, lum, merged); }
        for (auto* t : colour) if (t) t->Release();
        for (auto* t : fr.depth) if (t) t->Release();
        return n;
    };
    std::vector<Voxel> vb, vp, vd;
    const int nb = decode(base, nullptr, vb);
    // Where the box arrived: the middle of its voxels across, and its foot (+Z is down: the largest z).
    double sx = 0, sy = 0, foot = -1e9;
    int k = 0;
    for (int i = 0; i < nb; i++)
        if (((vb[i].rgb >> 16) & 255) > ((vb[i].rgb >> 8) & 255)) { sx += vb[i].x; sy += vb[i].y; foot = std::max(foot, (double)vb[i].z); k++; }
    if (k < 100) { check(false, "player look (Frostbite): the box arrived", k); return; }
    Settings at = base;
    at.feetX = (float)(sx / k); at.feetY = (float)(sy / k); at.feetZ = (float)foot;
    PlayerShape shape = FbPlayerShape(at, g);
    shape.radius = 2.5f; shape.height = 3.0f;   // the box is 3 x 2 m and 2.5 m tall: a player's cylinder, made to fit it
    Settings bright = base;
    bright.playerLook = true; bright.playerExposure = 2.0f;
    Settings dense = bright;
    dense.playerDensity = 3;
    const int np = decode(bright, &shape, vp), nd = decode(dense, &shape, vd);
    const LookStats b = lookStats(vb, nb), p = lookStats(vp, np), d = lookStats(vd, nd);
    printf("  (voxels %d / %d / %d, box %d / %d / %d)\n", b.n, p.n, d.n, b.box, p.box, d.box);
    check(b.box > 100 && p.box > 100 && p.boxR > b.boxR * 1.25,
          "player look (Frostbite): inside the cylinder, the player's exposure (+2 stops: box red before, after)", b.boxR, p.boxR);
    check(std::fabs(p.floorG - b.floorG) < 3, "player look (Frostbite): the floor, under the feet too, keeps the world's (floor green before, after)",
          b.floorG, p.floorG);
    check(d.box > p.box * 2 && std::abs(d.floor - p.floor) <= p.floor / 20,
          "player look (Frostbite): density 3 draws the player three deep, the world as it was (box voxels at 1, at 3)", p.box, d.box);
}

template <typename Decode>
static void checkPlayerLook(const char* api, Decode decode)
{
    Settings base = caseSettings(1, 0, 30, 56, 1);
    Settings bright = base;
    bright.playerLook = true; bright.playerExposure = 2.0f;
    Settings dense = bright;
    dense.playerDensity = 3;
    Settings world = base;
    world.playerLook = true; world.gamma = 2.0f; world.saturation = 0.0f;
    const LookStats b = decode(base), p = decode(bright), d = decode(dense), w = decode(world);
    printf("  (%s: voxels %d / %d / %d, box %d / %d / %d)\n", api, b.n, p.n, d.n, b.box, p.box, d.box);
    check(b.box > 100 && p.box > 100 && p.boxR > b.boxR * 1.25,
          "player look: the player's pixels take its own exposure (+2 stops: box red before, after)", b.boxR, p.boxR);
    check(std::fabs(p.floorG - b.floorG) < 3, "player look: the world keeps its own (floor green before, after)", b.floorG, p.floorG);
    check(d.box > p.box * 2 && std::abs(d.floor - p.floor) <= p.floor / 20,
          "player look: density 3 draws the player three deep, the world as it was (box voxels at 1, at 3)", p.box, d.box);
    check(w.floorG > b.floorG * 1.15 && w.floorSpread < 4,
          "look: the world's gamma 2 lifts its middle tones and saturation 0 greys it (floor green, colour spread)", w.floorG, w.floorSpread);
}

static void runCasePlayer11(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    printf("[D3D11: PLAYER LOOK, perspective - the box is the player]\n");
    checkPlayerLook("D3D11", [&](const Settings& c) {
        const Geometry g = Derive(c);
        const V3 centre = CentreFor(c, g);
        ID3D11ShaderResourceView* srv[6] = {};
        ID3D11ShaderResourceView* ply[6] = {};
        for (int v = 0; v < 6; v++)
            if (c.faceOn[v]) { const Cam cam = placeFace(v, c, g, centre); srv[v] = renderFace11(dev, cam); ply[v] = renderFace11(dev, cam, 2); }
        Decoder dec;
        std::string err;
        LookStats s;
        if (!dec.Init(dev, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return s; }
        std::vector<Voxel> vox;
        float lum = 0;
        uint32_t merged = 0;
        int n = -1;
        if (dec.Run(ctx, srv, c, g, 1.0f, 1.0f, nullptr, nullptr, ply))
            for (int tries = 0; tries < 2000 && n < 0; tries++) { ctx->Flush(); Sleep(1); n = dec.Poll(ctx, vox, lum, merged); }
        if (c.playerLook && c.playerDensity == 1 && c.gamma == 1.0f) checkVoxels(vox, n, lum, merged, c, g, centre);
        s = lookStats(vox, n);
        for (auto* x : srv) if (x) x->Release();
        for (auto* x : ply) if (x) x->Release();
        return s;
    });
}

// UNITY: the same, with each face's colour bottom-up and its depth, and the player's, in depth buffers of their own.
static void runCasePlayerUnity11(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    printf("[D3D11, UNITY: PLAYER LOOK, perspective - the box is the player]\n");
    checkPlayerLook("D3D11, Unity", [&](const Settings& base) {
        Settings c = base;
        c.engine = kEngineUnity;
        const Geometry g = Derive(c);
        const V3 centre = CentreFor(c, g);
        ID3D11ShaderResourceView* srv[6] = {};
        ID3D11ShaderResourceView* depth[6] = {};
        ID3D11ShaderResourceView* pcol[6] = {};
        ID3D11ShaderResourceView* ply[6] = {};
        for (int v = 0; v < 6; v++)
        {
            if (!c.faceOn[v]) continue;
            float n = 0, f = 0;
            DepthPlanes(v, c, g, n, f);
            Cam cam = placeFace(v, c, g, centre);
            cam.nearCm = n * 100;
            renderFaceUnity11(dev, cam, n, f, &srv[v], &depth[v]);
            renderFaceUnity11(dev, cam, n, f, &pcol[v], &ply[v], 2);
        }
        Decoder dec;
        std::string err;
        LookStats s;
        if (!dec.Init(dev, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return s; }
        std::vector<Voxel> vox;
        float lum = 0;
        uint32_t merged = 0;
        int n = -1;
        if (dec.Run(ctx, srv, c, g, 1.0f, 1.0f, depth, nullptr, ply))
            for (int tries = 0; tries < 2000 && n < 0; tries++) { ctx->Flush(); Sleep(1); n = dec.Poll(ctx, vox, lum, merged); }
        if (c.playerLook && c.playerDensity == 1 && c.gamma == 1.0f) checkVoxels(vox, n, lum, merged, c, g, centre);
        s = lookStats(vox, n);
        for (auto* x : srv) if (x) x->Release();
        for (auto* x : depth) if (x) x->Release();
        for (auto* x : pcol) if (x) x->Release();
        for (auto* x : ply) if (x) x->Release();
        return s;
    });
}

// SOLID SURFACES: the box see-through - its colour in the lit capture, the floor's depth behind it. Its colour then
// lands on the floor (or, against the sky, nowhere); the solid capture, with the box opaque, puts it back where it is,
// and so does the player capture when the box is the player's see-through part (a lightsaber's blade). The box's
// voxels' mean height (display z, +Z down) says where they landed.
struct SolidStats { int box = 0; double boxZ = 0; int standing = 0; double standingZ = 0; int lower = 0; };

// standing: the box's voxels clear of the floor (more than 0.1 display units above the floor's mean height), and their mean
// height - the part of it that stands where it is, not laid on the floor behind it. lower: those in its lower half, between
// the floor and halfway up to its top (its highest voxel).
static SolidStats solidStats(const std::vector<Voxel>& vox, int n)
{
    SolidStats s;
    double floorZ = 0; int floorN = 0;
    for (int i = 0; i < n && i < (int)vox.size(); i++)
    {
        const int r = (vox[i].rgb >> 16) & 255, gg = (vox[i].rgb >> 8) & 255;
        if (r > gg) { ++s.box; s.boxZ += vox[i].z; }
        else { ++floorN; floorZ += vox[i].z; }
    }
    if (s.box) s.boxZ /= s.box;
    if (floorN) floorZ /= floorN;
    for (int i = 0; i < n && i < (int)vox.size(); i++)
    {
        const int r = (vox[i].rgb >> 16) & 255, gg = (vox[i].rgb >> 8) & 255;
        if (r > gg && vox[i].z < floorZ - 0.1) { ++s.standing; s.standingZ += vox[i].z; }
    }
    if (s.standing) s.standingZ /= s.standing;
    double top = floorZ;
    for (int i = 0; i < n && i < (int)vox.size(); i++)
        if (((vox[i].rgb >> 16) & 255) > ((vox[i].rgb >> 8) & 255)) top = std::min(top, (double)vox[i].z);
    const double mid = (floorZ + top) / 2;
    for (int i = 0; i < n && i < (int)vox.size(); i++)
    {
        const int r = (vox[i].rgb >> 16) & 255, gg = (vox[i].rgb >> 8) & 255;
        if (r > gg && vox[i].z > mid + 0.05 && vox[i].z < floorZ - 0.1) ++s.lower;
    }
    return s;
}

template <typename Decode>
static void checkSolid(const char* api, Decode decode)
{
    const Settings base = caseSettings(1, 0, 30, 56, 1);
    const SolidStats a = decode(base, 0, false, false, false);  // opaque, as it was
    const SolidStats b = decode(base, 3, false, false, false);  // see-through, nothing done
    Settings s = base; s.solidChosen = true;
    const SolidStats cs = decode(s, 3, true, false, false);     // the solid capture
    Settings p = base; p.playerLook = true;
    const SolidStats d = decode(p, 3, false, true, false);      // the player capture
    printf("  (%s: box voxels %d / %d / %d / %d, mean z %.3f / %.3f / %.3f / %.3f)\n", api, a.box, b.box, cs.box, d.box,
           a.boxZ, b.boxZ, cs.boxZ, d.boxZ);
    check(a.box > 100 && (b.box < a.box / 4 || b.boxZ > a.boxZ + 0.1),
          "solid: a see-through box's colour lands on what is behind it (box mean z opaque, see-through)", a.boxZ, b.boxZ);
    check(cs.box > a.box * 0.8 && std::fabs(cs.boxZ - a.boxZ) < 0.03,
          "solid: the solid capture puts it back where it is (box mean z opaque, solid)", a.boxZ, cs.boxZ);
    check(d.box > a.box * 0.8 && std::fabs(d.boxZ - a.boxZ) < 0.03,
          "solid: the player capture puts the player's see-through part back (box mean z opaque, player)", a.boxZ, d.boxZ);
}

// ...only where opaque enough (solidOpacity, the glow capture): the box's upper half opaque, its lower half 20%. At 50%
// the upper half stands and the lower half's colour is laid on the floor behind; at 10% all of it stands, as with no
// glow capture; and the player capture's see-through part keeps to the same rule.
template <typename Decode>
static void checkGlow(const char* api, Decode decode)
{
    Settings s = caseSettings(1, 0, 30, 56, 1);
    s.solidChosen = true;
    const SolidStats all = decode(s, 3, true, false, false);    // solid wherever drawn
    s.solidOpacity = 0.5f;
    const SolidStats half = decode(s, 3, true, false, true);
    s.solidOpacity = 0.1f;
    const SolidStats low = decode(s, 3, true, false, true);
    Settings p = caseSettings(1, 0, 30, 56, 1);
    p.solidChosen = true; p.playerLook = true; p.solidOpacity = 0.5f;
    const SolidStats ph = decode(p, 3, true, true, true);        // with the player capture, the glow at 50%
    printf("  (%s: box voxels standing %d / %d / %d / %d, in its lower half %d / %d / %d / %d)\n", api, all.standing,
           half.standing, low.standing, ph.standing, all.lower, half.lower, low.lower, ph.lower);
    check(all.lower > 100 && half.lower < all.lower / 10,
          "solid from 50%: the box's 20%-opaque lower half no longer stands (its voxels: all solid, at 50%)", all.lower, half.lower);
    check(half.standing - half.lower > (all.standing - all.lower) * 0.9,
          "...and its opaque upper half still does (voxels above the middle: at 50%, all solid)", half.standing - half.lower,
          all.standing - all.lower);
    check(low.lower > all.lower * 0.9, "solid from 10%: all of it stands (lower-half voxels: at 10%, all solid)", low.lower, all.lower);
    check(ph.lower < all.lower / 10 && ph.standing - ph.lower > (all.standing - all.lower) * 0.9,
          "the player capture keeps to it too (lower-half voxels, upper-half voxels)", ph.lower, ph.standing - ph.lower);
}

static auto decodeSolid11(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    return [=](const Settings& c, int litKind, bool solid, bool player, bool glow) {
        const Geometry g = Derive(c);
        const V3 centre = CentreFor(c, g);
        ID3D11ShaderResourceView* srv[6] = {};
        ID3D11ShaderResourceView* sol[6] = {};
        ID3D11ShaderResourceView* ply[6] = {};
        ID3D11ShaderResourceView* glw[6] = {};
        for (int v = 0; v < 6; v++)
            if (c.faceOn[v])
            {
                const Cam cam = placeFace(v, c, g, centre);
                srv[v] = renderFace11(dev, cam, litKind);
                if (solid) sol[v] = renderFace11(dev, cam, 4);
                if (player) ply[v] = renderFace11(dev, cam, 2);
                if (glow) glw[v] = renderFace11(dev, cam, 5);
            }
        Decoder dec;
        std::string err;
        SolidStats s;
        if (!dec.Init(dev, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return s; }
        std::vector<Voxel> vox;
        float lum = 0;
        uint32_t merged = 0;
        int n = -1;
        if (dec.Run(ctx, srv, c, g, 1.0f, 1.0f, nullptr, nullptr, player ? ply : nullptr, solid ? sol : nullptr,
                    glow ? glw : nullptr))
            for (int tries = 0; tries < 2000 && n < 0; tries++) { ctx->Flush(); Sleep(1); n = dec.Poll(ctx, vox, lum, merged); }
        s = solidStats(vox, n);
        for (auto* arr : { srv, sol, ply, glw }) for (int v = 0; v < 6; v++) if (arr[v]) arr[v]->Release();
        return s;
    };
}

static void runCaseSolid11(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    printf("[D3D11: SOLID SURFACES - the box see-through]\n");
    checkSolid("D3D11", decodeSolid11(dev, ctx));
    printf("[D3D11: SOLID SURFACES - solid only where opaque enough]\n");
    checkGlow("D3D11", decodeSolid11(dev, ctx));
}

// UNITY: the same, each face's depth and its solid camera's (and player camera's) in depth buffers of their own.
static void runCaseSolidUnity11(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    printf("[D3D11, UNITY: SOLID SURFACES - the box see-through]\n");
    checkSolid("D3D11, Unity", [&](const Settings& base, int litKind, bool solid, bool player, bool) {
        Settings c = base;
        c.engine = kEngineUnity;
        const Geometry g = Derive(c);
        const V3 centre = CentreFor(c, g);
        ID3D11ShaderResourceView* srv[6] = {};
        ID3D11ShaderResourceView* depth[6] = {};
        ID3D11ShaderResourceView* scol[6] = {};
        ID3D11ShaderResourceView* sol[6] = {};
        ID3D11ShaderResourceView* pcol[6] = {};
        ID3D11ShaderResourceView* ply[6] = {};
        for (int v = 0; v < 6; v++)
        {
            if (!c.faceOn[v]) continue;
            float n = 0, f = 0;
            DepthPlanes(v, c, g, n, f);
            Cam cam = placeFace(v, c, g, centre);
            cam.nearCm = n * 100;
            renderFaceUnity11(dev, cam, n, f, &srv[v], &depth[v], litKind);
            if (solid) renderFaceUnity11(dev, cam, n, f, &scol[v], &sol[v], 4);
            if (player) renderFaceUnity11(dev, cam, n, f, &pcol[v], &ply[v], 2);
        }
        Decoder dec;
        std::string err;
        SolidStats s;
        if (!dec.Init(dev, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return s; }
        std::vector<Voxel> vox;
        float lum = 0;
        uint32_t merged = 0;
        int n = -1;
        if (dec.Run(ctx, srv, c, g, 1.0f, 1.0f, depth, nullptr, player ? ply : nullptr, solid ? sol : nullptr))
            for (int tries = 0; tries < 2000 && n < 0; tries++) { ctx->Flush(); Sleep(1); n = dec.Poll(ctx, vox, lum, merged); }
        s = solidStats(vox, n);
        for (auto* arr : { srv, depth, scol, sol, pcol, ply }) for (int v = 0; v < 6; v++) if (arr[v]) arr[v]->Release();
        return s;
    });
}

static auto decodeSolid12(ID3D12Device* dev, ID3D12CommandQueue* q)
{
    return [=](const Settings& c, int litKind, bool solid, bool player, bool glow) {
        const Geometry g = Derive(c);
        const V3 centre = CentreFor(c, g);
        ID3D12Resource* tex[6] = {};
        ID3D12Resource* sol[6] = {};
        ID3D12Resource* ply[6] = {};
        ID3D12Resource* glw[6] = {};
        for (int v = 0; v < 6; v++)
            if (c.faceOn[v])
            {
                const Cam cam = placeFace(v, c, g, centre);
                tex[v] = renderFace12(dev, q, cam, litKind);
                if (solid) sol[v] = renderFace12(dev, q, cam, 4);
                if (player) ply[v] = renderFace12(dev, q, cam, 2);
                if (glow) glw[v] = renderFace12(dev, q, cam, 5);
            }
        Decoder12 dec;
        std::string err;
        SolidStats s;
        if (!dec.Init(dev, q, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return s; }
        std::vector<Voxel> vox;
        float lum = 0;
        uint32_t merged = 0;
        int n = -1;
        if (dec.Run(tex, c, g, 1.0f, 1.0f, nullptr, nullptr, player ? ply : nullptr, solid ? sol : nullptr, glow ? glw : nullptr))
            for (int tries = 0; tries < 5000 && n < 0; tries++) { Sleep(1); n = dec.Poll(vox, lum, merged); }
        s = solidStats(vox, n);
        for (auto* arr : { tex, sol, ply, glw }) for (int v = 0; v < 6; v++) if (arr[v]) arr[v]->Release();
        return s;
    };
}

static void runCaseSolid12(ID3D12Device* dev, ID3D12CommandQueue* q)
{
    printf("[D3D12: SOLID SURFACES - the box see-through]\n");
    checkSolid("D3D12", decodeSolid12(dev, q));
    printf("[D3D12: SOLID SURFACES - solid only where opaque enough]\n");
    checkGlow("D3D12", decodeSolid12(dev, q));
}

static void runCasePlayer12(ID3D12Device* dev, ID3D12CommandQueue* q)
{
    printf("[D3D12: PLAYER LOOK, perspective - the box is the player]\n");
    checkPlayerLook("D3D12", [&](const Settings& c) {
        const Geometry g = Derive(c);
        const V3 centre = CentreFor(c, g);
        ID3D12Resource* tex[6] = {};
        ID3D12Resource* ply[6] = {};
        for (int v = 0; v < 6; v++)
            if (c.faceOn[v]) { const Cam cam = placeFace(v, c, g, centre); tex[v] = renderFace12(dev, q, cam); ply[v] = renderFace12(dev, q, cam, 2); }
        Decoder12 dec;
        std::string err;
        LookStats s;
        if (!dec.Init(dev, q, err) || !dec.Configure(c.faceRes, err)) { check(false, ("decoder init: " + err).c_str()); return s; }
        std::vector<Voxel> vox;
        float lum = 0;
        uint32_t merged = 0;
        int n = -1;
        if (dec.Run(tex, c, g, 1.0f, 1.0f, nullptr, nullptr, ply))
            for (int tries = 0; tries < 5000 && n < 0; tries++) { Sleep(1); n = dec.Poll(vox, lum, merged); }
        if (c.playerLook && c.playerDensity == 1 && c.gamma == 1.0f) checkVoxels(vox, n, lum, merged, c, g, centre);
        s = lookStats(vox, n);
        for (auto* t : tex) if (t) t->Release();
        for (auto* t : ply) if (t) t->Release();
        return s;
    });
}

int main()
{
    printf("GPU half self-test -- WARP, the add-on's own shaders, capture_gpu.hpp and capture_d3d12.hpp\n");
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
    {
        printf("FAIL no WARP device\n");
        return 2;
    }
    runCase11(dev, ctx, 1, 0, 30);
    runCase11(dev, ctx, 1, 0, 30, 0);
    runCase11(dev, ctx, 1, 0, 30, 112);   // a display twice as fine: the merge grid grows
    runCase11(dev, ctx, 3, 0, 30);
    runCase11(dev, ctx, 1, 45, 45);
    runCase11(dev, ctx, 1, 0, 30, 56, 1);    // perspective lenses: UE4's lighting
    runCase11(dev, ctx, 2, 45, 45, 56, 1);
    runCaseUnity11(dev, ctx, 1, 0, 30, 1);    // Unity: the rig's default lenses
    runCaseUnity11(dev, ctx, 2, 45, 45, 1);   // ...stood back, turned and tilted
    runCaseUnity11(dev, ctx, 1, 0, 30, 0);    // ...and orthographic
    runCase11(dev, ctx, 1, 0, 30, 56, 1, true, 30);       // turning with the player: the display turns with the view
    runCase11(dev, ctx, 2, 45, 45, 56, 1, true, -120);    // ...on top of a turned rig
    runCaseUnity11(dev, ctx, 1, 0, 30, 1, true, 75);      // ...and in a Unity game
    runCaseSurface11(dev, ctx, 0.0f);                       // the surfaces' own colours
    runCaseSurface11(dev, ctx, 0.5f);                       // ...with half the light over them
    runCaseSurface11(dev, ctx, 0.0f, 80, 220);              // COLOUR RANGE: the surfaces' colours drawn between 80 and 220
    runCasePlayer11(dev, ctx);                              // PLAYER LOOK: the player's own look and density
    runCasePlayerUnity11(dev, ctx);                         // ...in a Unity game
    runCaseSolid11(dev, ctx);                               // SOLID SURFACES: see-through surfaces put where they are
    runCaseSolidUnity11(dev, ctx);                          // ...in a Unity game
    runCaseFrostbite11(dev, ctx, 0, 0);                     // FROSTBITE: the engine's own extra views, placed by the add-on
    runCaseFrostbite11(dev, ctx, 30, 30);                   // ...the rig turned, its side cameras tilted down
    checkFrostbitePlayerLook(dev, ctx);                     // ...the player's own look, by where they stand
    runCaseSims4(dev, ctx, 0, 0, 480, 256);                 // THE SIMS 4: the game's renderer, the add-on's cameras
    runCaseSims4(dev, ctx, 30, 30, 256, 384);               // ...the rig turned and tilted, a picture taller than wide
    runCaseCryEngine2(dev, ctx, 0, 0, 256);                 // CRYSIS: the game's 3D engine, the add-on's cameras
    runCaseCryEngine2(dev, ctx, 30, 30, 300);               // ...the rig turned and tilted
    Look sq; sq.sx = 2;                                     // SCALE: twice the world along X, squashed in (Mortal Kombat 1)
    Look mixed; mixed.sx = 1.5f; mixed.sy = 0.75f; mixed.sz = 1.25f;   // ...squashed one way, stretched another
    Look vx2; vx2.boundR = 2; vx2.boundZ = 2;               // a VX2: as tall as wide, square side faces
    Look vx2sq = vx2; vx2sq.sx = 2; vx2sq.sz = 0.5f;
    runCase11(dev, ctx, 1, 0, 30, 56, 1, false, 0, sq);
    runCase11(dev, ctx, 1, 30, 30, 56, 1, true, 40, mixed);
    runCase11(dev, ctx, 1, 0, 0, 56, 1, false, 0, vx2);
    runCase11(dev, ctx, 2, 45, 30, 56, 1, false, 0, vx2sq);
    runCase11(dev, ctx, 1, 0, 30, 56, 0, false, 0, sq);    // orthographic: the scale is not used, and nothing breaks
    runCase11(dev, ctx, 1, 0, 30, 56, 0, false, 0, vx2);   // ...and a VX2's square side faces there too
    runCaseUnity11(dev, ctx, 1, 0, 30, 1, false, 0, sq);
    Look rng; rng.floor = 80; rng.ceiling = 220;            // COLOUR RANGE on the lit colour, both builds of the pack pass
    runCase11(dev, ctx, 1, 0, 30, 56, 1, false, 0, rng);
    runCaseUnity11(dev, ctx, 1, 0, 30, 1, false, 0, rng);
    ctx->Release(); dev->Release();

    IDXGIFactory4* fac = nullptr;
    IDXGIAdapter* warp = nullptr;
    ID3D12Device* dev12 = nullptr;
    ID3D12CommandQueue* q = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&fac))) || FAILED(fac->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev12))))
    {
        printf("FAIL no D3D12 WARP device\n");
        return 2;
    }
    D3D12_COMMAND_QUEUE_DESC qd{ D3D12_COMMAND_LIST_TYPE_DIRECT };
    dev12->CreateCommandQueue(&qd, IID_PPV_ARGS(&q));
    runCase12(dev12, q, 1, 0, 30);
    runCase12(dev12, q, 1, 0, 30, 0);
    runCase12(dev12, q, 1, 0, 30, 112);
    runCase12(dev12, q, 3, 0, 30);
    runCase12(dev12, q, 1, 45, 45);
    runCase12(dev12, q, 1, 0, 30, 56, 1);
    runCase12(dev12, q, 2, 45, 45, 56, 1);
    runCase12(dev12, q, 1, 0, 30, 56, 1, true, 30);       // turning with the player
    {
        Look sq12; sq12.sx = 2;
        Look vx2y; vx2y.boundR = 2; vx2y.boundZ = 2; vx2y.sy = 1.5f;
        runCase12(dev12, q, 1, 0, 30, 56, 1, false, 0, sq12);
        runCase12(dev12, q, 1, 0, 30, 56, 1, false, 0, vx2y);
    }
    runCaseSurface12(dev12, q, 0.0f);                       // the surfaces' own colours
    runCaseSurface12(dev12, q, 0.0f, 80, 220);              // ...drawn between 80 and 220
    runCasePlayer12(dev12, q);                              // PLAYER LOOK: the player's own look and density
    runCaseSolid12(dev12, q);                               // SOLID SURFACES
    runCaseLink();                                          // REDENGINE3: the settings link
    runCaseRed3Frame();                                     // REDENGINE3: native views' face camera in the renderer's frame
    runCaseRed3(dev12, q, 0);                               // REDENGINE3: the game's own camera swept round the cube
    runCaseRed3(dev12, q, 30);                              // ...the rig turned
    {
        Look sqr; sqr.sx = 2; sqr.sz = 1.25f;
        runCaseRed3(dev12, q, 0, sqr);                      // ...squashed and stretched
    }
    runCaseRed4(dev12, q, 0);                               // REDENGINE4: the game's render-to-texture cameras
    runCaseRed4(dev12, q, 30);                              // ...the rig turned
    runCaseRed4(dev12, q, 30, Look(), 35);                  // ...and its side cameras tilted down
    {
        Look sqr; sqr.sx = 2; sqr.sz = 1.25f;
        runCaseRed4(dev12, q, 0, sqr);                      // ...squashed and stretched
    }
    q->Release(); dev12->Release(); warp->Release(); fac->Release();

    printf(g_fail ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
