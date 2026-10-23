# make_mod.py - builds the capture rig's Minecraft Dungeons II mod, Content/Mods/VoxonRig, inside a copy of the
# Blueprint Mod Template (Blueprint Loader's, Nexus Mods minecraftdungeons2/mods/2), from this script alone. Run it
# inside UnrealEditor-Cmd 5.6.1 (the PythonScript commandlet) with the VoxonGraph plugin in the project (README.md, beside this).
#
# The mod is rig.lua's cube of cameras (rig/unreal/rig.lua) as a Blueprint: the game ends itself when UE4SS hooks its
# code, but loads Blueprint Loader mods, so the cameras come in as game content (docs/RESULTS.md 25). The add-on finds
# their render targets exactly as it finds rig.lua's: by the face code each is cleared to.
#
# SETTINGS. A Blueprint cannot read the app's control file. The app works the rig out instead and writes it to the
# game's project folder as a small 16-bit PNG (host/VoxonCaptureRig/BlueprintBridge.cs), which the mod reads twice a
# second: ImportFileAsTexture2D, drawn into a float render target, read back. GLOBALS and PER_FACE below are that file's
# layout and must match BlueprintBridge.cs (LAYOUT_VERSION with them). Until the file is there the mod uses the
# profile's settings (rig/profiles/mcdungeons2.ini), worked out here the same way.
#
# Per face (rig.lua's numbering: 0 +X side, 1 +Y side, 2 -X side, 3 -Y side, 4 top, 5 bottom) three captures:
#   FaceN    lit colour and linear depth (SceneColorSceneDepth), cleared to 1000 + N
#   PlayerN  the characters' depth alone (show-only list: every Pawn - the player and the NPCs), cleared to 1500 + N;
#            captured with Player look on: the add-on gives what it covers the player look
#   SurfaceN the surfaces' own colour (BaseColor), cleared to 2000 + N; captured with Lighting below 1
#
# REBUILDS. The add-on looks for a face's code only in targets made in the last few seconds, and lets go of a face
# turned off for a while; rig.lua makes new targets whenever which faces, their size, Lighting below 1 or Player look
# change (its buildKey). So does this mod: new targets for every capture (CreateRenderTarget2D), cleared to their codes,
# captures held off for the handshake - at BeginPlay and whenever the app's file changes one of those.
import math
import unreal

MOD = "VoxonRig"
ROOT = f"/Game/Mods/{MOD}"

# rig/profiles/mcdungeons2.ini: the settings until the app's file is read
PROFILE = dict(radiusM=8.0, boundR=4.0, boundZ=2.0, tiltDeg=30.0, faceRes=512, camDist=1.0, yawDeg=0.0, feetX=0.0,
               feetY=0.0, feetZ=1.5, turn=False, faces=[True, True, True, True, True, False], playerLook=False,
               lighting=1.0, scale=(1.0, 1.0, 1.0))

# rig.lua's handshake: each target cleared once to (MARK_R, code, 0, MARK_A); captures held off meanwhile
MARK_R, MARK_A = -1234.0, -4321.0
CODES = dict(Face=1000.0, Player=1500.0, Surface=2000.0)
HANDSHAKE_S, POLL_S, SHOW_ONLY_S = 2.0, 0.5, 1.0
TICK_S = 1.0 / 30                  # the profile's hz until the app's file says otherwise

# BlueprintBridge.cs's layout
BRIDGE_FILE, BRIDGE_WIDTH, LAYOUT_VERSION, MAGIC = "voxon_rig.png", 32, 1, 0xA55A
GLOBALS = [("magic", 0, 65535), ("layout", 0, 65535), ("on", 0, 1), ("hz", 0, 240), ("yawDeg", -720, 720), ("turn", 0, 1),
           ("faceRes", 0, 8192), ("sideH", 0, 8192), ("playerLook", 0, 1), ("surface", 0, 1), ("perspective", 0, 1),
           ("capsuleHalf", 0, 1000)]
