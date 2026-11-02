using Il2CppDust.Combat;
using Il2CppDust.Networking;
using System.Globalization;
using System.Text;
using UnityEngine;
using Object = UnityEngine.Object;
using Matchmaker = Il2CppDust.Servers.Matchmaker;

namespace Skins
{
    /// <summary>
    /// Скины других игроков с модом. Сетевую логику игры не трогаем: выбор скина передаётся через
    /// свойства сессии (как предметы в SandboxTools) — строки ключ → значение, общие для всех игроков:
    ///   запись — NetworkManager.SetProperty(key, value), чтение — Matchmaker.TryGetProperty (буфер 64 байта).
    /// Ключ — по сетевому ID экземпляра класса (NetworkPrefab.ObjectIdString): он одинаков у всех
    /// игроков, поэтому по копии чужого класса сразу видно, где лежит её скин:
    ///   «sk» + FNV-1a(ID) → имя скина, «-» — родной вид.
    /// Пустую строку сервер не принимает (в свойстве остаётся старое значение), поэтому родной вид —
    /// «-», а пустое при чтении значит «данных нет»: чтение иногда на миг отдаёт пусто (видимо, пока
    /// игра обновляет данные кластера) — скин от этого не снимаем, иначе он мигает.
    /// Запись повторяем раз в ResendInterval: первая сразу после появления класса бывает, что теряется.
    /// Игроки без мода свойства не читают и видят обычного персонажа. Скин у других надевается,
    /// только если у них есть файл с таким именем; та же схема у Quest-версии.
    /// </summary>
    internal static class SkinNet
    {
        private const string KeyPrefix = "sk";
        private const string NormalValue = "-";   // родной вид
        private const int MaxValueBytes = 63;      // буфер Matchmaker.TryGetProperty — 64 байта
        private const float ScanInterval = 1f;
        private const float ResendInterval = 10f;

        private sealed class Remote
        {
            internal string Skin = "";             // последнее прочитанное («» — родной вид, null — надеть заново)
            internal AppliedSkin Applied;
        }

        private const float SelfCheckDelay = 3f;

        private static string _sentKey;      // свойство, в котором сейчас наш скин
        private static string _sentValue;
        private static string _loggedValue;  // что последним написали в лог
        private static float _resendAt;
        private static float _selfCheckAt = -1f;
        private static int _selfCheckFails;
        private const int SelfCheckTries = 3;
        private static string _state;        // последнее состояние сети в логе — пишем только смены
        private static float _nextScan;
        private static readonly Dictionary<IntPtr, Remote> _remotes = new();
        private static readonly HashSet<string> _reportedMissing = new();
        private static bool _reportedLong;

        /// <summary>Каждый кадр: свой скин — в свойство сессии, чужие — надеть.</summary>
        internal static void Update(CombatClass local, string localSkin)
        {
            NetworkManager nm = Session();
            if (nm == null)
            {
                _sentKey = _sentValue = null;   // новая сессия — всё отправим заново
                return;
            }

            try { Publish(nm, local, localSkin ?? ""); }
            catch (Exception e) { Core.Log.Error("Отправка скина: " + e.Message); }

            foreach (Remote r in _remotes.Values)
            {
                try
                {
                    if (r.Applied == null || r.Applied.Sync()) continue;
                    r.Applied.Remove();   // игра выгрузила меши скина — следующий обход наденет заново
                    r.Applied = null;
                    r.Skin = null;
                }
                catch { r.Applied = null; }
            }

            if (Time.unscaledTime < _nextScan) return;
            _nextScan = Time.unscaledTime + ScanInterval;
            try { Scan(local); }
            catch (Exception e) { Core.Log.Error("Скины других игроков: " + e.Message); }
        }

        private static void Publish(NetworkManager nm, CombatClass local, string skin)
        {
            if (local == null) return;   // между смертью и появлением — обычное дело, не пишем
            if (local.GetComponent<NetworkPrefab>() == null)
            {
                State("у своего класса нет NetworkPrefab — другие игроки скин не увидят");
                return;
            }
            string key = KeyOf(local);
            if (key == null) return;     // сетевой ID выдают не сразу после появления
            State("работает");

            // Самопроверка: держит ли сервер наше значение; не держит — сразу отправляем ещё раз.
            // Первая запись после появления класса бывает, что теряется, — ругаемся, только если не выходит подряд.
            if (_selfCheckAt > 0f && Time.unscaledTime >= _selfCheckAt)
            {
                _selfCheckAt = -1f;
                if (Read(_sentKey) == _sentValue) _selfCheckFails = 0;
                else
                {
                    if (++_selfCheckFails == SelfCheckTries)
                        Core.Log.Warning($"Сервер не сохраняет выбор скина ({SelfCheckTries} раза подряд) — другие игроки могут его не увидеть.");
                    _resendAt = 0f;
                    _selfCheckAt = Time.unscaledTime + SelfCheckDelay;
                }
            }

            if (Encoding.UTF8.GetByteCount(skin) > MaxValueBytes)
            {
                if (!_reportedLong) Core.Log.Warning($"Имя скина '{skin}' длиннее {MaxValueBytes} байт — другие игроки его не увидят.");
                _reportedLong = true;
                skin = "";
            }
            string value = skin.Length > 0 ? skin : NormalValue;
            bool changed = key != _sentKey || value != _sentValue;
            if (!changed && Time.unscaledTime < _resendAt) return;

            if (_sentKey != null && _sentKey != key) nm.SetProperty(_sentKey, NormalValue);   // прошлый экземпляр класса
            nm.SetProperty(key, value);
            _resendAt = Time.unscaledTime + ResendInterval;
            if (!changed) return;   // повтор того же — без записи в лог
            if (value != _loggedValue)   // новый экземпляр класса с тем же скином — не пишем
                Core.Log.Msg($"Другим игрокам с модом: {(skin.Length > 0 ? "скин " + skin : "родной вид")}.");
            _loggedValue = value;
            _sentKey = key;
            _sentValue = value;
            _selfCheckAt = Time.unscaledTime + SelfCheckDelay;
        }

