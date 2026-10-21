using System.Runtime.CompilerServices;
using System.Numerics;
using Voxon;
using Voxon.GameKit.Voxelizer;

namespace Voxon.GameKit.Display;

/// <summary>
/// Owns the Voxon SDK for one process: initialises it, spins the motor, presents a frame of voxels
/// with a single <c>DrawVox_Batch</c>, and optionally renders the simulator view into a
/// <see cref="PreviewSurface"/> for the application's own window.
///
/// The caller drives it, one call per frame, so the game keeps its own loop:
/// <code>
///   using var d = new VoxonDisplay();
///   if (!d.Initialise()) return;
///   while (d.Breath()) { d.Present(voxels); d.RenderPreview(surface); }
/// </code>
///
/// IMPORTANT — threading and state. Everything the frame loop needs lives in fields on this object
/// but is seeded BEFORE the SDK init calls, and each init call sits in its own non-inlined method.
/// The SDK zeroes managed stack frames during LedWinInit: a local still needed afterwards can come
/// back corrupted (observed: a string local silently became empty). The SDK also uses COM STA
/// internally, so <see cref="Initialise"/> and every per-frame call must happen on the SAME thread,
/// and that thread should be STA.
/// </summary>
public sealed class VoxonDisplay : IDisposable
{
    // ── Why every field here is STATIC ────────────────────────────────────────────────────────
    // The SDK zeroes managed references on the stack during its init — locals AND `this`. An object
    // that keeps its state in instance fields and reads them after init can therefore come back
    // with a corrupted `this`, and the CLR dies with an internal error (0x80131506). Statics live in
    // the GC static area and are never zeroed, so all state lives there and the instance is only an
    // ergonomic wrapper. One display per process, which is what the hardware is anyway.
    private static LedHostCS s_ledHost;
    private static LedWinCS s_ledWin;
    private static vxl_state_t s_vs;
    private static bool s_ready;
    private static bool s_motorConfirmed;
    private static long s_ticks;

    // Camera for the simulator view, in the SDK's own orbit terms.
    private static float s_hAng, s_vAng, s_dist;
    private static int s_rpm = 600;
    private static int s_lastDrawn;
    private static float s_boundRadius, s_boundHalfZ, s_boundRadius2;
    private static int s_actualRpm;
    private static long s_requestTick;
    private static string s_error;
    private static string s_version;
    private static float s_defH, s_defV, s_defD;
    private static float[] s_x = new float[1], s_y = new float[1], s_z = new float[1];
    private static int[] s_col = new int[1];

    /// <summary>
    /// Requested motor speed. 0 stops it; negative leaves the motor alone.
    ///
    /// Assigning a NEW speed re-opens the request: the frame loop only talks to the motor until the
    /// SDK confirms it is turning, so without this a later change would be recorded and never sent.
    /// </summary>
    public int Rpm
    {
        get => s_rpm;
        set
        {
            if (value == s_rpm) return;
            s_rpm = value;
            s_motorConfirmed = false;
            s_requestTick = s_ticks;        // this request gets its own spin-up window
        }
    }

    /// <summary>Draws the SDK's boundary cage around the volume. Off by default.
    /// <para>
    /// A development aid, and the only way to see where the volume actually ends: without it a
    /// sparse scene gives no reference at all, so geometry that is clipped, off-centre, or on the
    /// wrong axis looks the same as geometry that is fine. The SDK checks
    /// <c>vxl_state_t.drawbord</c> each frame, so this can be toggled while running.
    /// </para></summary>
    /// <summary>
    /// What the dither threshold is set to at startup. The SDK's own default is 64; 16 is brighter
    /// for no extra voxels, which is the trade worth taking on a display that only adds light.
    /// </summary>
    /// <remarks>Ken Silverman's number, and his reasoning: brighter without hurting volume rate.</remarks>
    public const int DefaultDitherThreshold = 16;

    /// <summary>
    /// The SDK's dither threshold. Lower is brighter. Live, like <see cref="DrawBorder"/>: it is read
    /// off <c>vxl_state_t</c> every frame, so it can be changed while running.
    /// </summary>
    public int DitherThreshold
    {
        get => s_vs.dithresh;
        set => s_vs.dithresh = value;
    }

