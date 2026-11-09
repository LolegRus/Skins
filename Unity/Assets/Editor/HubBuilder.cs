using System.Collections.Generic;
using System.Globalization;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/// <summary>
/// Терминал скинов для хаба (модель Cinder, Assets/Hub/terminal.fbx) → бандл Windows → Skins\Hub.bundle
/// (зашивается в DLL ПК-мода). В бандле — префаб «Terminal»: монитор, кабели, голо-пьедестал (на нём мод
/// будет крутить голограмму скина). Оси модели: +Z — в комнату, Y — вверх, начало координат — на полу
/// под центром пьедестала, центр монитора по X = 0.
/// Материалы мод ставит сам (материал класса игры: вершинные цвета, альфа 0 — цвет команды), из FBX нужны только слоты.
///
/// Командная строка:
///   Unity.exe -batchmode -nographics -projectPath Unity -executeMethod HubBuilder.Build -quit
/// </summary>
public static class HubBuilder
{
    private const string Model = "Assets/Hub/terminal.fbx";
    private const string Prefab = "Assets/Hub/_build/Terminal.prefab";
    private const string OutDir = "Build/WindowsHub";
    private const string PcDir = @"..\Skins";   // Skins\Hub.bundle — EmbeddedResource ПК-мода
    private const string BundleName = "hub";
    private const string FileName = "Hub.bundle";

    [MenuItem("Tools/Skins/Build Hub Terminal")]
    public static void Build()
    {
        var importer = (ModelImporter)AssetImporter.GetAtPath(Model);
        if (importer == null) { Debug.LogError($"[Skins] Нет {Model}"); return; }
        importer.importAnimation = false;
        importer.animationType = ModelImporterAnimationType.None;
        importer.importCameras = false;
        importer.importLights = false;
        importer.importBlendShapes = false;
        importer.importNormals = ModelImporterNormals.Import;
        importer.meshCompression = ModelImporterMeshCompression.Off;
        importer.useFileScale = true;   // сантиметры FBX → метры
        importer.globalScale = 1f;
        importer.isReadable = true;
        importer.SaveAndReimport();

        var source = AssetDatabase.LoadAssetAtPath<GameObject>(Model);
        var instance = (GameObject)PrefabUtility.InstantiatePrefab(source);
        PrefabUtility.UnpackPrefabInstance(instance, PrefabUnpackMode.Completely, InteractionMode.AutomatedAction);
        instance.name = "Terminal";
        instance.transform.SetPositionAndRotation(Vector3.zero, Quaternion.identity);
        instance.transform.localScale = Vector3.one;

        // Размеры — в лог: по ним видно, что единицы и оси сошлись (монитор ~2 м, пол на y = 0)
        foreach (var r in instance.GetComponentsInChildren<MeshRenderer>(true))
        {
            Bounds b = r.bounds;
            Debug.Log($"[Skins] {r.name}: {V(b.min)}..{V(b.max)}");
            r.sharedMaterials = new Material[r.sharedMaterials.Length];
        }

        Directory.CreateDirectory(Path.GetDirectoryName(Prefab));
        PrefabUtility.SaveAsPrefabAsset(instance, Prefab);
        Object.DestroyImmediate(instance);
        BuildMirrorParts();
        BuildFurniture();

        Directory.CreateDirectory(OutDir);
        var build = new AssetBundleBuild { assetBundleName = BundleName, assetNames = new[] { Prefab, MirrorPrefab, FurniturePrefab } };
        if (BuildPipeline.BuildAssetBundles(OutDir, new[] { build }, BuildAssetBundleOptions.ChunkBasedCompression,
                BuildTarget.StandaloneWindows64) == null)
        {
            Debug.LogError("[Skins] Бандл терминала не собрался");
            return;
        }
        File.Copy(Path.Combine(OutDir, BundleName), Path.Combine(PcDir, FileName), true);
        Debug.Log($"[Skins] {FileName} → {Path.GetFullPath(PcDir)}");

        // Quest: тот же префаб под Android → Quest mods\Skins\build\Hub.bundle (в .qmod через fileCopies mod.json)
        Directory.CreateDirectory(AndroidOutDir);
        if (BuildPipeline.BuildAssetBundles(AndroidOutDir, new[] { build }, BuildAssetBundleOptions.ChunkBasedCompression,
                BuildTarget.Android) == null)
        {
            Debug.LogError("[Skins] Бандл терминала для Quest не собрался");
            return;
        }
        if (!Directory.Exists(QuestBuildDir))
        {
            Debug.LogWarning($"[Skins] Нет {Path.GetFullPath(QuestBuildDir)} — бандл остался в {Path.GetFullPath(AndroidOutDir)}");
            return;
        }
        File.Copy(Path.Combine(AndroidOutDir, BundleName), Path.Combine(QuestBuildDir, FileName), true);
        Debug.Log($"[Skins] {FileName} (Quest) → {Path.GetFullPath(QuestBuildDir)}");
    }

