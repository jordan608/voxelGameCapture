using System.Numerics;

namespace Voxon.GameKit.Voxelizer;

/// <summary>A single output point: a world position and an RGBA color. This is what the voxelizer
/// produces and what the Voxon renderer draws (via DrawVox_Batch in the host).</summary>
public readonly struct Voxel
{
    public readonly float X, Y, Z;
    public readonly byte R, G, B, A;

    public Voxel(float x, float y, float z, byte r, byte g, byte b, byte a)
    {
        X = x; Y = y; Z = z; R = r; G = g; B = b; A = a;
    }

    public Voxel(Vector3 p, byte r, byte g, byte b, byte a)
        : this(p.X, p.Y, p.Z, r, g, b, a) { }
}
