using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEngine;
using Object = UnityEngine.Object;

/// <summary>
/// Сборка скинов для мода Skins (ПК и Quest).
///
/// Скин — папка Assets/Skins/&lt;Имя скина&gt;/. В ней модель для класса, названная его ключом:
/// KOBOLD.fbx (или готовый KOBOLD.prefab), SCARAB.fbx и т.д. Из FBX префаб создаётся сам, при первой
/// сборке; дальше его можно править в Unity — он ссылается на FBX и обновляется вместе с ним.
/// Детали игры, которые скин прячет (например, Witch Hat), перечисляются в hide.txt рядом с моделью.
///
/// Имена объектов модели — как у узлов класса в игре (дамп мода: Ctrl+F11, копия — в ClassDumps\).
/// Модель — поза покоя класса относительно общего предка найденных узлов (у Kobold — «Anchors»),
/// масштабы узлов ниже него вшиты в вершины, оси — как в Blender. Сборщик сам переводит каждый меш
/// в оси и масштаб узла игры (поворот округляется до 90°) и собирает для мода плоский префаб:
///   «путь узла в игре» с MeshFilter      — заменить меш на этом узле;
///   «путь узла в игре» без MeshFilter    — спрятать меш на этом узле;
///   «+путь узла в игре» с детьми         — новые детали: дети вешаются на этот узел как есть.
/// Так модам не нужно ни считать позы, ни править вершины — на Quest это и не получилось бы.
///
/// Бандлы: Build/Windows → игра UserData\Skins\&lt;Имя&gt;.skin и Skins\Builtin\ (зашиваются в DLL),
/// Build/Android → Quest mods\Skins\build\&lt;Имя&gt;.skin (идут в .qmod — допиши их в fileCopies mod.json).
///
/// Командная строка:
///   Unity.exe -batchmode -nographics -projectPath Unity -executeMethod SkinBuilder.BuildAll -quit
/// </summary>
public static class SkinBuilder
{
    private const string SkinsRoot = "Assets/Skins";
    private const string OutDir = "Build";
    private const string DumpDir = "ClassDumps";   // дампы классов из игры (KOBOLD.prefab.txt)
    private const string HideFile = "hide.txt";    // детали игры, которые скин прячет, — по одной на строку
    private const string FittedDir = "_build";     // подогнанные меши и плоские префабы — генерируются
    private const string DefaultDustPath = @"C:\Users\loleg\Desktop\DUST\Dust 0.8.4";
    private const string QuestBuildDir = @"..\..\Quest mods\Skins\build";
    private const string BuiltinDir = @"..\Skins\Builtin";   // скины, которые идут с ПК-модом (EmbeddedResource)
    private const string OwnMaterialPrefix = "Own";

