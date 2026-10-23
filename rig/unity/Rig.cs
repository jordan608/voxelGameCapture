using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using BepInEx;
using BepInEx.Logging;
using UnityEngine;

namespace VoxonCaptureRig.Unity
{
    /// <summary>The camera half of the capture rig, for Unity games: rig.lua's job (rig/unreal) in Unity's terms.
    ///
    /// <para>Up to six cameras on the faces of a box round the player, which follows the player and never turns. Each
    /// renders lit colour into an RGBA16F target, which has a depth buffer of its own. The ReShade add-on finds each
    /// target by the face code it is cleared to when made, takes the depth buffer drawn with it, and decodes the pair on
    /// the GPU (addon/, engine=unity). Nothing passes between this and the add-on at run time but the settings.</para>
    ///
    /// <para>The cameras are rendered by hand (Camera.Render), at the settings' rate, once the player is found. A shipped
    /// Unity game cannot compile a new shader, so depth is not packed into the colour's alpha as Unreal's capture does;
    /// that is the one difference the add-on handles.</para></summary>
    internal sealed class Rig : MonoBehaviour
    {
        public ManualLogSource Log;

        // The handshake: each target's first texel is set to this once, so the add-on can tell the faces apart with no
        // engine knowledge. The add-on's FaceCode reads it (exact in fp16: -4321 arrives as -4320, inside its tolerance).
        private const float MarkR = -1234f, MarkA = -4321f, CodeBase = 1000f;
        private const float PlayerCodeBase = 1500f;    // PLAYER LOOK: a face's player camera (rig.lua's PLAYER_CODE_BASE)
        private const float SolidCodeBase = 1250f;     // SEE-THROUGH SURFACES: its solid camera (rig.lua's SOLID_CODE_BASE)
        private const float DepthCodeBase = 1100f;     // HDRP: its depth target (the add-on's kDepthCodeBase)
        private const float HandshakeSeconds = 2f;     // captures held off while the add-on reads the codes

        private sealed class Face
        {
            public Camera Lens;
            public RenderTexture Colour;            // lit colour, and its own depth buffer
            // PLAYER LOOK: the same lens with only the player drawn, into a target of its own, whose depth buffer the
            // add-on reads beside the face's: where the two depths agree, the face's pixel is the player's.
            public Camera PlayerLens;
            public RenderTexture PlayerTarget;
            // SEE-THROUGH SURFACES: the same lens with only the chosen see-through renderers drawn, made opaque, into a
            // target of its own, whose depth buffer the add-on reads: where it is nearer than the face's, it is taken.
            public Camera SolidLens;
            public RenderTexture SolidTarget;
            // HDRP: the face's depth, copied out by HDRP into a target of its own (Hdrp); the camera's own output, unread.
            public RenderTexture Depth, Output;
        }

        private readonly Face[] _faces = new Face[6];
        private RigSettings _cfg;
        private Geometry _geo;
        private string _builtKey, _iniPath;
        private bool _on = true;
        private float _nextRead, _nextShot, _handshakeUntil, _nextReport;
        private int _shots;
        private Follow _follow;
        private GameObject _player;                     // the followed object (Follow): its renderers are the player's
        private float _heading;                         // the player's view, degrees in the rig's frame (Heading)
        private string _headingFrom;
        private bool _inputBroken, _saidNoIni, _f7Down;
        // PLAYER LOOK: the player's renderers, listed again once a second, and the spare layer they stand on while the
        // player cameras render (no layer in a game's own use: one with no name) - and SEE-THROUGH SURFACES' renderers
        // while the solid cameras do.
        private Renderer[] _playerRenderers = new Renderer[0];
        private int[] _savedLayers = new int[0];
        private float _nextPlayerList;
        private int _spareLayer = -1;
        private string _playerListSaid;
        private SeeThrough _see;
        private bool _solidDrawn;                       // the solid cameras' last render drew something
        private Urp _urp;                               // URP games: the cameras drawn straight into their targets
        private Hdrp _hdrp;                             // HDRP games: their colour and depth copied out by HDRP

#if IL2CPP
        // IL2CPP: the game makes the component (AddComponent) and hands its pointer here.
        public Rig(IntPtr ptr) : base(ptr) { }
#endif

