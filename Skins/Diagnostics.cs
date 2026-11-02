using Il2CppDust;
using Il2CppDust.Combat;
using Il2CppDust.Networking;
using MelonLoader.Utils;
using System.Globalization;
using System.Text;
using UnityEngine;
using Object = UnityEngine.Object;

namespace Skins
{
    /// <summary>
    /// Дамп устройства классов в файлы (Ctrl+F11): по нему решаем, на какие кости вешать модель скина.
    /// Поля Il2Cpp-префабов офлайн не прочитать (в сборке нет typetree), поэтому смотрим в игре.
    ///
    /// UserData\Skins\Dumps\:
    ///   &lt;KEY&gt;.prefab.txt — префаб класса из реестра (поза покоя);
    ///   &lt;KEY&gt;.remoteprefab.txt — префаб, из которого его копию создают у других игроков;
    ///   &lt;KEY&gt;.live.txt   — класс локального игрока, как он есть сейчас;
    ///   &lt;KEY&gt;.remote.txt — класс другого игрока (его копия у нас).
    /// </summary>
    internal static class Diagnostics
    {
        internal static string Folder => Path.Combine(MelonEnvironment.UserDataDirectory, "Skins", "Dumps");

        internal static void DumpAll()
        {
            Directory.CreateDirectory(Folder);
            int files = 0;

            Try("реестры", () =>
            {
                var sb = new StringBuilder();
                foreach (var (key, prefab) in Game.ClassPrefabs())
                {
                    sb.AppendLine($"{key} -> {prefab.name}");
                    Try(key, () => { Write($"{key}.prefab.txt", DumpTree(prefab.transform)); files++; });
                    Try(key + " (у других)", () =>
                    {
                        GameObject remote = RemotePrefab(prefab);
                        if (remote == null) return;
                        Write($"{key}.remoteprefab.txt", DumpTree(remote.transform));
                        files++;
                    });
                }
                Write("classes.txt", sb.ToString());
                files++;
            });

            Try("игрок", () =>
            {
                Player local = Game.FindLocalPlayer();
                CombatClass cc = Game.CurrentClass(local);
                if (cc == null)
                {
                    Core.Log.Msg("Локальный игрок без класса — live-дампа нет (зайди за класс и нажми Ctrl+F11 ещё раз).");
                    return;
                }
                Write($"{KeyOf(cc)}.live.txt", DumpTree(cc.transform));
                files++;
            });

            Try("другие игроки", () =>
            {
                Player local = Game.FindLocalPlayer();
                foreach (Player p in Object.FindObjectsOfType<Player>())
                {
                    if (p == null || (local != null && p.Pointer == local.Pointer)) continue;
                    CombatClass cc = Game.CurrentClass(p);
                    if (cc == null) continue;
                    Write($"{KeyOf(cc)}.remote.txt", DumpTree(cc.transform));
                    files++;
                }
            });

            Core.Log.Msg($"Дамп: {files} файлов в {Folder}");
        }

        private static string KeyOf(CombatClass cc) => cc.classType.ToString().ToUpperInvariant();

        /// <summary>Префаб, из которого другие игроки создают копию нашего класса (NetworkPrefab.remotePrefabRef).</summary>
        private static GameObject RemotePrefab(GameObject local)
        {
            NetworkPrefab np = local.GetComponent<NetworkPrefab>();
            var reference = np != null ? np.remotePrefabRef : null;
            return reference != null ? reference.Prefab : null;
        }

        private static void Write(string name, string text) => File.WriteAllText(Path.Combine(Folder, name), text);

        /// <summary>Дерево объектов: слой, поза относительно родителя, компоненты, меши, кости, материалы.</summary>
        private static string DumpTree(Transform root)
        {
            var sb = new StringBuilder();
            sb.AppendLine($"# {root.name}  pos={V(root.position)} rot={V(root.eulerAngles)} lossyScale={V(root.lossyScale)}");
            Node(sb, root, root, 0);
            return sb.ToString();
        }

        private static void Node(StringBuilder sb, Transform t, Transform root, int depth)
        {
            string pad = new string(' ', depth * 2);
            GameObject go = t.gameObject;
            string active = go.activeSelf ? "" : ", off";
            Vector3 rootPos = root.InverseTransformPoint(t.position);
            sb.AppendLine($"{pad}{t.name} [L{go.layer}{active}] lp={V(t.localPosition)} lr={V(t.localEulerAngles)} ls={V(t.localScale)} root={V(rootPos)} :: {ComponentList(go)}");

            MeshFilter mf = go.GetComponent<MeshFilter>();
            if (mf != null)
            {
                Mesh m = mf.sharedMesh;
                sb.AppendLine($"{pad}  · MeshFilter: {MeshInfo(m)}");
            }

            SkinnedMeshRenderer smr = go.GetComponent<SkinnedMeshRenderer>();
            if (smr != null)
            {
                sb.AppendLine($"{pad}  · Skinned: {MeshInfo(smr.sharedMesh)}, rootBone={NameOf(smr.rootBone)}, bounds c={V(smr.localBounds.center)} size={V(smr.localBounds.size)}");
                var bones = smr.bones;
                var names = new List<string>();
                for (int i = 0; bones != null && i < bones.Length; i++) names.Add(bones[i] != null ? bones[i].name : "null");
                sb.AppendLine($"{pad}  · кости ({names.Count}): {string.Join(", ", names)}");
            }

            Renderer r = go.GetComponent<Renderer>();
            if (r != null)
            {
                var mats = r.sharedMaterials;
                var list = new List<string>();
                for (int i = 0; mats != null && i < mats.Length; i++)
                {
                    Material mat = mats[i];
                    list.Add(mat == null ? "null" : $"{mat.name} ({(mat.shader != null ? mat.shader.name : "no shader")})");
                }
                sb.AppendLine($"{pad}  · Renderer: enabled={r.enabled}, materials: {string.Join("; ", list)}");
            }

            for (int i = 0; i < t.childCount; i++) Node(sb, t.GetChild(i), root, depth + 1);
        }

        private static string MeshInfo(Mesh m) =>
            m == null ? "null" : $"'{m.name}' verts={m.vertexCount} sub={m.subMeshCount} readable={m.isReadable} bounds c={V(m.bounds.center)} size={V(m.bounds.size)}";

        // Точка как разделитель при любом языке системы: дамп читает сборщик скинов.
        private static string V(Vector3 v) => string.Format(CultureInfo.InvariantCulture, "({0:F3},{1:F3},{2:F3})", v.x, v.y, v.z);

        private static string ComponentList(GameObject go)
        {
            var sb = new StringBuilder();
            foreach (Component c in go.GetComponents<Component>())
            {
                if (c == null) continue;
                string type = c.GetIl2CppType().Name;
                if (type == "Transform") continue;
                if (sb.Length > 0) sb.Append(", ");
                sb.Append(type);
            }
            return sb.ToString();
        }

        private static string NameOf(Object o) => o != null ? o.name : "null";

        private static void Try(string what, Action action)
        {
            try { action(); }
            catch (Exception e) { Core.Log.Error($"Дамп ({what}): {e.Message}"); }
        }
    }
}
