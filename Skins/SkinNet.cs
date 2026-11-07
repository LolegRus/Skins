using Il2CppDust.Combat;
using Il2CppDust.Networking;
using Il2CppDust.Serialisation;
using Il2CppInterop.Runtime;
using MelonLoader.NativeUtils;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;
using Object = UnityEngine.Object;
using Matchmaker = Il2CppDust.Servers.Matchmaker;

namespace Skins
{
    /// <summary>
    /// Скины других игроков с модом — через RPC игры, как смена оружия у Kobold или рэгдолл у CombatClass,
    /// только с номером RPC, которого у компонента нет: игра без мода такой пакет молча выбрасывает
    /// (NetworkComponent.OnReceive: индекс ≥ числа RPC или > 254 — return), а мод ловит его хуком на
    /// OnReceive до этой проверки. Свойства сессии больше не трогаем: они копились до ухода всех игроков.
    ///
    /// Пакет шлём со своего CombatClass (NetworkComponent.SendRpc / SendRpcTo, ненадёжный канал), игра
    /// сама добавляет номер компонента. Сам пакет: [255]['S']['K'][версия][флаги][длина имени][имя UTF-8],
    /// пустое имя — родной вид. Пришёл на чужой CombatClass — значит, это скин этого экземпляра класса.
    ///
    /// Кто с модом, узнаём так (схема от разработчика игры):
    ///  • раз в HelloInterval шлём всем «привет» (флаг Hello) со своим скином — без мода его выбросят;
    ///    внеочередной — через секунду после появления своего класса или нового чужого (не чаще HelloMinGap);
    ///  • на «привет» от нового игрока отвечаем ему лично своим скином;
    ///  • известным игрокам с модом шлём скин лично: сразу при смене скина или экземпляра класса
    ///    (несколько раз — у них новый экземпляр может появиться чуть позже) и раз в PollInterval.
    /// Скин у других надевается, только если у них есть файл с таким именем; та же схема у Quest-версии.
    /// </summary>
    internal static class SkinNet
    {
        private const byte RpcIndex = 255;        // такого RPC нет ни у одного компонента (их не больше 254)
        private const byte Version = 1;
        private const byte FlagHello = 1;         // «я с модом, пришли свой скин»
        private const int HeaderBytes = 6;
        private const int MaxSkinBytes = 64;

        private const float HelloInterval = 30f;
        private const float HelloMinGap = 3f;        // внеочередной «привет» — не чаще
        private const float HelloAfterSpawn = 1f;    // через столько после появления класса (своего или чужого)
        private const float PollInterval = 5f;
        private const float PeerTimeout = 40f;    // не слышно столько — перестаём слать лично (его «привет» вернёт)
        private const float ReplyCooldown = 2f;
        private const float BurstInterval = 1f;
        private const int BurstSends = 3;
        private const float ScanInterval = 1f;

        private sealed class Remote
        {
            internal string Skin = "";             // что надето («» — родной вид, null — надеть заново)
            internal AppliedSkin Applied;
        }

        private sealed class Peer
        {
            internal float LastHeard;
            internal float LastReply = float.NegativeInfinity;
        }

        private readonly struct Incoming
        {
            internal readonly IntPtr Component;     // чужой CombatClass, на который пришёл пакет
            internal readonly Guid Sender;          // DID отправителя (16 байт)
            internal readonly string Skin;
            internal readonly bool Hello;

            internal Incoming(IntPtr component, Guid sender, string skin, bool hello)
            {
                Component = component;
                Sender = sender;
                Skin = skin;
                Hello = hello;
            }
        }

        // Хук OnReceive — сетевой код игры; разбираем пакеты в Update
        private static readonly object _inboxLock = new();
        private static readonly List<Incoming> _inbox = new();

        private static readonly Dictionary<Guid, Peer> _peers = new();
        // Скин, который владелец объявил своему экземпляру класса. Забываем, только если класса не видно
        // AnnouncedTtl: FindObjectsOfType не отдаёт выключенные объекты, а класс бывает выключен на миг
        private sealed class Announced
        {
            internal string Skin;
            internal float Seen;
        }
        private const float AnnouncedTtl = 60f;
        private static readonly Dictionary<IntPtr, Announced> _announced = new();
        private static readonly Dictionary<IntPtr, Remote> _remotes = new();
        private static volatile int _otherVersion = -1;   // версия чужого протокола из пакета (-1 — не было)
        private static int _reportedVersion = -1;
        private static readonly HashSet<string> _reportedMissing = new();

        private static IntPtr _sentInstance;  // экземпляр своего класса, о котором уже рассказали
        private static string _sentSkin;
        private static string _loggedSkin;
        private static float _nextHello, _nextPoll, _nextBurst, _nextScan;
        private static float _lastHello = float.NegativeInfinity;