    public bool DrawBorder
    {
        get => s_vs.drawbord != 0;
        set => s_vs.drawbord = value ? 1 : 0;
    }

    /// <summary>Voxels drawn on the last frame.</summary>
    public int LastDrawn => s_lastDrawn;

    /// <summary>Volume bounds the SDK reports once running: horizontal radius and vertical half-height.</summary>
    public float BoundRadius => s_boundRadius;
    public float BoundHalfZ => s_boundHalfZ;

    /// <summary>The third bound the SDK reports, <c>boundr2</c>: <c>vxl_state_t</c> carries <c>boundr, boundr2,
    /// boundz</c>, all three commented "World units for cropping". Read off the simulator (2026-10-16): 64 with boundr 8
    /// - the radius squared, for a clip test, not a second horizontal extent. The volume is round: one radius for x and
    /// y, and a half-height.</summary>
    public float BoundRadius2 => s_boundRadius2;

    /// <summary>Motor speed the SDK reports, or 0.</summary>
    public int ActualRpm => s_actualRpm;

    /// <summary>Set when initialisation failed, for the caller to show.</summary>
    public string? Error => s_error;

    /// <summary>
    /// Loads and initialises the SDK. Must run on the thread that will drive the frames.
    /// </summary>
    public bool Initialise(string windowTitle = "Voxon.GameKit")
    {
        if (!CheckStructLayout(out string layoutError))
        {
            s_error = layoutError;
            return false;
        }

        // Both DLLs, and BOTH checked before either is used. The resolver returns an empty string
        // when it finds nothing, and an empty string reaches Path.GetFullPath inside
        // LoadLedHostCS as an ArgumentException raised on the play-loop thread - a stack trace
        // about a path parameter, for a machine that simply has no runtime installed.
        string ledHostPath, ledWinPath;
        try
        {
            ledHostPath = VledRuntime.Find("LedHost.dll", out var looked);
            ledWinPath = VledRuntime.Find("LedWin.dll", out _);
            if (ledHostPath.Length == 0 || ledWinPath.Length == 0)
            {
                s_error = VledRuntime.NotFoundMessage(looked);
                return false;
            }
        }
        catch (Exception ex)
        {
            s_error = $"Voxon SDK not found: {ex.Message}. Install the VLED runtime from " +
                    "https://voxon.co/downloads.";
            return false;
        }

        // Before anything loads ledhost: its own static import has to be satisfied out of the
        // folder it came from, which the Windows loader will not do by itself. See
        // VledRuntime.PreloadDependencies - without this, a runtime anywhere but on PATH fails
        // with DllNotFoundException and nothing to say why.
        string depError = VledRuntime.PreloadDependencies(Path.GetDirectoryName(ledHostPath));
        if (depError.Length > 0)
        {
            s_error = $"Voxon runtime found but not loadable: {depError}";
            return false;
        }

        s_vs = new vxl_state_t();
        s_ledHost = new LedHostCS();
        if (s_ledHost.LoadLedHostCS(ledHostPath) == IntPtr.Zero)
        {
            // FOUND but would not LOAD is almost always one thing: ledhost.dll imports FTD3XXWU.dll
            // statically, and that one is FTDI's so it is not bundled. Windows reports this as a
            // bare "module could not be found" naming ledhost rather than the import it is missing,
            // which sends you looking at the wrong file. Say which file, and where it lives.
            bool ftdi = File.Exists(Path.Combine(
                Path.GetDirectoryName(ledHostPath) ?? ".", "FTD3XXWU.dll"));
            s_error = ftdi
                ? $"found LedHost.dll at {ledHostPath} but Windows would not load it"
                : $"found LedHost.dll at {ledHostPath} but Windows would not load it - most likely "
                  + "FTD3XXWU.dll, which it imports and which is not beside it. That one is FTDI's "
                  + "rather than Voxon's so it is not bundled: install the Voxon software, or put "
                  + "FTD3XXWU.dll beside the exe.";
            return false;
        }
        if (!CallLedHostInit())
        {
            s_error = "LedHost init failed (is the display connected and powered?)";
            return false;
        }
        s_version = s_ledHost.GetVer().ToString();

        // AFTER init, because vxl_loadini reads ledhost.ini and would overwrite it.
        DitherThreshold = DefaultDitherThreshold;

        s_ledWin = new LedWinCS(ledWinPath);
        CallLedWinInit(windowTitle);

        // Seed the simulator camera from whatever the SDK starts with, so the preview opens on a
        // sensible view and "reset" has something to go back to.
        //
        // The DISTANCE is pulled back from the SDK's own, which frames the volume tightly enough
        // that a scene overflows the pane and cannot be judged. Only the multiplier is ours;
        // everything else about the orbit camera is the SDK's own starting point.
        s_hAng = s_defH = s_ledWin.GetEmuHAng();
        s_vAng = s_defV = s_ledWin.GetEmuVAng();
        s_dist = s_defD = s_ledWin.GetEmuDist() * DefaultDistScale;

        // XInput, because the pad this serves is an Xbox one and DirectInput reports it with a
        // different axis and button layout. Probed with a real call rather than trusted: LedWinCS
        // maps its exports by name and leaves the delegate null when one is missing, so an older
        // LedWin.dll would otherwise fault on the first Pad() of the first frame.
        try
        {
            s_ledWin.SetJoyAPIType(VX_JOY_API_TYPE.VX_JOY_API_X_INPUT);
            s_ledWin.GetJoyCount();
            s_padOk = true;
        }
        catch { s_padOk = false; }

        s_ready = true;
        return true;
    }

