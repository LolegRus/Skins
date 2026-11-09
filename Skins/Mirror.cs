using UnityEngine;
using Object = UnityEngine.Object;

namespace Skins
{
    /// <summary>
    /// Настоящее зеркало: вторая камера стоит в точке, симметричной камере игрока относительно стекла,
    /// смотрит в комнату через прямоугольник стекла (внеосевая проекция — края кадра ровно по краям стекла)
    /// и рисует в текстуру стекла. Ближняя плоскость — само стекло: стена за ним в кадр не попадает.
    /// Без отражающей матрицы — значит, не нужен разворот обхода треугольников (в URP его не включить);
    /// зеркальность даёт сама картинка: стекло видно с другой стороны, чем снимает камера, — у квада стекла
    /// перевёрнутая по горизонтали развёртка (Meshes.Quad, flipU).
    /// Рисуется, только когда игрок рядом и стекло на экране.
    /// </summary>
    internal sealed class Mirror
    {
        private const float MaxDistance = 10f;
        private const int Width = 1024;

        private readonly float _w, _h;
        private readonly RenderTexture _texture;
        private readonly Camera _camera;
        private int _loggedMask = int.MinValue;

        internal Transform Glass;     // квад стекла: центр, лицевая сторона — локальная -Z
        internal bool Active;

        internal Mirror(float w, float h)
        {
            _w = w;
            _h = h;
            _texture = new RenderTexture(Width, Mathf.RoundToInt(Width * h / w), 24) { name = "Skins Mirror" };
            _texture.hideFlags = HideFlags.HideAndDontSave;

            var go = new GameObject("Skins Mirror Camera");
            Object.DontDestroyOnLoad(go);
            _camera = go.AddComponent<Camera>();
            _camera.targetTexture = _texture;
            _camera.enabled = false;
        }

        internal Material Material(Material unlit)
        {
            if (unlit.HasProperty("_BaseMap")) unlit.SetTexture("_BaseMap", _texture);
            if (unlit.HasProperty("_MainTex")) unlit.SetTexture("_MainTex", _texture);
            return unlit;
        }

        internal void Render()
        {
            Camera eye = Camera.main;
            bool show = Active && eye != null && Game.IsAlive(Glass) && Glass.gameObject.activeInHierarchy;
            Vector3 c = show ? Glass.position : default;
            Vector3 n = show ? -Glass.forward : default;   // в комнату
            Vector3 e = show ? eye.transform.position : default;
            float side = show ? Vector3.Dot(e - c, n) : 0f;
            show = show && side > 0.05f && (e - c).sqrMagnitude < MaxDistance * MaxDistance && Visible(eye, c);
            if (!show) { _camera.enabled = false; return; }

            // Камера за стеклом, смотрит в комнату; вверх — как у стекла
            Vector3 pos = e - 2f * side * n;
            _camera.transform.SetPositionAndRotation(pos, Quaternion.LookRotation(n, Glass.up));

            Vector3 rel = c - pos;
            Vector3 right = _camera.transform.right, up = _camera.transform.up;
            float d = Vector3.Dot(rel, n);                 // = side: от камеры до стекла
            float cx = Vector3.Dot(rel, right), cy = Vector3.Dot(rel, up);
            float near = d + 0.002f, k = near / d, far = Mathf.Max(eye.farClipPlane, near + 1f);
            _camera.projectionMatrix = Frustum((cx - _w / 2) * k, (cx + _w / 2) * k, (cy - _h / 2) * k, (cy + _h / 2) * k, near, far);

            // Видим то же, что игрок, и в придачу себя: своё тело камера игрока может не рисовать (вид от первого лица)
            int mask = eye.cullingMask | BodyLayers();
            _camera.cullingMask = mask;
            _camera.clearFlags = eye.clearFlags;
            _camera.backgroundColor = eye.backgroundColor;
            _camera.depth = eye.depth - 1f;
            if (mask != _loggedMask)
            {
                _loggedMask = mask;
                Core.Log.Msg($"Зеркало: слои камеры игрока {eye.cullingMask:X8}, зеркала {mask:X8}.");
            }

            // Своё тело игра может прятать от своей камеры не только слоем, но и «тенью без меша»:
            // рисуем зеркало вручную и на время его кадра тело показываем. Не вышло — камера рисует сама, без тела
            if (_manual)
            {
                _camera.enabled = false;
                var shown = ShowBody();
                try { _camera.Render(); }
                catch (Exception ex)
                {
                    _manual = false;
                    Core.Log.Warning("Зеркало: Camera.Render не работает (" + ex.Message + ") — без своего тела.");
                }
                finally { HideBody(shown); }
                if (_manual && !_loggedManual) { _loggedManual = true; Core.Log.Msg("Зеркало: рисуется вручную, своё тело видно."); }
            }
            if (!_manual) _camera.enabled = true;
        }