        /// <summary>Внеочередной «привет» не позже at (HelloMinGap держит Publish).</summary>
        private static void HelloSoon(float at) => _nextHello = Math.Min(_nextHello, at);
        private static int _burstLeft;
        private static bool _reportedLong;
        private static string _state;          // последнее состояние сети в логе — пишем только смены

        // API игры: методы ищем по имени (обёртки Il2CppInterop передают DID как 1 байт — нельзя)
        private static bool _apiTried, _apiReady;
        private static IntPtr _sendRpc, _sendRpcTo, _byteListClass;

        // Хук зовётся на каждый RPC игры, поэтому оригинал зовём указателем на функцию, а не делегатом
        // NativeHook.Trampoline. [UnmanagedCallersOnly] для обхода не взять: такой же атрибут есть в
        // UnityEngine.CoreModule, и компилятор их не различает
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void OnReceiveFn(IntPtr self, IntPtr message, IntPtr sender, IntPtr method);
        private static OnReceiveFn _detour;   // держим, чтобы GC не собрал
        private static NativeHook<OnReceiveFn> _hook;
        private static unsafe delegate* unmanaged<IntPtr, IntPtr, IntPtr, IntPtr, void> _original;

        /// <summary>Каждый кадр: свой скин — тем, у кого мод, чужие — надеть.</summary>
        internal static void Update(CombatClass local, string localSkin)
        {
            if (!Session())
            {
                ResetSession();
                return;
            }
            if (!EnsureApi()) return;

            float now = Time.unscaledTime;
            bool canSend = Ready(local);
            string skin = Clamp(localSkin ?? "");

            int other = _otherVersion;
            if (other >= 0 && other != _reportedVersion)
            {
                _reportedVersion = other;
                Core.Log.Warning($"Игрок с другой версией мода (протокол {other}, у нас {Version}) — скины друг друга не видно, обновитесь до одной версии.");
            }

            try { Drain(local, canSend, skin, now); }
            catch (Exception e) { Core.Log.Error("Пакеты скинов: " + e.Message); }

            if (canSend)
            {
                try { Publish(local, skin, now); }
                catch (Exception e) { Core.Log.Error("Отправка скина: " + e.Message); }
            }
            ExpirePeers(now);

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

            if (now < _nextScan) return;
            _nextScan = now + ScanInterval;
            try { Scan(local, now); }
            catch (Exception e) { Core.Log.Error("Скины других игроков: " + e.Message); }
        }

        private static void ResetSession()
        {
            _peers.Clear();
            _announced.Clear();
            lock (_inboxLock) _inbox.Clear();
            _sentInstance = IntPtr.Zero;
            _sentSkin = null;
            _nextHello = _nextPoll = 0f;
            _lastHello = float.NegativeInfinity;
            _burstLeft = 0;
        }

        /// <summary>Свой класс можно использовать для отправки: сетевой, с ID и владельцем.</summary>
        private static bool Ready(CombatClass local)
        {
            if (local == null) return false;   // между смертью и появлением — обычное дело
            NetworkPrefab np = local.GetComponent<NetworkPrefab>();
            if (np == null)
            {
                State("у своего класса нет NetworkPrefab — другие игроки скин не увидят");
                return false;
            }
            if (!np.HasId || !np.HasOwner) return false;   // сетевой ID выдают не сразу после появления
            State("работает");
            return true;
        }

        private static string Clamp(string skin)
        {
            if (Encoding.UTF8.GetByteCount(skin) <= MaxSkinBytes) return skin;
            if (!_reportedLong) Core.Log.Warning($"Имя скина '{skin}' длиннее {MaxSkinBytes} байт — другие игроки его не увидят.");
            _reportedLong = true;
            return "";
        }

        private static void Drain(CombatClass local, bool canSend, string skin, float now)
        {
            List<Incoming> batch;
            lock (_inboxLock)
            {
                if (_inbox.Count == 0) return;
                batch = new List<Incoming>(_inbox);
                _inbox.Clear();
            }

            foreach (Incoming m in batch)
            {
                if (!_peers.TryGetValue(m.Sender, out Peer p))
                {
                    _peers[m.Sender] = p = new Peer();
                    Core.Log.Msg($"Найден игрок с модом (всего {_peers.Count}).");
                }
                p.LastHeard = now;
                _announced[m.Component] = new Announced { Skin = m.Skin, Seen = now };

                if (!m.Hello || !canSend || now - p.LastReply < ReplyCooldown) continue;
                p.LastReply = now;
                Send(local, m.Sender, false, skin);
            }
        }