    /// <summary>
    /// How far back the preview camera starts, as a multiple of the SDK's own starting distance.
    /// </summary>
    /// <remarks>
    /// ONE NUMBER, on purpose: the preview's zoom is no longer on the scroll wheel - that drives
    /// the world's view radius now - so where this opens is where it stays unless someone drags it.
    /// That makes the default worth getting right and makes it worth being easy to move. It wants
    /// checking against a real display; it was chosen to fit the whole volume with air around it.
    /// </remarks>
    public const float DefaultDistScale = 1.8f;

    public string? Version => s_version;
    public float DefaultHAng => s_defH;
    public float DefaultVAng => s_defV;
    public float DefaultDist => s_defD;

    /// <summary>Simulator orbit camera. Applied on the next preview render.</summary>
    public void SetCamera(float hAng, float vAng, float dist)
    {
        s_hAng = hAng;
        s_vAng = vAng;
        s_dist = MathF.Max(0.05f, dist);
    }

    public (float HAng, float VAng, float Dist) Camera => (s_hAng, s_vAng, s_dist);

    /// <summary>Pumps the SDK's window. False means it wants to close, or we never started.</summary>
    public bool Breath() => s_ready && s_ledWin.Breath() == 0;

    // ── Gamepad ───────────────────────────────────────────────────────────────────────────────
    // Exposed because the SDK already polls a controller for its own window and there is no other
    // way to reach it: LedWinCS is private to this class, and the STA rule above means the caller
    // could not talk to it from another thread even if it were public. Read from the same thread
    // that calls Breath, which is what refreshes the state.
    //
    // Deliberately NOT a general input API. It is the four things a menu needs, because that is
    // what exists to serve. A game gets its controller from the OS like any other program.

    /// <summary>Whether the joystick exports were found in this LedWin.dll.</summary>
    private static bool s_padOk;

    /// <summary>One frame of the first gamepad. Everything is zero/false when none is attached.
    /// <para>
    /// The stick is the LEFT one, in -1..1 with the SDK's deadzone already applied. Y follows
    /// XInput, where pushing UP is positive.
    /// </para></summary>
    public readonly record struct PadState(
        bool Connected, float StickX, float StickY,
        bool Up, bool Down, bool Left, bool Right, bool A);

