using System.Globalization;
using System.IO;
using UnityEditor;
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

        Directory.CreateDirectory(OutDir);
        var build = new AssetBundleBuild { assetBundleName = BundleName, assetNames = new[] { Prefab } };
        if (BuildPipeline.BuildAssetBundles(OutDir, new[] { build }, BuildAssetBundleOptions.ChunkBasedCompression,
                BuildTarget.StandaloneWindows64) == null)
        {
            Debug.LogError("[Skins] Бандл терминала не собрался");
            return;
        }
        File.Copy(Path.Combine(OutDir, BundleName), Path.Combine(PcDir, FileName), true);
        Debug.Log($"[Skins] {FileName} → {Path.GetFullPath(PcDir)}");
    }

    private static string V(Vector3 v) => string.Format(CultureInfo.InvariantCulture, "({0:F3},{1:F3},{2:F3})", v.x, v.y, v.z);
}
