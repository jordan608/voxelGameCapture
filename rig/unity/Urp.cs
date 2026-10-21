using System;
using System.Collections.Generic;
using BepInEx.Logging;
using UnityEngine;

namespace VoxonCaptureRig.Unity
{
    /// <summary>URP (a game on the Universal Render Pipeline): the rig's cameras drawn straight into their targets, depth
    /// and all.
    ///
    /// <para>URP draws a camera into textures of its own, and copies only the colour across to the camera's target,
    /// whenever the camera wants a depth or opaque texture or post-processing, or its renderer has features (ambient
    /// occlusion, reflections...) and keeps intermediate textures "always" (URP's default). The target's own depth - what
    /// the add-on reads - then stays empty: in Gang Beasts (2026-10-21), whose renderer has three features, every volume
    /// came out with no voxels. So each of the rig's cameras asks for none of those, and while they render (Begin to End)
    /// the renderer's features are off and its intermediate textures only where needed; the player's view keeps them.</para>
    ///
    /// <para>All by reflection: most Unity games have no URP, and then this does nothing.</para></summary>
    internal sealed class Urp
    {
        private const string CameraDataClass = "UnityEngine.Rendering.Universal.UniversalAdditionalCameraData, Unity.RenderPipelines.Universal.Runtime";

        private readonly ManualLogSource _log;
        private Type _cameraData;
        private bool _tried, _said;
        private object _renderer;                       // the renderer the rig's cameras draw with, as its own class
        private readonly List<object> _turnedOff = new List<object>();
        private object _intermediate, _priming;         // the renderer's own settings, kept from Begin to End

        public Urp(ManualLogSource log) { _log = log; }

        /// <summary>A new camera of the rig's: no depth or opaque texture, post-processing or anti-aliasing of URP's, and
        /// the renderer the game's own camera draws with.</summary>
        public void Setup(Camera lens)
        {
            if (!_tried) { _tried = true; _cameraData = Members.TypeOf(CameraDataClass); }
            if (_cameraData == null) return;
            object data = Members.ComponentOf(lens.gameObject, _cameraData, add: true);
            bool ok = Members.Set(data, "requiresDepthOption", 0)          // CameraOverrideOption.Off
                      & Members.Set(data, "requiresColorOption", 0)        // ...the opaque texture: off
                      & Members.Set(data, "renderPostProcessing", false)
                      & Members.Set(data, "antialiasing", 0);              // AntialiasingMode.None
            Camera main = Camera.main;
            object mainData = main != null ? Members.ComponentOf(main.gameObject, _cameraData, add: false) : null;
            object index = Members.Get(mainData, "m_RendererIndex");
            if (index is int i && i >= 0) Members.Call(data, "SetRenderer", out _, i);
            _renderer = Members.Own(Members.Get(data, "scriptableRenderer"));
            if (_said) return;
            _said = true;
            object features = Members.Get(_renderer, "rendererFeatures");
            var names = new List<string>();
            int n = Members.Get(features, "Count") is int c ? c : 0;
            for (int k = 0; k < n; k++)
                if (Members.Call(features, "get_Item", out object f, k) && f is UnityEngine.Object o) names.Add(o.name);
            _log.LogInfo((ok ? "URP: the cameras render straight into their targets" : "URP: could not set the cameras to render straight into their targets") +
                         $"; renderer {_renderer?.GetType().Name ?? "none"}" +
                         $" ({index ?? "the default"}), {Members.Get(_renderer, "m_RenderingMode") ?? "?"}, intermediate textures " +
                         $"{Members.Get(_renderer, "m_IntermediateTextureMode") ?? "?"}, depth priming {Members.Get(_renderer, "m_DepthPrimingMode") ?? "?"}" +
                         $"; its features, off while the rig's cameras render: {(names.Count > 0 ? string.Join(", ", names) : "none")}");
        }

        /// <summary>Before the rig's cameras render: the renderer's features off, its intermediate textures only where
        /// needed (IntermediateTextureMode.Auto) and no depth priming.</summary>
        public void Begin()
        {
            if (_renderer == null) return;
            object features = Members.Get(_renderer, "rendererFeatures");
            int n = Members.Get(features, "Count") is int c ? c : 0;
            for (int k = 0; k < n; k++)
                if (Members.Call(features, "get_Item", out object f, k) && Members.Get(f, "isActive") is bool on && on
                    && Members.Call(f, "SetActive", out _, false))
                    _turnedOff.Add(f);
            _intermediate = Members.Get(_renderer, "m_IntermediateTextureMode");
            if (_intermediate != null) Members.Set(_renderer, "m_IntermediateTextureMode", 0);
            _priming = Members.Get(_renderer, "m_DepthPrimingMode");
            if (_priming != null) Members.Set(_renderer, "m_DepthPrimingMode", 0);
        }

        /// <summary>After them: the renderer as the game had it.</summary>
        public void End()
        {
            foreach (object f in _turnedOff) Members.Call(f, "SetActive", out _, true);
            _turnedOff.Clear();
            if (_intermediate != null) Members.Set(_renderer, "m_IntermediateTextureMode", _intermediate);
            if (_priming != null) Members.Set(_renderer, "m_DepthPrimingMode", _priming);
            _intermediate = _priming = null;
        }
    }
}
