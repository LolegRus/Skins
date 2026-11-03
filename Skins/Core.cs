using Il2CppDust.Combat;
using MelonLoader;
using UnityEngine;
using UnityEngine.InputSystem;

[assembly: MelonInfo(typeof(Skins.Core), "Skins", "0.2.0", "LolegRus", null)]
[assembly: MelonGame("VRDojo", "Dust")]

namespace Skins
{
    public class Core : MelonMod
    {
        internal static MelonLogger.Instance Log;

        private const string Original = "";   // «без скина» в настройках

        private MelonPreferences_Category _prefs;
        private SkinMenu _menu;
        private AppliedSkin _applied;
        private CombatClass _class;           // класс локального игрока в этом кадре
        private IntPtr _seenClass;            // экземпляр класса, для которого уже решили, что надеть

        /// <summary>Ключ текущего класса локального игрока (null — без класса).</summary>
        internal string CurrentKey => _class != null ? KeyOf(_class) : null;

        public override void OnInitializeMelon()
        {
            Log = LoggerInstance;
            _prefs = MelonPreferences.CreateCategory("Skins");
            _menu = new SkinMenu(this);
            Log.Msg($"Initialized. F2 — меню скинов, Ctrl+F11 — дамп классов. Скины: {SkinLibrary.Folder}");
        }

        public override void OnUpdate()
        {
            _class = Game.CurrentClass(Game.FindLocalPlayer());
            IntPtr current = _class != null ? _class.Pointer : IntPtr.Zero;

            // Экземпляр сменился (спавн, смерть, другой класс) — старый скин забываем, надеваем выбранный.
            if (current != _seenClass)
            {
                if (_applied != null && _applied.Target != (_class != null ? _class.transform.Pointer : IntPtr.Zero)) _applied = null;
                _seenClass = current;
                if (_class != null) Wear(_class, Selected(KeyOf(_class)));
            }

            try
            {
                if (_applied != null && !_applied.Sync())
                {
                    string name = _applied.Skin.Name;
                    if (_class != null) Wear(_class, name);
                    else _applied = null;
                }
            }
            catch (Exception e) { Log.Error("Скин: " + e.Message); _applied = null; }

            SkinNet.Update(_class, _applied?.Skin.Name);

            var kb = Keyboard.current;
            if (kb == null) return;

            bool ctrl = kb.leftCtrlKey.isPressed || kb.rightCtrlKey.isPressed;
            if (kb.f2Key.wasPressedThisFrame && !ctrl) _menu.Toggle();
            if (kb.f11Key.wasPressedThisFrame && ctrl) Diagnostics.DumpAll();

            try { _menu.Update(kb); }
            catch (Exception e) { Log.Error("Меню скинов: " + e); }
        }

        public override void OnGUI()
        {
            try { _menu.Draw(); }
            catch (Exception e) { Log.Error("Меню скинов: " + e.Message); }
        }

        /// <summary>Выбор скина для класса (null — родной вид). Текущий класс переодевается сразу.</summary>
        internal void Select(string key, string skinName)
        {
            Entry(key).Value = skinName ?? Original;
            _prefs.SaveToFile(false);
            Log.Msg($"{Title(key)}: {skinName ?? "родной вид"}");
            if (_class != null && KeyOf(_class) == key) Wear(_class, skinName ?? Original);
        }

        /// <summary>Выбранный скин класса. Свой скин — только у ПК-классов: VR-классы на ПК в родном виде.</summary>
        internal string Selected(string key) => Game.IsPcClass(key) ? Entry(key).Value : Original;

        private void Wear(CombatClass cc, string skinName)
        {
            try
            {
                _applied?.Remove();
                _applied = null;

                string key = KeyOf(cc);
                Skin skin = skinName == Original ? null : SkinLibrary.For(key).Find(s => s.Name == skinName);
                if (skinName != Original && skin == null) Log.Warning($"Скин '{skinName}' для {key} не найден — родной вид.");
                if (skin != null) _applied = new AppliedSkin(skin, cc.transform, "свой");
            }
            catch (Exception e)
            {
                Log.Error($"Скин '{skinName}': {e}");
                _applied = null;
            }
        }

        private MelonPreferences_Entry<string> Entry(string key) =>
            _prefs.GetEntry<string>(key) ?? _prefs.CreateEntry(key, Original, description: $"Скин для {Title(key)} (имя файла без .skin, пусто — родной вид)");

        internal static string KeyOf(CombatClass cc) => cc.classType.ToString().ToUpperInvariant();

        internal static string Title(string key) => key.Length > 1 ? key[0] + key.Substring(1).ToLowerInvariant() : key;
    }
}
