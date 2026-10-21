using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Styling;
using Avalonia.Themes.Fluent;

namespace VoxonCaptureRig;

/// <summary>Voxon Capture Rig: installs the camera rig into supported games and shows what it captures on a Voxon
/// display, or the simulator when none is attached.
///
///   VoxonCaptureRig.exe                 the app
///   VoxonCaptureRig.exe --max-vps 0     draw every pass, no cap (the default with hardware; 60 on the simulator)
///   VoxonCaptureRig.exe --no-motor      leave the motor alone (bench testing)
///   VoxonCaptureRig.exe --selftest      the installer and the pipe, against throwaway folders; writes selftest.txt
///
/// THREADS: Avalonia owns the main thread; the Voxon SDK is COM STA and owns a background STA thread of its own
/// (CaptureLoop). They share only AppState. Voxel64.Host settled this split after the SDK corrupted managed stack
/// frames when its init ran on the process's main STA thread.</summary>
internal static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Contains("--selftest")) return SelfTest.Run();
        Log.Open(AppPaths.LogFile);
        var state = new AppState();
        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--max-vps" when i + 1 < args.Length: state.MaxVps = int.Parse(args[++i]); break;
                case "--no-motor": state.Rpm = -1; break;
            }
        }
        App.State = state;
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        return App.ExitCode;
    }

    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>()
                  .UsePlatformDetect()
                  // Software rendering, as Voxel64.Host: the window must not compete with the game for the GPU.
                  .With(new Win32PlatformOptions { RenderingMode = [Win32RenderingMode.Software] })
                  .LogToTrace();
}

internal sealed class App : Application
{
    public static AppState State = new();
    public static int ExitCode;

    public override void Initialize()
    {
        Styles.Add(new FluentTheme());
        RequestedThemeVariant = ThemeVariant.Dark;
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            Avalonia.Threading.Dispatcher.UIThread.UnhandledException += (_, e) =>
            {
                Log.Write($"ui: {e.Exception}");
                e.Handled = true;
            };

            var window = new MainWindow(State);
            desktop.MainWindow = window;
            desktop.ShutdownRequested += (_, _) => State.Closing = true;

            var thread = new Thread(() =>
            {
                try { ExitCode = CaptureLoop.Run(State); }
                catch (Exception ex)
                {
                    Log.Write($"display: {ex}");
                    State.Status = "The display stopped - see the Log tab.";
                    ExitCode = 1;
                }
            })
            {
                IsBackground = true,
                Name = "VoxonDisplay",
                // Above normal so the volume keeps its timing while the window lays out a panel.
                Priority = ThreadPriority.AboveNormal,
            };
            thread.SetApartmentState(ApartmentState.STA);
            thread.Start();
        }
        base.OnFrameworkInitializationCompleted();
    }
}
