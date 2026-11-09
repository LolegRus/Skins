using System.Text.RegularExpressions;
using UnityEngine;
using Object = UnityEngine.Object;

namespace Skins
{
    /// <summary>
    /// Голограмма на пьедестале терминала: класс в выбранном скине («Review Skin»), медленно крутится на 360°
    /// и чуть покачивается, а из линзы пьедестала бьёт луч проекции (Projection).
    /// Модель — «манекен»: из префаба класса игры копируются только узлы, меши и материалы (без скриптов,
    /// сети, коллайдеров, частиц), на копию скин надевается тем же AppliedSkin, что и на игрока.
    /// Рисуются только части тела — меши с материалом тела игры (шейдер Dust/Grain): щиты, огонь, тросы,
    /// иконки оружия — другие шейдеры, их нет. Интерфейс на руках, меши вспышки урона и шляпа — тоже нет.
    /// Одна поза у всех, размер — как в игре (один масштаб): ось вращения — через «ядро» (голова и тело, без рук и того,
    /// что в руках); кисти (узлы с PoseInterpolator) смотрят наружу и стоят
    /// у боков тела на высоте плеч, на одном расстоянии от него.
    /// </summary>
    internal sealed class Hologram
    {
        // Верх пьедестала в осях модели терминала (до масштаба): центр по X/Z, высота крышки
        private static readonly Vector3 Top = new Vector3(-1.545f, 0.739f, 0f);
        private const float GameScale = 0.65f;            // доля размера в игре (в осях терминала, он сам ×1.3)
        // Поправка на глаз поверх размера из игры (после пределов)
        private static readonly Dictionary<string, float> ClassSize = new(StringComparer.OrdinalIgnoreCase)
        {
            ["SERAPH"] = 1.3f, ["KOBOLD"] = 1.3f, ["GOLEM"] = 1.3f, ["ASSASSIN"] = 1.3f,
            ["BOULDER"] = 0.75f, ["ROLLER"] = 0.75f,
        };
        private const float CoreSize = 0.5f;              // запасной размер, если у модели нет мешей
        private const float ShoulderAt = 0.62f;           // кисти — на этой доле высоты ядра
        private const float HandGap = 0.1f;               // от бока тела до кисти — доля высоты ядра
        private const float MaxRestShift = 0.35f;         // поза покоя дальше этого от префаба — не верим ей (крюк Grappler)
        private const float MaxHeight = 1.0f, MaxWidth = 1.3f, MaxDepth = 0.9f, Lift = 0.05f;   // предел для необычных
        private const float SpinSpeed = 40f;              // °/с — оборот за 9 с
        private const float BobHeight = 0.015f, BobSpeed = 1.6f;

        // Узлы, которые не копируются вместе с детьми
        private static readonly Regex JunkNode = new(@"^(Menu Icon Anchor|Crit Display|Weapon UI|Health UI|Slam Attack|AoE Damage Visual Radius)$");
        // Меши, которые не рисуются (узел и MeshFilter остаются — скин может на них сослаться)
        private static readonly Regex JunkMesh = new(@"(Damage Mesh|Damage Flash|\bDM\b|^Witch Hat$)");
        private const string BodyShader = "Dust/Grain";
        // Руки: в T-позе смотрят наружу. У Roller на «руках» пушки с поворотом в дочернем узле — их только двигаем
        private static readonly Regex HandNode = new(@"^(Left|Right) (Hand|Arm)( Anchor)?$");
        private static readonly HashSet<string> KeepHands = new(StringComparer.OrdinalIgnoreCase) { "ROLLER" };
        // Не ядро, хотя и не кисти: оружие Seraph в «руках»
        private static readonly Regex HeldNode = new(@"^(Offhand|Railgun) Pose$");
        // Брошенное классом (колесо Crossbolt, диски телепорта и мин) в префабе лежит рядом с телом: узлы
        // копируются (скин может на них сослаться), но прячутся
        private static readonly Regex ThrownNode = new(@"\(Projectile\)|Teleport Disk|Mine Disk|Teleport Trail");

        private readonly Transform _pivot;
        private readonly Projection _fx;
        private readonly HashSet<string> _logged = new();
        private GameObject _model;
        private AppliedSkin _skin;
        private string _class, _skinName;
        private float _time;

