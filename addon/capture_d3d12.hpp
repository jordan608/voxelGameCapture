// capture_d3d12.hpp - the GPU half on D3D12: the same two compute passes as capture_gpu.hpp's Decoder,
// for games that run only on D3D12 (Black Myth: Wukong, Oblivion Remastered; The Ascent can run either,
// which is how this is checked in a game on a machine without the others).
//
// Plain D3D12, its own everything: root signature, shader-visible descriptor heap, command allocator and
// list per ring slot, fence. It is executed on the game's own graphics queue at present, after the
// frame's work, so the captures it reads are finished; nothing is recorded into the game's lists.
//
// ONE ASSUMPTION, NOT VERIFIABLE FROM HERE: the game leaves each face's render target in a state a
// compute shader may read (a shader-resource state) after the capture. Unreal's RHI transitions a
// TextureRenderTarget2D to a readable state after rendering into it, because materials sample it next;
// this reads it without a transition, which would corrupt the RHI's own state tracking. If a face
// reads as garbage on a D3D12 title, this is the first suspect.
#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>

#include "capture_gpu.hpp"
#include "obj/probe_cs.h"   // g_probeCS - fxc /T cs_5_0, from shaders/probe.hlsl

namespace vxcap
{
    class Decoder12
    {
    public:
        static constexpr int kRing = 3;
        static constexpr int kCompactW = 2048;
        static constexpr int kProbeMax = 16;
        static constexpr int kPerSlot = 42;   // 6 face SRVs, pack SRV, pack+counter UAVs, compact+counter+merge UAVs, 6 surface SRVs, 6 depth SRVs, 6 player SRVs, 6 solid SRVs, 6 glow SRVs

        bool Init(ID3D12Device* dev, ID3D12CommandQueue* queue, std::string& err)
        {
            Release();
            _dev = dev; _queue = queue;
            _dev->AddRef(); _queue->AddRef();

            // Root signature: b0 root constants (the larger cbuffer), a table of one SRV at t0, a table of three UAVs
            // at u0-u2, a table of one SRV at t2 (SURFACE COLOUR: the face's surface-colour capture) and one at t1
            // (REDENGINE3: the frame's depth), one at t3 (PLAYER LOOK: the face's player capture), one at t4 (SOLID
            // SURFACES: the face's solid capture) and one at t5 (their glow capture). The same for every shader; only
            // compact uses u2 (the merge), only the surface-colour pack reads t2, only the depth-buffer pack t1, and the pack
            // t3, t4 and t5 only with their PlayerMask bits.
            D3D12_DESCRIPTOR_RANGE srv{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
            D3D12_DESCRIPTOR_RANGE uav{ D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
            D3D12_DESCRIPTOR_RANGE surf{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
            D3D12_DESCRIPTOR_RANGE dep{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
            D3D12_DESCRIPTOR_RANGE play{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
            D3D12_DESCRIPTOR_RANGE solid{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
            D3D12_DESCRIPTOR_RANGE glow{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
            D3D12_ROOT_PARAMETER p[8]{};
            p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            p[0].Constants = { 0, 0, (UINT)(sizeof(PackConstants) / 4) };   // the larger cbuffer, 56 dwords (of the 64 a root holds)
            p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[1].DescriptorTable = { 1, &srv };
            p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[2].DescriptorTable = { 1, &uav };
            p[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[3].DescriptorTable = { 1, &surf };
            p[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[4].DescriptorTable = { 1, &dep };
            p[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[5].DescriptorTable = { 1, &play };
            p[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            p[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[6].DescriptorTable = { 1, &solid };
            p[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            p[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[7].DescriptorTable = { 1, &glow };
            p[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            D3D12_ROOT_SIGNATURE_DESC rs{ 8, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE };
            ID3DBlob* blob = nullptr; ID3DBlob* blobErr = nullptr;
            if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &blobErr)))
            {
                err = "root signature: " + std::string(blobErr ? (const char*)blobErr->GetBufferPointer() : "?");
                if (blobErr) blobErr->Release();
                return false;
            }
            HRESULT hr = _dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&_root));
            blob->Release();
            if (FAILED(hr)) { err = "create root signature"; return false; }

            if (!Pipeline(g_packCS, sizeof(g_packCS), &_packPso, err)) return false;
            if (!Pipeline(g_packSurfaceCS, sizeof(g_packSurfaceCS), &_packSurfacePso, err)) return false;
            if (!Pipeline(g_packDepthCS, sizeof(g_packDepthCS), &_packDepthPso, err)) return false;
            if (!Pipeline(g_compactCS, sizeof(g_compactCS), &_compactPso, err)) return false;
            if (!Pipeline(g_probeCS, sizeof(g_probeCS), &_probePso, err)) return false;

            D3D12_DESCRIPTOR_HEAP_DESC hd{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, (UINT)(kRing * kPerSlot + kProbeMax + 3),
                                           D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
            if (FAILED(_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&_heap)))) { err = "descriptor heap"; return false; }
            hd.NumDescriptors = 4; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;   // CPU copies for the UAV clears
            if (FAILED(_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&_cpuHeap)))) { err = "cpu heap"; return false; }
            _inc = _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            if (FAILED(_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)))) { err = "fence"; return false; }

            if (!Tex(4, 1, DXGI_FORMAT_R32_UINT, &_counter, err)) return false;
            if (!Tex(kProbeMax, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, &_probeOut, err)) return false;
            if (!Readback(256, &_probeRb, err)) return false;
            for (auto& s : _ring)
            {
                if (FAILED(_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.alloc)))) { err = "allocator"; return false; }
                if (FAILED(_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.alloc, nullptr, IID_PPV_ARGS(&s.list)))) { err = "list"; return false; }
                s.list->Close();
                if (!Readback(256, &s.counterRb, err)) return false;
            }
            if (FAILED(_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_probeAlloc)))) { err = "allocator"; return false; }
            if (FAILED(_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _probeAlloc, nullptr, IID_PPV_ARGS(&_probeList)))) { err = "list"; return false; }
            _probeList->Close();