        private void Start()
        {
            _iniPath = Path.Combine(Paths.GameRootPath, "voxon_capture_ue.ini");
            _see = new SeeThrough(Log);
            _follow = new Follow(Log);
            _urp = new Urp(Log);
            _hdrp = new Hdrp(Log);
        }

        private void Update()
        {
            // F7, as in Unreal games. A game that has switched Unity's old input off throws here; then F7 is read from
            // Windows itself, while the game is the window in front.
            bool f7 = false;
            if (!_inputBroken)
            {
                try { f7 = Input.GetKeyDown(KeyCode.F7); }
                catch (Exception) { _inputBroken = true; Log.LogInfo("this game has Unity's old input off: F7 read from Windows"); }
            }
            if (_inputBroken)
            {
                bool down = WindowsKey.Down(0x76);     // VK_F7
                f7 = down && !_f7Down;
                _f7Down = down;
            }
            if (f7) { _on = !_on; Log.LogInfo(_on ? "started (F7)" : "stopped (F7 starts it again)"); }
        }


        private void LateUpdate()
        {
            float now = Time.unscaledTime;
            // 30 times a second: two small files, and the volume follows the app's rig-scale keys while they are held.
            if (now >= _nextRead) { _nextRead = now + 1f / 30f; ReadSettings(); }
            if (_cfg == null || !_cfg.On || !_on) return;

            if (!FindPlayer(now)) return;
            if (BuildKey(_cfg) != _builtKey) Build();
            _see.Work(_cfg, now);
            if (now < _handshakeUntil) return;

            // A capture is due once 1 / hz of real time has passed; far behind (a load), start the schedule again.
            float period = 1f / _cfg.Hz;
            if (now < _nextShot) return;
            _nextShot = Math.Max(_nextShot + period, now + period * 0.5f);

            // Turning with the player, the rig turns by the player's view as well as by its own yaw. The add-on turns
            // the finished volume back by the yaw alone, so the view lands on display -Y (capture_gpu.hpp,
            // DisplayTurnDeg), and the display is the world turned by the view and a quarter turn. Off, the display
            // keeps the world's axes. rig.lua does the same.
            // FOLLOW VIEW: the rig as much larger as the game's camera is further than followView from what it looks at.
            if (_cfg.FollowView > 0) _geo = Geometry.Derive(_cfg, Math.Max(_follow.ViewDistance, 0.1f) / _cfg.FollowView);
            double rigYaw = _cfg.YawDeg, dispTurn = _cfg.YawDeg;
            if (_cfg.TurnWithPlayer)
            {
                _heading = Heading(_heading);
                rigYaw += _heading;
                dispTurn = _cfg.YawDeg - 90;
            }
            // The volume's centre: the feet, less where they stand in the display (+Z down) - the add-on's mapping undone:
            // the display's turn back into the rig's frame, each axis's SCALE, then the rig's yaw into the world.
            double tA = dispTurn * Math.PI / 180, yA = rigYaw * Math.PI / 180;
            double u = (_cfg.FeetX * Math.Cos(tA) + _cfg.FeetY * Math.Sin(tA)) * (_geo.Ex / _geo.R);
            double w = (-_cfg.FeetX * Math.Sin(tA) + _cfg.FeetY * Math.Cos(tA)) * (_geo.Ey / _geo.R);
            var offset = new Vector3((float)(u * Math.Cos(yA) - w * Math.Sin(yA)), (float)(u * Math.Sin(yA) + w * Math.Cos(yA)), 0f);
            Vector3 centre = _follow.Feet() - Geometry.ToUnity(offset * _geo.S)
                             + Vector3.up * (_cfg.FeetZ * _geo.S * (_geo.Ez / _geo.Z));
            int mask = CullingMask();
            _urp.Begin();
            try
            {
                for (int v = 0; v < 6; v++)
                {
                    Face f = _faces[v];
                    if (f == null) continue;
                    _geo.Place(v, _cfg, centre, rigYaw, f.Lens);
                    f.Lens.cullingMask = mask;
                    f.Lens.Render();
                }
                // The solid and player cameras with the chosen see-through materials opaque, and those set back straight after.
                if ((_cfg.UseSolid || _cfg.PlayerLook) && _spareLayer >= 0)
                {
                    _see.MakeSolid(_cfg, centre, (float)(Math.Sqrt(_geo.Ex * _geo.Ex + _geo.Ey * _geo.Ey + _geo.Ez * _geo.Ez) * _geo.S), now);
                    try
                    {
                        if (_cfg.UseSolid) RenderSolid(centre, rigYaw);
                        if (_cfg.PlayerLook) RenderPlayer(now, centre, rigYaw);
                    }
                    finally { _see.Restore(); }
                }
            }
            finally { _urp.End(); }
            Snapshot();
            _shots++;
            if (now >= _nextReport)
            {
                Log.LogInfo($"capturing: centre ({centre.x:0.0}, {centre.y:0.0}, {centre.z:0.0}), scale {_geo.S:0.00} m/unit, " +
                            $"{_shots / 5f:0.0} captures/s" + (_cfg.TurnWithPlayer ? $", view {_heading:0} deg" : ""));
                _shots = 0;
                _nextReport = now + 5f;
            }
        }

