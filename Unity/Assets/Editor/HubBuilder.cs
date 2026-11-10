using System.Globalization;
using System.IO;
using UnityEditor;
using UnityEngine;

/// <summary>
/// Терминал скинов для хаба (модель Cinder, Assets/Hub/terminal.fbx) → бандл Windows → Skins\Hub.bundle
/// (зашивается в DLL ПК-мода) и бандл Android → Quest mods\Skins\build\Hub.bundle. В бандле — префаб «Terminal»:
/// монитор, кабели, голо-пьедестал (на нём мод крутит голограмму скина) — и «Mirror» (детали зеркала для Quest).
/// Оси модели: +Z — в комнату, Y — вверх, начало координат — на полу под центром пьедестала, центр монитора по X = 0.
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

        Directory.CreateDirectory(OutDir);
        var build = new AssetBundleBuild { assetBundleName = BundleName, assetNames = new[] { Prefab, MirrorPrefab } };
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

    private static string V(Vector3 v) => string.Format(CultureInfo.InvariantCulture, "({0:F3},{1:F3},{2:F3})", v.x, v.y, v.z);
}
