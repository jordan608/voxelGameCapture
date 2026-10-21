using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace VoxonCaptureRig;

/// <summary>The rig's scale (World radius) grown and shrunk from inside the game: while the game's window is in front,
/// holding one key takes in more of the world and the other less. The radius moves steadily while a key is held -
/// doubling or halving every <see cref="SecondsPerDoubling"/> - and eases in and out over a fraction of a second, so the
/// volume never jumps; the rig and the add-on follow it within a frame or two through the control file.</summary>
internal sealed class ScaleKeys
{
    /// <summary>The key pairs offered (grow, shrink), as Windows virtual keys: ones games seldom use.</summary>
    public static readonly (string Name, int Grow, int Shrink)[] Pairs =
    [
        ("Page Up / Page Down", 0x21, 0x22),
        ("Home / End", 0x24, 0x23),
        ("Numpad + / Numpad -", 0x6B, 0x6D),
        ("] / [", 0xDD, 0xDB),
        ("Insert / Delete", 0x2D, 0x2E),
    ];

    public const float MinRadius = 0.5f, MaxRadius = 1000f;   // the rigs' own bounds on it
    private const double SecondsPerDoubling = 1.0, EaseSeconds = 0.15;

    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private double _last, _target, _current;   // log2 of the radius: equal steps are equal ratios
    private bool _moving;
    private int _fgPid = -1;
    private string _fgPath = "";

    /// <summary>One step, about 60 a second. radius: the setting as it stands; gameDir: the game's exe folder, whose
    /// windows the keys work in. Moving: the radius to use now. Settled: the last step of a change, to keep.</summary>
    public (float Radius, bool Moving, bool Settled) Step(float radius, string? gameDir, int pair)
    {
        double now = _clock.Elapsed.TotalSeconds, dt = Math.Min(0.1, now - _last);
        _last = now;
        var (_, growKey, shrinkKey) = Pairs[Math.Clamp(pair, 0, Pairs.Length - 1)];
        bool inGame = gameDir != null
                      && ForegroundPath().StartsWith(gameDir.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase);
        int dir = !inGame ? 0 : (Down(growKey) ? 1 : 0) - (Down(shrinkKey) ? 1 : 0);
        if (!_moving)
        {
            if (dir == 0) return (radius, false, false);
            _target = _current = Math.Log2(Math.Clamp(radius, MinRadius, MaxRadius));   // from where the slider left it
            _moving = true;
        }
        _target = Math.Clamp(_target + dir * dt / SecondsPerDoubling, Math.Log2(MinRadius), Math.Log2(MaxRadius));
        _current += (_target - _current) * (1 - Math.Exp(-dt / EaseSeconds));
        if (dir == 0 && Math.Abs(_target - _current) < 0.0005)
        {
            _moving = false;
            return ((float)Math.Pow(2, _target), false, true);
        }
        return ((float)Math.Pow(2, _current), true, false);
    }

    private static bool Down(int vk) => (GetAsyncKeyState(vk) & 0x8000) != 0;

    /// <summary>The exe of the window in front, looked up again only when that window's process changes.</summary>
    private string ForegroundPath()
    {
        GetWindowThreadProcessId(GetForegroundWindow(), out int pid);
        if (pid == _fgPid) return _fgPath;
        _fgPid = pid;
        _fgPath = "";
        IntPtr h = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, false, pid);
        if (h == IntPtr.Zero) return _fgPath;
        var sb = new StringBuilder(1024);
        int n = sb.Capacity;
        if (QueryFullProcessImageName(h, 0, sb, ref n)) _fgPath = sb.ToString();
        CloseHandle(h);
        return _fgPath;
    }

    [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int vk);
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out int pid);
    [DllImport("kernel32.dll")] private static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern bool QueryFullProcessImageName(IntPtr h, int flags, StringBuilder name, ref int size);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr h);
}