        /// <summary>SNAPSHOT, as the Unreal rig's: when the settings' snapshot number changes (not at the first reading), each
        /// face's picture as it was just captured is written to a snapshots folder beside the app (beside the game without
        /// one), face&lt;v&gt;_&lt;n&gt;.png, display-encoded - for seeing what the cameras see. Its depth is not written.</summary>
        private void Snapshot()
        {
            if (_snapshot == null) _snapshot = _cfg.Snapshot;
            if (_cfg.Snapshot == _snapshot) return;
            _snapshot = _cfg.Snapshot;
            string dir = Path.Combine(_cfg.ControlFile.Length > 0 ? Path.GetDirectoryName(_cfg.ControlFile) : Paths.GameRootPath, "snapshots");
            Directory.CreateDirectory(dir);
            RenderTexture was = RenderTexture.active;
            for (int v = 0; v < 6; v++)
            {
                Face f = _faces[v];
                if (f == null) continue;
                string file = Path.Combine(dir, $"face{v}_{_cfg.Snapshot}.png");
                var pic = new Texture2D(f.Colour.width, f.Colour.height, TextureFormat.RGBAHalf, false, true);
                try
                {
                    RenderTexture.active = f.Colour;
                    pic.ReadPixels(new Rect(0, 0, f.Colour.width, f.Colour.height), 0, 0);
                    var px = pic.GetPixels();
                    for (int i = 0; i < px.Length; i++)
                    {
                        Color c = px[i];   // linear HDR, before any tone mapping: clamped and encoded for the eye
                        px[i] = new Color(Mathf.Pow(Mathf.Clamp01(c.r), 1 / 2.2f), Mathf.Pow(Mathf.Clamp01(c.g), 1 / 2.2f),
                                          Mathf.Pow(Mathf.Clamp01(c.b), 1 / 2.2f), 1f);
                    }
                    var png = new Texture2D(pic.width, pic.height, TextureFormat.RGBA32, false);
                    png.SetPixels(px);
                    File.WriteAllBytes(file, ImageConversion.EncodeToPNG(png).ToArray());
                    Destroy(png);
                    Log.LogInfo($"snapshot: face {v} -> {file}");
                }
                catch (Exception e) { Log.LogWarning($"snapshot: face {v} FAILED: {e.Message}"); }
                finally { Destroy(pic); }
            }
            RenderTexture.active = was;
        }

        private int? _snapshot;

