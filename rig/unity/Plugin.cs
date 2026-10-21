using BepInEx;
using UnityEngine;

namespace VoxonCaptureRig.Unity
{
    /// <summary>The BepInEx entry point. It puts the rig on an object of its own, which survives level changes, and
    /// nothing else.</summary>
    [BepInPlugin("com.voxon.capturerig", "Voxon Capture Rig", "1.0.0")]
    public sealed class Plugin : BaseUnityPlugin
    {
        private void Awake()
        {
            var host = new GameObject("VoxonCaptureRig");
            DontDestroyOnLoad(host);
            host.hideFlags = HideFlags.HideAndDontSave;
            host.AddComponent<Rig>().Log = Logger;
            Logger.LogInfo("armed: reads voxon_capture_ue.ini beside the game's exe; F7 stops and starts the capture");
        }
    }
}