            // Descriptors that never change: the counter's UAVs in every slot, the probe's output and a null UAV.
            // Every UAV table is three wide, so every third entry exists: the pack's u2 is the compact UAV after it, the
            // probe's u1 and u2 are null.
            for (int s = 0; s < kRing; s++)
            {
                Uav(_counter, DXGI_FORMAT_R32_UINT, Cpu(s * kPerSlot + 8));
                Uav(_counter, DXGI_FORMAT_R32_UINT, Cpu(s * kPerSlot + 10));
            }
            Uav(_counter, DXGI_FORMAT_R32_UINT, CpuClear(1));
            Uav(_probeOut, DXGI_FORMAT_R32G32B32A32_FLOAT, Cpu(ProbeBase() + kProbeMax));
            Uav(nullptr, DXGI_FORMAT_R32G32B32A32_FLOAT, Cpu(ProbeBase() + kProbeMax + 1));
            Uav(nullptr, DXGI_FORMAT_R32G32B32A32_FLOAT, Cpu(ProbeBase() + kProbeMax + 2));
            return MergeGrid(kMergeRows, err);
        }

        // The merge grid, at least this many rows: made at Init, remade larger when a finer grid is asked for. A remake
        // waits for the GPU first - the old grid may be in a list in flight - and drops what was in flight.
        bool MergeGrid(int rows, std::string& err)
        {
            if (rows <= _mergeRows && _occupied) return true;
            WaitIdle();
            SafeRelease(_occupied); _mergeRows = 0;
            if (!Tex(kMergeW, rows, DXGI_FORMAT_R32_UINT, &_occupied, err)) return false;
            for (int s = 0; s < kRing; s++) Uav(_occupied, DXGI_FORMAT_R32_UINT, Cpu(s * kPerSlot + 11));
            Uav(_occupied, DXGI_FORMAT_R32_UINT, CpuClear(3));
            _mergeRows = rows;
            return true;
        }

