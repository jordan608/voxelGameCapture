using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using BepInEx.Logging;
using UnityEngine;
#if IL2CPP
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.InteropTypes;
#endif

namespace VoxonCaptureRig.Unity
{
    /// <summary>What the volume follows, and where its feet are.
    ///
    /// <para>The followed object is the one carrying the profile's tag (followTag; Unity's "Player"), or, for a game
    /// that tags no player, the first object with a component of the game's own class (followComponent, "Namespace.Class,
    /// Assembly") for which the profile's conditions hold (followWhen: members of that class that must be true, "!" before
    /// one that must be false, "&amp;" between). Its renderers are the player's (player look). The feet are the object's
    /// own position, or where the members followPoint names are ("|" between, each a chain of members ending at a
    /// Transform, GameObject or Component): above the middle of them, as low as the lowest - a ragdoll's hips and feet,
    /// whose own object stays where it was made.</para>
    ///
    /// <para>FOLLOW VIEW (followView): for a game watched from a free camera, where nobody is the player (Totally
    /// Accurate Battle Simulator), the feet are the point the game's camera looks at instead: the first thing straight
    /// ahead of it that the game does not move about (the ground, a wall; not a soldier, whose body is a rigidbody), and
    /// ViewDistance is how far away that is, from which the rig takes its scale. Both ease towards each new reading, so
    /// the volume glides with the camera. A game whose world has no colliders to look along (Cities: Skylines II) names
    /// where its camera looks (viewPoint: the point its camera orbits), and the ground plane's height (feetHeight: its
    /// sea level), so the volume stands on the ground wherever the camera is.</para></summary>
    internal sealed class Follow
    {
        private const float CheckSeconds = 1f, FindSeconds = 0.5f, ViewEaseSeconds = 0.2f;

        private readonly ManualLogSource _log;
        private string _key, _said;
        private Type _type;
        private bool _typeTried;
        private object _comp;                           // the followed component, as its own class (Members.Own)
        private Transform[] _points = new Transform[0];
        private float _nextCheck, _nextFind;
        private bool _viewMode, _viewSet;               // FOLLOW VIEW: on; and a point found already
        private Vector3 _view;                          // ...the point the camera looks at, eased
        private float _viewAt;
        private Reading _viewPoint, _feetHeight;        // ...viewPoint and feetHeight, as the game has them

        /// <summary>The followed object, or null (Unity's null: also when destroyed).</summary>
        public GameObject Object;

        /// <summary>FOLLOW VIEW: how far the game's camera is from the point it looks at, eased.</summary>
        public float ViewDistance { get; private set; }

        public Follow(ManualLogSource log) { _log = log; }

        /// <summary>True while there is something to follow: found again when it is gone, and its conditions and points
        /// looked at once a second.</summary>
        public bool Find(RigSettings c, float now)
        {
            string key = $"{c.FollowTag}|{c.FollowComponent}|{c.FollowWhen}|{c.FollowPoint}|{c.FollowView > 0}";
            if (key != _key) { _key = key; Object = null; _type = null; _typeTried = false; _viewSet = false; }
            _viewMode = c.FollowView > 0;
            if (_viewMode) return FindView(c, now);
            if (Object != null)
            {
                if (now < _nextCheck) return true;
                _nextCheck = now + CheckSeconds;
                if (_comp == null || Holds(_comp, c.FollowWhen))
                {
                    _points = Points(c);
                    return true;
                }
                Object = null;                          // no longer the one: another is looked for
            }
            if (now < _nextFind) return false;
            _nextFind = now + FindSeconds;
            _comp = null;
            if (c.FollowComponent.Length > 0) FindByComponent(c);
            else
            {
                try { Object = GameObject.FindWithTag(c.FollowTag); }
                catch (Exception) { Object = null; }   // the tag is not defined in this game
            }
            if (Object == null) return false;
            _points = Points(c);
            _nextCheck = now + CheckSeconds;
            string said = $"following {Object.name} ({(c.FollowComponent.Length > 0 ? c.FollowComponent : "tag " + c.FollowTag)})" +
                          (_points.Length > 0 ? $", feet from {_points.Length} point(s)" : "");
            if (said != _said) { _said = said; _log.LogInfo($"{said} at {Feet()}"); }
            return true;
        }

