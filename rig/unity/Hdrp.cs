using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using BepInEx.Logging;
using UnityEngine;
#if !IL2CPP
using System.Linq.Expressions;
#endif

namespace VoxonCaptureRig.Unity
{
    /// <summary>HDRP (a game on Unity's High Definition Render Pipeline): a camera's lit colour and its depth, copied out
    /// for the add-on.
    ///
    /// <para>HDRP draws every camera into buffers of its own and hands its target only the finished, post-processed
    /// picture - never its depth, and there is no setting to draw straight into the target as URP has (Urp). What HDRP
    /// does offer is an AOV request (an "arbitrary output variable"): it copies, each time the camera renders, the outputs
    /// asked for into targets handed to it. So each of the rig's cameras asks for its lit colour (before post-processing:
    /// the add-on tone-maps it) into the face's target, and its depth into a second target of the face's size with a code
    /// of its own, by which the add-on finds it (Cities: Skylines II, 2026-10-22). The camera's own output goes to a
    /// target nobody reads.</para>
    ///
    /// <para>All by reflection, the callbacks HDRP wants made at run time: most Unity games have no HDRP, and then this
    /// does nothing. Mono only; an IL2CPP game with HDRP is not met yet.</para></summary>
    internal sealed class Hdrp
    {
        private const string Hd = "UnityEngine.Rendering.HighDefinition.{0}, Unity.RenderPipelines.HighDefinition.Runtime";
        private const int Color = 1, DepthStencil = 2;     // AOVBuffers

        // FrameSettingsField: what HDRP does for a camera that the volume has no use for, off for the rig's cameras alone -
        // post-processing (the colour is taken before it), motion vectors, volumetric fog and clouds, and the screen-space
        // effects. Shadows, decals, water and refraction stay: they are part of how the world looks.
        private static readonly string[] Off =
        {
            "Postprocess", "MotionVectors", "ObjectMotionVectors", "Volumetrics", "VolumetricClouds", "SSAO", "SSR",
            "TransparentSSR", "SSGI", "ContactShadows", "LowResTransparent",
        };

        private readonly ManualLogSource _log;
        private readonly Type _cameraData, _builder, _request, _buffers, _allocator, _callback, _rtHandles;
        private readonly List<object> _handles = new List<object>();   // RTHandles on the rig's targets
        private bool _said;

        public Hdrp(ManualLogSource log)
        {
            _log = log;
#if !IL2CPP
            _cameraData = Members.TypeOf(string.Format(Hd, "HDAdditionalCameraData"));
            if (_cameraData == null) return;
            _builder = Members.TypeOf(string.Format(Hd, "AOVRequestBuilder"));
            _request = Members.TypeOf(string.Format(Hd, "AOVRequest"));
            _buffers = Members.TypeOf(string.Format(Hd, "AOVBuffers"));
            _allocator = Members.TypeOf(string.Format(Hd, "AOVRequestBufferAllocator"));
            _callback = Members.TypeOf(string.Format(Hd, "FramePassCallback"));
            _rtHandles = Members.TypeOf("UnityEngine.Rendering.RTHandles, Unity.RenderPipelines.Core.Runtime");
#endif
        }

        /// <summary>The game draws with HDRP.</summary>
        public bool On => _cameraData != null;