        private static void Publish(CombatClass local, string skin, float now)
        {
            bool newInstance = local.Pointer != _sentInstance;
            if (newInstance || skin != _sentSkin)
            {
                _burstLeft = BurstSends;
                _nextBurst = now;
                if (skin != _loggedSkin)   // новый экземпляр класса с тем же скином — не пишем
                    Core.Log.Msg($"Другим игрокам с модом: {(skin.Length > 0 ? "скин " + skin : "родной вид")}.");
                _loggedSkin = skin;
                _sentInstance = local.Pointer;
                _sentSkin = skin;
                // Копия нового экземпляра у других появляется не сразу — «привет» в первый миг теряется
                if (newInstance) HelloSoon(now + HelloAfterSpawn);
            }

            if (now >= _nextHello && now - _lastHello >= HelloMinGap)
            {
                Send(local, null, true, skin);
                _lastHello = now;
                _nextHello = now + HelloInterval;
            }

            bool due = false;
            if (_burstLeft > 0 && now >= _nextBurst)
            {
                _burstLeft--;
                _nextBurst = now + BurstInterval;
                due = true;
            }
            if (now >= _nextPoll) due = true;
            if (!due) return;
            _nextPoll = now + PollInterval;
            foreach (Guid id in _peers.Keys) Send(local, id, false, skin);
        }

        private static void ExpirePeers(float now)
        {
            if (_peers.Count == 0) return;
            List<Guid> gone = null;
            foreach (var kv in _peers)
                if (now - kv.Value.LastHeard > PeerTimeout) (gone ??= new()).Add(kv.Key);
            if (gone == null) return;
            foreach (Guid id in gone) _peers.Remove(id);
            Core.Log.Msg($"Игрок с модом пропал (осталось {_peers.Count}).");
        }

        private static void State(string state)
        {
            if (state == _state) return;
            _state = state;
            Core.Log.Msg("Сеть: " + state);
        }

        private static void Scan(CombatClass local, float now)
        {
            var seen = new HashSet<IntPtr>();
            foreach (CombatClass cc in Object.FindObjectsOfType<CombatClass>())
            {
                if (cc == null || (local != null && cc.Pointer == local.Pointer)) continue;
                NetworkPrefab np = cc.GetComponent<NetworkPrefab>();
                if (np == null || np.IsOwned) continue;   // свой (или не сетевой) класс

                seen.Add(cc.Pointer);
                if (!_remotes.TryGetValue(cc.Pointer, out Remote r))
                {
                    _remotes[cc.Pointer] = r = new Remote();
                    // Новый чужой класс — может, это игрок с модом, который о нас ещё не знает
                    if (!_announced.ContainsKey(cc.Pointer)) HelloSoon(now + HelloAfterSpawn);
                }
                if (!_announced.TryGetValue(cc.Pointer, out Announced a)) continue;   // игрок без мода или ещё не прислал
                a.Seen = now;
                string skin = a.Skin;
                if (skin == r.Skin) continue;
                if (r.Skin != null)   // null — тот же скин надеваем заново после выгрузки, это не новость
                    Core.Log.Msg($"Другой игрок на {Core.Title(Core.KeyOf(cc))}: {(skin.Length > 0 ? "скин " + skin : "родной вид")}.");
                r.Applied?.Remove();
                r.Applied = null;
                r.Skin = skin;
                if (skin.Length > 0) r.Applied = Wear(cc, skin);
            }

            foreach (IntPtr gone in _remotes.Keys.Where(p => !seen.Contains(p)).ToList()) _remotes.Remove(gone);
            foreach (IntPtr gone in _announced.Where(kv => now - kv.Value.Seen > AnnouncedTtl).Select(kv => kv.Key).ToList())
                _announced.Remove(gone);
        }

        private static AppliedSkin Wear(CombatClass cc, string skinName)
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

        // ---------- отправка ----------

        /// <summary>Пакет со своего CombatClass: target — лично этому игроку, null — всем.</summary>
        private static unsafe void Send(CombatClass local, Guid? target, bool hello, string skin)
        {
            byte[] name = Encoding.UTF8.GetBytes(skin);
            var packet = new byte[HeaderBytes + name.Length];
            packet[0] = RpcIndex;
            packet[1] = (byte)'S';
            packet[2] = (byte)'K';
            packet[3] = Version;
            packet[4] = hello ? FlagHello : (byte)0;
            packet[5] = (byte)name.Length;
            name.CopyTo(packet, HeaderBytes);
            var buf = new ByteBuffer(packet.Length, 64);   // запас спереди — под номер компонента
            buf.Add(new Il2CppInterop.Runtime.InteropTypes.Arrays.Il2CppStructArray<byte>(packet));   // одним вызовом

            byte priority = PacketPriority.LOW;
            byte reliable = 0;
            IntPtr* args = stackalloc IntPtr[4];
            IntPtr method;
            if (target is Guid to)
            {
                method = _sendRpcTo;
                args[0] = (IntPtr)(&to);
                args[1] = (IntPtr)(&priority);
                args[2] = (IntPtr)(&reliable);
                args[3] = buf.Pointer;
            }
            else
            {
                method = _sendRpc;
                args[0] = (IntPtr)(&priority);
                args[1] = (IntPtr)(&reliable);
                args[2] = buf.Pointer;
            }
            IntPtr exc = IntPtr.Zero;
            IL2CPP.il2cpp_runtime_invoke(method, local.Pointer, (void**)args, ref exc);
            Il2CppInterop.Runtime.Il2CppException.RaiseExceptionIfNecessary(exc);
        }

