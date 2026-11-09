using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

/// <summary>
/// .glb → префаб (Tools → Skins → Import GLB, выдели .glb в Project). Unity сама .glb не читает.
/// Рядом с файлом: «&lt;имя&gt;.asset» (меш), «&lt;имя&gt;.prefab». Цвет текстуры переносится в цвета вершин
/// (игра красит модели ими — на ПК и на Quest, текстуры там не нужны), материал — Skins/Vertex Color, как в игре.
/// Поддержано то, что дают генераторы вроде Tripo: меши из треугольников, POSITION/NORMAL/TEXCOORD_0,
/// baseColorTexture (встроенная картинка или файл рядом), позы узлов. Оси glTF (правые) → Unity (левые): x зеркально.
/// </summary>
public static class GlbImport
{
    private const string VertexColorMaterial = "Assets/Hub/Reference/VertexColor.mat";

    [MenuItem("Tools/Skins/Import GLB")]
    public static void ImportSelected()
    {
        string path = AssetDatabase.GetAssetPath(Selection.activeObject);
        if (string.IsNullOrEmpty(path) || !path.EndsWith(".glb", StringComparison.OrdinalIgnoreCase))
        {
            EditorUtility.DisplayDialog("Import GLB", "Выдели .glb-файл в окне Project.", "OK");
            return;
        }
        Import(path);
    }

    [MenuItem("Tools/Skins/Import GLB", true)]
    private static bool CanImport() =>
        AssetDatabase.GetAssetPath(Selection.activeObject).EndsWith(".glb", StringComparison.OrdinalIgnoreCase);

    public static GameObject Import(string path)
    {
        byte[] data = File.ReadAllBytes(path);
        if (data.Length < 20 || Encoding.ASCII.GetString(data, 0, 4) != "glTF") throw new Exception("не GLB");
        int jsonLen = BitConverter.ToInt32(data, 12);
        var json = (Dictionary<string, object>)MiniJson.Parse(Encoding.UTF8.GetString(data, 20, jsonLen));
        int binStart = 20 + jsonLen + 8;

        string dir = Path.GetDirectoryName(path).Replace('\\', '/');
        string name = Path.GetFileNameWithoutExtension(path);
        var gltf = new Gltf(json, data, binStart, dir);

        // Все меши всех узлов — в один меш, в осях корня (так проще ставить и красить)
        var verts = new List<Vector3>();
        var normals = new List<Vector3>();
        var colors = new List<Color>();
        var tris = new List<int>();
        foreach (int root in gltf.Roots())
            gltf.Collect(root, Matrix4x4.identity, verts, normals, colors, tris);
        if (verts.Count == 0) throw new Exception("в GLB нет мешей");

        var mesh = new Mesh { name = name };
        if (verts.Count > 65000) mesh.indexFormat = UnityEngine.Rendering.IndexFormat.UInt32;
        mesh.SetVertices(verts);
        mesh.SetColors(colors);
        mesh.SetTriangles(tris, 0);
        if (normals.Count == verts.Count) mesh.SetNormals(normals); else mesh.RecalculateNormals();
        mesh.RecalculateBounds();
        string meshPath = $"{dir}/{name}.asset";
        AssetDatabase.DeleteAsset(meshPath);
        AssetDatabase.CreateAsset(mesh, meshPath);

        var mat = AssetDatabase.LoadAssetAtPath<Material>(VertexColorMaterial);
        if (mat == null)
        {
            mat = new Material(Shader.Find("Skins/Vertex Color")) { name = "Vertex Color" };
            AssetDatabase.CreateAsset(mat, VertexColorMaterial);
        }

        var go = new GameObject(name);
        go.AddComponent<MeshFilter>().sharedMesh = mesh;
        go.AddComponent<MeshRenderer>().sharedMaterial = mat;
        string prefabPath = $"{dir}/{name}.prefab";
        var prefab = PrefabUtility.SaveAsPrefabAsset(go, prefabPath);
        UnityEngine.Object.DestroyImmediate(go);
        AssetDatabase.SaveAssets();

        Bounds b = mesh.bounds;
        Debug.Log($"[Skins] GLB {name}: вершин {verts.Count}, треугольников {tris.Count / 3}, размер {b.size.x:F2}×{b.size.y:F2}×{b.size.z:F2} м, " +
                  $"текстура {(gltf.HadTexture ? "→ цвета вершин" : "нет (серый)")} → {prefabPath}");
        Selection.activeObject = prefab;
        return prefab;
    }

