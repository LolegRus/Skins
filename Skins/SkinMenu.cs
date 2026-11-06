using UnityEngine;
using UnityEngine.InputSystem;

namespace Skins
{
    /// <summary>
    /// Меню скинов слева сверху (F2): только класс, за который сейчас играешь, его скины — плитками по две в ряд.
    /// Первая плитка — родной вид. Выбор — клик мышью по плитке или цифра 1..9; скин надевается сразу.
    /// Курсор — как в SandboxTools: пока меню открыто, каждый кадр отпускаем системный курсор (игра залочивает
    /// его обратно) и выключаем карты ввода игры — камера не крутится, клик не стреляет. Закрыли — всё возвращаем.
    /// Системный курсор при этом невидим: рисуем свой, в стиле меню, — по позиции системного, а если игра
    /// всё же залочила курсор, — по дельте мыши.
    /// Вид — как у меню на Quest: цвета «графит», имя класса в углу, шрифт Pixelify Sans (SkinLibrary.MenuFont).
    /// Тексты — по-английски: у мода англоязычные пользователи.
    /// </summary>
    internal sealed class SkinMenu
    {
        private static readonly Color ColPanel = new Color32(0x12, 0x13, 0x14, 0xF7);
        private static readonly Color ColPanelLine = new Color(1f, 1f, 1f, 0.44f);
        private static readonly Color ColTile = new Color32(0x22, 0x24, 0x27, 0xFF);
        private static readonly Color ColTileHover = new Color32(0x2E, 0x31, 0x35, 0xFF);
        private static readonly Color ColWorn = new Color32(0x3A, 0x3D, 0x42, 0xFF);
        private static readonly Color ColWornLine = Color.white;
        private static readonly Color ColText = new Color32(0xE6, 0xE7, 0xE9, 0xFF);
        private static readonly Color ColDim = new Color32(0x8C, 0x8F, 0x94, 0xFF);

        private const float X = 16f, Y = 16f, Width = 320f;
        private const float Pad = 14f, HeaderH = 30f, TileH = 52f, Gap = 8f, FooterH = 24f, Line = 2f;
        private const int Columns = 2;
        // Плитки — страницами по PageSize (2 × 2); первая плитка первой страницы — родной вид.
        // Страниц больше одной — внизу «<», номер страницы, «>» (и клавиши ← →); цифры 1–4 — плитки страницы
        private const int PageSize = 4;
        private const float NavH = 28f, NavW = 48f;
        // ТЕСТ: столько кнопок-заглушек («Test 1»…) добавляется к скинам — проверить страницы. Перед релизом — 0
        private const int TestTiles = 0;
        private static readonly string[] TestNames = { "Test 1", "Test Two", "T3", "Test Number 4", "Test 5" };
        // Шрифт плиток один на все плитки: длинное имя (Code Breaker) рядом с коротким (Gear) — тем же размером
        private const int TileFont = 15, MinTileFont = 9;
        private const float TextRoom = (Width - Pad * 2 - Gap * (Columns - 1)) / Columns - 20f;   // плитка без полей

        private readonly Core _core;
        private string _key;           // класс, за который играешь (null — без класса)
        private List<Skin> _skins = new();
        private readonly List<string> _names = new();   // имена плиток: родной вид, скины, заглушки (тест)
        private int _page;
        private bool _pageChosen;      // страница надетого уже выбрана при открытии
        private string _testPick;      // ТЕСТ: выбранная заглушка — только подсветка, в настройки не пишется

        private Vector2 _cursor = new Vector2(-1f, -1f);
        private bool _cursorValid, _systemCursor;

        private readonly List<InputActionMap> _blocked = new();   // карты игры, выключенные на время меню
        private float _nextScan;

        private Font _font;
        private GUIStyle _panel, _round, _class, _tile, _tileOn, _dim, _dimCenter, _num, _arrowOff;
        private string _fittedFor;     // имена, под которые подобран шрифт плиток

        internal bool Open { get; private set; }

        internal SkinMenu(Core core) => _core = core;

