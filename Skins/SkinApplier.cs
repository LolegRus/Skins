using System.Globalization;
using UnityEngine;
using Object = UnityEngine.Object;

namespace Skins
{
    /// <summary>
    /// Скин, надетый на экземпляр класса. Сборщик (Unity\, Tools → Skins → Build All) уже подогнал меши
    /// под оси и масштаб узлов игры, поэтому модель скина — плоский список детей корня:
    ///
    ///  • «путь узла» с MeshFilter — на этом узле игры меняется только MeshFilter.sharedMesh. Рендерер,
    ///    материал (цвет команды, вспышка урона, растворение), аниматоры и коллайдеры остаются игры;
    ///  • «путь узла» без MeshFilter — меш игры на этом узле прячется (например, «Witch Hat»);
    ///  • «+путь узла» — новые детали: его дети вешаются на этот узел как есть. Их материал каждый кадр
    ///    повторяет материал ближайшего рендерера игры (кроме материалов «Own…» — они остаются свои).
    ///    Деталь с пустым ребёнком «@end» движется: её поза — покой, поза «@end» (в осях детали) — конец,
    ///    а между ними её каждый кадр ставит анимация игры у ближайшего предка (ScalarProceduralAnimation,
    ///    currentTime 0…1) — так у Crossbolt Sunset ручки затвора ездят вместе с болтом при выстреле.
    ///    «@end 0.3» — конец уже при currentTime 0.3 (отдача арбалета доводит время только до ~0.3–0.4).
    ///
    /// Путь — от корня класса, без его имени: «Anchors/Head Anchor/Head».
    /// </summary>
    internal sealed class AppliedSkin
    {
        private const string OwnMaterialPrefix = "Own";
        private const string ExtraPrefix = "+";
        private const string EndMarker = "@end";

        private readonly struct Swap
        {
            internal readonly MeshFilter Filter;
            internal readonly Mesh Original;
            internal readonly Mesh Wanted;   // null — меш спрятан

            internal Swap(MeshFilter filter, Mesh original, Mesh wanted)
            {
                Filter = filter;
                Original = original;
                Wanted = wanted;
            }
        }

        private sealed class Extra
        {
            internal GameObject Root;
            internal Transform Source;          // узел игры, чей материал повторяем
            internal Renderer[] Renderers;
            internal bool[][] Own;              // слоты с материалами «Own…» — не трогаем
            internal IntPtr Applied;
        }

        /// <summary>Новая деталь, которая ездит вместе с анимацией игры (см. «@end»).</summary>
        private sealed class Moving
        {
            internal Transform Part;
            internal Il2CppDust.ScalarProceduralAnimation Driver;
            internal Vector3 StartPos, EndPos;
            internal Quaternion StartRot, EndRot;
            internal float Full = 1f;   // currentTime, при котором деталь доходит до «@end»
        }

        internal readonly Skin Skin;
        internal readonly IntPtr Target;   // экземпляр класса

        private readonly Transform _root;
        private readonly Dictionary<string, Transform> _nodes = new();
        private readonly List<Swap> _swaps = new();
        private readonly List<Extra> _extras = new();
        private readonly List<Moving> _moving = new();
        private bool _reportedRestore;

        /// <param name="owner">Чей это класс — для лога («свой» или ID другого игрока).</param>
        internal AppliedSkin(Skin skin, Transform root, string owner)
        {
            Skin = skin;
            _root = root;
            Target = root.Pointer;
            Index(root, "");
            IndexProjectiles();

            var missing = new List<string>();
            Transform skinRoot = skin.Prefab.transform;
            for (int i = 0; i < skinRoot.childCount; i++)
            {
                Transform entry = skinRoot.GetChild(i);
                string name = entry.name;
                bool extra = name.StartsWith(ExtraPrefix);
                string path = extra ? name.Substring(ExtraPrefix.Length) : name;

                Transform node = path.Length == 0 ? root : Find(path);
                if (node == null)
                {
                    missing.Add(path);
                    continue;
                }

                if (extra)
                {
                    for (int j = 0; j < entry.childCount; j++) AddExtra(entry.GetChild(j), node);
                    continue;
                }

                MeshFilter mf = node.GetComponent<MeshFilter>();
                if (mf == null)
                {
                    missing.Add(path + " (без MeshFilter)");
                    continue;
                }
                MeshFilter skinMf = entry.GetComponent<MeshFilter>();
                Mesh mesh = skinMf != null ? skinMf.sharedMesh : null;
                AddSwap(mf, mesh);
            }

            if (missing.Count > 0)
                Core.Log.Warning($"Скин '{skin.Name}' на {Core.Title(skin.ClassKey)} ({owner}): в классе нет узлов ({missing.Count}) — скин собран под другую версию игры?\n  " + string.Join("\n  ", missing));
        }