    private const string AndroidOutDir = "Build/AndroidHub";
    private const string QuestBuildDir = @"..\..\Quest mods\Skins\build";

    // Детали зеркала для Quest (там меш из кода собирать неудобно): префаб «Mirror» с детьми «Glass» —
    // квад 1 × 1 лицом к -Z с отражённой по горизонтали развёрткой — и «Frame» — куб 1 × 1 × 1.
    // Размер мод задаёт масштабом. ПК-мод их не берёт: у него свои меши (Hub.cs, Meshes)
    private const string MirrorPrefab = "Assets/Hub/_build/Mirror.prefab";

    // Мебель в пещере — из сцены Assets/Hub/HubCave.unity (Tools → Skins → Hub Cave Scene): всё в корне сцены и
    // внутри «Furniture», кроме подложки «Cave (reference)», света, метки и объектов с тегом EditorOnly. Позиции —
    // мировые координаты хаба: префаб «Furniture» ставится модом в (0, 0, 0) без поворота. Материалы мод ставит сам
    // (материал тела игры, цвета вершин); меши — читаемые: мод вешает на них MeshCollider
    private const string FurniturePrefab = "Assets/Hub/_build/Furniture.prefab";
    private const string CaveScene = "Assets/Hub/HubCave.unity";

    private static void BuildFurniture()
    {
        var scene = EditorSceneManager.GetSceneByPath(CaveScene);
        bool opened = false;
        if (!scene.isLoaded)
        {
            scene = EditorSceneManager.OpenScene(CaveScene, OpenSceneMode.Additive);
            opened = true;
        }
        // Корни сцены — до того, как появится сборочный объект (иначе он попал бы в обход и копировался бы в себя)
        var sceneRoots = scene.GetRootGameObjects();
        var root = new GameObject("Furniture (build)");
        root.hideFlags = HideFlags.HideAndDontSave;
        int count = 0;
        void Take(GameObject go)
        {
            if (!go.activeInHierarchy || go.CompareTag("EditorOnly")) return;
            if (go.name == "Furniture")
            {
                foreach (Transform c in go.transform) Take(c.gameObject);
                return;
            }
            if (go.name == "Cave (reference)" || go.GetComponent<Light>() != null || go.name.StartsWith("Spot")) return;
            var copy = Object.Instantiate(go);
            copy.name = go.name;
            copy.transform.SetPositionAndRotation(go.transform.position, go.transform.rotation);
            copy.transform.localScale = go.transform.lossyScale;
            copy.transform.SetParent(root.transform, true);
            foreach (var r in copy.GetComponentsInChildren<Renderer>(true))
            {
                r.sharedMaterials = new Material[r.sharedMaterials.Length];
                var mf = r.GetComponent<MeshFilter>();
                string mp = mf != null && mf.sharedMesh != null ? AssetDatabase.GetAssetPath(mf.sharedMesh) : null;
                if (mp != null && AssetImporter.GetAtPath(mp) is ModelImporter mi && !mi.isReadable)
                {
                    mi.isReadable = true;
                    mi.SaveAndReimport();
                }
            }
            foreach (var c in copy.GetComponentsInChildren<Collider>(true)) Object.DestroyImmediate(c);
            Debug.Log($"[Skins] Мебель: {go.name} в {V(go.transform.position)}");
            count++;
        }
        var lights = new List<Light>();
        foreach (var go in sceneRoots)
            foreach (var l in go.GetComponentsInChildren<Light>(false))
                if (!l.CompareTag("EditorOnly") && l.enabled && l.type != LightType.Directional) lights.Add(l);
        foreach (var go in sceneRoots) Take(go);
        BakeLight(root, lights);
        root.hideFlags = HideFlags.None;
        root.name = "Furniture";
        PrefabUtility.SaveAsPrefabAsset(root, FurniturePrefab);
        Object.DestroyImmediate(root);
        if (opened) EditorSceneManager.CloseScene(scene, true);
        Debug.Log($"[Skins] Мебель: {count} предметов → {FurniturePrefab}");
    }