    /// <summary>Reads the first gamepad. Never throws: a display with no pad, or an older DLL
    /// without the joystick exports, reports a disconnected pad rather than failing a frame.
    /// </summary>
    public PadState Pad()
    {
        if (!s_ready || !s_padOk) return default;
        try
        {
            if (s_ledWin.GetJoyCount() <= 0) return default;
            return new PadState(
                Connected: true,
                StickX: s_ledWin.GetJoyAxisValue(0, VX_JOY_AXIS_CODES.JOY_AXIS_LEFT_STICK_X),
                StickY: s_ledWin.GetJoyAxisValue(0, VX_JOY_AXIS_CODES.JOY_AXIS_LEFT_STICK_Y),
                Up: s_ledWin.GetJoyButtonIsDown(0, VX_JOY_BUTTON_CODES.JOY_DPAD_UP) != 0,
                Down: s_ledWin.GetJoyButtonIsDown(0, VX_JOY_BUTTON_CODES.JOY_DPAD_DOWN) != 0,
                Left: s_ledWin.GetJoyButtonIsDown(0, VX_JOY_BUTTON_CODES.JOY_DPAD_LEFT) != 0,
                Right: s_ledWin.GetJoyButtonIsDown(0, VX_JOY_BUTTON_CODES.JOY_DPAD_RIGHT) != 0,
                A: s_ledWin.GetJoyButtonIsDown(0, VX_JOY_BUTTON_CODES.JOY_A) != 0);
        }
        catch
        {
            // An export that mapped but misbehaves must not be able to kill the display loop.
            s_padOk = false;
            return default;
        }
    }

    /// <summary>
    /// Draws one frame: motor upkeep, then FrameStart / one batched draw / FrameEnd. FrameEnd is
    /// what actually presents to the display, and what paces this to the sweep rate.
    /// </summary>
    /// <summary>A multiplier on every voxel's colour, per channel; white leaves the frame alone.
    /// <para>
    /// Set from <see cref="Protocol.LightingState.Tint"/> by the host each frame. Static because
    /// <see cref="Pack"/> is, which is because the SDK and its scratch buffers are.
    /// </para></summary>
    public Vector3 Tint
    {
        get => s_tint;
        set => s_tint = value;
    }

    private static Vector3 s_tint = Vector3.One;

    /// <summary>A gamma curve on every voxel's colour. 1.0 is untouched.
    /// <para>
    /// Worth having on THIS display specifically. The volume resolves about eight levels after
    /// dithering, so most of an 8-bit channel's range is spent on distinctions it cannot show, and
    /// which end of the range gets the resolution is a per-game judgement: a dark game wants its
    /// midtones lifted to be legible at all, a bright one wants them pushed down before everything
    /// crushes into the top level.
    /// </para>
    /// <para>
    /// Below 1 brightens, above 1 darkens -- the usual convention, so a value found in any other
    /// tool means the same thing here.
    /// </para></summary>
    public float Gamma
    {
        get => s_gamma;
        set => s_gamma = value > 0f ? value : 1f;
    }

    private static float s_gamma = 1f;

    /// <summary>How far a fully grey voxel is turned down. 0 changes nothing; 1 takes grey to black
    /// and leaves a saturated colour untouched.
    /// <para>
    /// A grey voxel has all three channels lit, so on an ADDITIVE display it is the brightest thing
    /// the volume can hold at a given level -- brighter than any colour, because a colour is by
    /// definition a voxel with at least one channel turned down. Most game surfaces are muted
    /// browns, greens and greys once shading is applied, so the colourless majority sits on top of
    /// everything and the coloured minority is what gets lost.
    /// </para>
    /// <para>
    /// Turning <see cref="Gamma"/> or the tint down does not help, because they take the colour with
    /// them. This scales a voxel by how COLOURLESS it is, so grey stone goes down and a red key does
    /// not. Measured on Ocarina of Time: a mean voxel of (96, 78, 71) with 2% of voxels anywhere
    /// near white, which read at the volume as a flat white mass with coloured specks in it.
    /// </para>
    /// <para>
    /// The same treatment <c>Voxon.GameKit.Scene</c>'s renderer applies, for the same reason. It is
    /// here because it belongs to the display rather than to one renderer: every app drawing into
    /// this volume meets the same physics.
    /// </para></summary>
    public float GreyDim
    {
        get => s_greyDim;
        set => s_greyDim = value < 0f ? 0f : value > 1f ? 1f : value;
    }

