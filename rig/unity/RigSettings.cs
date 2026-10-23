using System;
using System.Globalization;
using System.IO;

namespace VoxonCaptureRig.Unity
{
    /// <summary>The rig's settings: voxon_capture_ue.ini beside the game's exe, then the app's control file over it
    /// when that file names this game. The same keys, defaults and limits as rig.lua and the add-on's LoadSettings
    /// (addon/capture_gpu.hpp), because the add-on decodes these cameras from the same settings and the two must
    /// agree exactly.</summary>
    internal sealed class RigSettings
    {
        public bool On = true;
        public float RadiusM = 12f, FeetX = 0f, FeetY = 0f, FeetZ = 1.5f, BoundR = 4f, BoundZ = 2f;
        public bool[] FaceOn = { true, true, false, false, true, false };
        public int FaceRes = 512;
        public float TiltDeg = 30f;
        public int Hz = 30;
        public float CamDist = 1f, YawDeg = 0f;
        public float ScaleX = 1f, ScaleY = 1f, ScaleZ = 1f;   // world along the rig's X, Y and up, as multiples of RadiusM's
        public bool TurnWithPlayer;              // the rig turns with the player's view too, and the volume with it
        public bool Perspective = true;
        public string ControlFile = "", HostGame = "";
        public string FollowTag = "Player";      // the object the volume follows: the one carrying this tag
        // Or, for a game that tags no player, the first with a component of this class ("Namespace.Class, Assembly")
        // whose FollowWhen members hold ("!" false, "&" between); its feet from the FollowPoint members ("|" between):
        // Follow.
        public string FollowComponent = "", FollowWhen = "", FollowPoint = "";
        // ...FollowAll: every one of that class whose FollowWhen members hold, not the first - the feet above the middle
        // of them all, as low as the lowest, and all their renderers the player's (the two boxers of a fight).
        public bool FollowAll;
        // FOLLOW VIEW: above 0, the volume follows the point the game's camera looks at, in place of any of those, and
        // holds RadiusM of the world when the camera is this many metres from that point - more as it draws back.
        public float FollowView;
        // ...ViewPoint: where the camera looks, read from the game - "Class, Assembly: member" (Follow.Reading) - in place
        // of the first thing straight ahead (Cities: Skylines II's camera orbits a point it keeps). FeetHeight, for any
        // follow: the ground plane's height, a number or a reading (its sea level; a ring's canvas); empty, the point's own.
        public string ViewPoint = "", FeetHeight = "";
        // SNAPSHOT: change the number and each face's picture is written to a snapshots folder beside the app (Rig).
        public int Snapshot;
        // PLAYER LOOK: each camera also renders the player alone, so the add-on can give the player a look of its own.
        // PlayerModel: where a game keeps the player's model when it is not under the followed object, as
        // "Component.member" on that object (Risk of Rain 2: ModelLocator.modelTransform); empty, its children alone.
        public bool PlayerLook;
        public string PlayerModel = "";
        // SEE-THROUGH SURFACES: a see-through material (Unity's transparent queue) writes no depth, so a camera lays its
        // colour on whatever is behind it. SeeList: the ones met are listed for the app (SeeThrough). SolidMaterials (their
        // names, | between) or SolidAll: drawn opaque by each face's solid camera, whose depth the add-on takes where they
        // stand in front of the scene.
        public bool SeeList, SolidAll;
        public string SolidMaterials = "";
        public bool UseSolid => SolidAll || SolidMaterials.Length > 0;

        /// <summary>Both files, as text: reloaded only when this changes.</summary>
        public string Raw = "";

        public static RigSettings Load(string iniPath)
        {
            var c = new RigSettings();
            if (!File.Exists(iniPath)) return null;
            string ini = File.ReadAllText(iniPath);
            Apply(ini, c, false);
            c.Raw = ini;
            if (c.ControlFile.Length > 0 && File.Exists(c.ControlFile))
            {
                string host;
                try { host = File.ReadAllText(c.ControlFile); } catch (IOException) { host = ""; }
                if (Value(host, "game") == c.HostGame)
                {
                    Apply(host, c, true);
                    c.Raw += host;
                }
            }
            c.FaceRes = Math.Max(64, Math.Min(2560, c.FaceRes)) / 2 * 2;   // six faces stay inside D3D's 16384
            c.CamDist = Math.Max(1f, Math.Min(8f, c.CamDist));
            c.ScaleX = Math.Max(0.05f, c.ScaleX); c.ScaleY = Math.Max(0.05f, c.ScaleY); c.ScaleZ = Math.Max(0.05f, c.ScaleZ);
            if (c.BoundR <= 0) c.BoundR = 4f;
            if (c.BoundZ <= 0) c.BoundZ = 2f;
            c.RadiusM = Math.Max(0.5f, Math.Min(1000f, c.RadiusM));
            return c;
        }