        internal Hologram(Transform terminal)
        {
            var go = new GameObject("Skins Hologram");
            _pivot = go.transform;
            _pivot.SetParent(terminal, false);
            _pivot.localPosition = Top;
            _fx = new Projection(terminal);
        }

        internal void Show(string classKey, string skinName)
        {
            Clear();
            if (classKey != _class || skinName != _skinName) _retried = false;   // другой скин — первая пересборка снова сразу
            _broken = false;
            _class = classKey;
            _skinName = skinName;

            GameObject prefab = Game.FindPrefab(classKey);
            if (prefab == null) { Core.Log.Warning($"Голограмма: нет префаба класса {classKey}."); return; }
            Skin skin = SkinLibrary.For(classKey).Find(s => s.Name == skinName);

            // Размер считаем, пока голограмма стоит ровно (без поворота и покачивания)
            _pivot.localRotation = Quaternion.identity;
            _pivot.localPosition = Top;

            var copy = new CopyState { TPose = !KeepHands.Contains(classKey) };
            copy.Space = _pivot;
            copy.ClassKey = classKey;
            copy.RootScale = Mathf.Max(prefab.transform.localScale.x, 0.01f);
            _model = Copy(prefab.transform, _pivot, copy);
            int posed = RestPoses(prefab, copy, out int distrusted);
            _model.name = "Mannequin " + Core.Title(classKey);
            _model.transform.localPosition = Vector3.zero;
            _model.transform.localRotation = Quaternion.identity;
            _model.transform.localScale = Vector3.one;

            // Размер и центр — по оригиналу класса, скин надевается потом: голограммы одного класса одного размера
            (float height, float radius) = Arrange(copy);
            if (skin != null) _skin = new AppliedSkin(skin, _model.transform, "голограмма");
            _fx.Show(height + (Top.y - Projection.Lens.y), radius);

            // Что отброшено и какие руки повёрнуты — один раз на класс: по этому видно, не пропало ли нужное
            if (_logged.Add(classKey))
                Core.Log.Msg($"Голограмма {Core.Title(classKey)}: мешей {copy.Meshes}, деталей в позе покоя {posed} (не поверил {distrusted}), руки в T-позу: " +
                             $"{(copy.Hands.Count > 0 ? string.Join(", ", copy.Hands) : "нет")}; без эффектов ({copy.Dropped.Count}): " +
                             string.Join(", ", copy.Dropped.Distinct()));
        }

        internal void Update()
        {
            if (!Game.IsAlive(_pivot)) return;
            _time += Time.deltaTime;
            _pivot.localRotation = Quaternion.Euler(0f, _time * SpinSpeed, 0f);
            _pivot.localPosition = Top + Vector3.up * (BobHeight * Mathf.Sin(_time * BobSpeed));
            _fx.Update(_time);

            if (_broken)
            {
                if (Time.unscaledTime < _nextRetry) return;
                _broken = false;
                Show(_class, _skinName);
                return;
            }
            if (_skin == null) return;
            bool ok;
            try { ok = _skin.Sync(); }
            catch (Exception e) { Core.Log.Error("Голограмма: " + e.Message); ok = false; }
            if (ok) return;
            // Игра выгрузила меши скина — собираем заново, но не чаще раза в RetryDelay: если скин так и не
            // надевается, иначе голограмма пересобиралась бы (и писала ошибку) каждый кадр
            _broken = true;
            _nextRetry = Time.unscaledTime + (_retried ? RetryDelay : 0f);
            _retried = true;
        }

        private const float RetryDelay = 3f;
        private float _nextRetry;
        private bool _broken, _retried;   // _retried — уже пересобирали после сбоя (следующий раз — с паузой)

        private void Clear()
        {
            try { _skin?.Remove(); } catch { }
            _skin = null;
            if (Game.IsAlive(_model)) Object.Destroy(_model);
            _model = null;
        }