    /// <summary>The saturation at which a voxel stops being treated as grey at all.
    /// <para>
    /// A ramp rather than a threshold, because of what a threshold looks like here: two neighbouring
    /// texels either side of it get very different treatment, so a gently shaded surface bands.
    /// Below this the dimming fades in smoothly; at and above it there is none.
    /// </para></summary>
    public float GreySpan
    {
        get => s_greySpan;
        set => s_greySpan = value > 0.01f ? value : 0.01f;
    }

    private static float s_greyDim;
    private static float s_greySpan = 0.5f;

    // ── The range the display actually shows ──────────────────────────────────────────────────
    //
    // A voxel's 0-255 is not what a volumetric display gives you. The bright end saturates well
    // before 255, so everything above some level comes out the same white and the picture loses its
    // highlights; the dark end disappears into the room before it reaches 0. Turning the whole
    // frame up to rescue the dark end pushes more of it into that white, and turning it down to
    // rescue the highlights loses the dark end entirely - which is the bind Gamma alone puts you
    // in, because it is a curve through fixed endpoints and cannot move either end anywhere.
    //
    // This maps the range instead: 0 becomes Floor and 255 becomes Ceiling, linearly. 80-220 gives
    // a dark end that is visible without being lifted out of black, and a bright end that stops
    // short of saturation and so keeps its colour.
    //
    // The same control SceneRenderer has had; lifted here because the Voxel64 host feeds this class
    // directly and never goes through SceneRenderer's voxelizer.

    /// <summary>What a voxel value of 0 comes out as. 0 leaves the bottom alone.</summary>
    public byte ColorFloor { get => s_colorFloor; set => s_colorFloor = value; }

    /// <summary>What a voxel value of 255 comes out as. 255 leaves the top alone.</summary>
    public byte ColorCeiling { get => s_colorCeiling; set => s_colorCeiling = value; }

    private static byte s_colorFloor;
    private static byte s_colorCeiling = 255;

    /// <summary>256 entries, rebuilt only when the band actually moves. Same reasoning as the gamma
    /// table: one multiply-add per entry when it changes, none per voxel per channel.</summary>
    private static readonly byte[] s_rangeLut = new byte[256];
    private static int s_rangeBuilt = -1;

