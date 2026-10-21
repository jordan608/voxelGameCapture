using System;
using System.Collections.Generic;
using System.IO;
using BepInEx.Logging;
using UnityEngine;

namespace VoxonCaptureRig.Unity
{
    /// <summary>SEE-THROUGH SURFACES: rig.lua's (rig/unreal) in Unity's terms.
    ///
    /// <para>A material in Unity's transparent queue writes no depth, so a camera lays its colour on whatever is behind
    /// it: a glowing blade on the wall behind it, a glass door nowhere. The scene's renderers are looked at every few
    /// seconds (the new ones), and their see-through materials listed beside the app's control file for the app's list.
    /// For the solid and player cameras, the chosen ones (or all) on each renderer within reach of the volume are swapped
    /// for an opaque stand-in, and put back straight after (Restore). The colour still comes from the face's own camera,
    /// so a blade keeps its glow; only its depth comes from the solid camera.</para></summary>
    internal sealed class SeeThrough
    {
        private const int TransparentQueue = 2500;      // Unity's: above this, the transparent pass, which writes no depth
        private const float ScanSeconds = 2f, ReportSeconds = 2f, CleanSeconds = 5f;
        private const float FarMetres = 100f;           // beyond the volume's half-diagonal, still within reach

        private sealed class Slot { public int Index; public string Path; }
        private sealed class Entry { public Renderer R; public Slot[] Slots; public bool Near; public float NearAt; }
        private sealed class Mat { public string Kind; public readonly HashSet<string> Owners = new HashSet<string>(); public int N; }

        private readonly ManualLogSource _log;
        private readonly Dictionary<int, Entry> _entries = new Dictionary<int, Entry>();   // every renderer looked at, by id
        private readonly Dictionary<string, Mat> _mats = new Dictionary<string, Mat>();
        private readonly Dictionary<int, string> _paths = new Dictionary<int, string>();      // by the material's id
        private readonly HashSet<string> _chosen = new HashSet<string>();
        private readonly List<KeyValuePair<Renderer, Material[]>> _swapped = new List<KeyValuePair<Renderer, Material[]>>();
        private readonly System.Random _random = new System.Random();
        private string _chosenText;
        private float _nextScan, _nextReport, _nextClean;
        private bool _changed, _opaqueTried;
        private Material _opaque;

        /// <summary>The renderers the last MakeSolid drew opaque: what the solid cameras render.</summary>
        public readonly List<Renderer> Solid = new List<Renderer>();

        public SeeThrough(ManualLogSource log) { _log = log; }

        /// <summary>The looking, the tidying and the report: while the app wants the list or anything is chosen.</summary>
        public void Work(RigSettings c, float now)
        {
            if (!c.SeeList && !c.UseSolid) return;
            if (c.SolidMaterials != _chosenText)
            {
                _chosenText = c.SolidMaterials;
                _chosen.Clear();
                foreach (string p in c.SolidMaterials.Split('|')) if (p.Length > 0) _chosen.Add(p);
            }
            if (now >= _nextScan)
            {
                _nextScan = now + ScanSeconds;
                foreach (Renderer r in UnityEngine.Object.FindObjectsOfType<Renderer>())
                    if (!_entries.ContainsKey(r.GetInstanceID())) _entries[r.GetInstanceID()] = Look(r);
            }
            if (now >= _nextClean)
            {
                // Gone renderers let go, and each material's count made again.
                _nextClean = now + CleanSeconds;
                foreach (Mat rec in _mats.Values) rec.N = 0;
                var gone = new List<int>();
                foreach (var kv in _entries)
                {
                    if (kv.Value.R == null) { gone.Add(kv.Key); continue; }
                    foreach (Slot s in kv.Value.Slots) _mats[s.Path].N++;
                }
                foreach (int id in gone) _entries.Remove(id);
                _paths.Clear();
                _changed = true;
            }
            if (_changed && now >= _nextReport)
            {
                _nextReport = now + ReportSeconds;
                _changed = false;
                Report(c);
            }
        }

        /// <summary>One renderer, looked at once: its see-through slots kept, each material counted in the list.</summary>
        private Entry Look(Renderer r)
        {
            var slots = new List<Slot>();
            Material[] ms = r.sharedMaterials;
            for (int i = 0; i < ms.Length; i++)
            {
                Material m = ms[i];
                if (m == null || m.renderQueue <= TransparentQueue) continue;
                string path = PathOf(m);
                slots.Add(new Slot { Index = i, Path = path });
                if (!_mats.TryGetValue(path, out Mat rec))
                {
                    string type = Members.ClassName(r);
                    rec = _mats[path] = new Mat { Kind = type.Contains("Particle") || type.Contains("Trail") || type.Contains("Line") ? "particle" : "mesh" };
                }
                rec.Owners.Add(Tidy(r.transform.root.name));
                rec.N++;
                _changed = true;
            }
            return new Entry { R = r, Slots = slots.ToArray() };
        }