PER_FACE = [("on", 0, 1), ("x", -60000, 60000), ("y", -60000, 60000), ("z", -60000, 60000), ("pitch", -180, 180),
            ("yaw", -360, 360), ("fov", 0, 180), ("near", 0, 120000), ("reach", 0, 240000), ("orthoWidth", 0, 240000)]
LAYOUT = GLOBALS + [(f"face{f}.{n}", lo, hi) for f in range(6) for n, lo, hi in PER_FACE]
SLOT = {name: i for i, (name, _, _) in enumerate(LAYOUT)}

lib = unreal.VoxonGraphLib
log = unreal.log


def bridge_values(p):
    """BlueprintBridge.Values, term for term: what the app would write for these settings (a VX2-XL's size)."""
    sc = p["radiusM"] / p["boundR"]
    R, Z = p["boundR"] * sc, p["boundZ"] * sc
    ex, ey, ez = R * p["scale"][0], R * p["scale"][1], Z * p["scale"][2]
    t = math.radians(max(0.0, min(90.0, p["tiltDeg"])))
    ct, st = math.cos(t), math.sin(t)
    face_res = p["faceRes"]
    side_h = face_res // 2 if 2 * p["boundZ"] <= p["boundR"] * 1.001 else face_res
    k = max(1.0, p["camDist"])
    n_top = max(ex, ey) * k
    top_dist = ez + n_top
    fov = math.degrees(2 * math.atan(1 / k))
    disp = math.radians(p["yawDeg"] - 90 if p["turn"] else p["yawDeg"])
    u = (p["feetX"] * math.cos(disp) + p["feetY"] * math.sin(disp)) * (ex / R)
    w = (-p["feetX"] * math.sin(disp) + p["feetY"] * math.cos(disp)) * (ey / R)
    off_x, off_y, rise = u * sc * 100, w * sc * 100, p["feetZ"] * sc * (ez / Z) * 100
    v = [MAGIC, LAYOUT_VERSION, 1, 30, p["yawDeg"], 1 if p["turn"] else 0, face_res, side_h, 1 if p["playerLook"] else 0,
         1 if p["lighting"] < 0.999 else 0, 1, 90.0]
    for face in range(6):
        if face >= 4:
            sign = 1 if face == 4 else -1
            x, y, z, pitch, yaw = 0.0, 0.0, sign * top_dist * 100, -90.0 * sign, 90.0 * sign
            near, reach, ortho = n_top, (top_dist + ez) * 100 * 1.5, 2 * R * 100
        else:
            along, across = (ex, ey) if face % 2 == 0 else (ey, ex)
            half_depth, vert_half = along * ct + ez * st, along * st + ez * ct
            half_win = max(across, vert_half * face_res / side_h)
            n_side = half_win * k
            side_dist = half_depth + n_side
            lx0, ly0 = {0: (-1, 0), 1: (0, -1), 2: (1, 0), 3: (0, 1)}[face]
            lx, ly, lz, d = lx0 * ct, ly0 * ct, -st, side_dist * 100
            x, y, z = -lx * d, -ly * d, -lz * d
            pitch, yaw = math.degrees(math.asin(lz)), math.degrees(math.atan2(ly, lx))
            near, reach, ortho = n_side, (side_dist + half_depth) * 100 * 1.5, 2 * half_win * 100
        v += [1 if p["faces"][face] else 0, x - off_x, y - off_y, z + rise, pitch, yaw, fov, near * 100 * 0.98, reach, ortho]
    return v


# ── The assets ───────────────────────────────────────────────────────────────────────────────────────────────────

def fresh(path, template):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    return unreal.EditorAssetLibrary.duplicate_asset(template, path)