        // Поза и размер. Всё считается в осях модели (она пока стоит ровно, масштаб 1):
        //  1) ядро — меши не под кистями и не в «руках»; 2) кисти — к бокам ядра на высоту плеч;
        //  3) масштаб — GameScale, как в игре (с пределом на необычно большие); ось — через центр ядра,
        //  низ всей модели — на крышке. Возвращает высоту верха над крышкой и радиус ядра (под ширину луча)
        private (float, float) Arrange(CopyState state)
        {
            Transform model = _model.transform;
            var all = model.GetComponentsInChildren<Renderer>(false).Where(r => r != null && r.enabled).ToList();
            var aside = state.HandNodes.Select(h => h.Node).Concat(state.Held).ToList();
            var core = all.Where(r => !aside.Any(a => r.transform.IsChildOf(a))).ToList();
            if (core.Count == 0) core = all;
            if (!BoundsOf(core, model, out Bounds cb)) return (CoreSize, 0.3f);

            float h = Mathf.Max(cb.size.y, 0.01f);
            foreach (var hand in state.HandNodes)
            {
                if (hand.Rotate) hand.Node.rotation = model.rotation * Quaternion.Euler(0f, hand.Left ? 270f : 90f, 0f);
                var mine = all.Where(r => r.transform.IsChildOf(hand.Node)).ToList();
                bool has = BoundsOf(mine, model, out Bounds hb);
                Vector3 now = has ? hb.center : model.InverseTransformPoint(hand.Node.position);
                float half = has ? hb.extents.x : 0f;
                var want = new Vector3(hand.Left ? cb.min.x - HandGap * h - half : cb.max.x + HandGap * h + half,
                                       cb.min.y + ShoulderAt * h, cb.center.z);
                hand.Node.position += model.TransformVector(want - now);
            }

            BoundsOf(all, model, out Bounds ab);
            // Размер как в игре: один масштаб на всех (с масштабом корня префаба — у Jacket 0.7, у Sparrow 1.2),
            // Golem больше Kobold, Drone маленький; предел — только для тех, кто не влез бы над пьедесталом
            float scale = GameScale * state.RootScale;
            scale = Mathf.Min(scale, MaxHeight / Mathf.Max(ab.size.y, 0.01f));
            scale = Mathf.Min(scale, MaxWidth / Mathf.Max(ab.size.x, 0.01f));
            scale = Mathf.Min(scale, MaxDepth / Mathf.Max(ab.size.z, 0.01f));
            if (ClassSize.TryGetValue(state.ClassKey ?? "", out float k)) scale *= k;   // подобрано на глаз
            model.localScale = Vector3.one * scale;
            model.localPosition = new Vector3(-cb.center.x * scale, -ab.min.y * scale + Lift, -cb.center.z * scale);
            return (ab.size.y * scale + Lift, Mathf.Max(cb.extents.x, cb.extents.z) * scale);
        }

        // Габариты рендереров в осях space
        private static bool BoundsOf(List<Renderer> renderers, Transform space, out Bounds b)
        {
            b = new Bounds();
            bool any = false;
            foreach (Renderer r in renderers)
                foreach (Vector3 corner in Corners(r.bounds))
                {
                    Vector3 local = space.InverseTransformPoint(corner);
                    if (!any) { b = new Bounds(local, Vector3.zero); any = true; }
                    else b.Encapsulate(local);
                }
            return any;
        }

        private static IEnumerable<Vector3> Corners(Bounds b)
        {
            Vector3 mn = b.min, mx = b.max;
            for (int i = 0; i < 8; i++)
                yield return new Vector3((i & 1) == 0 ? mn.x : mx.x, (i & 2) == 0 ? mn.y : mx.y, (i & 4) == 0 ? mn.z : mx.z);
        }

        private sealed class CopyState
        {
            internal bool TPose;
            internal int Meshes;
            internal readonly List<string> Dropped = new();
            internal readonly List<string> Hands = new();
            internal readonly List<(Transform Node, bool Left, bool Rotate)> HandNodes = new();
            internal Transform Space;   // в чьих осях мерить сдвиг позы покоя
            internal float RootScale = 1f;
            internal string ClassKey;
            internal readonly List<Transform> Held = new();
            internal readonly Dictionary<IntPtr, Transform> Map = new();   // узел префаба → копия
        }