    [MenuItem("Tools/Skins/Build All")]
    public static void BuildAll()
    {
        string dust = Environment.GetEnvironmentVariable("DustPath");
        if (string.IsNullOrEmpty(dust)) dust = DefaultDustPath;
        RefreshDumps(dust);

        var builds = new List<AssetBundleBuild>();
        var names = new Dictionary<string, string>();   // имя бандла → имя скина
        var report = new StringBuilder();

        foreach (string dir in Directory.GetDirectories(SkinsRoot).OrderBy(d => d))
        {
            string skin = Path.GetFileName(dir);
            string folder = dir.Replace('\\', '/');
            foreach (string model in Directory.GetFiles(folder, "*.fbx"))
            {
                string key = Path.GetFileNameWithoutExtension(model).ToUpperInvariant();
                string source = $"{folder}/{key}.prefab";
                ConfigureImporter(model.Replace('\\', '/'));
                if (!File.Exists(source)) MakePrefab(model.Replace('\\', '/'), source);
            }

            string[] hide = File.Exists(Path.Combine(folder, HideFile))
                ? File.ReadAllLines(Path.Combine(folder, HideFile)).Select(l => l.Trim()).Where(l => l.Length > 0 && !l.StartsWith("#")).ToArray()
                : new string[0];

            var prefabs = new List<string>();
            var keys = new List<string>();
            foreach (string prefab in Directory.GetFiles(folder, "*.prefab"))
            {
                string path = prefab.Replace('\\', '/');
                string key = Path.GetFileNameWithoutExtension(path).ToUpperInvariant();
                report.AppendLine($"==== {skin} / {key}");
                string fitted = Fit(folder, key, AssetDatabase.LoadAssetAtPath<GameObject>(path), hide, report);
                if (fitted == null) continue;
                prefabs.Add(fitted);
                keys.Add(key);
            }

            if (prefabs.Count == 0)
            {
                Debug.LogWarning($"[Skins] В '{folder}' нет моделей (KOBOLD.fbx и т.п.) — пропуск.");
                continue;
            }

            string bundle = BundleName(skin);
            names[bundle] = skin;
            builds.Add(new AssetBundleBuild
            {
                assetBundleName = bundle,
                assetNames = prefabs.ToArray(),
                addressableNames = keys.ToArray(),   // мод ищет модель по ключу класса
            });
            Debug.Log($"[Skins] Скин '{skin}': {string.Join(", ", keys)}");
        }

        Directory.CreateDirectory(OutDir);
        File.WriteAllText(Path.Combine(OutDir, "report.txt"), report.ToString());
        if (builds.Count == 0)
        {
            Debug.LogError($"[Skins] Нечего собирать: положи модели в {SkinsRoot}/<Имя скина>/KOBOLD.fbx (и дамп класса в {DumpDir}).");
            return;
        }
        AssetDatabase.SaveAssets();

        string windows = Path.Combine(OutDir, "Windows"), android = Path.Combine(OutDir, "Android");
        Build(windows, builds, BuildTarget.StandaloneWindows64);
        Build(android, builds, BuildTarget.Android);

        Deliver(windows, names, Directory.Exists(dust) ? Path.Combine(dust, "UserData", "Skins") : null, "игра");
        Deliver(android, names, Directory.Exists(Path.GetDirectoryName(Path.GetFullPath(QuestBuildDir))) ? Path.GetFullPath(QuestBuildDir) : null, "Quest");
        Deliver(windows, names, Path.GetFullPath(BuiltinDir), "в мод (зашиваются в DLL)");
    }

    private static void Build(string dir, List<AssetBundleBuild> builds, BuildTarget target)
    {
        Directory.CreateDirectory(dir);
        BuildPipeline.BuildAssetBundles(dir, builds.ToArray(),
            BuildAssetBundleOptions.ChunkBasedCompression | BuildAssetBundleOptions.StrictMode, target);
    }

    private static void Deliver(string dir, Dictionary<string, string> names, string target, string what)
    {
        if (target == null)
        {
            Debug.LogWarning($"[Skins] Папка для «{what}» не найдена — бандлы остались в {Path.GetFullPath(dir)}. Переименуй их в <Имя>.skin.");
            return;
        }
        Directory.CreateDirectory(target);
        foreach (var pair in names)
            File.Copy(Path.Combine(dir, pair.Key), Path.Combine(target, pair.Value + ".skin"), true);
        Debug.Log($"[Skins] {what}: скопировано {names.Count} в {target}");
    }

    /// <summary>Свежие дампы из игры (если она есть) — в ClassDumps\, откуда их читает сборка.</summary>
    private static void RefreshDumps(string dust)
    {
        string from = Path.Combine(dust, "UserData", "Skins", "Dumps");
        if (!Directory.Exists(from)) return;
        Directory.CreateDirectory(DumpDir);
        foreach (string file in Directory.GetFiles(from, "*.prefab.txt"))
        {
            string to = Path.Combine(DumpDir, Path.GetFileName(file));
            if (!File.Exists(to) || File.GetLastWriteTimeUtc(file) > File.GetLastWriteTimeUtc(to)) File.Copy(file, to, true);
        }
    }

    // ---------------- подгонка под класс ----------------