        /// <summary>The feet: above the middle of the points, as low as the lowest; with none, the object's position.</summary>
        public Vector3 Feet()
        {
            if (_viewMode) return _view;
            int n = 0;
            float x = 0, z = 0, y = float.MaxValue;
            foreach (Transform t in _points)
            {
                if (t == null) continue;
                Vector3 p = t.position;
                x += p.x; z += p.z; y = Math.Min(y, p.y); n++;
            }
            return n > 0 ? new Vector3(x / n, y, z / n) : Object.transform.position;
        }

        /// <summary>FOLLOW VIEW: the game's camera, and the point it looks at, read again every frame.</summary>
        private bool FindView(RigSettings c, float now)
        {
            Camera cam = Camera.main;
            Object = cam != null ? cam.gameObject : null;
            if (Object == null) { _viewSet = false; return false; }
            if (_viewPoint?.Spec != c.ViewPoint.Trim()) _viewPoint = new Reading(c.ViewPoint, _log);
            if (_feetHeight?.Spec != c.FeetHeight.Trim()) _feetHeight = new Reading(c.FeetHeight, _log);
            LookAt(cam, now);
            if (!_viewSet) return false;                // looking at nothing yet
            string said = $"following the view of {Object.name}, the game's camera" +
                          (_viewPoint.Spec.Length > 0 ? $", where {_viewPoint.Spec} says it looks" : "") +
                          (_feetHeight.Spec.Length > 0 ? $", the ground at {_feetHeight.Spec}" : "");
            if (said != _said) { _said = said; _log.LogInfo($"{said}: {_view}, {ViewDistance:0.0} m away"); }
            return true;
        }

        /// <summary>Where the camera looks - the profile's viewPoint as the game has it, else the nearest thing straight
        /// ahead that the game does not move about - and how far that is; the feet on the ground plane when feetHeight
        /// says where it is. With nothing to look at (the sky), the point stays where it was.</summary>
        private void LookAt(Camera cam, float now)
        {
            Vector3 hit;
            if (_viewPoint.Spec.Length > 0 ? !Members.PlaceOf(_viewPoint.Get(now), out hit) : !Ahead(cam, out hit)) return;
            float distance = Vector3.Distance(cam.transform.position, hit);
            if (_feetHeight.Spec.Length > 0)
            {
                if (!Members.Number(_feetHeight.Get(now), out float ground)) return;
                hit.y = ground;
            }
            float ease = _viewSet ? 1f - Mathf.Exp(-Math.Max(0f, now - _viewAt) / ViewEaseSeconds) : 1f;
            _view = Vector3.Lerp(_view, hit, ease);
            ViewDistance = Mathf.Lerp(ViewDistance, distance, ease);
            _viewAt = now;
            _viewSet = true;
        }

        private static bool Ahead(Camera cam, out Vector3 hit)
        {
            float best = float.MaxValue;
            hit = default;
            var hits = Physics.RaycastAll(cam.transform.position, cam.transform.forward, cam.farClipPlane, ~0, QueryTriggerInteraction.Ignore);
            for (int i = 0; i < hits.Length; i++)
            {
                if (hits[i].collider.attachedRigidbody != null || hits[i].distance >= best) continue;
                best = hits[i].distance;
                hit = hits[i].point;
            }
            return best < float.MaxValue;
        }

        private void FindByComponent(RigSettings c)
        {
            if (!_typeTried)
            {
                _typeTried = true;
                _type = Members.TypeOf(c.FollowComponent);
                if (_type == null) _log.LogWarning($"followComponent: no class {c.FollowComponent} in this game");
            }
            if (_type == null) return;
            foreach (object comp in Members.FindAll(_type))
            {
                if (!Holds(comp, c.FollowWhen)) continue;
                _comp = comp;
                Object = ((Component)comp).gameObject;
                return;
            }
        }

        /// <summary>Every condition true ("!": false). A member that is not there, or not a bool, fails.</summary>
        private bool Holds(object comp, string when)
        {
            foreach (string raw in when.Split('&'))
            {
                string name = raw.Trim();
                if (name.Length == 0) continue;
                bool want = !name.StartsWith("!", StringComparison.Ordinal);
                if (!want) name = name.Substring(1).Trim();
                if (!(Members.Get(comp, name) is bool b) || b != want) return false;
            }
            return true;
        }

        private Transform[] Points(RigSettings c)
        {
            var list = new List<Transform>();
            if (c.FollowPoint.Length == 0) return list.ToArray();
            object from = _comp ?? Object;
            foreach (string raw in c.FollowPoint.Split('|'))
            {
                string chain = raw.Trim();
                if (chain.Length == 0) continue;
                Transform t = Members.TransformOf(Members.Get(from, chain));
                if (t != null) list.Add(t);
            }
            return list.ToArray();
        }
    }