        // Детали, которые в игре двигает ScalarProceduralAnimation (клинки Assassin, щиты Kobold, затворы…),
        // в префабе стоят как попало; ставим их так же, как игра при появлении класса: между позой начала
        // и конца по currentTime анимации
        private static int RestPoses(GameObject prefab, CopyState state, out int distrusted)
        {
            int count = 0;
            distrusted = 0;
            foreach (var anim in prefab.GetComponentsInChildren<Il2CppDust.ScalarProceduralAnimation>(true))
            {
                if (anim == null || anim.animatedTransforms == null) continue;
                float time = Mathf.Clamp01(anim.currentTime);
                var list = anim.animatedTransforms;
                for (int i = 0; i < list.Count; i++)
                {
                    var at = list[i];
                    Transform src = at != null ? at.transform : null;
                    if (src == null || !state.Map.TryGetValue(src.Pointer, out Transform t) || t == null) continue;
                    Vector3 was = t.localPosition, wasAt = state.Space.InverseTransformPoint(t.position);
                    t.localPosition = Vector3.Lerp(at.startPos, at.endPosition, time);
                    if ((state.Space.InverseTransformPoint(t.position) - wasAt).magnitude > MaxRestShift)
                    {
                        t.localPosition = was;
                        distrusted++;
                        continue;
                    }
                    t.localRotation = Quaternion.Slerp(at.startRot, at.endRotation, time);
                    Vector3 scale = Vector3.Lerp(at.startScale, at.endScale, time);
                    if (scale.sqrMagnitude > 1e-8f) t.localScale = scale;   // нулевой масштаб — «нет позы», не прячем
                    count++;
                }
            }
            return count;
        }

        // Копия узла: имя, поза, включённость; меш и материалы — только у частей тела
        private static GameObject Copy(Transform src, Transform parent, CopyState state)
        {
            var go = new GameObject(src.name) { layer = 0 };   // слой 0 — видно камере игрока
            Transform t = go.transform;
            t.SetParent(parent, false);
            t.localPosition = src.localPosition;
            t.localRotation = src.localRotation;
            t.localScale = src.localScale;
            state.Map[src.Pointer] = t;

            if (HandNode.IsMatch(src.name) && HasComponent(src, "PoseInterpolator"))
            {
                state.HandNodes.Add((t, src.name.StartsWith("Left"), state.TPose));
                if (state.TPose) state.Hands.Add(src.name);
            }
            else if (HeldNode.IsMatch(src.name)) state.Held.Add(t);

            MeshFilter mf = src.GetComponent<MeshFilter>();
            Mesh mesh = mf != null ? mf.sharedMesh : null;
            var smr = src.GetComponent<SkinnedMeshRenderer>();
            if (mesh == null && smr != null) mesh = smr.sharedMesh;
            if (mesh != null)
            {
                go.AddComponent<MeshFilter>().sharedMesh = mesh;
                Renderer source = src.GetComponent<MeshRenderer>();
                if (source == null) source = smr;
                string notBody = source != null ? NotBody(source) : null;
                if (source != null && notBody == null && !JunkMesh.IsMatch(src.name))
                {
                    var r = go.AddComponent<MeshRenderer>();
                    // Один материал тела на всех: цвет команды по умолчанию у префабов разный (у Seraph — красный)
                    var body = new Material[source.sharedMaterials.Length];
                    for (int k = 0; k < body.Length; k++) body[k] = Hub.GameMaterial();
                    r.sharedMaterials = body;
                    r.enabled = source.enabled;
                    state.Meshes++;
                }
                else if (notBody != null) state.Dropped.Add($"{src.name} ({notBody})");
            }

            for (int i = 0; i < src.childCount; i++)
            {
                Transform c = src.GetChild(i);
                if (JunkNode.IsMatch(c.name) || c.GetComponent<RectTransform>() != null || c.GetComponent<Canvas>() != null) continue;
                Copy(c, t, state);
            }
            if (!src.gameObject.activeSelf || ThrownNode.IsMatch(src.name)) go.SetActive(false);
            return go;
        }

        // null — часть тела (все материалы — тела игры); иначе — имя чужого шейдера
        private static string NotBody(Renderer r)
        {
            Material[] mats = r.sharedMaterials;
            if (mats.Length == 0) return "без материала";
            foreach (Material m in mats)
            {
                string shader = m != null && m.shader != null ? m.shader.name : "нет";
                if (!shader.StartsWith(BodyShader, StringComparison.Ordinal)) return shader;
            }
            return null;
        }

        private static bool HasComponent(Transform t, string typeName)
        {
            foreach (Component c in t.GetComponents<Component>())
                if (c != null && c.GetIl2CppType().Name == typeName) return true;
            return false;
        }

