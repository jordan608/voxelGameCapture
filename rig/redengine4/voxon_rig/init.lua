-- Voxon Capture Rig - the cube of cameras for Cyberpunk 2077 (REDengine 4), the script half: a Cyber Engine Tweaks mod.
--
-- REDengine 4 has cameras of its own that render the world into a texture (entRenderToTextureCameraComponent, the
-- holocall camera's kind), in the same frame as the player's view. This mod gives V five of them, one on each face of a
-- box round her, looking in, the near plane on the box's face - what stands between camera and box is cut away. The
-- add-on (addon/red4_rtt.hpp) finds each camera's picture and depth on the GPU by the picture's size, and builds the
-- volume.
--
-- Four things measured on 2.31 (2026-10-13/14) shape it:
--   * A component added to an entity that is already in the world is never attached (Codeware's AddComponent: a light
--     added to V lit nothing). Added while V is being assembled - a save loading - it is. So the cameras go on in
--     PlayerPuppet:OnRequestComponents, and a save has to be loaded once after the mod is installed.
--   * Cameras on one entity that share a virtualCameraName (unset, every one is "Component") all render the view of the
--     last one added: five on V gave five pictures of the top face's view. Each has a name of its own.
--   * A camera renders at its resolutionWidth x Height (100 x 100 unset) and its picture is scaled to its output
--     texture's size; both can be set while the game runs (the camera switched off and on again takes them). All the
--     cameras render at the largest one's resolution, into one shared depth buffer, so every face renders at faceRes;
--     each gets its own spare texture of the game's, faceRes + 1 + face texels square: the add-on knows a face's picture
--     by its size, and copies its depth as the picture is drawn.
--   * A running camera follows its moves, turns and near and far planes at once; its depth is reversed. Unset, its far
--     plane is about a kilometre away; each is given its own just past the box (red4_rtt.hpp, Red4FarM), which the add-on
--     decodes with, and nothing beyond the box is drawn.
--
-- The settings come from rig.txt in this folder, which the add-on writes from the app's (on=0 while the app is not
-- taking volumes). The camera maths is addon/sweep_red3.hpp's Red3Place, and the centre the Witcher rig's
-- (rig/redengine3/voxonRig.ws), term for term: the same world axes (metres, Z up), the same display frame.

local FACE_TEXTURE = {   -- spare render textures of the game's, one a face: none is drawn in ordinary play
    [0] = "base\\gameplay\\gui\\world\\delamain\\delamain_rendertexture.dtex",
    [1] = "base\\gameplay\\gui\\fullscreen\\credits\\final_boards_rendertexture.dtex",
    [2] = "base\\gameplay\\gui\\fullscreen\\main_menu\\gender_selection_male.dtex",
    [3] = "base\\gameplay\\gui\\fullscreen\\main_menu\\gender_selection_female.dtex",
    [4] = "base\\gameplay\\gui\\fullscreen\\main_menu\\preview.dtex",
}
local NEAR_SHARE = 0.98   -- the near plane this much of the way from the camera to the box's face (Red3Place)

local Rig = {
    cfg = { on = 0, radius = 12, boundR = 4, boundZ = 2, scaleX = 1, scaleY = 1, scaleZ = 1, feetX = 0, feetY = 0,
            feetZ = 1.5, yaw = 0, turn = 0, faces = 31, faceRes = 512, tilt = 0 },
    cfgText = nil,
    cams = {},            -- face -> the camera on V
    tokens = {},          -- face -> its texture, held loaded
    size = {},            -- face -> the size its texture was given
    live = {},            -- face -> switched on
    fresh = true,         -- V has just been assembled or spawned: her cameras are to be found again
    viewAngle = 0,
    clock = 0,
}

local function log(s) print("[voxon_rig] " .. s) end

