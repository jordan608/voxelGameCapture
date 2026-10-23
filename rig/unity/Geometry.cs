using System;
using UnityEngine;

namespace VoxonCaptureRig.Unity
{
    /// <summary>Where each camera goes, and its lens. PORTED term for term from the add-on's Derive, FrameFor and
    /// DepthPlanes (addon/capture_gpu.hpp) and the GPU self-test's placeFace (addon/selftest_gpu.cpp), which is the
    /// Unreal rig's placement: the add-on decodes these cameras with those same formulas, so a difference here is a
    /// systematic offset of the whole volume.
    ///
    /// <para>The formulas are in the rig's own frame, Unreal's axes (X and Y level, Z up). Unity's are Y up, so a
    /// vector goes across as (x, y, z) to (y, z, x): a cyclic swap, which keeps handedness, so nothing is mirrored.
    /// Lengths are metres throughout (Unreal's are centimetres, which the add-on accounts for on that side).</para>
    /// </summary>
    internal sealed class Geometry
    {
        // SCALE: the cameras capture a box of half-extents Ex, Ey, Ez along the rig's X, Y and up - the display's R, R and
        // Z times the settings' scales - and the add-on scales each axis back into the display. Each side camera frames its
        // own box: faces 0 and 2 look along X (through Ex, across Ey), 1 and 3 along Y. With every scale 1 these are the
        // original formulas. Perspective lenses only.
        public float S, R, Z, Ex, Ey, Ez, Ct, St, TopDist, NTop, TanP;
        public readonly float[] HalfDepth = new float[4], VertHalf = new float[4], HalfWin = new float[4],
                                SideDist = new float[4], NSide = new float[4];
        public int SideH;
        public bool Persp;

        /// <summary>zoom: FOLLOW VIEW's - the whole rig that much larger: every length here goes with S. Perspective
        /// lenses then take the same pictures as of a world that much smaller, so the add-on, which decodes at the
        /// settings' radius, needs no word of it.</summary>
        public static Geometry Derive(RigSettings c, float zoom = 1f)
        {
            var g = new Geometry();
            g.S = c.RadiusM * zoom / c.BoundR;
            g.R = c.BoundR * g.S;
            g.Z = c.BoundZ * g.S;
            g.Persp = c.Perspective;
            g.Ex = g.R * (g.Persp ? c.ScaleX : 1f);
            g.Ey = g.R * (g.Persp ? c.ScaleY : 1f);
            g.Ez = g.Z * (g.Persp ? c.ScaleZ : 1f);
            float tilt = Mathf.Clamp(c.TiltDeg, 0f, 90f) * Mathf.Deg2Rad;
            g.Ct = Mathf.Cos(tilt);
            g.St = Mathf.Sin(tilt);
            g.SideH = SideHeight(c);
            float k = Math.Max(1f, c.CamDist);
            if (g.Persp) g.TanP = 1f / k;
            for (int v = 0; v < 4; v++)
            {
                float along = v % 2 == 0 ? g.Ex : g.Ey, across = v % 2 == 0 ? g.Ey : g.Ex;
                g.HalfDepth[v] = along * g.Ct + g.Ez * g.St;
                g.VertHalf[v] = along * g.St + g.Ez * g.Ct;
                g.HalfWin[v] = Math.Max(across, g.VertHalf[v] * c.FaceRes / g.SideH);
                g.SideDist[v] = g.HalfDepth[v] * c.CamDist;
                if (g.Persp)
                {
                    g.NSide[v] = g.HalfWin[v] * k;
                    g.SideDist[v] = g.HalfDepth[v] + g.NSide[v];
                }
            }
            g.TopDist = g.Ez * c.CamDist;
            if (g.Persp)
            {
                g.NTop = Math.Max(g.Ex, g.Ey) * k;
                g.TopDist = g.Ez + g.NTop;
            }
            return g;
        }

        /// <summary>A side face's height: half its width on a display at least twice as wide as tall (VX2-XL), square on
        /// one as tall as wide (VX2). capture_gpu.hpp's SideHeight.</summary>
        public static int SideHeight(RigSettings c) => 2f * c.BoundZ <= c.BoundR * 1.001f ? c.FaceRes / 2 : c.FaceRes;

