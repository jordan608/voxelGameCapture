using System.Buffers.Binary;
using System.IO.Compression;

namespace VoxonCaptureRig;

/// <summary>BLUEPRINT games (engine "blueprint", Minecraft Dungeons II): the rig in the game is a Blueprint Loader mod
/// (tools/mcdungeons2-mod), and a Blueprint cannot read the control file. So the app works the rig out itself - each
/// camera's place, turn, lens and clip, as rig.lua's geometry() and place() do - and writes it, with the settings the mod
/// acts on, as a small 16-bit PNG in the game's project folder: an image is the one file a Blueprint can read
/// (ImportFileAsTexture2D). The mod reads it twice a second. The add-on reads the control file as it always has, so the
/// two halves decode with the same numbers.
///
/// Every value is 16 bits across its slot's range; slot i is pixel i/4, channel i%4 (red, green, blue, alpha). Faces
/// are placed relative to the player's feet in the rig's own frame, before its yaw: the mod stands at the feet, turned
/// by the rig's yaw (and the view's, turning with the player), and its cameras take these places relative to it.
/// make_mod.py holds the same table: change both together, and LayoutVersion with them.</summary>
internal static class BlueprintBridge
{
    public const string FileName = "voxon_rig.png";
    public const int LayoutVersion = 1;
    public const int Width = 32;                       // four values a pixel: room for 128
    public const ushort Magic = 0xA55A;
    public const float CapsuleHalfCm = 90f;            // rig.lua's: a pawn's location is its capsule's centre

    private static readonly (string Name, float Lo, float Hi)[] Globals =
    [
        ("magic", 0, 65535), ("layout", 0, 65535), ("on", 0, 1), ("hz", 0, 240), ("yawDeg", -720, 720), ("turn", 0, 1),
        ("faceRes", 0, 8192), ("sideH", 0, 8192), ("playerLook", 0, 1), ("surface", 0, 1), ("perspective", 0, 1),
        ("capsuleHalf", 0, 1000),
    ];
    private static readonly (string Name, float Lo, float Hi)[] PerFace =
    [
        ("on", 0, 1), ("x", -60000, 60000), ("y", -60000, 60000), ("z", -60000, 60000), ("pitch", -180, 180),
        ("yaw", -360, 360), ("fov", 0, 180), ("near", 0, 120000), ("reach", 0, 240000), ("orthoWidth", 0, 240000),
    ];

    /// <summary>Every slot's range, in order: the globals, then each face's.</summary>
    public static IEnumerable<(string Name, float Lo, float Hi)> Layout()
    {
        foreach (var g in Globals) yield return g;
        for (int f = 0; f < 6; f++)
            foreach (var p in PerFace) yield return ($"face{f}.{p.Name}", p.Lo, p.Hi);
    }

    /// <summary>The project folder, where the mod looks (UE's ProjectDir): two up from the exe's.</summary>
    public static string PathFor(string exeDir) => Path.GetFullPath(Path.Combine(exeDir, "..", "..", FileName));

