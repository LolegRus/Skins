using UnityEngine;
using UnityEngine.InputSystem;

namespace Skins
{
    /// <summary>
    /// Меню скинов слева сверху (F2): ПК-классы игры (Game.IsPcClass), у каждого — выбранный скин.
    /// ↑/↓ — класс, ←/→ — скин (после последнего — родной вид). Скин текущего класса надевается сразу,
    /// для остальных выбор запоминается и надевается при появлении за этот класс.
    /// Вид — как у меню SandboxTools (те же цвета и скруглённая панель).
    /// </summary>
    internal sealed class SkinMenu
    {
        private static readonly Color ColPanel = new Color(0.059f, 0.086f, 0.149f, 0.95f);
        private static readonly Color ColSelected = new Color(0.13f, 0.36f, 0.42f, 1f);
        private static readonly Color ColAccent = new Color(0.24f, 0.90f, 0.85f, 1f);
        private static readonly Color ColText = new Color(0.95f, 0.97f, 1f, 1f);
        private static readonly Color ColDim = new Color(0.64f, 0.71f, 0.82f, 1f);
        private static readonly Color ColGold = new Color(1f, 0.80f, 0.35f, 1f);

        private const float X = 16f, Y = 16f, Width = 380f;
        private const float Pad = 14f, TitleH = 30f, RowH = 28f, FooterH = 26f;

        private readonly Core _core;
        private List<string> _classes = new();
        private int _row;

        private GUIStyle _round, _title, _row_, _rowOn, _value, _valueOn, _dim, _gold;

        internal bool Open { get; private set; }

        internal SkinMenu(Core core) => _core = core;

        internal void Toggle()
        {
            Open = !Open;
            if (!Open) return;

            _classes = Game.ClassPrefabs().Select(c => c.key.ToUpperInvariant()).Where(Game.IsPcClass).Distinct().ToList();
            string current = _core.CurrentKey;
            int i = current != null ? _classes.IndexOf(current) : -1;
            _row = i >= 0 ? i : Mathf.Clamp(_row, 0, Math.Max(0, _classes.Count - 1));
            if (_classes.Count == 0) Core.Log.Warning("Классы не найдены — меню пустое (зайди в хаб).");
        }

        internal void Update(Keyboard kb)
        {
            if (!Open || _classes.Count == 0) return;

            if (kb.upArrowKey.wasPressedThisFrame) _row = (_row - 1 + _classes.Count) % _classes.Count;
            if (kb.downArrowKey.wasPressedThisFrame) _row = (_row + 1) % _classes.Count;
            if (kb.leftArrowKey.wasPressedThisFrame) Step(-1);
            if (kb.rightArrowKey.wasPressedThisFrame) Step(1);
        }

        /// <summary>Соседний скин класса в строке; 0 — родной вид, 1..N — скины.</summary>
        private void Step(int step)
        {
            string key = _classes[_row];
            List<Skin> skins = SkinLibrary.For(key);
            if (skins.Count == 0) return;

            int n = skins.Count + 1;
            int index = skins.FindIndex(s => s.Name == _core.Selected(key)) + 1;
            index = ((index + step) % n + n) % n;
            _core.Select(key, index == 0 ? null : skins[index - 1].Name);
        }

        internal void Draw()
        {
            if (!Open) return;
            EnsureStyles();

            int rows = Math.Max(1, _classes.Count);
            float h = Pad + TitleH + rows * RowH + FooterH + Pad;
            Box(new Rect(X, Y, Width, h), ColPanel);

            float x = X + Pad, w = Width - Pad * 2, y = Y + Pad;
            GUI.Label(new Rect(x, y, w, TitleH), "Скины", _title);
            GUI.Label(new Rect(x, y, w, TitleH), "F2 — закрыть", Right(_dim));
            y += TitleH;

            if (_classes.Count == 0)
            {
                GUI.Label(new Rect(x, y, w, RowH), "Классы не найдены — зайди в хаб", _dim);
                y += RowH;
            }

            string current = _core.CurrentKey;
            for (int i = 0; i < _classes.Count; i++, y += RowH)
            {
                string key = _classes[i];
                bool on = i == _row;
                var rect = new Rect(x - 6, y + 2, w + 12, RowH - 4);
                if (on) Box(rect, ColSelected);

                GUI.Label(new Rect(x, y, 150, RowH), Core.Title(key), on ? _rowOn : _row_);
                if (key == current) GUI.Label(new Rect(x + 96, y, 60, RowH), "● сейчас", _gold);

                List<Skin> skins = SkinLibrary.For(key);
                string selected = _core.Selected(key);
                string value = skins.Count == 0 ? "нет скинов"
                    : string.IsNullOrEmpty(selected) ? "родной вид" : selected;
                if (on && skins.Count > 0) value = "‹  " + value + "  ›";
                GUI.Label(new Rect(x + 160, y, w - 160, RowH), value, skins.Count == 0 ? Right(_dim) : on ? _valueOn : _value);
            }

            GUI.Label(new Rect(x, y + 4, w, FooterH), "↑↓ класс    ←→ скин", _dim);
        }

        private void Box(Rect r, Color c)
        {
            Color was = GUI.color;
            GUI.color = c;
            GUI.Box(r, GUIContent.none, _round);
            GUI.color = was;
        }

        private readonly Dictionary<GUIStyle, GUIStyle> _right = new();
        private GUIStyle Right(GUIStyle st)
        {
            if (_right.TryGetValue(st, out var r)) return r;
            r = Make(st.fontSize, TextAnchor.MiddleRight, st.normal.textColor, st.fontStyle);
            _right[st] = r;
            return r;
        }

        private void EnsureStyles()
        {
            if (_title != null) return;
            _title = Make(16, TextAnchor.MiddleLeft, ColAccent, FontStyle.Bold);
            _row_ = Make(13, TextAnchor.MiddleLeft, ColText);
            _rowOn = Make(13, TextAnchor.MiddleLeft, ColAccent, FontStyle.Bold);
            _value = Make(13, TextAnchor.MiddleRight, ColDim);
            _valueOn = Make(13, TextAnchor.MiddleRight, ColText, FontStyle.Bold);
            _dim = Make(11, TextAnchor.MiddleLeft, ColDim);
            _gold = Make(11, TextAnchor.MiddleLeft, ColGold, FontStyle.Bold);

            _round = new GUIStyle();
            _round.normal.background = RoundTexture(16, 6f);
            _round.border = new RectOffset(8, 8, 8, 8);
        }

        private static GUIStyle Make(int size, TextAnchor align, Color color, FontStyle style = FontStyle.Normal)
        {
            var st = new GUIStyle();
            st.fontSize = size;
            st.alignment = align;
            st.wordWrap = false;
            st.clipping = TextClipping.Clip;
            st.fontStyle = style;
            st.normal.textColor = color;
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
