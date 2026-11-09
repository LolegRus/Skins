using Il2CppTMPro;
using MelonLoader;
using System.Reflection;
using UnityEngine;
using UnityEngine.InputSystem;
using Object = UnityEngine.Object;

namespace Skins
{
    /// <summary>
    /// Меню на экране терминала в хабе (макет Cinder): слева «DustOS» — классы, у которых есть скины, справа —
    /// версия мода и скины выбранного класса; плитки по 2 × 3, стрелки страниц внизу, «Review Skin» — показать
    /// выбранный скин на голо-пьедестале. Это библиотека: скин здесь не надевается (надеть — F2).
    /// Управление: навести прицел на экран (появится курсор) и нажать левую кнопку мыши.
    /// Всё — объекты в мире на экране монитора: прямоугольники без света (UI/Default) и надписи TextMeshPro
    /// шрифтом Pixelify Sans; кто поверх кого — sortingOrder (по расстоянию надписи уходили бы под плитки).
    /// </summary>
    internal sealed class TerminalMenu
    {
        // Экран модели (оси модели до масштаба): центр, размер, плоскость чуть перед гранью экрана (z -0.807)
        private const float CenterY = 1.612f, FrontZ = -0.803f;
        private const float W = 1.884f, H = 1.102f;
        private const float Reach = 7f;          // дальше — экран не ловит прицел

        private const float Pad = 0.035f, PanelGap = 0.03f, BarH = 0.11f, BarGap = 0.025f, TileGap = 0.022f;
        private const int Cols = 2, Rows = 3, PageSize = Cols * Rows;

        private static readonly Color ColBack = new Color(0.90f, 0.93f, 0.97f);
        private static readonly Color ColPanel = new Color(0.27f, 0.57f, 0.87f);
        private static readonly Color ColBar = new Color(0.17f, 0.45f, 0.79f);
        private static readonly Color ColHeader = new Color(0.55f, 0.76f, 0.95f);
        private static readonly Color ColHeaderText = new Color(0.05f, 0.16f, 0.36f);
        private static readonly Color ColTile = new Color(0.89f, 0.90f, 0.91f);
        private static readonly Color ColTileHover = new Color(0.78f, 0.88f, 0.98f);
        private static readonly Color ColTileOn = new Color(0.07f, 0.22f, 0.48f);
        private static readonly Color ColText = new Color(0.08f, 0.08f, 0.09f);
        private static readonly Color ColArrowOff = new Color(1f, 1f, 1f, 0.3f);
        private static readonly Color ColCursorLine = new Color(0.05f, 0.12f, 0.28f);
        private static readonly Color ColCursorFill = Color.white;
        private static readonly Color ColCursorHot = new Color(1f, 0.82f, 0.25f);   // над кнопкой — золото пьедестала

        // Слои: чем больше, тем ближе к глазу (и sortingOrder, и сдвиг к зрителю)
        private const int LBack = 0, LPanel = 1, LBar = 2, LTile = 3, LText = 4, LRipple = 5, LCursor = 6;

        // Курсор — пиксельная стрелка (B — обводка, W — заливка), остриё — левый верхний пиксель
        private static readonly string[] Pointer =
        {
            "B",
            "BB",
            "BWB",
            "BWWB",
            "BWWWB",
            "BWWWWB",
            "BWWWWWB",
            "BWWWWWWB",
            "BWWWWWWWB",
            "BWWWWWWWWB",
            "BWWWWWBBBBB",
            "BWWBWWB",
            "BWB BWWB",
            "BB  BWWB",
            "B    BWWB",
            "     BWWB",
            "      BB",
        };
        private const float Px = 0.0055f;            // пиксель курсора, м (до масштаба терминала)
        private const float PressTime = 0.12f, RippleTime = 0.35f;

        private sealed class Button
        {
            internal Rect Rect;
            internal MeshRenderer Back;       // плитка (у стрелок — сам треугольник)
            internal TextMeshPro Label;
            internal Action Click;
            internal Func<bool> On;           // выбрана (тёмная плитка)
            internal Func<bool> Enabled;
            internal bool Arrow;
            internal int Index;               // номер плитки на странице
        }