-- rig.txt: key=value lines. Read 30 times a second, so the volume follows the app's rig-scale keys while they are held;
-- nothing changes unless the text does.
local function readSettings()
    local f = io.open("rig.txt", "r")
    if not f then Rig.cfg.on = 0; return end
    local text = f:read("*a")
    f:close()
    if text == Rig.cfgText then return end
    Rig.cfgText = text
    for k, v in text:gmatch("([%w_]+)%s*=%s*([^\r\n]+)") do
        if Rig.cfg[k] ~= nil then Rig.cfg[k] = tonumber(v) or Rig.cfg[k] end
    end
end

local function faceOn(face) return math.floor(Rig.cfg.faces / 2 ^ face) % 2 == 1 end

-- Quaternions (i, j, k, r) and vectors {x, y, z}.
local function qmul(a, b)
    return { i = a.r * b.i + a.i * b.r + a.j * b.k - a.k * b.j,
             j = a.r * b.j - a.i * b.k + a.j * b.r + a.k * b.i,
             k = a.r * b.k + a.i * b.j - a.j * b.i + a.k * b.r,
             r = a.r * b.r - a.i * b.i - a.j * b.j - a.k * b.k }
end
local function qconj(q) return { i = -q.i, j = -q.j, k = -q.k, r = q.r } end
local function qrot(q, v)
    local p = qmul(qmul(q, { i = v.x, j = v.y, k = v.z, r = 0 }), qconj(q))
    return { x = p.i, y = p.j, z = p.k }
end
-- The rotation whose columns are the camera's axes in the world: X its picture's right, Y its look, Z its picture's up.
local function qbasis(x, y, z)
    local m00, m01, m02 = x.x, y.x, z.x
    local m10, m11, m12 = x.y, y.y, z.y
    local m20, m21, m22 = x.z, y.z, z.z
    local t = m00 + m11 + m22
    if t > 0 then
        local s = math.sqrt(t + 1) * 2
        return { r = 0.25 * s, i = (m21 - m12) / s, j = (m02 - m20) / s, k = (m10 - m01) / s }
    elseif m00 > m11 and m00 > m22 then
        local s = math.sqrt(1 + m00 - m11 - m22) * 2
        return { r = (m21 - m12) / s, i = 0.25 * s, j = (m01 + m10) / s, k = (m02 + m20) / s }
    elseif m11 > m22 then
        local s = math.sqrt(1 + m11 - m00 - m22) * 2
        return { r = (m02 - m20) / s, i = (m01 + m10) / s, j = 0.25 * s, k = (m12 + m21) / s }
    end
    local s = math.sqrt(1 + m22 - m00 - m11) * 2
    return { r = (m10 - m01) / s, i = (m02 + m20) / s, j = (m12 + m21) / s, k = 0.25 * s }
end

-- The captured box's half-extents (capture_gpu.hpp, Derive).
local function extents()
    local c = Rig.cfg
    local s = c.radius / c.boundR
    return c.radius * c.scaleX, c.radius * c.scaleY, c.boundZ * s * c.scaleZ
end

-- The rig's yaw (radians, the display's frame): its own, and the player's view when turning with it.
local function rho()
    local r = math.rad(Rig.cfg.yaw)
    if Rig.cfg.turn > 0.5 then r = r + Rig.viewAngle end
    return r
end

-- The volume's centre: V's feet, less where they stand in the display - the compact pass undone: its turn, each axis's
-- scale, then the rig's yaw, then the display's frame back to the world's (voxonRig.ws, Centre).
local function centre(feet, r)
    local c = Rig.cfg
    local s = c.radius / c.boundR
    local ex, ey = extents()
    local t = math.rad(c.yaw)
    if c.turn > 0.5 then t = math.rad(c.yaw - 90) end
    local qx = (c.feetX * math.cos(t) + c.feetY * math.sin(t)) * ex / c.radius * s
    local qy = (-c.feetX * math.sin(t) + c.feetY * math.cos(t)) * ey / c.radius * s
    local qz = c.feetZ * c.scaleZ * s
    local rx = qx * math.cos(r) - qy * math.sin(r)
    local ry = qx * math.sin(r) + qy * math.cos(r)
    return { x = feet.x - rx, y = feet.y + ry, z = feet.z + qz }