    /// <summary>Плоский префаб для мода (см. описание класса); null — дампа класса нет.</summary>
    private static string Fit(string folder, string key, GameObject source, string[] hide, StringBuilder report)
    {
        string dump = Path.Combine(DumpDir, key + ".prefab.txt");
        if (!File.Exists(dump))
        {
            Debug.LogError($"[Skins] Нет дампа класса {dump} — сними его в игре (Ctrl+F11) и собери снова.");
            return null;
        }

        string outDir = $"{folder}/{FittedDir}";
        if (AssetDatabase.IsValidFolder(outDir)) AssetDatabase.DeleteAsset(outDir);
        AssetDatabase.CreateFolder(folder, FittedDir);

        GameObject rest = BuildRest(dump);
        // Экземпляр стоит как префаб: корень FBX — с поворотом 270° по X (это оси Blender) и масштабом 100.
        GameObject skin = (GameObject)PrefabUtility.InstantiatePrefab(source);
        skin.transform.position = Vector3.zero;
        var output = new GameObject(key);
        try
        {
            var index = new NodeIndex(rest.transform);
            Transform skinRoot = skin.transform;
            Transform restBase = CommonAncestor(skinRoot, index);
            report.AppendLine($"корень модели = '{PathOf(restBase, rest.transform)}'");

            var ctx = new Ctx { Index = index, Rest = rest.transform, Base = restBase, SkinRoot = skinRoot, Out = output.transform, Folder = outDir, Report = report };
            for (int i = 0; i < skinRoot.childCount; i++) Visit(ctx, skinRoot.GetChild(i), null, null);

            foreach (string name in hide)
            {
                Transform node = index.Resolve(name);
                if (node == null) { report.AppendLine($"  ! спрятать '{name}': в классе такого нет"); continue; }
                new GameObject(PathOf(node, rest.transform)).transform.SetParent(output.transform, false);
                report.AppendLine($"  {PathOf(node, rest.transform)}: спрятать");
            }

            string path = $"{outDir}/{key}.prefab";
            PrefabUtility.SaveAsPrefabAsset(output, path);
            return path;
        }
        finally
        {
            Object.DestroyImmediate(output);
            Object.DestroyImmediate(skin);
            Object.DestroyImmediate(rest);
        }
    }

    private sealed class Ctx
    {
        internal NodeIndex Index;
        internal Transform Rest, Base, SkinRoot, Out;
        internal string Folder;
        internal StringBuilder Report;
        internal readonly Dictionary<string, Transform> Extras = new();
        internal int MeshCount;
        internal readonly Dictionary<Transform, Matrix4x4> Turns = new();
    }

    /// <summary>Перевод меша в оси узла игры: поворот, кратный 90° (возможно, с отражением), и масштаб.</summary>
    private readonly struct FitData
    {
        internal static readonly FitData None = new FitData(Matrix4x4.identity, 1f);
        internal readonly Matrix4x4 Turn;   // только 3×3: переставляет оси и, может быть, отражает
        internal readonly float Scale;
        internal FitData(Matrix4x4 turn, float scale) { Turn = turn; Scale = scale; }
        internal bool Mirror => Turn.determinant < 0f;
        internal bool IsNone => IsIdentity(Turn) && Mathf.Abs(Scale - 1f) < 0.01f;
        internal Vector3 Apply(Vector3 v) => Turn.MultiplyVector(v) * Scale;
        public override string ToString() => TurnText(Turn) + (Mathf.Abs(Scale - 1f) < 0.01f ? "" : $" масштаб {Scale:F3}");
    }

    // Отражение, из которого вместе с поворотами складываются все 48 перестановок осей
    private static readonly Matrix4x4 FlipX = Matrix4x4.Scale(new Vector3(-1f, 1f, 1f));

    private static bool IsIdentity(Matrix4x4 m) =>
        (m.GetColumn(0) - (Vector4)Vector3.right).sqrMagnitude + (m.GetColumn(1) - (Vector4)Vector3.up).sqrMagnitude +
        (m.GetColumn(2) - (Vector4)Vector3.forward).sqrMagnitude < 1e-4f;

    /// <summary>Поворотная часть (у отражения — после снятия FlipX) и признак отражения.</summary>
    private static (Quaternion rotation, bool mirror) Split(Matrix4x4 m)
    {
        bool mirror = m.determinant < 0f;
        Matrix4x4 r = mirror ? m * FlipX : m;
        return (Quaternion.LookRotation(r.GetColumn(2), r.GetColumn(1)), mirror);
    }

    private static string TurnText(Matrix4x4 m)
    {
        if (IsIdentity(m)) return "";
        var (rotation, mirror) = Split(m);
        return $" доворот {V(rotation.eulerAngles)}{(mirror ? " с отражением X" : "")}";
    }