        private void ReadSettings()
        {
            RigSettings c;
            try { c = RigSettings.Load(_iniPath); }
            catch (Exception e) { Log.LogWarning($"settings: {e.Message}"); return; }
            if (c == null)
            {
                if (!_saidNoIni) Log.LogWarning($"no {_iniPath}: rig idle");
                _saidNoIni = true;
                _cfg = null;
                return;
            }
            _saidNoIni = false;
            if (_cfg != null && c.Raw == _cfg.Raw) return;
            bool scaleOnly = _cfg != null && WithoutScale(c.Raw) == WithoutScale(_cfg.Raw);
            _cfg = c;
            _geo = Geometry.Derive(c);
            if (scaleOnly) return;          // the rig's scale alone, as the app's keys change it: taken without a word
            _playerListSaid = null;
            Log.LogInfo($"settings: faces {FacesKey(c)}, {c.FaceRes} px, radius {c.RadiusM:0.0} m, {(c.Perspective ? "perspective" : "orthographic")}, " +
                        $"tilt {c.TiltDeg:0}, yaw {c.YawDeg:0}{(c.TurnWithPlayer ? " turning with the player" : "")}, " +
                        $"feet ({c.FeetX:0.00}, {c.FeetY:0.00}, {c.FeetZ:0.00}), {c.Hz} Hz, camera distance {c.CamDist:0.00}" +
                        (c.PlayerLook ? ", player look" : "") +
                        (c.UseSolid ? (c.SolidAll ? ", every see-through surface solid" : ", chosen see-through surfaces solid") : "") +
                        (c.ControlFile.Length > 0 ? " (the app's Cameras tab over the ini)" : ""));
        }

        /// <summary>The settings' text without the rig's scale (radiusM, ueRadius).</summary>
        private static string WithoutScale(string raw) =>
            string.Join("\n", raw.Split('\n').Where(l => !l.TrimStart().StartsWith("radiusM", StringComparison.Ordinal)
                                                         && !l.TrimStart().StartsWith("ueRadius", StringComparison.Ordinal)));

        /// <summary>PLAYER LOOK: each face's player camera, with only the player's renderers in it - moved onto the spare
        /// layer for these renders alone and put back at once, so the game never sees them there.</summary>
        private void RenderPlayer(float now, Vector3 centre, double rigYaw)
        {
            if (now >= _nextPlayerList) { _nextPlayerList = now + 1f; ListPlayerRenderers(); }
            var rs = _playerRenderers;
            if (_savedLayers.Length != rs.Length) _savedLayers = new int[rs.Length];
            for (int i = 0; i < rs.Length; i++)
            {
                if (rs[i] == null) continue;
                _savedLayers[i] = rs[i].gameObject.layer;
                rs[i].gameObject.layer = _spareLayer;
            }
            try
            {
                for (int v = 0; v < 6; v++)
                {
                    Face f = _faces[v];
                    if (f?.PlayerLens == null) continue;
                    _geo.Place(v, _cfg, centre, rigYaw, f.PlayerLens);
                    f.PlayerLens.cullingMask = 1 << _spareLayer;
                    f.PlayerLens.Render();
                }
            }
            finally
            {
                // Backwards: a GameObject with two renderers was saved twice, the second time already moved.
                for (int i = rs.Length - 1; i >= 0; i--) if (rs[i] != null) rs[i].gameObject.layer = _savedLayers[i];
            }
        }

        /// <summary>SEE-THROUGH SURFACES: each face's solid camera, with only the renderers made opaque in it - moved onto
        /// the spare layer for these renders alone. With none, skipped, after one render that leaves its depth empty.</summary>
        private void RenderSolid(Vector3 centre, double rigYaw)
        {
            List<Renderer> rs = _see.Solid;
            bool drawn = rs.Count > 0;
            if (!drawn && !_solidDrawn) return;
            _solidDrawn = drawn;
            var layers = new int[rs.Count];
            for (int i = 0; i < rs.Count; i++) { layers[i] = rs[i].gameObject.layer; rs[i].gameObject.layer = _spareLayer; }
            try
            {
                for (int v = 0; v < 6; v++)
                {
                    Face f = _faces[v];
                    if (f?.SolidLens == null) continue;
                    _geo.Place(v, _cfg, centre, rigYaw, f.SolidLens);
                    f.SolidLens.cullingMask = 1 << _spareLayer;
                    f.SolidLens.Render();
                }
            }
            finally
            {
                for (int i = rs.Count - 1; i >= 0; i--) if (rs[i] != null) rs[i].gameObject.layer = layers[i];
            }
        }