        /// <summary>A camera of the rig's: HDRP to copy its lit colour into colour and its depth into depth.</summary>
        public void Setup(Camera lens, RenderTexture colour, RenderTexture depth)
        {
#if !IL2CPP
            try
            {
                object data = Members.ComponentOf(lens.gameObject, _cameraData, add: true);
                try { Lighten(data); }
                catch (Exception e) { _log.LogWarning($"HDRP: the cameras keep the game's frame settings: {e.GetBaseException().Message}"); }
                object colourHandle = Alloc(colour), depthHandle = Alloc(depth);
                Type handle = colourHandle.GetType();
                // The target HDRP is to fill for each output asked for.
                ParameterExpression id = Expression.Parameter(_buffers, "id");
                Delegate allocator = Expression.Lambda(_allocator,
                    Expression.Condition(Expression.Equal(Expression.Convert(id, typeof(int)), Expression.Constant(Color)),
                                         Expression.Constant(colourHandle, handle), Expression.Constant(depthHandle, handle)),
                    id).Compile();
                // Called once the outputs are filled: nothing more to do.
                ParameterExpression[] args = _callback.GetMethod("Invoke").GetParameters()
                                                      .Select(p => Expression.Parameter(p.ParameterType, p.Name)).ToArray();
                Delegate done = Expression.Lambda(_callback, Expression.Empty(), args).Compile();
                Array outputs = Array.CreateInstance(_buffers, 2);
                outputs.SetValue(Enum.ToObject(_buffers, Color), 0);
                outputs.SetValue(Enum.ToObject(_buffers, DepthStencil), 1);
                object request = _request.GetMethod("NewDefault", BindingFlags.Public | BindingFlags.Static).Invoke(null, null);
                object builder = Activator.CreateInstance(_builder);
                _builder.GetMethods().First(m => m.Name == "Add" && m.GetParameters().Length == 5)
                        .Invoke(builder, new object[] { request, allocator, null, outputs, done });
                object requests = _builder.GetMethod("Build").Invoke(builder, null);
                _cameraData.GetMethod("SetAOVRequests").Invoke(data, new[] { requests });
                if (!_said)
                {
                    _said = true;
                    _log.LogInfo("HDRP: the cameras' lit colour and depth copied out by HDRP (AOV requests); off for them alone: " +
                                 string.Join(", ", Off));
                }
            }
            catch (Exception e) { _log.LogWarning($"HDRP: could not ask for the cameras' colour and depth: {e.GetBaseException().Message}"); }
#endif
        }

        /// <summary>The camera's frame settings its own, with the Off ones off. HDRP takes a field from a camera's own
        /// settings only where its override mask has that field's bit; the settings and the mask are structs, set back
        /// whole. The settings are public only as a by-reference property, so the field behind it is used.</summary>
        private void Lighten(object data)
        {
            Type field = Members.TypeOf(string.Format(Hd, "FrameSettingsField"));
            FieldInfo settingsField = _cameraData.GetField("m_RenderingPathCustomFrameSettings", BindingFlags.NonPublic | BindingFlags.Instance);
            FieldInfo maskField = _cameraData.GetField("renderingPathCustomFrameSettingsOverrideMask");
            object settings = settingsField.GetValue(data), mask = maskField.GetValue(data);
            FieldInfo bitsField = mask.GetType().GetField("mask");
            object bits = bitsField.GetValue(mask);
            PropertyInfo bit = bits.GetType().GetProperty("Item");
            Type bitIndex = bit.GetIndexParameters()[0].ParameterType;
            MethodInfo setEnabled = settings.GetType().GetMethod("SetEnabled");
            foreach (string name in Off)
            {
                object f;
                try { f = Enum.Parse(field, name); }
                catch (ArgumentException) { continue; }     // not in this HDRP
                setEnabled.Invoke(settings, new[] { f, (object)false });
                bit.SetValue(bits, true, new[] { Convert.ChangeType(Convert.ToInt32(f), bitIndex) });
            }
            bitsField.SetValue(mask, bits);
            settingsField.SetValue(data, settings);
            maskField.SetValue(data, mask);
            Members.Set(data, "customRenderingSettings", true);
        }

        /// <summary>The handles on the rig's targets let go, with the targets.</summary>
        public void Release()
        {
            foreach (object h in _handles) Members.Call(h, "Release", out _);
            _handles.Clear();
        }

        private object Alloc(RenderTexture rt)
        {
            object h = _rtHandles.GetMethod("Alloc", BindingFlags.Public | BindingFlags.Static, null, new[] { typeof(RenderTexture) }, null)
                                 .Invoke(null, new object[] { rt });
            _handles.Add(h);
            return h;
        }
    }
}