    private static void BuildMirrorParts()
    {
        var quad = new Mesh { name = "Mirror Glass" };
        quad.vertices = new[] { new Vector3(-0.5f, -0.5f, 0), new Vector3(-0.5f, 0.5f, 0), new Vector3(0.5f, 0.5f, 0), new Vector3(0.5f, -0.5f, 0) };
        quad.uv = new[] { new Vector2(1, 0), new Vector2(1, 1), new Vector2(0, 1), new Vector2(0, 0) };
        quad.triangles = new[] { 0, 1, 2, 0, 2, 3 };
        quad.RecalculateNormals();
        quad.RecalculateBounds();
        AssetDatabase.CreateAsset(quad, "Assets/Hub/_build/MirrorGlass.asset");

        var cube = GameObject.CreatePrimitive(PrimitiveType.Cube);
        Mesh box = Object.Instantiate(cube.GetComponent<MeshFilter>().sharedMesh);
        Object.DestroyImmediate(cube);
        box.name = "Mirror Frame";
        AssetDatabase.CreateAsset(box, "Assets/Hub/_build/MirrorFrame.asset");

        var root = new GameObject("Mirror");
        foreach (var (name, mesh) in new[] { ("Glass", quad), ("Frame", box) })
        {
            var part = new GameObject(name);
            part.transform.SetParent(root.transform, false);
            part.AddComponent<MeshFilter>().sharedMesh = mesh;
            part.AddComponent<MeshRenderer>().sharedMaterials = new Material[1];
        }
        PrefabUtility.SaveAsPrefabAsset(root, MirrorPrefab);
        Object.DestroyImmediate(root);
    }

    // ---- свет мебели ----
    // Хаб освещён запечённым светом (лайтмапы), а шейдер тела игры живой свет почти не берёт — мебель в пещере
    // выходила чёрной. Поэтому свет «запекается» в цвета вершин копий мешей: тёплый полумрак + Point/Spot Light из
    // сцены HubCave (без тега EditorOnly; в бандл они не идут). Нет ламп — один «свет лавы» из ямы. Альфа 0 (цвет
    // команды) не трогается
    private static readonly Color Ambient = new Color(0.9f, 0.85f, 0.8f);   // основной свет даёт лайтмапа пола (мод), здесь — только оттенок
    private static readonly Vector3 LavaAt = new Vector3(-17.4f, -24.2f, -67.6f);
    private const string BakedDir = "Assets/Hub/_build/Furniture";

    // Модели, у которых часть граней смотрит внутрь (перевёрнутые нормали): разворачиваем их наружу при сборке.
    // Годится для выпуклых, почти коробочных деталей (холодильник); диван и т. п. сюда не добавлять
    private static readonly HashSet<string> FacesOut = new();   // холодильник Cinder переделал — правка не нужна

    private static string TopName(Transform t, Transform root)
    {
        while (t.parent != null && t.parent != root) t = t.parent;
        return t.name;
    }