        internal void Toggle()
        {
            Open = !Open;
            _cursorValid = false;
            if (Open)
            {
                _blocked.Clear();
                _nextScan = 0f;
                _pageChosen = false;
                _testPick = null;
                return;
            }
            RestoreGameInput();
            Cursor.lockState = CursorLockMode.Locked;
            Cursor.visible = false;
        }

        internal void Update(Keyboard kb)
        {
            if (!Open) return;
            FreeCursor();
            BlockGameInput();

            // Класс может смениться при открытом меню (смерть, другой класс) — список скинов берём каждый кадр
            _key = _core.CurrentKey;
            _skins = _key != null && Game.IsPcClass(_key) ? SkinLibrary.For(_key) : new List<Skin>();
            _names.Clear();
            if (_key == null) return;
            _names.Add("Original");
            foreach (var s in _skins) _names.Add(s.Name);
            for (int i = 0; i < TestTiles; i++) _names.Add(TestNames[i % TestNames.Length] + (i >= TestNames.Length ? " b" : ""));

            int count = _names.Count, pages = Pages;
            if (!_pageChosen)   // открываем на странице надетого
            {
                _pageChosen = true;
                int worn = _names.IndexOf(Shown());
                _page = worn > 0 ? worn / PageSize : 0;
            }
            _page = Mathf.Clamp(_page, 0, pages - 1);

            int onPage = Math.Min(PageSize, count - _page * PageSize);
            for (int i = 0; i < onPage; i++)
                if (kb[Key.Digit1 + i].wasPressedThisFrame || kb[Key.Numpad1 + i].wasPressedThisFrame) Pick(_page * PageSize + i);
            if (kb.leftArrowKey.wasPressedThisFrame) Turn(-1);
            if (kb.rightArrowKey.wasPressedThisFrame) Turn(1);

            var mouse = Mouse.current;
            if (mouse == null) return;
            UpdateCursor(mouse);
            if (!mouse.leftButton.wasPressedThisFrame) return;
            for (int i = 0; i < onPage; i++)
                if (TileRect(i).Contains(_cursor)) Pick(_page * PageSize + i);
            if (pages > 1)
            {
                if (NavRect(-1).Contains(_cursor)) Turn(-1);
                if (NavRect(1).Contains(_cursor)) Turn(1);
            }
        }

        private int Pages => Math.Max(1, (_names.Count + PageSize - 1) / PageSize);

        private void Turn(int step) => _page = Mathf.Clamp(_page + step, 0, Pages - 1);

        private void Pick(int index)
        {
            if (index > _skins.Count)   // ТЕСТ: заглушка — только подсветить
            {
                _testPick = _names[index];
                return;
            }
            _testPick = null;
            _core.Select(_key, index == 0 ? null : _skins[index - 1].Name);
        }

        // Что подсвечено как надетое: тестовая заглушка (если выбрана) или скин из настроек; "" — родной вид
        private string Shown() => _testPick ?? _core.Selected(_key) ?? "";

        // Системный курсор отпущен — берём его позицию; залочен (игра успела вернуть) — копим дельту.
        // Позицию залоченного не берём: она каждый кадр прыгает в центр экрана, и курсор «прилипал» к центру.
        private void UpdateCursor(Mouse mouse)
        {
            _systemCursor = Cursor.lockState == CursorLockMode.None;
            if (_systemCursor)
            {
                Vector2 abs = mouse.position.ReadValue();
                _cursor = new Vector2(abs.x, Screen.height - abs.y);   // вход снизу, GUI сверху
            }
            else
            {
                if (!_cursorValid) _cursor = new Vector2(X + Width / 2, Y + 100f);
                Vector2 delta = mouse.delta.ReadValue();
                _cursor += new Vector2(delta.x, -delta.y);
            }
            _cursor = new Vector2(Mathf.Clamp(_cursor.x, 0f, Screen.width), Mathf.Clamp(_cursor.y, 0f, Screen.height));
            _cursorValid = true;
        }

