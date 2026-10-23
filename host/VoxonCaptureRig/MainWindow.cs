using System.Runtime.InteropServices;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Platform.Storage;
using Avalonia.Threading;

namespace VoxonCaptureRig;

/// <summary>What the app remembers between runs: the game last served, and folders the player pointed it at.</summary>
internal sealed class AppConfig
{
    public string? Game { get; set; }
    // The display's volume as the Voxon runtime last reported it - radius and half-height, display units - which the
    // cameras are framed to. A VX2-XL's until a display has run.
    public float BoundR { get; set; } = 4f;
    public float BoundZ { get; set; } = 2f;
    public Dictionary<string, string> Folders { get; set; } = [];
    public int ScaleKeys { get; set; }              // which of ScaleKeys.Pairs grows and shrinks the rig in the game

    private static string PathOf => Path.Combine(AppPaths.Settings, "app.json");

    public static AppConfig Load()
    {
        try { return File.Exists(PathOf) ? JsonSerializer.Deserialize<AppConfig>(File.ReadAllText(PathOf)) ?? new() : new(); }
        catch (Exception) { return new(); }
    }

    public void Save()
    {
        Directory.CreateDirectory(AppPaths.Settings);
        File.WriteAllText(PathOf, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
    }
}

/// <summary>The app's one window: the simulator's view of the volume on the left; on the right, the games (install
/// and remove the rig), the cameras (what the rig captures, live), and the log. Built in code: there are few enough
/// controls that a XAML file would only be another place to look.</summary>
internal sealed class MainWindow : Window
{
    private readonly AppState _s;
    private readonly IReadOnlyList<GameDef> _games;
    private readonly AppConfig _cfg = AppConfig.Load();
    private GameDef? _game;
    private string? _gameDir;                       // the game's exe folder, where the rig-scale keys work
    private RigSettings _rig = new();
    private readonly ScaleKeys _scaleKeys = new();
    private long _scaleWrittenAt;
    private bool _loading;