        private readonly Transform _ui;
        private readonly Hologram _holo;
        private readonly List<Button> _buttons = new();
        private readonly Button[] _classTiles = new Button[PageSize], _skinTiles = new Button[PageSize];
        private readonly Dictionary<Color, Material> _mats = new();
        private Transform _cursor, _ripple;
        private MeshFilter _cursorMesh;
        private Mesh _pointer, _pointerHot;
        private Material _rippleMat;
        private float _cursorScale = 1f, _pressT = 1f, _rippleT = 1f;
        private bool _onScreen;
        private Vector2 _point;
        private Button _hover;

        private List<string> _classes = new();
        private int _classPage, _skinPage;
        private string _class, _skin;
        private float _nextClassScan;

        /// <summary>Скин, который сейчас показан на пьедестале (класс и имя).</summary>
        internal (string Class, string Skin)? Reviewed { get; private set; }

        internal TerminalMenu(Transform terminal)
        {
            var go = new GameObject("Skins Terminal Menu");
            _ui = go.transform;
            _ui.SetParent(terminal, false);
            _ui.localPosition = new Vector3(0f, CenterY, FrontZ);
            _ui.localRotation = Quaternion.Euler(0f, 180f, 0f);   // экран смотрит в +Z модели; у меню лицо — к -Z
            Build();
            _holo = new Hologram(terminal);
            Refresh();
        }

        // ---- раскладка ----

        private void Build()
        {
            Box("Back", new Rect(-W / 2, -H / 2, W, H), ColBack, LBack);
            float pw = (W - Pad * 2 - PanelGap) / 2;
            for (int side = 0; side < 2; side++)
            {
                float x = -W / 2 + Pad + side * (pw + PanelGap);
                float top = H / 2 - Pad, bottom = -H / 2 + Pad;
                Box("Panel", new Rect(x, bottom, pw, top - bottom), ColPanel, LPanel);
                var header = new Rect(x, top - BarH, pw, BarH);
                Box("Header", header, ColHeader, LBar);
                Text("Title", Inset(header, 0.03f, 0.018f), side == 0 ? "DustOS" : "Skins v" + Version(), ColHeaderText,
                    TextAlignmentOptions.Left);
                var footer = new Rect(x, bottom, pw, BarH);
                Box("Footer", footer, ColBar, LBar);

                float tilesTop = top - BarH - BarGap, tilesBottom = bottom + BarH + BarGap;
                float tw = (pw - TileGap * (Cols + 1)) / Cols, th = (tilesTop - tilesBottom - TileGap * (Rows - 1)) / Rows;
                for (int i = 0; i < PageSize; i++)
                {
                    int c = i % Cols, r = i / Cols;
                    var rect = new Rect(x + TileGap + c * (tw + TileGap), tilesTop - (r + 1) * th - r * TileGap, tw, th);
                    var b = Tile(rect, i, side == 0);
                    (side == 0 ? _classTiles : _skinTiles)[i] = b;
                }

                // Стрелки страниц: у классов — слева в подвале, у скинов — справа (слева — «Review Skin»)
                float ay = bottom + BarH / 2, aw = 0.075f, ah = 0.075f;
                float ax = side == 0 ? x + 0.05f : x + pw - 0.05f - aw * 2 - 0.04f;
                Arrow(new Rect(ax, ay - ah / 2, aw, ah), -1, side == 0);
                Arrow(new Rect(ax + aw + 0.04f, ay - ah / 2, aw, ah), 1, side == 0);
                if (side == 1) ReviewButton(new Rect(x + 0.03f, ay - 0.035f, 0.30f, 0.07f));
            }

            _pointer = PixelMesh(Pointer, ColCursorLine, ColCursorFill);
            _pointerHot = PixelMesh(Pointer, ColCursorLine, ColCursorHot);
            MeshRenderer cr = Part("Cursor", _pointer, Mat(Color.white), LCursor);
            _cursor = cr.transform;
            _cursorMesh = cr.GetComponent<MeshFilter>();
            _cursor.gameObject.SetActive(false);

            // Круг при клике: свой материал — прозрачность меняется, пока он расходится
            _rippleMat = Hub.Unlit(Color.white);
            _ripple = Part("Ripple", PixelMesh(Ring(7), ColCursorHot, ColCursorHot, centered: true), _rippleMat, LRipple).transform;
            _ripple.gameObject.SetActive(false);
        }