        private static void FreeCursor()
        {
            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = false;   // свободен, но невидим — рисуем свой
        }

        // Выключаем включённые карты ввода игры; каждый кадр — снова (после загрузки сцены игра включает их
        // обратно), раз в секунду ищем новые. Выключенные игрой не трогаем — и потом не включаем.
        private void BlockGameInput()
        {
            foreach (var map in _blocked)
                try { if (map != null && map.enabled) map.Disable(); } catch { }

            if (Time.unscaledTime < _nextScan) return;
            _nextScan = Time.unscaledTime + 1f;
            var assets = new List<InputActionAsset>();
            try { if (InputSystem.actions != null) assets.Add(InputSystem.actions); } catch { }
            try
            {
                foreach (var a in Resources.FindObjectsOfTypeAll<InputActionAsset>())
                    if (a != null && !assets.Contains(a)) assets.Add(a);
            }
            catch { }
            foreach (var asset in assets)
            {
                var maps = asset.actionMaps;
                for (int i = 0; i < maps.Count; i++)
                {
                    try
                    {
                        var map = maps[i];
                        if (map == null || !map.enabled) continue;
                        map.Disable();
                        if (!_blocked.Contains(map)) _blocked.Add(map);
                    }
                    catch { }
                }
            }
        }

        private void RestoreGameInput()
        {
            foreach (var map in _blocked)
                try { if (map != null && !map.enabled) map.Enable(); } catch { }
            _blocked.Clear();
        }

        private static Rect TileRect(int i)
        {
            float w = (Width - Pad * 2 - Gap * (Columns - 1)) / Columns;
            float x = X + Pad + (i % Columns) * (w + Gap);
            float y = Y + Pad + HeaderH + (i / Columns) * (TileH + Gap);
            return new Rect(x, y, w, TileH);
        }

        // Кнопка страниц: -1 — «<» слева, +1 — «>» справа; ряд под двумя рядами плиток
        private static Rect NavRect(int side)
        {
            float y = Y + Pad + HeaderH + 2 * (TileH + Gap);
            float x = side < 0 ? X + Pad : X + Width - Pad - NavW;
            return new Rect(x, y, NavW, NavH);
        }

        internal void Draw()
        {
            if (!Open) return;
            if (Event.current.type == EventType.Repaint) FreeCursor();   // игра могла залочить его после Update
            EnsureStyles();

            float x = X + Pad, w = Width - Pad * 2;
            int count = _key != null ? _names.Count : 0, pages = Pages;
            // Высота не меняется от страницы к странице: при нескольких страницах — всегда два ряда и ряд стрелок
            int rows = pages > 1 ? 2 : (count + Columns - 1) / Columns;
            float body = count > 0 ? rows * (TileH + Gap) - Gap + (pages > 1 ? Gap + NavH : 0) : 40f;
            var panel = new Rect(X, Y, Width, Pad + HeaderH + body + FooterH + Pad);
            Shape(panel, ColPanelLine, _panel);
            Shape(Inset(panel, Line), ColPanel, _panel);

            float y = Y + Pad;
            GUI.Label(new Rect(x, y, w, HeaderH - 6), _key != null ? Core.Title(_key) : "Skins", _class);
            GUI.Label(new Rect(x, y, w, HeaderH - 6), "F2 to close", Right(_dim));
            y += HeaderH;

            if (_key == null)
            {
                GUI.Label(new Rect(x, y, w, body), "Spawn as a class\nto pick its skin", _dimCenter);
                if (_cursorValid) DrawCursor();
                return;
            }

            FitTiles(count);
            string selected = Shown();
            int onPage = Math.Min(PageSize, count - _page * PageSize);
            for (int i = 0; i < onPage; i++)
            {
                int index = _page * PageSize + i;
                Rect r = TileRect(i);
                bool on = index == 0 ? string.IsNullOrEmpty(selected) : _names[index] == selected;
                bool hover = _cursorValid && r.Contains(_cursor);
                if (on)
                {
                    Shape(r, ColWornLine, _round);
                    Shape(Inset(r, Line), ColWorn, _round);
                }
                else
                {
                    Shape(r, hover ? ColTileHover : ColTile, _round);
                }
                GUI.Label(r, _names[index], on || hover ? _tileOn : _tile);
                GUI.Label(new Rect(r.x + 7, r.y + 4, 16, 16), (i + 1).ToString(), _num);
            }
            if (count == 1)
                GUI.Label(TileRect(1), "No skins yet", _dimCenter);

            if (pages > 1)
            {
                // Стрелки страниц; у края — тусклая и не нажимается
                for (int side = -1; side <= 1; side += 2)
                {
                    Rect r = NavRect(side);
                    bool enabled = side < 0 ? _page > 0 : _page < pages - 1;
                    bool hover = enabled && _cursorValid && r.Contains(_cursor);
                    Shape(r, hover ? ColTileHover : ColTile, _round);
                    GUI.Label(r, side < 0 ? "<" : ">", enabled ? _tileOn : _arrowOff);
                }
                Rect nav = NavRect(-1);
                GUI.Label(new Rect(x, nav.y, w, NavH), $"{_page + 1}/{pages}", _dimCenter);
                y = nav.yMax;
            }
            else
            {
                y = TileRect(count - 1).yMax;
            }
            GUI.Label(new Rect(x, y + 6, w, FooterH),
                pages > 1 ? "Click, or 1-4 and arrow keys" : "Click a skin or press its number", _dim);

            if (_cursorValid) DrawCursor();
        }