        /// <summary>
        /// Эффекты проекции: светящийся конус луча из линзы пьедестала (мерцает), свечение линзы,
        /// кольца-сканеры, бегущие снизу вверх, и пиксельные искры, всплывающие в луче. Всё — без света
        /// (UI/Default), прозрачность — альфой цвета вершин и материала.
        /// </summary>
        private sealed class Projection
        {
            internal static readonly Vector3 Lens = new Vector3(-1.545f, 0.705f, 0f);   // линза чуть ниже крышки
            private const float R0 = 0.12f;                     // радиус луча у линзы
            private float _r1 = 0.5f;                           // и наверху — под ширину модели
            private const int Rings = 1, Sparks = 6;
            private const float BeamAlpha = 0.1f, RingAlpha = 0.3f;
            private static readonly Color Cyan = new Color(0.3f, 0.95f, 1f);

            private readonly Transform _root, _beam, _glow;
            private readonly Material _beamMat, _glowMat;
            private readonly Transform[] _rings = new Transform[Rings];
            private readonly Material[] _ringMats = new Material[Rings];
            private readonly Transform[] _sparks = new Transform[Sparks];
            private readonly Vector3[] _sparkDir = new Vector3[Sparks];
            private readonly float[] _sparkSpeed = new float[Sparks], _sparkPhase = new float[Sparks];
            private float _height = 1f;

            internal Projection(Transform terminal)
            {
                _root = new GameObject("Skins Projection").transform;
                _root.SetParent(terminal, false);
                _root.localPosition = Lens;

                _beamMat = Hub.Unlit(Cyan);
                _beam = Part("Beam", Cone(R0, 1f, 1f, 32, BeamAlpha, 0f), _beamMat);
                _glowMat = Hub.Unlit(Cyan);
                _glow = Part("Glow", Disc(R0 * 0.8f, 32, 0.4f), _glowMat);
                _glow.localPosition = Vector3.up * 0.004f;

                Mesh ring = Annulus(0.97f, 1f, 48, 1f);
                for (int i = 0; i < Rings; i++)
                {
                    _ringMats[i] = Hub.Unlit(Cyan);
                    _rings[i] = Part("Scan Ring", ring, _ringMats[i]);
                }

                var rnd = new System.Random(49);
                Mesh px = Meshes.Quad(0.01f, 0.01f, false);
                Material sparkMat = Hub.Unlit(new Color(0.75f, 1f, 1f, 0.65f));
                for (int i = 0; i < Sparks; i++)
                {
                    _sparks[i] = Part("Spark", px, sparkMat);
                    float a = (float)(rnd.NextDouble() * Math.PI * 2), d = 0.15f + 0.8f * (float)rnd.NextDouble();
                    _sparkDir[i] = new Vector3(Mathf.Cos(a) * d, 0f, Mathf.Sin(a) * d);
                    _sparkSpeed[i] = 0.25f + 0.35f * (float)rnd.NextDouble();
                    _sparkPhase[i] = (float)rnd.NextDouble();
                }
                _root.gameObject.SetActive(false);   // до первого «Review Skin» пьедестал пуст
            }

            internal void Show(float height, float radius)
            {
                _height = Mathf.Max(0.12f, height * 0.3f);   // луч — низкий, до трети модели
                _r1 = Mathf.Clamp(radius + 0.05f, 0.22f, 0.45f);
                RebuildBeam();
                _root.gameObject.SetActive(true);
            }

            internal void Update(float t)
            {
                if (!Game.IsAlive(_root) || !_root.gameObject.activeSelf) return;

                // Мерцание луча и пульс линзы
                float flicker = 0.85f + 0.1f * Mathf.Sin(t * 23f) * Mathf.Sin(t * 7.3f) ;
                SetAlpha(_beamMat, flicker);
                SetAlpha(_glowMat, 0.7f + 0.3f * Mathf.Sin(t * 3f));

                for (int i = 0; i < Rings; i++)
                {
                    float p = Mathf.Repeat(t * 0.45f + i / (float)Rings, 1f);
                    float r = Mathf.Lerp(R0, _r1, p);
                    _rings[i].localPosition = Vector3.up * (p * _height);
                    _rings[i].localScale = new Vector3(r, 1f, r);
                    SetAlpha(_ringMats[i], Mathf.Sin(p * Mathf.PI) * RingAlpha);
                }

                Camera cam = Camera.main;
                for (int i = 0; i < Sparks; i++)
                {
                    float p = Mathf.Repeat(t * _sparkSpeed[i] + _sparkPhase[i], 1f);
                    float r = Mathf.Lerp(R0, _r1, p);
                    Transform s = _sparks[i];
                    s.localPosition = _sparkDir[i] * r + Vector3.up * (p * _height);
                    s.localScale = Vector3.one * Mathf.Sin(p * Mathf.PI);   // появляется и тает
                    if (cam != null) s.rotation = Quaternion.LookRotation(s.position - cam.transform.position);   // лицом к камере
                }
            }