    private sealed class Gltf
    {
        private readonly Dictionary<string, object> _j;
        private readonly byte[] _data;
        private readonly int _bin;
        private readonly string _dir;
        private readonly Dictionary<int, Texture2D> _textures = new();
        internal bool HadTexture;

        internal Gltf(Dictionary<string, object> j, byte[] data, int bin, string dir) { _j = j; _data = data; _bin = bin; _dir = dir; }

        private List<object> Arr(string k) => _j.TryGetValue(k, out var v) ? (List<object>)v : new List<object>();
        private static Dictionary<string, object> D(object o) => (Dictionary<string, object>)o;
        private static int I(object o) => Convert.ToInt32(o);
        private static float F(object o) => Convert.ToSingle(o);

        internal IEnumerable<int> Roots()
        {
            var scenes = Arr("scenes");
            if (scenes.Count > 0)
            {
                int s = _j.TryGetValue("scene", out var si) ? I(si) : 0;
                foreach (var n in (List<object>)D(scenes[s])["nodes"]) yield return I(n);
                yield break;
            }
            for (int i = 0; i < Arr("nodes").Count; i++) yield return i;
        }

        // Матрица узла в осях glTF
        private static Matrix4x4 Local(Dictionary<string, object> n)
        {
            if (n.TryGetValue("matrix", out var m))
            {
                var a = (List<object>)m;
                var r = new Matrix4x4();
                for (int c = 0; c < 4; c++) for (int row = 0; row < 4; row++) r[row, c] = F(a[c * 4 + row]);
                return r;
            }
            Vector3 t = Vector3.zero, s = Vector3.one;
            Quaternion q = Quaternion.identity;
            if (n.TryGetValue("translation", out var tv)) { var a = (List<object>)tv; t = new Vector3(F(a[0]), F(a[1]), F(a[2])); }
            if (n.TryGetValue("rotation", out var rv)) { var a = (List<object>)rv; q = new Quaternion(F(a[0]), F(a[1]), F(a[2]), F(a[3])); }
            if (n.TryGetValue("scale", out var sv)) { var a = (List<object>)sv; s = new Vector3(F(a[0]), F(a[1]), F(a[2])); }
            return Matrix4x4.TRS(t, q, s);
        }

        internal void Collect(int node, Matrix4x4 parent, List<Vector3> verts, List<Vector3> normals, List<Color> colors, List<int> tris)
        {
            var n = D(Arr("nodes")[node]);
            Matrix4x4 m = parent * Local(n);
            if (n.TryGetValue("mesh", out var mi))
                foreach (var p in (List<object>)D(Arr("meshes")[I(mi)])["primitives"])
                    AddPrimitive(D(p), m, verts, normals, colors, tris);
            if (n.TryGetValue("children", out var ch))
                foreach (var c in (List<object>)ch) Collect(I(c), m, verts, normals, colors, tris);
        }