        bool Configure(int faceRes, std::string& err)
        {
            if (faceRes == _faceRes && _pack) return true;
            WaitIdle();
            ReleaseSized();
            _faceRes = faceRes;
            if (!Tex(6 * faceRes, 2 * faceRes, DXGI_FORMAT_R32_UINT, &_pack, err)) return false;
            _rows = CompactRows(faceRes, kCompactW);   // every pixel of the faces, up to kMaxVoxels
            if (!Tex(kCompactW, _rows, DXGI_FORMAT_R32G32B32A32_FLOAT, &_compact, err)) return false;
            for (int s = 0; s < kRing; s++)
            {
                if (!Readback((UINT64)kCompactW * 16 * _rows, &_ring[s].compactRb, err)) return false;
                D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
                sd.Format = DXGI_FORMAT_R32_UINT;
                sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                sd.Texture2D.MipLevels = 1;
                _dev->CreateShaderResourceView(_pack, &sd, Cpu(s * kPerSlot + 6));
                Uav(_pack, DXGI_FORMAT_R32_UINT, Cpu(s * kPerSlot + 7));
                Uav(_compact, DXGI_FORMAT_R32G32B32A32_FLOAT, Cpu(s * kPerSlot + 9));
            }
            Uav(_pack, DXGI_FORMAT_R32_UINT, CpuClear(0));
            return true;
        }

        uint32_t Capacity() const { return (uint32_t)(kCompactW * _rows); }

        // REDENGINE3: whole game frames rather than the rig's own targets - each face's colour and depth copies, in
        // these formats, with its pack constants made by the caller from the camera the frame was drawn with
        // (sweep_red3.hpp), every face activeRows tall.
        struct Frames
        {
            ID3D12Resource* depth[6] = {};
            DXGI_FORMAT colourFormat = DXGI_FORMAT_UNKNOWN, depthFormat[6] = {};   // each face's depth, as its view reads it
            PackConstants pack[6] = {};
            int activeRows = 0;
        };