        /// <summary>A material's lasting name, "Shader/Name.Name" (the app shows the name, under its shader). A copy made
        /// while the game runs carries " (Instance)", and goes by the material it was made from: a choice holds for every
        /// copy, and in every session.</summary>
        private string PathOf(Material m)
        {
            if (_paths.TryGetValue(m.GetInstanceID(), out string p)) return p;
            string name = m.name;
            while (name.EndsWith(" (Instance)", StringComparison.Ordinal)) name = name.Substring(0, name.Length - 11);
            p = $"{(m.shader != null ? m.shader.name : "?")}/{name}.{name}";
            _paths[m.GetInstanceID()] = p;
            return p;
        }

        private static string Tidy(string name) => name.Replace("(Clone)", "").Replace(',', ' ').Replace('\t', ' ').Trim();

        private void Report(RigSettings c)
        {
            if (c.ControlFile.Length == 0 || c.HostGame.Length == 0) return;
            var rows = new List<string>();
            foreach (var kv in _mats)
            {
                var owners = new List<string>(kv.Value.Owners);
                owners.Sort(StringComparer.Ordinal);
                rows.Add($"{kv.Key}\t{kv.Value.Kind}\t{string.Join(",", owners.ToArray())}\t{kv.Value.N}");
            }
            rows.Sort(StringComparer.Ordinal);
            string file = Path.Combine(Path.GetDirectoryName(c.ControlFile) ?? ".", $"see_through_{c.HostGame}.txt");
            try
            {
                File.WriteAllText(file, "; see-through materials the rig has found: path, mesh or particle, the objects carrying it, renderers now\n" +
                                        string.Join("\n", rows.ToArray()) + "\n");
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        /// <summary>The chosen see-through materials (or all) swapped for the opaque stand-in on every renderer within
        /// reach of the volume (reachM, its half-diagonal), listed in Solid; Restore puts them back. Where a renderer is,
        /// is asked about once a second, at a time of its own, and FarMetres covers its travel in between.</summary>
        public void MakeSolid(RigSettings c, Vector3 centre, float reachM, float now)
        {
            Solid.Clear();
            _swapped.Clear();
            if (!c.UseSolid) return;
            Material opaque = Opaque();
            if (opaque == null) return;
            float reach = reachM * 1.1f + FarMetres;
            foreach (Entry e in _entries.Values)
            {
                if (e.Slots.Length == 0 || e.R == null) continue;
                bool wanted = c.SolidAll;
                if (!wanted) foreach (Slot s in e.Slots) if (_chosen.Contains(s.Path)) { wanted = true; break; }
                if (!wanted) continue;
                if (now >= e.NearAt)
                {
                    Bounds b = e.R.bounds;
                    e.Near = (b.center - centre).magnitude - b.extents.magnitude <= reach;
                    e.NearAt = now + 1f + (float)_random.NextDouble();
                }
                if (!e.Near || !e.R.enabled || !e.R.gameObject.activeInHierarchy) continue;
                Material[] was = e.R.sharedMaterials;
                var made = (Material[])was.Clone();
                bool any = false;
                foreach (Slot s in e.Slots)
                    if (s.Index < made.Length && (c.SolidAll || _chosen.Contains(s.Path))) { made[s.Index] = opaque; any = true; }
                if (!any) continue;
                e.R.sharedMaterials = made;
                _swapped.Add(new KeyValuePair<Renderer, Material[]>(e.R, was));
                Solid.Add(e.R);
            }
        }

        /// <summary>What MakeSolid changed, put back, last first.</summary>
        public void Restore()
        {
            for (int i = _swapped.Count - 1; i >= 0; i--)
                if (_swapped[i].Key != null) _swapped[i].Key.sharedMaterials = _swapped[i].Value;
            _swapped.Clear();
        }

        /// <summary>The stand-in: a shader every Unity player carries (a shipped game cannot compile one), drawn in the
        /// opaque queue, writing depth, both sides of a thin surface.</summary>
        private Material Opaque()
        {
            if (_opaqueTried) return _opaque;
            _opaqueTried = true;
            foreach (string name in new[] { "Hidden/Internal-Colored", "Standard", "Legacy Shaders/Diffuse", "Unlit/Color" })
            {
                Shader s = Shader.Find(name);
                if (s == null || !s.isSupported) continue;
                _opaque = new Material(s) { name = "VoxonRig opaque", renderQueue = 2000, hideFlags = HideFlags.DontSave };
                if (_opaque.HasProperty("_ZWrite")) _opaque.SetInt("_ZWrite", 1);
                if (_opaque.HasProperty("_Cull")) _opaque.SetInt("_Cull", 0);
                if (_opaque.HasProperty("_Color")) _opaque.SetColor("_Color", Color.white);
                _log.LogInfo($"see-through surfaces: made opaque with {name}");
                return _opaque;
            }
            _log.LogWarning("see-through surfaces: no opaque stand-in shader in this game, so they stay see-through");
            return null;
        }
    }
}