        // Один размер шрифта на все плитки — под самое длинное имя; пересчёт, только когда меняются имена
        private void FitTiles(int count)
        {
            var names = _names;   // все страницы — шрифт не меняется при листании
            string key = string.Join("\n", names);
            if (key == _fittedFor) return;
            _fittedFor = key;

            int size = TileFont;
            _tile.fontSize = _tileOn.fontSize = TileFont;
            foreach (string name in names)
            {
                float width = _tile.CalcSize(new GUIContent(name)).x;
                if (width > TextRoom) size = Math.Min(size, (int)(TileFont * TextRoom / width));
            }
            size = Math.Max(size, MinTileFont);
            _tile.fontSize = _tileOn.fontSize = size;
        }

        // Курсор — маленький белый кружок с тонкой тёмной обводкой (виден и на светлом, и на тёмном).
        // Текстура — ровно в размер кружка, пиксель в пиксель с экраном: так край чёткий и без мыла.
        // Только то, что уже работает в меню: часть методов Unity в сборке игры вырезана («Method unstripping
        // failed»), в том числе GUI.DrawTexture (даже короткий вызывает версию с borderRadius) — рисуем через
        // GUI.Box со стилем, как все фигуры меню.
        private const int CursorSize = 9;
        private GUIStyle _dot;

        private void DrawCursor()
        {
            if (_dot == null)
            {
                _dot = new GUIStyle();
                _dot.normal.background = DotTexture(CursorSize);
            }
            float left = (float)Math.Floor(_cursor.x) - CursorSize / 2, top = (float)Math.Floor(_cursor.y) - CursorSize / 2;
            Shape(new Rect(left, top, CursorSize, CursorSize), Color.white, _dot);
        }

        // Круг со сглаженным краем (4×4 отсчёта на пиксель): белая середина, тёмное кольцо ~0.9 px
        private static Texture2D DotTexture(int size)
        {
            var tex = new Texture2D(size, size, TextureFormat.RGBA32, false);
            tex.filterMode = FilterMode.Point;
            tex.wrapMode = TextureWrapMode.Clamp;
            double r = size / 2.0, inner = r - 0.9;
            const float darkR = 0.07f, darkG = 0.075f, darkB = 0.08f, darkA = 0.85f;
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                {
                    int outerHits = 0, innerHits = 0;
                    for (int sy = 0; sy < 4; sy++)
                        for (int sx = 0; sx < 4; sx++)
                        {
                            double dx = x + (sx + 0.5) / 4 - r, dy = y + (sy + 0.5) / 4 - r;
                            double d2 = dx * dx + dy * dy;
                            if (d2 <= r * r) outerHits++;
                            if (d2 <= inner * inner) innerHits++;
                        }
                    if (outerHits == 0)
                    {
                        tex.SetPixel(x, y, new Color(0f, 0f, 0f, 0f));
                        continue;
                    }
                    float w = (float)innerHits / outerHits;   // доля белой середины в закрытой части пикселя
                    float coverage = outerHits / 16f;
                    tex.SetPixel(x, y, new Color(w + (1 - w) * darkR, w + (1 - w) * darkG, w + (1 - w) * darkB,
                        coverage * (w + (1 - w) * darkA)));
                }
            tex.Apply();
            tex.hideFlags = HideFlags.HideAndDontSave;
            return tex;
        }