    private readonly Image _preview = new() { Stretch = Stretch.Uniform };
    private readonly TextBlock _status = new() { Margin = new Thickness(8, 4), Foreground = Brushes.Gainsboro };
    private readonly ListBox _gameList = new();
    private readonly TextBlock _gameTitle = new() { FontSize = 18, FontWeight = FontWeight.SemiBold, TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _gameInfo = new() { TextWrapping = TextWrapping.Wrap, Foreground = Brushes.Silver };
    private readonly TextBlock _gameFolder = new() { TextWrapping = TextWrapping.Wrap, FontSize = 12 };
    private readonly TextBlock _gameState = new() { TextWrapping = TextWrapping.Wrap, FontWeight = FontWeight.SemiBold };
    private readonly Button _browse = new() { Content = "Set folder..." };
    private readonly Button _launch = new() { Content = "Launch game" };
    private readonly Button _install = new() { Content = "Install rig" };
    private readonly Button _remove = new() { Content = "Remove rig" };
    private readonly CheckBox[] _faces = new CheckBox[6];
    private readonly ComboBox _res = new() { ItemsSource = RigSettings.Resolutions.Select(r => $"{r} px").ToArray(), MinWidth = 140 };
    private readonly ComboBox _scaleKeyPick = new() { ItemsSource = ScaleKeys.Pairs.Select(p => p.Name).ToArray(), MinWidth = 200 };
    private readonly CheckBox _persp = new() { Content = "Perspective lenses (keeps the game's lighting)" };
    private readonly CheckBox _showCams = new() { Content = "Show the cameras in the volume (debug)" };
    private readonly CheckBox _turn = new() { Content = "Turn the volume with the player" };
    private readonly CheckBox _playerLook = new() { Content = "Give the player a look of its own (Unreal, Unity, Battlefront II)" };
    private readonly CheckBox _seeList = new() { Content = "List the see-through materials the cameras meet" };
    private readonly CheckBox _solidAll = new() { Content = "Make every see-through surface solid" };
    private readonly StackPanel _seePanel = new() { Spacing = 2 };
    private DateTime _seeStamp;
    private string? _seeGame;
    private readonly TextBox _log = new() { IsReadOnly = true, AcceptsReturn = true, FontFamily = new FontFamily("Consolas,monospace"), FontSize = 12 };
    private readonly List<(Slider Slider, TextBlock Label, string Name, string Format)> _sliders = [];
    private Slider _radius = null!, _camDist = null!, _yaw = null!, _tilt = null!, _feetX = null!, _feetY = null!, _feetZ = null!;
    private Slider _exposure = null!, _lighting = null!, _floor = null!, _ceiling = null!, _expMin = null!, _expMax = null!;
    private Slider _gamma = null!, _saturation = null!, _pExposure = null!, _pGamma = null!, _pSaturation = null!, _pDensity = null!;
    private Slider _scaleX = null!, _scaleY = null!, _scaleZ = null!, _solidOpacity = null!;
    private readonly TextBlock _displayInfo = new() { TextWrapping = TextWrapping.Wrap };
    private int _boundsSeen;

    private WriteableBitmap? _bmp;
    private byte[]? _frameBuf;
    private volatile bool _framePending;
    private readonly DispatcherTimer _save = new() { Interval = TimeSpan.FromMilliseconds(300) };
    private Point? _drag;

    public MainWindow(AppState s)
    {
        _s = s;
        Title = "Voxon Capture Rig";
        Width = 1360; Height = 860;
        Background = new SolidColorBrush(Color.FromRgb(24, 24, 28));
        _games = Games.Load(AppPaths.GamesJson);

        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("*,420") };
        grid.Children.Add(BuildPreview());
        var tabs = new TabControl { Margin = new Thickness(8, 4, 8, 8) };
        tabs.Items.Add(new TabItem { Header = "Games", Content = BuildGamesTab() });
        tabs.Items.Add(new TabItem { Header = "Cameras", Content = BuildCamerasTab() });
        tabs.Items.Add(new TabItem { Header = "Log", Content = _log });
        Grid.SetColumn(tabs, 1);
        grid.Children.Add(tabs);
        Content = grid;

        s.OnPreviewFrame = OnPreviewFrame;
        _save.Tick += (_, _) => { _save.Stop(); SaveRig(); };
        var statusTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        statusTimer.Tick += (_, _) =>
        {
            _status.Text = _s.Status;
            // The display's size, as the runtime reports it: the game told within a second of it changing.
            if (_s.BoundsVersion == _boundsSeen) return;
            _boundsSeen = _s.BoundsVersion;
            if (_cfg.BoundR != _s.BoundR || _cfg.BoundZ != _s.BoundZ)
            {
                _cfg.BoundR = _s.BoundR; _cfg.BoundZ = _s.BoundZ;
                _cfg.Save();
                SaveRig();
            }
            ShowDisplay();
        };
        statusTimer.Start();
        var scaleTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(16) };
        scaleTimer.Tick += (_, _) => StepScale();
        scaleTimer.Start();
        foreach (string line in Log.Snapshot()) _log.Text += line + "\n";
        Log.Line += line => Dispatcher.UIThread.Post(() =>
        {
            string t = (_log.Text ?? "") + line + "\n";
            _log.Text = t.Length > 60_000 ? t[^40_000..] : t;
            _log.CaretIndex = _log.Text.Length;
        });
        Closing += (_, _) => _s.Closing = true;

        RefreshGames();
        int start = Math.Max(0, _games.ToList().FindIndex(g => g.Id == _cfg.Game));
        if (_games.Count > 0) _gameList.SelectedIndex = start;
    }

    // ── The preview ─────────────────────────────────────────────────────────────────────────────────

    private Control BuildPreview()
    {
        var frame = new Border { Background = Brushes.Black, Child = _preview, Cursor = new Cursor(StandardCursorType.SizeAll) };
        ToolTip.SetTip(frame, "Drag to orbit, scroll to zoom, double-click to reset the view");
        frame.PointerPressed += (_, e) => _drag = e.GetPosition(frame);
        frame.PointerReleased += (_, _) => _drag = null;
        frame.PointerMoved += (_, e) =>
        {
            if (_drag is not Point last || !_s.CameraReady) return;
            Point p = e.GetPosition(frame);
            _s.HAng -= (float)(p.X - last.X) * 0.01f;      // drag right, the volume turns right
            _s.VAng = Math.Clamp(_s.VAng - (float)(p.Y - last.Y) * 0.01f, -1.55f, 1.55f);   // drag down, the top tips toward you
            _s.CameraChanged = true;
            _drag = p;
        };
        frame.PointerWheelChanged += (_, e) =>
        {
            if (!_s.CameraReady) return;
            _s.Dist = Math.Clamp(_s.Dist * (e.Delta.Y > 0 ? 0.9f : 1.1f), 0.5f, 50f);
            _s.CameraChanged = true;
        };
        frame.DoubleTapped += (_, _) =>
        {
            if (!_s.CameraReady) return;
            _s.HAng = _s.HomeH; _s.VAng = _s.HomeV; _s.Dist = _s.HomeD;
            _s.CameraChanged = true;
        };
        var dock = new DockPanel();
        DockPanel.SetDock(_status, Dock.Bottom);
        dock.Children.Add(_status);
        dock.Children.Add(frame);
        return dock;
    }

