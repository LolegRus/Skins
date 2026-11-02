using Il2Cpp;
using Il2CppDust;
using Il2CppDust.Collections;
using Il2CppDust.Combat;
using UnityEngine;
using Object = UnityEngine.Object;

namespace Skins
{
    using ClassEntry = Registry<GameObject, ClassRegistry.EmptyData>.RegistryEntry<ClassRegistry.EmptyData>;

    /// <summary>Поиск игрока, его класса и префабов классов в реестре (как в Ballista).</summary>
    internal static class Game
    {
        /// <summary>Игрок, которому принадлежит камера (чужие игроки тоже несут компонент Player).</summary>
        internal static Player FindLocalPlayer()
        {
            try
            {
                Camera cam = Camera.main;
                if (cam == null) return null;

                Player p = cam.GetComponentInParent<Player>();
                if (p != null) return p;

                foreach (Player candidate in Object.FindObjectsOfType<Player>())
                {
                    Transform head = candidate != null ? candidate.Head : null;
                    if (head != null && cam.transform.IsChildOf(head)) return candidate;
                }
            }
            catch (Exception e)
            {
                Core.Log.Error("Поиск игрока: " + e.Message);
            }
            return null;
        }

        internal static CombatClass CurrentClass(Player player)
        {
            if (player == null) return null;
            try
            {
                var current = player.CurrentClass;
                CombatClass cc = current.HasValue ? current.Value : null;
                return IsAlive(cc) ? cc : null;
            }
            catch
            {
                return null;
            }
        }

        /// <summary>Все записи реестров классов: ключ (KOBOLD, SCARAB...) и префаб.</summary>
        internal static List<(string key, GameObject prefab)> ClassPrefabs()
        {
            var list = new List<(string, GameObject)>();
            foreach (ClassRegistry reg in Resources.FindObjectsOfTypeAll<ClassRegistry>())
            {
                if (reg == null) continue;
                var entries = reg.prefabs;
                for (int i = 0; i < entries.Count; i++)
                {
                    ClassEntry e = entries[i];
                    if (e != null && e.prefab != null) list.Add((e.name, e.prefab));
                }
            }
            return list;
        }

        /// <summary>Префаб класса по ключу без учёта регистра.</summary>
        internal static GameObject FindPrefab(string key)
        {
            foreach (var (k, prefab) in ClassPrefabs())
                if (string.Equals(k, key, StringComparison.OrdinalIgnoreCase)) return prefab;
            return null;
        }

        internal static bool IsAlive(Object o) => o != null && !o.WasCollected;
    }
}