        public static int FaceHeight(int face, RigSettings c) => face >= 4 ? c.FaceRes : SideHeight(c);

        /// <summary>capture_gpu.hpp's Reach: how far past the centre a face's camera may see and still be in the box.</summary>
        private float Reach(int face)
        {
            float slab = face < 4 ? HalfDepth[face] : Math.Max(HalfDepth[0], HalfDepth[1]);
            return Math.Max(Math.Max(Ex, Ey), Math.Max(Ez, slab));
        }

        /// <summary>The add-on's DepthPlanes: the near clip on the volume's near face less 2% (orthographic, just in
        /// front of the camera), and the far one past the volume's far side with a margin.</summary>
        public void DepthPlanes(int face, out float nearM, out float farM)
        {
            float camD = face >= 4 ? TopDist : SideDist[face];
            nearM = Persp ? (face >= 4 ? NTop : NSide[face]) * 0.98f : 0.05f;
            farM = (camD + 2f * Reach(face)) * 1.5f;
        }

        /// <summary>A face's camera: placeFace, in the rig's frame (metres from the centre), then turned into Unity's
        /// axes. <paramref name="rigYawDeg"/> is the rig's yaw for this capture: its own, and the player's view when it
        /// turns with the player. <paramref name="lens"/> is the camera to set.</summary>
        public void Place(int face, RigSettings c, Vector3 centre, double rigYawDeg, Camera lens)
        {
            double pitch, yaw;
            double px, py, pz;                     // the camera's offset from the centre, Unreal axes
            if (face >= 4)
            {
                double sign = face == 4 ? 1 : -1;
                px = 0; py = 0; pz = sign * TopDist;
                pitch = -90 * sign; yaw = 90 * sign + rigYawDeg;
            }
            else
            {
                double[,] L = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };
                double a = rigYawDeg * Math.PI / 180, ca = Math.Cos(a), sa = Math.Sin(a);
                double bx = L[face, 0] * ca - L[face, 1] * sa, by = L[face, 0] * sa + L[face, 1] * ca;
                double lx = bx * Ct, ly = by * Ct, lz = -St;
                px = -lx * SideDist[face]; py = -ly * SideDist[face]; pz = -lz * SideDist[face];
                pitch = Math.Asin(lz) * 180 / Math.PI; yaw = Math.Atan2(ly, lx) * 180 / Math.PI;
            }
            double P = pitch * Math.PI / 180, Y = yaw * Math.PI / 180;
            var fwd = new Vector3((float)(Math.Cos(P) * Math.Cos(Y)), (float)(Math.Cos(P) * Math.Sin(Y)), (float)Math.Sin(P));
            var up = new Vector3((float)(-Math.Sin(P) * Math.Cos(Y)), (float)(-Math.Sin(P) * Math.Sin(Y)), (float)Math.Cos(P));

            lens.transform.position = centre + ToUnity(new Vector3((float)px, (float)py, (float)pz));
            lens.transform.rotation = Quaternion.LookRotation(ToUnity(fwd), ToUnity(up));

            int w = c.FaceRes, h = FaceHeight(face, c);
            DepthPlanes(face, out float nearM, out float farM);
            lens.aspect = (float)w / h;
            lens.nearClipPlane = nearM;
            lens.farClipPlane = farM;
            lens.orthographic = !Persp;
            if (Persp)
            {
                float tanV = TanP * h / w;          // side faces are half height; the lens spans the volume across
                lens.fieldOfView = 2f * Mathf.Atan(tanV) * Mathf.Rad2Deg;   // Unity's is vertical
            }
            else
                lens.orthographicSize = (face >= 4 ? R : HalfWin[face]) * h / w;  // half the height: placeFace's width x h/w / 2
        }

        /// <summary>The rig's frame (Unreal axes: X, Y level, Z up) to Unity's (Y up): (x, y, z) to (y, z, x).</summary>
        public static Vector3 ToUnity(Vector3 v) => new Vector3(v.y, v.z, v.x);
    }
}
