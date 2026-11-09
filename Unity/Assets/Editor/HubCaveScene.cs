using System.Collections.Generic;
using System.Globalization;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;

/// <summary>
/// Сцена Assets/Hub/HubCave.unity — пещера у лавы в хабе, чтобы расставить в ней мебель Cinder.
/// «Cave (reference)» — кусок хаба из файлов игры (Unity\HubReference\hub_cave.obj, его делает export_hub_area.py)
/// в мировых координатах игры, с цветами вершин. Только подложка: в бандл не идёт, двигать не нужно.
/// «Furniture» стоит в (0, 0, 0) без поворота — что лежит под ним, то и встанет в хабе (сборщик бандла и мод — потом).
/// Сцену можно пересоздать: подложка собирается заново, «Furniture» с мебелью остаётся.
/// </summary>
public static class HubCaveScene
{
    private const string Obj = "HubReference/hub_cave.obj";
    private const string MeshDir = "Assets/Hub/Reference/Cave";
    private const string ScenePath = "Assets/Hub/HubCave.unity";
    private const string ShaderPath = "Assets/Hub/Reference/VertexColor.shader";
    // Точка, где игрок нажал Ctrl+F12 (лог «Место: … точка …») — туда смотрит камера сцены
    private static readonly Vector3 Spot = new Vector3(-14.32f, -17.21f, -71.12f);

    [MenuItem("Tools/Skins/Hub Cave Scene")]
    public static void Build()
    {
        if (!File.Exists(Obj)) { Debug.LogError($"[Skins] Нет {Obj} — сначала export_hub_area.py"); return; }
        var shader = AssetDatabase.LoadAssetAtPath<Shader>(ShaderPath);
        var mat = new Material(shader) { name = "Cave Vertex Color" };
        Directory.CreateDirectory(MeshDir);
        AssetDatabase.CreateAsset(mat, MeshDir + "/Cave.mat");

        var scene = File.Exists(ScenePath)
            ? EditorSceneManager.OpenScene(ScenePath)
            : EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

        foreach (var go in scene.GetRootGameObjects())
            if (go.name == "Cave (reference)") Object.DestroyImmediate(go);
        var cave = new GameObject("Cave (reference)");
        cave.tag = "EditorOnly";   // в сборку не попадёт

        int count = 0, tris = 0;
        foreach (var (name, verts, colors, faces) in Read(Obj))
        {
            var mesh = new Mesh { name = name, indexFormat = verts.Count > 65000 ? IndexFormat.UInt32 : IndexFormat.UInt16 };
            mesh.SetVertices(verts);
            mesh.SetColors(colors);
            mesh.SetTriangles(faces, 0);
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
            AssetDatabase.CreateAsset(mesh, $"{MeshDir}/{count:D3} {Safe(name)}.asset");
            var part = new GameObject(name);
            part.transform.SetParent(cave.transform, false);
            part.AddComponent<MeshFilter>().sharedMesh = mesh;
            part.AddComponent<MeshRenderer>().sharedMaterial = mat;
            part.AddComponent<MeshCollider>().sharedMesh = mesh;   // для привязки к поверхности (V / Shift+Ctrl)
            count++;
            tris += faces.Count / 3;
        }

        GameObject furniture = null;
        foreach (var go in scene.GetRootGameObjects())
            if (go.name == "Furniture") furniture = go;
        if (furniture == null)
        {
            furniture = new GameObject("Furniture");
            furniture.transform.SetPositionAndRotation(Vector3.zero, Quaternion.identity);
        }
        GameObject marker = GameObject.Find("Spot (Ctrl+F12)");
        if (marker == null)
        {
            marker = new GameObject("Spot (Ctrl+F12)");
            marker.tag = "EditorOnly";
        }
        marker.transform.position = Spot;

        if (Object.FindFirstObjectByType<Light>() == null)
        {
            var light = new GameObject("Light").AddComponent<Light>();
            light.type = LightType.Directional;
            light.transform.rotation = Quaternion.Euler(50, -30, 0);
            light.gameObject.tag = "EditorOnly";
        }

        EditorSceneManager.SaveScene(scene, ScenePath);
        AssetDatabase.SaveAssets();
        if (!Application.isBatchMode && SceneView.lastActiveSceneView != null)
        {
            SceneView.lastActiveSceneView.LookAt(Spot, Quaternion.Euler(35, -150, 0), 12f);
            Selection.activeGameObject = furniture;
        }
        Debug.Log($"[Skins] Пещера: {count} объектов, {tris} треугольников → {ScenePath}");
    }

    private static string Safe(string s)
    {
        foreach (char c in Path.GetInvalidFileNameChars()) s = s.Replace(c, '_');
        return s;
    }

    // OBJ из export_hub_area.py: «o имя», «v x y z r g b» (x зеркально), «f a b c» (обход для правой системы)
    private static IEnumerable<(string, List<Vector3>, List<Color>, List<int>)> Read(string path)
    {
        string name = null;
        var allV = new List<Vector3>();
        var allC = new List<Color>();
        List<int> faces = null;
        var map = new Dictionary<int, int>();
        List<Vector3> verts = null;
        List<Color> cols = null;
        var ci = CultureInfo.InvariantCulture;
        foreach (string line in File.ReadLines(path))
        {
            if (line.StartsWith("o "))
            {
                if (faces != null && faces.Count > 0) yield return (name, verts, cols, faces);
                name = line.Substring(2).Replace('_', ' ');
                verts = new List<Vector3>(); cols = new List<Color>(); faces = new List<int>(); map.Clear();
            }
            else if (line.StartsWith("v "))
            {
                var p = line.Split(' ');
                allV.Add(new Vector3(-float.Parse(p[1], ci), float.Parse(p[2], ci), float.Parse(p[3], ci)));   // назад в оси игры
                allC.Add(p.Length >= 7 ? new Color(float.Parse(p[4], ci), float.Parse(p[5], ci), float.Parse(p[6], ci), 1) : Color.gray);
            }
            else if (line.StartsWith("f ") && faces != null)
            {
                var p = line.Split(' ');
                var idx = new int[3];
                for (int k = 0; k < 3; k++)
                {
                    int g = int.Parse(p[1 + k].Split('/')[0], ci) - 1;
                    if (!map.TryGetValue(g, out int l))
                    {
                        l = verts.Count;
                        map[g] = l;
                        verts.Add(allV[g]);
                        cols.Add(allC[g]);
                    }
                    idx[k] = l;
                }
                faces.Add(idx[0]); faces.Add(idx[2]); faces.Add(idx[1]);   // зеркало по x — обход обратно
            }
        }
        if (faces != null && faces.Count > 0) yield return (name, verts, cols, faces);
    }
}