        // Pack each face given, compact, and copy into the next free ring slot's readback; executed now on the
        // game's queue. Never waits: no free slot means this volume is skipped. surfaces: each face's surface-colour
        // capture, used when every face given has one and UseSurface; otherwise the lit colour alone. frames: whole
        // game frames instead (REDENGINE3), the faces given their colour. players: each face's player capture (PLAYER
        // LOOK), used when every face given has one and UsePlayer; solids each face's solid capture (SOLID SURFACES), the
        // same with UseSolid; glows their glow captures, the same with UseGlow.
        bool Run(ID3D12Resource* const faces[6], const Settings& c, const Geometry& g, float exposure, float keep,
                 ID3D12Resource* const surfaces[6] = nullptr, const Frames* frames = nullptr,
                 ID3D12Resource* const players[6] = nullptr, ID3D12Resource* const solids[6] = nullptr,
                 ID3D12Resource* const glows[6] = nullptr)
        {
            // First, before a slot is taken: growing the merge grid waits for the GPU and forgets what was in flight.
            CompactConstants q = CompactFor(c, g, Capacity(), keep);
            if (frames) q.ActiveRows = (uint32_t)frames->activeRows;
            if (q.MergeGrid && MergeRows(q.MergeGrid, q.MergeGridZ) > _mergeRows)
            {
                std::string gridErr;
                if (!MergeGrid(MergeRows(q.MergeGrid, q.MergeGridZ), gridErr)) q.MergeGrid = 0;   // unmerged, not stopped
            }

            Slot* slot = nullptr;
            int si = 0;
            for (; si < kRing; si++) if (_ring[si].state == 0) { slot = &_ring[si]; break; }
            if (!slot || !_pack) { ++dropped; return false; }
            const int base = si * kPerSlot;

            bool surface = UseSurface(c) && surfaces && !frames;
            for (int v = 0; v < 6; v++) if (surface && c.faceOn[v] && faces[v] && !surfaces[v]) surface = false;
            bool player = UsePlayer(c) && players && !frames;
            for (int v = 0; v < 6; v++) if (player && c.faceOn[v] && faces[v] && !players[v]) player = false;
            bool solid = UseSolid(c) && solids && !frames;
            for (int v = 0; v < 6; v++) if (solid && c.faceOn[v] && faces[v] && !solids[v]) solid = false;
            bool glow = solid && UseGlow(c) && glows;
            for (int v = 0; v < 6; v++) if (glow && c.faceOn[v] && faces[v] && !glows[v]) glow = false;
            for (int v = 0; v < 6; v++)
            {
                if (!c.faceOn[v] || !faces[v] || (frames && (!frames->depth[v] || !frames->depthFormat[v]))) continue;
                if (frames)
                {
                    FaceSrv(faces[v], Cpu(base + v), frames->colourFormat);
                    FaceSrv(frames->depth[v], Cpu(base + 18 + v), frames->depthFormat[v]);
                }
                else FaceSrv(faces[v], Cpu(base + v));
                if (surface) FaceSrv(surfaces[v], Cpu(base + 12 + v));
                if (player) FaceSrv(players[v], Cpu(base + 24 + v));
                if (solid) FaceSrv(solids[v], Cpu(base + 30 + v));
                if (glow) FaceSrv(glows[v], Cpu(base + 36 + v));
            }
            // Held until the GPU is done with them: the game can free a capture target (a rig rebuild, a level change)
            // while this list is still in flight, and reading freed memory removes the device.
            for (int v = 0; v < 6; v++)
            {
                SafeRelease(slot->held[v]); SafeRelease(slot->held[6 + v]); SafeRelease(slot->held[12 + v]); SafeRelease(slot->held[18 + v]);
                SafeRelease(slot->held[24 + v]);
                if (!c.faceOn[v] || !faces[v] || (frames && (!frames->depth[v] || !frames->depthFormat[v]))) continue;
                faces[v]->AddRef(); slot->held[v] = faces[v];
                if (surface) { surfaces[v]->AddRef(); slot->held[6 + v] = surfaces[v]; }
                if (frames) { frames->depth[v]->AddRef(); slot->held[6 + v] = frames->depth[v]; }
                if (player) { players[v]->AddRef(); slot->held[12 + v] = players[v]; }
                if (solid) { solids[v]->AddRef(); slot->held[18 + v] = solids[v]; }
                if (glow) { glows[v]->AddRef(); slot->held[24 + v] = glows[v]; }
            }

            slot->alloc->Reset();
            ID3D12GraphicsCommandList* l = slot->list;
            l->Reset(slot->alloc, nullptr);
            l->SetDescriptorHeaps(1, &_heap);
            const UINT zero[4] = { 0, 0, 0, 0 };
            l->ClearUnorderedAccessViewUint(Gpu(base + 7), CpuClear(0), _pack, zero, 0, nullptr);
            l->ClearUnorderedAccessViewUint(Gpu(base + 8), CpuClear(1), _counter, zero, 0, nullptr);
            UavBarrier(l, nullptr);

            l->SetComputeRootSignature(_root);
            l->SetPipelineState(frames ? _packDepthPso : surface ? _packSurfacePso : _packPso);
            l->SetComputeRootDescriptorTable(2, Gpu(base + 7));          // pack UAV, counter UAV
            for (int v = 0; v < 6; v++)
            {
                if (!c.faceOn[v] || !faces[v] || (frames && (!frames->depth[v] || !frames->depthFormat[v]))) continue;
                PackConstants p = frames ? frames->pack[v] : FaceConstants(v, c, g, exposure, true);
                if (!frames) p.PlayerMask = (player ? 1u : 0u) | (solid ? 2u : 0u) | (glow ? 4u : 0u);
                p.SolidOpacity = c.solidOpacity;
                // REDENGINE3: the copies were left as copy destinations; read as shader resources, then given back.
                if (frames)
                {
                    Transition(l, faces[v], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    Transition(l, frames->depth[v], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }
                l->SetComputeRoot32BitConstants(0, (UINT)(sizeof(p) / 4), &p, 0);
                l->SetComputeRootDescriptorTable(1, Gpu(base + v));
                l->SetComputeRootDescriptorTable(3, Gpu(base + (surface ? 12 + v : v)));   // unread without the surface
                l->SetComputeRootDescriptorTable(4, Gpu(base + (frames ? 18 + v : v)));   // unread without the depth
                l->SetComputeRootDescriptorTable(5, Gpu(base + (player ? 24 + v : v)));   // unread without the player
                l->SetComputeRootDescriptorTable(6, Gpu(base + (solid ? 30 + v : v)));    // unread without the solid capture
                l->SetComputeRootDescriptorTable(7, Gpu(base + (glow ? 36 + v : v)));     // unread without the glow capture
                l->Dispatch((p.FaceRes + 7) / 8, (p.ActiveH + 7) / 8, 1);
                if (frames)
                {
                    Transition(l, faces[v], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                    Transition(l, frames->depth[v], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                }
            }
            Transition(l, _pack, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            UavBarrier(l, _counter);

            if (q.MergeGrid)
            {
                l->ClearUnorderedAccessViewUint(Gpu(base + 11), CpuClear(3), _occupied, zero, 0, nullptr);
                UavBarrier(l, _occupied);
            }
            l->SetPipelineState(_compactPso);
            l->SetComputeRoot32BitConstants(0, (UINT)(sizeof(q) / 4), &q, 0);
            l->SetComputeRootDescriptorTable(1, Gpu(base + 6));          // pack SRV
            l->SetComputeRootDescriptorTable(3, Gpu(base + 6));          // set, though compact does not read t2
            l->SetComputeRootDescriptorTable(4, Gpu(base + 6));          // ... nor t1
            l->SetComputeRootDescriptorTable(5, Gpu(base + 6));          // ... nor t3
            l->SetComputeRootDescriptorTable(6, Gpu(base + 6));          // ... nor t4
            l->SetComputeRootDescriptorTable(7, Gpu(base + 6));          // ... nor t5
            l->SetComputeRootDescriptorTable(2, Gpu(base + 9));          // compact UAV, counter UAV, merge UAV
            l->Dispatch((6 * c.faceRes + 7) / 8, (c.faceRes + 7) / 8, 1);

            Transition(l, _compact, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            Transition(l, _counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            slot->rows = RowsToCopy(_haveCount, _lastCount, _rows, kCompactW);
            CopyToReadback(l, _compact, slot->compactRb, DXGI_FORMAT_R32G32B32A32_FLOAT, kCompactW, (UINT)slot->rows, kCompactW * 16);
            CopyToReadback(l, _counter, slot->counterRb, DXGI_FORMAT_R32_UINT, 4, 1, 256);
            Transition(l, _compact, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Transition(l, _counter, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Transition(l, _pack, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (FAILED(l->Close())) { ++dropped; return false; }

            ID3D12CommandList* lists[] = { l };
            _queue->ExecuteCommandLists(1, lists);
            _queue->Signal(_fence, ++_fenceValue);
            slot->value = _fenceValue;
            slot->state = 1;
            slot->seq = ++_seq;
            return true;
        }

        // merged: how many the compact pass merged into a cell another face had already lit.
        int Poll(std::vector<Voxel>& out, float& meanLum, uint32_t& merged)
        {
            Slot* slot = nullptr;
            for (auto& s : _ring) if (s.state == 1 && (!slot || s.seq < slot->seq)) slot = &s;
            if (!slot || _fence->GetCompletedValue() < slot->value) return -1;

            void* p = nullptr;
            D3D12_RANGE r{ 0, 16 };
            if (FAILED(slot->counterRb->Map(0, &r, &p))) { for (auto& h : slot->held) SafeRelease(h); slot->state = 0; return -1; }
            const uint32_t* ctr = (const uint32_t*)p;
            _lastCount = std::min(ctr[0], Capacity()); _haveCount = true;
            const uint32_t count = std::min(_lastCount, (uint32_t)(slot->rows * kCompactW));
            if (count < _lastCount) ++truncated;
            meanLum = ctr[2] ? std::pow(2.0f, (float)ctr[1] / (float)ctr[2] / 64.0f - 16.0f) : 0.0f;
            merged = ctr[3];
            D3D12_RANGE none{ 0, 0 };
            slot->counterRb->Unmap(0, &none);

            out.resize(count);
            if (count)
            {
                r = { 0, (SIZE_T)count * sizeof(Voxel) };
                if (FAILED(slot->compactRb->Map(0, &r, &p))) { for (auto& h : slot->held) SafeRelease(h); slot->state = 0; return -1; }
                memcpy(out.data(), p, (size_t)count * sizeof(Voxel));   // rows are exactly 2048 x 16 bytes: contiguous
                slot->compactRb->Unmap(0, &none);
            }
            for (auto& h : slot->held) SafeRelease(h);
            slot->state = 0;
            return (int)count;
        }

        // The handshake: texel (0,0) of up to kProbeMax textures, read on the GPU and copied back. One
        // submission at a time; ProbeResult hands the values over once it has finished.
        bool ProbeSubmit(ID3D12Resource* const tex[], int n)
        {
            if (_probeBusy || n <= 0) return false;
            n = std::min(n, kProbeMax);
            for (int i = 0; i < n; i++) FaceSrv(tex[i], Cpu(ProbeBase() + i));
            for (int i = 0; i < n; i++) { tex[i]->AddRef(); _probeHeld[i] = tex[i]; }   // as Run's: until the probe is done
            _probeAlloc->Reset();
            ID3D12GraphicsCommandList* l = _probeList;
            l->Reset(_probeAlloc, nullptr);
            l->SetDescriptorHeaps(1, &_heap);
            l->SetComputeRootSignature(_root);
            l->SetPipelineState(_probePso);
            l->SetComputeRootDescriptorTable(2, Gpu(ProbeBase() + kProbeMax));
            for (int i = 0; i < n; i++)
            {
                const uint32_t slot[4] = { (uint32_t)i, 0, 0, 0 };
                l->SetComputeRoot32BitConstants(0, 4, slot, 0);
                l->SetComputeRootDescriptorTable(1, Gpu(ProbeBase() + i));
                l->SetComputeRootDescriptorTable(3, Gpu(ProbeBase() + i));   // set, though the probe does not read t2
                l->SetComputeRootDescriptorTable(4, Gpu(ProbeBase() + i));   // ... nor t1
                l->Dispatch(1, 1, 1);
            }
            Transition(l, _probeOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            CopyToReadback(l, _probeOut, _probeRb, DXGI_FORMAT_R32G32B32A32_FLOAT, kProbeMax, 1, 256);
            Transition(l, _probeOut, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (FAILED(l->Close())) { for (auto& h : _probeHeld) SafeRelease(h); return false; }
            ID3D12CommandList* lists[] = { l };
            _queue->ExecuteCommandLists(1, lists);
            _queue->Signal(_fence, ++_fenceValue);
            _probeValue = _fenceValue;
            _probeN = n;
            _probeBusy = true;
            return true;
        }

        bool ProbeResult(float out[kProbeMax][4], int& n)
        {
            if (!_probeBusy || _fence->GetCompletedValue() < _probeValue) return false;
            void* p = nullptr;
            D3D12_RANGE r{ 0, 256 };
            if (SUCCEEDED(_probeRb->Map(0, &r, &p)))
            {
                memcpy(out, p, sizeof(float) * 4 * kProbeMax);
                D3D12_RANGE none{ 0, 0 };
                _probeRb->Unmap(0, &none);
            }
            n = _probeN;
            for (auto& h : _probeHeld) SafeRelease(h);
            _probeBusy = false;
            return true;
        }

        bool ProbeBusy() const { return _probeBusy; }

        void Release()
        {
            WaitIdle();
            ReleaseSized();
            for (auto& s : _ring) { SafeRelease(s.list); SafeRelease(s.alloc); SafeRelease(s.counterRb); for (auto& h : s.held) SafeRelease(h); s.state = 0; }
            for (auto& h : _probeHeld) SafeRelease(h);
            SafeRelease(_probeList); SafeRelease(_probeAlloc); SafeRelease(_probeRb); SafeRelease(_probeOut);
            SafeRelease(_counter); SafeRelease(_occupied); _mergeRows = 0; SafeRelease(_fence);
            SafeRelease(_heap); SafeRelease(_cpuHeap);
            SafeRelease(_packPso); SafeRelease(_packSurfacePso); SafeRelease(_packDepthPso); SafeRelease(_compactPso); SafeRelease(_probePso); SafeRelease(_root);
            SafeRelease(_queue); SafeRelease(_dev);
            _probeBusy = false;
        }

        ~Decoder12() { Release(); }

        uint64_t dropped = 0, truncated = 0;

    private:
        struct Slot
        {
            ID3D12CommandAllocator* alloc = nullptr;
            ID3D12GraphicsCommandList* list = nullptr;
            ID3D12Resource* compactRb = nullptr;
            ID3D12Resource* counterRb = nullptr;
            UINT64 value = 0;
            int state = 0;
            uint64_t seq = 0;
            int rows = 0;                   // compact rows copied back
            ID3D12Resource* held[30] = {};  // the faces (and surface, player, solid and glow captures) this slot's list reads, until it completes
        };

        ID3D12Device* _dev = nullptr;
        ID3D12CommandQueue* _queue = nullptr;
        ID3D12RootSignature* _root = nullptr;
        ID3D12PipelineState* _packPso = nullptr; ID3D12PipelineState* _compactPso = nullptr; ID3D12PipelineState* _probePso = nullptr;
        ID3D12PipelineState* _packSurfacePso = nullptr; ID3D12PipelineState* _packDepthPso = nullptr;
        ID3D12DescriptorHeap* _heap = nullptr; ID3D12DescriptorHeap* _cpuHeap = nullptr;
        UINT _inc = 0;
        ID3D12Fence* _fence = nullptr;
        UINT64 _fenceValue = 0;
        ID3D12Resource* _pack = nullptr; ID3D12Resource* _compact = nullptr; ID3D12Resource* _counter = nullptr;
        ID3D12Resource* _occupied = nullptr;
        int _mergeRows = 0;
        uint32_t _lastCount = 0; bool _haveCount = false;
        ID3D12Resource* _probeOut = nullptr; ID3D12Resource* _probeRb = nullptr;
        ID3D12CommandAllocator* _probeAlloc = nullptr; ID3D12GraphicsCommandList* _probeList = nullptr;
        UINT64 _probeValue = 0; int _probeN = 0; bool _probeBusy = false;
        ID3D12Resource* _probeHeld[kProbeMax] = {};
        Slot _ring[kRing];
        int _faceRes = 0, _rows = 0;
        uint64_t _seq = 0;

        template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

        static int ProbeBase() { return kRing * kPerSlot; }
        D3D12_CPU_DESCRIPTOR_HANDLE Cpu(int i) const { auto h = _heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += (SIZE_T)i * _inc; return h; }
        D3D12_GPU_DESCRIPTOR_HANDLE Gpu(int i) const { auto h = _heap->GetGPUDescriptorHandleForHeapStart(); h.ptr += (UINT64)i * _inc; return h; }
        D3D12_CPU_DESCRIPTOR_HANDLE CpuClear(int i) const { auto h = _cpuHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += (SIZE_T)i * _inc; return h; }

        void WaitIdle()
        {
            if (!_fence || !_queue) return;
            _queue->Signal(_fence, ++_fenceValue);
            if (_fence->GetCompletedValue() < _fenceValue)
            {
                HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (e && SUCCEEDED(_fence->SetEventOnCompletion(_fenceValue, e))) WaitForSingleObject(e, 2000);
                if (e) CloseHandle(e);
            }
            // Whatever was in flight is finished and forgotten: its references go with it, or the game's targets leak.
            for (auto& s : _ring) { for (auto& h : s.held) SafeRelease(h); s.state = 0; }
            for (auto& h : _probeHeld) SafeRelease(h);
            _probeBusy = false;
        }

        void ReleaseSized()
        {
            for (auto& s : _ring) { SafeRelease(s.compactRb); s.state = 0; }
            SafeRelease(_pack); SafeRelease(_compact);
            _faceRes = 0; _haveCount = false;
        }

        bool Pipeline(const void* code, size_t size, ID3D12PipelineState** out, std::string& err)
        {
            D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
            d.pRootSignature = _root;
            d.CS = { code, size };
            if (FAILED(_dev->CreateComputePipelineState(&d, IID_PPV_ARGS(out)))) { err = "compute pipeline"; return false; }
            return true;
        }

        bool Tex(int w, int h, DXGI_FORMAT f, ID3D12Resource** out, std::string& err)
        {
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
            D3D12_RESOURCE_DESC d{};
            d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            d.Width = (UINT64)w; d.Height = (UINT)h; d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = f;
            d.SampleDesc.Count = 1; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (FAILED(_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                     nullptr, IID_PPV_ARGS(out))))
            { err = "texture " + std::to_string(w) + "x" + std::to_string(h); return false; }
            return true;
        }

        bool Readback(UINT64 bytes, ID3D12Resource** out, std::string& err)
        {
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_READBACK };
            D3D12_RESOURCE_DESC d{};
            d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = DXGI_FORMAT_UNKNOWN;
            d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST,
                                                     nullptr, IID_PPV_ARGS(out))))
            { err = "readback " + std::to_string(bytes); return false; }
            return true;
        }

        void Uav(ID3D12Resource* r, DXGI_FORMAT f, D3D12_CPU_DESCRIPTOR_HANDLE h)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
            u.Format = f;
            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            _dev->CreateUnorderedAccessView(r, nullptr, &u, h);
        }

        // The game's target, read as float whatever its typeless or typed format (Unreal's are RGBA16F).
        void FaceSrv(ID3D12Resource* r, D3D12_CPU_DESCRIPTOR_HANDLE h, DXGI_FORMAT f = DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = f;
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2D.MipLevels = 1;
            _dev->CreateShaderResourceView(r, &sd, h);
        }

        static void Transition(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
        {
            D3D12_RESOURCE_BARRIER br{};
            br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            br.Transition = { r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b };
            l->ResourceBarrier(1, &br);
        }

        static void UavBarrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r)
        {
            D3D12_RESOURCE_BARRIER br{};
            br.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            br.UAV.pResource = r;
            l->ResourceBarrier(1, &br);
        }

        static void CopyToReadback(ID3D12GraphicsCommandList* l, ID3D12Resource* src, ID3D12Resource* dst, DXGI_FORMAT f,
                                   UINT w, UINT h, UINT rowPitch)
        {
            D3D12_TEXTURE_COPY_LOCATION s{}, d{};
            s.pResource = src; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
            d.pResource = dst; d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            d.PlacedFootprint.Offset = 0;
            d.PlacedFootprint.Footprint = { f, w, h, 1, rowPitch };
            const D3D12_BOX box{ 0, 0, 0, w, h, 1 };   // the top h rows: RowsToCopy's, or the whole texture
            l->CopyTextureRegion(&d, 0, 0, 0, &s, &box);
        }
    };
}