    /// <summary>Called on the display thread with the SDK's picture. Copied, then drawn on the UI thread; a frame
    /// arriving while the last is still being drawn is dropped rather than queued.</summary>
    private void OnPreviewFrame(byte[] buf, int w, int h)
    {
        if (_framePending) return;
        _framePending = true;
        int bytes = w * h * 4;
        if (_frameBuf == null || _frameBuf.Length != bytes) _frameBuf = new byte[bytes];
        Array.Copy(buf, _frameBuf, Math.Min(buf.Length, bytes));
        byte[] snapshot = _frameBuf;
        Dispatcher.UIThread.Post(() =>
        {
            // Opaque, not premultiplied: the SDK writes an alpha channel that, honoured, leaves the picture black.
            if (_bmp == null || _bmp.PixelSize.Width != w || _bmp.PixelSize.Height != h)
                _bmp = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Opaque);
            using (ILockedFramebuffer fb = _bmp.Lock())
                Marshal.Copy(snapshot, 0, fb.Address, Math.Min(snapshot.Length, bytes));
            _preview.Source = _bmp;
            _preview.InvalidateVisual();
            _framePending = false;
        }, DispatcherPriority.Background);
    }

    // ── Games ───────────────────────────────────────────────────────────────────────────────────────

    private Control BuildGamesTab()
    {
        _gameList.SelectionChanged += (_, _) =>
        {
            if (_gameList.SelectedIndex >= 0 && _gameList.SelectedIndex < _games.Count) Select(_games[_gameList.SelectedIndex]);
        };
        _browse.Click += async (_, _) => await BrowseAsync();
        _launch.Click += (_, _) => LaunchGame();
        _install.Click += async (_, _) => await RunAsync(true);
        _remove.Click += async (_, _) => await RunAsync(false);
        var refresh = new Button { Content = "Refresh" };
        refresh.Click += (_, _) => RefreshGames();

        var buttons = new WrapPanel { Orientation = Orientation.Horizontal };
        foreach (var b in new[] { _launch, _install, _remove, _browse, refresh }) { b.Margin = new Thickness(0, 0, 8, 8); buttons.Children.Add(b); }

        var details = new StackPanel { Spacing = 8, Margin = new Thickness(0, 12, 0, 0) };
        details.Children.Add(_gameTitle);
        details.Children.Add(_gameInfo);
        details.Children.Add(_gameFolder);
        details.Children.Add(_gameState);
        details.Children.Add(buttons);
        details.Children.Add(new TextBlock
        {
            TextWrapping = TextWrapping.Wrap, FontSize = 12, Foreground = Brushes.Gray,
            Text = "Install with the game closed. Then Launch game (or start it as you always do) and load into play: " +
                   "the rig arms itself, and F7 stops and starts it. Turn off frame generation (DLSS-G / FSR 3) in the " +
                   "game. Remove rig puts the game's folder back as it was. Set folder... points the app at the game's " +
                   "install folder when it is somewhere the app does not look, and is remembered.",
        });

        var dock = new DockPanel { Margin = new Thickness(4, 8) };
        DockPanel.SetDock(details, Dock.Bottom);
        dock.Children.Add(details);
        dock.Children.Add(new ScrollViewer { Content = _gameList, MaxHeight = 330, VerticalAlignment = VerticalAlignment.Top });
        return new ScrollViewer { Content = dock };
    }

    private void RefreshGames()
    {
        int sel = _gameList.SelectedIndex;
        _gameList.ItemsSource = _games.Select(g => $"{g.Name}  -  {StateText(g)}").ToArray();
        _gameList.SelectedIndex = sel;
        if (_game != null) ShowGame(_game);
    }

    private string? FolderOf(GameDef g) => Games.FindExeDir(g, _cfg.Folders.GetValueOrDefault(g.Id));

    private string StateText(GameDef g) => RigInstaller.StateOf(g, FolderOf(g)) switch
    {
        RigInstaller.State.NotFound => "not found",
        RigInstaller.State.NotInstalled => "ready to install",
        RigInstaller.State.Installed => "rig installed",
        _ => "rig from an older install",
    };

    private void Select(GameDef g)
    {
        _game = g;
        _rig = RigSettings.For(g);
        _cfg.Game = g.Id;
        _cfg.Save();
        ShowGame(g);
        LoadRigControls();
        SaveRig();            // the control file now names this game, so the rig in it takes these settings
    }

    private void ShowGame(GameDef g)
    {
        _gameTitle.Text = g.Name;
        _gameInfo.Text = $"{g.Status}  -  {g.Graphics}\n{g.Notes}";
        string? dir = FolderOf(g);
        _gameDir = dir;
        var state = RigInstaller.StateOf(g, dir);
        bool chosen = dir != null && _cfg.Folders.TryGetValue(g.Id, out string? set) && Games.ExeDirUnder(g, set) == dir;
        _gameFolder.Text = dir == null ? "Not found on this PC. If it is installed, use Set folder... to point at the game's folder."
                                       : $"{dir}\n{(chosen ? "the folder you set" : "found by the app")}";
        (_gameState.Text, _gameState.Foreground) = state switch
        {
            RigInstaller.State.NotFound => ("Game not found", Brushes.Orange),
            RigInstaller.State.NotInstalled => ("Rig not installed", Brushes.Silver),
            RigInstaller.State.Installed => ("Rig installed - this app is driving it", Brushes.LightGreen),
            _ => ("A rig from an older install is here. Install rig takes it over; Remove rig then puts it back.", Brushes.Khaki),
        };
        _install.IsEnabled = _launch.IsEnabled = dir != null;
        _remove.IsEnabled = state is RigInstaller.State.Installed or RigInstaller.State.Foreign;
    }

    private async Task BrowseAsync()
    {
        if (_game is not GameDef g) return;
        var picked = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = $"Find {g.Name}'s install folder",
            AllowMultiple = false,
        });
        string? path = picked.FirstOrDefault()?.TryGetLocalPath();
        if (path == null) return;
        if (Games.ExeDirUnder(g, path) is null)
        {
            _gameState.Text = $"No {g.Name} exe found under {path}. Pick the folder the game is installed in.";
            _gameState.Foreground = Brushes.Orange;
            return;
        }
        _cfg.Folders[g.Id] = path;
        _cfg.Save();
        RefreshGames();
    }

    // Launch game: the game started the way its store starts it (GameLauncher), unless it is running already.
    private void LaunchGame()
    {
        if (_game is not GameDef g || FolderOf(g) is not string dir) return;
        if (RigInstaller.IsRunning(g, dir)) { _gameState.Text = $"{g.Name} is running already."; _gameState.Foreground = Brushes.Silver; return; }
        var (target, how) = GameLauncher.Target(g, dir);
        if (target == null) { _gameState.Text = $"Cannot launch {g.Name}: {how}."; _gameState.Foreground = Brushes.Orange; return; }
        try
        {
            GameLauncher.Start(target, dir);
            Log.Write($"launch: {g.Name} through {how}: {target}");
            _gameState.Text = $"Starting {g.Name} through {how}...";
            _gameState.Foreground = Brushes.Silver;
        }
        catch (Exception ex)
        {
            Log.Write($"launch: FAILED - {ex.Message}");
            _gameState.Text = $"Could not start {g.Name}: {ex.Message}";
            _gameState.Foreground = Brushes.OrangeRed;
        }
    }

    private async Task RunAsync(bool install)
    {
        if (_game is not GameDef g || FolderOf(g) is not string dir) return;
        _install.IsEnabled = _remove.IsEnabled = _browse.IsEnabled = false;
        _gameState.Text = install ? "Installing..." : "Removing...";
        _gameState.Foreground = Brushes.Silver;
        string? error = await Task.Run(() =>
        {
            try
            {
                if (install) RigInstaller.Install(g, dir); else RigInstaller.Remove(g, dir);
                return null;
            }
            catch (Exception ex) { Log.Write($"{(install ? "install" : "remove")}: FAILED - {ex.Message}"); return ex.Message; }
        });
        _browse.IsEnabled = true;
        RefreshGames();
        if (error != null) { _gameState.Text = error; _gameState.Foreground = Brushes.OrangeRed; }
        if (install && error == null) SaveRig();
    }

    // ── Cameras ─────────────────────────────────────────────────────────────────────────────────────

    private Control BuildCamerasTab()
    {
        var panel = new StackPanel { Spacing = 10, Margin = new Thickness(4, 8) };
        panel.Children.Add(Header("Display"));
        ShowDisplay();
        panel.Children.Add(_displayInfo);
        panel.Children.Add(Note("The volume is shaped for the display the Voxon runtime is driving, as it reports its size: " +
                                "change the display (or the simulator's model) and the cameras are framed to it within a " +
                                "second. A display at least twice as wide as tall gets side pictures half as tall as wide."));

        panel.Children.Add(Header("Cameras"));
        var faces = new UniformGrid { Columns = 2 };
        for (int i = 0; i < 6; i++)
        {
            int face = i;
            _faces[i] = new CheckBox { Content = RigSettings.FaceNames[i] };
            _faces[i].IsCheckedChanged += (_, _) => { if (!_loading) { _rig.Faces[face] = _faces[face].IsChecked == true; Changed(); } };
            faces.Children.Add(_faces[i]);
        }
        panel.Children.Add(faces);

        panel.Children.Add(Header("Resolution, each camera"));
        _res.SelectionChanged += (_, _) =>
        {
            if (_loading || _res.SelectedIndex < 0) return;
            _rig.FaceRes = RigSettings.Resolutions[_res.SelectedIndex];
            Changed();
        };
        panel.Children.Add(_res);
        panel.Children.Add(Note("Every camera is a full render of the game. 1024 px is the working ceiling for six on an RTX 3060 Ti; " +
                                "past it a VX2-XL gains little, because most extra pixels land in display cells already filled."));

        panel.Children.Add(Header("Framing"));
        _radius = AddSlider(panel, "World radius", 1, 60, 0.5, "0.0 m", v => _rig.RadiusM = (float)v);
        panel.Children.Add(new TextBlock { Text = "Rig scale keys, in the game" });
        _scaleKeyPick.SelectedIndex = Math.Clamp(_cfg.ScaleKeys, 0, ScaleKeys.Pairs.Length - 1);
        _scaleKeyPick.SelectionChanged += (_, _) =>
        {
            if (_scaleKeyPick.SelectedIndex < 0) return;
            _cfg.ScaleKeys = _scaleKeyPick.SelectedIndex;
            _cfg.Save();
        };
        panel.Children.Add(_scaleKeyPick);
        panel.Children.Add(Note("While playing, hold the first key to take in more of the world and the second to take in " +
                                "less: World radius doubles or halves about every second, easing in and out, and the volume " +
                                "follows it smoothly. The slider follows too, and the scale is kept for next time."));
        _camDist = AddSlider(panel, "Camera distance", 1, 8, 0.1, "0.0x", v => _rig.CamDist = (float)v);
        _yaw = AddSlider(panel, "Rig rotation", -180, 180, 1, "0 deg", v => _rig.YawDeg = (float)v);
        _tilt = AddSlider(panel, "Side camera tilt", 0, 60, 1, "0 deg", v => _rig.TiltDeg = (float)v);
        _scaleX = AddSlider(panel, "World X", 0.25, 4, 0.05, "0.00x", v => _rig.ScaleX = (float)v);
        _scaleY = AddSlider(panel, "World Y", 0.25, 4, 0.05, "0.00x", v => _rig.ScaleY = (float)v);
        _scaleZ = AddSlider(panel, "World height", 0.25, 4, 0.05, "0.00x", v => _rig.ScaleZ = (float)v);
        panel.Children.Add(Note("How much world each axis holds, as a multiple of World radius. Above 1 more is squashed into the " +
                                "display along it, below 1 less is stretched: a fighting game's stage, deep behind the fighters, " +
                                "fits at World X 2-3. X and Y are the game world's at Rig rotation 0. Perspective lenses only."));

        panel.Children.Add(Header("Look"));
        _lighting = AddSlider(panel, "Lighting", 0, 100, 5, "0'%'", v => _rig.Lighting = (float)(v / 100));
        panel.Children.Add(Note("How much of the game's lighting the volume shows. 100% is the game's light and shadow as the " +
                                "cameras see it. Below that each camera also captures the surfaces' own colours and the light " +
                                "is mixed over them: 0% is the surface colours alone, evenly lit, with nothing lost in shadow " +
                                "or glare. Below 100% every camera renders twice, the second time without lighting."));
        // Wide: a physically lit game hands over light values thousands of times brighter than a picture's (Cities:
        // Skylines II's HDRP: about -13 stops), others far dimmer.
        _expMin = AddSlider(panel, "Auto-exposure: lowest", -24, 16, 0.25, "+0.00;-0.00;0", v =>
        {
            _rig.ExposureMin = (float)v;
            if (_expMax.Value < v) _expMax.Value = v;
        });
        _expMax = AddSlider(panel, "Auto-exposure: highest", -24, 16, 0.25, "+0.00;-0.00;0", v =>
        {
            _rig.ExposureMax = (float)v;
            if (_expMin.Value > v) _expMin.Value = v;
        });
        panel.Children.Add(Note("The automatic exposure brings the volume's average to middle grey, but only within these, in " +
                                "stops from the picture as the cameras took it: lowest is the most it may darken, highest the most " +
                                "it may brighten. Set both to 0 to turn it off. Each stop doubles the light."));
        _exposure = AddSlider(panel, "Brightness", -4, 6, 0.25, "+0.00;-0.00;0", v => _rig.Exposure = (float)v);
        panel.Children.Add(Note("Stops of exposure on top of the automatic exposure; it scales the surface colours too."));
        _gamma = AddSlider(panel, "Gamma", 0.4, 2.5, 0.05, "0.00", v => _rig.Gamma = (float)v);
        _saturation = AddSlider(panel, "Saturation", 0, 2, 0.05, "0.00", v => _rig.Saturation = (float)v);
        panel.Children.Add(Note("Gamma above 1 lifts the dark and middle tones without blowing out the bright ones; below 1 " +
                                "deepens them. Saturation 0 is grey, 1 the colours as captured, 2 twice as vivid."));
        _floor = AddSlider(panel, "Colour range: floor", 0, 255, 1, "0", v => _rig.ColorFloor = (int)v);
        _ceiling = AddSlider(panel, "Colour range: ceiling", 0, 255, 1, "0", v => _rig.ColorCeiling = (int)v);
        panel.Children.Add(Note("Every voxel's brightness, 0-255, is drawn between these instead, and its colour kept. The " +
                                "display's bright end saturates well before 255, so highlights all come out the same white; its " +
                                "dark end disappears into the room. Brightness is a multiply and cannot move both ends; this " +
                                "moves them apart. 0 and 255 leave it alone; try 80 and 220."));

        panel.Children.Add(Header("Player look"));
        _playerLook.IsCheckedChanged += (_, _) => { if (!_loading) { _rig.PlayerLook = _playerLook.IsChecked == true; Changed(); } };
        panel.Children.Add(_playerLook);
        panel.Children.Add(Note("On, each camera also captures the player on their own, so the player can be brighter, more " +
                                "vivid or denser than the world around them. It costs frame rate: every camera renders once " +
                                "more. Unreal and Unity games. In Star Wars Battlefront II there is no second render: the " +
                                "player is what stands within about 60 cm of their feet, so a wall or a droid that " +
                                "close takes the player's look too."));
        _pExposure = AddSlider(panel, "Player brightness", -4, 6, 0.25, "+0.00;-0.00;0", v => _rig.PlayerExposure = (float)v);
        _pGamma = AddSlider(panel, "Player gamma", 0.4, 2.5, 0.05, "0.00", v => _rig.PlayerGamma = (float)v);
        _pSaturation = AddSlider(panel, "Player saturation", 0, 2, 0.05, "0.00", v => _rig.PlayerSaturation = (float)v);
        _pDensity = AddSlider(panel, "Player density (voxels deep)", 1, 4, 1, "0", v => _rig.PlayerDensity = (int)v);
        panel.Children.Add(Note("Brightness is stops on top of the world's. Density draws the player's surface that many " +
                                "voxels deep, so they look more solid and brighter on the display; 1 is a shell, as the world."));

        panel.Children.Add(Header("See-through surfaces"));
        panel.Children.Add(Note("Glass, holograms, lightsaber blades and blaster bolts are see-through to the game, and leave no " +
                                "depth: the cameras put their colour on whatever is behind them, or nowhere. Made solid, they " +
                                "stand where they are, in their own colours. Tick the ones you want solid: while any is near, " +
                                "each camera renders once more. Every one at once also makes fog, smoke and outlines solid. " +
                                "Unreal and Unity games."));
        _seeList.IsCheckedChanged += (_, _) => { if (!_loading) { _rig.SeeList = _seeList.IsChecked == true; Changed(); } };
        _solidAll.IsCheckedChanged += (_, _) => { if (!_loading) { _rig.SolidAll = _solidAll.IsChecked == true; Changed(); } };
        panel.Children.Add(_seeList);
        panel.Children.Add(_solidAll);
        _solidOpacity = AddSlider(panel, "Solid where at least this opaque", 0, 100, 5, "0'%'", v => _rig.SolidOpacity = (float)(v / 100));
        panel.Children.Add(Note("A lightsaber's mesh is wider than its glowing core, which the game fades out toward the mesh's " +
                                "edge: at 0% the whole mesh is made solid, a flat card; at 50% the core. Above 0% each camera " +
                                "renders once more while a ticked surface is near, to see how opaque it is. Unreal games."));
        panel.Children.Add(_seePanel);
        var seeTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        seeTimer.Tick += (_, _) => RefreshSeeThrough(false);
        seeTimer.Start();

        panel.Children.Add(Header("Player"));
        _turn.IsCheckedChanged += (_, _) => { if (!_loading) { _rig.TurnWithPlayer = _turn.IsChecked == true; Changed(); } };
        panel.Children.Add(_turn);
        panel.Children.Add(Note("On, the volume turns as the player looks around: what is ahead on the screen is always toward " +
                                "the back of the display. Off, the world keeps its own orientation and the player turns in it."));
        _feetX = AddSlider(panel, "Feet x (left - right)", -4, 4, 0.1, "0.0", v => _rig.FeetX = (float)v);
        _feetY = AddSlider(panel, "Feet y (back - front)", -4, 4, 0.1, "0.0", v => _rig.FeetY = (float)v);
        _feetZ = AddSlider(panel, "Feet z (top - bottom)", -2, 2, 0.1, "0.0", v => _rig.FeetZ = (float)v);
        panel.Children.Add(Note("Where the player's feet stand in the display, in display units: a VX2-XL is -4 to 4 across and " +
                                "-2 (top) to 2 (bottom) high. With the volume turning, a feet y toward the front leaves more of " +
                                "the volume for what is ahead."));

        panel.Children.Add(Header("Options"));
        _persp.IsCheckedChanged += (_, _) => { if (!_loading) { _rig.Perspective = _persp.IsChecked == true; Changed(); } };
        _showCams.IsCheckedChanged += (_, _) => { if (!_loading) { _rig.ShowCameras = _showCams.IsChecked == true; Changed(); } };
        panel.Children.Add(_persp);
        panel.Children.Add(_showCams);
        var reset = new Button { Content = "Reset to this game's defaults", Margin = new Thickness(0, 8, 0, 0) };
        reset.Click += (_, _) =>
        {
            if (_game == null) return;
            File.Delete(Path.Combine(AppPaths.Settings, _game.Id + ".json"));
            _rig = RigSettings.For(_game);
            LoadRigControls();
            SaveRig();
        };
        panel.Children.Add(reset);
        panel.Children.Add(Note("Changes reach the game within a second."));
        return new ScrollViewer { Content = panel };
    }

    /// <summary>The see-through materials' list: the rig's report for this game, and any chosen that it has not met yet,
    /// each a tick box - ticked, made solid. Built again when the report changes or the game does.</summary>
    private void RefreshSeeThrough(bool force)
    {
        if (_game == null) return;
        string file = SeeThroughMaterial.FileFor(_game);
        DateTime stamp = File.Exists(file) ? File.GetLastWriteTimeUtc(file) : DateTime.MinValue;
        if (!force && stamp == _seeStamp && _seeGame == _game.Id) return;
        _seeStamp = stamp;
        _seeGame = _game.Id;
        var found = SeeThroughMaterial.Read(_game);
        foreach (string p in _rig.SolidMaterials)
            if (!found.Any(m => m.Path == p)) found.Add(new SeeThroughMaterial(p, "not met yet", "", 0));
        _seePanel.Children.Clear();
        if (found.Count == 0)
        {
            _seePanel.Children.Add(Note(_rig.SeeList ? "None met yet: play on, and the list fills as the cameras meet them."
                                                     : "Tick the box above to list them while you play."));
            return;
        }
        foreach (var m in found.OrderBy(m => m.Name, StringComparer.OrdinalIgnoreCase))
        {
            string what = m.Kind == "not met yet" ? m.Kind
                        : $"{m.Kind}, {(m.Owners.Length > 60 ? m.Owners[..57] + "..." : m.Owners)}  ({m.Count})";
            var box = new CheckBox
            {
                IsChecked = _rig.SolidMaterials.Contains(m.Path),
                Content = new TextBlock
                {
                    TextWrapping = TextWrapping.Wrap,
                    Inlines = [new Run(m.Name), new Run("   " + what) { Foreground = Brushes.Gray, FontSize = 12 }],
                },
            };
            ToolTip.SetTip(box, m.Path);
            string path = m.Path;
            box.IsCheckedChanged += (_, _) =>
            {
                if (_loading) return;
                _rig.SolidMaterials.Remove(path);
                if (box.IsChecked == true) _rig.SolidMaterials.Add(path);
                Changed();
            };
            _seePanel.Children.Add(box);
        }
    }

    /// <summary>The display's size: as the runtime reports it, or the last it reported while none is running.</summary>
    private void ShowDisplay()
    {
        float r = _cfg.BoundR, z = _cfg.BoundZ;
        _displayInfo.Text = $"{2 * r:0.##} x {2 * r:0.##} x {2 * z:0.##} (x, y and z run {-r:0.##} to {r:0.##}, {-r:0.##} to {r:0.##} " +
                            $"and {-z:0.##} to {z:0.##})" + (_s.BoundR > 0.1f ? "" : ", as the display last reported");
    }

    private Slider AddSlider(Panel panel, string name, double min, double max, double step, string format, Action<double> set)
    {
        var label = new TextBlock();
        var slider = new Slider { Minimum = min, Maximum = max, SmallChange = step, LargeChange = step * 10,
                                  TickFrequency = step, IsSnapToTickEnabled = true };
        slider.ValueChanged += (_, e) =>
        {
            label.Text = $"{name}: {e.NewValue.ToString(format)}";
            if (_loading) return;
            set(e.NewValue);
            Changed();
        };
        _sliders.Add((slider, label, name, format));
        panel.Children.Add(label);
        panel.Children.Add(slider);
        return slider;
    }

    private void LoadRigControls()
    {
        _loading = true;
        for (int i = 0; i < 6; i++) _faces[i].IsChecked = _rig.Faces[i];
        int r = Array.IndexOf(RigSettings.Resolutions, _rig.FaceRes);
        _res.SelectedIndex = r >= 0 ? r : Array.IndexOf(RigSettings.Resolutions, 512);
        _radius.Value = _rig.RadiusM;
        _camDist.Value = _rig.CamDist;
        _yaw.Value = _rig.YawDeg;
        _tilt.Value = _rig.TiltDeg;
        _feetX.Value = _rig.FeetX;
        _feetY.Value = _rig.FeetY;
        _feetZ.Value = _rig.FeetZ;
        _turn.IsChecked = _rig.TurnWithPlayer;
        _exposure.Value = _rig.Exposure;
        _expMin.Value = float.IsNaN(_rig.ExposureMin) ? -10 : _rig.ExposureMin;
        _expMax.Value = float.IsNaN(_rig.ExposureMax) ? 8 : _rig.ExposureMax;
        _lighting.Value = Math.Round(_rig.Lighting * 100);
        _scaleX.Value = _rig.ScaleX;
        _scaleY.Value = _rig.ScaleY;
        _scaleZ.Value = _rig.ScaleZ;
        _floor.Value = _rig.ColorFloor;
        _ceiling.Value = _rig.ColorCeiling;
        _gamma.Value = _rig.Gamma;
        _saturation.Value = _rig.Saturation;
        _playerLook.IsChecked = _rig.PlayerLook;
        _pExposure.Value = _rig.PlayerExposure;
        _pGamma.Value = _rig.PlayerGamma;
        _pSaturation.Value = _rig.PlayerSaturation;
        _pDensity.Value = Math.Clamp(_rig.PlayerDensity, 1, 4);
        _seeList.IsChecked = _rig.SeeList;
        _solidAll.IsChecked = _rig.SolidAll;
        _solidOpacity.Value = Math.Round((float.IsNaN(_rig.SolidOpacity) ? 0f : _rig.SolidOpacity) * 100);
        RefreshSeeThrough(true);
        _persp.IsChecked = _rig.Perspective;
        _showCams.IsChecked = _rig.ShowCameras;
        foreach (var (s, l, n, f) in _sliders) l.Text = $"{n}: {s.Value.ToString(f)}";
        _loading = false;
    }

    /// <summary>The rig-scale keys, about 60 times a second: while one is held in the game, the radius moves and the
    /// control file follows it (up to 30 times a second); once it settles, it is saved.</summary>
    private void StepScale()
    {
        if (_game == null) return;
        var (radius, moving, settled) = _scaleKeys.Step(_rig.RadiusM, _gameDir, _cfg.ScaleKeys);
        if (!moving && !settled) return;
        _rig.RadiusM = radius;
        _loading = true;
        _radius.Value = Math.Clamp(radius, _radius.Minimum, _radius.Maximum);
        _loading = false;
        var label = _sliders.First(s => s.Slider == _radius).Label;
        label.Text = $"World radius: {radius:0.00} m";
        if (settled) { SaveRig(); return; }
        long now = Environment.TickCount64;
        if (now - _scaleWrittenAt < 30) return;
        _scaleWrittenAt = now;
        try { _rig.WriteControlFile(_game, _cfg.BoundR, _cfg.BoundZ, _gameDir); }
        catch (IOException) { }            // a rig was reading it just then: the next step writes it
        catch (UnauthorizedAccessException) { }
    }

    private void Changed()
    {
        _save.Stop();
        _save.Start();
    }

    private void SaveRig()
    {
        if (_game == null) return;
        try
        {
            _rig.Save(_game);
            _rig.WriteControlFile(_game, _cfg.BoundR, _cfg.BoundZ, _gameDir);
        }
        catch (Exception ex) { Log.Write($"settings: could not save ({ex.Message})"); }
    }

    private static TextBlock Header(string text) =>
        new() { Text = text, FontSize = 15, FontWeight = FontWeight.SemiBold, Margin = new Thickness(0, 8, 0, 0) };

    private static TextBlock Note(string text) =>
        new() { Text = text, TextWrapping = TextWrapping.Wrap, FontSize = 12, Foreground = Brushes.Gray };
}
