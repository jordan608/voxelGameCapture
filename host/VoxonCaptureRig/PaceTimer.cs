using System.Runtime.InteropServices;

namespace VoxonCaptureRig;

/// <summary>Sleeps for about a millisecond, actually. Thread.Sleep(1) waits for the system timer's next tick - 15.6 ms
/// on Windows unless something has raised the resolution - which would hold a new volume back by most of a frame. A
/// high-resolution waitable timer (Windows 10 1803 and later) wakes on time without changing the timer for any other
/// process; where one cannot be made, this falls back to Thread.Sleep.</summary>
internal sealed class PaceTimer : IDisposable
{
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateWaitableTimerExW(IntPtr attributes, string? name, uint flags, uint access);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetWaitableTimer(IntPtr timer, ref long dueTime, int period, IntPtr completion, IntPtr arg, bool resume);
    [DllImport("kernel32.dll")] private static extern uint WaitForSingleObject(IntPtr handle, uint ms);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);

    private const uint HighResolution = 0x2, TimerAllAccess = 0x1F0003;
    private readonly IntPtr _timer = CreateWaitableTimerExW(IntPtr.Zero, null, HighResolution, TimerAllAccess);

    public void Sleep(double ms)
    {
        long due = -(long)(ms * 10_000);    // relative, in 100 ns units
        if (_timer != IntPtr.Zero && SetWaitableTimer(_timer, ref due, 0, IntPtr.Zero, IntPtr.Zero, false))
            WaitForSingleObject(_timer, 1000);
        else
            Thread.Sleep((int)Math.Ceiling(ms));
    }

    public void Dispose()
    {
        if (_timer != IntPtr.Zero) CloseHandle(_timer);
    }
}