        /// <summary>
        /// Узел по пути. Если точного нет — по концу пути, когда он однозначен: копия класса у других
        /// игроков (свой префаб) может отличаться промежуточными узлами.
        /// </summary>
        private Transform Find(string path)
        {
            if (_nodes.TryGetValue(path, out Transform exact)) return exact;
            string[] parts = path.Split('/');
            for (int skip = 1; skip < parts.Length; skip++)
            {
                string tail = "/" + string.Join("/", parts, skip, parts.Length - skip);
                Transform found = null;
                int count = 0;
                foreach (var pair in _nodes)
                    if (pair.Key.EndsWith(tail) || pair.Key == tail.Substring(1))
                    {
                        found = pair.Value;
                        count++;
                    }
                if (count == 1) return found;
                if (count > 1) return null;   // дальше хвост только короче — однозначнее не станет
            }
            return null;
        }

        /// <summary>Узлы класса по пути от корня (первый при повторах).</summary>
        private void Index(Transform t, string prefix)
        {
            for (int i = 0; i < t.childCount; i++)
            {
                Transform c = t.GetChild(i);
                string path = prefix.Length == 0 ? c.name : prefix + "/" + c.name;
                _nodes.TryAdd(path, c);
                Index(c, path);
            }
        }

        /// <summary>
        /// Снаряды, которые класс бросает (у Crossbolt — колесо «03_BIGWHEEL (Projectile)»), во время игры
        /// лежат не внутри класса. Их находим по ссылке ProjectileTeleporter.teleportProjectile из узлов класса
        /// и добавляем в путь под их именем: «03_BIGWHEEL (Projectile)/03_BIGWHEEL».
        /// </summary>
        private void IndexProjectiles()
        {
            foreach (Transform node in _nodes.Values.ToList())
            {
                var teleporter = node.GetComponent<Il2CppDust.Combat.ProjectileTeleporter>();
                var projectile = teleporter != null ? teleporter.teleportProjectile : null;
                if (projectile == null) continue;
                Transform t = projectile.transform;
                if (t.IsChildOf(_root) || !_nodes.TryAdd(t.name, t)) continue;
                Index(t, t.name);
            }
        }

        private void AddSwap(MeshFilter mf, Mesh mesh)
        {
            // Один меш на деталь: второй путь к ней (поиск по концу пути) сломал бы и вид, и возврат меша игры
            foreach (Swap s in _swaps)
                if (s.Filter != null && s.Filter.Pointer == mf.Pointer)
                {
                    Core.Log.Warning($"'{mf.name}': меш скина на эту деталь уже стоит — второй пропущен.");
                    return;
                }
            Mesh original = mf.sharedMesh;
            _swaps.Add(new Swap(mf, original, mesh));
            mf.sharedMesh = mesh;
            if (mesh == null) return;

            Renderer r = mf.GetComponent<Renderer>();
            int slots = r != null ? r.sharedMaterials.Length : 0;
            if (mesh.subMeshCount > slots)
                Core.Log.Warning($"'{mf.name}': у меша скина {mesh.subMeshCount} частей, а материалов у игры {slots} — лишние части не видно.");
        }

        private void AddExtra(Transform part, Transform parent)
        {
            GameObject copy = Object.Instantiate(part.gameObject, parent, false);
            copy.name = "Skin " + part.name;

            int layer = parent.gameObject.layer;
            foreach (Transform t in copy.GetComponentsInChildren<Transform>(true)) t.gameObject.layer = layer;

            Renderer[] renderers = copy.GetComponentsInChildren<Renderer>(true);
            var own = new bool[renderers.Length][];
            for (int i = 0; i < renderers.Length; i++)
            {
                Material[] mats = renderers[i].sharedMaterials;
                own[i] = new bool[mats.Length];
                for (int j = 0; j < mats.Length; j++)
                    own[i][j] = mats[j] != null && mats[j].name.StartsWith(OwnMaterialPrefix, StringComparison.OrdinalIgnoreCase);
            }

            _extras.Add(new Extra { Root = copy, Source = parent, Renderers = renderers, Own = own });
            SyncExtra(_extras[_extras.Count - 1]);

            for (int i = 0; i < copy.transform.childCount; i++)
            {
                Transform end = copy.transform.GetChild(i);
                if (end.name == EndMarker || end.name.StartsWith(EndMarker + " "))
                {
                    AddMoving(copy.transform, end, parent);
                    break;
                }
            }
        }

