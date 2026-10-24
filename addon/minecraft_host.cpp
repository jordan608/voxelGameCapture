// minecraft_host.cpp - the decode for Minecraft: Java Edition (rig/minecraft, engine=minecraft).
//
// The rig mod draws the cube's faces in the game (the game's own level render, once more per face, in the frame) and
// reads each face's colour and depth back through the game's renderer, which runs on OpenGL or Vulkan - neither of
// which the add-on reads. So the mod hands the pictures over in a file both map (THE SHARED FILE, below), and this
// program, started by the mod, runs the add-on's own decoder on a D3D11 device of its own (capture_gpu.hpp: the same
// code) and sends each volume down the app's pipe (Feed.cpp), as the add-on does. It reads the same settings -
// voxon_capture_ue.ini in the game's folder, the app's control file over it - and ends when the game does.
//
//   voxon_capture_mc.exe <game folder> <the game's process id>
#include "capture_gpu.hpp"
#include "Feed.h"

#include <chrono>
#include <cstdarg>
#include <share.h>
#include <thread>

namespace
{
    // THE SHARED FILE (rig/minecraft FaceFile.java writes it; the two must agree): %TEMP%\voxon_capture_mc.faces.
    // A 4096-byte header, then kSlots slots of slotBytes each: a 256-byte slot header, then every face's picture at a
    // fixed place - its colour (RGBA8, rows bottom-up) and then its depth (float, 1 near, 0 far, rows bottom-up), each
    // faceRes x FaceHeight. The mod fills a slot, then sets the slot's sequence and the header's newest slot and
    // sequence, in that order.
    constexpr uint32_t kMagic = 0x434D5856;   // "VXMC"
    constexpr uint32_t kVersion = 1;
    constexpr size_t kHeader = 4096, kSlotHeader = 256;

#pragma pack(push, 1)
    struct FileHeader { uint32_t magic, version, slots, slotBytes; uint64_t latest; uint32_t latestSlot, faceRes, sideH; };
    struct SlotHeader { uint64_t seq; uint32_t faceMask, faceRes, sideH; };
#pragma pack(pop)

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

    size_t FaceBytes(int v, int faceRes, int sideH) { return (size_t)faceRes * (v >= 4 ? faceRes : sideH) * 8; }

    size_t FaceOffset(int v, int faceRes, int sideH)
    {
        size_t o = kSlotHeader;
        for (int i = 0; i < v; i++) o += FaceBytes(i, faceRes, sideH);
        return o;
    }

    // The faces on the GPU: colour (sRGB, so the decode reads it linear) and depth as the game draws it - reversed, 1
    // near and 0 far (its Projection swaps the planes), as Unity's is, which the decode reads.
    struct Face
    {
        ID3D11Texture2D* colour = nullptr; ID3D11ShaderResourceView* colourSrv = nullptr;
        ID3D11Texture2D* depth = nullptr;  ID3D11ShaderResourceView* depthSrv = nullptr;
        int w = 0, h = 0;
        void Release()
        {
            if (colourSrv) colourSrv->Release(); if (colour) colour->Release();
            if (depthSrv) depthSrv->Release(); if (depth) depth->Release();
            *this = Face{};
        }
    };

