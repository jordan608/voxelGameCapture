using BepInEx;
using BepInEx.Unity.IL2CPP;

namespace VoxonCaptureRig.Unity
{
    /// <summary>The BepInEx 6 entry point for IL2CPP games: the rig (..\Rig.cs) made a class the game knows, on
    /// BepInEx's own object, which survives level changes, and nothing else.</summary>
    [BepInPlugin("com.voxon.capturerig", "Voxon Capture Rig", "1.0.0")]
    public sealed class Plugin : BasePlugin
    {
        public override void Load()
        {
            AddComponent<Rig>().Log = Log;
            Log.LogInfo("armed: reads voxon_capture_ue.ini beside the game's exe; F7 stops and starts the capture");
        }
    }
}