    /// <param name="anchor">Ближайший сопоставленный предок в модели.</param>
    /// <param name="restAnchor">Его пара в классе.</param>
    private static void Visit(Ctx ctx, Transform node, Transform anchor, Transform restAnchor)
    {
        Transform rest = ctx.Index.Resolve(node.name);
        var mf = node.GetComponent<MeshFilter>();
        Mesh mesh = mf != null ? mf.sharedMesh : null;

        if (rest != null)
        {
            string path = PathOf(rest, ctx.Rest);
            if (mesh != null)
            {
                FitData fit = FitOf(ctx, node, rest);
                var target = new GameObject(path);
                target.transform.SetParent(ctx.Out, false);
                target.AddComponent<MeshFilter>().sharedMesh = Fitted(ctx, mesh, fit);
                ctx.Report.AppendLine($"  {path}: меш '{mesh.name}' size={V(mesh.bounds.size)}{fit}");
            }
            else if (node.childCount == 0 && mf == null)
            {
                new GameObject(path).transform.SetParent(ctx.Out, false);
                ctx.Report.AppendLine($"  {path}: спрятать");
            }
            for (int i = 0; i < node.childCount; i++) Visit(ctx, node.GetChild(i), node, rest);
            return;
        }

        if (mesh != null)
        {
            AddExtra(ctx, node, anchor, restAnchor);
            return;
        }
        for (int i = 0; i < node.childCount; i++) Visit(ctx, node.GetChild(i), anchor, restAnchor);
    }

    /// <summary>Новая деталь: копия поддерева узла с позой относительно узла игры (в его осях и масштабе).</summary>
    private static void AddExtra(Ctx ctx, Transform node, Transform anchor, Transform restAnchor)
    {
        string parentPath = restAnchor != null ? PathOf(restAnchor, ctx.Rest) : "";
        if (!ctx.Extras.TryGetValue(parentPath, out Transform container))
        {
            container = new GameObject("+" + parentPath).transform;
            container.SetParent(ctx.Out, false);
            ctx.Extras[parentPath] = container;
        }

        FitData fit = anchor != null ? FitOf(ctx, anchor, restAnchor) : FitData.None;
        Transform from = anchor != null ? anchor : ctx.SkinRoot;
        Vector3 pos = fit.Apply(from.InverseTransformPoint(node.position));
        Vector3 a = from.lossyScale, n = node.lossyScale;
        Vector3 scale = new Vector3(n.x / a.x, n.y / a.y, n.z / a.z) * fit.Scale;
        // Поворот детали в осях узла игры; отражение уходит в отрицательный масштаб по X
        Matrix4x4 local = fit.Turn * Matrix4x4.Rotate(Quaternion.Inverse(from.rotation) * node.rotation);
        if (fit.Mirror)
        {
            local *= FlipX;
            scale.x = -scale.x;
        }
        Quaternion rot = Quaternion.LookRotation(local.GetColumn(2), local.GetColumn(1));

        GameObject copy = Object.Instantiate(node.gameObject, container, false);
        copy.name = node.name;
        copy.transform.localPosition = pos;
        copy.transform.localRotation = rot;
        copy.transform.localScale = scale;
        // Материалы игра всё равно подменит материалом узла (кроме «Own…») — свои шейдеры в бандл не тянем.
        foreach (var r in copy.GetComponentsInChildren<Renderer>(true))
        {
            var mats = r.sharedMaterials;
            for (int i = 0; i < mats.Length; i++)
                if (mats[i] != null && !mats[i].name.StartsWith(OwnMaterialPrefix, StringComparison.OrdinalIgnoreCase)) mats[i] = null;
            r.sharedMaterials = mats;
        }
        ctx.Report.AppendLine($"  +{parentPath}: новая деталь '{node.name}' pos={V(pos)} scale={scale.x:F3}");
    }

    /// <summary>
    /// Из осей узла модели в оси узла игры. Масштаб: у модели масштабы узлов вшиты в вершины,
    /// а игра применит масштаб узла ещё раз (тело Kobold — 1.15) — делим на него. Поворот — TurnOf.
    /// </summary>
    private static FitData FitOf(Ctx ctx, Transform skinNode, Transform rest)
    {
        if (rest == null) return FitData.None;
        float gameScale = rest.lossyScale.x / ctx.Base.lossyScale.x;
        float skinScale = skinNode.lossyScale.x / ctx.SkinRoot.lossyScale.x;
        float scale = gameScale > 1e-4f ? skinScale / gameScale : 1f;
        if (Mathf.Abs(scale - 1f) < 0.01f) scale = 1f;
        return new FitData(TurnOf(ctx, skinNode, rest), scale);
    }