        private bool _manual = true, _loggedManual;

        private readonly struct Hidden
        {
            internal readonly Renderer R;
            internal readonly UnityEngine.Rendering.ShadowCastingMode Shadows;
            internal Hidden(Renderer r) { R = r; Shadows = r.shadowCastingMode; }
        }

        private readonly List<Renderer> _body = new();
        private IntPtr _bodyFor;

        private List<Hidden> ShowBody()
        {
            var shown = new List<Hidden>();
            ScanBody();
            // Только «тень без меша» — так тело прячут от своей камеры. forceRenderingOff не трогаем: им игра и моды
            // прячут части нарочно (Ballista — корпус Scarab), в зеркале они бы появились
            foreach (Renderer r in _body)
            {
                if (!Game.IsAlive(r) || r.shadowCastingMode != UnityEngine.Rendering.ShadowCastingMode.ShadowsOnly) continue;
                shown.Add(new Hidden(r));
                r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.On;
            }
            return shown;
        }

        private static void HideBody(List<Hidden> shown)
        {
            foreach (var h in shown)
            {
                if (Game.IsAlive(h.R)) h.R.shadowCastingMode = h.Shadows;
            }
        }

        // Рендереры игрока и его класса; список — заново при смене класса (и раз в секунду, вместе со слоями).
        // Для нового класса — один раз в лог: как игра прячет тело от своей камеры
        private void ScanBody()
        {
            var player = Game.FindLocalPlayer();
            var cc = Game.CurrentClass(player);
            IntPtr key = cc != null ? cc.Pointer : player != null ? player.Pointer : IntPtr.Zero;
            if (key == _bodyFor && _body.Count > 0 && Time.unscaledTime < _nextBodyList) return;
            _nextBodyList = Time.unscaledTime + 1f;
            bool fresh = key != _bodyFor;
            _bodyFor = key;
            _body.Clear();
            if (player != null) Add(player.transform);
            if (cc != null && !cc.transform.IsChildOf(player.transform)) Add(cc.transform);
            if (!fresh || _body.Count == 0) return;

            var lines = _body.Take(60).Select(r =>
                $"  {r.name}: {(r.enabled ? "on" : "off")}{(r.gameObject.activeInHierarchy ? "" : " (inactive)")}, " +
                $"слой {r.gameObject.layer}, тень {r.shadowCastingMode}{(r.forceRenderingOff ? ", forceRenderingOff" : "")}");
            Core.Log.Msg($"Зеркало: тело {(cc != null ? Core.KeyOf(cc) : "игрока")} — рендереров {_body.Count}:\n" + string.Join("\n", lines));
        }

        private float _nextBodyList;

        private void Add(Transform root)
        {
            foreach (Renderer r in root.GetComponentsInChildren<Renderer>(true))
                if (r != null && !_body.Contains(r)) _body.Add(r);
        }

        private static bool Visible(Camera eye, Vector3 point)
        {
            Vector3 v = eye.WorldToViewportPoint(point);
            return v.z > 0f && v.x > -1f && v.x < 2f && v.y > -1f && v.y < 2f;   // с запасом: стекло большое
        }

        private int _bodyLayers;
        private float _nextBodyScan;

        // Слои своего тела — раз в секунду (тело меняется при спавне). Класс (Drone, Kobold…) лежит не под игроком,
        // и его слой (10 — своя камера его не рисует) надо брать у рендереров класса — их собирает ScanBody
        private int BodyLayers()
        {
            if (Time.unscaledTime < _nextBodyScan) return _bodyLayers;
            _nextBodyScan = Time.unscaledTime + 1f;
            _bodyLayers = 0;
            ScanBody();
            foreach (Renderer r in _body)
                if (Game.IsAlive(r) && r.gameObject.layer != 5) _bodyLayers |= 1 << r.gameObject.layer;   // 5 — UI (трещины экрана)
            return _bodyLayers;
        }

        // Перспективная проекция по краям кадра на ближней плоскости (как glFrustum)
        private static Matrix4x4 Frustum(float l, float r, float b, float t, float n, float f)
        {
            var m = new Matrix4x4();
            m.m00 = 2f * n / (r - l);
            m.m02 = (r + l) / (r - l);
            m.m11 = 2f * n / (t - b);
            m.m12 = (t + b) / (t - b);
            m.m22 = -(f + n) / (f - n);
            m.m23 = -2f * f * n / (f - n);
            m.m32 = -1f;
            return m;
        }

        internal void Dispose()
        {
            if (Game.IsAlive(_camera)) Object.Destroy(_camera.gameObject);
            if (Game.IsAlive(_texture)) { _texture.Release(); Object.Destroy(_texture); }
        }
    }
}