        /// <summary>The player's renderers: those under the followed object, and under the model the profile names
        /// (playerModel, "Component.member") when the game keeps it elsewhere.</summary>
        private void ListPlayerRenderers()
        {
            var list = new List<Renderer>();
            foreach (GameObject go in _follow.All()) list.AddRange(go.GetComponentsInChildren<Renderer>());   // followAll: all of them
            var ids = new HashSet<int>();
            foreach (Renderer r in list) ids.Add(r.GetInstanceID());
            string from = "the followed object";
            Transform model = PlayerModel();
            if (model != null)
            {
                foreach (Renderer r in model.GetComponentsInChildren<Renderer>()) if (ids.Add(r.GetInstanceID())) list.Add(r);
                from += $" and {model.name}";
            }
            _playerRenderers = list.ToArray();
            string said = $"{_playerRenderers.Length} renderer(s) from {from}";
            if (said != _playerListSaid) { _playerListSaid = said; Log.LogInfo($"player look: {said}, drawn on layer {_spareLayer}"); }
        }

        /// <summary>The transform the profile's playerModel names: a component of the followed object, by its type's
        /// name, and a field or property of it holding a Transform, a GameObject or a Component.</summary>
        private Transform PlayerModel()
        {
            string spec = _cfg.PlayerModel;
            int dot = spec.LastIndexOf('.');
            if (dot <= 0) return null;
            Component comp = _player.GetComponent(spec.Substring(0, dot));
            if (comp == null) return null;
            return Members.TransformOf(Members.Get(Members.Own(comp), spec.Substring(dot + 1)));
        }

        /// <summary>The layer the player stands on while the player cameras render: one no one uses (no name), else one the
        /// faces never draw (faceMask), so nothing else on it can be the surface a face sees and be taken for the player
        /// (Risk of Rain 2 names all 32). -1 if none.</summary>
        private static int SpareLayer(int faceMask)
        {
            for (int i = 31; i >= 8; i--) if (LayerMask.LayerToName(i).Length == 0) return i;
            for (int i = 31; i >= 0; i--) if (i != 5 && (faceMask & (1 << i)) == 0) return i;
            return -1;
        }

        /// <summary>The way the player is looking, as a yaw in degrees in the rig's frame (Unreal's: from +X toward +Y):
        /// the game camera's, which is what the screen shows, else the followed object's own facing. Looking straight
        /// up or down has no heading, and the last one is kept.</summary>
        private float Heading(float last)
        {
            Camera main = Camera.main;
            string from = main != null ? "the game's camera" : "the player's facing";
            Vector3 f = main != null ? main.transform.forward : _player.transform.forward;
            if (from != _headingFrom) { _headingFrom = from; Log.LogInfo($"turning with {from}"); }
            if (f.x * f.x + f.z * f.z < 1e-6f) return last;
            return Mathf.Atan2(f.x, f.z) * Mathf.Rad2Deg;   // Unity (x, y, z) is the rig's (y, z, x): Geometry.ToUnity
        }

        /// <summary>The followed object (Follow), found again when it is gone - a new level makes a new one.</summary>
        private bool FindPlayer(float now)
        {
            GameObject was = _player;
            _player = _follow.Find(_cfg, now) ? _follow.Object : null;
            if (_player != was) _nextPlayerList = 0;    // a new player: its renderers listed again
            return _player != null;
        }