        // ---------- приём ----------

        private static unsafe bool EnsureApi()
        {
            if (_apiTried) return _apiReady;
            _apiTried = true;
            try
            {
                IntPtr component = IL2CPP.GetIl2CppClass("Dust.Networking.dll", "Dust.Networking", "NetworkComponent");
                _byteListClass = IL2CPP.GetIl2CppClass("Dust.Networking.dll", "", "ByteList");
                if (component == IntPtr.Zero || _byteListClass == IntPtr.Zero)
                    throw new Exception("нет класса NetworkComponent или ByteList");
                _sendRpc = IL2CPP.il2cpp_class_get_method_from_name(component, "SendRpc", 3);
                _sendRpcTo = IL2CPP.il2cpp_class_get_method_from_name(component, "SendRpcTo", 4);
                IntPtr onReceive = IL2CPP.il2cpp_class_get_method_from_name(component, "OnReceive", 2);
                if (_sendRpc == IntPtr.Zero || _sendRpcTo == IntPtr.Zero || onReceive == IntPtr.Zero)
                    throw new Exception("нет NetworkComponent.SendRpc/SendRpcTo/OnReceive");

                _detour = OnReceive;
                _hook = new NativeHook<OnReceiveFn>(*(IntPtr*)onReceive, Marshal.GetFunctionPointerForDelegate(_detour));
                _hook.Attach();
                _original = (delegate* unmanaged<IntPtr, IntPtr, IntPtr, IntPtr, void>)_hook.TrampolineHandle;
                _apiReady = true;
            }
            catch (Exception e)
            {
                State("сеть мода не запустилась: " + e.Message);
            }
            return _apiReady;
        }

        /// <summary>
        /// NetworkComponent.OnReceive(ReadOnlySpan&lt;byte&gt; message, DID sender). На x64 обе структуры по
        /// 16 байт приходят указателями. ReadOnlySpan: ushort startIndex, ushort length, IReadOnlyList items;
        /// items — упакованный ByteList (IntPtr ptr, int count) — так его строит NetworkPrefab.OnReceive.
        /// </summary>
        private static unsafe void OnReceive(IntPtr self, IntPtr message, IntPtr sender, IntPtr method)
        {
            try
            {
                if (TryTake(self, message, sender)) return;   // наш пакет — игре он не нужен
            }
            catch { }
            _original(self, message, sender, method);
        }

        private static unsafe bool TryTake(IntPtr self, IntPtr message, IntPtr sender)
        {
            if (message == IntPtr.Zero || sender == IntPtr.Zero) return false;
            int start = *(ushort*)message;
            int length = *(ushort*)(message + 2);
            IntPtr items = *(IntPtr*)(message + 8);
            if (length < HeaderBytes || items == IntPtr.Zero || *(IntPtr*)items != _byteListClass) return false;

            byte* data = *(byte**)(items + 16);
            int count = *(int*)(items + 24);
            if (data == null || start + length > count) return false;
            byte* p = data + start;
            if (p[0] != RpcIndex || p[1] != 'S' || p[2] != 'K') return false;

            if (p[3] != Version) _otherVersion = p[3];   // пакет наш, но разобрать не можем — скажем в Update
            else
            {
                int n = p[5];
                if (HeaderBytes + n <= length && n <= MaxSkinBytes)
                {
                    var m = new Incoming(self, *(Guid*)sender, Encoding.UTF8.GetString(p + HeaderBytes, n), (p[4] & FlagHello) != 0);
                    lock (_inboxLock) _inbox.Add(m);
                }
            }
            return true;
        }

        /// <summary>
        /// NetworkManager, если игра в сети. HasSession для этого не годится (в матче он false — похоже,
        /// это вход в аккаунт), поэтому смотрим, есть ли сетевой клиент и не офлайн ли Matchmaker.
        /// </summary>
        private static bool Session()
        {
            try
            {
                NetworkManager nm = NetworkManager.Instance;
                return nm != null && Matchmaker.Instance != null && nm.Client != null && !Matchmaker.offlineMode;
            }
            catch (Exception e)
            {
                State("NetworkManager недоступен: " + e.Message);
                return false;
            }
        }
    }
}
