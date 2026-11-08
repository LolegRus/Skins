using System.IO;
using TMPro;
using UnityEditor;
using UnityEngine;
using UnityEngine.TextCore.LowLevel;

/// <summary>
/// Пиксельный шрифт меню скинов: Pixelify Sans (Assets/Fonts, лицензия OFL — PixelifySans-OFL.txt).
/// Quest: шрифт TextMeshPro со статическим атласом (ASCII) → бандл Android → Quest mods\Skins\build\MenuFont.font
/// (идёт в .qmod через fileCopies mod.json). Мод сам заменит шейдер материала на шейдер шрифта игры:
/// в этом проекте нет XR, и шейдер из бандла рисовался бы в шлеме одним глазом.
/// ПК: сам TTF (Font для IMGUI) → бандл Windows → Skins\MenuFont.font (зашивается в DLL).
///
/// Командная строка:
///   Unity.exe -batchmode -nographics -projectPath Unity -executeMethod MenuFontBuilder.Build -quit
/// </summary>
public static class MenuFontBuilder
{
    private const string Source = "Assets/Fonts/PixelifySans.ttf";
    private const string License = "Assets/Fonts/PixelifySans-OFL.txt";
    private const string Asset = "Assets/Fonts/PixelifySans SDF.asset";   // в бандле — «PixelifySans SDF»; генерируется
    private const string Essentials = "Packages/com.unity.ugui/Package Resources/TMP Essential Resources.unitypackage";
    private const string OutDir = "Build/AndroidFont";
    private const string QuestBuildDir = @"..\..\Quest mods\Skins\build";
    private const string PcOutDir = "Build/WindowsFont";
    private const string PcDir = @"..\Skins";   // Skins\MenuFont.font — EmbeddedResource ПК-мода
    private const string BundleName = "menufont";
    private const string FileName = "MenuFont.font";

    [MenuItem("Tools/Skins/Build Menu Font")]
    public static void Build()
    {
        // Шейдеры и настройки TMP (Assets/TextMesh Pro, в git не лежат) — из пакета ugui; без них
        // CreateFontAsset не создаст материал. В batch-режиме ImportPackage не успевает до выхода —
        // импорт отдельным запуском: Unity.exe -batchmode -projectPath Unity -importPackage "<Essentials>" -quit
        if (Shader.Find("TextMeshPro/Mobile/Distance Field") == null)
        {
            if (Application.isBatchMode)
            {
                Debug.LogError("[Skins] Нет TMP Essential Resources. Импортируй: Unity.exe -batchmode -projectPath Unity " +
                               $"-importPackage \"{Path.GetFullPath(Essentials)}\" -quit — и запусти снова");
                return;
            }
            AssetDatabase.ImportPackage(Essentials, false);
            Debug.Log("[Skins] TMP Essential Resources импортируются — запусти Build Menu Font ещё раз");
            return;
        }

        var font = AssetDatabase.LoadAssetAtPath<Font>(Source);
        if (font == null) { Debug.LogError($"[Skins] Нет {Source}"); return; }

        // 64 пункта и поле 8 — с запасом для крупного меню; ASCII + неразрывный пробел
        var asset = TMP_FontAsset.CreateFontAsset(font, 64, 8, GlyphRenderMode.SDFAA, 1024, 1024,
            AtlasPopulationMode.Dynamic, false);
        if (asset == null) { Debug.LogError("[Skins] TMP не создал шрифт"); return; }
        string chars = " ";
        for (char c = ' '; c <= '~'; c++) chars += c;
        if (!asset.TryAddCharacters(chars, out string missing) && !string.IsNullOrEmpty(missing))
            Debug.LogWarning($"[Skins] В шрифте нет символов: {missing}");
        asset.atlasPopulationMode = AtlasPopulationMode.Static;
        asset.name = Path.GetFileNameWithoutExtension(Asset);

        AssetDatabase.DeleteAsset(Asset);
        AssetDatabase.CreateAsset(asset, Asset);
        var atlas = asset.atlasTextures[0];
        atlas.name = asset.name + " Atlas";
        AssetDatabase.AddObjectToAsset(atlas, asset);
        asset.material.name = asset.name + " Material";
        AssetDatabase.AddObjectToAsset(asset.material, asset);
        EditorUtility.SetDirty(asset);
        AssetDatabase.SaveAssets();
        Debug.Log($"[Skins] Шрифт: {asset.characterTable.Count} символов, атлас {atlas.width}×{atlas.height}");

        Directory.CreateDirectory(OutDir);
        var build = new AssetBundleBuild { assetBundleName = BundleName, assetNames = new[] { Asset } };
        var manifest = BuildPipeline.BuildAssetBundles(OutDir, new[] { build },
            BuildAssetBundleOptions.ChunkBasedCompression, BuildTarget.Android);
        if (manifest == null) { Debug.LogError("[Skins] Бандл шрифта не собрался"); return; }

        // ПК: меню F2 на IMGUI — ему нужен обычный Font (TTF с данными шрифта); надписи на терминале в хабе —
        // TextMeshPro, ему — тот же SDF-шрифт, что на Quest. Бандл зашивается в DLL
        Directory.CreateDirectory(PcOutDir);
        var pc = new AssetBundleBuild { assetBundleName = BundleName, assetNames = new[] { Source, Asset } };
        if (BuildPipeline.BuildAssetBundles(PcOutDir, new[] { pc }, BuildAssetBundleOptions.ChunkBasedCompression,
                BuildTarget.StandaloneWindows64) == null)
            Debug.LogError("[Skins] Бандл шрифта для ПК не собрался");
        else if (Directory.Exists(PcDir))
        {
            File.Copy(Path.Combine(PcOutDir, BundleName), Path.Combine(PcDir, FileName), true);
            Debug.Log($"[Skins] {FileName} (ПК) → {Path.GetFullPath(PcDir)}");
        }

        if (!Directory.Exists(QuestBuildDir))
        {
            Debug.LogWarning($"[Skins] Нет {Path.GetFullPath(QuestBuildDir)} — бандл остался в {Path.GetFullPath(OutDir)}");
            return;
        }
        File.Copy(Path.Combine(OutDir, BundleName), Path.Combine(QuestBuildDir, FileName), true);
        File.Copy(License, Path.Combine(QuestBuildDir, Path.GetFileName(License)), true);   // OFL: лицензия — вместе со шрифтом
        Debug.Log($"[Skins] {FileName} → {Path.GetFullPath(QuestBuildDir)}");
    }
}
