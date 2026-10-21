// pack.hlsl - PORTED from Voxel Photonics' VLED Volume Capture plugin, VoxelStudioPack.usf
// (see ../PORTED.md for the source and what changed). The body below MainCS's signature is the
// plugin's but for the additions marked (PERSPECTIVE, UNITY, SURFACE COLOUR, COLOUR RANGE, REDENGINE3/4, LOOK, PLAYER
// LOOK); the engine include is gone and the loose parameters are an explicit constant buffer, because outside Unreal
// nothing else binds them.
//
// Compute shader: reads one combined colour+depth RT for a single cubemap face,
// applies the VoxelStudio §4.3 flip remapping and §5.2 depth encode, and writes
// packed output into a shared R32_UINT texture:
//
//   Output texture layout (6*FaceRes wide, 2*FaceRes tall):
//     Top half  [row 0 .. FaceRes-1]          — colour pixels:  R|G<<8|B<<16
//     Bottom half [row FaceRes .. 2*FaceRes-1] — depth pixels:  D16hi|(D16lo<<8)
//
// Each dispatch processes one face: group grid = ceil(FaceRes/8) x ceil(ActiveH/8).
// Disabled faces are never dispatched (their output columns stay zero from ClearUAV).

// ── per-dispatch constants ──  (layout mirrors PackConstants in voxon_capture_ue.cpp)
cbuffer PackConstants : register(b0)
{
    uint  FaceIndex;    // 0-5
    uint  FaceRes;      // pixels per face side (256 or 512)
    uint  ActiveH;      // filled rows (<= FaceRes; VX2-XL side faces = FaceRes/2)
    uint  MeterEnable;  // 1 = accumulate auto-exposure metering, 0 = skip (manual exposure)

    float CamAxisPos;   // camera distance from actor along face axis (metres, signed)
    float LookSign;     // same sign as CamAxisPos: +1 or -1
    float Bound;        // BOUNDR or BOUNDZ in metres
    float DepthSign;    // +1 for faces 0,1,4 / -1 for faces 2,3,5

    float D_min;        // = -Bound
    float D_scale;      // = 65535 / (2 * Bound)
    float CamDist;      // side faces: camera orbit radius (m) — dc = Z_m - CamDist
    float HalfDepth;    // side faces: volume half-extent along the (tilted) look dir (m)

    float EmptyCutoff;  // sentinel: Z_m >= this means empty sky pixel
    float Exposure;     // linear multiplier applied to HDR colour before tonemap (manual EV)
    uint  FlipX;        // 1 = mirror source columns before sampling
    uint  FlipY;        // 1 = mirror source rows before sampling

    // PERSPECTIVE: the face is an ordinary lens. UE4 drops dynamic lights and shadows in orthographic captures
    // (measured on The Ascent, 2026-10-11: 4.8x darker than a perspective lens, same frame), so the rig can use
    // perspective cameras. Each pixel is then placed along its own ray here, in the compact pass's frame (metres
    // from the volume's centre, display axes, +Z down), and the depth row holds that position packed 11:11:10.
    uint   Perspective;           // 1: the fields below are used
    float  CamD;                  // the camera's distance behind the centre, along Fwd (m)
    float  TanH;                  // tan of half the horizontal field of view
    float  TanV;                  // ... and of the vertical
    float3 Fwd;   float QuantX;   // camera forward; the packed position's range along the rig's X (m)
    float3 Right; float QuantZ;   // image +x; its range up and down (m)
    float3 Up;    float QuantY;   // image -y: rows run down; the range along the rig's Y (SCALE: each axis its own)

    // UNITY (rig/unity), added: depth from the face's own depth buffer (reversed-Z device depth, 1 near, 0 far),
    // turned back into distance by the near and far planes the rig set; and rows stored bottom-up, as Unity renders
    // into render textures on D3D11. Read only by the DEPTH_BUFFER build of this file; the Unreal build ignores them.
    uint  DepthFromBuffer;
    uint  BottomUp;
    float DepthNear;              // metres
    float DepthFar;

    // SURFACE COLOUR (rig: lighting below 1), added: the surfaces' own colours from the base-colour capture made with this
    // face, scaled by SurfaceGain (the app's Brightness), with this much of the lit colour mixed in. Read only by the
    // SURFACE_COLOUR build of this file.
    float Lighting;
    float SurfaceGain;

    // COLOUR RANGE, added: the brightness the display draws 0 and 1 as (the app's Colour range floor and ceiling / 255).
    float ColorFloor;
    float ColorCeiling;

    // REDENGINE3 (rig/redengine3), added. CamRel: the camera's place from the volume's centre in the compact pass's
    // frame (m) - -CamD x Fwd for the rigs that stand a camera on each face's axis, wherever the frame says it was for
    // a swept game camera. DisplayColour: the colour is the game's finished picture, not HDR: scaled, not tone-mapped.
    // SrcStep: every SrcStep-th texel, for a game frame too wide for six across the pack texture.
    // REDENGINE4 (rig/redengine4), added. SrcSize: the face's colour is this many texels square, not FaceRes like its
    // depth - its pictures are told apart by size, a texel or two apart - and is read at the texel nearest each depth
    // texel. 0: as before.
    float3 CamRel;
    uint   DisplayColour;
    uint   SrcStep;
    uint   SrcSize;
    // PLAYER LOOK (an Unreal or Unity rig), added: PlayerMask bit 0, the face's player capture is read (t3) and the player's pixels
    // take PlayerGain on the exposure and the player's gamma and saturation, and are flagged for the compact pass.
    // LOOK, added: the world's gamma and saturation. Kept so that 0 changes nothing: an inverse gamma of 0 is taken for
    // 1, and the saturations are offsets from 1.
    uint   PlayerMask;    // bit 0 PLAYER LOOK (t3); bit 1 SOLID SURFACES, the solid capture's depth (t4); bit 2 their
                          // glow capture (t5), and they are solid only where at least SolidOpacity opaque
    float  PlayerGain;
    float  InvGamma;
    float  SatDelta;
    float  PlayerInvGamma;
    float  PlayerSatDelta;
    float  SolidOpacity;
    // FROSTBITE (frostbite_views.hpp), added: the face's depth is its picture's size too (SrcSize), so it is read at the
    // same nearest texel. 0: texel for texel, as before.
    uint   SrcDepthScaled;
    // CRYENGINE2 (cryengine2_views.hpp), added: the depth is the engine's linear depth target, view depth over the face's
    // far plane (DepthFar). 0: device depth, as before.
    uint   DepthLinear;
};