    private static void BuildRange(int floor, int ceiling)
    {
        float lo = floor, hi = ceiling;
        for (int i = 0; i < 256; i++)
        {
            int v = (int)MathF.Round(lo + (hi - lo) * (i / 255f));
            s_rangeLut[i] = (byte)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        s_rangeBuilt = floor << 8 | ceiling;
    }

    /// <summary>1/n for n in 0..255, so saturation costs a multiply rather than a divide.</summary>
    private static readonly float[] s_invByte = BuildInvByte();

    private static float[] BuildInvByte()
    {
        var t = new float[256];
        for (int i = 1; i < 256; i++) t[i] = 1f / i;
        return t;                                     // t[0] stays 0: black has no saturation
    }

    /// <summary>256 entries, rebuilt only when the gamma actually moves.
    /// <para>
    /// A table rather than a pow() per channel per voxel: this runs three times for every voxel of
    /// every frame, and there are only 256 possible inputs.
    /// </para></summary>
    private static readonly byte[] s_gammaLut = BuildGamma(1f);
    private static float s_gammaBuilt = 1f;

    private static byte[] BuildGamma(float g)
    {
        var lut = new byte[256];
        for (int i = 0; i < 256; i++)
        {
            int v = (int)(MathF.Pow(i / 255f, g) * 255f + 0.5f);
            lut[i] = (byte)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        return lut;
    }

    public void Present(IReadOnlyList<Voxel> voxels)
    {
        if (!s_ready) return;

        // The motor is requested from inside the loop but BEFORE FrameStart. A SetRPM before the
        // loop starts is ignored outright, and one sandwiched between FrameStart/FrameEnd disturbs
        // the frame (observed: the display went blank). Re-request until the SDK reports it turning.
        long since = s_ticks - s_requestTick;
        if (s_rpm >= 0 && (since % 30) == 0)
        {
            // Read back every time, not just while a request is outstanding: the reported speed is
            // the only feedback anyone gets that a Motor On/Off actually took.
            if (!s_motorConfirmed) s_ledHost.SetRPM(ref s_vs, s_rpm);
            s_actualRpm = s_ledHost.GetRPM(ref s_vs);

            if (!s_motorConfirmed)
            {
                bool reached = s_rpm > 0 ? s_actualRpm >= s_rpm * 0.9 : s_actualRpm == 0;
                // Giving up quietly matters: the simulator has no motor to report on at all.
                s_motorConfirmed = reached || since >= 300;
            }
        }
        s_ticks++;

        // PACKED EVERY FRAME, AND IT CANNOT BE SKIPPED. The obvious optimisation here is to
        // notice that Pack is a pure function of the voxels and the colour settings, and to reuse
        // the arrays when neither has changed - the display redraws far more often than the picture
        // changes, 56 volumes a second against 24.8 publishes on Duke, so more than half of this
        // work looks redundant.
        //
        // IT IS NOT. DrawVox_Batch takes the four arrays by reference and TRANSFORMS THEM IN PLACE,
        // so after a draw they no longer hold what Pack wrote. Drawing them a second time without
        // re-packing put the whole scene onto the XZ plane with its depth gone - reported from the
        // display as "every other frame has no Y data, a squashed 2D plane". Packed and skipped
        // frames alternating is exactly what that looks like.
        //
        // THE PRISTINE-COPY VERSION WAS ALSO TRIED, AND MEASURED WORSE. Keep a second set that Pack
        // writes and nothing else touches, memcpy it into the scratch each frame, and skip Pack when
        // neither the voxels nor the settings have moved. It works correctly and it is slower, at
        // matched voxel counts, in every band measured on Duke:
        //
        //     voxels        pack every frame     pristine + skip
        //     100-150k               92.5 VPS            89.7 VPS
        //     150-200k               77.7                66.7
        //     200-250k               71.1                65.6
        //     250-300k               68.9                68.2
        //
        // Because the cost here was never the arithmetic - it is the memory traffic, and the copy
        // has exactly as much of it. Pack reads 16 bytes a voxel and writes 16; the copy reads 16
        // and writes 16. So an unchanged frame trades one full pass for an equally expensive one,
        // and a CHANGED frame - about one in three, at 25 publishes against 77 volumes - now does
        // both. Plus double the array memory.
        //
        // Anything that helps here has to touch FEWER voxels per drawn frame, not touch the same
        // ones more cheaply.
        Pack(voxels);

        s_ledHost.FrameStart(ref s_vs);

        // EVERY FRAME, not once. s_vs is refilled by FrameStart above, so the SDK reports the
        // volume's size continuously - and the hardware type can be changed while the host is
        // running. Latching on the first non-zero report meant a switch from a VX2-XL to a VX2 was
        // seen by the SDK, drawn by the simulator, and invisible to everything downstream: the
        // voxelizer kept clipping to the old radius and the game was still told the old one over
        // the control file, so a HUD sized to it ran out through the wall.
        //
        // The guard that remains is only against ZERO, which is what boundr reads before the device
        // has run a frame.
        if (s_vs.boundr > 0.1f)
        {
            s_boundRadius = s_vs.boundr;
            s_boundHalfZ = s_vs.boundz;
            s_boundRadius2 = s_vs.boundr2;
        }

        if (s_lastDrawn > 0)
            s_ledHost.DrawVox_Batch(ref s_vs, ref s_x[0], ref s_y[0], ref s_z[0], ref s_col[0], s_lastDrawn, 0);

        s_ledHost.FrameEnd(ref s_vs);
    }

    /// <summary>
    /// Renders the volume into <paramref name="surface"/> as a 2D image — the simulator view, for
    /// the application's own window. Call after <see cref="Present"/>.
    /// </summary>
    public void RenderPreview(PreviewSurface surface)
    {
        if (!s_ready || !surface.Ready) return;
        surface.Clear();
        s_ledHost.Rend2D(ref s_vs, ref surface.Tile, s_hAng, s_vAng, s_dist);
        // Rend2D overshoots the picture. CheckOverrun measures it and refills the canary;
        // dropping this call turns a contained overshoot back into silent heap corruption.
        // See PreviewSurface's type comment.
        surface.CheckOverrun();
    }

    // ── Interop layout guard ──────────────────────────────────────────────────────────────────
    //
    // Refuses to initialise if vxl_state_t is not the size the native DLL writes. This exists
    // because it was wrong for the entire life of the project and nobody could see it:
    // module_t was missing its leading `ind` field, making it 24 bytes instead of 28. It is
    // embedded 16 times in vxl_state_t, so the managed struct was 576 bytes against the DLL's
    // 640 and EVERY call taking `ref vs` — vxl_init, vxl_loadini, vxl_frame_start,
    // vxl_frame_end — wrote 64 bytes past the end of it. 16 x 4 = 64, exactly.
    //
    // That is also the real cause of the "SDK init zeroes managed stack frames" behaviour this
    // file's own header describes, and of the fatal 0x80131506 that follows it. The static-field
    // discipline above is a containment strategy for a bug that is now fixed; it is cheap and
    // worth keeping, but this check is what stops the bug coming back.
    //
    // Expected values come from the SDK's own reference wrapper, VoxonTypes.cs inside
    // VisualStudioTemplates/New_CS_VLED_Application.zip: module_t = 28 bytes (ind, x0, y0, xs,
    // ys, rot, reserved0), vxl_state_t = 640 (192-byte header + 16 x 28).
    //
    // Failing closed is deliberate. A short struct is a guaranteed heap/stack overflow on every
    // frame; running anyway only buys an unpredictable crash somewhere else later.
    private const int ExpectedModuleSize = 28;
    private const int ExpectedStateSize = 640;

    /// <summary>
    /// Verifies the SDK interop structs match the native layout. Runs before any SDK call, needs
    /// no hardware, and is cheap enough to leave on. Public so a host can assert it in a test.
    /// </summary>
    public static bool CheckStructLayout(out string error)
    {
        int mod = System.Runtime.InteropServices.Marshal.SizeOf<module_t>();
        int st = System.Runtime.InteropServices.Marshal.SizeOf<vxl_state_t>();
        var attr = typeof(vxl_state_t).StructLayoutAttribute;

        if (mod != ExpectedModuleSize)
        {
            error = $"SDK interop layout wrong: module_t is {mod} bytes, expected {ExpectedModuleSize}. " +
                    "The SDK declares ind/x0/y0/xs/ys/rot/reserved0 — a missing field here is " +
                    "multiplied by 16 inside vxl_state_t and overflows every `ref vs` call.";
            return false;
        }
        if (st != ExpectedStateSize)
        {
            error = $"SDK interop layout wrong: vxl_state_t is {st} bytes, expected {ExpectedStateSize} " +
                    $"(delta {st - ExpectedStateSize}). The DLL writes the full native size regardless, " +
                    "so a short struct is a buffer overflow on every call taking `ref vs`.";
            return false;
        }
        if (attr == null || attr.Value != System.Runtime.InteropServices.LayoutKind.Sequential || attr.Pack != 1)
        {
            error = "SDK interop layout wrong: vxl_state_t must be " +
                    "[StructLayout(LayoutKind.Sequential, Pack = 1)] or the CLR may insert padding.";
            return false;
        }

        error = "";
        return true;
    }

    /// <summary>Stops the motor. Safe to call when nothing was started.</summary>
    public void StopMotor()
    {
        if (s_ready) try { s_ledHost.SetRPM(ref s_vs, 0); } catch { }
    }

    // Grow-only scratch, so a steady stream of frames does not churn the GC.
    private static void Pack(IReadOnlyList<Voxel> voxels)
    {
        int n = voxels.Count;
        if (s_x.Length < n)
        {
            int cap = Math.Max(n, s_x.Length * 2);
            s_x = new float[cap]; s_y = new float[cap]; s_z = new float[cap]; s_col = new int[cap];
        }
        // The tint, applied HERE because this is the one pass that already reads every voxel's
        // colour -- so it costs three multiplies on a loop that was happening anyway, and nothing
        // at all when it is white, which is every frame that is not a transition or a dusk.
        //
        // It has to be at draw time rather than at the producer. Geometry is cached by key and sent
        // once, so a tint applied when a quad was written reaches only what was written that frame:
        // the characters dim and the ground they are standing on does not. Everything the host is
        // holding is tinted here, including chunks sent minutes ago.
        float tr = s_tint.X, tg = s_tint.Y, tb = s_tint.Z;
        bool gamma = MathF.Abs(s_gamma - 1f) > 0.001f;
        if (gamma && s_gammaBuilt != s_gamma)
        {
            var built = BuildGamma(s_gamma);
            Array.Copy(built, s_gammaLut, 256);
            s_gammaBuilt = s_gamma;
        }
        bool grey = s_greyDim > 0.001f;
        float greySpan = s_greySpan, greyDim = s_greyDim;
        bool range = s_colorFloor != 0 || s_colorCeiling != 255;
        if (range && s_rangeBuilt != (s_colorFloor << 8 | s_colorCeiling))
            BuildRange(s_colorFloor, s_colorCeiling);
        bool plain = !gamma && !grey && !range && tr >= 0.999f && tg >= 0.999f && tb >= 0.999f;
        for (int i = 0; i < n; i++)
        {
            var v = voxels[i];
            s_x[i] = v.X; s_y[i] = v.Y; s_z[i] = v.Z;
            if (plain)
            {
                s_col[i] = (v.R << 16) | (v.G << 8) | v.B;      // 0xRRGGBB
            }
            else
            {
                // Clamped per channel. A tint above 1 would otherwise wrap a byte and turn a bright
                // face mid-grey in patches, which reads as a texturing fault rather than as too
                // much light -- the same trap the quad tint records on the producer's side.
                int r = (int)(v.R * tr), g = (int)(v.G * tg), b = (int)(v.B * tb);
                if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
                if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
                // Grey dimmed BEFORE gamma and after the tint, for the same reason gamma sits last:
                // this is a light level, and gamma is how the display's eight levels are spread
                // across whatever light level survives. Saturation is (max - min) / max, which is a
                // ratio and so survives the tint above unchanged.
                if (grey)
                {
                    int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
                    int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
                    float sat = (hi - lo) * s_invByte[hi];
                    if (sat < greySpan)
                    {
                        // 1 at the span, 1-greyDim at fully colourless. Linear between, so a shaded
                        // surface fades rather than banding at the edge of the ramp.
                        float k = 1f - greyDim * (1f - sat / greySpan);
                        r = (int)(r * k); g = (int)(g * k); b = (int)(b * k);
                    }
                }
                // Gamma AFTER the tint, because the tint is a light level and gamma is how this
                // display's eight levels are spread across the result of it.
                if (gamma) { r = s_gammaLut[r]; g = s_gammaLut[g]; b = s_gammaLut[b]; }
                // The range LAST of all, because it is the only step that describes the HARDWARE
                // rather than the picture: everything above decides how bright a voxel should be,
                // and this decides what this display can actually show. Putting it earlier would
                // let gamma push a value back out of the band it was just mapped into.
                //
                // Deliberately NOT applied to a voxel the producer sent as black - the frame's own
                // zeroes come through the plain branch above, which this one is the else of. The
                // floor is for lifting the dim, not for resurrecting the black.
                if (range) { r = s_rangeLut[r]; g = s_rangeLut[g]; b = s_rangeLut[b]; }
                s_col[i] = (r << 16) | (g << 8) | b;
            }
        }
        s_lastDrawn = n;
    }

    // Each SDK init call is isolated in its own non-inlined frame (see the class remarks).
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static bool CallLedHostInit() => s_ledHost.InitaliseLedHostCS(ref s_vs);

    [MethodImpl(MethodImplOptions.NoOptimization | MethodImplOptions.NoInlining)]
    private static void CallLedWinInit(string title)
    {
        // 1x1 and parked off-screen deliberately. A real LedWin window has its 2D framebuffer drawn
        // into the volume as a flat sheet through the middle of the scene, and it takes keyboard
        // focus. The preview comes from Rend2D into our own buffer instead.
        s_ledWin.LedWinInit(title, 1, 1, -32000, -32000);
    }

    public void Dispose()
    {
        if (!s_ready) return;
        s_ready = false;
        try { s_ledHost.UnLoadLedHostCS(ref s_vs); s_ledHost.Dispose(); } catch { }
        try { s_ledWin.UninitWindow(); s_ledWin.Dispose(); } catch { }
    }
}