        private void AddPrimitive(Dictionary<string, object> p, Matrix4x4 m, List<Vector3> verts, List<Vector3> normals,
                                  List<Color> colors, List<int> tris)
        {
            if (p.TryGetValue("mode", out var mode) && I(mode) != 4) return;   // только треугольники
            var at = D(p["attributes"]);
            Vector3[] pos = Vec3(I(at["POSITION"]));
            Vector3[] nrm = at.TryGetValue("NORMAL", out var na) ? Vec3(I(na)) : null;
            Vector2[] uv = at.TryGetValue("TEXCOORD_0", out var ua) ? Vec2(I(ua)) : null;
            Color[] vc = at.TryGetValue("COLOR_0", out var ca) ? Col(I(ca)) : null;
            Texture2D tex = null;
            Color baseColor = Color.white;
            if (p.TryGetValue("material", out var mt))
            {
                var mat = D(Arr("materials")[I(mt)]);
                if (mat.TryGetValue("pbrMetallicRoughness", out var pbrO))
                {
                    var pbr = D(pbrO);
                    if (pbr.TryGetValue("baseColorFactor", out var bf))
                    {
                        var a = (List<object>)bf;
                        baseColor = new Color(F(a[0]), F(a[1]), F(a[2]), 1);
                    }
                    if (pbr.TryGetValue("baseColorTexture", out var bt)) tex = Texture(I(D(bt)["index"]));
                }
            }
            Matrix4x4 nm = m.inverse.transpose;
            int baseIndex = verts.Count;
            for (int i = 0; i < pos.Length; i++)
            {
                Vector3 v = m.MultiplyPoint3x4(pos[i]);
                verts.Add(new Vector3(-v.x, v.y, v.z));
                if (nrm != null)
                {
                    Vector3 w = nm.MultiplyVector(nrm[i]).normalized;
                    normals.Add(new Vector3(-w.x, w.y, w.z));
                }
                Color c = baseColor;
                if (tex != null && uv != null) c *= tex.GetPixelBilinear(uv[i].x, 1f - uv[i].y);   // в glTF v сверху
                if (vc != null) c *= vc[i];
                c.a = 1;   // альфа 1 — свой цвет (0 — цвет команды)
                colors.Add(c);
            }
            int[] idx = p.TryGetValue("indices", out var ii) ? Ints(I(ii)) : null;
            int count = idx != null ? idx.Length : pos.Length;
            for (int k = 0; k + 2 < count; k += 3)
            {
                int a = idx != null ? idx[k] : k, b = idx != null ? idx[k + 1] : k + 1, c = idx != null ? idx[k + 2] : k + 2;
                tris.Add(baseIndex + a); tris.Add(baseIndex + c); tris.Add(baseIndex + b);   // зеркало — обход обратно
            }
        }

        private Texture2D Texture(int index)
        {
            if (_textures.TryGetValue(index, out var t)) return t;
            var tex = D(Arr("textures")[index]);
            t = null;
            if (tex.TryGetValue("source", out var src))
            {
                var img = D(Arr("images")[I(src)]);
                byte[] bytes = null;
                if (img.TryGetValue("bufferView", out var bv)) bytes = View(I(bv));
                else if (img.TryGetValue("uri", out var uri) && File.Exists($"{_dir}/{uri}")) bytes = File.ReadAllBytes($"{_dir}/{uri}");
                if (bytes != null)
                {
                    t = new Texture2D(2, 2, TextureFormat.RGBA32, false);
                    if (!t.LoadImage(bytes)) t = null;
                }
            }
            if (t != null) HadTexture = true;
            _textures[index] = t;
            return t;
        }

        private byte[] View(int index)
        {
            var v = D(Arr("bufferViews")[index]);
            int off = v.TryGetValue("byteOffset", out var o) ? I(o) : 0, len = I(v["byteLength"]);
            var r = new byte[len];
            Buffer.BlockCopy(_data, _bin + off, r, 0, len);
            return r;
        }

        // Данные accessor'а: (смещение в файле, шаг, число элементов, тип компонента, нормализован)
        private (int off, int stride, int count, int comp, bool norm) Acc(int index, int elemBytes)
        {
            var a = D(Arr("accessors")[index]);
            var v = D(Arr("bufferViews")[I(a["bufferView"])]);
            int off = _bin + (v.TryGetValue("byteOffset", out var vo) ? I(vo) : 0) + (a.TryGetValue("byteOffset", out var ao) ? I(ao) : 0);
            int stride = v.TryGetValue("byteStride", out var st) ? I(st) : elemBytes;
            return (off, stride, I(a["count"]), I(a["componentType"]), a.TryGetValue("normalized", out var n) && (bool)n);
        }

        private float Comp(int at, int comp, bool norm)
        {
            switch (comp)
            {
                case 5126: return BitConverter.ToSingle(_data, at);
                case 5121: return norm ? _data[at] / 255f : _data[at];
                case 5123: return norm ? BitConverter.ToUInt16(_data, at) / 65535f : BitConverter.ToUInt16(_data, at);
                default: throw new Exception("тип компонента " + comp);
            }
        }

        private static int Size(int comp) => comp == 5126 || comp == 5125 ? 4 : comp == 5123 || comp == 5122 ? 2 : 1;