    /// <summary>
    /// Поворот из осей узла модели в оси узла игры (кратный 90°, может быть с отражением).
    ///
    /// По умолчанию — из поз: у узла без сопоставленного предка сравниваются позы покоя относительно
    /// корня модели и общего предка в классе (у модели — вместе с поворотом корня FBX, это и есть
    /// «Z вверх» Blender); у остальных поворот предка переносится через локальные повороты узла.
    /// Но раскладка модели может не совпадать с позой покоя (щиты Kobold Pim прицепил к руке иначе,
    /// чем они стоят в игре, а правый щит у него — зеркальная копия левого), поэтому, если у узла
    /// есть сопоставленные дети, сверяем ещё и их позиции: когда поворот по умолчанию им явно
    /// противоречит, а какой-то другой ложится точно — берём его (у Projector Ring 3 это четыре Gubbin).
    /// </summary>
    private static Matrix4x4 TurnOf(Ctx ctx, Transform skinNode, Transform rest)
    {
        if (ctx.Turns.TryGetValue(skinNode, out Matrix4x4 cached)) return cached;

        Matrix4x4 turn;
        var (anchor, restAnchor) = MatchedAncestor(ctx, skinNode);
        if (anchor != null)
        {
            Quaternion lg = Quaternion.Inverse(restAnchor.rotation) * rest.rotation;
            Quaternion ls = Quaternion.Inverse(anchor.rotation) * skinNode.rotation;
            turn = SnapTo90(Matrix4x4.Rotate(Quaternion.Inverse(lg)) * TurnOf(ctx, anchor, restAnchor) * Matrix4x4.Rotate(ls));
        }
        else
        {
            Quaternion game = Quaternion.Inverse(ctx.Base.rotation) * rest.rotation;
            turn = SnapTo90(Matrix4x4.Rotate(Quaternion.Inverse(game) * skinNode.rotation));
        }

        Matrix4x4? byChildren = TurnByChildren(ctx, skinNode, rest, turn);
        if (byChildren.HasValue)
        {
            ctx.Report.AppendLine($"  ({skinNode.name}: по позициям детей{TurnText(byChildren.Value)} вместо{TurnText(turn)})");
            turn = byChildren.Value;
        }
        ctx.Turns[skinNode] = turn;
        return turn;
    }

    /// <summary>
    /// Перестановка осей, при которой позиции сопоставленных детей узла в модели ложатся на их позиции
    /// в игре. null — детей мало, позиции не определяют её однозначно или поворот по умолчанию им не хуже.
    /// </summary>
    private static Matrix4x4? TurnByChildren(Ctx ctx, Transform skinNode, Transform rest, Matrix4x4 fallback)
    {
        var skin = new List<Vector3>();
        var game = new List<Vector3>();
        foreach (var (child, restChild) in MatchedDescendants(ctx, skinNode))
        {
            Vector3 s = skinNode.InverseTransformPoint(child.position);
            Vector3 g = rest.InverseTransformPoint(restChild.position);
            if (s.magnitude < 0.01f || g.magnitude < 0.01f) continue;   // дети в центре узла поворот не покажут
            skin.Add(s);
            game.Add(g);
        }
        if (skin.Count < 2) return null;

        float size = game.Average(g => g.magnitude);
        float Error(Matrix4x4 m)
        {
            // Масштаб между моделью и игрой подбираем тут же: сравниваем только раскладку
            float dot = 0f, norm = 0f;
            for (int i = 0; i < skin.Count; i++)
            {
                Vector3 ms = m.MultiplyVector(skin[i]);
                dot += Vector3.Dot(ms, game[i]);
                norm += ms.sqrMagnitude;
            }
            float k = norm > 0f ? Mathf.Max(dot / norm, 0f) : 0f;
            float sum = 0f;
            for (int i = 0; i < skin.Count; i++) sum += (m.MultiplyVector(skin[i]) * k - game[i]).sqrMagnitude;
            return Mathf.Sqrt(sum / skin.Count) / size;
        }

        var ranked = AxisTurns().Select(m => (m, e: Error(m))).OrderBy(x => x.e).ToList();
        var (best, bestError) = ranked[0];
        float secondError = ranked[1].e;
        float fallbackError = Error(fallback);

        bool exact = bestError < 0.1f;                        // дети ложатся точно
        bool unique = secondError > 2f * bestError + 0.05f;   // и только при этой перестановке
        bool fallbackBad = fallbackError > 3f * bestError + 0.2f;
        if (!exact || !unique || !fallbackBad || IsIdentity(best * fallback.transpose)) return null;
        return SnapTo90(best);   // убрать шум float после LookRotation
    }