def render_target(name, w, h, fmt, clear):
    path = f"{ROOT}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    rt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, ROOT, unreal.TextureRenderTarget2D,
                                                                 unreal.TextureRenderTargetFactoryNew())
    rt.set_editor_property("size_x", w)
    rt.set_editor_property("size_y", h)
    rt.set_editor_property("render_target_format", fmt)
    rt.set_editor_property("clear_color", clear)
    rt.set_editor_property("target_gamma", 1.0)
    rt.set_editor_property("auto_generate_mips", False)
    unreal.EditorAssetLibrary.save_loaded_asset(rt, False)
    return rt


def add_component(bp, sub, parent, cls, name):
    handle, why = sub.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=parent, new_class=cls, blueprint_context=bp))
    if not unreal.SubobjectDataBlueprintFunctionLibrary.is_handle_valid(handle):
        raise RuntimeError(f"{name}: not added ({why})")
    sub.rename_subobject(handle, unreal.Text(name))
    return handle, unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle))


def capture_defaults(comp, kind, vals, face):
    comp.set_editor_property("capture_every_frame", False)       # the rig's own clock: the Tick; targets: the rebuild
    comp.set_editor_property("capture_on_movement", False)
    comp.set_editor_property("projection_type", unreal.CameraProjectionMode.PERSPECTIVE)
    comp.set_editor_property("fov_angle", vals[SLOT[f"face{face}.fov"]])
    # rig.lua's lens(): the near plane on the volume's near face, so walls outside the volume do not block it
    comp.set_editor_property("override_custom_near_clipping_plane", True)
    comp.set_editor_property("custom_near_clipping_plane", vals[SLOT[f"face{face}.near"]])
    comp.set_editor_property("max_view_distance_override", vals[SLOT[f"face{face}.reach"]])
    try:
        comp.set_editor_property("use_ray_tracing_if_enabled", False)
    except Exception:
        pass
    if kind == "Face":
        comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_SCENE_COLOR_SCENE_DEPTH)
        comp.set_editor_property("post_process_blend_weight", 1.0)
        pp = comp.get_editor_property("post_process_settings")      # rig.lua's exposure: manual, pinned; the add-on meters it
        for k, v in (("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                     ("override_auto_exposure_min_brightness", True), ("auto_exposure_min_brightness", 1.0),
                     ("override_auto_exposure_max_brightness", True), ("auto_exposure_max_brightness", 1.0),
                     ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.0)):
            pp.set_editor_property(k, v)
        comp.set_editor_property("post_process_settings", pp)
        comp.set_editor_property("relative_location", unreal.Vector(*(vals[SLOT[f"face{face}.{a}"]] for a in "xyz")))
        comp.set_editor_property("relative_rotation", unreal.Rotator(roll=0.0, pitch=vals[SLOT[f"face{face}.pitch"]],
                                                                     yaw=vals[SLOT[f"face{face}.yaw"]]))
    elif kind == "Player":
        comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_SCENE_DEPTH)
        comp.set_editor_property("primitive_render_mode", unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    else:   # Surface: the surfaces' own colour needs no lights, shadows, occlusion or fog (rig.lua)
        comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_BASE_COLOR)
        comp.set_editor_property("show_flag_settings", [unreal.EngineShowFlagsSetting(show_flag_name=f, enabled=False)
                                                        for f in ("Lighting", "DynamicShadows", "AmbientOcclusion", "Fog", "VolumetricFog")])


# ── The graph ────────────────────────────────────────────────────────────────────────────────────────────────────