        private Button Tile(Rect rect, int index, bool classes)
        {
            var b = new Button
            {
                Rect = rect,
                Index = index,
                Back = Box(classes ? "Class" : "Skin", rect, ColTile, LTile),
                Label = Text("Label", Inset(rect, 0.03f, rect.height * 0.3f), "", ColText, TextAlignmentOptions.Center),
            };
            if (classes)
            {
                b.Click = () => PickClass(_classPage * PageSize + b.Index);
                b.On = () => Item(_classes, _classPage, b.Index) == _class;
            }
            else
            {
                b.Click = () => _skin = Item(SkinNames(), _skinPage, b.Index);
                b.On = () => _skin != null && Item(SkinNames(), _skinPage, b.Index) == _skin;
            }
            b.Enabled = () => b.Back.gameObject.activeSelf;
            _buttons.Add(b);
            return b;
        }

        private void Arrow(Rect rect, int step, bool classes)
        {
            var b = new Button { Rect = Inset(rect, -0.015f, -0.015f), Arrow = true };
            MeshRenderer r = Part(step < 0 ? "Left" : "Right", Meshes.Triangle(rect.width, rect.height, step > 0), Mat(Color.white), LTile);
            r.transform.localPosition = Place(rect.center, LTile);
            b.Back = r;
            if (classes)
            {
                b.Click = () => _classPage = Mathf.Clamp(_classPage + step, 0, Pages(_classes.Count) - 1);
                b.Enabled = () => step < 0 ? _classPage > 0 : _classPage < Pages(_classes.Count) - 1;
            }
            else
            {
                b.Click = () => _skinPage = Mathf.Clamp(_skinPage + step, 0, Pages(SkinNames().Count) - 1);
                b.Enabled = () => step < 0 ? _skinPage > 0 : _skinPage < Pages(SkinNames().Count) - 1;
            }
            _buttons.Add(b);
        }

        private void ReviewButton(Rect rect)
        {
            var b = new Button
            {
                Rect = rect,
                Back = Box("Review", rect, ColTile, LTile),
                Label = Text("Label", Inset(rect, 0.02f, 0.014f), "Review Skin", ColText, TextAlignmentOptions.Center),
                Click = Review,
                Enabled = () => _skin != null,
            };
            _buttons.Add(b);
        }

        // ---- данные ----

        // Имена скинов выбранного класса — один список на класс (Refresh, плитки и стрелки спрашивают его каждый кадр)
        private List<string> _skinNames = new();
        private string _skinNamesFor;

        private List<string> SkinNames()
        {
            if (_class == _skinNamesFor) return _skinNames;
            _skinNamesFor = _class;
            _skinNames = _class != null ? SkinLibrary.For(_class).Select(s => s.Name).ToList() : new List<string>();
            return _skinNames;
        }

        private static string Item(List<string> list, int page, int index)
        {
            int i = page * PageSize + index;
            return i >= 0 && i < list.Count ? list[i] : null;
        }

        private static int Pages(int count) => Math.Max(1, (count + PageSize - 1) / PageSize);

        // Классы, у которых есть хоть один скин; пока реестр классов пуст (загрузка) — ищем раз в секунду
        private void ScanClasses()
        {
            if (_classes.Count > 0 || Time.unscaledTime < _nextClassScan) return;
            _nextClassScan = Time.unscaledTime + 1f;
            _classes = Game.ClassPrefabs().Select(c => c.key.ToUpperInvariant()).Distinct()
                .Where(k => SkinLibrary.For(k).Count > 0).OrderBy(Core.Title, StringComparer.OrdinalIgnoreCase).ToList();
            if (_classes.Count == 0) return;
            Core.Log.Msg($"Терминал: классы со скинами — {string.Join(", ", _classes.Select(Core.Title))}.");
            // Сначала — свой класс, если у него есть скины
            string own = Core.Instance?.CurrentKey;
            PickClass(own != null && _classes.Contains(own) ? _classes.IndexOf(own) : 0);
            _classPage = _classes.IndexOf(_class) / PageSize;
        }