    /// <summary>
    /// Все потомки узла модели, у которых есть пара в классе. Не только ближайшие: у Inner Shield
    /// прямые дети (кольца, центр) стоят в его центре, а раскладку показывают Gubbin глубже.
    /// </summary>
    private static IEnumerable<(Transform skin, Transform rest)> MatchedDescendants(Ctx ctx, Transform node)
    {
        for (int i = 0; i < node.childCount; i++)
        {
            Transform child = node.GetChild(i);
            Transform rest = ctx.Index.Resolve(child.name);
            if (rest != null) yield return (child, rest);
            foreach (var pair in MatchedDescendants(ctx, child)) yield return pair;
        }
    }

    /// <summary>Ближайший предок узла модели, у которого есть пара в классе.</summary>
    private static (Transform skin, Transform rest) MatchedAncestor(Ctx ctx, Transform node)
    {
        for (Transform p = node.parent; p != null && p != ctx.SkinRoot; p = p.parent)
        {
            Transform rest = ctx.Index.Resolve(p.name);
            if (rest != null) return (p, rest);
        }
        return (null, null);
    }

    /// <summary>48 перестановок осей: 24 поворота и они же с отражением.</summary>
    private static IEnumerable<Matrix4x4> AxisTurns()
    {
        Vector3[] axes = { Vector3.right, Vector3.left, Vector3.up, Vector3.down, Vector3.forward, Vector3.back };
        foreach (Vector3 f in axes)
            foreach (Vector3 u in axes)
            {
                if (Mathf.Abs(Vector3.Dot(f, u)) > 0.5f) continue;
                Matrix4x4 r = Matrix4x4.Rotate(Quaternion.LookRotation(f, u));
                yield return r;
                yield return r * FlipX;
            }
    }

    private static Mesh Fitted(Ctx ctx, Mesh mesh, FitData fit)
    {
        Mesh copy = Object.Instantiate(mesh);
        copy.name = mesh.name;
        if (!fit.IsNone)
        {
            var verts = copy.vertices;
            for (int i = 0; i < verts.Length; i++) verts[i] = fit.Apply(verts[i]);
            copy.vertices = verts;
            var normals = copy.normals;
            for (int i = 0; i < normals.Length; i++) normals[i] = fit.Turn.MultiplyVector(normals[i]);
            if (normals.Length > 0) copy.normals = normals;
            var tangents = copy.tangents;
            for (int i = 0; i < tangents.Length; i++)
            {
                Vector3 d = fit.Turn.MultiplyVector(new Vector3(tangents[i].x, tangents[i].y, tangents[i].z));
                tangents[i] = new Vector4(d.x, d.y, d.z, fit.Mirror ? -tangents[i].w : tangents[i].w);
            }
            if (tangents.Length > 0) copy.tangents = tangents;
            // Отражение выворачивает грани наизнанку — меняем порядок вершин в треугольниках
            if (fit.Mirror)
                for (int sub = 0; sub < copy.subMeshCount; sub++)
                {
                    int[] tris = copy.GetTriangles(sub);
                    for (int i = 0; i + 2 < tris.Length; i += 3) (tris[i + 1], tris[i + 2]) = (tris[i + 2], tris[i + 1]);
                    copy.SetTriangles(tris, sub);
                }
            copy.RecalculateBounds();
        }
        copy.UploadMeshData(false);   // остаётся читаемым — модам это не мешает
        AssetDatabase.CreateAsset(copy, $"{ctx.Folder}/mesh{ctx.MeshCount++:D3}.asset");
        return copy;
    }

    /// <summary>
    /// Узел класса, которому соответствует корень модели: общий предок всех узлов с парой.
    /// У Kobold — «Anchors» (его масштаб 1.1 в модель не входит).
    /// </summary>
    private static Transform CommonAncestor(Transform skinRoot, NodeIndex index)
    {
        List<Transform> chain = null;
        foreach (Transform t in skinRoot.GetComponentsInChildren<Transform>(true))
        {
            var mf = t.GetComponent<MeshFilter>();
            if (t == skinRoot || mf == null || mf.sharedMesh == null) continue;
            Transform rest = index.Resolve(t.name);
            if (rest == null) continue;
            var path = new List<Transform>();
            for (Transform p = rest.parent; p != null; p = p.parent) path.Insert(0, p);
            if (chain == null) { chain = path; continue; }
            int n = 0;
            while (n < chain.Count && n < path.Count && chain[n] == path[n]) n++;
            chain.RemoveRange(n, chain.Count - n);
        }
        return chain != null && chain.Count > 0 ? chain[chain.Count - 1] : index.Root;
    }

