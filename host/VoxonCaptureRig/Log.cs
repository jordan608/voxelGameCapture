namespace VoxonCaptureRig;

/// <summary>One log for the app: VoxonCaptureRig.log beside the exe, and the last lines kept for the Log tab. The app
/// has no console, so this is where anything worth reading after the fact goes.</summary>
internal static class Log
{
    private static readonly object Gate = new();
    private static readonly Queue<string> Recent = new();
    private static StreamWriter? _file;

    public static event Action<string>? Line;

    public static void Open(string path)
    {
        try { _file = new StreamWriter(path, append: false) { AutoFlush = true }; }
        catch { _file = null; }   // a read-only folder still runs; it just keeps no file
    }

    public static void Write(string message)
    {
        string line = $"{DateTime.Now:HH:mm:ss}  {message}";
        lock (Gate)
        {
            _file?.WriteLine(line);
            Recent.Enqueue(line);
            while (Recent.Count > 500) Recent.Dequeue();
        }
        Line?.Invoke(line);
    }

    public static string[] Snapshot()
    {
        lock (Gate) return Recent.ToArray();
    }
}