        private static void Apply(string text, RigSettings c, bool host)
        {
            foreach (string raw in text.Split('\n'))
            {
                string line = raw;
                int semi = line.IndexOf(';');
                if (semi >= 0) line = line.Substring(0, semi);
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;
                string k = line.Substring(0, eq).Trim(), v = line.Substring(eq + 1).Trim();
                if (host)
                {
                    switch (k)   // the app's Cameras tab
                    {
                        case "ueFaces": SetFaces(c, v); break;
                        case "ueFaceRes": c.FaceRes = (int)F(v, c.FaceRes); break;
                        case "ueRadius": c.RadiusM = F(v, c.RadiusM); break;
                        case "ueCamDist": c.CamDist = F(v, c.CamDist); break;
                        case "ueYaw": c.YawDeg = F(v, c.YawDeg); break;
                        case "ueTilt": c.TiltDeg = F(v, c.TiltDeg); break;
                        case "uePerspective": c.Perspective = F(v, 1) != 0; break;
                        case "ueFeetX": c.FeetX = F(v, c.FeetX); break;
                        case "ueFeetY": c.FeetY = F(v, c.FeetY); break;
                        case "ueFeetZ": c.FeetZ = F(v, c.FeetZ); break;
                        case "ueTurn": c.TurnWithPlayer = F(v, 0) != 0; break;
                        case "ueScaleX": c.ScaleX = F(v, c.ScaleX); break;
                        case "ueScaleY": c.ScaleY = F(v, c.ScaleY); break;
                        case "ueScaleZ": c.ScaleZ = F(v, c.ScaleZ); break;
                        case "ueBoundR": c.BoundR = F(v, c.BoundR); break;      // the app's Display
                        case "ueBoundZ": c.BoundZ = F(v, c.BoundZ); break;
                        case "uePlayerLook": c.PlayerLook = F(v, 0) != 0; break;
                        case "ueSeeList": c.SeeList = F(v, 0) != 0; break;
                        case "ueSolidAll": c.SolidAll = F(v, 0) != 0; break;
                        case "ueSolidMaterials": c.SolidMaterials = v; break;
                    }
                    continue;
                }
                switch (k)
                {
                    case "on": c.On = F(v, 1) != 0; break;
                    case "radiusM": c.RadiusM = F(v, c.RadiusM); break;
                    case "feetX": c.FeetX = F(v, c.FeetX); break;
                    case "feetY": c.FeetY = F(v, c.FeetY); break;
                    case "feetZ": c.FeetZ = F(v, c.FeetZ); break;
                    case "boundR": c.BoundR = F(v, c.BoundR); break;
                    case "boundZ": c.BoundZ = F(v, c.BoundZ); break;
                    case "faces": SetFaces(c, v); break;
                    case "faceRes": c.FaceRes = (int)F(v, c.FaceRes); break;
                    case "tiltDeg": c.TiltDeg = F(v, c.TiltDeg); break;
                    case "hz": c.Hz = Math.Max(1, (int)F(v, c.Hz)); break;
                    case "camDist": c.CamDist = F(v, c.CamDist); break;
                    case "yawDeg": c.YawDeg = F(v, c.YawDeg); break;
                    case "turnWithPlayer": c.TurnWithPlayer = F(v, 0) != 0; break;
                    case "scaleX": c.ScaleX = F(v, c.ScaleX); break;
                    case "scaleY": c.ScaleY = F(v, c.ScaleY); break;
                    case "scaleZ": c.ScaleZ = F(v, c.ScaleZ); break;
                    case "perspective": c.Perspective = F(v, 1) != 0; break;
                    case "controlFile": c.ControlFile = v; break;
                    case "hostGame": c.HostGame = v; break;
                    case "followTag": c.FollowTag = v; break;
                    case "followComponent": c.FollowComponent = v; break;
                    case "followWhen": c.FollowWhen = v; break;
                    case "followPoint": c.FollowPoint = v; break;
                    case "followAll": c.FollowAll = F(v, 0) != 0; break;
                    case "followView": c.FollowView = Math.Max(0f, F(v, 0)); break;
                    case "viewPoint": c.ViewPoint = v; break;
                    case "feetHeight": c.FeetHeight = v; break;
                    case "snapshot": c.Snapshot = (int)F(v, 0); break;
                    case "playerLook": c.PlayerLook = F(v, 0) != 0; break;
                    case "playerModel": c.PlayerModel = v; break;
                    case "seeList": c.SeeList = F(v, 0) != 0; break;
                    case "solidAll": c.SolidAll = F(v, 0) != 0; break;
                    case "solidMaterials": c.SolidMaterials = v; break;
                }
            }
        }

        private static void SetFaces(RigSettings c, string v)
        {
            c.FaceOn = new bool[6];
            foreach (char ch in v) if (ch >= '0' && ch <= '5') c.FaceOn[ch - '0'] = true;
        }

        private static float F(string v, float fallback) =>
            float.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out float f) ? f : fallback;

        private static string Value(string text, string key)
        {
            foreach (string raw in text.Split('\n'))
            {
                int eq = raw.IndexOf('=');
                if (eq > 0 && raw.Substring(0, eq).Trim() == key) return raw.Substring(eq + 1).Trim();
            }
            return null;
        }
    }
}