    /// <summary>Ближайшая перестановка осей (столбцы — к ближайшим осям); если они совпали — исходная.</summary>
    private static Matrix4x4 SnapTo90(Matrix4x4 m)
    {
        Vector3 x = Snap(m.GetColumn(0)), y = Snap(m.GetColumn(1)), z = Snap(m.GetColumn(2));
        if (Mathf.Abs(Vector3.Dot(x, y)) > 0.5f || Mathf.Abs(Vector3.Dot(y, z)) > 0.5f || Mathf.Abs(Vector3.Dot(x, z)) > 0.5f) return m;
        var r = Matrix4x4.identity;
        r.SetColumn(0, x);
        r.SetColumn(1, y);
        r.SetColumn(2, z);
        return r;
    }

    private static Vector3 Snap(Vector3 v)
    {
        float ax = Mathf.Abs(v.x), ay = Mathf.Abs(v.y), az = Mathf.Abs(v.z);
        if (ax >= ay && ax >= az) return new Vector3(Mathf.Sign(v.x), 0, 0);
        if (ay >= az) return new Vector3(0, Mathf.Sign(v.y), 0);
        return new Vector3(0, 0, Mathf.Sign(v.z));
    }

    /// <summary>Путь узла от корня класса (без имени корня) — по нему моды находят узел в игре.</summary>
    private static string PathOf(Transform t, Transform root)
    {
        if (t == null || t == root) return "";
        string path = t.name;
        for (Transform p = t.parent; p != null && p != root; p = p.parent) path = p.name + "/" + path;
        return path;
    }

    // ---------------- дамп класса → поза покоя ----------------

    private static readonly Regex NodeLine = new Regex(
        @"^(?<pad> *)(?<name>.+?) \[L\d+(?:, off)?\] lp=\((?<lp>[^)]*)\) lr=\((?<lr>[^)]*)\) ls=\((?<ls>[^)]*)\)", RegexOptions.Compiled);

    /// <summary>Пустые объекты в позе покоя класса по дампу мода (KOBOLD.prefab.txt).</summary>
    private static GameObject BuildRest(string dump)
    {
        GameObject root = null;
        var stack = new List<Transform>();
        foreach (string line in File.ReadAllLines(dump))
        {
            Match m = NodeLine.Match(line);
            if (!m.Success) continue;
            int depth = m.Groups["pad"].Value.Length / 2;
            var go = new GameObject(m.Groups["name"].Value);
            go.hideFlags = HideFlags.HideAndDontSave;
            if (depth == 0)
            {
                root = go;
                stack.Clear();
            }
            else
            {
                if (depth > stack.Count) continue;   // битая строка
                go.transform.SetParent(stack[depth - 1], false);
                go.transform.localPosition = Vec(m.Groups["lp"].Value);
                go.transform.localEulerAngles = Vec(m.Groups["lr"].Value);
                go.transform.localScale = Vec(m.Groups["ls"].Value);
            }
            stack.RemoveRange(depth, stack.Count - depth);
            stack.Add(go.transform);
        }
        if (root == null) throw new Exception("пустой дамп " + dump);
        return root;
    }

    private static Vector3 Vec(string s)
    {
        string[] p = s.Split(',');
        return new Vector3(F(p[0]), F(p[1]), F(p[2]));
    }

    private static float F(string s) => float.Parse(s, NumberStyles.Float, CultureInfo.InvariantCulture);

    /// <summary>Узлы класса по именам и поиск узла по имени из модели скина (те же правила, что были в моде).</summary>
    private sealed class NodeIndex
    {
        // Имена в модели, которые в игре называются иначе (после снятия префикса LH/RH).
        private static readonly Dictionary<string, string> Aliases = new(StringComparer.OrdinalIgnoreCase)
        {
            { "Shield Center", "Center" },   // Kobold: центр щита
            { "Left Panel", "LeftPanel" },   // Kobold: левая панель правой руки — «RH LeftPanel» (с префиксом)
        };

        private static readonly (string prefix, string anchor)[] Sides =
        {
            ("LH ", "Left Hand Anchor"),
            ("RH ", "Right Hand Anchor"),
        };

        internal readonly Transform Root;
        private readonly Dictionary<string, List<Transform>> _nodes = new();