    /// <summary>The values, in Layout's order, for these settings on a display of this size.</summary>
    public static float[] Values(RigSettings s, float boundR, float boundZ)
    {
        // rig.lua's geometry(c)
        double sc = s.RadiusM / boundR, R = boundR * sc, Z = boundZ * sc;
        bool persp = s.Perspective;
        double ex = R * (persp ? s.ScaleX : 1), ey = R * (persp ? s.ScaleY : 1), ez = Z * (persp ? s.ScaleZ : 1);
        double t = Math.Clamp(s.TiltDeg, 0, 90) * Math.PI / 180, ct = Math.Cos(t), st = Math.Sin(t);
        int faceRes = s.FaceRes, sideH = 2 * boundZ <= boundR * 1.001 ? faceRes / 2 : faceRes;
        double k = Math.Max(1, s.CamDist);
        double topDist = ez * s.CamDist, nTop = 0, tanP = 0;
        if (persp) { nTop = Math.Max(ex, ey) * k; topDist = ez + nTop; tanP = 1 / k; }
        double fov = 2 * Math.Atan(tanP) * 180 / Math.PI;

        // rig.lua's centre: the feet, less where they stand in the display, then up by feetZ - in the rig's frame
        double dispTurn = (s.TurnWithPlayer ? s.YawDeg - 90 : s.YawDeg) * Math.PI / 180;
        double u = (s.FeetX * Math.Cos(dispTurn) + s.FeetY * Math.Sin(dispTurn)) * (ex / R);
        double w = (-s.FeetX * Math.Sin(dispTurn) + s.FeetY * Math.Cos(dispTurn)) * (ey / R);
        double offX = u * sc * 100, offY = w * sc * 100, rise = s.FeetZ * sc * (ez / Z) * 100;

        var v = new List<float> { Magic, LayoutVersion, 1, 30, s.YawDeg, s.TurnWithPlayer ? 1 : 0, faceRes, sideH,
                                  s.PlayerLook ? 1 : 0, s.Lighting < 0.999f ? 1 : 0, persp ? 1 : 0, CapsuleHalfCm };
        for (int face = 0; face < 6; face++)
        {
            // rig.lua's place(), at yaw 0
            double x, y, z, pitch, yaw, near, reach, ortho;
            if (face >= 4)
            {
                int sign = face == 4 ? 1 : -1;
                (x, y, z) = (0, 0, sign * topDist * 100);
                (pitch, yaw) = (-90.0 * sign, 90.0 * sign);
                near = nTop; reach = (topDist + ez) * 100 * (persp ? 1.5 : 1.05); ortho = 2 * R * 100;
            }
            else
            {
                double along = face % 2 == 0 ? ex : ey, across = face % 2 == 0 ? ey : ex;
                double halfDepth = along * ct + ez * st, vertHalf = along * st + ez * ct;
                double halfWin = Math.Max(across, vertHalf * faceRes / sideH);
                double nSide = 0, sideDist = halfDepth * s.CamDist;
                if (persp) { nSide = halfWin * k; sideDist = halfDepth + nSide; }
                var (lx0, ly0) = face switch { 0 => (-1, 0), 1 => (0, -1), 2 => (1, 0), _ => (0, 1) };
                double lx = lx0 * ct, ly = ly0 * ct, lz = -st, d = sideDist * 100;
                (x, y, z) = (-lx * d, -ly * d, -lz * d);
                (pitch, yaw) = (Math.Asin(lz) * 180 / Math.PI, Math.Atan2(ly, lx) * 180 / Math.PI);
                near = nSide; reach = (sideDist + halfDepth) * 100 * (persp ? 1.5 : 1.05); ortho = 2 * halfWin * 100;
            }
            v.AddRange([s.Faces[face] ? 1 : 0, (float)(x - offX), (float)(y - offY), (float)(z + rise), (float)pitch,
                        (float)yaw, (float)fov, (float)(near * 100 * 0.98), (float)reach, (float)ortho]);
        }
        return [.. v];
    }

    /// <summary>The values as 16-bit samples across their ranges.</summary>
    public static ushort[] Encode(float[] values)
    {
        var layout = Layout().ToArray();
        var raw = new ushort[Width * 4];
        for (int i = 0; i < values.Length; i++)
        {
            var (_, lo, hi) = layout[i];
            raw[i] = (ushort)Math.Clamp(Math.Round((values[i] - lo) / (hi - lo) * 65535.0), 0, 65535);
        }
        return raw;
    }

    /// <summary>16-bit samples back into values (the mod's sum, for the self-test).</summary>
    public static float[] Decode(ushort[] raw)
    {
        var layout = Layout().ToArray();
        var v = new float[layout.Length];
        for (int i = 0; i < v.Length; i++) v[i] = layout[i].Lo + raw[i] / 65535f * (layout[i].Hi - layout[i].Lo);
        return v;
    }