        private void PickClass(int index)
        {
            if (index < 0 || index >= _classes.Count) return;
            if (_classes[index] == _class) return;
            _class = _classes[index];
            _skin = null;
            _skinPage = 0;
        }

        private void Review()
        {
            if (_class == null || _skin == null) return;
            Reviewed = (_class, _skin);
            Core.Log.Msg($"Терминал: на пьедестал — {_skin} ({Core.Title(_class)}).");
            _holo.Show(_class, _skin);
        }

        // ---- кадр ----

        internal void Update()
        {
            if (!Game.IsAlive(_ui)) return;
            ScanClasses();
            _holo.Update();
            UpdateHover();

            if (_onScreen && Clicked())
            {
                _pressT = 0f;
                _rippleT = 0f;
                _ripple.localPosition = Place(_point, LRipple);
                if (_hover != null)
                {
                    try { _hover.Click(); }
                    catch (Exception e) { Core.Log.Error("Терминал: " + e.Message); }
                }
            }
            AnimateCursor();
            Refresh();
        }

        // Пока прицел на экране, действия игры на левой кнопке мыши (выстрел) выключены — клик по кнопке не стреляет.
        // Ушёл с экрана (или меню убрали) — включаем обратно только то, что выключали сами
        private readonly List<InputAction> _muted = new();
        private float _nextMuteScan;

        private void MuteFire(bool mute)
        {
            if (!mute)
            {
                foreach (var a in _muted)
                    try { if (a != null && !a.enabled) a.Enable(); } catch { }
                _muted.Clear();
                return;
            }
            foreach (var a in _muted)   // игра могла включить их обратно
                try { if (a != null && a.enabled) a.Disable(); } catch { }
            if (_muted.Count > 0 && Time.unscaledTime < _nextMuteScan) return;
            _nextMuteScan = Time.unscaledTime + 1f;
            var assets = new List<InputActionAsset>();
            try { if (InputSystem.actions != null) assets.Add(InputSystem.actions); } catch { }
            try
            {
                foreach (var asset in Resources.FindObjectsOfTypeAll<InputActionAsset>())
                    if (asset != null && !assets.Contains(asset)) assets.Add(asset);
            }
            catch { }
            foreach (var asset in assets)
            {
                var maps = asset.actionMaps;
                for (int m = 0; m < maps.Count; m++)
                {
                    var actions = maps[m].actions;
                    for (int i = 0; i < actions.Count; i++)
                    {
                        var a = actions[i];
                        if (a == null || !a.enabled || _muted.Contains(a)) continue;
                        bool onLeft = false;
                        var binds = a.bindings;
                        for (int k = 0; k < binds.Count && !onLeft; k++)
                            onLeft = (binds[k].effectivePath ?? "").Contains("leftButton");
                        if (!onLeft) continue;
                        a.Disable();
                        _muted.Add(a);
                    }
                }
            }
        }

        /// <summary>Меню убрали (вышли из хаба) — вернуть выстрел.</summary>
        internal void Release() => MuteFire(false);

        private bool Clicked()
        {
            if (Core.Instance != null && Core.Instance.MenuOpen) return false;   // открыто F2 — мышь у него
            var mouse = Mouse.current;
            return mouse != null && mouse.leftButton.wasPressedThisFrame;
        }

        // Луч из центра камеры (прицел) — в плоскость экрана
        private void UpdateHover()
        {
            _hover = null;
            Camera cam = Camera.main;
            bool on = false;
            Vector2 p = default;
            if (cam != null)
            {
                Transform t = cam.transform;
                Vector3 o = _ui.InverseTransformPoint(t.position);
                Vector3 d = _ui.InverseTransformPoint(t.position + t.forward) - o;
                if (d.z > 1e-4f && o.z < 0f)   // смотрим на лицевую сторону, стоя перед экраном
                {
                    Vector3 hit = o + d * (-o.z / d.z);
                    p = hit;
                    float dist = Vector3.Distance(t.position, _ui.TransformPoint(hit));
                    on = dist < Reach && Mathf.Abs(p.x) < W / 2 && Mathf.Abs(p.y) < H / 2;
                }
            }
            _onScreen = on;
            _point = p;
            if (on || _muted.Count > 0)
                try { MuteFire(on && !(Core.Instance != null && Core.Instance.MenuOpen)); }
                catch (Exception e) { Core.Log.Warning("Терминал: выстрел — " + e.Message); }
            _cursor.gameObject.SetActive(on);
            if (!on) return;
            _cursor.localPosition = Place(p, LCursor);
            foreach (var b in _buttons)
                if (b.Rect.Contains(p) && b.Enabled()) { _hover = b; break; }
        }