    /// <summary>A value read from the game as it plays, as a profile names it: "Class, Assembly: member" (a chain, "a.b")
    /// - the member of a live object of that class, a component or one of the game's Entities systems, looked for again
    /// while there is none - or a plain number.</summary>
    internal sealed class Reading
    {
        public readonly string Spec;
        private readonly ManualLogSource _log;
        private readonly string _class = "", _chain = "";
        private readonly float? _number;
        private Type _type;
        private bool _typeTried;
        private object _from;
        private float _nextFind;

        public Reading(string spec, ManualLogSource log)
        {
            Spec = spec.Trim();
            _log = log;
            if (float.TryParse(Spec, NumberStyles.Float, CultureInfo.InvariantCulture, out float n)) { _number = n; return; }
            int colon = Spec.LastIndexOf(':');
            if (colon < 0) return;
            _class = Spec.Substring(0, colon).Trim();
            _chain = Spec.Substring(colon + 1).Trim();
        }

        /// <summary>The value now, or null while there is nothing to read it from.</summary>
        public object Get(float now)
        {
            if (_number.HasValue) return _number.Value;
            if (_chain.Length == 0) return null;
            if (_from is UnityEngine.Object o && o == null) _from = null;      // destroyed (a new level)
            if (_from == null)
            {
                if (now < _nextFind) return null;
                _nextFind = now + 0.5f;
                if (!_typeTried)
                {
                    _typeTried = true;
                    _type = Members.TypeOf(_class);
                    if (_type == null) _log.LogWarning($"{Spec}: no class {_class} in this game");
                }
                if (_type == null) return null;
                _from = typeof(UnityEngine.Object).IsAssignableFrom(_type) ? Members.FindAll(_type).FirstOrDefault() : Members.EcsSystem(_type);
                if (_from == null) return null;
            }
            return Members.Get(_from, _chain);
        }
    }

    /// <summary>A game's own classes and members, read by reflection: the game's classes under Mono; under IL2CPP,
    /// BepInEx's interop classes for them, where the game's fields are properties.</summary>
    internal static class Members
    {
        private const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;

        /// <summary>A class by its name: "Namespace.Class, Assembly", or "Namespace.Class" in any assembly loaded.</summary>
        public static Type TypeOf(string name)
        {
            Type t = null;
            try { t = Type.GetType(name, false); } catch (Exception) { }
            if (t != null) return t;
            string bare = name.Split(',')[0].Trim();
            foreach (Assembly a in AppDomain.CurrentDomain.GetAssemblies())
            {
                try { t = a.GetType(bare, false); } catch (Exception) { }
                if (t != null) return t;
            }
            return null;
        }

        /// <summary>Every live object with a component of class t, as that class.</summary>
        public static IEnumerable<object> FindAll(Type t)
        {
#if IL2CPP
            foreach (UnityEngine.Object o in UnityEngine.Object.FindObjectsOfType(Il2CppType.From(t)))
                yield return Activator.CreateInstance(t, o.Pointer);
#else
            foreach (UnityEngine.Object o in UnityEngine.Object.FindObjectsOfType(t)) yield return o;
#endif
        }

        /// <summary>An object as its own class, whose members reflection can then see (a component handed over as a
        /// Component, a renderer as its base class). Under Mono it is already.</summary>
        public static object Own(object o)
        {
#if IL2CPP
            if (!(o is Il2CppSystem.Object io)) return o;
            Il2CppSystem.Type it = io.GetIl2CppType();
            Type t = TypeOf($"{it.FullName}, {it.Assembly.GetName().Name}");
            return t != null && t != o.GetType() ? Activator.CreateInstance(t, io.Pointer) : o;
#else
            return o;
#endif
        }

        /// <summary>The class's own name (for a renderer: MeshRenderer, ParticleSystemRenderer...).</summary>
        public static string ClassName(UnityEngine.Object o)
        {
#if IL2CPP
            return o.GetIl2CppType().Name;
#else
            return o.GetType().Name;
#endif
        }

        /// <summary>A chain of members, "a.b.c": at each step a property, a field or a method with no parameters. Null when
        /// one is not there or is null.</summary>
        public static object Get(object o, string chain)
        {
            foreach (string name in chain.Split('.'))
            {
                if (o == null) return null;
                Type t = o.GetType();
                try
                {
                    PropertyInfo p = t.GetProperty(name, Any);
                    if (p != null && p.GetIndexParameters().Length == 0) { o = p.GetValue(o, null); continue; }
                    FieldInfo f = t.GetField(name, Any);
                    if (f != null) { o = f.GetValue(o); continue; }
                    MethodInfo m = t.GetMethod(name, Any, null, Type.EmptyTypes, null);
                    if (m != null) { o = m.Invoke(o, null); continue; }
                }
                catch (Exception) { }
                return null;
            }
            return o;
        }