        internal NodeIndex(Transform root)
        {
            Root = root;
            foreach (Transform t in root.GetComponentsInChildren<Transform>(true))
            {
                if (t == root) continue;
                if (!_nodes.TryGetValue(t.name, out var list)) _nodes[t.name] = list = new List<Transform>();
                list.Add(t);
            }
        }

        /// <summary>Точное имя (если одно), затем без префикса LH/RH в своей руке, затем синонимы, затем первое совпадение.</summary>
        internal Transform Resolve(string name)
        {
            if (_nodes.TryGetValue(name, out var exact) && exact.Count == 1) return exact[0];

            foreach (var (prefix, anchor) in Sides)
            {
                if (!name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) continue;
                string bare = name.Substring(prefix.Length);
                Transform found = UnderAnchor(bare, anchor);
                if (found == null && Aliases.TryGetValue(bare, out string alias))
                    found = UnderAnchor(alias, anchor) ?? UnderAnchor(prefix + alias, anchor);
                if (found != null) return found;
            }
            return exact != null && exact.Count > 0 ? exact[0] : null;
        }

        private Transform UnderAnchor(string name, string anchor)
        {
            if (!_nodes.TryGetValue(name, out var list)) return null;
            foreach (Transform t in list)
                for (Transform p = t.parent; p != null && p != Root; p = p.parent)
                    if (p.name == anchor) return t;
            return null;
        }
    }

    // ---------------- импорт ----------------

    /// <summary>Имя бандла — латиница и цифры из имени скина (Unity приводит имена бандлов к нижнему регистру).</summary>
    private static string BundleName(string skin)
    {
        var sb = new StringBuilder();
        foreach (char c in skin.ToLowerInvariant())
            sb.Append(c < 128 && char.IsLetterOrDigit(c) ? c : '_');
        uint hash = 2166136261;   // FNV-1a: русские имена дают одинаковые «_», хэш их различает
        foreach (char c in skin) hash = (hash ^ c) * 16777619;
        return "skin_" + sb + "_" + hash.ToString("x8");
    }

    private static void ConfigureImporter(string path)
    {
        var importer = (ModelImporter)AssetImporter.GetAtPath(path);
        if (importer == null) return;
        bool dirty = false;
        void Set<T>(T now, T want, Action apply)
        {
            if (EqualityComparer<T>.Default.Equals(now, want)) return;
            apply();
            dirty = true;
        }
        Set(importer.importAnimation, false, () => importer.importAnimation = false);
        Set(importer.animationType, ModelImporterAnimationType.None, () => importer.animationType = ModelImporterAnimationType.None);
        Set(importer.importCameras, false, () => importer.importCameras = false);
        Set(importer.importLights, false, () => importer.importLights = false);
        Set(importer.importBlendShapes, false, () => importer.importBlendShapes = false);
        Set(importer.importNormals, ModelImporterNormals.Import, () => importer.importNormals = ModelImporterNormals.Import);
        Set(importer.meshCompression, ModelImporterMeshCompression.Off, () => importer.meshCompression = ModelImporterMeshCompression.Off);
        // Меши встают на узлы игры без трансформов модели, поэтому вершины нужны в метрах как в Blender:
        // перевод единиц FBX ужал бы их в 100 раз (Blender компенсирует это масштабом 100 на корне).
        Set(importer.useFileScale, false, () => importer.useFileScale = false);
        Set(importer.globalScale, 1f, () => importer.globalScale = 1f);
        // Сборщик переводит вершины в оси узла игры — меш должен читаться.
        Set(importer.isReadable, true, () => importer.isReadable = true);
        // Материалы в игре всё равно берутся у класса — из FBX нужны только слоты.
        Set(importer.materialImportMode, ModelImporterMaterialImportMode.ImportStandard, () => importer.materialImportMode = ModelImporterMaterialImportMode.ImportStandard);
        if (dirty) importer.SaveAndReimport();
    }

    private static void MakePrefab(string model, string prefab)
    {
        GameObject asset = AssetDatabase.LoadAssetAtPath<GameObject>(model);
        var instance = (GameObject)PrefabUtility.InstantiatePrefab(asset);
        instance.name = Path.GetFileNameWithoutExtension(prefab);
        PrefabUtility.SaveAsPrefabAsset(instance, prefab);
        Object.DestroyImmediate(instance);
        Debug.Log("[Skins] Создан префаб " + prefab);
    }

    private static string V(Vector3 v) => string.Format(CultureInfo.InvariantCulture, "({0:F3},{1:F3},{2:F3})", v.x, v.y, v.z);
}
