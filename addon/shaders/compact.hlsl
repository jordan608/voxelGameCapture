// compact.hlsl - PORTED from Voxel Photonics' VLED Volume Capture plugin, VoxelStudioCompact.usf
// (see ../PORTED.md). Body unchanged but for the additions marked DISPLAY, MERGE, PERSPECTIVE, SCALE, REDENGINE3 and PLAYER LOOK;
// the engine include is gone and the loose parameters are an explicit constant buffer.
//
// Second-pass compute shader: reads the R32_UINT packed output texture produced
// by pack.hlsl, decodes every non-empty pixel back to world-space voxel coordinates
// (metres, relative to the volume centre, display +Z = physically DOWN), and writes
// float4(vx, vy, vz, asfloat(0x00RRGGBB)) into a flat RGBA32F compact buffer using
// atomic slot allocation.
//
// Output format (CompactTex — RWTexture2D<float4>, 2048 wide × MaxVoxels/2048 tall):
//   CompactTex[uint2(i%2048, i/2048)] = float4(display x, y, z, asfloat(0x00RRGGBB))   -- DISPLAY: was metres
//   Counter[uint2(0,0)] after dispatch = total voxel count.
//
// Dispatch: ceil(6*FaceRes/8) × ceil(FaceRes/8) × 1 groups.

cbuffer CompactConstants : register(b0)   // layout mirrors CompactConstants in voxon_capture_ue.cpp
{
    uint  FaceRes;      // pixels per face side (256 or 512)
    float BoundR;       // lateral extent, metres (2.0 = VX2 / 4.0 = VX2-XL, × world scale)
    float BoundZ;       // vertical extent, metres
    uint  VX2XL;        // 1 if VX2-XL format (side-face active height = FaceRes/2)

    uint  MaxVoxels;    // capacity of CompactTex (padded to safe limit)
    float KeepFraction; // 0..1 even-decimation keep probability ("Density" slider)
    float SideTiltRad;  // the 4 side cameras are pitched down by this (radians); 0 = axis-aligned
    uint  MergeGrid;    // MERGE: display cells across the volume's box (0 = no merge)

    float DispXY;       // DISPLAY: metres -> display units, across (boundR / BoundR)
    float DispZ;        // DISPLAY: and up (boundZ / BoundZ)
    float YawCos;       // DISPLAY: the rig's yaw, turned back: the compact pass works in the rig's frame
    float YawSin;
    float MergePer;     // MERGE: display cells a display unit (56 on a VX2-XL)
    float MergeOffXY;   // MERGE: display cells from the box's edge to its centre
    float MergeOffZ;
    uint  MergeGridZ;

    uint  Perspective;  // PERSPECTIVE: the pack pass stored positions, 11:11:10 over these ranges (m)
    float QuantX;
    float QuantZ;
    float QuantY;
    float ScaleX;       // SCALE: each of the rig's axes back into the display's box: above 1 world was squashed in
    float ScaleY;
    float ScaleZ;
    uint  ActiveRows;   // REDENGINE3: every face this many rows (a swept game frame); 0: the display's shape below
    uint  PlayerDepth;  // PLAYER LOOK: each of the player's voxels drawn this many deep (1: as it is), a display cell
    float CellStep;     // (this, in display units) at a time along its face's line of sight, into the player's body
    uint  PadC0;
    uint  PadC1;
    float4 PlayerDir[6]; // each face's line of sight, display units
};

Texture2D<uint>     PackTex    : register(t0);
RWTexture2D<float4> CompactTex : register(u0);
RWTexture2D<uint>   Counter    : register(u1);   // [0] voxels kept; [3] MERGE: voxels merged away
RWTexture2D<uint>   Occupied   : register(u2);   // MERGE: one bit per display cell, 2048 words a row

