using UnityEngine;
using UnityEngine.SceneManagement;
using Object = UnityEngine.Object;

namespace Skins
{
    /// <summary>
    /// Уголок скинов в хабе: терминал (модель Cinder: монитор с меню скинов и голо-пьедестал) и зеркало рядом.
    /// Место — в коде (Scene, MonitorAt, MirrorAt: точка на стене и нормаль стены; так же в Quest-моде).
    /// Объекты видны только в хабе: в матче или на карте CustomMap — нет.
    /// </summary>
    internal static class Hub
    {
        internal const float MirrorW = 2.4f, MirrorH = 2.6f;
        private const float FromWall = 0.03f;
        // Зеркало можно выключить: не ставится и не рисуется
        private const bool MirrorOn = true;

        private const string Scene = "The Hub";
        private static readonly Vector3 Wall = new Vector3(-1f, 0f, 0f);              // нормаль стены (в комнату)
        private static readonly Vector3 MonitorAt = new Vector3(-7.961f, -6.212f, 29.305f);   // центр монитора по ширине
        private static readonly Vector3 MirrorAt = new Vector3(-7.961f, -6.624f, 32.075f);    // центр зеркала по ширине

        private static GameObject _monitorObj, _mirrorObj;
        private static Mirror _mirrorView;
        private static Shader _lit, _unlit;
        private static bool _shadersLooked;

        // Коллизия — на слое пола под объектом: с ним игрок точно сталкивается, и камера его рисует
        private static void Solid(GameObject go, int layer)
        {
            go.layer = layer;
            if (go.GetComponent<Collider>() == null) go.AddComponent<MeshCollider>();
        }

        /// <summary>Каждый кадр: создать объекты в хабе (сцена могла перезагрузиться), спрятать вне хаба.</summary>
        internal static void Update()
        {
            bool inHub = SceneManager.GetActiveScene().name == Scene;
            Keep(ref _monitorObj, inHub, MonitorAt, false);
            if (Game.IsAlive(_monitorObj) && _monitorObj.activeSelf) Menu?.Update();
            Keep(ref _mirrorObj, inHub && MirrorOn, MirrorAt, true);
        }

        /// <summary>После движения камеры: зеркало рисует отражение для её нового положения.</summary>
        internal static void LateUpdate()
        {
            try { _mirrorView?.Render(); }
            catch (Exception e) { Core.Log.Error("Зеркало: " + e.Message); _mirrorView?.Dispose(); _mirrorView = null; }
        }

        private static void Keep(ref GameObject obj, bool inHub, Vector3 p, bool mirror)
        {
            Vector3 n = Wall;
            if (!inHub)
            {
                if (Game.IsAlive(obj) && obj.activeSelf) obj.SetActive(false);
                if (mirror && _mirrorView != null) _mirrorView.Active = false;
                return;
            }
            if (!Game.IsAlive(obj))
            {
                try { obj = mirror ? MakeMirror(p, n) : MakeMonitor(p, n); }
                catch (Exception e) { Core.Log.Error($"Хаб: {(mirror ? "зеркало" : "монитор")}: {e}"); obj = null; return; }
                if (obj == null) return;
            }
            if (!obj.activeSelf) obj.SetActive(true);
            if (mirror && _mirrorView != null) _mirrorView.Active = true;
        }

        private static void Destroy(bool mirror)
        {
            if (mirror)
            {
                _mirrorView?.Dispose();
                _mirrorView = null;
                if (Game.IsAlive(_mirrorObj)) Object.Destroy(_mirrorObj);
                _mirrorObj = null;
            }
            else
            {
                if (Game.IsAlive(_monitorObj)) Object.Destroy(_monitorObj);
                _monitorObj = null;
            }
        }

        // Объект стоит лицом от стены: локальная -Z смотрит в комнату (так видна лицевая сторона Quad)
        private static GameObject Root(string name, Vector3 p, Vector3 n)
        {
            var go = new GameObject(name);
            go.transform.SetPositionAndRotation(p + n * FromWall, Quaternion.LookRotation(-n, Vector3.up));
            return go;
        }

        // Терминал Cinder (SkinLibrary.Terminal): +Z модели — в комнату, начало — на полу под центром пьедестала,
        // центр монитора по X = 0, задняя стенка монитора — на z = -TerminalBack. Точка на стене — центр монитора
        // по горизонтали; высоту даёт сама модель (ставим на пол под этой точкой).
        internal const float TerminalBack = 0.986f;
        internal const float TerminalScale = 1.3f;   // модель Cinder — в метрах; в хабе чуть крупнее

        internal static TerminalMenu Menu { get; private set; }

