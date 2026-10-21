--[[
  rig.lua -- the CAMERA HALF of the capture rig, for Unreal games (docs/ARCHITECTURE.md).

  Voxel Photonics' VLED Volume Capture plugin, as far as a shipped game allows: stock
  SceneCapture2D actors (no new classes without the game's source), lit colour + linear depth in
  ONE capture per face (SCS_SceneColorSceneDepth), up to six cameras on the faces of a box round a
  volume that follows the player and never turns. Perspective lenses by default; the plugin's
  orthographic cameras as an option. The GPU half -- the plugin's pack + compact shaders -- runs in
  the ReShade add-on (addon/), which finds these render targets by the face code each one is
  cleared to at creation. The two halves share nothing at run time but the settings, read from
  voxon_capture_ue.ini beside the game's exe by both.

  Enum values are read by NAME from the running engine (resolveEnums below): they move between versions. The probe on
  The Ascent (UE 4.26, 2026-10-10) confirmed SCS_SceneColorSceneDepth = 3, RTF_RGBA16f = 6, Orthographic = 1,
  AEM_Manual = 2, which stay as the fallback; Star Wars Jedi: Fallen Order (UE 4.21, 2026-10-15) has RTF_RGBA16f = 5
  and no AEM_Manual. bAutoCalculateOrthoPlanes is UE5-only (MISS on 4.26) and is not touched.

  TWO DELIBERATE DIFFERENCES FROM THE PLUGIN.
  * camDist. The plugin stands each camera 4 x Bound out, right for a subject in open space. In a
    level, every wall out there hides the volume, so here the camera stands ON the volume's edge by
    default (camDist 1) and its near plane cuts the level away like a section. The GPU half decodes
    depth as (depth - CamDist), so both halves derive it from the same settings, by the same formula
    (geometry() below / Derive() in capture_gpu.hpp).
  * yawDeg. The whole rig can be turned about the vertical (asked 2026-10-10: at 45 degrees walls
    and floors block the cameras less). The decode runs in the rig's own frame, unchanged, and the
    add-on turns the finished voxels back.
  Both, with the cameras, the resolution and the scale, come from the Voxon Capture Rig app's Cameras
  tab when the ini names its control file (readConfig below).
]]

-- Not UEHelpers: its world-context lookup goes through FindAllOf("PlayerController"), which finds
-- nothing in The Ascent, and then calls an undefined Print() (measured 2026-10-10). The pawn is a
-- world context object, and the libraries' default objects are found by path.
local function lib(name) return StaticFindObject("/Script/Engine.Default__" .. name) end

local M = {}

-- Enum values that have never moved; the ones that have are read from the engine (resolveEnums).
local ORTHOGRAPHIC             = 1
local PERSPECTIVE              = 0        -- ECameraProjectionMode::Perspective
local ALWAYS_SPAWN             = 1

-- The plugin's FaceTable, copied not re-derived (VledRenderVolume.cpp). Base look direction per
-- face; the top camera looks straight down with its image up along world +Y.
local FACE_LOOK = {
    [0] = { X = -1, Y =  0 },   -- +X side, looking toward -X
    [1] = { X =  0, Y = -1 },   -- +Y side, looking toward -Y
    [2] = { X =  1, Y =  0 },   -- -X side
    [3] = { X =  0, Y =  1 },   -- -Y side
}

-- The handshake: each target is cleared once to this, so the add-on can tell the faces apart with
-- no engine offsets. Values are exact in fp16 (integers below 2048, and the markers are integers).
local MARK_R, MARK_A, CODE_BASE = -1234.0, -4321.0, 1000.0
local SURFACE_CODE_BASE = 2000.0    -- a face's surface-colour target: its second code (lighting below 1)
local PLAYER_CODE_BASE = 1500.0     -- a face's player capture: its third code (playerLook); below 2048, so exact in fp16
local SOLID_CODE_BASE = 1250.0      -- a face's solid capture (see-through surfaces made opaque); exact in fp16 too
local GLOW_CODE_BASE = 1750.0       -- a face's glow capture (those surfaces as the game draws them, solidOpacity); exact too
local HANDSHAKE_SECONDS = 2         -- captures held off while the add-on reads the codes: it re-reads its settings
                                    -- once a second, so a new resolution can reach it a second after it reaches here
-- The loop runs this many times faster than the capture rate, and a capture goes out once 1/hz of the game's
-- own clock has passed. A loop AT the capture rate waits on the game thread: at 56 fps a 30 Hz loop delivered
-- 15 captures a second (The Ascent on D3D12, 2026-10-11), where at 125 fps it had delivered 29 (D3D11, the day
-- before). The game's frame rate, not the API, was the difference.
local TICKS_PER_SHOT = 3

local CFG_NAME = "voxon_capture_ue.ini"
local DEFAULTS = {
    on = 1,           -- 0 tears the rig down
    autostart = 1,    -- 0: the loop waits for F7 (debugging: with no loop live, Ctrl+R hot reload is safe)
    radiusM = 12.0,   -- gameplay radius: metres of world from the player to the display's edge
    feetX = 0.0,      -- display x of the player's feet (left -4, right +4 on a VX2-XL)
    feetY = 0.0,      -- display y of the player's feet (back -4, front +4: display +Y is toward the viewer)
    feetZ = 1.5,      -- display z of the player's feet (top -2, bottom +2), as Skyrim's
    boundR = 4.0,     -- VX2-XL: radius 4
    boundZ = 2.0,     --         half-height 2
    faces = "0,1,4",  -- X+, Y+, top (the user, 2026-10-10: three to start)
    faceRes = 512,    -- the plugin's VX2-XL Medium; XL side faces are faceRes x faceRes/2
    tiltDeg = 30.0,   -- side cameras pitched down to see the ground (the plugin's default)
    hz = 30,          -- captures a second; every face is a full lit render
    camDist = 1.0,    -- camera stand-off as a multiple of the volume's edge: 1 cuts the level away like a
                      -- section; the plugin stands its cameras about 4 out, right for a subject in open space
    yawDeg = 0.0,     -- the whole rig turned about the vertical: at 45 side cameras meet walls on the diagonal
    scaleX = 1.0,     -- how much world each of the rig's axes holds, as a multiple of radiusM's: above 1 more is squashed
    scaleY = 1.0,     -- into the display along it, below 1 less is stretched (Mortal Kombat 1's background: X at 2-3).
    scaleZ = 1.0,     -- The rig's X, Y and up are the world's at yawDeg 0. Perspective lenses only.
    turnWithPlayer = 0, -- 1: the rig turns with the player's view as well, and the volume with it, the view toward
                      -- the display's back (display -Y); 0: the world keeps its own orientation
    lighting = 1.0,   -- how much of the game's lighting the volume shows. 1: all of it, the lit capture alone. Below
                      -- 1 each camera also captures the surfaces' own colours (base colour, no lights) and the add-on
                      -- mixes them in: 0 is the surface colours alone. Depth always comes from the lit capture.
    playerLook = 0,   -- 1: each camera also captures the player alone (a second capture, depth only, of the player and
                      -- what is attached to it), so the add-on can tell the player's pixels from the world's and give
                      -- them a look of their own (the app's Player look)
    -- SEE-THROUGH SURFACES: a see-through (translucent) material writes no depth, so a camera lays its colour on what is
    -- behind it. The rig lists the ones it finds for the app (seeList 1), and each camera also captures the scene's depth
    -- with the chosen ones made opaque - solidMaterials, their paths with | between, or all of them with solidAll 1.
    seeList = 0,
    solidAll = 0,
    solidMaterials = "",
    solidOpacity = 0, -- above 0, a chosen surface is solid only where it is at least this opaque (0-1): each camera also
                      -- captures the chosen ones as the game draws them, and the add-on reads their opacity from it - a
                      -- blade's mesh is wider than its glowing core, which its material fades out to the edge
    snapshot = 0,     -- change this number and each camera's picture is written to a snapshots folder beside the app
    followClass = "", -- a game whose player is not a pawn: follow the middle of the visible actors of this class
                      -- instead (Mortal Kombat 1: MKCharacterActor, the two fighters), on the ground under the lower one
    orthoAutoPlanes = -1, -- UE5 only: 1/0 sets SceneCaptureComponent2D.bAutoCalculateOrthoPlanes, as the plugin
                      -- sets it (1); -1 leaves the engine's default. UE4 has no such property (probe: MISS on 4.26)
    -- What each capture costs. Live: applied to the cameras within a second of the ini changing.
    lodFactor = 1.0,  -- SceneCaptureComponent.LODDistanceFactor: above 1 the cameras take lower LODs than the player's
                      -- view (CLAUDE.md 7: a volume cannot show the detail anyway)
    aoOff = 0,        -- 1: no screen-space ambient occlusion in the captures
    ssrOff = 0,       -- 1: no screen-space reflections in the captures
    persistState = 0, -- 1: each camera keeps its render state between captures (bAlwaysPersistRenderingState), so
                      -- occlusion culling has the last capture's results; costs that state's memory per camera
    growTargets = 1,  -- 1: r.SceneRenderTargetResizeMethod 2, the scene buffers grow to the largest view and stay
                      -- (see sceneTargets); 0: the engine's default 0, resized to every view; -1: left alone
    perspective = 1,  -- 1: perspective lenses. UE4 drops dynamic lights and their shadows in orthographic captures
                      -- (The Ascent, 2026-10-11: 4.8x darker than a lens, same frame). 0: the plugin's orthographic
                      -- cameras, which sample evenly and are right on UE5.1 and later.
    controlFile = "", -- the app's control file; its Cameras tab (the ue* keys) wins over this ini...
    hostGame = "ascent", -- ...when that file says game= this
}

-- The app's control-file keys (its Cameras tab) and the ini keys they set.
local HOST_KEYS = { ueFaces = "faces", ueFaceRes = "faceRes", ueRadius = "radiusM",
                    ueCamDist = "camDist", ueYaw = "yawDeg", ueTilt = "tiltDeg", uePerspective = "perspective",
                    ueFeetX = "feetX", ueFeetY = "feetY", ueFeetZ = "feetZ", ueTurn = "turnWithPlayer",
                    ueLighting = "lighting", ueScaleX = "scaleX", ueScaleY = "scaleY", ueScaleZ = "scaleZ",
                    ueBoundR = "boundR", ueBoundZ = "boundZ", uePlayerLook = "playerLook",
                    ueSeeList = "seeList", ueSolidAll = "solidAll", ueSolidMaterials = "solidMaterials",
                    ueSolidOpacity = "solidOpacity" }

local cfg, cfgStamp = nil, ""
local state = { faces = {}, key = nil, handshake = 0, ticks = 0, shots = 0, pending = false, hz = 0, loop = 0 }
local startLoop   -- forward: a change of rate restarts the loop

local function log(s) print("[VoxonRig] " .. s .. "\n") end

local function keyValues(text)
    local kv = {}
    for line in text:gmatch("[^\r\n]+") do
        -- Comments first: most lines carry one, and a pattern that stops at ';' can never then reach $.
        local k, v = line:gsub(";.*$", ""):match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
        if k then kv[k] = v end
    end
    return kv
end

local function set(c, k, v)
    if c[k] == nil then return end
    c[k] = (type(DEFAULTS[k]) == "number") and (tonumber(v) or c[k]) or v
end

-- The ini, then -- when it names one and the app is on this game -- the app's control file over it,
-- so the panel's cameras, resolution and rig win. voxon_capture_ue.cpp does the same, key for key. The
-- stamp is both files, so a change to either is seen.
local function readConfig()
    local f = io.open(CFG_NAME, "r")
    if not f then return nil, nil end
    local text = f:read("*a"); f:close()
    local c = {}
    for k, v in pairs(DEFAULTS) do c[k] = v end
    for k, v in pairs(keyValues(text)) do set(c, k, v) end
    if c.controlFile ~= "" then
        local h = io.open(c.controlFile, "r")
        if h then
            local host = h:read("*a"); h:close()
            local kv = keyValues(host)
            if kv.game == c.hostGame then
                for hk, k in pairs(HOST_KEYS) do if kv[hk] then set(c, k, kv[hk]) end end
                text = text .. host
            end
        end
    end
    -- 2560 at most: the add-on packs six faces side by side, and 6 x 2560 is inside D3D's 16384 texture width.
    c.faceRes = math.floor(math.max(64, math.min(2560, c.faceRes)) / 2) * 2
    c.camDist = math.max(1, math.min(8, c.camDist))
    c.scaleX, c.scaleY, c.scaleZ = math.max(0.05, c.scaleX), math.max(0.05, c.scaleY), math.max(0.05, c.scaleZ)
    return c, text
end

-- The settings' text without the rig's scale (radiusM, ueRadius): a change of scale alone - the app's rig-scale keys
-- make one many times a second while held - is taken without a word in the log.
local function withoutScale(text)
    return (text:gsub("[^\n]*", function(line)
        if line:match("^%s*radiusM%s*=") or line:match("^%s*ueRadius%s*=") then return "" end
    end))
end

-- Each digit a face, in the plugin's order: "0,1,4" from the ini and "014" from the host both work.
local function enabledFaces(c)
    local list = {}
    for d in tostring(c.faces):gmatch("%d") do
        local i = tonumber(d)
        if i <= 5 then list[#list + 1] = i end
    end
    return list
end

-- World metres of the volume: BoundR/BoundZ scaled so the display's radius holds radiusM of world.
-- The SAME formulas live in voxon_capture_ue.cpp; they must agree exactly.
-- A side face's height: half its width on a display at least twice as wide as tall (VX2-XL, 8 x 4), square on one as
-- tall as wide (VX2, 4 x 4). capture_gpu.hpp's SideHeight.
local function sideHeight(c)
    return (2 * c.boundZ <= c.boundR * 1.001) and math.floor(c.faceRes / 2) or c.faceRes
end

-- SCALE: the cameras capture a box of half-extents ex, ey, ez along the rig's X, Y and up - the display's R, R and Z
-- times the settings' scales - and the add-on scales each axis back into the display. Each side camera frames its own
-- box: faces 0 and 2 look along X (through ex, across ey), 1 and 3 along Y. With every scale 1 these are the original
-- formulas. capture_gpu.hpp's Derive, term for term.
local function geometry(c)
    local s = c.radiusM / c.boundR
    local R, Z = c.boundR * s, c.boundZ * s
    local persp = c.perspective ~= 0
    local ex, ey, ez = R * (persp and c.scaleX or 1), R * (persp and c.scaleY or 1), Z * (persp and c.scaleZ or 1)
    local t = math.rad(math.max(0, math.min(90, c.tiltDeg)))
    local ct, st = math.cos(t), math.sin(t)
    local sideH = sideHeight(c)
    -- Perspective: the near clip plane sits on the volume's near face, nSide / nTop in front of the camera, where the
    -- lens just spans the volume's cross-section: tan(half FOV) = 1 / camDist.
    local k = math.max(1, c.camDist)
    local side = {}
    for v = 0, 3 do
        local along, across = (v % 2 == 0) and ex or ey, (v % 2 == 0) and ey or ex
        local f = { halfDepth = along * ct + ez * st, vertHalf = along * st + ez * ct, nSide = 0 }   -- slab along the tilted look
        f.halfWin = math.max(across, f.vertHalf * c.faceRes / sideH)
        f.sideDist = f.halfDepth * c.camDist
        if persp then f.nSide = f.halfWin * k; f.sideDist = f.halfDepth + f.nSide end
        side[v] = f
    end
    local topDist, nTop, tanP = ez * c.camDist, 0, 0
    if persp then nTop = math.max(ex, ey) * k; topDist = ez + nTop; tanP = 1 / k end
    local yaw = math.rad(c.yawDeg)
    return { s = s, R = R, Z = Z, ex = ex, ey = ey, ez = ez, ct = ct, st = st, t = t, sideH = sideH, side = side,
             topDist = topDist, persp = persp, nTop = nTop, fovDeg = math.deg(2 * math.atan(tanP)),
             yawDeg = c.yawDeg, cy = math.cos(yaw), sy = math.sin(yaw) }
end

-- The solid capture is made: see-through materials chosen to be made opaque, or all of them.
local function useSolid(c) return c.solidAll ~= 0 or c.solidMaterials ~= "" end
-- ...and the glow capture with it, which tells how opaque they are where they are drawn.
local function useGlow(c) return useSolid(c) and c.solidOpacity > 0 end

local function destroyAll()
    for _, f in pairs(state.faces) do
        for _, a in ipairs({ f.actor, f.pactor or false, f.sactor or false, f.gactor or false }) do
            if a and a:IsValid() then pcall(function() a:K2_DestroyActor() end) end
        end
    end
    state.faces, state.key = {}, nil
end

-- A face's capture components, the ones it has: its own, the surface colour's, the player's, the solid one, the glow.
local function comps(f)
    local list = { f.comp }
    if f.surf then list[#list + 1] = f.surf end
    if f.pcomp then list[#list + 1] = f.pcomp end
    if f.scomp then list[#list + 1] = f.scomp end
    if f.gcomp then list[#list + 1] = f.gcomp end
    return list
end

-- What a change of which rebuilds the cameras.
local function buildKey(c)
    return string.format("%s|%d|%d|%d|%d", c.faces, c.faceRes, c.lighting < 0.999 and 1 or 0, sideHeight(c),
                         c.playerLook ~= 0 and 1 or 0) .. (useSolid(c) and "|solid" or "") .. (useGlow(c) and "|glow" or "")
end

-- Each engine call is a STEP: logged before it runs, so a failure names the call. UE4SS hands some
-- errors back as a bare function value with no message (measured 2026-10-10), so the step name is
-- the only record of where it happened.
local function step(name, f)
    local ok, r = pcall(f)
    if ok then return r end
    local msg = tostring(r)
    if type(r) == "function" then
        local ok2, m = pcall(r)
        msg = "function error -> " .. tostring(m) .. (ok2 and "" or " (and calling it failed)")
    end
    error("step '" .. name .. "': " .. msg, 0)
end

-- An enum value by NAME from the running engine's own enum. Returns the value and how it was had: "read"; "absent",
-- the enum is there without that name (an older engine); "assumed", the enum could not be read, so the fallback.
local function enumValue(path, name, fallback)
    local seen, found = false, nil
    pcall(function()
        local e = StaticFindObject(path)
        if not (e and e:IsValid()) then return end
        e:ForEachName(function(n, v)
            seen = true
            local s = n:ToString()
            if s == name or s:sub(-(#name + 2)) == "::" .. name then found = v end
        end)
    end)
    if found ~= nil then return found, "read" end
    if seen then return nil, "absent" end
    return fallback, "assumed"
end

-- The enum values that move between engine versions, read once and logged. RTF_RGBA16f: 6 on 4.26, 5 on 4.21, where
-- RTF_RGBA8_SRGB is not yet before it (6 there made single-channel R32 targets, which the add-on rightly ignored).
-- AEM_Manual: absent before the engine had a manual exposure; nil then, and the capture's exposure is pinned instead.
local function resolveEnums()
    if state.enums then return state.enums end
    local E, said = {}, {}
    local function get(key, path, name, fallback)
        local v, how = enumValue(path, name, fallback)
        if v == nil and key ~= "AEM_Manual" then v, how = fallback, "assumed" end
        E[key] = v
        said[#said + 1] = string.format("%s %s (%s)", name, tostring(v), how)
    end
    get("RTF_RGBA16f", "/Script/Engine.ETextureRenderTargetFormat", "RTF_RGBA16f", 6)
    get("SCS_SceneColorSceneDepth", "/Script/Engine.ESceneCaptureSource", "SCS_SceneColorSceneDepth", 3)
    get("SCS_BaseColor", "/Script/Engine.ESceneCaptureSource", "SCS_BaseColor", 7)
    get("SCS_SceneDepth", "/Script/Engine.ESceneCaptureSource", "SCS_SceneDepth", 4)
    get("SCS_SceneColorHDR", "/Script/Engine.ESceneCaptureSource", "SCS_SceneColorHDR", 0)
    get("PRM_UseShowOnlyList", "/Script/Engine.ESceneCapturePrimitiveRenderMode", "PRM_UseShowOnlyList", 2)
    get("AEM_Manual", "/Script/Engine.EAutoExposureMethod", "AEM_Manual", 2)
    state.enums = E
    log("engine enums: " .. table.concat(said, ", "))
    return E
end

-- A face's render target, by whichever form of CreateRenderTarget2D this engine has: the clear colour came in after
-- Unreal 4.21 (Star Wars Jedi: Fallen Order, 2026-10-15: the six-argument call failed there) and the mip-map flag later
-- still. Newest first; the first form that works is logged and remembered.
local function createTarget(krl, ctx, w, h)
    local RTF_RGBA16f = resolveEnums().RTF_RGBA16f
    local forms = {
        { "(ctx, w, h, format, clear colour, mip maps)", function()
            return krl:CreateRenderTarget2D(ctx, w, h, RTF_RGBA16f, { R = 0, G = 0, B = 0, A = 0 }, false) end },
        { "(ctx, w, h, format, clear colour)", function()
            return krl:CreateRenderTarget2D(ctx, w, h, RTF_RGBA16f, { R = 0, G = 0, B = 0, A = 0 }) end },
        { "(ctx, w, h, format)", function() return krl:CreateRenderTarget2D(ctx, w, h, RTF_RGBA16f) end },
    }
    local last
    for i = (state.targetForm or 1), #forms do
        local ok, rt = pcall(forms[i][2])
        if ok and rt and rt:IsValid() then
            if state.targetForm ~= i then
                state.targetForm = i
                log("render targets made by CreateRenderTarget2D" .. forms[i][1])
            end
            return rt
        end
        last = ok and "no target" or tostring(rt)
    end
    error("no form of CreateRenderTarget2D worked: " .. tostring(last), 0)
end

local function spawnFace(face, c, g, ctx)
    local cls = step("find SceneCapture2D", function() return StaticFindObject("/Script/Engine.SceneCapture2D") end)
    local gs = step("GameplayStatics", function() return lib("GameplayStatics") end)
    local krl = step("KismetRenderingLibrary", function() return lib("KismetRenderingLibrary") end)
    if not (cls and cls:IsValid() and gs and gs:IsValid() and krl and krl:IsValid()) then
        error(string.format("objects: class %s, statics %s, rendering %s", tostring(cls and cls:IsValid()),
            tostring(gs and gs:IsValid()), tostring(krl and krl:IsValid())), 0)
    end
    local xform = { Rotation = { X = 0, Y = 0, Z = 0, W = 1 }, Translation = { X = 0, Y = 0, Z = 0 },
                    Scale3D = { X = 1, Y = 1, Z = 1 } }
    -- The Ascent, 2026-10-10: BeginDeferredActorSpawnFromClass(ctx, cls, xform, 1) and (..., 1, nil)
    -- both fail with a message-less error. So each way of spawning is tried in turn and the first
    -- that works is logged and remembered.
    local function spawnCapture()
        local ways = {
            { "deferred spawn, Owner = pawn", function()
                local a = gs:BeginDeferredActorSpawnFromClass(ctx, cls, xform, ALWAYS_SPAWN, ctx)
                gs:FinishSpawningActor(a, xform)
                return a
            end },
            { "World:SpawnActor", function()
                local world = ctx:GetWorld()
                return world:SpawnActor(cls, { X = 0, Y = 0, Z = 0 }, { Pitch = 0, Yaw = 0, Roll = 0 })
            end },
        }
        -- Once: does ANY static GameplayStatics call work from here? Separates "this function's
        -- arguments" from "static calls through the default object".
        if not state.sanity then
            state.sanity = true
            local ok, r = pcall(function() return gs:GetPlayerPawn(ctx, 0) end)
            log("sanity: GameplayStatics:GetPlayerPawn(ctx, 0) -> " ..
                (ok and ((r and r:IsValid()) and r:GetFullName() or "nil") or ("error " .. tostring(r))))
        end
        for i = (state.spawnWay or 1), #ways do
            local ok, a = pcall(ways[i][2])
            if ok and a and a:IsValid() then
                if state.spawnWay ~= i then log("spawning by: " .. ways[i][1]) end
                state.spawnWay = i
                return a
            end
            log(string.format("spawn way '%s' failed: %s", ways[i][1], ok and "no actor" or tostring(a)))
        end
        error("no way of spawning a SceneCapture2D worked", 0)
    end
    local actor = spawnCapture()
    local comp = step("CaptureComponent2D", function() return actor.CaptureComponent2D end)

    local w = c.faceRes
    local h = (face >= 4) and c.faceRes or g.sideH
    local rt = step("CreateRenderTarget2D", function() return createTarget(krl, ctx, w, h) end)
    step("TargetGamma", function() rt.TargetGamma = 1.0 end)
    -- The handshake code, written once before any capture overwrites it.
    step("ClearRenderTarget2D",
        function() krl:ClearRenderTarget2D(ctx, rt, { R = MARK_R, G = CODE_BASE + face, B = 0, A = MARK_A }) end)

    if c.orthoAutoPlanes >= 0 then
        -- The plugin forces this on: UE5 turns it off for assets saved before its auto-plane version, and with it
        -- off the ortho depth slab cannot move. Guarded, because UE4 has no such property.
        local ok = pcall(function() comp.bAutoCalculateOrthoPlanes = (c.orthoAutoPlanes ~= 0) end)
        if not ok and face == 0 then log("bAutoCalculateOrthoPlanes not settable on this engine -- left alone") end
    end
    step("capture properties", function()
        comp.ProjectionType     = ORTHOGRAPHIC
        comp.CaptureSource      = resolveEnums().SCS_SceneColorSceneDepth
        comp.TextureTarget      = rt
        comp.bCaptureEveryFrame = false   -- the rig drives its own clock: every capture is a lit render
        comp.bCaptureOnMovement = false
    end)
    pcall(function() comp.bUseRayTracingIfEnabled = false end)   -- Unreal 4.22 on; older engines have no ray tracing
    -- A fixed exposure: six captures adapting on their own pulse and disagree (the plugin's note).
    -- The pack shader is the one exposure authority.
    pcall(function()
        local aem = resolveEnums().AEM_Manual
        if aem then
            comp.PostProcessSettings.bOverride_AutoExposureMethod = true
            comp.PostProcessSettings.AutoExposureMethod           = aem
        else   -- no manual exposure on this engine: the eye adaptation held at one brightness does the same
            comp.PostProcessSettings.bOverride_AutoExposureMinBrightness = true
            comp.PostProcessSettings.AutoExposureMinBrightness           = 1.0
            comp.PostProcessSettings.bOverride_AutoExposureMaxBrightness = true
            comp.PostProcessSettings.AutoExposureMaxBrightness           = 1.0
        end
        comp.PostProcessSettings.bOverride_AutoExposureBias   = true
        comp.PostProcessSettings.AutoExposureBias             = 0.0
        comp.PostProcessBlendWeight = 1.0
    end)

    -- SURFACE COLOUR, below full lighting: a second capture on the same actor writes the surfaces' own colours (base
    -- colour) into a target of its own, cleared to the face's second code. It needs no lights, shadows, occlusion or
    -- fog, so those are off. A capture reads its show flags only as it registers, and the actor's own component
    -- already has, so this one is added deferred, given the flags, then finished (AddComponentByClass, Unreal 4.26 on).
    -- With lighting off the engine still fills the base colour (it is the base pass, not the lighting) - measured on
    -- Mortal Kombat 1, 2026-10-12, where lighting off alone gave black.
    local surf, srt
    -- An engine without AddComponentByClass (before 4.26) cannot make it: the face is kept, lit only, and said once.
    local surfOk, surfErr = pcall(function()
        if c.lighting < 0.999 and not state.noSurface then
            local cls2 = step("find SceneCaptureComponent2D", function() return StaticFindObject("/Script/Engine.SceneCaptureComponent2D") end)
            surf = step("AddComponentByClass (surface colour)", function() return actor:AddComponentByClass(cls2, false, xform, true) end)
            step("show flags (surface colour)", function()
                for i, flag in ipairs({ "Lighting", "DynamicShadows", "AmbientOcclusion", "Fog", "VolumetricFog" }) do
                    surf.ShowFlagSettings[i] = { ShowFlagName = flag, Enabled = false }
                end
            end)
            step("FinishAddComponent (surface colour)", function() actor:FinishAddComponent(surf, false, xform) end)
            srt = step("CreateRenderTarget2D (surface colour)", function() return createTarget(krl, ctx, w, h) end)
            step("TargetGamma (surface colour)", function() srt.TargetGamma = 1.0 end)
            step("ClearRenderTarget2D (surface colour)",
                function() krl:ClearRenderTarget2D(ctx, srt, { R = MARK_R, G = SURFACE_CODE_BASE + face, B = 0, A = MARK_A }) end)
            step("surface colour properties", function()
                surf.ProjectionType     = ORTHOGRAPHIC
                surf.CaptureSource      = resolveEnums().SCS_BaseColor
                surf.TextureTarget      = srt
                surf.bCaptureEveryFrame = false
                surf.bCaptureOnMovement = false
            end)
            pcall(function() surf.bUseRayTracingIfEnabled = false end)
        end
    end)
    if not surfOk then
        state.noSurface = true
        surf, srt = nil, nil
        log("surface colour cannot be captured on this engine, so faces stay lit (Lighting 100%): " .. tostring(surfErr))
    end

    -- DEPTH CAPTURES (the player's, the solid one) need the scene's depth alone, and with the engine's every show flag
    -- each was a full lit render: lights, shadows, occlusion, reflections, the see-through pass, post-processing. With
    -- only the solid capture's on, The Ascent ran at 90 frames a second, not 204 (2026-10-15). So each is a capture
    -- added to its actor, as the surface colour's is, with those off before it registers; the actor's own is kept idle.
    -- An engine without AddComponentByClass (before 4.26: Jedi: Fallen Order) keeps the actor's own, at the full cost, and
    -- says so once. Tried again at each build: once, at The Ascent's title screen, it failed where it worked in play.
    -- keep: show flags left on (the glow capture keeps the see-through pass).
    local function depthCapture(a, what, keep)
        local own = step("CaptureComponent2D (" .. what .. ")", function() return a.CaptureComponent2D end)
        own.bCaptureEveryFrame = false
        own.bCaptureOnMovement = false
        local ok, cmp = pcall(function()
            local cls2 = step("find SceneCaptureComponent2D", function() return StaticFindObject("/Script/Engine.SceneCaptureComponent2D") end)
            local added = step("AddComponentByClass (" .. what .. ")", function() return a:AddComponentByClass(cls2, false, xform, true) end)
            step("show flags (" .. what .. ")", function()
                local i = 0
                for _, flag in ipairs({ "Lighting", "DynamicShadows", "AmbientOcclusion", "DistanceFieldAO", "ScreenSpaceReflections",
                                        "ReflectionEnvironment", "SkyLighting", "Fog", "VolumetricFog", "LightShafts", "Decals",
                                        "Translucency", "PostProcessing", "Bloom", "MotionBlur" }) do
                    if not (keep and keep[flag]) then
                        i = i + 1
                        added.ShowFlagSettings[i] = { ShowFlagName = flag, Enabled = false }
                    end
                end
            end)
            step("FinishAddComponent (" .. what .. ")", function() a:FinishAddComponent(added, false, xform) end)
            return added
        end)
        if ok and cmp and cmp:IsValid() then return cmp end
        if not state.depthFlagsSaid then
            state.depthFlagsSaid = true
            log("the player and solid captures render in full here: " .. tostring(cmp))
        end
        return own
    end

    -- PLAYER LOOK: a capture of the player alone, from this face's camera - an actor of its own, so it needs no
    -- AddComponentByClass - with only the player's components rendered (PRM_UseShowOnlyList, filled by showOnly), and
    -- only their depth, into a target cleared to the face's third code. Where its depth is the face's, the add-on takes
    -- the pixel for the player's. Its place, lens and clip follow the face's camera (place, lens); it fires after it.
    local pactor, pcomp, prt
    if c.playerLook ~= 0 and not state.noPlayer then
        local ok, err = pcall(function()
            pactor = spawnCapture()
            pcomp = depthCapture(pactor, "player")
            prt = step("CreateRenderTarget2D (player)", function() return createTarget(krl, ctx, w, h) end)
            step("TargetGamma (player)", function() prt.TargetGamma = 1.0 end)
            step("ClearRenderTarget2D (player)",
                function() krl:ClearRenderTarget2D(ctx, prt, { R = MARK_R, G = PLAYER_CODE_BASE + face, B = 0, A = MARK_A }) end)
            step("player capture properties", function()
                pcomp.ProjectionType      = ORTHOGRAPHIC
                pcomp.CaptureSource       = resolveEnums().SCS_SceneDepth
                pcomp.TextureTarget       = prt
                pcomp.bCaptureEveryFrame  = false
                pcomp.bCaptureOnMovement  = false
                pcomp.PrimitiveRenderMode = resolveEnums().PRM_UseShowOnlyList
            end)
            pcall(function() pcomp.bUseRayTracingIfEnabled = false end)
        end)
        if not ok then
            if pactor and pactor:IsValid() then pcall(function() pactor:K2_DestroyActor() end) end
            pactor, pcomp, prt = nil, nil, nil
            state.noPlayer = true
            log("the player cannot be captured on its own on this engine, so it keeps the world's look: " .. tostring(err))
        end
    end
    -- SEE-THROUGH SURFACES: one more capture from this face's camera, of the chosen see-through surfaces alone
    -- (PRM_UseShowOnlyList, filled by makeSolid), made opaque while it renders, their depth into a target cleared to the
    -- face's fourth code. Where one stands in front of what the face saw, the add-on takes its depth. Drawing the whole
    -- scene again for it took The Ascent from 204 frames a second to 70 (2026-10-15).
    local sactor, scomp, sdrt
    if useSolid(c) and not state.noSolid then
        local ok, err = pcall(function()
            sactor = spawnCapture()
            scomp = depthCapture(sactor, "solid")
            sdrt = step("CreateRenderTarget2D (solid)", function() return createTarget(krl, ctx, w, h) end)
            step("TargetGamma (solid)", function() sdrt.TargetGamma = 1.0 end)
            step("ClearRenderTarget2D (solid)",
                function() krl:ClearRenderTarget2D(ctx, sdrt, { R = MARK_R, G = SOLID_CODE_BASE + face, B = 0, A = MARK_A }) end)
            step("solid capture properties", function()
                scomp.ProjectionType      = ORTHOGRAPHIC
                scomp.CaptureSource       = resolveEnums().SCS_SceneDepth
                scomp.TextureTarget       = sdrt
                scomp.bCaptureEveryFrame  = false
                scomp.bCaptureOnMovement  = false
                scomp.PrimitiveRenderMode = resolveEnums().PRM_UseShowOnlyList
            end)
            pcall(function() scomp.bUseRayTracingIfEnabled = false end)
        end)
        if not ok then
            if sactor and sactor:IsValid() then pcall(function() sactor:K2_DestroyActor() end) end
            sactor, scomp, sdrt = nil, nil, nil
            state.noSolid = true
            log("see-through surfaces cannot be made solid on this engine: " .. tostring(err))
        end
    end
    -- THEIR GLOW (solidOpacity above 0): the same surfaces once more, as the game draws them - before they are made
    -- opaque - into a target cleared to the face's fifth code (SCS_SceneColorHDR: the colour, and in alpha how much light
    -- they let through, 1 where none was drawn). The add-on keeps the solid depth only where they are opaque enough, so a
    -- blade is as thick as its core, not its mesh.
    local gactor, gcomp, grt
    if sactor and useGlow(c) and not state.noGlow then
        local ok, err = pcall(function()
            gactor = spawnCapture()
            gcomp = depthCapture(gactor, "glow", { Translucency = true })
            grt = step("CreateRenderTarget2D (glow)", function() return createTarget(krl, ctx, w, h) end)
            step("TargetGamma (glow)", function() grt.TargetGamma = 1.0 end)
            step("ClearRenderTarget2D (glow)",
                function() krl:ClearRenderTarget2D(ctx, grt, { R = MARK_R, G = GLOW_CODE_BASE + face, B = 0, A = MARK_A }) end)
            step("glow capture properties", function()
                gcomp.ProjectionType      = ORTHOGRAPHIC
                gcomp.CaptureSource       = resolveEnums().SCS_SceneColorHDR
                gcomp.TextureTarget       = grt
                gcomp.bCaptureEveryFrame  = false
                gcomp.bCaptureOnMovement  = false
                gcomp.PrimitiveRenderMode = resolveEnums().PRM_UseShowOnlyList
            end)
            pcall(function() gcomp.bUseRayTracingIfEnabled = false end)
        end)
        if not ok then
            if gactor and gactor:IsValid() then pcall(function() gactor:K2_DestroyActor() end) end
            gactor, gcomp, grt = nil, nil, nil
            state.noGlow = true
            log("see-through surfaces' opacity cannot be captured on this engine, so they are solid where drawn: " .. tostring(err))
        end
    end
    return { actor = actor, comp = comp, surf = surf, rt = rt, srt = srt, pactor = pactor, pcomp = pcomp, prt = prt,
             sactor = sactor, scomp = scomp, sdrt = sdrt, gactor = gactor, gcomp = gcomp, grt = grt, face = face, w = w, h = h }
end

local function traced(e) return tostring(e) .. ((debug and debug.traceback) and (" | " .. debug.traceback()) or "") end

local function build(c, ctx)
    destroyAll()
    local g = geometry(c)
    local built = 0
    for _, face in ipairs(enabledFaces(c)) do
        local ok, f = xpcall(spawnFace, traced, face, c, g, ctx)
        if ok then
            state.faces[face] = f
            built = built + 1
            log(string.format("face %d: %dx%d target, code %d%s%s%s", face, f.w, f.h, CODE_BASE + face,
                f.surf and string.format(", surface colour code %d", SURFACE_CODE_BASE + face) or "",
                f.pcomp and string.format(", player code %d", PLAYER_CODE_BASE + face) or "",
                (f.scomp and string.format(", solid code %d", SOLID_CODE_BASE + face) or "")
                .. (f.gcomp and string.format(", glow code %d", GLOW_CODE_BASE + face) or "")))
        else
            log("face " .. face .. " FAILED: " .. tostring(f))
        end
    end
    -- Nothing built: try again in five seconds rather than capture with no cameras for ever.
    state.key = (built > 0) and buildKey(c) or nil
    state.showOnlyAt = 0
    state.retryAt = (built > 0) and 0 or state.ticks + 5 * math.max(1, c.hz) * TICKS_PER_SHOT
    state.handshake = math.ceil(HANDSHAKE_SECONDS * math.max(1, c.hz))   -- counted in captures
end

-- THE SCENE BUFFERS GROW TO THE LARGEST VIEW AND STAY THERE. By default UE resizes its scene render targets to each
-- view it renders (r.SceneRenderTargetResizeMethod 0), so a camera taller or wider than the game's window reallocates
-- them twice a frame: for the camera, then back for the player's view. Measured on The Ascent 2026-10-11, six cameras,
-- window 1000 x 662: 640 px ran the game at 191 fps, 768 px at 55, with the GPU two-thirds idle; at a 2580 x 1490
-- window the same cliff had sat between 1280 and 1536 px. 2 is the engine's own "expand to the largest requested",
-- in UE4 and UE5 alike - not a setting for one game. It costs the buffers' memory at the largest camera.
local function sceneTargets(c)
    local pc = state.pc
    if c.growTargets < 0 or not pc or not pc:IsValid() then return end
    local cmd = "r.SceneRenderTargetResizeMethod " .. ((c.growTargets ~= 0) and "2" or "0")
    local ok, err = pcall(function() lib("KismetSystemLibrary"):ExecuteConsoleCommand(pc, cmd, pc) end)
    log(cmd .. (ok and "" or (" FAILED: " .. tostring(err))))
end

-- The feet, not the actor: a Character's location is its capsule's centre.
local function feet(pawn)
    local o = pawn:K2_GetActorLocation()
    local half = 90.0
    pcall(function() half = pawn.CapsuleComponent:GetScaledCapsuleHalfHeight() end)
    return { X = o.X, Y = o.Y, Z = o.Z - half }
end

-- What the volume follows, and where its feet are. The player's pawn; or, with followClass, the middle of every visible
-- actor of that class (its subclasses included), on the ground under the lowest - a fighting game's two fighters. The
-- class's actors are listed again once a second, since FindAllOf walks the objects; their places are read each tick.
-- Returns the feet and an actor of the player's world (nil when there is none yet).
local function subject(c, pc)
    if c.followClass == "" then
        local pawn = lib("GameplayStatics"):GetPlayerPawn(pc, 0)
        if not pawn or not pawn:IsValid() then return nil end
        return feet(pawn), pawn
    end
    if not state.followList or state.ticks >= (state.followAt or 0) then
        state.followList = FindAllOf(c.followClass) or {}
        state.followAt = state.ticks + math.max(1, c.hz) * TICKS_PER_SHOT
    end
    local n, x, y, z, first = 0, 0, 0, nil, nil
    for _, a in ipairs(state.followList) do
        local ok, l = pcall(function()
            if a:IsValid() and not a.bHidden then return a:K2_GetActorLocation() end
        end)
        if ok and l then
            n, x, y = n + 1, x + l.X, y + l.Y
            z = math.min(z or l.Z, l.Z)
            first = first or a
        end
    end
    if n == 0 then return nil end
    return { X = x / n, Y = y / n, Z = z }, first
end

-- What the rig is waiting for, every 5 s of ticks while there is no player to follow: a game that never gives one
-- (its player is not a possessed pawn) shows here instead of a silent rig.
local function fullName(o)
    local ok, n = pcall(function() return (o and o:IsValid()) and o:GetFullName() or "none" end)
    return ok and n or "unreadable"
end
local function waiting(what, pc)
    state.waitTicks = (state.waitTicks or 0) + 1
    if state.waitTicks % (5 * math.max(1, cfg.hz) * TICKS_PER_SHOT) ~= 1 then return end
    local detail = ""
    if pc then
        local pawn, target = nil, nil
        pcall(function() pawn = pc.Pawn end)
        pcall(function() target = pc.PlayerCameraManager.ViewTarget.Target end)
        detail = string.format(" - controller %s, its Pawn %s, its camera's view target %s", fullName(pc), fullName(pawn), fullName(target))
    end
    log("waiting: " .. what .. detail)
end

-- The way the player is looking, as a yaw in degrees: the player camera's, which is what the screen shows, else the
-- pawn's own facing. nil when neither can be read.
local function heading(pawn)
    local ok, yaw = pcall(function()
        local cm = lib("GameplayStatics"):GetPlayerCameraManager(pawn, 0)
        if cm and cm:IsValid() then return cm:GetCameraRotation().Yaw end
    end)
    local from = "the player's camera"
    if not (ok and type(yaw) == "number") then
        ok, yaw = pcall(function() return pawn:K2_GetActorRotation().Yaw end)
        from = "the pawn's facing"
    end
    if not (ok and type(yaw) == "number") then return nil end
    if state.headingFrom ~= from then state.headingFrom = from; log("turning with " .. from) end
    return yaw
end

-- The cost settings, on every camera whose settings are older than the ini's. Post-process overrides are read
-- by the renderer at each capture, so no rebuild is needed.
local function quality(c, f)
    local key = string.format("%.2f|%d|%d|%d|%d", c.lodFactor, c.aoOff, c.ssrOff, c.persistState, c.perspective)
    if f.qualityKey == key then return end
    f.qualityKey = key
    for _, cmp in ipairs(comps(f)) do      -- the surface and player captures, when there are, see as the lit one does
        pcall(function() cmp.LODDistanceFactor = c.lodFactor end)
        pcall(function() cmp.bAlwaysPersistRenderingState = c.persistState ~= 0 end)
        pcall(function() cmp.ProjectionType = (c.perspective ~= 0) and PERSPECTIVE or ORTHOGRAPHIC end)
    end
    pcall(function()   -- written through the component each time, as the exposure is: one copy would change nothing
        f.comp.PostProcessSettings.bOverride_AmbientOcclusionIntensity = c.aoOff ~= 0
        f.comp.PostProcessSettings.AmbientOcclusionIntensity = 0.0
        f.comp.PostProcessSettings.bOverride_ScreenSpaceReflectionIntensity = c.ssrOff ~= 0
        f.comp.PostProcessSettings.ScreenSpaceReflectionIntensity = 0.0
    end)
    if f.face == next(state.faces) then
        log(string.format("cameras: %s; LOD distance x%.2f, ambient occlusion %s, screen-space reflections %s, state %s, lighting %.2f",
            c.perspective ~= 0 and "perspective" or "orthographic", c.lodFactor, c.aoOff ~= 0 and "off" or "on",
            c.ssrOff ~= 0 and "off" or "on", c.persistState ~= 0 and "kept" or "dropped", c.lighting))
    end
end

-- A perspective camera's field of view and near clip plane: the plane on the volume's near face, less 2%, so the
-- level between the camera and the volume is cut away as the orthographic cameras' section cut does.
local function lens(f, g, near)
    if not g.persp then
        for _, cmp in ipairs(comps(f)) do pcall(function() cmp.bOverride_CustomNearClippingPlane = false end) end
        return
    end
    local ok = pcall(function()
        for _, cmp in ipairs(comps(f)) do
            cmp.FOVAngle = g.fovDeg
            cmp.bOverride_CustomNearClippingPlane = true
            cmp.CustomNearClippingPlane = near * 100 * 0.98
        end
    end)
    if not ok and not state.lensLogged then
        state.lensLogged = true
        log("perspective lens: FOVAngle / CustomNearClippingPlane not settable on this engine -- walls will block the cameras")
    end
end

local function place(c, g, centre)
    for face, f in pairs(state.faces) do
        quality(c, f)
        local pos, rot
        if face >= 4 then
            local sign = (face == 4) and 1 or -1
            pos = { X = centre.X, Y = centre.Y, Z = centre.Z + sign * g.topDist * 100 }
            -- Look straight down (up), image up along world +Y, as the plugin's
            -- MakeFromXZ((0,0,-+1), (0,1,0)): a rotator's up at pitch -+90 is (+-cos yaw, +-sin yaw, 0),
            -- so yaw 90 looking down and -90 looking up both put it on +Y.
            rot = { Pitch = -90 * sign, Yaw = 90 * sign + g.yawDeg, Roll = 0 }   -- the rig's yaw turns its image
            for _, cmp in ipairs(comps(f)) do
                cmp.OrthoWidth = 2 * g.R * 100
                cmp.MaxViewDistanceOverride = (g.topDist + g.ez) * 100 * (g.persp and 1.5 or 1.05)
            end
            lens(f, g, g.nTop)
        else
            local L = FACE_LOOK[face]
            -- The rig's yaw turns the base look about the vertical; the add-on turns the voxels back.
            local bx, by = L.X * g.cy - L.Y * g.sy, L.X * g.sy + L.Y * g.cy
            local lx, ly, lz = bx * g.ct, by * g.ct, -g.st          -- tilted look
            local fs = g.side[face]
            pos = { X = centre.X - lx * fs.sideDist * 100,
                    Y = centre.Y - ly * fs.sideDist * 100,
                    Z = centre.Z - lz * fs.sideDist * 100 }
            rot = { Pitch = math.deg(math.asin(lz)), Yaw = math.deg(math.atan(ly, lx)), Roll = 0 }
            for _, cmp in ipairs(comps(f)) do
                cmp.OrthoWidth = 2 * fs.halfWin * 100
                cmp.MaxViewDistanceOverride = (fs.sideDist + fs.halfDepth) * 100 * (g.persp and 1.5 or 1.05)
            end
            lens(f, g, fs.nSide)
        end
        f.actor:K2_SetActorLocationAndRotation(pos, rot, false, {}, true)
        if f.pactor then f.pactor:K2_SetActorLocationAndRotation(pos, rot, false, {}, true) end
        if f.sactor then f.sactor:K2_SetActorLocationAndRotation(pos, rot, false, {}, true) end
        if f.gactor then f.gactor:K2_SetActorLocationAndRotation(pos, rot, false, {}, true) end
    end
end

-- SEE-THROUGH SURFACES. A translucent material writes no depth, so a camera lays its colour on whatever is behind it:
-- a lightsaber's blade on the wall behind it, a glass door nowhere at all. The rig keeps a list of the see-through
-- materials it finds - every mesh and particle component, looked at once (the world looked through when the rig starts
-- and every few seconds after) - and writes it beside the app's control file for the app's list. For the solid capture, and the
-- player capture, the chosen ones (or all) are made opaque: each such slot of each component within reach of the volume
-- set to the engine's default material, the captures made, and set back at once. A capture sends the pending render
-- changes first, so it sees the swap; the player's own view, rendered at the end of the frame, never does. The colour
-- still comes from the lit capture, so a blade keeps its glow; only its depth comes from the opaque one.
local SEE_CLASSES = { "StaticMeshComponent", "InstancedStaticMeshComponent", "HierarchicalInstancedStaticMeshComponent",
                      "SkeletalMeshComponent", "PoseableMeshComponent", "ProceduralMeshComponent", "SplineMeshComponent",
                      "ParticleSystemComponent", "NiagaraComponent" }
-- Within reach of the volume: a component's origin no further than the volume's half-diagonal and this. Its bounds are
-- not to be read from here (Bounds is no reflected property: read on a skeletal mesh, it crashed Jedi: Fallen Order
-- inside UE4SS, 2026-10-15), and few meshes reach further than this from their origin.
local SEE_FAR_CM = 10000
local SEE_RESCAN_S = 10      -- seconds between looks through the whole world for components not yet seen
local see = { comps = {}, held = {}, mats = {}, blend = {}, chosen = {}, chosenText = nil,
              scanned = false, scanWhy = "the rig started", rescanAt = 0, changed = false, reportAt = 0,
              opaque = nil, active = false }

-- By full name, not address: a new component (a bolt) can take a gone one's address.
local function keyOf(o)
    local ok, k = pcall(function() return o:GetFullName() end)
    return ok and k or nil
end

-- "MaterialInstanceConstant /Game/Path/MI_X.MI_X" -> "/Game/Path/MI_X.MI_X"; the class name alone for an actor.
local function pathOf(o) local f = o:GetFullName(); return f:match("^%S+%s+(.+)$") or f end
local function classOf(o) return (o:GetFullName():match("^(%S+)")) end

-- A material's lasting name. A dynamic instance is made while the game runs, named after its actor and a number that
-- changes every session (Jedi: Fallen Order's blade: ...Saber_Blade.MaterialInstanceDynamic_2147481739), so it goes by
-- the asset it was made from: a choice made once holds in every session, and for every copy.
local function materialPath(m)
    local p = m
    for _ = 1, 8 do
        if classOf(p) ~= "MaterialInstanceDynamic" then break end
        local ok, parent = pcall(function() return p.Parent end)
        if not (ok and parent and parent:IsValid()) then break end
        p = parent
    end
    return pathOf(p)
end

-- The material's blend mode (EBlendMode: 0 opaque, 1 masked; 2 translucent, 3 additive, 4 modulate, 5 alpha composite,
-- 6 alpha holdout: see-through), an instance's own override first.
local function blendMode(m)
    local mode
    pcall(function()
        local o = m.BasePropertyOverrides
        if o.bOverride_BlendMode then mode = o.BlendMode end
    end)
    if mode == nil then pcall(function() mode = m:GetBaseMaterial().BlendMode end) end
    return tonumber(mode)
end

-- One component looked at, once: its see-through slots kept (by its name: see SEE-THROUGH SURFACES), each material
-- added to the list. Called only with an object found in this tick. Returns its key and entry (false: nothing
-- see-through), known or new.
local function look(comp)
    if not comp or not comp:IsValid() then return nil end
    local key = keyOf(comp)
    if not key then return nil end
    if see.comps[key] ~= nil then return key, see.comps[key] end
    local entry = false
    pcall(function()
        local n = comp:GetNumMaterials()
        if not n or n <= 0 then return end
        local cls = classOf(comp)
        local kind = (cls:find("Particle") or cls:find("Niagara")) and "particle" or "mesh"
        for i = 0, n - 1 do
            local m = comp:GetMaterial(i)
            if m and m:IsValid() then
                local path = materialPath(m)
                local mode = see.blend[path]
                if mode == nil then mode = blendMode(m) or 0; see.blend[path] = mode end
                if mode >= 2 then
                    if not entry then
                        local owner = "?"
                        pcall(function() local o = comp:GetOwner(); if o and o:IsValid() then owner = classOf(o) end end)
                        entry = { slots = {}, owner = owner }
                    end
                    entry.slots[#entry.slots + 1] = { i = i, path = path }
                    local rec = see.mats[path] or { kind = kind, owners = {}, n = 0 }
                    rec.owners[entry.owner] = true
                    see.mats[path] = rec
                    see.changed = true
                end
            end
        end
    end)
    see.comps[key] = entry
    return key, entry
end

-- The engine's default material: opaque, and made for every kind of mesh and particle.
local function opaqueMaterial()
    if see.opaque == nil then
        see.opaque = false
        for _, p in ipairs({ "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial",
                             "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial" }) do
            local ok, m = pcall(StaticFindObject, p)
            if ok and m and m:IsValid() then
                see.opaque = m
                log("see-through surfaces: made opaque with " .. p)
                break
            end
        end
        if not see.opaque then log("see-through surfaces: no opaque stand-in material, so they stay see-through") end
    end
    return see.opaque or nil
end

-- The list, its looking, its tidying and its report: while the app wants the list (seeList) or anything is chosen.
local function seeWork(c)
    see.active = c.seeList ~= 0 or useSolid(c)
    if not see.active then return end
    if c.solidMaterials ~= see.chosenText or (c.solidAll ~= 0) ~= see.allChosen then
        see.chosenText, see.allChosen = c.solidMaterials, c.solidAll ~= 0
        see.chosen = {}
        for p in c.solidMaterials:gmatch("[^|]+") do see.chosen[p] = true end
        see.scanned = false   -- the chosen ones taken up at once, not at the next look round
    end
    local second = math.max(1, c.hz) * TICKS_PER_SHOT
    -- The world looked through again every SEE_RESCAN_S, as well as when the player changes (scanWhy): a game that streams
    -- its levels into one world keeps the rig's cameras across a load (Jedi: Fallen Order, 2026-10-16: after a save was
    -- loaded nothing of its level was listed, the blade included), and actors come into play all the time (an enemy's
    -- lightsaber). Those already looked at are passed over by name. Not UE4SS's NotifyOnNewObject: it calls back on
    -- whichever thread made the object, and The Ascent makes them on its loading thread - the rig crashed the game as it
    -- started (2026-10-15). Nor its BeginPlay hook: it listed nothing in Jedi: Fallen Order, and with it on, the game
    -- crashed inside UE4SS in combat (2026-10-16, a lock released by a thread that did not hold it).
    -- All in this one tick: what FindAllOf hands back is safe to ask only until the engine next collects its garbage
    -- (looked at over the following ticks, some were gone by then: find, above).
    if state.ticks >= see.rescanAt then see.scanned = false end
    if not see.scanned then
        see.scanned = true
        see.rescanAt = state.ticks + SEE_RESCAN_S * second
        local n = 0
        local all = c.solidAll ~= 0
        see.held = {}
        for _, rec in pairs(see.mats) do rec.n = 0 end
        for _, cls in ipairs(SEE_CLASSES) do
            local ok, list = pcall(FindAllOf, cls)
            if ok and list then
                for _, o in ipairs(list) do
                    local key, e = look(o)
                    if e then
                        local wanted = all
                        for _, s in ipairs(e.slots) do
                            see.mats[s.path].n = see.mats[s.path].n + 1
                            wanted = wanted or see.chosen[s.path] == true
                        end
                        if wanted then see.held[key] = o end
                    end
                end
                n = n + #list
            end
        end
        see.changed = true
        if see.scanWhy then log(string.format("see-through surfaces: %d components looked through (%s)", n, see.scanWhy)) end
        see.scanWhy = nil
    end
    if see.changed and state.ticks >= see.reportAt then
        see.reportAt = state.ticks + 2 * second
        see.changed = false
        local rows = {}
        for path, rec in pairs(see.mats) do
            local owners = {}
            for o in pairs(rec.owners) do owners[#owners + 1] = o end
            table.sort(owners)
            rows[#rows + 1] = table.concat({ path, rec.kind, table.concat(owners, ","), tostring(rec.n) }, "\t")
        end
        table.sort(rows)
        local dir = (c.controlFile ~= "" and c.controlFile:match("^(.*)[\\/][^\\/]*$")) or "."
        local h = io.open(dir .. "\\see_through_" .. c.hostGame .. ".txt", "w")
        if h then
            h:write("; see-through materials the rig has found: path, mesh or particle, the actors carrying it, components now\n")
            h:write(table.concat(rows, "\n") .. "\n")
            h:close()
        end
    end
end

-- A new player or a new world (a load): every component listed so far forgotten - the last scene's may be being torn
-- down, and one found again by name in the middle of that and asked anything crashed Jedi: Fallen Order inside UE4SS
-- as a save loaded (2026-10-16, twice) - and the world looked through again once the load has had SEE_SETTLE_S to
-- finish, not in the middle of it.
local SEE_SETTLE_S = 3
local function seeAfresh(c, why)
    see.comps, see.held = {}, {}
    see.scanned, see.scanWhy = true, why
    see.rescanAt = state.ticks + SEE_SETTLE_S * math.max(1, c.hz) * TICKS_PER_SHOT
end

-- The chosen see-through materials (or all) made opaque on every component within reach of the volume; what was there
-- is kept, to be set back by restoreSolid straight after the captures. Where a component is, is asked about once a
-- second, at a time of its own, not every capture: asking each of The Ascent's thousands every capture took the game
-- from 200 frames a second to 23 (2026-10-15). A new one is asked at once, and SEE_FAR_CM covers the rest's travel.
local function makeSolid(c, centre, g)
    if not useSolid(c) then return nil end
    local mat = opaqueMaterial()
    if not mat then return nil end
    local reach = math.sqrt(g.ex * g.ex + g.ey * g.ey + g.ez * g.ez) * g.s * 100 * 1.1 + SEE_FAR_CM   -- cm
    local all = c.solidAll ~= 0
    local second = math.max(1, c.hz) * TICKS_PER_SHOT
    local todo, shown, nShown = {}, {}, 0
    for key, comp in pairs(see.held) do
        local e = see.comps[key]
        if e then
            pcall(function()
                -- Asked every tick, so one the game destroys is let go while its object is still there to ask.
                if not comp:IsValid() then see.held[key] = nil; return end
                if state.ticks >= (e.nearAt or 0) then
                    local l = comp:K2_GetComponentLocation()
                    local dx, dy, dz = l.X - centre.X, l.Y - centre.Y, l.Z - centre.Z
                    e.near = math.sqrt(dx * dx + dy * dy + dz * dz) <= reach
                    e.nearAt = state.ticks + second + math.random(0, second)
                end
                if not e.near then return end
                -- Hidden (a lightsaber's blade while it is put away): nothing to draw, and with nothing drawn the solid
                -- and glow captures are skipped - in Jedi: Fallen Order each is a whole render of the scene.
                local shownNow = true
                pcall(function() shownNow = comp:IsVisible() end)
                if not shownNow then return end
                for _, s in ipairs(e.slots) do
                    if all or see.chosen[s.path] then
                        todo[#todo + 1] = { comp, s.i }
                        if not shown[key] then shown[key], nShown = comp, nShown + 1 end
                    end
                end
            end)
        end
    end
    -- What the solid and glow captures draw: these components, listed again only when they change.
    for _, f in pairs(state.faces) do
        if f.scomp then
            local same = f.shownN == nShown
            if same then for key in pairs(shown) do if not f.shown[key] then same = false; break end end end
            if not same then
                f.shown, f.shownN = shown, nShown
                for _, cap in ipairs({ f.scomp, f.gcomp or false }) do
                    if cap then
                        pcall(function() cap:ClearShowOnlyComponents() end)
                        for _, comp in pairs(shown) do pcall(function() cap:ShowOnlyComponent(comp) end) end
                    end
                end
            end
        end
    end
    -- THEIR GLOW, while they are still as the game draws them; skipped when nothing is near, as the solid capture is.
    for _, f in pairs(state.faces) do
        if f.gcomp then
            if nShown > 0 or f.glowDrawn ~= false then pcall(function() f.gcomp:CaptureScene() end) end
            f.glowDrawn = nShown > 0
        end
    end
    local swapped = {}
    for _, t in ipairs(todo) do
        pcall(function()
            local was = t[1]:GetMaterial(t[2])
            t[1]:SetMaterial(t[2], mat)
            swapped[#swapped + 1] = { t[1], t[2], was }
        end)
    end
    return swapped
end

local function restoreSolid(swapped)
    for k = #swapped, 1, -1 do
        local r = swapped[k]
        pcall(function() r[1]:SetMaterial(r[2], r[3]) end)
    end
end

-- PLAYER LOOK: the player - the pawn, or with followClass every visible actor of the class (both fighters) - and the
-- actors attached to them (a drawn weapon, a lightsaber): the owners of the components hung under theirs, walked
-- through each component's AttachChildren - a property, read where it lives. Not GetAttachedActors: once the player had
-- actors attached (Jedi: Fallen Order's menu, 2026-10-15), what its out-array handed back crashed the game inside UE4SS
-- (a null read), where this walk lists them.
local function playerActors(c, pawn)
    local roots = { pawn }
    if c.followClass ~= "" then
        roots = {}
        for _, a in ipairs(state.followList or {}) do
            local ok, shown = pcall(function() return a:IsValid() and not a.bHidden end)
            if ok and shown then roots[#roots + 1] = a end
        end
    end
    local actors, seenActor, seenComp = {}, {}, {}
    for _, r in ipairs(roots) do
        local name = r:GetFullName()
        if not seenActor[name] then seenActor[name] = true; actors[#actors + 1] = r end
    end
    local players = #actors
    local function walk(comp)
        local key = comp:GetFullName()
        if seenComp[key] then return end
        seenComp[key] = true
        comp.AttachChildren:ForEach(function(_, elem)
            local child = elem:get()
            if child and child:IsValid() then
                local owner = child:GetOwner()
                if owner and owner:IsValid() then
                    local name = owner:GetFullName()
                    if not seenActor[name] then seenActor[name] = true; actors[#actors + 1] = owner end
                end
                walk(child)
            end
        end)
    end
    for _, r in ipairs(roots) do
        local ok, err = pcall(function() walk(r.RootComponent) end)
        if not ok and tostring(err) ~= state.attachErr then
            state.attachErr = tostring(err)
            log("player look: the actors attached to the player cannot be listed, so only the player is: " .. state.attachErr)
        end
    end
    return actors, players
end

-- What the player captures render, listed again once a second, since a weapon is drawn and put away and the pawn can
-- change.
local function showOnly(c, pawn)
    if c.playerLook == 0 or state.ticks < (state.showOnlyAt or 0) then return end
    state.showOnlyAt = state.ticks + math.max(1, c.hz) * TICKS_PER_SHOT
    local actors, players = playerActors(c, pawn)
    local n = 0
    for _, f in pairs(state.faces) do
        if f.pcomp then
            pcall(function() f.pcomp:ClearShowOnlyComponents() end)
            for _, a in ipairs(actors) do
                local ok = pcall(function() f.pcomp:ShowOnlyActorComponents(a, true) end)
                if not ok then pcall(function() f.pcomp:ShowOnlyActorComponents(a) end) end
            end
            n = n + 1
        end
    end
    if n > 0 and #actors ~= state.showOnlyCount then
        state.showOnlyCount = #actors
        log(string.format("player look: the player captures show %d player actor(s) and %d attached", players, #actors - players))
    end
end

local function tick()
    -- Settings re-read once a capture, so the volume follows the app's rig-scale keys while they are held (two small
    -- files, about 30 times a second); never per frame.
    if not cfg or state.ticks % TICKS_PER_SHOT == 0 then
        local c, stamp = readConfig()
        if not c then
            if not cfg then log("no " .. CFG_NAME .. " in the working directory -- rig idle"); cfg = DEFAULTS end
        elseif stamp ~= cfgStamp then
            local scaleOnly = cfgStamp ~= "" and withoutScale(stamp) == withoutScale(cfgStamp)
            cfg, cfgStamp = c, stamp
            if not scaleOnly then log(string.format("settings: radius %.1f m, scale %.2f/%.2f/%.2f, display %.1f x %.1f, feet (%.2f, %.2f, %.2f), faces %s, %d px, %s, tilt %.0f, yaw %.0f%s, %d Hz, camera distance %.2f%s",
                c.radiusM, c.scaleX, c.scaleY, c.scaleZ, c.boundR, c.boundZ, c.feetX, c.feetY, c.feetZ, c.faces, c.faceRes,
                c.perspective ~= 0 and "perspective" or "orthographic",
                c.tiltDeg, c.yawDeg, c.turnWithPlayer ~= 0 and " turning with the player" or "", c.hz, c.camDist,
                c.controlFile ~= "" and " (host panel over the ini)" or "")) end
        end
        if cfg.hz ~= state.hz then startLoop(cfg.hz) end
    end
    local c = cfg
    state.ticks = state.ticks + 1
    if c.on == 0 then if state.key then destroyAll(); log("off") end return end

    -- FindFirstOf, not UEHelpers.GetPlayerController: that one raises an error when there is no
    -- controller yet, which in the main menu and every load screen is 30 errors a second.
    -- ...and GetPlayerPawn(…, 0), not that controller's .Pawn: FindFirstOf can return another
    -- controller, and in The Ascent its pawn sat at (0, 0, 120362) cm, a kilometre above the level
    -- (measured 2026-10-10), while GetPlayerPawn named KallamariPlayerCharacter_C.
    -- Kept between ticks, since most ticks only check the clock and FindFirstOf searches the objects.
    local pc = state.pc
    if not pc or not pc:IsValid() then
        pc = FindFirstOf("PlayerController")
        if not pc or not pc:IsValid() then waiting("no PlayerController yet"); return end   -- menus, loading
        state.pc = pc
    end

    local fp, pawn = subject(c, pc)
    if not pawn then waiting(c.followClass ~= "" and ("no visible " .. c.followClass) or "no player pawn", pc); return end

    -- Is a capture due? Timed by the real-time clock of the PLAYER's world. Read off the controller FindFirstOf
    -- returned, the clock stood still - a world that is not ticking - and every tick captured: 85 a second per
    -- camera against 30 asked (measured 2026-10-11). A clock that has not moved since the last tick, or cannot be
    -- read, falls back to every TICKS_PER_SHOT-th tick once it has stayed that way for a while.
    local period = 1 / math.max(1, c.hz)
    local okNow, now = pcall(function() return lib("GameplayStatics"):GetRealTimeSeconds(pawn) end)
    if okNow and type(now) == "number" and now ~= state.lastNow then
        state.lastNow, state.still = now, 0
        if state.nextShot and now < state.nextShot then
            if state.nextShot - now <= 2 * period then return end
            state.nextShot = now                -- far behind the schedule: a new world's clock, start again
        end
        state.nextShot = math.max((state.nextShot or now) + period, now + period / 2)
    else
        state.still = (state.still or 0) + 1
        if state.still <= 2 * TICKS_PER_SHOT or state.ticks % TICKS_PER_SHOT ~= 0 then return end
    end
    -- By name, not by value: UE4SS hands back a fresh wrapper each call, so `~=` is always true.
    local name = c.followClass ~= "" and ("the visible " .. c.followClass .. "s") or pawn:GetFullName()
    if name ~= state.pawnName then
        state.pawnName = name
        log(string.format("following %s at (%.0f, %.0f, %.0f) cm", name, fp.X, fp.Y, fp.Z))
        seeAfresh(c, "a new player")   -- a load: the world looked through again
    end

    if state.grow ~= c.growTargets then state.grow = c.growTargets; sceneTargets(c) end

    -- A level change destroys the actors with the world: rebuild, and the see-through list's components looked for again.
    local stale = false
    for _, f in pairs(state.faces) do if not f.actor:IsValid() then stale = true end end
    if stale then seeAfresh(c, "a new world") end
    if (stale or state.key ~= buildKey(c)) and state.ticks >= (state.retryAt or 0) then
        build(c, pawn)
    end
    if not state.key then return end
    showOnly(c, pawn)

    local g = geometry(c)
    -- Turning with the player, the rig turns by the player's view as well as by its own yaw. The add-on turns the
    -- finished volume back by the yaw alone, so the view lands on display -Y (capture_gpu.hpp, DisplayTurnDeg), and
    -- the display is the world turned by the view and a quarter turn. Off, the display keeps the world's axes.
    local rigYaw, dispTurn = c.yawDeg, c.yawDeg
    if c.turnWithPlayer ~= 0 then
        state.heading = heading(pawn) or state.heading or 0
        rigYaw, dispTurn = c.yawDeg + state.heading, c.yawDeg - 90
        g.yawDeg, g.cy, g.sy = rigYaw, math.cos(math.rad(rigYaw)), math.sin(math.rad(rigYaw))
    end
    -- The volume's centre: the feet, less where they stand in the display (+Z down) - the add-on's mapping undone: the
    -- display's turn back into the rig's frame, each axis's SCALE, then the rig's yaw into the world.
    local tA, yA = math.rad(dispTurn), math.rad(rigYaw)
    local u = (c.feetX * math.cos(tA) + c.feetY * math.sin(tA)) * (g.ex / g.R)
    local w = (-c.feetX * math.sin(tA) + c.feetY * math.cos(tA)) * (g.ey / g.R)
    local ox = (u * math.cos(yA) - w * math.sin(yA)) * g.s * 100
    local oy = (u * math.sin(yA) + w * math.cos(yA)) * g.s * 100
    local centre = { X = fp.X - ox, Y = fp.Y - oy, Z = fp.Z + c.feetZ * g.s * (g.ez / g.Z) * 100 }
    place(c, g, centre)

    seeWork(c)
    if state.handshake > 0 then state.handshake = state.handshake - 1; return end
    for _, f in pairs(state.faces) do
        f.comp:CaptureScene()
        if f.surf then f.surf:CaptureScene() end
    end
    -- The solid and player captures with the chosen see-through materials opaque, and those set back straight after.
    local swapped = makeSolid(c, centre, g)
    -- A solid capture with nothing to draw is skipped (each costs a capture's fixed share of a frame, drawing nothing),
    -- after one that leaves its target empty.
    local okCap, errCap = pcall(function()
        for _, f in pairs(state.faces) do
            if f.scomp then
                local drawn = (f.shownN or 0) > 0
                if drawn or f.solidDrawn ~= false then f.scomp:CaptureScene() end
                f.solidDrawn = drawn
            end
            if f.pcomp then f.pcomp:CaptureScene() end
        end
    end)
    if swapped then restoreSolid(swapped) end
    if not okCap then error(errCap, 0) end
    if swapped and #swapped ~= state.swappedCount and state.ticks >= (state.swappedLogAt or 0) then
        state.swappedCount, state.swappedLogAt = #swapped, state.ticks + 10 * math.max(1, c.hz) * TICKS_PER_SHOT
        log(string.format("see-through surfaces: %d made opaque for the solid and player captures", #swapped))
    end
    state.shots = state.shots + 1

    -- SNAPSHOT: what each camera sees, as an image (KismetRenderingLibrary.ExportRenderTarget; .hdr for these float
    -- targets, colour only; the player and solid captures' depth, cm, in red), into a snapshots folder beside the app,
    -- whenever the settings' snapshot number changes.
    -- Its read-back waits for the captures just asked for, so the pictures are this tick's.
    if state.snapshot == nil then state.snapshot = c.snapshot end
    if c.snapshot ~= state.snapshot then
        state.snapshot = c.snapshot
        local dir = ((c.controlFile ~= "" and c.controlFile:match("^(.*)[\\/][^\\/]*$")) or ".") .. "\\snapshots"
        for face, f in pairs(state.faces) do
            for _, shot in ipairs({ { f.rt, "" }, { f.srt, "_surface" }, { f.prt, "_player" }, { f.sdrt, "_solid" }, { f.grt, "_glow" } }) do
                if shot[1] then
                    local file = string.format("face%d_%d%s.hdr", face, c.snapshot, shot[2])
                    local ok, err = pcall(function() lib("KismetRenderingLibrary"):ExportRenderTarget(pawn, shot[1], dir, file) end)
                    log(string.format("snapshot: face %d -> %s\\%s%s", face, dir, file, ok and "" or (" FAILED: " .. tostring(err))))
                end
            end
        end
    end

    -- Counted in captures, not os.clock(): in this Lua that is the PROCESS's CPU time, which a
    -- many-threaded game runs several times faster than the wall. Every 5 s at the full rate, with the rate
    -- actually reached by the game's clock.
    if state.shots % (5 * math.max(1, c.hz)) == 0 then
        local t = state.lastNow
        local rate = (t and state.logAt and t > state.logAt) and (5 * math.max(1, c.hz) / (t - state.logAt)) or 0
        state.logAt = t
        log(string.format("capturing: centre (%.0f, %.0f, %.0f) cm, scale %.2f m/unit, %.1f captures/s%s%s",
            centre.X, centre.Y, centre.Z, g.s, rate, (state.still or 0) > 2 * TICKS_PER_SHOT and " (tick-timed)" or "",
            c.turnWithPlayer ~= 0 and string.format(", view %.0f deg", state.heading) or ""))
    end
end

-- LoopAsync's period is fixed when the loop starts, so a new rate retires the old loop (it sees a
-- newer generation and returns true) and starts another. `pending` keeps a slow game thread from
-- building a backlog of queued ticks: one waits, the rest are dropped. It ticks TICKS_PER_SHOT times per
-- capture; tick() decides which tick captures.
startLoop = function(hz)
    hz = math.max(1, math.min(120, hz))
    state.hz, state.loop = hz, state.loop + 1
    local gen = state.loop
    LoopAsync(math.max(5, math.floor(1000 / (hz * TICKS_PER_SHOT))), function()
        if gen ~= state.loop then return true end
        if state.pending then return false end
        state.pending = true
        ExecuteInGameThread(function()
            local ok, err = pcall(tick)
            state.pending = false
            -- Each distinct error once: at 30 Hz a repeating one buries everything else in the log.
            if not ok and tostring(err) ~= state.lastErr then
                state.lastErr = tostring(err)
                log("tick threw: " .. state.lastErr)
            end
        end)
        return false
    end)
    log(string.format("loop at %d Hz, capturing at %d Hz", hz * TICKS_PER_SHOT, hz))
end

-- Stopping retires the loop by generation (it returns true on its next tick) and removes the
-- cameras, so nothing is left running for a hot reload to wait on.
function M.Toggle()
    if state.hz > 0 then
        state.loop, state.hz = state.loop + 1, 0
        destroyAll()
        log("stopped (F7 starts it again)")
    else
        startLoop((cfg and cfg.hz) or DEFAULTS.hz)
    end
end

function M.Start()
    local c = readConfig()
    if c and c.autostart == 0 then
        log("armed, NOT started: autostart=0 -- F7 starts and stops the capture")
        return
    end
    startLoop(DEFAULTS.hz)
    log("armed -- reads " .. CFG_NAME .. " beside the game's exe; F7 stops and starts it")
end

return M