        private void AddMoving(Transform part, Transform end, Transform parent)
        {
            Il2CppDust.ScalarProceduralAnimation driver = null;
            for (Transform p = parent; p != null && driver == null; p = p.parent)
            {
                driver = p.GetComponent<Il2CppDust.ScalarProceduralAnimation>();
                if (p == _root) break;
            }
            if (driver == null)
            {
                Core.Log.Warning($"'{part.name}': над '{parent.name}' нет анимации игры — деталь стоит на месте.");
                return;
            }
            Vector3 s = part.localScale;
            float full = 1f;
            string arg = end.name.Substring(EndMarker.Length).Trim();
            if (arg.Length > 0 && (!float.TryParse(arg, NumberStyles.Float, CultureInfo.InvariantCulture, out full) || full <= 0f))
            {
                Core.Log.Warning($"'{part.name}': в «{end.name}» не число больше 0 — конец при 1.");
                full = 1f;
            }
            _moving.Add(new Moving
            {
                Part = part,
                Driver = driver,
                StartPos = part.localPosition,
                StartRot = part.localRotation,
                EndPos = part.localPosition + part.localRotation * Vector3.Scale(s, end.localPosition),
                EndRot = part.localRotation * end.localRotation,
                Full = full,
            });
        }

        private static void SyncMoving(Moving m)
        {
            if (m.Part == null || m.Driver == null) return;
            float t = Mathf.Clamp01(m.Driver.currentTime / m.Full);
            m.Part.localPosition = Vector3.LerpUnclamped(m.StartPos, m.EndPos, t);
            m.Part.localRotation = Quaternion.SlerpUnclamped(m.StartRot, m.EndRot, t);
        }

        /// <summary>
        /// Каждый кадр: игра подменяет материалы рендереров (цвет команды, вспышка, растворение) — новые
        /// детали повторяют их; меши, которые игра могла вернуть, ставим снова.
        /// false — игра выгрузила меши самого скина (смена сцены): его надо снять и надеть заново.
        /// </summary>
        internal bool Sync()
        {
            foreach (Swap s in _swaps)
                if (!ReferenceEquals(s.Wanted, null) && !Game.IsAlive(s.Wanted)) return false;

            int restored = 0;
            foreach (Swap s in _swaps)
            {
                if (s.Filter == null) continue;
                Mesh now = s.Filter.sharedMesh;
                IntPtr nowPtr = now != null ? now.Pointer : IntPtr.Zero;
                IntPtr wantedPtr = s.Wanted != null ? s.Wanted.Pointer : IntPtr.Zero;
                if (nowPtr == wantedPtr) continue;
                s.Filter.sharedMesh = s.Wanted;
                restored++;
            }
            if (restored > 0 && !_reportedRestore)
            {
                Core.Log.Msg($"Игра вернула свои меши ({restored}) — ставлю скин снова.");
                _reportedRestore = true;
            }

            foreach (Extra e in _extras) SyncExtra(e);
            foreach (Moving m in _moving) SyncMoving(m);
            return true;
        }

        private void SyncExtra(Extra e)
        {
            Material m = SourceMaterial(e.Source);
            if (m == null || m.Pointer == e.Applied) return;
            for (int i = 0; i < e.Renderers.Length; i++)
            {
                Renderer r = e.Renderers[i];
                if (r == null) continue;
                Material[] mats = r.sharedMaterials;
                for (int j = 0; j < mats.Length; j++)
                    if (j >= e.Own[i].Length || !e.Own[i][j]) mats[j] = m;
                r.sharedMaterials = mats;
            }
            e.Applied = m.Pointer;
        }

        /// <summary>Материал ближайшего к узлу рендерера игры (вверх по иерархии).</summary>
        private Material SourceMaterial(Transform t)
        {
            for (Transform p = t; p != null; p = p.parent)
            {
                Renderer r = p.GetComponent<Renderer>();
                if (r != null && r.sharedMaterial != null) return r.sharedMaterial;
                if (p == _root) break;
            }
            return null;
        }

        /// <summary>Снимает скин: меши игры — обратно, новые детали — удалить.</summary>
        internal void Remove()
        {
            foreach (Swap s in _swaps)
                if (s.Filter != null) s.Filter.sharedMesh = s.Original;
            foreach (Extra e in _extras)
                if (e.Root != null) Object.Destroy(e.Root);
            _swaps.Clear();
            _extras.Clear();
            _moving.Clear();
        }
    }
}