    bool MakeFace(ID3D11Device* dev, Face& f, int w, int h)
    {
        f.Release();
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc = { 1, 0 };
        d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &f.colour)) || FAILED(dev->CreateShaderResourceView(f.colour, nullptr, &f.colourSrv))) return false;
        d.Format = DXGI_FORMAT_R32_FLOAT;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &f.depth)) || FAILED(dev->CreateShaderResourceView(f.depth, nullptr, &f.depthSrv))) return false;
        f.w = w; f.h = h;
        return true;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: voxon_capture_mc.exe <game folder> <game pid>\n"); return 2; }
    const std::string gameDir = argv[1];
    const std::string iniPath = gameDir + "\\voxon_capture_ue.ini";
    g_log = _fsopen((gameDir + "\\voxon_capture_ue.log").c_str(), "w", _SH_DENYNO);   // readable while it runs
    HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)strtoul(argv[2], nullptr, 10));
    Log("voxon_capture_mc started: settings from %s, the game is process %s", iniPath.c_str(), argv[2]);

    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
    { Log("no D3D11 device"); return 1; }
    vxcap::Decoder dec;
    std::string err;
    if (!dec.Init(dev, err)) { Log("decoder: init FAILED %s", err.c_str()); return 1; }

    char temp[MAX_PATH]; GetTempPathA(MAX_PATH, temp);
    const std::string facesPath = std::string(temp) + "voxon_capture_mc.faces";
    HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
    const uint8_t* view = nullptr;
    size_t mappedBytes = 0;

    vxcap::Settings cfg; vxcap::Geometry geo{}; std::string raw, rawSeen;
    int configuredRes = 0;
    Face faces[6];
    std::vector<vxcap::Voxel> voxels;
    std::vector<voxon::VoxelPoint> pts;
    voxon::FeedPipe pipe; voxon::FeedWriter w;
    float autoExp = 1.0f;
    uint64_t seen = 0, decoded = 0, sent = 0, dropped = 0, lastCount = 0;
    ULONGLONG settingsAt = 0, statsAt = GetTickCount64(), pipeTried = 0, expAt = 0;

    for (;;)
    {
        if (game && WaitForSingleObject(game, 0) == WAIT_OBJECT_0) { Log("the game has ended"); break; }
        const ULONGLONG now = GetTickCount64();

        // THE SETTINGS, read again twice a second: the add-on's own LoadSettings, so both halves agree.
        if (now - settingsAt > 500)
        {
            settingsAt = now;
            vxcap::Settings c;
            if (vxcap::LoadSettings(iniPath.c_str(), c, raw) && raw != rawSeen)
            {
                rawSeen = raw;
                cfg = c;
                cfg.perspective = 1;           // the mod draws perspective lenses only, whatever the app asks
                geo = vxcap::Derive(cfg);
                Log("settings: %s, faces %d%d%d%d%d%d, %d px, radius %.1f m", cfg.engine == vxcap::kEngineMinecraft ? "minecraft" : "NOT engine=minecraft",
                    cfg.faceOn[0], cfg.faceOn[1], cfg.faceOn[2], cfg.faceOn[3], cfg.faceOn[4], cfg.faceOn[5], cfg.faceRes, cfg.radiusM);
                if (cfg.faceRes != configuredRes)
                {
                    if (!dec.Configure(cfg.faceRes, err)) { Log("decoder: configure FAILED %s", err.c_str()); configuredRes = 0; }
                    else configuredRes = cfg.faceRes;
                }
            }
        }

        // THE SHARED FILE, mapped once the mod has made it, and again when its size changes (a new face size).
        if (!view)
        {
            file = CreateFileA(facesPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                LARGE_INTEGER size{}; GetFileSizeEx(file, &size);
                mapping = size.QuadPart > (LONGLONG)kHeader ? CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
                view = mapping ? (const uint8_t*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
                mappedBytes = (size_t)size.QuadPart;
                if (view) Log("faces: mapped %s, %zu bytes", facesPath.c_str(), mappedBytes);
                else { if (mapping) CloseHandle(mapping); CloseHandle(file); mapping = nullptr; file = INVALID_HANDLE_VALUE; }
            }
            if (!view) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }
        }
        const FileHeader* hdr = (const FileHeader*)view;
        const size_t need = kHeader + (size_t)hdr->slots * hdr->slotBytes;
        if (hdr->magic != kMagic || hdr->version != kVersion || need > mappedBytes)
        {
            UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(file);
            view = nullptr; mapping = nullptr; file = INVALID_HANDLE_VALUE;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // A NEW CAPTURE: each face's pictures onto the GPU, then the decode.
        const uint64_t latest = hdr->latest;
        if (latest != seen && latest != 0 && configuredRes != 0 && hdr->latestSlot < hdr->slots)
        {
            seen = latest;
            const uint8_t* slot = view + kHeader + (size_t)hdr->latestSlot * hdr->slotBytes;
            const SlotHeader* sh = (const SlotHeader*)slot;
            if (sh->seq == latest && (int)sh->faceRes == cfg.faceRes && (int)sh->sideH == vxcap::SideHeight(cfg))
            {
                ID3D11ShaderResourceView* srv[6] = {}, * depthSrv[6] = {};
                for (int v = 0; v < 6; v++)
                {
                    if (!(sh->faceMask & (1u << v)) || !cfg.faceOn[v]) continue;
                    const int fw = cfg.faceRes, fh = vxcap::FaceHeight(v, cfg);
                    if (faces[v].w != fw || faces[v].h != fh) MakeFace(dev, faces[v], fw, fh);
                    const uint8_t* pic = slot + FaceOffset(v, sh->faceRes, sh->sideH);
                    ctx->UpdateSubresource(faces[v].colour, 0, nullptr, pic, fw * 4, 0);
                    ctx->UpdateSubresource(faces[v].depth, 0, nullptr, pic + (size_t)fw * fh * 4, fw * 4, 0);
                    srv[v] = faces[v].colourSrv;
                    depthSrv[v] = faces[v].depthSrv;
                }
                const float exposure = autoExp * std::pow(2.0f, cfg.exposureComp);
                if (dec.Run(ctx, srv, cfg, geo, exposure, 1.0f, depthSrv)) ++decoded;
            }
        }

        // A FINISHED VOLUME: the add-on's auto-exposure (Deliver), then down the pipe if the app is ready for it.
        float lum = 0; uint32_t merged = 0;
        const int n = dec.Poll(ctx, voxels, lum, merged);
        if (n >= 0)
        {
            lastCount = (uint64_t)n;
            const float dt = expAt ? std::min(0.5f, (now - expAt) / 1000.0f) : 0.033f;
            expAt = now;
            if (lum > 0)
            {
                const float lo = std::min(cfg.exposureMinEV, cfg.exposureMaxEV), hi = std::max(cfg.exposureMinEV, cfg.exposureMaxEV);
                const float target = std::clamp(0.18f / lum, std::exp2(lo), std::exp2(hi));
                const float a = 1.0f - std::exp(-3.0f * dt);
                const float cur = std::max(autoExp, 1e-9f);
                autoExp = std::pow(2.0f, std::log2(cur) + (std::log2(target) - std::log2(cur)) * a);
            }
            if (!pipe.IsConnected() && now - pipeTried > 1000)
            {
                pipeTried = now;
                if (pipe.Connect(cfg.pipe))
                {
                    Log("pipe: connected to \\\\.\\pipe\\%s", cfg.pipe);
                    w.Clear(); w.Reset();
                    pipe.Write(w.Data(), w.Size());
                }
            }
            if (pipe.IsConnected())
            {
                if (!pipe.WaitIdle(8)) ++dropped;
                else
                {
                    pts.resize(voxels.size());
                    for (size_t i = 0; i < voxels.size(); i++)
                    {
                        const vxcap::Voxel& v = voxels[i];
                        pts[i] = { v.x, v.y, v.z, (uint8_t)(v.rgb >> 16), (uint8_t)(v.rgb >> 8), (uint8_t)v.rgb };
                    }
                    w.Clear();
                    w.VoxelBatch(pts, 0, pts.size());
                    if (pipe.Write(w.Data(), w.Size())) ++sent;
                    else { Log("pipe: broke -- retrying"); pipe.Disconnect(); }
                }
            }
        }
        if (now - statsAt >= 5000)
        {
            const float s = (now - statsAt) / 1000.0f;
            Log("decoded %.1f/s, sent %.1f/s, dropped %llu | last %llu voxels | auto-exposure %+.2f stops | pipe %s",
                decoded / s, sent / s, (unsigned long long)dropped, (unsigned long long)lastCount, std::log2(autoExp),
                pipe.IsConnected() ? "up" : "down");
            decoded = sent = dropped = 0;
            statsAt = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // The game has ended, and with it the mod's hold on the shared file: it goes, so it is there only while they run.
    if (view) { UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(file); }
    if (game) DeleteFileA(facesPath.c_str());
    for (auto& f : faces) f.Release();
    dec.Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    return 0;
}