end

-- A face's camera (sweep_red3.hpp, Red3Place, a square lens of 90 degrees): side face 0 on the rig's +X side, 1 on +Y,
-- 2 on -X, 3 on -Y, as the app names them, standing back from its face until the face fits the picture, looking in -
-- tilted, raised and looking down at the centre by the tilt; the top one looks straight down, its picture's top the
-- rig's +Y (the display's front).
local function place(face, ctr, r)
    local ex, ey, ez = extents()
    local cr, sr = math.cos(r), math.sin(r)
    local ax, ay = { x = cr, y = -sr, z = 0 }, { x = -sr, y = -cr, z = 0 }   -- the rig's X and Y, in the world
    local cam = {}
    if face < 4 then
        local a = (face % 2 == 0) and ax or ay
        local sign = face < 2 and -1 or 1   -- faces 0 and 1 on the + side, looking toward -
        local look = { x = a.x * sign, y = a.y * sign, z = 0 }   -- level
        local t = math.rad(math.min(math.max(Rig.cfg.tilt, 0), 90))
        local ct, st = math.cos(t), math.sin(t)
        local across = (face % 2 == 0) and ey or ex
        local along = (face % 2 == 0) and ex or ey
        local s = math.max(across, along * st + ez * ct)
        local dist = along * ct + ez * st + s
        cam.fwd = { x = look.x * ct, y = look.y * ct, z = -st }   -- tilted down
        cam.up = { x = look.x * st, y = look.y * st, z = ct }     -- the picture's top tips toward the look
        cam.right = { x = look.y, y = -look.x, z = 0 }
        cam.pos = { x = ctr.x - cam.fwd.x * dist, y = ctr.y - cam.fwd.y * dist, z = ctr.z - cam.fwd.z * dist }
        cam.near = s * NEAR_SHARE
        cam.dist = dist
    else
        local s = math.max(ex, ey)
        cam.pos = { x = ctr.x, y = ctr.y, z = ctr.z + ez + s }
        cam.fwd = { x = 0, y = 0, z = -1 }
        cam.right = { x = -ax.x, y = -ax.y, z = 0 }
        cam.up = ay
        cam.near = s * NEAR_SHARE
        cam.dist = ez + s
    end
    cam.far = (cam.dist + math.sqrt(ex * ex + ey * ey + ez * ez)) * 1.5   -- red4_rtt.hpp, Red4FarM
    return cam
end

-- The cameras, added while V is assembled (only then are they attached). Every player puppet is assembled this way -
-- menus and previews have their own - and V is the one with entity ID 1; and V herself is asked for her components
-- many times over while a save loads (21 on 2.31): the cameras go on the first time only.
local function addCameras(puppet)
    local ok, id = pcall(function() return tostring(puppet:GetEntityID().hash) end)
    if not ok or id ~= "1ULL" then return end
    if puppet:FindComponentByName(CName.new("voxon_face0")) then return end
    for face = 0, 4 do
        local cam = entRenderToTextureCameraComponent.new()
        cam.name = CName.new("voxon_face" .. face)
        cam.virtualCameraName = cam.name   -- its own: cameras that share one (all "Component" unset) share one view
        cam.fov = 90
        cam.aspectRatio = 1.0
        cam.dynamicTextureRes = FACE_TEXTURE[face]
        puppet:AddComponent(cam)
    end
    Rig.fresh = true
    log("five face cameras added to V")
end

local function allOff()
    for face, cam in pairs(Rig.cams) do
        if Rig.live[face] then pcall(function() cam:Toggle(false) end) end
        Rig.live[face] = false
    end