        private static Rect Inset(Rect r, float d) => new Rect(r.x + d, r.y + d, r.width - d * 2, r.height - d * 2);

        private static void Shape(Rect r, Color c, GUIStyle style)
        {
            Color was = GUI.color;
            GUI.color = c;
            GUI.Box(r, GUIContent.none, style);
            GUI.color = was;
        }

        private readonly Dictionary<GUIStyle, GUIStyle> _right = new();
        private GUIStyle Right(GUIStyle st)
        {
            if (_right.TryGetValue(st, out var r)) return r;
            r = Make(st.fontSize, TextAnchor.MiddleRight, st.normal.textColor);
            _right[st] = r;
            return r;
        }

        private void EnsureStyles()
        {
            if (_class != null) return;
            _font = SkinLibrary.MenuFont();
            // У пиксельного шрифта нет жирного начертания (Unity нарисовала бы размазанный) — только обычный
            _class = Make(17, TextAnchor.MiddleLeft, Color.white);
            _tile = Make(TileFont, TextAnchor.MiddleCenter, ColText);
            _tileOn = Make(TileFont, TextAnchor.MiddleCenter, Color.white);
            _dim = Make(12, TextAnchor.MiddleLeft, ColDim);
            _dimCenter = Make(12, TextAnchor.MiddleCenter, ColDim);
            _num = Make(11, TextAnchor.UpperLeft, ColDim);
            _arrowOff = Make(TileFont, TextAnchor.MiddleCenter, new Color32(0x55, 0x58, 0x5D, 0xFF));

            _panel = RoundStyle(32, 10f);
            _round = RoundStyle(16, 6f);
        }

        private GUIStyle Make(int size, TextAnchor align, Color color)
        {
            var st = new GUIStyle();
            if (_font != null) st.font = _font;
            st.fontSize = size;
            st.alignment = align;
            st.wordWrap = false;
            st.clipping = TextClipping.Clip;
            st.normal.textColor = color;
            return st;
        }

        private static GUIStyle RoundStyle(int size, float radius)
        {
            var st = new GUIStyle();
            st.normal.background = RoundTexture(size, radius);
            int b = Mathf.CeilToInt(radius) + 2;
            st.border = new RectOffset(b, b, b, b);
            return st;
        }

        // Белая текстура со скруглёнными углами для девятисегментного стиля (как в SandboxTools:
        // GUI.DrawTexture с borderRadius в этой сборке вырезан).
        private static Texture2D RoundTexture(int size, float radius)
        {
            var tex = new Texture2D(size, size, TextureFormat.RGBA32, false);
            tex.filterMode = FilterMode.Bilinear;
            tex.wrapMode = TextureWrapMode.Clamp;
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                {
                    float px = x + 0.5f, py = y + 0.5f;
                    float dx = Mathf.Max(radius - px, px - (size - radius));
                    float dy = Mathf.Max(radius - py, py - (size - radius));
                    float dist = Mathf.Sqrt(dx * dx + dy * dy);
                    tex.SetPixel(x, y, new Color(1f, 1f, 1f, Mathf.Clamp01(radius - dist + 0.5f)));
                }
            tex.Apply();
            tex.hideFlags = HideFlags.HideAndDontSave;
            return tex;
        }
    }
}