    /// <summary>As the settings change: the file written for a Blueprint game with the rig installed in exeDir, and for
    /// no other - after Remove rig the game's folder stays as it was.</summary>
    public static void Update(GameDef g, RigSettings s, float boundR, float boundZ, string? exeDir)
    {
        if (g.Engine == "blueprint" && exeDir != null && RigInstaller.StateOf(g, exeDir) == RigInstaller.State.Installed)
            Write(s, boundR, boundZ, exeDir);
    }

    /// <summary>Writes the file for the game in exeDir: through a temporary file, so the mod never reads half of one,
    /// and again if the mod has it open just then.</summary>
    public static void Write(RigSettings s, float boundR, float boundZ, string exeDir)
    {
        string path = PathFor(exeDir), tmp = path + ".tmp";
        File.WriteAllBytes(tmp, Png16(Width, 1, Encode(Values(s, boundR, boundZ))));
        for (int attempt = 0; ; attempt++)
        {
            try { File.Move(tmp, path, overwrite: true); return; }
            catch (IOException) when (attempt < 5) { Thread.Sleep(20); }
        }
    }

    // ── A 16-bit RGBA PNG: signature, IHDR, one IDAT (filter 0 rows, zlib), IEND ─────────────────────────────────

    public static byte[] Png16(int width, int height, ushort[] rgba)
    {
        var rows = new byte[height * (1 + width * 8)];
        for (int y = 0, o = 0; y < height; y++)
        {
            rows[o++] = 0;                                     // filter: none
            for (int i = 0; i < width * 4; i++, o += 2) BinaryPrimitives.WriteUInt16BigEndian(rows.AsSpan(o), rgba[(y * width * 4 + i) % rgba.Length]);
        }
        using var z = new MemoryStream();
        using (var zs = new ZLibStream(z, CompressionLevel.Optimal, leaveOpen: true)) zs.Write(rows);
        var ihdr = new byte[13];
        BinaryPrimitives.WriteInt32BigEndian(ihdr, width);
        BinaryPrimitives.WriteInt32BigEndian(ihdr.AsSpan(4), height);
        ihdr[8] = 16; ihdr[9] = 6;                             // 16 bits a channel, RGBA
        using var png = new MemoryStream();
        png.Write([137, 80, 78, 71, 13, 10, 26, 10]);
        Chunk(png, "IHDR", ihdr);
        Chunk(png, "IDAT", z.ToArray());
        Chunk(png, "IEND", []);
        return png.ToArray();
    }

    /// <summary>The 16-bit samples of a PNG Png16 wrote (filter 0 rows only), for the self-test.</summary>
    public static ushort[] ReadPng16(byte[] png)
    {
        int width = BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(16));
        using var idat = new MemoryStream();
        for (int o = 8; o < png.Length;)
        {
            int len = BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(o));
            if (System.Text.Encoding.ASCII.GetString(png, o + 4, 4) == "IDAT") idat.Write(png, o + 8, len);
            o += 12 + len;
        }
        idat.Position = 0;
        using var zs = new ZLibStream(idat, CompressionMode.Decompress);
        using var rows = new MemoryStream();
        zs.CopyTo(rows);
        var b = rows.ToArray();
        var raw = new ushort[width * 4];
        for (int i = 0; i < raw.Length; i++) raw[i] = BinaryPrimitives.ReadUInt16BigEndian(b.AsSpan(1 + i * 2));
        return raw;
    }

    private static void Chunk(Stream s, string type, byte[] data)
    {
        Span<byte> len = stackalloc byte[4];
        BinaryPrimitives.WriteInt32BigEndian(len, data.Length);
        s.Write(len);
        var td = new byte[4 + data.Length];
        System.Text.Encoding.ASCII.GetBytes(type, td);
        data.CopyTo(td, 4);
        s.Write(td);
        BinaryPrimitives.WriteUInt32BigEndian(len, Crc32(td));
        s.Write(len);
    }

    private static uint Crc32(byte[] data)
    {
        uint c = 0xFFFFFFFF;
        foreach (byte b in data)
        {
            c ^= b;
            for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
        }
        return ~c;
    }
}