        private static GameObject MakeMonitor(Vector3 p, Vector3 n)
        {
            GameObject prefab = SkinLibrary.Terminal();
            if (prefab == null) return null;

            Vector3 origin = p + n * (TerminalBack * TerminalScale + 0.01f);
            // Пол — лучом вниз перед стеной; не нашли — считаем, что точка на высоте центра монитора
            int layer = 0;
            if (Physics.Raycast(origin + Vector3.up * 0.5f, Vector3.down, out RaycastHit floor, 4f, ~0, QueryTriggerInteraction.Ignore))
            {
                origin.y = floor.point.y;
                layer = floor.collider.gameObject.layer;
            }
            else
            {
                origin.y = p.y - 1.61f * TerminalScale;
                Core.Log.Warning("Хаб: пол под терминалом не найден — ставлю по высоте точки.");
            }

            GameObject root = Object.Instantiate(prefab);
            root.name = "Skins Terminal";
            root.hideFlags = HideFlags.None;
            root.transform.SetPositionAndRotation(origin, Quaternion.LookRotation(n, Vector3.up));
            root.transform.localScale = Vector3.one * TerminalScale;

            Material mat = GameMaterial();
            foreach (MeshRenderer r in root.GetComponentsInChildren<MeshRenderer>(true))
            {
                var mats = new Material[r.sharedMaterials.Length];
                for (int i = 0; i < mats.Length; i++) mats[i] = mat;
                r.sharedMaterials = mats;
                Solid(r.gameObject, layer);   // до меню и голограммы: у них своя, слой 0, без коллизии
            }
            Menu = new TerminalMenu(root.transform);
            Core.Log.Msg($"Хаб: терминал поставлен, пол на y = {origin.y:0.###}.");
            return root;
        }

        private static Material _gameMaterial;

        // Материал класса игры: вершинные цвета, как у скинов; альфа 0 — цвет команды (экран терминала).
        // Нет классов — простой серый со светом
        internal static Material GameMaterial()
        {
            if (Game.IsAlive(_gameMaterial)) return _gameMaterial;
            foreach (var (key, prefab) in Game.ClassPrefabs())
            {
                foreach (MeshRenderer r in prefab.GetComponentsInChildren<MeshRenderer>(true))
                {
                    Material m = r.sharedMaterial;
                    if (m == null) continue;
                    _gameMaterial = m;
                    Core.Log.Msg($"Хаб: материал терминала — '{m.name}' ({(m.shader != null ? m.shader.name : "?")}) от {key}/{r.name}.");
                    return m;
                }
            }
            Core.Log.Warning("Хаб: нет материала класса — терминал серый.");
            return _gameMaterial = Lit(new Color(0.5f, 0.5f, 0.5f));
        }

        // Зеркало стоит на полу (низ — чуть над полом под точкой прицела), по ширине — с центром в точке прицела
        private const float MirrorFromFloor = 0.15f;

        private static GameObject MakeMirror(Vector3 p, Vector3 n)
        {
            Vector3 probe = p + n * 0.3f;
            int layer = 0;
            if (Physics.Raycast(probe + Vector3.up * 0.5f, Vector3.down, out RaycastHit floor, 6f, ~0, QueryTriggerInteraction.Ignore))
            {
                p.y = floor.point.y + MirrorFromFloor + MirrorH / 2;
                layer = floor.collider.gameObject.layer;
            }
            else Core.Log.Warning("Хаб: пол под зеркалом не найден — центр по прицелу.");
            GameObject root = Root("Skins Mirror", p, n);
            Part(root, "Frame", Meshes.Box(MirrorW + 0.08f, MirrorH + 0.08f, 0.04f), Lit(new Color(0.09f, 0.1f, 0.11f)), new Vector3(0, 0, 0.02f));
            _mirrorView?.Dispose();
            _mirrorView = new Mirror(MirrorW, MirrorH);
            // Отражение — картинка камеры «за стеклом»; по горизонтали она зеркальна относительно стекла
            Part(root, "Glass", Meshes.Quad(MirrorW, MirrorH, true), _mirrorView.Material(Unlit(Color.white)), new Vector3(0, 0, -0.003f));
            _mirrorView.Glass = root.transform.Find("Glass");
            // Коллизия — коробка по рамке, толщиной 30 см в комнату: сквозь зеркало не пройти
            GameObject frame = root.transform.Find("Frame").gameObject;
            frame.layer = layer;
            var box = frame.AddComponent<BoxCollider>();
            box.size = new Vector3(MirrorW + 0.08f, MirrorH + 0.08f, 0.3f);
            box.center = new Vector3(0f, 0f, -0.13f);
            Core.Log.Msg("Хаб: зеркало поставлено.");
            return root;
        }

        private static void Part(GameObject root, string name, Mesh mesh, Material mat, Vector3 local)
        {
            var go = new GameObject(name);
            go.transform.SetParent(root.transform, false);
            go.transform.localPosition = local;
            go.AddComponent<MeshFilter>().sharedMesh = mesh;
            var r = go.AddComponent<MeshRenderer>();
            r.sharedMaterial = mat;
            r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
        }