        private Vector3[] Vec3(int index)
        {
            var a = D(Arr("accessors")[index]);
            int cs = Size(I(a["componentType"]));
            var (off, stride, count, comp, norm) = Acc(index, cs * 3);
            var r = new Vector3[count];
            for (int i = 0; i < count; i++)
            {
                int p = off + i * stride;
                r[i] = new Vector3(Comp(p, comp, norm), Comp(p + cs, comp, norm), Comp(p + 2 * cs, comp, norm));
            }
            return r;
        }

        private Vector2[] Vec2(int index)
        {
            var a = D(Arr("accessors")[index]);
            int cs = Size(I(a["componentType"]));
            var (off, stride, count, comp, norm) = Acc(index, cs * 2);
            var r = new Vector2[count];
            for (int i = 0; i < count; i++)
            {
                int p = off + i * stride;
                r[i] = new Vector2(Comp(p, comp, true), Comp(p + cs, comp, true));
            }
            return r;
        }

        private Color[] Col(int index)
        {
            var a = D(Arr("accessors")[index]);
            int cs = Size(I(a["componentType"]));
            int n = (string)a["type"] == "VEC4" ? 4 : 3;
            var (off, stride, count, comp, _) = Acc(index, cs * n);
            var r = new Color[count];
            for (int i = 0; i < count; i++)
            {
                int p = off + i * stride;
                r[i] = new Color(Comp(p, comp, true), Comp(p + cs, comp, true), Comp(p + 2 * cs, comp, true), 1);
            }
            return r;
        }

        private int[] Ints(int index)
        {
            var a = D(Arr("accessors")[index]);
            int comp = I(a["componentType"]), cs = Size(comp);
            var (off, stride, count, _, _) = Acc(index, cs);
            var r = new int[count];
            for (int i = 0; i < count; i++)
            {
                int p = off + i * stride;
                r[i] = comp == 5125 ? (int)BitConverter.ToUInt32(_data, p) : comp == 5123 ? BitConverter.ToUInt16(_data, p) : _data[p];
            }
            return r;
        }
    }

    // Небольшой JSON-парсер (в редакторе нет встроенного для произвольного JSON)
    private static class MiniJson
    {
        public static object Parse(string s) { int i = 0; return Value(s, ref i); }

        private static void Ws(string s, ref int i) { while (i < s.Length && char.IsWhiteSpace(s[i])) i++; }

        private static object Value(string s, ref int i)
        {
            Ws(s, ref i);
            char c = s[i];
            if (c == '{')
            {
                var d = new Dictionary<string, object>();
                i++; Ws(s, ref i);
                if (s[i] == '}') { i++; return d; }
                while (true)
                {
                    Ws(s, ref i);
                    string k = Str(s, ref i);
                    Ws(s, ref i); i++;   // :
                    d[k] = Value(s, ref i);
                    Ws(s, ref i);
                    if (s[i++] == '}') return d;
                }
            }
            if (c == '[')
            {
                var l = new List<object>();
                i++; Ws(s, ref i);
                if (s[i] == ']') { i++; return l; }
                while (true)
                {
                    l.Add(Value(s, ref i));
                    Ws(s, ref i);
                    if (s[i++] == ']') return l;
                }
            }
            if (c == '"') return Str(s, ref i);
            if (s.Substring(i).StartsWith("true")) { i += 4; return true; }
            if (s.Substring(i).StartsWith("false")) { i += 5; return false; }
            if (s.Substring(i).StartsWith("null")) { i += 4; return null; }
            int st = i;
            while (i < s.Length && "+-0123456789.eE".IndexOf(s[i]) >= 0) i++;
            return double.Parse(s.Substring(st, i - st), System.Globalization.CultureInfo.InvariantCulture);
        }

        private static string Str(string s, ref int i)
        {
            var sb = new StringBuilder();
            i++;   // "
            while (s[i] != '"')
            {
                if (s[i] == '\\')
                {
                    i++;
                    char e = s[i];
                    if (e == 'u') { sb.Append((char)Convert.ToInt32(s.Substring(i + 1, 4), 16)); i += 4; }
                    else sb.Append(e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'b' ? '\b' : e == 'f' ? '\f' : e);
                }
                else sb.Append(s[i]);
                i++;
            }
            i++;
            return sb.ToString();
        }
    }
}
