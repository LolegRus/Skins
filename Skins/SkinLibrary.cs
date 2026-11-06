using Il2CppInterop.Runtime;
using MelonLoader.Utils;
using System.Reflection;
using System.Runtime.InteropServices;
using UnityEngine;
using Object = UnityEngine.Object;

namespace Skins
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct ManagedSpanWrapper
    {
        public unsafe void* ptr;
        public int length;

        public unsafe ManagedSpanWrapper(void* ptr, int length)
        {
            this.ptr = ptr;
            this.length = length;
        }
    }

    /// <summary>Скин: модель для одного класса из одного бандла.</summary>
    internal sealed class Skin
    {
        internal string Name;        // имя файла бандла без расширения — так скин видно игроку
        internal string ClassKey;    // KOBOLD, SCARAB...
        internal SkinLibrary.Bundle Source;
        internal GameObject Loaded;  // модель из бандла; игра может её выгрузить — тогда грузим заново

        /// <summary>Модель скина (null — не загрузилась).</summary>
        internal GameObject Prefab => SkinLibrary.PrefabOf(this);
    }

    /// <summary>
    /// Скины из UserData\Skins\*.skin (зашитые в мод — Skins\Builtin — выкладываются туда же).
    /// Каждый файл — AssetBundle (собирается в Unity\, Tools → Skins → Build All),
    /// внутри префабы, названные ключом класса: KOBOLD.prefab — модель для Kobold, SCARAB.prefab — для Scarab.
    /// Бандл грузится через ICall, как в Ballista и CustomMap (обычный AssetBundle.LoadFromFile в
    /// Il2Cpp-сборке игры недоступен). Модель класса ищется по имени ассета, а не по списку
    /// содержимого: GetAllAssetNames в этой сборке interop ненадёжен.
    /// </summary>
    internal static class SkinLibrary
    {
        internal const string Extension = ".skin";
        private const string BuiltinPrefix = "Skins.Builtin.";   // LogicalName ресурсов в .csproj

        private delegate IntPtr LoadFromFileDelegate(ref ManagedSpanWrapper path, uint crc, ulong offset);
        private delegate IntPtr LoadAssetDelegate(IntPtr self, ref ManagedSpanWrapper name, IntPtr type);

        internal sealed class Bundle
        {
            internal string Name;
            internal string Path;
            internal AssetBundle Asset;
            internal IntPtr Handle;   // GC-хэндл держит бандл живым
        }

        private static List<Bundle> _bundles;
        private static readonly Dictionary<string, List<Skin>> _byClass = new(StringComparer.OrdinalIgnoreCase);

        internal static string Folder => Path.Combine(MelonEnvironment.UserDataDirectory, "Skins");

        /// <summary>Скины для класса (по алфавиту). Модели из бандлов читаются при первом запросе.</summary>
        internal static List<Skin> For(string classKey)
        {
            if (_byClass.TryGetValue(classKey, out var list)) return list;

            list = new List<Skin>();
            foreach (Bundle b in Bundles())
            {
                var skin = new Skin { Name = b.Name, ClassKey = classKey.ToUpperInvariant(), Source = b };
                if (skin.Prefab != null) list.Add(skin);
            }
            _byClass[classKey] = list;
            if (list.Count > 0) Core.Log.Msg($"Скины для {classKey}: {string.Join(", ", list.Select(s => s.Name))}");
            return list;
        }

        /// <summary>
        /// Модель скина. При смене сцены игра выгружает ассеты (и, бывает, бандлы) — модель тогда
        /// уничтожена, и её загружаем заново (бандл — тоже, если выгружен и он).
        /// </summary>
        internal static GameObject PrefabOf(Skin skin)
        {
            if (Game.IsAlive(skin.Loaded)) return skin.Loaded;
            skin.Loaded = null;

            Bundle b = skin.Source;
            if (b.Asset == null || b.Asset.WasCollected)
            {
                Bundle fresh = Load(b.Path);
                if (fresh == null) return null;
                b.Asset = fresh.Asset;
                b.Handle = fresh.Handle;
            }

            GameObject prefab = LoadAsset<GameObject>(b, skin.ClassKey);
            if (prefab == null) return null;
            Protect(prefab);
            skin.Loaded = prefab;
            return prefab;
        }

        /// <summary>Модель и её меши — не выгружать вместе с неиспользуемыми ассетами сцены.</summary>
        private static void Protect(GameObject prefab)
        {
            prefab.hideFlags |= HideFlags.DontUnloadUnusedAsset;
            foreach (MeshFilter mf in prefab.GetComponentsInChildren<MeshFilter>(true))
            {
                Mesh m = mf.sharedMesh;
                if (m != null) m.hideFlags |= HideFlags.DontUnloadUnusedAsset;
            }
        }

        private static List<Bundle> Bundles()
        {
            if (_bundles != null) return _bundles;
            _bundles = new List<Bundle>();

            Directory.CreateDirectory(Folder);
            ExtractBuiltin();
            foreach (string path in Directory.GetFiles(Folder, "*" + Extension).OrderBy(p => p, StringComparer.OrdinalIgnoreCase))
            {
                try
                {
                    Bundle b = Load(path);
                    if (b != null) _bundles.Add(b);
                }
                catch (Exception e)
                {
                    Core.Log.Error($"Скин '{Path.GetFileName(path)}': {e.Message}");
                }
            }
            Core.Log.Msg($"Файлов скинов: {_bundles.Count} ({Folder})");
            return _bundles;
        }

        /// <summary>
        /// Скины, зашитые в DLL (Skins\Builtin\*.skin), — в папку скинов: мод ставится одним файлом.
        /// Unity грузит бандл только с диска. Файл переписывается, если он устарел (новая версия мода).
        /// </summary>
        private static void ExtractBuiltin()
        {
            Assembly asm = Assembly.GetExecutingAssembly();
            foreach (string resource in asm.GetManifestResourceNames())
            {
                if (!resource.StartsWith(BuiltinPrefix, StringComparison.Ordinal)) continue;
                string path = Path.Combine(Folder, resource.Substring(BuiltinPrefix.Length));
                try
                {
                    if (Extract(resource, path)) Core.Log.Msg($"Скин из мода выложен: {Path.GetFileName(path)}");
                }
                catch (Exception e)
                {
                    Core.Log.Error($"Скин из мода '{Path.GetFileName(path)}': {e.Message}");
                }
            }
        }

        /// <summary>Ресурс DLL — в файл, если там его ещё нет или он другой. true — записан.</summary>
        private static bool Extract(string resource, string path)
        {
            byte[] bytes;
            using (Stream res = Assembly.GetExecutingAssembly().GetManifestResourceStream(resource))
            using (var ms = new MemoryStream())
            {
                if (res == null) throw new FileNotFoundException("нет ресурса " + resource);
                res.CopyTo(ms);
                bytes = ms.ToArray();
            }
            if (File.Exists(path) && File.ReadAllBytes(path).AsSpan().SequenceEqual(bytes)) return false;
            File.WriteAllBytes(path, bytes);
            return true;
        }

        // ---- шрифт меню ----
        // Pixelify Sans (лицензия SIL OFL 1.1): бандл с TTF зашит в DLL и выкладывается в папку скинов
        // вместе с текстом лицензии. Собирает Unity\ (Tools → Skins → Build Menu Font).
        private const string FontResource = "Skins.MenuFont.font";
        private const string FontLicenseResource = "Skins.PixelifySans-OFL.txt";
        private static Font _menuFont;
        private static Bundle _fontBundle;
        private static bool _fontFailed;

        /// <summary>Пиксельный шрифт меню (null — не загрузился, меню пишет шрифтом по умолчанию).</summary>
        internal static Font MenuFont()
        {
            if (_menuFont != null) return _menuFont;   // == у Unity-объекта учитывает и уничтоженный
            if (_fontFailed) return null;
            try
            {
                Directory.CreateDirectory(Folder);
                string path = Path.Combine(Folder, "MenuFont.font");
                Extract(FontResource, path);
                Extract(FontLicenseResource, Path.Combine(Folder, "PixelifySans-OFL.txt"));
                _fontBundle ??= Load(path);
                if (_fontBundle == null) throw new Exception("бандл не загрузился");
                _menuFont = LoadAsset<Font>(_fontBundle, "PixelifySans");
                if (_menuFont == null) throw new Exception("нет шрифта в бандле");
                _menuFont.hideFlags |= HideFlags.DontUnloadUnusedAsset;   // иначе игра выгрузит его при смене сцены
            }
            catch (Exception e)
            {
                _fontFailed = true;
                Core.Log.Warning($"Шрифт меню: {e.Message} — шрифт по умолчанию");
            }
            return _menuFont;
        }

        private static Bundle Load(string path)
        {
            path = Path.GetFullPath(path);
            var loadFromFile = IL2CPP.ResolveICall<LoadFromFileDelegate>("UnityEngine.AssetBundle::LoadFromFile_Internal_Injected");
            IntPtr handle;
            unsafe
            {
                fixed (char* p = path)
                {
                    var span = new ManagedSpanWrapper(p, path.Length);
                    handle = loadFromFile(ref span, 0, 0);
                }
            }

            IntPtr ptr = handle != IntPtr.Zero ? IL2CPP.il2cpp_gchandle_get_target(handle) : IntPtr.Zero;
            if (ptr == IntPtr.Zero)
            {
                Core.Log.Error("Бандл скина не загрузился (собран не той версией Unity или не под Windows?): " + path);
                return null;
            }
            return new Bundle { Name = System.IO.Path.GetFileNameWithoutExtension(path), Path = path, Asset = new AssetBundle(ptr), Handle = handle };
        }

        private static T LoadAsset<T>(Bundle b, string name) where T : Object
        {
            try
            {
                Object asset = b.Asset.LoadAsset(name, Il2CppType.Of<T>());
                if (asset != null) return asset.TryCast<T>();
                return null;
            }
            catch (Exception e)
            {
                Core.Log.Warning("AssetBundle.LoadAsset недоступен, пробую ICall: " + e.Message);
            }

            var loadAsset = IL2CPP.ResolveICall<LoadAssetDelegate>("UnityEngine.AssetBundle::LoadAsset_Internal_Injected");
            IntPtr result;
            unsafe
            {
                fixed (char* p = name)
                {
                    var span = new ManagedSpanWrapper(p, name.Length);
                    result = loadAsset(IL2CPP.Il2CppObjectBaseToPtrNotNull(b.Asset), ref span,
                        IL2CPP.Il2CppObjectBaseToPtrNotNull(Il2CppType.Of<T>()));
                }
            }
            return result != IntPtr.Zero ? new Object(result).TryCast<T>() : null;
        }
    }
}