        private static Material Lit(Color c) => Colored(LitShader() ?? UnlitShader(), c);
        internal static Material Unlit(Color c) => Colored(UnlitShader(), c);

        private static Material Colored(Shader shader, Color c)
        {
            var m = new Material(shader);
            if (m.HasProperty("_BaseColor")) m.SetColor("_BaseColor", c);
            if (m.HasProperty("_Color")) m.SetColor("_Color", c);
            m.hideFlags = HideFlags.DontUnloadUnusedAsset;
            return m;
        }

        private static Shader LitShader() { LookShaders(); return _lit; }

        private static Shader UnlitShader()
        {
            LookShaders();
            if (_unlit == null) throw new Exception("нет ни одного шейдера без света (Unlit) в сборке игры");
            return _unlit;
        }

        // Какие шейдеры есть в сборке игры — заранее не знаем: берём первый найденный и пишем его в лог
        private static void LookShaders()
        {
            if (_shadersLooked) return;
            _shadersLooked = true;
            _lit = Find("Universal Render Pipeline/Lit", "Universal Render Pipeline/Simple Lit");
            _unlit = Find("Universal Render Pipeline/Unlit", "Unlit/Texture", "UI/Default", "Sprites/Default");
            Core.Log.Msg($"Хаб: шейдеры — со светом '{(_lit != null ? _lit.name : "нет")}', без света '{(_unlit != null ? _unlit.name : "нет")}'.");
        }

        private static Shader Find(params string[] names)
        {
            foreach (string name in names)
            {
                Shader s = Shader.Find(name);
                if (s != null) return s;
            }
            return null;
        }

    }

    /// <summary>Простые меши в метрах: GameObject.CreatePrimitive в сборке игры может быть вырезан.</summary>
    internal static class Meshes
    {
        /// <summary>Прямоугольник в плоскости XY, лицом к -Z. flipU — текстура отражена по горизонтали.</summary>
        internal static Mesh Quad(float w, float h, bool flipU)
        {
            float x = w / 2, y = h / 2;
            float u0 = flipU ? 1 : 0, u1 = 1 - u0;
            var m = new Mesh { name = "Skins Quad" };
            m.vertices = new[] { new Vector3(-x, -y, 0), new Vector3(-x, y, 0), new Vector3(x, y, 0), new Vector3(x, -y, 0) };
            m.uv = new[] { new Vector2(u0, 0), new Vector2(u0, 1), new Vector2(u1, 1), new Vector2(u1, 0) };
            m.triangles = new[] { 0, 1, 2, 0, 2, 3 };
            m.RecalculateNormals();
            m.RecalculateBounds();
            return m;
        }

        /// <summary>Треугольник-стрелка в плоскости XY, лицом к -Z: right — остриём вправо (+X), иначе влево.</summary>
        internal static Mesh Triangle(float w, float h, bool right)
        {
            float x = w / 2, y = h / 2;
            var m = new Mesh { name = "Skins Triangle" };
            // Обход по часовой, если смотреть с -Z (как у Quad)
            m.vertices = right
                ? new[] { new Vector3(-x, -y, 0), new Vector3(-x, y, 0), new Vector3(x, 0, 0) }
                : new[] { new Vector3(x, -y, 0), new Vector3(-x, 0, 0), new Vector3(x, y, 0) };
            m.uv = new[] { Vector2.zero, Vector2.up, Vector2.one };
            m.triangles = new[] { 0, 1, 2 };
            m.RecalculateNormals();
            m.RecalculateBounds();
            return m;
        }

        internal static Mesh Box(float w, float h, float d)
        {
            Vector3 s = new Vector3(w / 2, h / 2, d / 2);
            // Грани: нормаль и две оси на грани (u × v = -нормаль, чтобы обход был по часовой снаружи)
            var faces = new[]
            {
                (Vector3.back, Vector3.right, Vector3.up), (Vector3.forward, Vector3.left, Vector3.up),
                (Vector3.left, Vector3.back, Vector3.up), (Vector3.right, Vector3.forward, Vector3.up),
                (Vector3.up, Vector3.right, Vector3.forward), (Vector3.down, Vector3.right, Vector3.back),
            };
            var verts = new List<Vector3>();
            var tris = new List<int>();
            foreach (var (n, u, v) in faces)
            {
                int i = verts.Count;
                foreach (var (a, b) in new[] { (-1, -1), (-1, 1), (1, 1), (1, -1) })
                    verts.Add(Vector3.Scale(n + u * a + v * b, s));
                tris.AddRange(new[] { i, i + 1, i + 2, i, i + 2, i + 3 });
            }
            var m = new Mesh { name = "Skins Box" };
            m.vertices = verts.ToArray();
            m.triangles = tris.ToArray();
            m.RecalculateNormals();
            m.RecalculateBounds();
            return m;
        }
    }
}