        /// <summary>The cameras and their targets, made again whenever the faces or the size change. Each target is
        /// cleared to its face code and left alone for a moment, so the add-on can find it.</summary>
        private void Build()
        {
            DestroyAll();
            for (int v = 0; v < 6; v++)
            {
                if (!_cfg.FaceOn[v]) continue;
                int w = _cfg.FaceRes, h = Geometry.FaceHeight(v, _cfg);
                // ONE TARGET WITH ITS OWN DEPTH, set as the camera's targetTexture - Unity's ordinary path. A colour and
                // a depth buffer from two targets (SetTargetBuffers) got the depth but never the colour: in deferred
                // shading Unity lit into its own buffer and did not copy it across (Risk of Rain 2, 2026-10-12). The
                // add-on takes the depth buffer that is bound with the colour target, so it finds this one the same way.
                RenderTexture colour = Target($"VoxonRig face {v}", w, h);
                WriteCode(colour, CodeBase + v);
                var face = new Face { Colour = colour, Lens = MakeLens($"VoxonRig camera {v}", colour) };
                if (_hdrp.On)
                {
                    // HDRP: it copies the lit colour into the face's target and the depth into one of its own, coded
                    // for the add-on; the camera's own output, post-processed, goes where nobody reads it.
                    face.Depth = Target($"VoxonRig face {v} depth", w, h);
                    WriteCode(face.Depth, DepthCodeBase + v);
                    face.Output = new RenderTexture(w, h, 24, RenderTextureFormat.ARGB32) { name = $"VoxonRig face {v} output", hideFlags = HideFlags.HideAndDontSave };
                    face.Output.Create();
                    face.Lens.targetTexture = face.Output;
                    _hdrp.Setup(face.Lens, colour, face.Depth);
                }
                if (_cfg.PlayerLook)
                {
                    // PLAYER LOOK: the same size and kind of target, its own code; forward rendering, since only its
                    // depth is read and the player alone is drawn.
                    face.PlayerTarget = Target($"VoxonRig face {v} player", w, h);
                    WriteCode(face.PlayerTarget, PlayerCodeBase + v);
                    face.PlayerLens = MakeLens($"VoxonRig camera {v} player", face.PlayerTarget);
                    face.PlayerLens.renderingPath = RenderingPath.Forward;
                }
                if (_cfg.UseSolid)
                {
                    // SEE-THROUGH SURFACES: the same again, its own code.
                    face.SolidTarget = Target($"VoxonRig face {v} solid", w, h);
                    WriteCode(face.SolidTarget, SolidCodeBase + v);
                    face.SolidLens = MakeLens($"VoxonRig camera {v} solid", face.SolidTarget);
                    face.SolidLens.renderingPath = RenderingPath.Forward;
                }
                _faces[v] = face;
                Log.LogInfo($"face {v}: {w}x{h} target, code {CodeBase + v}" + (face.Depth != null ? $", depth code {DepthCodeBase + v}" : "") +
                            (face.PlayerLens != null ? $", player code {PlayerCodeBase + v}" : "") +
                            (face.SolidLens != null ? $", solid code {SolidCodeBase + v}" : ""));
            }
            if (_cfg.PlayerLook || _cfg.UseSolid)
            {
                _spareLayer = SpareLayer(CullingMask());
                if (_spareLayer < 0) Log.LogWarning("no spare layer in this game, so the player keeps the world's look and see-through surfaces stay see-through");
                else Log.LogInfo($"the player and see-through surfaces drawn alone on layer {_spareLayer} ({LayerMask.LayerToName(_spareLayer)})");
            }
            _solidDrawn = true;
            _builtKey = BuildKey(_cfg);
            _handshakeUntil = Time.unscaledTime + HandshakeSeconds;
        }

        private static RenderTexture Target(string name, int w, int h)
        {
            // HideAndDontSave: kept through the game's Resources.UnloadUnusedAssets at a level change. Under IL2CPP nothing
            // on the game's side refers to the rig's targets, and Undisputed's fight unloaded them from under HDRP.
            var rt = new RenderTexture(w, h, 32, RenderTextureFormat.ARGBHalf, RenderTextureReadWrite.Linear)
            {
                name = name, useMipMap = false, autoGenerateMips = false, antiAliasing = 1, filterMode = FilterMode.Point,
                hideFlags = HideFlags.HideAndDontSave,
            };
            rt.Create();
            return rt;
        }

        private Camera MakeLens(string name, RenderTexture target)
        {
            var go = new GameObject(name);
            go.transform.SetParent(transform, false);
            var lens = go.AddComponent<Camera>();
            lens.enabled = false;                      // rendered by hand, at the rig's rate
            lens.clearFlags = CameraClearFlags.SolidColor;
            lens.backgroundColor = Color.black;
            lens.allowHDR = true;                      // linear HDR colour, before any tone mapping: the add-on maps it
            lens.allowMSAA = false;
            lens.depthTextureMode = DepthTextureMode.None;
            lens.targetTexture = target;
            _urp.Setup(lens);
            return lens;
        }