class Graph:
    """Nodes on a grid, and every link checked: a failure names the pins the node has."""

    def __init__(self, bp):
        self.bp, self.n = bp, 0

    def _at(self):
        self.n += 1
        return (self.n % 48) * 340, (self.n // 48) * 300 + 1200

    def _ok(self, node, what):
        if node is None:
            raise RuntimeError(f"no node for {what}")
        return node

    def call(self, cls, fn, **pins):
        node = self._ok(lib.call(self.bp, cls.static_class(), fn, *self._at()), f"{cls.__name__}.{fn}")
        for pin, value in pins.items():
            self.set_pin(node, pin, value)
        return node

    def event(self, name):
        return self._ok(lib.event(self.bp, name, *self._at()), name)

    def get(self, var):
        return self._ok(lib.get(self.bp, var, *self._at()), f"get {var}")

    def set(self, var, value=None):
        node = self._ok(lib.set(self.bp, var, *self._at()), f"set {var}")
        if value is not None:
            self.feed(value, node, var)
        return node

    def set_member(self, cls, prop):
        return self._ok(lib.set_member(self.bp, cls.static_class(), prop, *self._at()), f"set {cls.__name__}.{prop}")

    def branch(self, cond):
        node = self._ok(lib.branch(self.bp, *self._at()), "branch")
        self.feed(cond, node, "Condition")
        return node

    def sequence(self, n):
        return self._ok(lib.sequence(self.bp, n, *self._at()), "sequence")

    def array_get(self, array, index):
        node = self._ok(lib.array_get(self.bp, *self._at()), "array get")
        self.link(array[0], array[1], node, "Array")
        self.set_pin(node, "Dimension 1", str(index))
        return node

    def link(self, a, ap, b, bp):
        if not lib.link(a, ap, b, bp):
            raise RuntimeError(f"cannot join {ap} to {bp}: from-pins {list(lib.pins(a))}, to-pins {list(lib.pins(b))}")

    def set_pin(self, node, pin, value):
        if not lib.set_pin(node, pin, str(value)):
            raise RuntimeError(f"no pin {pin}: {list(lib.pins(node))}")

    def feed(self, value, node, pin):
        """A value into a pin: (node, pin) is joined, anything else is typed in."""
        if isinstance(value, tuple):
            self.link(value[0], value[1], node, pin)
        else:
            self.set_pin(node, pin, value)

    def then(self, *steps):
        """Exec through the steps: each a node (its 'then') or (node, out-pin) for the one it leaves by."""
        for a, b in zip(steps, steps[1:]):
            node, pin = (a, "then") if not isinstance(a, tuple) else a
            self.link(node, pin, b, "execute")
        return steps[-1]

    # pure helpers: each returns (node, "ReturnValue")
    def math(self, fn, **ins):
        node = self.call(unreal.MathLibrary, fn)
        for pin, value in ins.items():
            self.feed(value, node, pin)
        return node, "ReturnValue"

    def gt(self, a, b):
        return self.math("Greater_DoubleDouble", A=a, B=b)

    def var(self, name):
        return self.get(name), name


def build_graph(g, settings_rt, vals):
    Actor, Gs, Krl, Ksl = unreal.Actor, unreal.GameplayStatics, unreal.RenderingLibrary, unreal.SystemLibrary
    Scc = unreal.SceneCaptureComponent2D
    kinds = ("Face", "Player", "Surface")

    def key_bits(on, look, surface):
        """rig.lua's buildKey less the sizes (kept apart): the faces on, Player look, Lighting below 1, as bits."""
        k = g.math("Multiply_DoubleDouble", A=look, B="64.0")
        k = g.math("Add_DoubleDouble", A=k, B=g.math("Multiply_DoubleDouble", A=surface, B="128.0"))
        for f in range(6):
            k = g.math("Add_DoubleDouble", A=k, B=g.math("Multiply_DoubleDouble", A=on[f], B=f"{2 ** f:f}"))
        return k

    # THE REBUILD, entered from BeginPlay and from a changed key: for every capture a new target of the size in Res and
    # SideH, its code cleared into it; captures held off for the handshake (the Tick counts Hold down).
    width, side = g.math("Round", A=g.var("Res")), g.math("Round", A=g.var("SideH"))
    rebuild = []
    for kind in kinds:
        for f in range(6):
            code = f"(R={MARK_R:f},G={CODES[kind] + f:f},B=0.000000,A={MARK_A:f})"
            rt = g.call(Krl, "CreateRenderTarget2D", Format="RTF_RGBA16f", ClearColor=code, bAutoGenerateMipMaps="false")
            g.feed(width, rt, "Width")
            g.feed(width if f >= 4 else side, rt, "Height")
            use = g.set_member(Scc, "TextureTarget")
            g.link(*g.var(f"{kind}{f}"), use, "self")
            g.link(rt, "ReturnValue", use, "TextureTarget")
            clear = g.call(Krl, "ClearRenderTarget2D", ClearColor=code)
            g.link(rt, "ReturnValue", clear, "TextureRenderTarget")
            rebuild += [rt, use, clear]
    rebuild.append(g.set("Hold", f"{HANDSHAKE_S:f}"))
    g.then(*rebuild)

    # BEGIN PLAY: the profile's settings, the settings file's full path, and the rebuild
    begin = g.event("ReceiveBeginPlay")
    rate = g.call(Actor, "SetActorTickInterval", TickInterval=f"{TICK_S:f}")
    steps = [begin, rate]
    for var, value in (("On", 1.0), ("Yaw", vals[SLOT["yawDeg"]]), ("Turn", vals[SLOT["turn"]]),
                       ("PlayerLook", vals[SLOT["playerLook"]]), ("Surface", vals[SLOT["surface"]]),
                       ("CapsuleHalf", vals[SLOT["capsuleHalf"]]), ("Res", vals[SLOT["faceRes"]]),
                       ("SideH", vals[SLOT["sideH"]]), ("Acc", POLL_S), ("ShowAcc", SHOW_ONLY_S),
                       ("BuildKey", 64 * vals[SLOT["playerLook"]] + 128 * vals[SLOT["surface"]] +
                        sum(2 ** f * vals[SLOT[f"face{f}.on"]] for f in range(6)))):
        steps.append(g.set(var, f"{value:f}"))
    for f in range(6):
        steps.append(g.set(f"FaceOn{f}", f"{vals[SLOT[f'face{f}.on']]:f}"))
    proj = g.call(unreal.Paths, "ProjectDir")
    cat = g.call(unreal.StringLibrary, "Concat_StrStr", B=BRIDGE_FILE)
    g.link(proj, "ReturnValue", cat, "A")
    full = g.call(unreal.Paths, "ConvertRelativePathToFull")
    g.link(cat, "ReturnValue", full, "InPath")
    steps.append(g.set("BridgePath", (full, "ReturnValue")))
    g.then(*steps, rebuild[0])

    # TICK, in four parts
    tick = g.event("ReceiveTick")
    dt = (tick, "DeltaSeconds")
    seq = g.sequence(4)
    g.then(tick, seq)

    # 1. READ THE SETTINGS every POLL_S
    acc = g.math("Add_DoubleDouble", A=g.var("Acc"), B=dt)
    set_acc = g.set("Acc", acc)
    due = g.branch(g.math("GreaterEqual_DoubleDouble", A=acc, B=f"{POLL_S:f}"))
    g.then((seq, "then_0"), set_acc, due)
    zero = g.set("Acc", "0.0")
    g.then((due, "then"), zero)
    imp = g.call(Krl, "ImportFileAsTexture2D")
    g.feed(g.var("BridgePath"), imp, "Filename")
    valid = g.call(Ksl, "IsValid")
    g.link(imp, "ReturnValue", valid, "Object")
    have = g.branch((valid, "ReturnValue"))
    draw_begin = g.call(Krl, "BeginDrawCanvasToRenderTarget", TextureRenderTarget=settings_rt.get_path_name())
    draw = g.call(unreal.Canvas, "K2_DrawTexture", ScreenPosition="(X=0.000000,Y=0.000000)",
                  ScreenSize=f"(X={BRIDGE_WIDTH:f},Y=1.000000)", CoordinatePosition="(X=0.000000,Y=0.000000)",
                  CoordinateSize="(X=1.000000,Y=1.000000)", RenderColor="(R=1.000000,G=1.000000,B=1.000000,A=1.000000)",
                  BlendMode="BLEND_Opaque", Rotation="0.0", PivotPoint="(X=0.500000,Y=0.500000)")
    g.link(draw_begin, "Canvas", draw, "self")
    g.link(imp, "ReturnValue", draw, "RenderTexture")
    draw_end = g.call(Krl, "EndDrawCanvasToRenderTarget")
    g.link(draw_begin, "Context", draw_end, "Context")
    read = g.call(Krl, "ReadRenderTargetRawPixelArea", TextureRenderTarget=settings_rt.get_path_name(), MinX="0",
                  MinY="0", MaxX=str(BRIDGE_WIDTH), MaxY="1", bNormalize="false")
    # every value: its pixel, its channel, back into its range
    v = {}
    for p in range((len(LAYOUT) + 3) // 4):
        px = g.array_get((read, "ReturnValue"), p)
        brk = g.call(unreal.MathLibrary, "BreakColor")
        g.link(px, "Output", brk, "InColor")
        for c, ch in enumerate("RGBA"):
            i = p * 4 + c
            if i >= len(LAYOUT):
                break
            name, lo, hi = LAYOUT[i]
            v[name] = g.math("MapRangeUnclamped", Value=(brk, ch), InRangeA="0.0", InRangeB="1.0", OutRangeA=f"{lo:f}",
                             OutRangeB=f"{hi:f}")
    mark = g.math("Less_DoubleDouble", A=g.math("Abs", A=g.math("Subtract_DoubleDouble", A=v["magic"], B=f"{MAGIC:f}")), B="0.5")
    lay = g.math("Less_DoubleDouble", A=g.math("Abs", A=g.math("Subtract_DoubleDouble", A=v["layout"], B=f"{LAYOUT_VERSION:f}")), B="0.5")
    ours = g.branch(g.math("BooleanAND", A=mark, B=lay))
    g.then(zero, imp, have)
    g.then((have, "then"), draw_begin, draw, draw_end, read, ours)

    # ...applied: the flags, the rate, each face's place and lens, and the rebuild if its key changed
    steps = [(ours, "then")]
    for var, slot in (("On", "on"), ("Yaw", "yawDeg"), ("Turn", "turn"), ("PlayerLook", "playerLook"), ("Surface", "surface"),
                      ("CapsuleHalf", "capsuleHalf")):
        steps.append(g.set(var, v[slot]))
    hz = g.math("FMax", A=v["hz"], B="1.0")
    steps.append(g.call(Actor, "SetActorTickInterval"))
    g.feed(g.math("Divide_DoubleDouble", A="1.0", B=hz), steps[-1], "TickInterval")
    for f in range(6):
        steps.append(g.set(f"FaceOn{f}", v[f"face{f}.on"]))
        place = g.call(unreal.SceneComponent, "K2_SetRelativeLocationAndRotation", bTeleport="true")
        g.link(*g.var(f"Face{f}"), place, "self")
        g.feed(g.math("MakeVector", X=v[f"face{f}.x"], Y=v[f"face{f}.y"], Z=v[f"face{f}.z"]), place, "NewLocation")
        g.feed(g.math("MakeRotator", Roll="0.0", Pitch=v[f"face{f}.pitch"], Yaw=v[f"face{f}.yaw"]), place, "NewRotation")
        steps.append(place)
        for kind in kinds:
            comp = g.var(f"{kind}{f}")
            for prop, slot in (("FOVAngle", "fov"), ("CustomNearClippingPlane", "near"), ("MaxViewDistanceOverride", "reach"),
                               ("OrthoWidth", "orthoWidth")):
                s = g.set_member(Scc, prop)
                g.link(comp[0], comp[1], s, "self")
                g.feed(v[f"face{f}.{slot}"], s, prop)
                steps.append(s)
    bits = key_bits([v[f"face{f}.on"] for f in range(6)], v["playerLook"], v["surface"])
    differs = lambda a, b: g.gt(g.math("Abs", A=g.math("Subtract_DoubleDouble", A=a, B=b)), "0.5")
    changed = g.math("BooleanOR", A=differs(bits, g.var("BuildKey")),
                     B=g.math("BooleanOR", A=differs(v["faceRes"], g.var("Res")), B=differs(v["sideH"], g.var("SideH"))))
    anew = g.branch(changed)
    # rig.lua's lens(): perspective with the near plane on the volume's face, or orthographic with the engine's own
    persp = g.branch(g.gt(v["perspective"], "0.5"))
    steps.append(persp)
    g.then(*steps)
    for out, mode, near in (("then", "Perspective", "true"), ("else", "Orthographic", "false")):
        chain = [(persp, out)]
        for f in range(6):
            for kind in kinds:
                comp = g.var(f"{kind}{f}")
                for prop, value in (("ProjectionType", mode), ("bOverride_CustomNearClippingPlane", near)):
                    s = g.set_member(Scc, prop)
                    g.link(comp[0], comp[1], s, "self")
                    g.set_pin(s, prop, value)
                    chain.append(s)
        g.then(*chain, anew)
    g.then((anew, "then"), g.set("BuildKey", bits), g.set("Res", v["faceRes"]), g.set("SideH", v["sideH"]), rebuild[0])

    # 2. FOLLOW: the rig on the player's feet (capsule centre less half its height), turned by its yaw and, turning
    # with the player, the view's
    pawn = g.call(Gs, "GetPlayerPawn", PlayerIndex="0")
    alive = g.call(Ksl, "IsValid")
    g.link(pawn, "ReturnValue", alive, "Object")
    there = g.branch((alive, "ReturnValue"))
    where = g.call(Actor, "K2_GetActorLocation")
    g.link(pawn, "ReturnValue", where, "self")
    feet = g.math("Subtract_VectorVector", A=(where, "ReturnValue"), B=g.math("MakeVector", X="0.0", Y="0.0", Z=g.var("CapsuleHalf")))
    cam = g.call(Gs, "GetPlayerCameraManager", PlayerIndex="0")
    view = g.call(unreal.PlayerCameraManager, "GetCameraRotation")
    g.link(cam, "ReturnValue", view, "self")
    heading = g.call(unreal.MathLibrary, "BreakRotator")
    g.link(view, "ReturnValue", heading, "InRot")
    rig_yaw = g.math("Add_DoubleDouble", A=g.var("Yaw"), B=g.math("Multiply_DoubleDouble", A=g.var("Turn"), B=(heading, "Yaw")))
    move = g.call(Actor, "K2_SetActorLocationAndRotation", bTeleport="true")
    g.feed(feet, move, "NewLocation")
    g.feed(g.math("MakeRotator", Roll="0.0", Pitch="0.0", Yaw=rig_yaw), move, "NewRotation")
    g.then((seq, "then_1"), there)
    g.then((there, "then"), move)

    # 3. CAPTURE each face on: its picture, its surfaces' colour (Lighting below 1), its characters (Player look);
    # nothing while the add-on reads the codes
    hold = g.var("Hold")
    holding = g.branch(g.gt(hold, "0.0"))
    g.then((seq, "then_2"), holding)
    g.then((holding, "then"), g.set("Hold", g.math("Subtract_DoubleDouble", A=hold, B=dt)))
    on = g.branch(g.gt(g.var("On"), "0.5"))
    g.then((holding, "else"), on)
    entries = [(on, "then")]
    for f in range(6):
        face_on = g.branch(g.gt(g.var(f"FaceOn{f}"), "0.5"))
        for e in entries:
            g.link(e[0], e[1], face_on, "execute")
        shot = g.call(Scc, "CaptureScene")
        g.link(*g.var(f"Face{f}"), shot, "self")
        surf_on = g.branch(g.gt(g.var("Surface"), "0.5"))
        surf = g.call(Scc, "CaptureScene")
        g.link(*g.var(f"Surface{f}"), surf, "self")
        look_on = g.branch(g.gt(g.var("PlayerLook"), "0.5"))
        look = g.call(Scc, "CaptureScene")
        g.link(*g.var(f"Player{f}"), look, "self")
        g.then((face_on, "then"), shot, surf_on)
        g.then((surf_on, "then"), surf, look_on)
        g.link(surf_on, "else", look_on, "execute")
        g.then((look_on, "then"), look)
        entries = [(face_on, "else"), (look_on, "else"), (look, "then")]

    # 4. THE CHARACTERS the player captures show: every Pawn, listed again each SHOW_ONLY_S
    sacc = g.math("Add_DoubleDouble", A=g.var("ShowAcc"), B=dt)
    set_sacc = g.set("ShowAcc", sacc)
    due2 = g.branch(g.math("GreaterEqual_DoubleDouble", A=sacc, B=f"{SHOW_ONLY_S:f}"))
    g.then((seq, "then_3"), set_sacc, due2)
    pawns = g.call(Gs, "GetAllActorsOfClass", ActorClass="/Script/Engine.Pawn")
    steps = [(due2, "then"), g.set("ShowAcc", "0.0"), pawns]
    for f in range(6):
        s = g.set_member(Scc, "ShowOnlyActors")
        g.link(*g.var(f"Player{f}"), s, "self")
        g.link(pawns, "OutActors", s, "ShowOnlyActors")
        steps.append(s)
    g.then(*steps)
    log(f"{MOD}: graph built, {g.n} nodes")


def mod_info(info):
    info.set_editor_property("version", "0.2")
    info.set_editor_property("author", "Voxon")
    info.set_editor_property("description", "Voxon Capture Rig: a cube of cameras round your hero, for a Voxon volumetric "
                             "display. Needs the Voxon Capture Rig app, which installs it and sets it up.")


def main():
    vals = bridge_values(PROFILE)
    # a clean folder: no targets left from an older build (the captures' are made at run time)
    if unreal.EditorAssetLibrary.does_directory_exist(ROOT):
        unreal.EditorAssetLibrary.delete_directory(ROOT)
    unreal.EditorAssetLibrary.make_directory(ROOT)
    bp = fresh(f"{ROOT}/ModActor", "/Game/Templates/ModActor")
    info = fresh(f"{ROOT}/ModInfo", "/Game/Templates/ModInfo")
    mod_info(info)
    # the settings file is drawn into this and read back: float, and gamma 1 (a 32-bit float target made at run time
    # would take the display's gamma)
    settings_rt = render_target("RT_Settings", BRIDGE_WIDTH, 1, unreal.TextureRenderTargetFormat.RTF_RGBA32F,
                                unreal.LinearColor(0.0, 0.0, 0.0, 0.0))

    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root = None
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        if isinstance(unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)),
                      unreal.SceneComponent):
            root = h
            break
    if root is None:
        raise RuntimeError("ModActor has no scene root")
    for f in range(6):
        fh, face = add_component(bp, sub, root, unreal.SceneCaptureComponent2D, f"Face{f}")
        capture_defaults(face, "Face", vals, f)
        for kind in ("Player", "Surface"):     # under the face: its place, turned with it
            _, comp = add_component(bp, sub, fh, unreal.SceneCaptureComponent2D, f"{kind}{f}")
            capture_defaults(comp, kind, vals, f)

    real = unreal.BlueprintEditorLibrary.get_basic_type_by_name("real")
    for var in ["Acc", "ShowAcc", "Hold", "Res", "SideH", "BuildKey", "On", "Yaw", "Turn", "PlayerLook", "Surface", "CapsuleHalf"] + \
               [f"FaceOn{f}" for f in range(6)]:
        unreal.BlueprintEditorLibrary.add_member_variable(bp, var, real)
    unreal.BlueprintEditorLibrary.add_member_variable(bp, "BridgePath", unreal.BlueprintEditorLibrary.get_basic_type_by_name("string"))

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)   # the components and variables become what the graph can use
    build_graph(Graph(bp), settings_rt, vals)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    for asset in (bp, info):
        unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    log(f"{MOD}: built in {ROOT}")


main()