#ifdef FROSTBITE
// FROSTBITE PLAYER LOOK (PlayerMask bit 3), added: no capture of the player alone, so the player is what stands within
// PlayerRadius of their feet (PlayerAt: metres from the volume's centre, this pass's frame, +Z down) and up to
// PlayerHeight above them - the ground they stand on, and anything beside them farther away, is not. A buffer of its
// own: b0 is as large as the D3D12 root signature holds, and Frostbite is D3D11 here.
cbuffer PlayerShape : register(b1)
{
    float3 PlayerAt;
    float  PlayerRadius;
    float  PlayerHeight;
    float3 PadS;
};
#endif

// ── inputs ───────────────────────────────────────────────────────────────────

// PF_FloatRGBA render target — SCS_SceneColorSceneDepth (single combined pass).
// .x=R, .y=G, .z=B  (lit scene colour, HDR; saturate before quantising)
// .w=A              (linear scene depth, UE cm)
Texture2D<float4> ColorTex : register(t0);
#ifdef DEPTH_BUFFER
Texture2D<float> DepthTex : register(t1);   // UNITY: the camera's depth buffer, read through a single-channel view
#endif
#ifdef SURFACE_COLOUR
Texture2D<float4> SurfaceTex : register(t2); // the face's base-colour capture: the same camera, so the same texel
#endif
// PLAYER LOOK: the face's player capture - the same camera with only the player drawn, its scene depth (cm) in red.
// UNITY: the player camera's own depth buffer instead, device depth as the face's (DEPTH_BUFFER build).
Texture2D<float4> PlayerTex : register(t3);
// SOLID SURFACES: the face's solid capture - the same camera with only the chosen see-through surfaces drawn, made
// opaque, their scene depth (cm) in red, far where none was. Read with PlayerMask bit 1. UNITY: the solid camera's own
// depth buffer, as the player's (DEPTH_BUFFER build).
Texture2D<float4> SolidTex : register(t4);
// ...and their glow capture: the same surfaces again as the game draws them (SCS_SceneColorHDR), in alpha the light
// they let through - 1 where none was drawn. Read with PlayerMask bit 2.
Texture2D<float4> GlowTex : register(t5);

// ── output ───────────────────────────────────────────────────────────────────

