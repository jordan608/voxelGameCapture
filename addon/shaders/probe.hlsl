// probe.hlsl - the face-code handshake on D3D12: one texel, (0,0), of a candidate render target into slot
// `Slot` of a small float texture that is copied back. rig.lua clears each face's target to a code
// before its first capture; this reads it. A compute read, rather than a copy, so the target is only
// ever read the way the decode reads it (as a shader resource) and its state assumption is the same.
cbuffer ProbeConstants : register(b0)
{
    uint  Slot;
    uint3 _pad;
};

Texture2D<float4>   Src : register(t0);
RWTexture2D<float4> Out : register(u0);

[numthreads(1, 1, 1)]
void MainCS()
{
    Out[uint2(Slot, 0)] = Src.Load(int3(0, 0, 0));
}