        private static void State(string state)
        {
            if (state == _state) return;
            _state = state;
            Core.Log.Msg("Сеть: " + state);
        }

        private static void Scan(CombatClass local)
        {
            var seen = new HashSet<IntPtr>();
            foreach (CombatClass cc in Object.FindObjectsOfType<CombatClass>())
            {
                if (cc == null || (local != null && cc.Pointer == local.Pointer)) continue;
                NetworkPrefab np = cc.GetComponent<NetworkPrefab>();
                if (np == null || np.IsOwned) continue;   // свой (или не сетевой) класс
                string key = KeyOf(cc);
                if (key == null) continue;

                seen.Add(cc.Pointer);
                if (!_remotes.TryGetValue(cc.Pointer, out Remote r)) _remotes[cc.Pointer] = r = new Remote();

                string value = Read(key);
                if (value.Length == 0) continue;   // данных нет (игрок без мода или свойство на миг не читается) — не трогаем
                string skin = value == NormalValue ? "" : value;
                if (skin == r.Skin) continue;
                if (r.Skin != null)   // null — тот же скин надеваем заново после выгрузки, это не новость
                    Core.Log.Msg($"Другой игрок на {Core.Title(Core.KeyOf(cc))}: {(skin.Length > 0 ? "скин " + skin : "родной вид")}.");
                r.Applied?.Remove();
                r.Applied = null;
                r.Skin = skin;
                if (skin.Length > 0) r.Applied = Wear(cc, skin, np);
            }

            foreach (IntPtr gone in _remotes.Keys.Where(p => !seen.Contains(p)).ToList()) _remotes.Remove(gone);
        }

        private static AppliedSkin Wear(CombatClass cc, string skinName, NetworkPrefab np)
        {
            string classKey = Core.KeyOf(cc);
            Skin skin = SkinLibrary.For(classKey).Find(s => s.Name == skinName);
            if (skin == null)
            {
                if (_reportedMissing.Add(classKey + "/" + skinName))
                    Core.Log.Msg($"У другого игрока на {classKey} скин '{skinName}', а у тебя нет такого файла — он виден тебе обычным.");
                return null;
            }
            try { return new AppliedSkin(skin, cc.transform, "другой игрок"); }
            catch (Exception e)
            {
                Core.Log.Error($"Скин '{skinName}' на чужом {classKey}: {e.Message}");
                return null;
            }
        }

        /// <summary>Ключ свойства для экземпляра класса; null — сетевого ID ещё нет.</summary>
        private static string KeyOf(CombatClass cc)
        {
            NetworkPrefab np = cc.GetComponent<NetworkPrefab>();
            if (np == null || !np.HasId) return null;
            string id = np.ObjectIdString;
            if (string.IsNullOrEmpty(id)) return null;
            uint h = 2166136261;   // FNV-1a: string.GetHashCode в .NET случайный, а ключ должен совпасть у всех
            foreach (char c in id) h = (h ^ c) * 16777619;
            return KeyPrefix + h.ToString("x8", CultureInfo.InvariantCulture);
        }

        private static string Read(string key)
        {
            try
            {
                Matchmaker mm = Matchmaker.Instance;
                return mm != null && mm.TryGetProperty(key, out string value) && value != null ? value : "";
            }
            catch (Exception e)
            {
                State("чтение свойства упало: " + e.Message);
                return "";
            }
        }

        /// <summary>
        /// NetworkManager, если игра в сети. HasSession для этого не годится (в матче он false — похоже,
        /// это вход в аккаунт), поэтому смотрим, есть ли сетевой клиент и не офлайн ли Matchmaker.
        /// </summary>
        private static NetworkManager Session()
        {
            try
            {
                NetworkManager nm = NetworkManager.Instance;
                return nm != null && Matchmaker.Instance != null && nm.Client != null && !Matchmaker.offlineMode ? nm : null;
            }
            catch (Exception e)
            {
                State("NetworkManager недоступен: " + e.Message);
                return null;
            }
        }
    }
}