// PF_R32_UINT — one uint per pixel.
RWTexture2D<uint> OutputTex : register(u0);

// Shared counter+meter texture (4×1 R32_UINT).  Slot 0 is the compact pass's voxel
// counter; slots 1/2 are auto-exposure metering written here:
//   [1] = sum of fixed-point log2(luminance)   [2] = sample count
// Sampled 1-in-16 (bounds atomic traffic + uint32 overflow).  The CPU reads it back
// to derive next frame's exposure.  Cleared to 0 before this pass runs.
RWTexture2D<uint> Counter : register(u1);

// ── HDR → display tonemap ──────────────────────────────────────────────────────
// SCS_SceneColorSceneDepth gives raw LINEAR HDR scene colour (pre-exposure,
// pre-tonemap).  Outdoor/sunlit scenes have radiance well above 1.0, so the old
// straight saturate() clipped everything to white.  Apply a manual exposure
// (Exposure, an EV multiplier set by the user) then the Narkowicz ACES filmic
// curve so highlights roll off gracefully instead of hard-clipping.
float3 ACESFilm(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void MainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint col = DTid.x;
    uint row = DTid.y;

    // Bounds check — thread groups are rounded up, guard excess threads.
    if (col >= FaceRes || row >= ActiveH) return;

    // §4.3 flip remapping. UNITY: its rows are stored bottom-up, so the flip turns over once more, and viewRow - the
    // texel's row counted from the TOP of the camera's view - is what places it along its ray below.
    uint srcCol = FlipX ? (FaceRes - 1u - col) : col;
    uint srcRow = (FlipY != BottomUp) ? (ActiveH - 1u - row) : row;
    uint viewRow = BottomUp ? (ActiveH - 1u - srcRow) : srcRow;
    const uint step = max(SrcStep, 1u);                 // REDENGINE3: the texel read, every step-th
    const int3 texel = int3(srcCol * step, srcRow * step, 0);
    const int3 colourTexel = SrcSize != 0u ? int3(srcCol * SrcSize / FaceRes, srcRow * SrcSize / ActiveH, 0) : texel;   // REDENGINE4

    // §5.3 — combined texture read: .xyz=scene colour (HDR), .w=linear depth (UE cm)
    float4 c = ColorTex.Load(colourTexel);
#ifdef DEPTH_BUFFER
    // UNITY: reversed-Z device depth to view-space z, in metres. 0 is the far plane (cleared, nothing drawn).
    // REDENGINE3: reversed too, though its projection matrix runs forward (measured: read forward, the faces disagreed
    // on the ground's height by 20 m; read reversed, they agree). Its sky is at 0, 10-20% of a level face.
    float d = DepthTex.Load(SrcDepthScaled != 0u ? colourTexel : texel);
    float Z_m = d <= 0.0 ? 0.0 : DepthLinear != 0u ? d * DepthFar
              : Perspective != 0u ? DepthNear * DepthFar / (DepthNear + d * (DepthFar - DepthNear))
                                  : DepthFar - d * (DepthFar - DepthNear);
#else
    float Z_m = c.w / 100.0;
#endif

    // SOLID SURFACES: a see-through surface writes no depth, so the scene's depth there is whatever is behind it, and
    // its colour would land there. Where the solid capture saw one in front of that (or the face saw nothing), its depth
    // is taken; behind a wall, the wall's stays. The colour stays the face's: the surface's own over what is behind.
    // With their glow capture (bit 2), only where they are at least SolidOpacity opaque: a lightsaber's mesh is wider
    // than its glowing core, which its material fades out toward the mesh's edge.
    const bool opaqueEnough = (PlayerMask & 4u) == 0u || 1.0 - saturate(GlowTex.Load(texel).w) >= SolidOpacity;
    if ((PlayerMask & 2u) != 0u && opaqueEnough)
    {
        float sd = SolidTex.Load(texel).x;
#ifdef DEPTH_BUFFER
        sd = sd > 0.0 ? 100.0 * (Perspective != 0u ? DepthNear * DepthFar / (DepthNear + sd * (DepthFar - DepthNear))
                                                     : DepthFar - sd * (DepthFar - DepthNear)) : 0.0;
#endif
        const float sceneCm = Z_m * 100.0;
        if (sd > 0.0 && sd < EmptyCutoff * 100.0 &&
            (Z_m <= 0.0 || Z_m >= EmptyCutoff || sd < sceneCm - max(3.0, 0.01 * sceneCm)))
            Z_m = sd / 100.0;
    }

    // PLAYER LOOK: the player's pixel where the player's own capture saw the player at this pixel's depth. Anything
    // in front of the player there is not the player's, and the capture holds a far depth where it drew nothing. Where
    // it saw the player NEARER than the scene's depth, a see-through part of the player (a lightsaber's blade, made
    // opaque for that capture) stood in front of the scene: its depth is the player capture's.
    bool isPlayer = false;
    if ((PlayerMask & 1u) != 0u)
    {
        float pd = PlayerTex.Load(texel).x;
#ifdef DEPTH_BUFFER
        // UNITY: device depth, turned into cm by the same planes as the face's; 0 (the far plane) where nothing was drawn.
        pd = pd > 0.0 ? 100.0 * (Perspective != 0u ? DepthNear * DepthFar / (DepthNear + pd * (DepthFar - DepthNear))
                                                     : DepthFar - pd * (DepthFar - DepthNear)) : 0.0;
#endif
        const float sceneCm = Z_m * 100.0;
        const float tol = max(3.0, 0.01 * sceneCm);
        const bool drawn = pd > 0.0 && pd < EmptyCutoff * 100.0;
        // In front of the scene: a see-through part made opaque, so only where it is opaque enough (as above).
        if (drawn && (Z_m <= 0.0 || Z_m >= EmptyCutoff || pd < sceneCm - tol)) { if (opaqueEnough) { Z_m = pd / 100.0; isPlayer = true; } }
        else isPlayer = drawn && abs(pd - sceneCm) <= tol;
    }

    // Empty-pixel sentinel: far-plane or degenerate (sky, no geometry).
    // Leave both colour and depth output as 0 (zeroed by ClearUAV before dispatch).
    if (Z_m <= 0.0 || Z_m >= EmptyCutoff) return;

    uint depthWord;
    if (Perspective != 0u)
    {
        // PERSPECTIVE: along this pixel's ray, by the scene depth (view-space z, so no normalising), from the
        // camera's place behind the centre. Outside the volume's box (with the compact pass's 5%) is dropped.
        float ndcX = ((float)srcCol + 0.5) / (float)FaceRes * 2.0 - 1.0;
        float ndcY = ((float)viewRow + 0.5) / (float)ActiveH * 2.0 - 1.0;
        if (SrcDepthScaled != 0u)
        {
            // FROSTBITE: the ray through the texel read, not this one's: a face's own size is a few texels off FaceRes,
            // and on ground seen edge-on half a texel is a long way.
            ndcX = ((float)colourTexel.x + 0.5) / (float)SrcSize * 2.0 - 1.0;
            ndcY = ((float)colourTexel.y + 0.5) / (float)SrcSize * 2.0 - 1.0;
        }
        float3 P = Z_m * (Fwd + ndcX * TanH * Right - ndcY * TanV * Up) + CamRel;   // REDENGINE3: was - CamD * Fwd
        if (abs(P.x) > QuantX || abs(P.y) > QuantY || abs(P.z) > QuantZ) return;
#ifdef FROSTBITE
        if ((PlayerMask & 8u) != 0u)
        {   // FROSTBITE PLAYER LOOK: inside the player's cylinder, above the ground under their feet
            const float2 across = P.xy - PlayerAt.xy;
            const float up = PlayerAt.z - P.z;
            isPlayer = dot(across, across) < PlayerRadius * PlayerRadius && up > 0.05 && up < PlayerHeight;
        }
#endif
        uint qx = (uint)round((P.x / QuantX * 0.5 + 0.5) * 2047.0);
        uint qy = (uint)round((P.y / QuantY * 0.5 + 0.5) * 2047.0);
        uint qz = (uint)round((P.z / QuantZ * 0.5 + 0.5) * 1023.0);
        depthWord = qx | (qy << 11u) | (qz << 22u);
    }
    else
    {
    // Reject surfaces outside the volume's depth slab.  Otherwise the clamp below
    // pins them onto the near/far plane, which shows up as a flat sheet of voxels
    // (very visible once capture-tilt widens the view and pulls in background).
    float D16f;
    if (FaceIndex < 4u)
    {
        // Side faces: signed depth from the volume centre along the (tilted) look dir.
        // dc = Z_m - CamDist (orbit radius); quantised over [-HalfDepth, HalfDepth].
        // The compact pass reconstructs world pos = dc*Forward + lateral*Right + vert*Up.
        float dc = Z_m - CamDist;
        D16f = (dc + HalfDepth) * (65535.0 / (2.0 * HalfDepth));
    }
    else
    {
        // Top/bottom: legacy axis-aligned encode.
        float WorldD  = CamAxisPos - LookSign * Z_m;
        float VsDepth = WorldD * DepthSign;
        D16f = (VsDepth - D_min) * D_scale;
    }
    if (D16f < 0.0 || D16f > 65535.0) return;
    uint D16 = (uint)round(D16f);
    depthWord = ((D16 >> 8u) & 0xFFu) | ((D16 & 0xFFu) << 8u);
    }

    const float exposure = isPlayer ? Exposure * PlayerGain : Exposure;

    // REDENGINE3/4: a finished picture is display-encoded (read as UNORM): metered and exposed in linear light, then
    // encoded again, so at exposure 1 it comes out as it went in.
    const float3 lin = DisplayColour != 0u ? pow(max(c.xyz, 0.0), 2.2) : c.xyz;

    // ── Auto-exposure metering (raw HDR luminance, BEFORE exposure/tonemap) ────
    if (MeterEnable != 0u && (col & 3u) == 0u && (row & 3u) == 0u)
    {
        float lum      = max(dot(lin, float3(0.2126, 0.7152, 0.0722)), 1e-4);
        uint  logFixed = (uint)clamp((log2(lum) + 16.0) * 64.0, 0.0, 8191.0);
        uint  prev;
        InterlockedAdd(Counter[uint2(1, 0)], logFixed, prev);
        InterlockedAdd(Counter[uint2(2, 0)], 1u,       prev);
    }

    // Output column in the shared texture
    uint outX = FaceIndex * FaceRes + col;

    // ── §5.1 — colour output (top half, row < FaceRes) ───────────────────────
    float3 litRGB = DisplayColour != 0u ? pow(saturate(lin * exposure), 1.0 / 2.2) : ACESFilm(c.xyz * exposure);   // REDENGINE3
#ifdef SURFACE_COLOUR
    // SURFACE COLOUR: the surfaces' own colours, and Lighting of the lit colour over them.
    float3 surface = saturate(SurfaceTex.Load(texel).rgb * SurfaceGain * (isPlayer ? PlayerGain : 1.0));
    litRGB = lerp(surface, litRGB, Lighting);
#endif
    // LOOK: gamma (above 1 lifts the middle tones), then saturation (0 grey, 1 as it was) - the world's or the player's.
    const float invGamma = isPlayer ? PlayerInvGamma : InvGamma;
    if (invGamma > 0.0 && invGamma != 1.0) litRGB = pow(max(litRGB, 0.0), invGamma);
    const float satDelta = isPlayer ? PlayerSatDelta : SatDelta;
    if (satDelta != 0.0)
    {
        const float luma = dot(litRGB, float3(0.2126, 0.7152, 0.0722));
        litRGB = saturate(lerp(luma.xxx, litRGB, 1.0 + satDelta));
    }
    // COLOUR RANGE, added: brightness (the brightest channel) 0 becomes ColorFloor and 1 ColorCeiling - the band a
    // Voxon display shows well, its dark end lost in the room and its bright end saturating. The colour is scaled
    // whole, so its hue and saturation stay as they were; black, which has neither, becomes grey at the floor.
    float bright = max(litRGB.r, max(litRGB.g, litRGB.b));
    float target = lerp(ColorFloor, ColorCeiling, saturate(bright));
    litRGB = bright > 0.0 ? litRGB * (target / bright) : target.xxx;
    uint R = (uint)(saturate(litRGB.x) * 255.5);
    uint G = (uint)(saturate(litRGB.y) * 255.5);
    uint B = (uint)(saturate(litRGB.z) * 255.5);
    // Black (0,0,0) sentinel: nudge B to 1 so VoxelStudio treats it as geometry
    if (R == 0u && G == 0u && B == 0u) B = 1u;
    OutputTex[uint2(outX, row)] = R | (G << 8u) | (B << 16u) | (isPlayer ? (1u << 24u) : 0u);   // PLAYER LOOK: the flag

    // ── §5.2 — depth output (bottom half, row + FaceRes) ─────────────────────
    OutputTex[uint2(outX, FaceRes + row)] = depthWord;   // D16 byte-swapped, or PERSPECTIVE's packed position
}