        // Над кнопкой стрелка золотая и чуть крупнее; клик — короткое «нажатие» и расходящийся круг
        private void AnimateCursor()
        {
            float dt = Time.unscaledDeltaTime;
            Mesh want = _hover != null ? _pointerHot : _pointer;
            if (_cursorMesh.sharedMesh != want) _cursorMesh.sharedMesh = want;

            _cursorScale = Mathf.MoveTowards(_cursorScale, _hover != null ? 1.2f : 1f, dt * 3f);
            _pressT = Mathf.Min(1f, _pressT + dt / PressTime);
            float press = 1f - 0.25f * Mathf.Sin(_pressT * Mathf.PI);
            _cursor.localScale = Vector3.one * (_cursorScale * press);

            bool rippling = _rippleT < 1f;
            if (_ripple.gameObject.activeSelf != rippling) _ripple.gameObject.SetActive(rippling);
            if (!rippling) return;
            _rippleT = Mathf.Min(1f, _rippleT + dt / RippleTime);
            float e = 1f - (1f - _rippleT) * (1f - _rippleT);   // быстро в начале, плавно в конце
            _ripple.localScale = Vector3.one * Mathf.Lerp(0.4f, 2.2f, e);
            var c = new Color(1f, 1f, 1f, 1f - _rippleT);
            if (_rippleMat.HasProperty("_Color")) _rippleMat.SetColor("_Color", c);
            if (_rippleMat.HasProperty("_BaseColor")) _rippleMat.SetColor("_BaseColor", c);
        }

        // Пиксельное кольцо радиусом r пикселей (для PixelMesh)
        private static string[] Ring(int r)
        {
            int n = r * 2 + 1;
            var rows = new string[n];
            for (int y = 0; y < n; y++)
            {
                var row = new char[n];
                for (int x = 0; x < n; x++)
                {
                    float d = Mathf.Sqrt((x - r) * (x - r) + (y - r) * (y - r));
                    row[x] = Mathf.Abs(d - (r - 0.5f)) < 0.75f ? 'B' : ' ';
                }
                rows[y] = new string(row);
            }
            return rows;
        }

        // Картинка из символов → меш из квадратиков с цветами вершин (UI/Default их рисует).
        // Начало координат — левый верхний пиксель (остриё стрелки) или центр (centered)
        private static Mesh PixelMesh(string[] rows, Color line, Color fill, bool centered = false)
        {
            var verts = new List<Vector3>();
            var colors = new List<Color>();
            var tris = new List<int>();
            int width = rows.Max(r => r.Length);
            float ox = centered ? -width * Px / 2 : 0f, oy = centered ? rows.Length * Px / 2 : 0f;
            for (int r = 0; r < rows.Length; r++)
                for (int c = 0; c < rows[r].Length; c++)
                {
                    char ch = rows[r][c];
                    if (ch != 'B' && ch != 'W') continue;
                    Color col = ch == 'B' ? line : fill;
                    float x = ox + c * Px, y = oy - r * Px;
                    int i = verts.Count;
                    // Обход по часовой, если смотреть с -Z (как у Quad)
                    verts.Add(new Vector3(x, y - Px, 0));
                    verts.Add(new Vector3(x, y, 0));
                    verts.Add(new Vector3(x + Px, y, 0));
                    verts.Add(new Vector3(x + Px, y - Px, 0));
                    for (int k = 0; k < 4; k++) colors.Add(col);
                    tris.AddRange(new[] { i, i + 1, i + 2, i, i + 2, i + 3 });
                }
            var m = new Mesh { name = "Skins Pixels" };
            m.vertices = verts.ToArray();
            m.colors = colors.ToArray();
            m.triangles = tris.ToArray();
            m.RecalculateNormals();
            m.RecalculateBounds();
            return m;
        }