            // Конус луча под текущие высоту и радиус (масштабом нельзя: верх и низ разного радиуса)
            private void RebuildBeam()
            {
                var mf = _beam.GetComponent<MeshFilter>();
                Mesh old = mf.sharedMesh;
                mf.sharedMesh = Cone(R0, _r1, _height, 32, BeamAlpha, 0f);
                if (old != null) Object.Destroy(old);
            }

            private Transform Part(string name, Mesh mesh, Material mat)
            {
                var go = new GameObject(name);
                go.transform.SetParent(_root, false);
                go.AddComponent<MeshFilter>().sharedMesh = mesh;
                var r = go.AddComponent<MeshRenderer>();
                r.sharedMaterial = mat;
                r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
                r.receiveShadows = false;
                return go.transform;
            }

            private static void SetAlpha(Material m, float a)
            {
                var c = new Color(Cyan.r, Cyan.g, Cyan.b, Mathf.Clamp01(a));
                if (m.HasProperty("_Color")) m.SetColor("_Color", c);
                if (m.HasProperty("_BaseColor")) m.SetColor("_BaseColor", c);
            }

            // Конус без крышек: от радиуса r0 (y 0) до r1 (y h); альфа вершин — от a0 внизу до a1 вверху
            private static Mesh Cone(float r0, float r1, float h, int n, float a0, float a1)
            {
                var v = new Vector3[n * 2];
                var c = new Color[n * 2];
                var tris = new List<int>();
                for (int i = 0; i < n; i++)
                {
                    float a = i * Mathf.PI * 2 / n;
                    Vector3 dir = new Vector3(Mathf.Cos(a), 0f, Mathf.Sin(a));
                    v[i] = dir * r0;
                    v[n + i] = dir * r1 + Vector3.up * h;
                    c[i] = new Color(1f, 1f, 1f, a0);
                    c[n + i] = new Color(1f, 1f, 1f, a1);
                    int j = (i + 1) % n;
                    tris.AddRange(new[] { i, n + i, n + j, i, n + j, j });
                }
                return Build("Skins Beam", v, c, tris);
            }

            // Диск со светлой серединой и прозрачным краем
            private static Mesh Disc(float r, int n, float a)
            {
                var v = new Vector3[n + 1];
                var c = new Color[n + 1];
                var tris = new List<int>();
                v[n] = Vector3.zero;
                c[n] = new Color(1f, 1f, 1f, a);
                for (int i = 0; i < n; i++)
                {
                    float ang = i * Mathf.PI * 2 / n;
                    v[i] = new Vector3(Mathf.Cos(ang) * r, 0f, Mathf.Sin(ang) * r);
                    c[i] = new Color(1f, 1f, 1f, 0f);
                    tris.AddRange(new[] { n, i, (i + 1) % n });
                }
                return Build("Skins Glow", v, c, tris);
            }

            // Плоское кольцо радиусом 1 (наружный край) — масштабом подгоняется под луч
            private static Mesh Annulus(float inner, float outer, int n, float a)
            {
                var v = new Vector3[n * 2];
                var c = new Color[n * 2];
                var tris = new List<int>();
                for (int i = 0; i < n; i++)
                {
                    float ang = i * Mathf.PI * 2 / n;
                    Vector3 dir = new Vector3(Mathf.Cos(ang), 0f, Mathf.Sin(ang));
                    v[i] = dir * inner;
                    v[n + i] = dir * outer;
                    c[i] = c[n + i] = new Color(1f, 1f, 1f, a);
                    int j = (i + 1) % n;
                    tris.AddRange(new[] { i, n + i, n + j, i, n + j, j });
                }
                return Build("Skins Scan Ring", v, c, tris);
            }

            private static Mesh Build(string name, Vector3[] v, Color[] c, List<int> tris)
            {
                var m = new Mesh { name = name };
                m.vertices = v;
                m.colors = c;
                m.triangles = tris.ToArray();
                m.RecalculateNormals();
                m.RecalculateBounds();
                return m;
            }
        }
    }
}