    // Грань, чья нормаль смотрит к середине детали, — внутрь: меняем обход, нормали пересчитываем
    private static void FixInwardFaces(Mesh mesh, string part)
    {
        Vector3[] v = mesh.vertices;
        Vector3 center = mesh.bounds.center;
        int flipped = 0, total = 0;
        for (int sub = 0; sub < mesh.subMeshCount; sub++)
        {
            int[] t = mesh.GetTriangles(sub);
            for (int k = 0; k + 2 < t.Length; k += 3)
            {
                Vector3 a = v[t[k]], b = v[t[k + 1]], c = v[t[k + 2]];
                Vector3 n = Vector3.Cross(b - a, c - a);
                total++;
                if (Vector3.Dot(n, (a + b + c) / 3f - center) >= 0f) continue;
                (t[k + 1], t[k + 2]) = (t[k + 2], t[k + 1]);
                flipped++;
            }
            mesh.SetTriangles(t, sub);
        }
        if (flipped > 0) mesh.RecalculateNormals();
        Debug.Log($"[Skins] Мебель: {part} — развёрнуто наружу граней {flipped} из {total}");
    }

    private static void BakeLight(GameObject root, List<Light> lights)
    {
        var src = new List<(Vector3 pos, Color color, float range, float intensity)>();
        foreach (var l in lights) src.Add((l.transform.position, l.color, l.range, l.intensity));
        if (src.Count == 0) src.Add((LavaAt, new Color(1f, 0.5f, 0.2f), 14f, 0.9f));
        Debug.Log($"[Skins] Мебель: свет — {(lights.Count > 0 ? lights.Count + " ламп из сцены" : "лава по умолчанию")}");

        Directory.CreateDirectory(BakedDir);
        int n = 0;
        foreach (var mf in root.GetComponentsInChildren<MeshFilter>(true))
        {
            Mesh m = mf.sharedMesh;
            if (m == null) continue;
            var mesh = Object.Instantiate(m);
            mesh.name = m.name;
            Vector3[] v = mesh.vertices;
            Vector3[] nr = mesh.normals;
            if (nr == null || nr.Length != v.Length) { mesh.RecalculateNormals(); nr = mesh.normals; }
            Color[] c = mesh.colors;
            if (c == null || c.Length != v.Length) { c = new Color[v.Length]; for (int i = 0; i < c.Length; i++) c[i] = new Color(0.6f, 0.6f, 0.6f, 1); }
            Matrix4x4 w = mf.transform.localToWorldMatrix, wn = w.inverse.transpose;
            for (int i = 0; i < v.Length; i++)
            {
                if (c[i].a < 0.5f) continue;   // цвет команды
                Vector3 p = w.MultiplyPoint3x4(v[i]), nn = wn.MultiplyVector(nr[i]).normalized;
                Color light = Ambient;
                foreach (var (pos, color, range, intensity) in src)
                {
                    Vector3 d = pos - p;
                    float dist = d.magnitude;
                    if (dist >= range) continue;
                    float att = 1f - dist / range;
                    float lambert = 0.35f + 0.65f * Mathf.Max(0f, Vector3.Dot(nn, d / Mathf.Max(dist, 1e-4f)));
                    light += color * (intensity * att * att * lambert);
                }
                c[i] = new Color(Mathf.Clamp01(c[i].r * light.r), Mathf.Clamp01(c[i].g * light.g), Mathf.Clamp01(c[i].b * light.b), c[i].a);
            }
            mesh.colors = c;
            if (FacesOut.Contains(TopName(mf.transform, root.transform))) FixInwardFaces(mesh, mf.name);
            string path = $"{BakedDir}/{n++:D2} {mf.name}.asset";
            AssetDatabase.DeleteAsset(path);
            AssetDatabase.CreateAsset(mesh, path);
            mf.sharedMesh = mesh;
        }
    }

    private static string V(Vector3 v) => string.Format(CultureInfo.InvariantCulture, "({0:F3},{1:F3},{2:F3})", v.x, v.y, v.z);
}
