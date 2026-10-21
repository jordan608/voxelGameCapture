using System;
using System.Collections.Generic;
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
    /// whose own object stays where it was made.</para></summary>
    internal sealed class Follow
    {
        private const float CheckSeconds = 1f, FindSeconds = 0.5f;

        private readonly ManualLogSource _log;
        private string _key, _said;
        private Type _type;
        private bool _typeTried;
        private object _comp;                           // the followed component, as its own class (Members.Own)
        private Transform[] _points = new Transform[0];
        private float _nextCheck, _nextFind;

        /// <summary>The followed object, or null (Unity's null: also when destroyed).</summary>
        public GameObject Object;

        public Follow(ManualLogSource log) { _log = log; }

        /// <summary>True while there is something to follow: found again when it is gone, and its conditions and points
        /// looked at once a second.</summary>
        public bool Find(RigSettings c, float now)
        {
            string key = $"{c.FollowTag}|{c.FollowComponent}|{c.FollowWhen}|{c.FollowPoint}";
            if (key != _key) { _key = key; Object = null; _type = null; _typeTried = false; }
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