end

-- Each frame: the cameras found, sized, placed round V and switched on - or all off while the rig is not wanted.
local function tick(dt)
    Rig.clock = Rig.clock + dt
    if Rig.clock >= 1 / 30 then Rig.clock = 0; readSettings() end
    -- V's cameras, found again only when she is new: Game.GetPlayer() hands back a new object every frame, so it cannot
    -- tell (taken for a new V every frame, the cameras were resized and switched off and on every frame: 5 fps).
    local player = Game.GetPlayer()
    if not player then Rig.fresh = true; return end
    if Game.GetSystemRequestsHandler():IsPreGame() then allOff(); return end   -- the main menu's own V
    if Rig.fresh then
        Rig.fresh = false
        Rig.cams, Rig.size, Rig.live = {}, {}, {}
        for face = 0, 4 do Rig.cams[face] = player:FindComponentByName(CName.new("voxon_face" .. face)) end
    end
    if Rig.cfg.on < 0.5 or not Rig.cams[0] then allOff(); return end

    local dir = Game.GetCameraSystem():GetActiveCameraForward()
    if math.abs(dir.x) + math.abs(dir.y) > 0.01 then Rig.viewAngle = math.atan2(-dir.y, dir.x) end
    local r = rho()
    local feet = player:GetWorldPosition()
    local ctr = centre(feet, r)
    local vq = player:GetWorldOrientation()
    local inv = qconj({ i = vq.i, j = vq.j, k = vq.k, r = vq.r })
    for face = 0, 4 do
        local cam = Rig.cams[face]
        if cam and faceOn(face) then
            local want = math.floor(Rig.cfg.faceRes) + 1 + face
            if Rig.size[face] ~= want then
                Rig.tokens[face] = Rig.tokens[face] or Game.GetResourceDepot():LoadResource(FACE_TEXTURE[face])
                local token = Rig.tokens[face]
                if token:IsLoaded() then
                    local res = token:GetResource()
                    res.width, res.height = want, want
                    res.scaleToViewport = false
                    res.dataFormat = Enum.new("DynamicTextureDataFormat", "RGBA_Uint8_SRGB")
                    cam.resolutionWidth = math.floor(Rig.cfg.faceRes)   -- what it renders at (100 x 100 unset)
                    cam.resolutionHeight = cam.resolutionWidth
                    pcall(function() cam:Toggle(false) end)
                    Rig.live[face] = false
                    Rig.size[face] = want
                end
            end
            if Rig.size[face] == want then
                local c = place(face, ctr, r)
                local rel = qrot(inv, { x = c.pos.x - feet.x, y = c.pos.y - feet.y, z = c.pos.z - feet.z })
                local q = qmul(inv, qbasis(c.right, c.fwd, c.up))
                cam:SetLocalPosition(Vector4.new(rel.x, rel.y, rel.z, 1))
                cam:SetLocalOrientation(Quaternion.new(q.i, q.j, q.k, q.r))
                cam.nearPlaneOverride = c.near
                cam.farPlaneOverride = c.far
                if not Rig.live[face] then cam:Toggle(true); Rig.live[face] = true end
            end
        elseif cam and Rig.live[face] then
            pcall(function() cam:Toggle(false) end)
            Rig.live[face] = false
        end
    end
end

registerForEvent("onInit", function()
    ObserveBefore("PlayerPuppet", "OnRequestComponents", function(self) addCameras(self) end)
    ObserveAfter("PlayerPuppet", "OnGameAttached", function() Rig.fresh = true end)
    readSettings()
    log("loaded")
end)

local lastError = nil
registerForEvent("onUpdate", function(dt)
    local ok, err = pcall(tick, dt)
    if ok then return end
    if tostring(err) ~= lastError then lastError = tostring(err); log("tick: " .. lastError) end
    pcall(allOff)
end)

registerForEvent("onShutdown", function() allOff() end)