[numthreads(8, 8, 1)]
void MainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint gx = DTid.x;   // global column (0 .. 6*FaceRes - 1)
    uint gy = DTid.y;   // row in the colour half (0 .. FaceRes - 1)

    if (gx >= 6u * FaceRes || gy >= FaceRes) return;

    uint face = gx / FaceRes;
    uint col  = gx % FaceRes;
    uint row  = gy;

    // VX2-XL side faces use only the top half of each slot (§3.2).
    uint activeH = ActiveRows != 0u ? ActiveRows : (VX2XL != 0u && face < 4u) ? (FaceRes / 2u) : FaceRes;
    if (row >= activeH) return;

    // ── Read colour pixel (top half of pack texture) ───────────────────────
    uint colorPacked = PackTex.Load(int3(gx, row, 0));
    const bool isPlayer = ((colorPacked >> 24u) & 1u) != 0u;   // PLAYER LOOK: the pack pass's flag
    uint R = colorPacked & 0xFFu;
    uint G = (colorPacked >> 8u)  & 0xFFu;
    uint B = (colorPacked >> 16u) & 0xFFu;
    if ((R | G | B) == 0u) return;   // empty pixel (no geometry or sky)

    // ── Read depth pixel (bottom half: row + FaceRes) ──────────────────────
    uint depthPacked = PackTex.Load(int3(gx, FaceRes + row, 0));
    uint D16_hi = depthPacked & 0xFFu;
    uint D16_lo = (depthPacked >> 8u) & 0xFFu;
    uint D16    = (D16_hi << 8u) | D16_lo;

    // ── Decode depth → world-space signed distance along face axis ─────────
    bool  isTopBot = (face >= 4u);
    float bound    = isTopBot ? BoundZ : BoundR;
    float rawDepth = (float)D16 * ((2.0f * bound) / 65535.0f) - bound;

    // ── Lateral positions (column → X/Y, row → Z or row → Y) ─────────────
    float rcpFR1  = 1.0f / (float)(FaceRes  - 1u);
    float rcpAH1  = 1.0f / (float)(activeH  - 1u);

    float lLat  = (float)col * (2.0f * BoundR * rcpFR1) - BoundR;   // col → lateral
    float lZ    = (float)row * (2.0f * BoundZ * rcpAH1) - BoundZ;   // row → Z (sides)
    float lLatR = (float)row * (2.0f * BoundR * rcpFR1) - BoundR;   // row → Y (top/bot)

    // ── Reconstruct (vx, vy, vz) from face index ──────────────────────────
    // VX2 convention: +Z is physically DOWN — world +Z (UE up) maps to display -Z.
    float vx, vy, vz;
    if (Perspective != 0u)
    {
        // PERSPECTIVE: placed already, along the pixel's ray, by the pack pass.
        vx = ((float)(depthPacked & 2047u) / 2047.0f * 2.0f - 1.0f) * QuantX;
        vy = ((float)((depthPacked >> 11u) & 2047u) / 2047.0f * 2.0f - 1.0f) * QuantY;
        vz = ((float)(depthPacked >> 22u) / 1023.0f * 2.0f - 1.0f) * QuantZ;
    }
    else if (face < 4u)
    {
        const float ct = cos(SideTiltRad), st = sin(SideTiltRad);
        float3 Dvec0, Lvec;
        if      (face == 0u) { Dvec0 = float3(-1, 0, 0); Lvec = float3(0, 1, 0); }
        else if (face == 1u) { Dvec0 = float3( 0,-1, 0); Lvec = float3(1, 0, 0); }
        else if (face == 2u) { Dvec0 = float3( 1, 0, 0); Lvec = float3(0, 1, 0); }
        else                 { Dvec0 = float3( 0, 1, 0); Lvec = float3(1, 0, 0); }
        const float3 Vvec0 = float3(0, 0, -1);
        const float3 Dvec = Dvec0 * ct + float3(0, 0, 1) * st;
        const float3 Vvec = Vvec0 * ct + Dvec0 * st;
        const float halfDepth = BoundR * ct + BoundZ * st;
        const float dc = (float)D16 * (2.0f * halfDepth / 65535.0f) - halfDepth;
        const float vertHalf = BoundR * st + BoundZ * ct;
        const float activeHf = (VX2XL != 0u) ? (float)(FaceRes / 2u) : (float)FaceRes;
        const float halfWin  = max(BoundR, vertHalf * (float)FaceRes / activeHf);
        const float lateral  = (lLat / BoundR) * halfWin;
        const float vertical = (lZ   / BoundZ) * vertHalf;
        const float3 P = dc * Dvec + lateral * Lvec + vertical * Vvec;
        vx = P.x; vy = P.y; vz = P.z;
        // Cull geometry outside the volume (the widened/tilted view pulls in extra scene).
        if (abs(vx) > BoundR * 1.05f || abs(vy) > BoundR * 1.05f || abs(vz) > BoundZ * 1.05f) return;
    }
    else if (face == 4u) { vx = lLat; vy = lLatR; vz =  rawDepth; } // top
    else                 { vx = lLat; vy = lLatR; vz = -rawDepth; } // bottom

    // ── SCALE: each axis back into the display's box (1 when nothing is squashed or stretched) ──
    vx *= ScaleX; vy *= ScaleY; vz *= ScaleZ;

    // ── Reject non-finite reconstructions ─────────────────────────────────
    if (!(isfinite(vx) && isfinite(vy) && isfinite(vz))) return;

    // ── Even spatial decimation ("Density" slider) ────────────────────────
    if (KeepFraction < 0.999f)
    {
        uint3 q = (uint3)((float3(vx, vy, vz) + 8.0f) * 1024.0f); // ~1mm cells, positive
        uint  h = (q.x * 73856093u) ^ (q.y * 19349663u) ^ (q.z * 83492791u);
        h ^= (h >> 13u); h *= 0x5bd1e995u; h ^= (h >> 15u);
        if ((float)(h & 0x00FFFFFFu) * (1.0f / 16777216.0f) >= KeepFraction)
            return;
    }

    // ── DISPLAY: into display units, the rig's yaw turned back ────────────
    // Done here, not per voxel on the CPU: the host takes these as they are.
    const float dx = (vx * YawCos - vy * YawSin) * DispXY;
    const float dy = (vx * YawSin + vy * YawCos) * DispXY;
    const float dz = vz * DispZ;

    // PLAYER LOOK: the player's voxel, and PlayerDepth - 1 more behind it, a display cell apart along its face's line of
    // sight, into the player's body: a thicker, denser, brighter player on an additive display. Each one merges as any.
    const uint copies = (isPlayer && PlayerDepth > 1u) ? PlayerDepth : 1u;
    const uint colorRGB = (R << 16u) | (G << 8u) | B;
    for (uint k = 0u; k < copies; k++)
    {
        const float3 D = float3(dx, dy, dz) + PlayerDir[face].xyz * (CellStep * (float)k);

        // ── MERGE: one voxel per display cell ─────────────────────────────────
        // The display is additive: where two faces see the same surface, both pixels landed in one display
        // voxel and lit it twice, in the sum of their colours. The first to claim the cell's bit keeps it.
        // Cells are the display's own (56 a unit on a VX2-XL). Anything outside the volume's box is left alone.
        if (MergeGrid != 0u)
        {
            float cx = D.x * MergePer + MergeOffXY;
            float cy = D.y * MergePer + MergeOffXY;
            float cz = D.z * MergePer + MergeOffZ;
            if (cx >= 0.0f && cy >= 0.0f && cz >= 0.0f && cx < (float)MergeGrid && cy < (float)MergeGrid && cz < (float)MergeGridZ)
            {
                uint cell = ((uint)cz * MergeGrid + (uint)cy) * MergeGrid + (uint)cx;
                uint word = cell >> 5u, bit = 1u << (cell & 31u), was;
                InterlockedOr(Occupied[uint2(word % 2048u, word / 2048u)], bit, was);
                if ((was & bit) != 0u) { InterlockedAdd(Counter[uint2(3, 0)], 1u); continue; }
            }
        }

        // ── Atomically claim a slot in CompactTex ─────────────────────────────
        uint slot;
        InterlockedAdd(Counter[uint2(0,0)], 1u, slot);
        if (slot >= MaxVoxels) return;   // safeguard against overflow

        // ── Write pre-decoded voxel ───────────────────────────────────────────
        CompactTex[uint2(slot % 2048u, slot / 2048u)] = float4(D, asfloat(colorRGB));   // DISPLAY
    }
}