        /// <summary>A property or field set; an enum's by its number. False when there is no such member to set.</summary>
        public static bool Set(object o, string name, object value)
        {
            if (o == null) return false;
            Type t = o.GetType();
            try
            {
                PropertyInfo p = t.GetProperty(name, Any);
                if (p != null && p.CanWrite) { p.SetValue(o, Fit(p.PropertyType, value), null); return true; }
                FieldInfo f = t.GetField(name, Any);
                if (f != null) { f.SetValue(o, Fit(f.FieldType, value)); return true; }
            }
            catch (Exception) { }
            return false;
        }

        private static object Fit(Type to, object value) => to.IsEnum ? Enum.ToObject(to, value) : value;

        /// <summary>The object's component of class t as that class; with add, added if it has none (else null).</summary>
        public static object ComponentOf(GameObject go, Type t, bool add)
        {
#if IL2CPP
            Component c = go.GetComponent(Il2CppType.From(t));
            if (c == null && add) c = go.AddComponent(Il2CppType.From(t));
            return c != null ? Activator.CreateInstance(t, c.Pointer) : null;
#else
            Component c = go.GetComponent(t);
            if (c == null && add) c = go.AddComponent(t);
            return c;
#endif
        }

        /// <summary>A method called by its name, with these arguments, and what it returned; false when there is none
        /// that takes them.</summary>
        public static bool Call(object o, string name, out object result, params object[] args)
        {
            result = null;
            if (o == null) return false;
            foreach (MethodInfo m in o.GetType().GetMethods(Any))
            {
                if (m.Name != name || m.GetParameters().Length != args.Length) continue;
                try { result = m.Invoke(o, args); return true; }
                catch (Exception) { }
            }
            return false;
        }

        /// <summary>One of the game's Entities systems of class t, in the default world: Unity's Entities keeps its
        /// systems there, not among the scene's objects. Null without Entities (and under IL2CPP, not needed yet).</summary>
        public static object EcsSystem(Type t)
        {
#if IL2CPP
            return null;
#else
            Type world = TypeOf("Unity.Entities.World, Unity.Entities");
            object w = world?.GetProperty("DefaultGameObjectInjectionWorld", BindingFlags.Public | BindingFlags.Static)?.GetValue(null, null);
            if (w == null) return null;
            foreach (string name in new[] { "GetExistingSystemManaged", "GetExistingSystem" })   // Entities 1.x; before
            {
                MethodInfo m = world.GetMethod(name, BindingFlags.Public | BindingFlags.Instance, null, new[] { typeof(Type) }, null);
                if (m == null) continue;
                try { object s = m.Invoke(w, new object[] { t }); if (s != null) return s; }
                catch (Exception) { }
            }
            return null;
#endif
        }

        /// <summary>The place a member's value stands for: a Vector3, anything with x, y and z (Unity.Mathematics'
        /// float3), or a thing's position.</summary>
        public static bool PlaceOf(object v, out Vector3 p)
        {
            p = default;
            if (v == null) return false;
            if (v is Vector3 vec) { p = vec; return true; }
            Transform t = TransformOf(v);
            if (t != null) { p = t.position; return true; }
            if (!Number(Get(v, "x"), out float x) || !Number(Get(v, "y"), out float y) || !Number(Get(v, "z"), out float z)) return false;
            p = new Vector3(x, y, z);
            return true;
        }

        public static bool Number(object v, out float f)
        {
            f = 0;
            if (v == null) return false;
            try { f = Convert.ToSingle(v, CultureInfo.InvariantCulture); return true; }
            catch (Exception) { return false; }
        }

        /// <summary>The transform a member's value stands for: a Transform, a GameObject's or a Component's.</summary>
        public static Transform TransformOf(object v)
        {
            if (v is Transform t) return t;
            if (v is GameObject go) return go.transform;
            if (v is Component c) return c.transform;
#if IL2CPP
            if (v is Il2CppObjectBase o) return o.TryCast<Transform>() ?? o.TryCast<GameObject>()?.transform ?? o.TryCast<Component>()?.transform;
#endif
            return null;
        }
    }
}