        /// <summary>The face code into the target's first texel. Copied from a 1x1 texture of the same format rather than
        /// cleared to a colour: a clear colour goes through the project's colour space, and the code would arrive
        /// changed.</summary>
        private static void WriteCode(RenderTexture rt, float codeG)
        {
            var code = new Texture2D(1, 1, TextureFormat.RGBAHalf, false, true);
            ushort[] px = { Half(MarkR), Half(codeG), Half(0f), Half(MarkA) };
            var bytes = new byte[8];
            Buffer.BlockCopy(px, 0, bytes, 0, 8);
            code.LoadRawTextureData(bytes);
            code.Apply(false);
            Graphics.CopyTexture(code, 0, 0, 0, 0, 1, 1, rt, 0, 0, 0, 0);
            Destroy(code);
        }

        /// <summary>The player camera's layers, without the UI layer (5): the volume shows the world, not the HUD.</summary>
        private static int CullingMask()
        {
            Camera main = Camera.main;
            int mask = main != null ? main.cullingMask : ~0;
            return mask & ~(1 << 5);
        }

        private void DestroyAll()
        {
            for (int v = 0; v < 6; v++)
            {
                Face f = _faces[v];
                if (f == null) continue;
                if (f.Lens != null) { f.Lens.targetTexture = null; Destroy(f.Lens.gameObject); }
                if (f.Colour != null) { f.Colour.Release(); Destroy(f.Colour); }
                if (f.PlayerLens != null) { f.PlayerLens.targetTexture = null; Destroy(f.PlayerLens.gameObject); }
                if (f.PlayerTarget != null) { f.PlayerTarget.Release(); Destroy(f.PlayerTarget); }
                if (f.SolidLens != null) { f.SolidLens.targetTexture = null; Destroy(f.SolidLens.gameObject); }
                if (f.SolidTarget != null) { f.SolidTarget.Release(); Destroy(f.SolidTarget); }
                if (f.Depth != null) { f.Depth.Release(); Destroy(f.Depth); }
                if (f.Output != null) { f.Output.Release(); Destroy(f.Output); }
                _faces[v] = null;
            }
            _hdrp?.Release();
            _builtKey = null;
        }

        private void OnDestroy() => DestroyAll();

        private static string BuildKey(RigSettings c) => $"{FacesKey(c)}|{c.FaceRes}|{Geometry.SideHeight(c)}|{c.PlayerLook}|{c.UseSolid}";

        private static string FacesKey(RigSettings c)
        {
            var s = "";
            for (int v = 0; v < 6; v++) if (c.FaceOn[v]) s += v;
            return s;
        }

        /// <summary>A float to IEEE half bits, enough for the face code: normal numbers, rounded to nearest.</summary>
        private static ushort Half(float f)
        {
            uint x = BitConverter.ToUInt32(BitConverter.GetBytes(f), 0);
            uint sign = (x >> 16) & 0x8000u;
            int exp = (int)((x >> 23) & 0xFF) - 127 + 15;
            uint mant = x & 0x7FFFFFu;
            if (exp <= 0) return (ushort)sign;                       // too small: zero
            if (exp >= 31) return (ushort)(sign | 0x7C00u);          // too large: infinity
            uint h = sign | ((uint)exp << 10) | (mant >> 13);
            if ((mant & 0x1000u) != 0) h++;                          // round to nearest
            return (ushort)h;
        }
    }

    /// <summary>A key held down, from Windows itself, while the game is the window in front: F7 for a game that has
    /// Unity's old input off.</summary>
    internal static class WindowsKey
    {
        [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int vk);
        [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("kernel32.dll")] private static extern uint GetCurrentProcessId();

        public static bool Down(int vk)
        {
            if ((GetAsyncKeyState(vk) & 0x8000) == 0) return false;
            GetWindowThreadProcessId(GetForegroundWindow(), out uint pid);
            return pid == GetCurrentProcessId();
        }
    }
}