        // Подписи, видимость и цвета плиток — каждый кадр: меняются от выбора, страниц и наведения
        private void Refresh()
        {
            List<string> skins = SkinNames();
            _skinPage = Mathf.Clamp(_skinPage, 0, Pages(skins.Count) - 1);
            for (int i = 0; i < PageSize; i++)
            {
                SetTile(_classTiles[i], Item(_classes, _classPage, i) is string c ? Core.Title(c) : null);
                SetTile(_skinTiles[i], Item(skins, _skinPage, i));
            }
            foreach (var b in _buttons)
            {
                if (!b.Back.gameObject.activeSelf) continue;
                bool enabled = b.Enabled(), hover = b == _hover;
                if (b.Arrow)
                {
                    b.Back.sharedMaterial = Mat(!enabled ? ColArrowOff : hover ? ColHeader : Color.white);
                    continue;
                }
                bool on = b.On != null && b.On();
                b.Back.sharedMaterial = Mat(on ? ColTileOn : hover ? ColTileHover : enabled ? ColTile : new Color(0.7f, 0.72f, 0.74f));
                if (b.Label != null) b.Label.color = on ? Color.white : enabled ? ColText : new Color(0.4f, 0.42f, 0.45f);
            }
        }

        private static void SetTile(Button b, string text)
        {
            bool show = text != null;
            if (b.Back.gameObject.activeSelf != show) b.Back.gameObject.SetActive(show);
            if (b.Label.gameObject.activeSelf != show) b.Label.gameObject.SetActive(show);
            if (show && b.Label.text != text) b.Label.text = text;
        }

        // ---- объекты ----

        private static Rect Inset(Rect r, float dx, float dy) => new Rect(r.x + dx, r.y + dy, r.width - dx * 2, r.height - dy * 2);

        // Точка меню → локальная позиция: слой ближе к глазу — дальше по -Z (лицо меню смотрит в -Z)
        private static Vector3 Place(Vector2 p, int layer) => new Vector3(p.x, p.y, -0.0015f * layer);

        private MeshRenderer Box(string name, Rect r, Color c, int layer)
        {
            MeshRenderer mr = Part(name, Meshes.Quad(r.width, r.height, false), Mat(c), layer);
            mr.transform.localPosition = Place(r.center, layer);
            return mr;
        }

        private MeshRenderer Part(string name, Mesh mesh, Material mat, int layer)
        {
            var go = new GameObject(name);
            go.transform.SetParent(_ui, false);
            go.AddComponent<MeshFilter>().sharedMesh = mesh;
            var r = go.AddComponent<MeshRenderer>();
            r.sharedMaterial = mat;
            r.sortingOrder = layer;
            r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            r.receiveShadows = false;
            return r;
        }

        // Надпись в прямоугольнике: размер шрифта подбирает TextMeshPro — самый крупный, что влезает
        private TextMeshPro Text(string name, Rect r, string text, Color color, TextAlignmentOptions align)
        {
            var go = new GameObject(name);
            go.transform.SetParent(_ui, false);
            go.transform.localPosition = Place(r.center, LText);
            var t = go.AddComponent<TextMeshPro>();
            TMP_FontAsset font = SkinLibrary.ScreenFont();
            if (font != null) t.font = font;
            t.rectTransform.sizeDelta = r.size;
            t.textWrappingMode = TextWrappingModes.NoWrap;
            t.overflowMode = TextOverflowModes.Overflow;
            t.enableAutoSizing = true;
            t.fontSizeMin = 0.05f;
            t.fontSizeMax = 20f;
            t.alignment = align;
            t.color = color;
            t.text = text;
            t.sortingOrder = LText;
            return t;
        }

        private Material Mat(Color c)
        {
            if (_mats.TryGetValue(c, out var m) && Game.IsAlive(m)) return m;
            m = Hub.Unlit(c);
            _mats[c] = m;
            return m;
        }

        private static string _version;
        private static string Version() =>
            _version ??= typeof(Core).Assembly.GetCustomAttribute<MelonInfoAttribute>()?.Version ?? "?";
    }
}
