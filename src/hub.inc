// ---------- уголок скинов в хабе ----------
// Подключается в конец main.cpp (внутри его безымянного пространства имён): build.ps1 компилирует только *.cpp.
// То же, что в ПК-моде (Skins\Skins\Hub.cs, TerminalMenu.cs, Hologram.cs, Mirror.cs), на том же месте хаба:
//  • терминал Cinder из Hub.bundle (собирает Skins\Unity: Tools → Skins → Build Hub Terminal);
//  • меню на экране: слева классы со скинами, справа скины класса, «Review Skin» — голограмма на пьедестале.
//    Шейдеров, кроме TextMeshPro, в сборке для Quest нет, поэтому всё — надписи TMP: прямоугольники —
//    подложки <mark> (как у меню на руке), пиксельные стрелки и курсор — из таких же прямоугольников.
//    Наведи правый контроллер на экран (луч, как у меню) и жми курок. Меню на руке открыто — экран не слушает;
//  • голограмма — копия префаба класса (узлы, меши, материал тела), на неё надевается скин (Wear);
//    эффекты проекции — кольца-буквы «O», полосы и искры той же TMP;
//  • зеркало — камера за стеклом рисует в текстуру одной картинкой на оба глаза (в шлеме может укачивать;
//    пробовали экран-камеру — не понравилась). Материал стекла — из найденных в игре материалов с текстурой.

constexpr char kHubScene[] = "The Hub";
constexpr char kHubBundle[] = "Hub.bundle";
constexpr Vec3 kWall{-1.f, 0.f, 0.f};                   // нормаль стены (в комнату)
constexpr Vec3 kMonitorAt{-7.961f, -6.212f, 29.305f};   // точка на стене — центр монитора по ширине
constexpr Vec3 kMirrorAt{-7.961f, -6.624f, 32.075f};    // точка на стене — центр зеркала по ширине
constexpr float kHubFloorY = -8.213f;                   // пол у стены (на ПК — лучом вниз)
constexpr float kTerminalBack = 0.986f, kTerminalScale = 1.3f;
constexpr float kMirrorW = 2.4f, kMirrorH = 2.6f, kMirrorFromFloor = 0.15f, kFromWall = 0.03f;

Il2CppObject* (*il2cpp_object_get_class_fn)(Il2CppObject*);

// Unity API хаба (необязательное: чего нет — та часть просто не работает, причина в логе)
MethodInfo const* gActiveScene;     // SceneManager.GetActiveScene
MethodInfo const* gSceneName;       // Scene.get_name (у значения — this по указателю)
MethodInfo const* gGetActiveSelf;
MethodInfo const* gRendererEnabled;
MethodInfo const* gSetReceiveShadows;
// Свет мебели — свой световой зонд (как на ПК: тёплый полумрак + свет от лавы): векторы unity_SH* в MaterialPropertyBlock
Il2CppClass* gPropertyBlockClass;
MethodInfo const* gPropertyBlockCtor;
MethodInfo const* gPropertyBlockSetVector;   // SetVector(string, Vector4)
MethodInfo const* gSetPropertyBlock;         // Renderer.SetPropertyBlock(MaterialPropertyBlock)
MethodInfo const* gSetLightProbeUsage;       // Renderer.set_lightProbeUsage  // мебель в пещере — без теней (иначе её затеняет потолок)
MethodInfo const* gMeshBoundsHub;   // Mesh.get_bounds
void (*gMeshBoundsIcall)(void*, void*);
MethodInfo const* gInstantiateOne;  // Object.Instantiate(Object) — копия материала
MethodInfo const* gSetMainTexture;
MethodInfo const* gGetCullingMask;
MethodInfo const* gSetCullingMask;
MethodInfo const* gSetTargetTexture;
MethodInfo const* gSetProjection;
MethodInfo const* gGetClearFlags;
MethodInfo const* gSetClearFlags;
MethodInfo const* gGetBackground;
MethodInfo const* gSetBackground;
MethodInfo const* gGetDepth;
MethodInfo const* gSetDepth;
MethodInfo const* gGetFarClip;
MethodInfo const* gSetStereoEye;
MethodInfo const* gSetBehaviourEnabled;
MethodInfo const* gRenderTextureCtor;
MethodInfo const* gSetAntiAliasing;
MethodInfo const* gBoxSetSize;
MethodInfo const* gBoxSetCenter;
Il2CppClass* gRenderTextureClass;
Il2CppObject* gMeshRendererType;
Il2CppObject* gMeshColliderType;
Il2CppObject* gBoxColliderType;
Il2CppObject* gCameraType;
Il2CppObject* gRectTransformType;
Il2CppObject* gMaterialType;
size_t gAnimatedTransformsOffset;   // ScalarProceduralAnimation.animatedTransforms
bool gHubFailed;

// Детали терминала (меню, голограмма, эффекты) живут, пока стоит терминал: их GC handle — в одном списке,
// освобождаются вместе (FreeTerminalParts), иначе каждый заход в хаб оставлял бы сотню вечных handle
std::vector<Held> gTerminalHeld;
Il2CppObject* HoldPart(Il2CppObject* obj) {
  if (obj) gTerminalHeld.push_back(Hold(obj));
  return obj;
}
Held PartHeld(Il2CppObject* obj) { return {HoldPart(obj), nullptr}; }  // handle — в gTerminalHeld, не в самом Held

struct Mat4 {
  float m[16];  // по столбцам, как Matrix4x4 у Unity: m[столбец * 4 + строка]
};
void (*gSetProjectionIcall)(void*, Mat4 const*);  // Camera.set_projectionMatrix в сборке для Quest вырезан

struct Color4 {
  float r, g, b, a;
};

Quat QuatAxis(Vec3 axis, float degrees) {
  float h = degrees * 3.14159265f / 360.f, s = std::sin(h);
  return {axis.x * s, axis.y * s, axis.z * s, std::cos(h)};
}
Vec3 Mul(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
Vec3 Div(Vec3 a, Vec3 b) { return {a.x / b.x, a.y / b.y, a.z / b.z}; }
float Length3(Vec3 v) { return std::sqrt(Dot(v, v)); }

// Поза узла в мире: позиция, поворот, масштаб (lossyScale, без перекоса)
struct Pose3 {
  Vec3 p;
  Quat r;
  Vec3 s;
};
Pose3 WorldPose(Il2CppObject* t) {
  Vec3 s = gGetLossyScale ? Call<Vec3>(gGetLossyScale, t) : Vec3{1, 1, 1};
  return {Call<Vec3>(gGetPosition, t), Call<Quat>(gGetRotation, t), s};
}
Vec3 ToWorld(Pose3 const& f, Vec3 local) { return f.p + Rotate(f.r, Mul(f.s, local)); }
Vec3 ToLocal(Pose3 const& f, Vec3 world) { return Div(Rotate(Conj(f.r), world - f.p), f.s); }

Il2CppObject* TransformOfComponent(Il2CppObject* c) { return Call<Il2CppObject*>(gComponentTransform, c); }
bool ActiveSelf(Il2CppObject* go) { return gGetActiveSelf ? Call<bool>(gGetActiveSelf, go) : true; }
void SetActive(Il2CppObject* go, bool on) { Invoke(gSetActive, go, &on); }
void SetLocal(Il2CppObject* t, Vec3 p, Quat r, Vec3 s) {
  Call<void>(gSetLocalPosition, t, p);
  Call<void>(gSetLocalRotation, t, r);
  Invoke(gSetLocalScale, t, &s);
}

bool InHubScene() {
  static int checked = -1000;
  static bool inHub;
  int frame = gLastFrame;
  if (frame - checked < 30) return inHub;
  checked = frame;
  if (!gActiveScene || !gSceneName) return inHub = false;
  int32_t scene = Call<int32_t>(gActiveScene);
  auto name = Call<Il2CppString*>(gSceneName, &scene);
  bool now = ToUtf8(name) == kHubScene;
  if (now != inHub) LOG(ANDROID_LOG_INFO, "хаб: сцена '%s' — %s", ToUtf8(name).c_str(), now ? "ставлю уголок скинов" : "вне хаба");
  return inHub = now;
}

// Границы меша (локальные). Нули — нет API
Bounds MeshBounds(Il2CppObject* mesh) {
  Bounds b{};
  if (!mesh) return b;
  if (gMeshBoundsHub) return Call<Bounds>(gMeshBoundsHub, mesh);
  if (gMeshBoundsIcall) gMeshBoundsIcall(NativePtr(mesh), &b);
  return b;
}

// ---- бандл хаба ----
Held gHubBundleH, gTerminalPrefab, gMirrorPrefab, gFurniturePrefab;
bool gHubBundleFailed;

Il2CppObject* HubAsset(Held& slot, char const* name) {
  if (Alive(slot.obj)) return slot.obj;
  Free(slot);
  if (gHubBundleFailed) return nullptr;
  if (!Alive(gHubBundleH.obj)) {
    Free(gHubBundleH);
    gHubBundleH = Hold(LoadBundle(std::string(kSkinDir) + kHubBundle));
    if (!gHubBundleH.obj) {
      gHubBundleFailed = true;
      LOG(ANDROID_LOG_ERROR, "хаб: %s не загрузился (нет файла или собран не под Android)", kHubBundle);
      return nullptr;
    }
  }
  slot = Hold(LoadAsset(gHubBundleH.obj, name));
  if (!slot.obj) LOG(ANDROID_LOG_ERROR, "хаб: в %s нет '%s'", kHubBundle, name);
  return slot.obj;
}

// Материал тела классов (шейдер Dust/Grain): вершинные цвета, альфа 0 — цвет команды
Held gBodyMaterial;
std::map<std::string, Held> gClassPrefabs;  // ключ → префаб класса (GameObject)
bool gPrefabsScanned;

std::string ShaderName(Il2CppObject* material) {
  auto shader = material && gGetShader ? Invoke(gGetShader, material) : nullptr;
  return shader ? NameOf(shader) : "";
}

bool IsBodyMaterial(Il2CppObject* m) { return m && ShaderName(m).rfind("Dust/Grain", 0) == 0; }

// Префабы классов — среди всех CombatClass в памяти те, что не «(Clone)» (не экземпляры в сцене)
void ScanClassPrefabs() {
  for (auto it = gClassPrefabs.begin(); it != gClassPrefabs.end();)
    if (!Alive(it->second.obj)) {
      Free(it->second);
      it = gClassPrefabs.erase(it);
    } else {
      ++it;
    }
  if (gPrefabsScanned && !gClassPrefabs.empty()) return;
  gPrefabsScanned = true;
  auto all = reinterpret_cast<Il2CppArray*>(Invoke(gFindAllOfType, nullptr, gCombatClassType));
  for (uint32_t i = 0; i < Length(all); i++) {
    auto cc = At(all, i);
    if (!Alive(cc)) continue;
    auto go = GameObjectOf(cc);
    std::string name = NameOf(go);
    if (name.find("(Clone)") != std::string::npos) continue;
    std::string key = ClassKeyOf(go);
    auto it = gClassPrefabs.find(key);
    bool local = name.find(kLocalMark) != std::string::npos;
    if (it != gClassPrefabs.end() && !(local && NameOf(it->second.obj).find(kLocalMark) == std::string::npos)) continue;
    if (it != gClassPrefabs.end()) Free(it->second);
    gClassPrefabs[key] = Hold(go);
  }
  std::string keys;
  for (auto const& [k, v] : gClassPrefabs) keys += (keys.empty() ? "" : ", ") + k + " (" + NameOf(v.obj) + ")";
  LOG(ANDROID_LOG_INFO, "хаб: префабы классов — %s", keys.empty() ? "не найдены" : keys.c_str());
}

void FindBodyMaterialIn(Il2CppObject* t) {
  if (Alive(gBodyMaterial.obj)) return;
  if (auto r = Component(GameObjectOf(t), gRendererType)) {
    auto m = Call<Il2CppObject*>(gGetSharedMaterial, r);
    if (IsBodyMaterial(m)) {
      gBodyMaterial = Hold(m);
      LOG(ANDROID_LOG_INFO, "хаб: материал тела — '%s' (%s)", NameOf(m).c_str(), ShaderName(m).c_str());
      return;
    }
  }
  for (int i = 0, n = ChildCount(t); i < n && !Alive(gBodyMaterial.obj); i++) FindBodyMaterialIn(Child(t, i));
}

Il2CppObject* BodyMaterial() {
  if (Alive(gBodyMaterial.obj)) return gBodyMaterial.obj;
  Free(gBodyMaterial);
  // Из префаба класса, как на ПК: у живого класса материал без цвета команды (его ставит игра)
  ScanClassPrefabs();
  for (char const* key : {"KOBOLD", "ASSASSIN", "GRAPPLER"}) {
    auto it = gClassPrefabs.find(key);
    if (it != gClassPrefabs.end()) FindBodyMaterialIn(TransformOf(it->second.obj));
  }
  for (auto const& [k, v] : gClassPrefabs) FindBodyMaterialIn(TransformOf(v.obj));
  if (!Alive(gBodyMaterial.obj) && Alive(gLocal.cls.obj)) FindBodyMaterialIn(TransformOf(gLocal.cls.obj));
  return gBodyMaterial.obj;
}

// Все рендереры под t — материал тела (терминал и рамка зеркала)
void PaintBody(Il2CppObject* t, Il2CppObject* material, bool solid) {
  auto go = GameObjectOf(t);
  if (auto r = Component(go, gRendererType)) {
    if (material) Call<void>(gSetSharedMaterial, r, material);
    if (solid && gMeshColliderType && !Component(go, gMeshColliderType)) Invoke(gAddComponent, go, gMeshColliderType);
  }
  for (int i = 0, n = ChildCount(t); i < n; i++) PaintBody(Child(t, i), material, solid);
}

// ---- прямоугольники и надписи TMP ----
// Прямоугольник — подложка <mark> (её размер меряем один раз, gMarkSize/gMarkCenter — как у меню на руке)
struct Mark {
  Il2CppObject* text = nullptr;
  Il2CppObject* t = nullptr;
  std::string shown;
  Vec3 s{1, 1, 1}, p{0, 0, 0};
};

Il2CppObject* gHubFont;

bool NewMark(Mark& m, Il2CppObject* parent, int32_t order) {
  m.text = NewText(gHubFont, {0, 0, 0}, order, kAlignCenter, 0, parent);
  if (!m.text) return false;
  m.t = HoldPart(TransformOfComponent(m.text));
  m.shown.clear();
  return true;
}

void MarkColor(Mark& m, char const* color) {
  std::string text = color ? MarkText(color) : "";
  if (m.shown == text) return;
  m.shown = text;
  Invoke(gSetText, m.text, il2cpp_string_new(text.c_str()));
}

// Прямоугольник (x0, y0)–(x1, y1) в плоскости родителя на глубине z
void PlaceMark(Mark& m, char const* color, float x0, float y0, float x1, float y1, float z) {
  MarkColor(m, color);
  if (!color) return;
  Vec3 s{(x1 - x0) / gMarkSize.x, (y1 - y0) / gMarkSize.y, 1};
  Vec3 p{(x0 + x1) / 2 - gMarkCenter.x * s.x, (y0 + y1) / 2 - gMarkCenter.y * s.y, z};
  if (!Same(s, m.s)) {
    m.s = s;
    Invoke(gSetLocalScale, m.t, &s);
  }
  if (!Same(p, m.p)) {
    m.p = p;
    Invoke(gSetLocalPosition, m.t, &p);
  }
}

// Надпись, вписанная в прямоугольник: масштаб — по границам меша TMP (появляются после отрисовки)
struct Label {
  Il2CppObject* text = nullptr;
  Il2CppObject* t = nullptr;
  std::string plain, shown;  // текст без цвета и с цветом
  float cx = 0, cy = 0, maxW = 0, maxH = 0, z = 0;
  bool left = false;         // прижать к левому краю (cx — левый край)
  bool fitted = false;
  int fitAfter = 0;
  float fit = 0;             // масштаб, при котором надпись влезает (0 — ещё не измерена)
  Bounds b{};
  float applied = -1;          // кадр, после которого меш TMP уже с новым текстом
};

bool NewLabel(Label& l, Il2CppObject* parent, int32_t order) {
  l.text = NewText(gHubFont, {0, 0, 0}, order, kAlignCenter, 0, parent);
  if (!l.text) return false;
  l.t = HoldPart(TransformOfComponent(l.text));
  return true;
}

void SetLabel(Label& l, std::string const& plain, char const* color) {
  std::string shown = plain.empty() ? "" : std::string("<color=#") + color + ">" + plain + "</color>";
  if (plain != l.plain) {
    l.fitted = false;
    l.fitAfter = gLastFrame + 2;  // TMP перестроит меш при отрисовке — в этом кадре границы ещё старые
  }
  l.plain = plain;
  if (shown == l.shown) return;
  l.shown = shown;
  Invoke(gSetText, l.text, il2cpp_string_new(shown.c_str()));
}

void FitLabel(Label& l) {
  if (l.plain.empty()) return;
  if (!l.fitted && gLastFrame >= l.fitAfter) {
    Bounds b = TextBounds(l.text);
    if (!(b.extents.x > 1e-5f && b.extents.y > 1e-5f)) return;  // меша ещё нет — в следующем кадре
    l.b = b;
    l.fit = std::min(l.maxW / (2 * b.extents.x), l.maxH / (2 * b.extents.y));
    l.fitted = true;
    l.applied = -1;
  }
}

// Масштаб s (не больше того, при котором надпись влезает)
void ApplyLabel(Label& l, float s) {
  if (!l.fitted || l.plain.empty()) return;
  s = std::min(s, l.fit);
  if (std::fabs(s - l.applied) < 1e-6f) return;
  l.applied = s;
  float x = l.left ? l.cx + l.b.extents.x * s : l.cx;
  Vec3 p{x - l.b.center.x * s, l.cy - l.b.center.y * s, l.z};
  Vec3 sc{s, s, 1};
  Invoke(gSetLocalScale, l.t, &sc);
  Invoke(gSetLocalPosition, l.t, &p);
}

// Пиксельная картинка из прямоугольников: подряд идущие одинаковые пиксели строки — один прямоугольник.
// Начало — левый верхний пиксель (centered — середина). Цвета: B — обводка, W — заливка
struct Pixels {
  Held root;
  Il2CppObject* t = nullptr;
  std::vector<Mark> marks;
  std::vector<char> kinds;
};

bool BuildPixels(Pixels& px, char const* name, Il2CppObject* parent, std::vector<std::string> const& rows, float size,
                 int32_t order, bool centered, char const* line, char const* fill) {
  auto go = NewGameObject(name, parent);
  px.root = PartHeld(go);
  px.t = HoldPart(TransformOf(go));
  size_t width = 0;
  for (auto const& r : rows) width = std::max(width, r.size());
  float ox = centered ? -size * width / 2 : 0, oy = centered ? size * rows.size() / 2 : 0;
  for (size_t r = 0; r < rows.size(); r++)
    for (size_t c = 0; c < rows[r].size();) {
      char k = rows[r][c];
      size_t e = c;
      while (e < rows[r].size() && rows[r][e] == k) e++;
      if (k == 'B' || k == 'W') {
        Mark m;
        if (!NewMark(m, px.t, order)) return false;
        PlaceMark(m, k == 'B' ? line : fill, ox + c * size, oy - (r + 1) * size, ox + e * size, oy - r * size, 0);
        px.marks.push_back(m);
        px.kinds.push_back(k);
      }
      c = e;
    }
  return true;
}

void PixelsColor(Pixels& px, char kind, char const* color) {
  for (size_t i = 0; i < px.marks.size(); i++)
    if (px.kinds[i] == kind) MarkColor(px.marks[i], color);
}

std::vector<std::string> RingRows(int r) {
  std::vector<std::string> rows;
  for (int y = 0; y <= 2 * r; y++) {
    std::string row;
    for (int x = 0; x <= 2 * r; x++) {
      float d = std::sqrt(float((x - r) * (x - r) + (y - r) * (y - r)));
      row += std::fabs(d - (r - 0.5f)) < 0.75f ? 'B' : ' ';
    }
    rows.push_back(row);
  }
  return rows;
}

// ---- меню на экране терминала (как TerminalMenu.cs) ----
constexpr float kScrCenterY = 1.612f, kScrFrontZ = -0.803f, kScrW = 1.884f, kScrH = 1.102f;
constexpr float kScrPad = 0.035f, kScrPanelGap = 0.03f, kScrBarH = 0.11f, kScrBarGap = 0.025f, kScrTileGap = 0.022f;
constexpr int kScrCols = 2, kScrRows = 3, kScrPage = kScrCols * kScrRows;
constexpr float kScrReach = 7.f;
constexpr float kLayer = 0.0015f;  // сдвиг слоя к глазу (лицо меню — к -Z)
constexpr int kLBack = 0, kLPanel = 1, kLBar = 2, kLTile = 3, kLText = 4, kLRipple = 5, kLCursor = 6;
int32_t ScrOrder(int layer) { return 1 + layer; }  // ниже меню на руке (10…17)

constexpr char const* kCBack = "E6EDF7FF";
constexpr char const* kCPanel = "4591DEFF";
constexpr char const* kCBar = "2B73C9FF";
constexpr char const* kCHeader = "8CC2F2FF";
constexpr char const* kCHeaderText = "0D295CFF";
constexpr char const* kCTile = "E3E6E8FF";
constexpr char const* kCTileHover = "C7E0FAFF";
constexpr char const* kCTileOn = "12387AFF";
constexpr char const* kCTileOff = "B3B8BDFF";
constexpr char const* kCText = "141417FF";
constexpr char const* kCTextOff = "666B73FF";
constexpr char const* kCArrow = "FFFFFFFF";
constexpr char const* kCArrowOff = "FFFFFF4D";
constexpr char const* kCCursorLine = "0D1F47FF";
constexpr char const* kCCursorFill = "FFFFFFFF";
constexpr char const* kCCursorHot = "FFD140FF";

const std::vector<std::string> kPointerRows = {
    "B",        "BB",        "BWB",      "BWWB",     "BWWWB",    "BWWWWB",
    "BWWWWWB",  "BWWWWWWB",  "BWWWWWWWB", "BWWWWWWWWB", "BWWWWWBBBBB", "BWWBWWB",
    "BWB BWWB", "BB  BWWB",  "B    BWWB", "     BWWB", "      BB"};
const std::vector<std::string> kArrowRightRows = {"B", "BB", "BBB", "BBBB", "BBB", "BB", "B"};
const std::vector<std::string> kArrowLeftRows = {"   B", "  BB", " BBB", "BBBB", " BBB", "  BB", "   B"};
constexpr float kCursorPx = 0.0055f, kArrowPx = 0.0107f;

struct ScrButton {
  float x0, y0, x1, y1;
  int kind;    // 0 — класс, 1 — скин, 2 — стрелка классов, 3 — стрелка скинов, 4 — Review
  int index;   // плитка на странице / шаг стрелки
  Mark back;
  Label label;
  Pixels arrow;
};

ScrButton MakeButton(float x0, float y0, float x1, float y1, int kind, int index) {
  ScrButton b{};
  b.x0 = x0;
  b.y0 = y0;
  b.x1 = x1;
  b.y1 = y1;
  b.kind = kind;
  b.index = index;
  return b;
}

struct Screen {
  Held root;
  Il2CppObject* ui = nullptr;  // Transform меню (поверх экрана терминала)
  std::vector<Mark> fixed;     // фон, панели, полосы
  Label titles[2];
  std::vector<ScrButton> buttons;
  Pixels cursor, ripple;
  bool built = false;
  bool failed = false;  // не собралось — недостроенное удалено, до нового терминала не пробуем
  int hover = -1;
  bool onScreen = false;
  Vec3 point{};
  float cursorScale = 1, pressT = 1, rippleT = 1;
} gScr;

std::vector<std::string> gScrClasses;  // ключи классов со скинами
bool gScrClassesScanned;
std::string gScrClass, gScrSkin;
int gScrClassPage, gScrSkinPage;
bool gHubTriggerWas;  // gHubRay — в main.cpp (его смотрит меню на руке)

// Имена скинов выбранного класса — список собирается раз за кадр (или при смене класса), не на каждую кнопку
std::vector<std::string> const& ScrSkinNames() {
  static std::vector<std::string> names;
  static std::string forClass;
  static int frame = -1;
  if (frame != gLastFrame || forClass != gScrClass) {
    frame = gLastFrame;
    forClass = gScrClass;
    names.clear();
    if (!gScrClass.empty())
      for (auto s : SkinsFor(gScrClass)) names.push_back(s->name);
  }
  return names;
}
int ScrPages(size_t n) { return std::max(1, int((n + kScrPage - 1) / kScrPage)); }
std::string ScrItem(std::vector<std::string> const& list, int page, int i) {
  int k = page * kScrPage + i;
  return k >= 0 && k < int(list.size()) ? list[k] : "";
}

void ScrPickClass(int i) {
  if (i < 0 || i >= int(gScrClasses.size()) || gScrClasses[i] == gScrClass) return;
  gScrClass = gScrClasses[i];
  gScrSkin.clear();
  gScrSkinPage = 0;
}

void ScanScrClasses() {
  if (gScrClassesScanned) return;  // скины уже загружены (gSkinsLoaded) — список не меняется
  gScrClassesScanned = true;
  for (auto key : kClassKeys)
    if (std::string(key) != "STYX" && !SkinsFor(key).empty()) gScrClasses.push_back(key);
  std::sort(gScrClasses.begin(), gScrClasses.end(), [](auto& a, auto& b) { return Title(a) < Title(b); });
  if (gScrClasses.empty()) return;
  std::string list;
  for (auto& k : gScrClasses) list += (list.empty() ? "" : ", ") + Title(k);
  LOG(ANDROID_LOG_INFO, "терминал: классы со скинами — %s", list.c_str());
  auto own = std::find(gScrClasses.begin(), gScrClasses.end(), gLocal.key);
  ScrPickClass(own != gScrClasses.end() ? int(own - gScrClasses.begin()) : 0);
  gScrClassPage = int(std::find(gScrClasses.begin(), gScrClasses.end(), gScrClass) - gScrClasses.begin()) / kScrPage;
}

Vec3 ScrPlace(float x, float y, int layer) { return {x, y, -kLayer * layer}; }

bool BuildScreen(Il2CppObject* terminal) {
  auto go = NewGameObject("Skins Terminal Menu", terminal);
  gScr.root = PartHeld(go);
  gScr.ui = HoldPart(TransformOf(go));
  SetLocal(gScr.ui, {0, kScrCenterY, kScrFrontZ}, QuatAxis({0, 1, 0}, 180), {1, 1, 1});
  gScr.fixed.clear();
  gScr.buttons.clear();

  auto fixed = [&](char const* color, float x0, float y0, float x1, float y1, int layer) {
    Mark m;
    if (!NewMark(m, gScr.ui, ScrOrder(layer))) return false;
    PlaceMark(m, color, x0, y0, x1, y1, -kLayer * layer);
    gScr.fixed.push_back(m);
    return true;
  };
  if (!fixed(kCBack, -kScrW / 2, -kScrH / 2, kScrW / 2, kScrH / 2, kLBack)) return false;

  float pw = (kScrW - kScrPad * 2 - kScrPanelGap) / 2;
  for (int side = 0; side < 2; side++) {
    float x = -kScrW / 2 + kScrPad + side * (pw + kScrPanelGap);
    float top = kScrH / 2 - kScrPad, bottom = -kScrH / 2 + kScrPad;
    fixed(kCPanel, x, bottom, x + pw, top, kLPanel);
    fixed(kCHeader, x, top - kScrBarH, x + pw, top, kLBar);
    fixed(kCBar, x, bottom, x + pw, bottom + kScrBarH, kLBar);
    Label& title = gScr.titles[side];
    if (!NewLabel(title, gScr.ui, ScrOrder(kLText))) return false;
    title.left = true;
    title.cx = x + 0.03f;
    title.cy = top - kScrBarH / 2;
    title.maxW = pw * 0.6f;
    title.maxH = kScrBarH - 0.05f;
    title.z = -kLayer * kLText;
    SetLabel(title, side == 0 ? "DustOS" : std::string("Skins v") + kModVersion, kCHeaderText);

    float tilesTop = top - kScrBarH - kScrBarGap, tilesBottom = bottom + kScrBarH + kScrBarGap;
    float tw = (pw - kScrTileGap * (kScrCols + 1)) / kScrCols;
    float th = (tilesTop - tilesBottom - kScrTileGap * (kScrRows - 1)) / kScrRows;
    for (int i = 0; i < kScrPage; i++) {
      int c = i % kScrCols, r = i / kScrCols;
      float x0 = x + kScrTileGap + c * (tw + kScrTileGap), y1 = tilesTop - r * (th + kScrTileGap);
      ScrButton b = MakeButton(x0, y1 - th, x0 + tw, y1, side, i);
      if (!NewMark(b.back, gScr.ui, ScrOrder(kLTile)) || !NewLabel(b.label, gScr.ui, ScrOrder(kLText))) return false;
      b.label.cx = (b.x0 + b.x1) / 2;
      b.label.cy = (b.y0 + b.y1) / 2;
      b.label.maxW = tw - 0.06f;
      b.label.maxH = th * 0.4f;
      b.label.z = -kLayer * kLText;
      gScr.buttons.push_back(std::move(b));
    }
    // Стрелки: у классов — слева в подвале, у скинов — справа (слева — «Review Skin»)
    float ay = bottom + kScrBarH / 2, aw = 0.075f;
    float ax = side == 0 ? x + 0.05f : x + pw - 0.05f - aw * 2 - 0.04f;
    for (int step : {-1, 1}) {
      float bx = ax + (step > 0 ? aw + 0.04f : 0.f);
      ScrButton b = MakeButton(bx - 0.015f, ay - aw / 2 - 0.015f, bx + aw + 0.015f, ay + aw / 2 + 0.015f, 2 + side, step);
      if (!BuildPixels(b.arrow, "Arrow", gScr.ui, step > 0 ? kArrowRightRows : kArrowLeftRows, kArrowPx,
                       ScrOrder(kLTile), true, kCArrow, kCArrow))
        return false;
      Call<void>(gSetLocalPosition, b.arrow.t, ScrPlace(bx + aw / 2, ay, kLTile));
      gScr.buttons.push_back(std::move(b));
    }
    if (side == 1) {
      ScrButton b = MakeButton(x + 0.03f, ay - 0.035f, x + 0.33f, ay + 0.035f, 4, 0);
      if (!NewMark(b.back, gScr.ui, ScrOrder(kLTile)) || !NewLabel(b.label, gScr.ui, ScrOrder(kLText))) return false;
      b.label.cx = (b.x0 + b.x1) / 2;
      b.label.cy = ay;
      b.label.maxW = 0.26f;
      b.label.maxH = 0.04f;
      b.label.z = -kLayer * kLText;
      gScr.buttons.push_back(std::move(b));
    }
  }

  if (!BuildPixels(gScr.cursor, "Cursor", gScr.ui, kPointerRows, kCursorPx, ScrOrder(kLCursor), false, kCCursorLine,
                   kCCursorFill) ||
      !BuildPixels(gScr.ripple, "Ripple", gScr.ui, RingRows(5), kCursorPx, ScrOrder(kLRipple), true, kCCursorHot,
                   kCCursorHot))
    return false;
  SetActive(gScr.cursor.root.obj, false);
  SetActive(gScr.ripple.root.obj, false);
  gScr.built = true;
  LOG(ANDROID_LOG_INFO, "терминал: меню на экране — %zu кнопок", gScr.buttons.size());
  return true;
}

bool ScrEnabled(ScrButton const& b) {
  switch (b.kind) {
    case 0: return !ScrItem(gScrClasses, gScrClassPage, b.index).empty();
    case 1: return !ScrItem(ScrSkinNames(), gScrSkinPage, b.index).empty();
    case 2: return b.index < 0 ? gScrClassPage > 0 : gScrClassPage < ScrPages(gScrClasses.size()) - 1;
    case 3: return b.index < 0 ? gScrSkinPage > 0 : gScrSkinPage < ScrPages(ScrSkinNames().size()) - 1;
    default: return !gScrSkin.empty();
  }
}

void ShowHologram(std::string const& key, std::string const& skin);

void ScrClick(ScrButton const& b) {
  switch (b.kind) {
    case 0: ScrPickClass(gScrClassPage * kScrPage + b.index); break;
    case 1: gScrSkin = ScrItem(ScrSkinNames(), gScrSkinPage, b.index); break;
    case 2: gScrClassPage = std::clamp(gScrClassPage + b.index, 0, ScrPages(gScrClasses.size()) - 1); break;
    case 3: gScrSkinPage = std::clamp(gScrSkinPage + b.index, 0, ScrPages(ScrSkinNames().size()) - 1); break;
    default:
      LOG(ANDROID_LOG_INFO, "терминал: на пьедестал — %s (%s)", gScrSkin.c_str(), Title(gScrClass).c_str());
      ShowHologram(gScrClass, gScrSkin);
  }
}

void RefreshScreen() {
  auto const& skins = ScrSkinNames();
  gScrSkinPage = std::clamp(gScrSkinPage, 0, ScrPages(skins.size()) - 1);
  for (int i = 0; i < int(gScr.buttons.size()); i++) {
    auto& b = gScr.buttons[i];
    bool enabled = ScrEnabled(b), hover = i == gScr.hover;
    if (b.kind == 2 || b.kind == 3) {
      PixelsColor(b.arrow, 'B', !enabled ? kCArrowOff : hover ? kCHeader : kCArrow);
      continue;
    }
    std::string text;
    bool on = false;
    if (b.kind == 0) {
      std::string key = ScrItem(gScrClasses, gScrClassPage, b.index);
      text = key.empty() ? "" : Title(key);
      on = !key.empty() && key == gScrClass;
    } else if (b.kind == 1) {
      text = ScrItem(skins, gScrSkinPage, b.index);
      on = !text.empty() && text == gScrSkin;
    } else {
      text = "Review Skin";
    }
    if (text.empty()) {
      MarkColor(b.back, nullptr);
      SetLabel(b.label, "", kCText);
      continue;
    }
    char const* back = on ? kCTileOn : hover ? kCTileHover : enabled ? kCTile : kCTileOff;
    PlaceMark(b.back, back, b.x0, b.y0, b.x1, b.y1, -kLayer * kLTile);
    SetLabel(b.label, text, on ? "FFFFFFFF" : enabled ? kCText : kCTextOff);
    FitLabel(b.label);
  }
  // Плитки одной колонки — одним размером, по самой длинной надписи (как меню на руке)
  for (int kind : {0, 1}) {
    float s = 1e9f;
    bool ready = true;
    for (auto& b : gScr.buttons)
      if (b.kind == kind && !b.label.plain.empty()) {
        if (b.label.fitted) s = std::min(s, b.label.fit);
        else ready = false;
      }
    if (!ready) continue;  // дождаться замера всех — иначе размер прыгает
    for (auto& b : gScr.buttons)
      if (b.kind == kind) ApplyLabel(b.label, s);
  }
  for (auto& b : gScr.buttons)
    if (b.kind == 4) ApplyLabel(b.label, 1e9f);
  for (auto& t : gScr.titles) {
    FitLabel(t);
    ApplyLabel(t, 1e9f);
  }
}

// Луч правой руки — как у меню на руке: поза контроллера в мир через камеру, начало — у видимой руки класса
bool RightRay(Vec3& origin, Vec3& dir, Vec3& camPos, float& scale) {
  auto camera = Call<Il2CppObject*>(gGetMainCamera);
  if (!camera) return false;
  auto camT = TransformOfComponent(camera);
  camPos = Call<Vec3>(gGetPosition, camT);
  Quat camRot = Call<Quat>(gGetRotation, camT);
  scale = gGetLossyScale ? Call<Vec3>(gGetLossyScale, camT).x : 1.f;
  if (!(scale > 0.01f)) scale = 1.f;
  Vec3 headPos{}, rayPos{};
  Quat headRot{0, 0, 0, 1}, rayRot{0, 0, 0, 1};
  if (!Pose(kHead, gDevicePosition, gDeviceRotation, headPos, headRot)) return false;
  bool pose = gUsePointerPose ? Pose(kRightHand, gPointerPosition, gPointerRotation, rayPos, rayRot)
                              : Pose(kRightHand, gDevicePosition, gDeviceRotation, rayPos, rayRot);
  if (!pose) return false;
  Quat rigRot = camRot * Conj(headRot);
  Vec3 rigPos = camPos - Rotate(rigRot, headPos * scale);
  origin = rigPos + Rotate(rigRot, rayPos * scale);
  dir = Rotate(rigRot * rayRot, {0, 0, 1});
  if (auto hand = RightHand()) origin = Call<Vec3>(gGetPosition, hand);
  return true;
}

// Луч больше не у экрана: спрятать его (если меню на руке открыто — луч уже рисует оно, не трогаем).
// Без этого меню на руке, видя gHubRay, не прятало бы луч и после выхода из хаба
void ReleaseHubRay() {
  if (!gHubRay) return;
  gHubRay = false;
  if (!gMenuVisible) DrawRay({}, {0, 0, 1}, 0, {}, 1.f);
}

void TickScreen() {
  if (!gScr.built || !Alive(gScr.root.obj)) {
    ReleaseHubRay();
    return;
  }
  ScanScrClasses();

  // Луч в плоскость экрана (лицо меню — к -Z его осей)
  Vec3 origin{}, dir{0, 0, 1}, camPos{};
  float scale = 1;
  bool onScreen = false;
  Vec3 p{};
  float t = 0;
  int hover = -1;
  if (!gMenuVisible && RightRay(origin, dir, camPos, scale)) {
    Pose3 ui = WorldPose(gScr.ui);
    Vec3 o = ToLocal(ui, origin), d = Rotate(Conj(ui.r), dir);
    d = Div(d, ui.s);
    if (d.z > 1e-4f && o.z < 0.f) {
      float k = -o.z / d.z;
      p = o + d * k;
      Vec3 hit = ToWorld(ui, p);
      t = Length3(hit - origin);
      onScreen = t < kScrReach * scale && std::fabs(p.x) < kScrW / 2 && std::fabs(p.y) < kScrH / 2;
    }
  }
  if (onScreen)
    for (int i = 0; i < int(gScr.buttons.size()); i++) {
      auto& b = gScr.buttons[i];
      if (p.x > b.x0 && p.x < b.x1 && p.y > b.y0 && p.y < b.y1 && ScrEnabled(b)) hover = i;
    }

  // Луч рисует меню на руке (DrawRay); пока он у экрана, меню на руке его не прячет (gHubRay)
  if (onScreen) {
    float start = kRayStart * scale;
    DrawRay(origin + dir * start, dir, t - kRayGap * scale - start, camPos, scale);
    gHubRay = true;
  } else {
    ReleaseHubRay();
  }

  if (hover >= 0 && hover != gScr.hover) Buzz(kRightHand, 0.1f, 0.01f);
  gScr.hover = hover;
  if (onScreen != gScr.onScreen) SetActive(gScr.cursor.root.obj, onScreen);
  gScr.onScreen = onScreen;
  gScr.point = p;
  if (onScreen) Call<void>(gSetLocalPosition, gScr.cursor.t, ScrPlace(p.x, p.y, kLCursor));

  // Фронт — по самой кнопке: курок, зажатый, пока закрывали меню на руке, — не клик по экрану
  bool trigger = Button(kRightHand, gTriggerButton);
  bool down = trigger && !gHubTriggerWas && !gMenuVisible;
  gHubTriggerWas = trigger;
  if (onScreen && down) {
    gScr.pressT = 0;
    gScr.rippleT = 0;
    Call<void>(gSetLocalPosition, gScr.ripple.t, ScrPlace(p.x, p.y, kLRipple));
    if (hover >= 0) {
      Buzz(kRightHand, 0.4f, 0.05f);
      ScrClick(gScr.buttons[hover]);
    }
  }

  // Курсор: над кнопкой — золотой и крупнее; клик — «нажатие» и расходящееся кольцо
  static double last = Now();
  double now = Now();
  float dt = float(std::min(0.1, now - last));
  last = now;
  PixelsColor(gScr.cursor, 'W', hover >= 0 ? kCCursorHot : kCCursorFill);
  float want = hover >= 0 ? 1.2f : 1.f;
  gScr.cursorScale += std::clamp(want - gScr.cursorScale, -dt * 3, dt * 3);
  gScr.pressT = std::min(1.f, gScr.pressT + dt / 0.12f);
  float s = gScr.cursorScale * (1.f - 0.25f * std::sin(gScr.pressT * 3.14159265f));
  Vec3 cs{s, s, 1};
  Invoke(gSetLocalScale, gScr.cursor.t, &cs);
  bool rippling = gScr.rippleT < 1.f;
  static bool rippleShown;
  if (rippling != rippleShown) SetActive(gScr.ripple.root.obj, rippling);
  rippleShown = rippling;
  if (rippling) {
    gScr.rippleT = std::min(1.f, gScr.rippleT + dt / 0.35f);
    float e = 1.f - (1.f - gScr.rippleT) * (1.f - gScr.rippleT);
    float rs = 0.4f + 1.8f * e;
    Vec3 r3{rs, rs, 1};
    Invoke(gSetLocalScale, gScr.ripple.t, &r3);
    char color[16];
    snprintf(color, sizeof color, "FFD140%02X", int(255 * (1.f - gScr.rippleT)));
    PixelsColor(gScr.ripple, 'B', color);
  }
  RefreshScreen();
}

// ---- голограмма (как Hologram.cs) ----
constexpr Vec3 kHoloTop{-1.545f, 0.739f, 0.f};   // крышка пьедестала в осях терминала
constexpr Vec3 kHoloLens{-1.545f, 0.705f, 0.f};
constexpr float kGameScale = 0.65f, kShoulderAt = 0.62f, kHandGap = 0.1f, kMaxRestShift = 0.35f;
constexpr float kHoloMaxH = 1.0f, kHoloMaxW = 1.3f, kHoloMaxD = 0.9f, kHoloLift = 0.05f;
constexpr float kSpinSpeed = 40.f, kBobHeight = 0.015f, kBobSpeed = 1.6f;

float ClassSize(std::string const& key) {
  if (key == "SERAPH" || key == "KOBOLD" || key == "GOLEM" || key == "ASSASSIN") return 1.3f;
  if (key == "BOULDER" || key == "ROLLER") return 0.75f;
  return 1.f;
}

bool JunkNode(std::string const& n) {
  for (char const* j : {"Menu Icon Anchor", "Crit Display", "Weapon UI", "Health UI", "Slam Attack", "AoE Damage Visual Radius"})
    if (n == j) return true;
  return false;
}
bool JunkMesh(std::string const& n) {
  if (n == "Witch Hat" || n.find("Damage Mesh") != std::string::npos || n.find("Damage Flash") != std::string::npos)
    return true;
  // «DM» отдельным словом: «Head DM», «DM Body», «Tread FR DM»
  for (size_t i = n.find("DM"); i != std::string::npos; i = n.find("DM", i + 1)) {
    bool before = i == 0 || n[i - 1] == ' ', after = i + 2 == n.size() || n[i + 2] == ' ';
    if (before && after) return true;
  }
  return false;
}
bool ThrownNode(std::string const& n) {
  for (char const* j : {"(Projectile)", "Teleport Disk", "Mine Disk", "Teleport Trail"})
    if (n.find(j) != std::string::npos) return true;
  return false;
}
bool HandNodeName(std::string const& n) {
  for (char const* j : {"Left Hand", "Right Hand", "Left Arm", "Right Arm", "Left Hand Anchor", "Right Hand Anchor"})
    if (n == j) return true;
  return false;
}

struct HoloHand {
  Il2CppObject* t;
  bool left, rotate;
};

struct HoloCopy {
  std::unordered_map<Il2CppObject*, Il2CppObject*> map;  // Transform префаба → копии
  std::vector<std::pair<Il2CppObject*, Il2CppObject*>> anims;  // ScalarProceduralAnimation префаба, его Transform
  std::vector<HoloHand> hands;
  std::vector<Il2CppObject*> held;   // оружие Seraph «в руках» — не ядро
  std::vector<Il2CppObject*> meshes;  // Transform копий с рендерером
  int dropped = 0;
  bool tpose = true;
};

// inHand — узел уже внутри руки (у Grappler «Left Hand/Left Hand», у Kobold «Left Hand Anchor/…/Left Hand»: рука — внешний)
Il2CppObject* CopyNode(Il2CppObject* src, Il2CppObject* parent, HoloCopy& c, Il2CppObject* material, bool inHand = false) {
  std::string name = NameOf(src);
  auto go = NewGameObject(name.c_str(), parent);
  auto t = TransformOf(go);
  SetLocal(t, Call<Vec3>(gGetLocalPosition, src), Call<Quat>(gGetLocalRotation, src), Call<Vec3>(gGetLocalScale, src));
  c.map[src] = t;
  auto srcGo = GameObjectOf(src);

  // Рука — узел с PoseInterpolator; на Quest этого типа не видно — тогда внешний узел с именем руки
  bool hand = HandNodeName(name) && !inHand &&
              (!gPoseInterpolatorType || Component(srcGo, gPoseInterpolatorType));
  if (hand)
    c.hands.push_back({t, name.rfind("Left", 0) == 0, c.tpose});
  else if (name == "Offhand Pose" || name == "Railgun Pose")
    c.held.push_back(t);
  if (gScalarAnimType)
    if (auto anim = Component(srcGo, gScalarAnimType)) c.anims.push_back({anim, src});

  auto filter = Component(srcGo, gMeshFilterType);
  auto mesh = filter ? Call<Il2CppObject*>(gGetSharedMesh, filter) : nullptr;
  if (mesh) {
    Invoke(gSetSharedMesh, Invoke(gAddComponent, go, gMeshFilterType), mesh);
    auto renderer = Component(srcGo, gRendererType);
    auto m = renderer ? Call<Il2CppObject*>(gGetSharedMaterial, renderer) : nullptr;
    bool enabled = renderer && (!gRendererEnabled || Call<bool>(gRendererEnabled, renderer));
    if (renderer && IsBodyMaterial(m) && !JunkMesh(name) && enabled) {
      auto r = Invoke(gAddComponent, go, gMeshRendererType);
      if (r) Call<void>(gSetSharedMaterial, r, material);
      c.meshes.push_back(t);
    } else if (renderer && !IsBodyMaterial(m)) {
      c.dropped++;
    }
  }
  for (int i = 0, n = ChildCount(src); i < n; i++) {
    auto child = Child(src, i);
    if (!child || JunkNode(NameOf(child))) continue;
    auto childGo = GameObjectOf(child);
    if (gRectTransformType && Component(childGo, gRectTransformType)) continue;
    CopyNode(child, t, c, material, inHand || hand);
  }
  if (!ActiveSelf(srcGo) || ThrownNode(name)) SetActive(go, false);
  return go;
}

// Детали, которые двигает ScalarProceduralAnimation, — в позу по её currentTime (как игра при появлении класса)
int RestPoses(HoloCopy& c, Il2CppObject* space, int& distrusted) {
  int count = 0;
  distrusted = 0;
  if (!gAnimatedTransformsOffset || !il2cpp_object_get_class_fn) return 0;
  Pose3 sp = WorldPose(space);
  for (auto [anim, owner] : c.anims) {
    float time = std::clamp(*reinterpret_cast<float*>(reinterpret_cast<char*>(anim) + gCurrentTimeOffset), 0.f, 1.f);
    auto list = *reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(anim) + gAnimatedTransformsOffset);
    if (!list) continue;
    auto listClass = reinterpret_cast<Il2CppClass*>(il2cpp_object_get_class_fn(list));
    auto itemsF = il2cpp_class_get_field_from_name(listClass, "_items");
    auto sizeF = il2cpp_class_get_field_from_name(listClass, "_size");
    if (!itemsF || !sizeF) continue;
    auto items = *reinterpret_cast<Il2CppArray**>(reinterpret_cast<char*>(list) + il2cpp_field_get_offset(itemsF));
    int size = *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(list) + il2cpp_field_get_offset(sizeF));
    for (int i = 0; i < size && i < int(Length(items)); i++) {
      auto at = At(items, i);
      if (!at) continue;
      auto k = reinterpret_cast<Il2CppClass*>(il2cpp_object_get_class_fn(at));
      auto off = [&](char const* f) -> size_t {
        auto field = il2cpp_class_get_field_from_name(k, f);
        return field ? il2cpp_field_get_offset(field) : 0;
      };
      size_t oT = off("transform"), oSP = off("startPos"), oEP = off("endPosition"), oSR = off("startRot"),
             oER = off("endRotation"), oSS = off("startScale"), oES = off("endScale");
      if (!oT || !oSP || !oEP || !oSR || !oER) continue;
      auto base = reinterpret_cast<char*>(at);
      auto src = *reinterpret_cast<Il2CppObject**>(base + oT);
      auto it = c.map.find(src);
      if (!src || it == c.map.end()) continue;
      auto t = it->second;
      auto get3 = [&](size_t o) { return *reinterpret_cast<Vec3*>(base + o); };
      auto getQ = [&](size_t o) { return *reinterpret_cast<Quat*>(base + o); };
      Vec3 was = Call<Vec3>(gGetLocalPosition, t), wasAt = ToLocal(sp, Call<Vec3>(gGetPosition, t));
      Call<void>(gSetLocalPosition, t, Lerp(get3(oSP), get3(oEP), time));
      if (Length3(ToLocal(sp, Call<Vec3>(gGetPosition, t)) - wasAt) > kMaxRestShift) {
        Call<void>(gSetLocalPosition, t, was);
        distrusted++;
        continue;
      }
      Call<void>(gSetLocalRotation, t, Nlerp(getQ(oSR), getQ(oER), time));
      if (oSS && oES) {
        Vec3 s = Lerp(get3(oSS), get3(oES), time);
        if (Dot(s, s) > 1e-8f) Invoke(gSetLocalScale, t, &s);
      }
      count++;
    }
  }
  return count;
}

struct Box3 {
  Vec3 mn, mx;
  bool any = false;
  void Add(Vec3 p) {
    if (!any) {
      mn = mx = p;
      any = true;
      return;
    }
    mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
    mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
  }
  Vec3 Center() const { return (mn + mx) * 0.5f; }
  Vec3 Size() const { return mx - mn; }
};

bool Under(Il2CppObject* t, Il2CppObject* root) {
  for (auto p = t; p; p = Call<Il2CppObject*>(gGetParent, p))
    if (p == root) return true;
  return false;
}

bool ActiveInTree(Il2CppObject* t, Il2CppObject* stop) {
  for (auto p = t; p && p != stop; p = Call<Il2CppObject*>(gGetParent, p))
    if (!ActiveSelf(GameObjectOf(p))) return false;
  return true;
}

// Габариты мешей (их границ) в осях space
Box3 MeshBox(std::vector<Il2CppObject*> const& nodes, Pose3 const& space) {
  Box3 box;
  for (auto t : nodes) {
    auto filter = Component(GameObjectOf(t), gMeshFilterType);
    auto mesh = filter ? Call<Il2CppObject*>(gGetSharedMesh, filter) : nullptr;
    Bounds b = MeshBounds(mesh);
    if (!(b.extents.x > 0 || b.extents.y > 0 || b.extents.z > 0)) continue;
    Pose3 w = WorldPose(t);
    for (int i = 0; i < 8; i++) {
      Vec3 c{b.center.x + ((i & 1) ? b.extents.x : -b.extents.x), b.center.y + ((i & 2) ? b.extents.y : -b.extents.y),
             b.center.z + ((i & 4) ? b.extents.z : -b.extents.z)};
      box.Add(ToLocal(space, ToWorld(w, c)));
    }
  }
  return box;
}

// Projection — эффекты пьедестала из надписей TMP
struct Projection {
  Held root;
  Il2CppObject* t = nullptr;
  std::vector<Mark> rings;  // «O», лёжа
  Mark scan;
  std::vector<Mark> sparks;
  Mark glow;
  std::vector<Vec3> sparkDir;
  std::vector<float> sparkSpeed, sparkPhase;
  Bounds o{};  // границы буквы «O»
  bool ready = false, shown = false;
  float height = 0.3f, r1 = 0.3f;
} gFx;

constexpr float kFxR0 = 0.12f;
constexpr int kFxRings = 2, kFxSparks = 6;

// Буква «O» как плоское кольцо радиуса r на высоте y (в осях эффекта)
void PlaceRing(Mark& m, float r, float y, char const* color) {
  std::string text = std::string("<color=#") + color + ">O</color>";
  if (m.shown != text) {
    m.shown = text;
    Invoke(gSetText, m.text, il2cpp_string_new(text.c_str()));
  }
  Vec3 s{2 * r / (2 * gFx.o.extents.x), 2 * r / (2 * gFx.o.extents.y), 1};
  Quat rot = QuatAxis({1, 0, 0}, 90);
  Vec3 p = Vec3{0, y, 0} - Rotate(rot, Vec3{gFx.o.center.x * s.x, gFx.o.center.y * s.y, 0});
  SetLocal(m.t, p, rot, s);
}

bool BuildProjection(Il2CppObject* terminal) {
  auto go = NewGameObject("Skins Projection", terminal);
  gFx.root = PartHeld(go);
  gFx.t = HoldPart(TransformOf(go));
  Call<void>(gSetLocalPosition, gFx.t, kHoloLens);
  gFx.rings.assign(kFxRings, Mark{});
  for (auto& m : gFx.rings)
    if (!NewMark(m, gFx.t, 0)) return false;
  if (!NewMark(gFx.scan, gFx.t, 0) || !NewMark(gFx.glow, gFx.t, 0)) return false;
  gFx.sparks.assign(kFxSparks, Mark{});
  for (auto& m : gFx.sparks)
    if (!NewMark(m, gFx.t, 0)) return false;
  uint32_t seed = 49;
  auto rnd = [&]() {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) / float(1 << 24);
  };
  for (int i = 0; i < kFxSparks; i++) {
    float a = rnd() * 6.2832f, d = 0.15f + 0.8f * rnd();
    gFx.sparkDir.push_back({std::cos(a) * d, 0, std::sin(a) * d});
    gFx.sparkSpeed.push_back(0.25f + 0.35f * rnd());
    gFx.sparkPhase.push_back(rnd());
  }
  // Буква для колец — меряем, как подложку
  Invoke(gSetText, gFx.scan.text, il2cpp_string_new("<color=#00000000>O</color>"));
  gFx.scan.shown = "<color=#00000000>O</color>";
  // Не выключаем: TMP строит меш только у включённых — пустые надписи и так не видно до «Review Skin»
  return true;
}

void PlaceStaticRings() {
  if (!gFx.ready) return;  // буква ещё не измерена — кольца встанут, когда измерим
  for (int i = 0; i < kFxRings; i++) {
    float k = (i + 1.f) / kFxRings;
    char color[16];
    snprintf(color, sizeof color, "4DF2FF%02X", int(255 * (0.16f - 0.1f * k)));
    PlaceRing(gFx.rings[i], kFxR0 + (gFx.r1 - kFxR0) * k, gFx.height * k, color);
  }
}

void ShowProjection(float height, float radius) {
  if (!Alive(gFx.root.obj)) return;
  gFx.height = std::max(0.12f, height * 0.3f);
  gFx.r1 = std::clamp(radius + 0.05f, 0.22f, 0.45f);
  gFx.shown = true;
  // Свечение линзы — квадрат, лёжа
  Quat flat = QuatAxis({1, 0, 0}, 90);
  Vec3 gs{kFxR0 * 1.6f / gMarkSize.x, kFxR0 * 1.6f / gMarkSize.y, 1};
  MarkColor(gFx.glow, "4DF2FF30");
  SetLocal(gFx.glow.t, Vec3{0, 0.004f, 0} - Rotate(flat, Vec3{gMarkCenter.x * gs.x, gMarkCenter.y * gs.y, 0}), flat, gs);
  PlaceStaticRings();
}

void TickProjection(float time, Vec3 camPos) {
  if (!Alive(gFx.root.obj)) return;
  if (!gFx.ready) {
    Bounds b = TextBounds(gFx.scan.text);
    if (!(b.extents.x > 1e-5f && b.extents.y > 1e-5f)) return;
    gFx.o = b;
    gFx.ready = true;
    if (gFx.shown) PlaceStaticRings();
  }
  if (!gFx.shown) return;
  float p = std::fmod(time * 0.45f, 1.f);
  char color[16];
  snprintf(color, sizeof color, "4DF2FF%02X", int(255 * 0.22f * std::sin(p * 3.14159265f)));
  PlaceRing(gFx.scan, kFxR0 + (gFx.r1 - kFxR0) * p, p * gFx.height, color);
  for (int i = 0; i < kFxSparks; i++) {
    float q = std::fmod(time * gFx.sparkSpeed[i] + gFx.sparkPhase[i], 1.f);
    float r = kFxR0 + (gFx.r1 - kFxR0) * q;
    float size = 0.01f * std::sin(q * 3.14159265f);
    Vec3 local = gFx.sparkDir[i] * r + Vec3{0, q * gFx.height, 0};
    MarkColor(gFx.sparks[i], "BFFFFFA6");
    Vec3 s{std::max(size, 1e-4f) / gMarkSize.x, std::max(size, 1e-4f) / gMarkSize.y, 1};
    Invoke(gSetLocalScale, gFx.sparks[i].t, &s);
    Call<void>(gSetLocalPosition, gFx.sparks[i].t, local);
    Vec3 world = Call<Vec3>(gGetPosition, gFx.sparks[i].t);
    Call<void>(gSetRotation, gFx.sparks[i].t, LookRotation(world - camPos, {0, 1, 0}));
  }
}

struct Hologram {
  Held pivot;
  Il2CppObject* pt = nullptr;
  Held model;
  Applied skin;
  std::string key, name;
  double start = 0;
  std::vector<std::string> logged;
  std::vector<Held> held;  // узлы копии (NewGameObject) — отпускаются вместе с ней
} gHolo;

void ClearHologram() {
  Strip(gHolo.skin, false);
  Free(gHolo.skin.cls);
  if (Alive(gHolo.model.obj) && gDestroy) Invoke(gDestroy, nullptr, gHolo.model.obj);
  Free(gHolo.model);
  for (auto& h : gHolo.held) Free(h);
  gHolo.held.clear();
}

void ShowHologram(std::string const& key, std::string const& skinName) {
  if (!Alive(gHolo.pivot.obj)) return;
  ClearHologram();
  gHolo.key = key;
  gHolo.name = skinName;
  ScanClassPrefabs();
  auto it = gClassPrefabs.find(key);
  auto material = BodyMaterial();
  if (it == gClassPrefabs.end() || !material) {
    LOG(ANDROID_LOG_WARN, "голограмма: нет %s", it == gClassPrefabs.end() ? "префаба класса" : "материала тела");
    return;
  }
  auto prefabT = TransformOf(it->second.obj);
  Call<void>(gSetLocalRotation, gHolo.pt, Quat{0, 0, 0, 1});
  Call<void>(gSetLocalPosition, gHolo.pt, kHoloTop);

  HoloCopy c;
  c.tpose = key != "ROLLER";  // у Roller на «руках» пушки — только двигаем
  Il2CppObject* model;
  {
    KeepInto keep(gHolo.held);
    model = CopyNode(prefabT, gHolo.pt, c, material);
  }
  gHolo.model = Hold(model);
  auto mt = TransformOf(model);
  SetLocal(mt, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1});
  int distrusted = 0, posed = RestPoses(c, gHolo.pt, distrusted);

  // Поза и размер — по оригиналу класса, скин надевается потом
  Pose3 sp = WorldPose(gHolo.pt);
  std::vector<Il2CppObject*> visible, core;
  for (auto t : c.meshes)
    if (ActiveInTree(t, mt)) visible.push_back(t);
  for (auto t : visible) {
    bool aside = false;
    for (auto& h : c.hands) aside = aside || Under(t, h.t);
    for (auto h : c.held) aside = aside || Under(t, h);
    if (!aside) core.push_back(t);
  }
  if (core.empty()) core = visible;
  Box3 cb = MeshBox(core, sp);
  if (cb.any) {
    float h = std::max(cb.Size().y, 0.01f);
    for (auto& hand : c.hands) {
      if (hand.rotate) Call<void>(gSetRotation, hand.t, sp.r * QuatAxis({0, 1, 0}, hand.left ? 270.f : 90.f));
      std::vector<Il2CppObject*> mine;
      for (auto t : visible)
        if (Under(t, hand.t)) mine.push_back(t);
      Box3 hb = MeshBox(mine, sp);
      Vec3 now = hb.any ? hb.Center() : ToLocal(sp, Call<Vec3>(gGetPosition, hand.t));
      float half = hb.any ? hb.Size().x / 2 : 0.f;
      Vec3 want{hand.left ? cb.mn.x - kHandGap * h - half : cb.mx.x + kHandGap * h + half, cb.mn.y + kShoulderAt * h,
                cb.Center().z};
      Vec3 pos = Call<Vec3>(gGetPosition, hand.t);
      Call<void>(gSetPosition, hand.t, pos + Rotate(sp.r, Mul(sp.s, want - now)));
    }
  }
  Box3 ab = MeshBox(visible, sp);
  float height = 1.f, radius = 0.3f;
  if (cb.any && ab.any) {
    float root = std::max(Call<Vec3>(gGetLocalScale, prefabT).x, 0.01f);
    float scale = kGameScale * root;
    Vec3 as = ab.Size();
    scale = std::min({scale, kHoloMaxH / std::max(as.y, 0.01f), kHoloMaxW / std::max(as.x, 0.01f),
                      kHoloMaxD / std::max(as.z, 0.01f)});
    scale *= ClassSize(key);
    Vec3 cc = cb.Center();
    SetLocal(mt, {-cc.x * scale, -ab.mn.y * scale + kHoloLift, -cc.z * scale}, {0, 0, 0, 1}, {scale, scale, scale});
    height = as.y * scale + kHoloLift;
    radius = std::max(cb.Size().x, cb.Size().z) / 2 * scale;
  }

  gHolo.skin.cls = Hold(model);
  gHolo.skin.key = key;
  if (!skinName.empty()) Wear(gHolo.skin, skinName, "голограмма");
  ShowProjection(height + (kHoloTop.y - kHoloLens.y), radius);

  if (std::find(gHolo.logged.begin(), gHolo.logged.end(), key) == gHolo.logged.end()) {
    gHolo.logged.push_back(key);
    LOG(ANDROID_LOG_INFO, "голограмма %s: мешей %zu (ядро %zu), без эффектов %d, в позе покоя %d (не поверил %d), рук %zu",
        Title(key).c_str(), visible.size(), core.size(), c.dropped, posed, distrusted, c.hands.size());
  }
}

void TickHologram(Vec3 camPos) {
  if (!Alive(gHolo.pivot.obj)) return;
  float t = float(Now() - gHolo.start);
  Call<void>(gSetLocalRotation, gHolo.pt, QuatAxis({0, 1, 0}, t * kSpinSpeed));
  Call<void>(gSetLocalPosition, gHolo.pt, kHoloTop + Vec3{0, kBobHeight * std::sin(t * kBobSpeed), 0});
  TickProjection(t, camPos);
  if (Alive(gHolo.model.obj) && !gHolo.skin.skin.empty() && !Sync(gHolo.skin)) ShowHologram(gHolo.key, gHolo.name);
}

// ---- зеркало (как Mirror.cs, но одной картинкой на оба глаза: стерео без своего шейдера не сделать) ----
constexpr int kMirrorTexW = 1024, kMirrorMsaa = 2;  // чётче, чем 512; MSAA — без «лесенки» на краях
constexpr float kMirrorMaxDistance = 10.f;
Held gMirrorCam, gMirrorTex, gMirrorGlass, gMirrorGlassT, gMirrorMaterial;
Il2CppObject* gMirrorCamT;
bool gMirrorFailed, gMirrorOn;
int gBodyLayers, gBodyLayersFrame = -1000;

// Камера зеркала переживает смену сцены (DontDestroyOnLoad): включена — рисует сцену в текстуру каждый кадр.
// Выключать её всегда, когда зеркала не видно: вне хаба, стекло уничтожено, игрок за зеркалом
void MirrorCamera(bool on) {
  if (on == gMirrorOn) return;
  gMirrorOn = on;
  if (Alive(gMirrorCam.obj) && gSetBehaviourEnabled) Invoke(gSetBehaviourEnabled, gMirrorCam.obj, &on);
}

// Материал для стекла: копия игрового материала, который рисует текстуру (одна на все заходы в хаб).
// Какие шейдеры есть — в лог
Il2CppObject* MirrorMaterial() {
  if (Alive(gMirrorMaterial.obj)) return gMirrorMaterial.obj;
  Free(gMirrorMaterial);
  static char const* const kWanted[] = {"Universal Render Pipeline/Unlit", "Unlit/Texture", "UI/Default",
                                        "Sprites/Default", "Universal Render Pipeline/Simple Lit",
                                        "Universal Render Pipeline/Lit"};
  auto all = reinterpret_cast<Il2CppArray*>(Invoke(gFindAllOfType, nullptr, gMaterialType));
  std::map<std::string, Il2CppObject*> byShader;
  for (uint32_t i = 0; i < Length(all); i++) {
    auto m = At(all, i);
    if (!Alive(m)) continue;
    std::string s = ShaderName(m);
    if (!s.empty() && !byShader.count(s)) byShader[s] = m;
  }
  std::string list;
  for (auto const& [s, m] : byShader) list += (list.empty() ? "" : ", ") + s;
  LOG(ANDROID_LOG_INFO, "зеркало: шейдеры в игре — %s", list.c_str());
  for (auto w : kWanted) {
    auto it = byShader.find(w);
    if (it == byShader.end()) continue;
    auto copy = gInstantiateOne ? Invoke(gInstantiateOne, nullptr, it->second) : nullptr;
    LOG(ANDROID_LOG_INFO, "зеркало: стекло — '%s' (%s)%s", NameOf(it->second).c_str(), w, copy ? "" : ", копию сделать не вышло");
    gMirrorMaterial = Hold(copy);
    return copy;
  }
  LOG(ANDROID_LOG_WARN, "зеркало: нет шейдера с текстурой — зеркала не будет");
  return nullptr;
}

Il2CppObject* BuildMirror() {
  auto prefab = HubAsset(gMirrorPrefab, "Mirror");
  if (!prefab || !gCameraType || !gRenderTextureCtor || !gSetTargetTexture || !(gSetProjection || gSetProjectionIcall) ||
      !gSetMainTexture)
    return nullptr;
  Vec3 p = kMirrorAt;
  p.y = kHubFloorY + kMirrorFromFloor + kMirrorH / 2;
  auto root = Instantiate(prefab, nullptr);
  if (!root) return nullptr;
  Invoke(gSetObjectName, root, il2cpp_string_new("Skins Mirror"));
  auto rt = TransformOf(root);
  Call<void>(gSetPosition, rt, p + kWall * kFromWall);
  Call<void>(gSetRotation, rt, LookRotation(kWall * -1.f, {0, 1, 0}));
  for (int i = 0, n = ChildCount(rt); i < n; i++) {
    auto c = Child(rt, i);
    std::string name = NameOf(c);
    if (name == "Frame") {
      SetLocal(c, {0, 0, 0.02f}, {0, 0, 0, 1}, {kMirrorW + 0.08f, kMirrorH + 0.08f, 0.04f});
      PaintBody(c, BodyMaterial(), false);
    } else if (name == "Glass") {
      SetLocal(c, {0, 0, -0.003f}, {0, 0, 0, 1}, {kMirrorW, kMirrorH, 1});
      gMirrorGlass = Hold(GameObjectOf(c));
      gMirrorGlassT = Hold(c);
    }
  }
  // Сквозь зеркало не пройти: коробка по рамке, 30 см в комнату
  if (gBoxColliderType && gBoxSetSize && gBoxSetCenter) {
    auto wall = NewGameObject("Collider", rt);
    auto box = Invoke(gAddComponent, wall, gBoxColliderType);
    Vec3 size{kMirrorW + 0.08f, kMirrorH + 0.08f, 0.3f}, center{0, 0, -0.13f};
    if (box) {
      Invoke(gBoxSetSize, box, &size);
      Invoke(gBoxSetCenter, box, &center);
    }
  }
  if (!gMirrorGlassT.obj) return root;

  if (!Alive(gMirrorTex.obj)) {
    Free(gMirrorTex);
    auto tex = il2cpp_object_new(gRenderTextureClass);
    int32_t w = kMirrorTexW, h = int32_t(kMirrorTexW * kMirrorH / kMirrorW), depth = 24;
    Invoke(gRenderTextureCtor, tex, &w, &h, &depth);
    int32_t msaa = kMirrorMsaa;
    if (gSetAntiAliasing) Invoke(gSetAntiAliasing, tex, &msaa);  // до первой отрисовки — текстура ещё не создана
    gMirrorTex = Hold(tex);
  }
  auto glassRenderer = Component(gMirrorGlass.obj, gRendererType);
  auto material = MirrorMaterial();
  if (!material || !glassRenderer) {
    gMirrorFailed = true;
    SetActive(gMirrorGlass.obj, false);
    return root;
  }
  Invoke(gSetMainTexture, material, gMirrorTex.obj);
  Call<void>(gSetSharedMaterial, glassRenderer, material);

  if (!Alive(gMirrorCam.obj)) {
    Free(gMirrorCam);
    auto camGo = NewGameObject("Skins Mirror Camera", nullptr);
    Invoke(gDontDestroyOnLoad, nullptr, camGo);
    auto cam = Invoke(gAddComponent, camGo, gCameraType);
    if (!cam) return root;
    gMirrorCam = Hold(cam);
    gMirrorCamT = Keep(TransformOf(camGo));
    bool off = false;
    Invoke(gSetBehaviourEnabled, cam, &off);
    int32_t none = 0;  // StereoTargetEyeMask.None — обычная камера, не шлем
    if (gSetStereoEye) Invoke(gSetStereoEye, cam, &none);
    Invoke(gSetTargetTexture, cam, gMirrorTex.obj);
  }
  LOG(ANDROID_LOG_INFO, "зеркало: поставлено, текстура %d px, MSAA %s", kMirrorTexW, gSetAntiAliasing ? "да" : "нет");
  return root;
}

// Слои своего класса: своя камера их может не рисовать, а в зеркале себя видно
int BodyLayers() {
  if (gLastFrame - gBodyLayersFrame < 60) return gBodyLayers;
  gBodyLayersFrame = gLastFrame;
  gBodyLayers = 0;
  if (!Alive(gLocal.cls.obj)) return 0;
  std::vector<HandNode> nodes;
  WalkNodes(TransformOf(gLocal.cls.obj), 0, nodes);
  for (auto& n : nodes) {
    auto go = GameObjectOf(n.t);
    if (!Component(go, gRendererType)) continue;
    int layer = Unbox<int32_t>(Invoke(gGetLayer, go));
    if (layer != 5) gBodyLayers |= 1 << layer;  // 5 — UI
  }
  return gBodyLayers;
}

Mat4 Frustum(float l, float r, float b, float t, float n, float f) {
  Mat4 m{};
  m.m[0] = 2 * n / (r - l);
  m.m[8] = (r + l) / (r - l);
  m.m[5] = 2 * n / (t - b);
  m.m[9] = (t + b) / (t - b);
  m.m[10] = -(f + n) / (f - n);
  m.m[14] = -2 * f * n / (f - n);
  m.m[11] = -1;
  return m;
}

// Зеркало: камера в точке, симметричной глазу относительно стекла, смотрит в комнату через стекло
// (внеосевая проекция). Одна картинка на оба глаза — в шлеме может укачивать (экран-камера не понравилась)
void TickMirror() {
  if (!Alive(gMirrorCam.obj) || !Alive(gMirrorGlassT.obj) || !Alive(gMirrorGlass.obj)) {
    MirrorCamera(false);
    return;
  }
  auto eye = Call<Il2CppObject*>(gGetMainCamera);
  bool show = eye != nullptr;
  Vec3 c{}, n{}, e{};
  float side = 0;
  Quat glassRot{0, 0, 0, 1};
  if (show) {
    glassRot = Call<Quat>(gGetRotation, gMirrorGlassT.obj);
    c = Call<Vec3>(gGetPosition, gMirrorGlassT.obj);
    n = Rotate(glassRot, {0, 0, -1});  // в комнату
    e = Call<Vec3>(gGetPosition, TransformOfComponent(eye));
    side = Dot(e - c, n);
    show = side > 0.05f && Length3(e - c) < kMirrorMaxDistance;
  }
  MirrorCamera(show);
  if (!show) return;

  Vec3 up = Rotate(glassRot, {0, 1, 0});
  Vec3 pos = e - n * (2 * side);
  Quat rot = LookRotation(n, up);
  Call<void>(gSetPosition, gMirrorCamT, pos);
  Call<void>(gSetRotation, gMirrorCamT, rot);
  Vec3 rel = c - pos, right = Rotate(rot, {1, 0, 0}), camUp = Rotate(rot, {0, 1, 0});
  float d = Dot(rel, n), cx = Dot(rel, right), cy = Dot(rel, camUp);
  float near = d + 0.002f, k = near / d;
  float far = gGetFarClip ? std::max(Unbox<float>(Invoke(gGetFarClip, eye)), near + 1.f) : 200.f;
  Mat4 proj = Frustum((cx - kMirrorW / 2) * k, (cx + kMirrorW / 2) * k, (cy - kMirrorH / 2) * k, (cy + kMirrorH / 2) * k,
                      near, far);
  if (gSetProjection) Invoke(gSetProjection, gMirrorCam.obj, &proj);
  else gSetProjectionIcall(NativePtr(gMirrorCam.obj), &proj);
  int32_t mask = Unbox<int32_t>(Invoke(gGetCullingMask, eye)) | BodyLayers();
  Invoke(gSetCullingMask, gMirrorCam.obj, &mask);
  if (gGetClearFlags && gSetClearFlags) {
    int32_t flags = Unbox<int32_t>(Invoke(gGetClearFlags, eye));
    Invoke(gSetClearFlags, gMirrorCam.obj, &flags);
  }
  if (gGetBackground && gSetBackground) {
    Color4 bg = Unbox<Color4>(Invoke(gGetBackground, eye));
    Invoke(gSetBackground, gMirrorCam.obj, &bg);
  }
  if (gGetDepth && gSetDepth) {
    float depth = Unbox<float>(Invoke(gGetDepth, eye)) - 1.f;
    Invoke(gSetDepth, gMirrorCam.obj, &depth);
  }
}

// ---- мебель в пещере у лавы ----
// Префаб «Furniture» из Hub.bundle — уже в мировых координатах хаба (расставлена в Unity, Assets/Hub/HubCave.unity):
// ставим в (0, 0, 0), материал тела игры (цвета вершин), коллизия по мешам
Held gFurniture, gFurnitureLight;  // gFurnitureLight — MaterialPropertyBlock со светом зонда
bool gFurnitureFailed;

Il2CppObject* BuildFurniture() {
  auto prefab = HubAsset(gFurniturePrefab, "Furniture");
  auto material = BodyMaterial();
  if (!prefab || !material) return nullptr;
  auto go = Instantiate(prefab, nullptr);
  if (!go) return nullptr;
  Invoke(gSetObjectName, go, il2cpp_string_new("Skins Furniture"));
  auto t = TransformOf(go);
  Call<void>(gSetPosition, t, Vec3{0, 0, 0});
  Call<void>(gSetRotation, t, Quat{0, 0, 0, 1});
  PaintBody(t, material, true);
  // Свет: числа — из лога ПК («Мебель: свет зонда (для Quest)»), там же они считаются из CaveAmbient/LavaLight
  struct V4 {
    float x, y, z, w;
  };
  static const std::pair<char const*, V4> kProbe[] = {
      {"unity_SHAr", {0.29929f, 0.59859f, -0.22447f, 0.72978f}}, {"unity_SHAg", {0.12770f, 0.25540f, -0.09577f, 0.37337f}},
      {"unity_SHAb", {0.03991f, 0.07981f, -0.02993f, 0.18730f}}, {"unity_SHBr", {0.47588f, -0.35691f, -0.23050f, -0.17845f}},
      {"unity_SHBg", {0.20304f, -0.15228f, -0.09835f, -0.07614f}}, {"unity_SHBb", {0.06345f, -0.04759f, -0.03073f, -0.02379f}},
      {"unity_SHC", {-0.17845f, -0.07614f, -0.02379f, 1.f}}};
  Il2CppObject* block = nullptr;
  if (gPropertyBlockClass && gPropertyBlockCtor && gPropertyBlockSetVector && gSetPropertyBlock && gSetLightProbeUsage) {
    Free(gFurnitureLight);  // рендереры копируют блок в SetPropertyBlock — прежний больше не нужен
    gFurnitureLight = Hold(il2cpp_object_new(gPropertyBlockClass));
    block = gFurnitureLight.obj;
    Invoke(gPropertyBlockCtor, block);
    for (auto const& [name, v] : kProbe) {
      V4 value = v;
      Invoke(gPropertyBlockSetVector, block, il2cpp_string_new(name), &value);
    }
  }
  std::vector<HandNode> nodes;
  WalkNodes(t, 0, nodes);
  bool no = false;
  int32_t custom = 4;  // LightProbeUsage.CustomProvided
  int lit = 0;
  for (auto& n : nodes)
    if (auto r = Component(GameObjectOf(n.t), gRendererType)) {
      if (gSetReceiveShadows) Invoke(gSetReceiveShadows, r, &no);
      if (block) {
        Invoke(gSetLightProbeUsage, r, &custom);
        Invoke(gSetPropertyBlock, r, block);
        lit++;
      }
    }
  LOG(ANDROID_LOG_INFO, "мебель: свет зонда на %d рендерерах", lit);
  LOG(ANDROID_LOG_INFO, "мебель: поставлена (%d предметов)", ChildCount(t));
  return go;
}

// ---- уголок целиком ----
Held gTerminal, gMirror;
int gHubBuildFrames;

// Поставить терминал, меню, голограмму (сцена хаба, скины загружены, шрифт и подложка измерены)
Il2CppObject* BuildTerminal() {
  auto prefab = HubAsset(gTerminalPrefab, "Terminal");
  if (!prefab) return nullptr;
  auto material = BodyMaterial();
  if (!material) return nullptr;  // классы ещё не загружены — попробуем позже
  auto go = Instantiate(prefab, nullptr);
  if (!go) return nullptr;
  Invoke(gSetObjectName, go, il2cpp_string_new("Skins Terminal"));
  auto t = TransformOf(go);
  Vec3 origin = kMonitorAt + kWall * (kTerminalBack * kTerminalScale + 0.01f);
  origin.y = kHubFloorY;
  Call<void>(gSetPosition, t, origin);
  Call<void>(gSetRotation, t, LookRotation(kWall, {0, 1, 0}));
  Vec3 s{kTerminalScale, kTerminalScale, kTerminalScale};
  Invoke(gSetLocalScale, t, &s);
  PaintBody(t, material, true);  // до меню и голограммы: у них коллизии нет

  LOG(ANDROID_LOG_INFO, "хаб: терминал поставлен");
  return go;
}

// Подложка <mark> — меряем сами, если меню на руке ещё не открывали. Надпись-замер — на экране терминала
Held gProbe;
bool MeasureHubMark(Il2CppObject* terminal) {
  if (gMarkMeasured) return true;
  if (!Alive(gProbe.obj)) {
    Free(gProbe);
    auto text = NewText(gHubFont, {0, kScrCenterY, kScrFrontZ - 0.01f}, 0, kAlignCenter, 0, terminal);
    if (!text) return false;
    Invoke(gSetText, text, il2cpp_string_new(MarkText("00000000").c_str()));
    gProbe = Hold(GameObjectOf(text));
    return false;
  }
  auto text = Component(gProbe.obj, gTextMeshProType);
  Bounds b = text ? TextBounds(text) : Bounds{};
  if (!(b.extents.x > 1e-5f && b.extents.y > 1e-5f)) return false;
  gMarkCenter = b.center;
  gMarkSize = b.extents * 2.f;
  gMarkMeasured = true;
  LOG(ANDROID_LOG_INFO, "хаб: подложка <mark> %.4f × %.4f", gMarkSize.x, gMarkSize.y);
  if (gDestroy) Invoke(gDestroy, nullptr, gProbe.obj);
  Free(gProbe);
  return true;
}

// Меню на экране, голограмма и эффекты — когда есть шрифт и измерена подложка
void BuildTerminalParts() {
  if (gScr.built || gScr.failed || gMenuFailed || !gHasBackground || !gGameFont) return;
  KeepInto keep(gTerminalHeld);  // всё, что ниже создаётся, отпускает FreeTerminalParts
  if (!gHubFont) {
    auto pixel = MenuFont(gGameFont);
    gHubFont = pixel ? pixel : gGameFont;
  }
  auto t = TransformOf(gTerminal.obj);
  if (!MeasureHubMark(t)) return;
  if (!BuildScreen(t)) {
    LOG(ANDROID_LOG_ERROR, "терминал: меню на экране не собралось");
    // Недостроенное меню (без курсора и части кнопок) не оставляем: TickScreen работает только с built
    if (gDestroy && Alive(gScr.root.obj)) Invoke(gDestroy, nullptr, gScr.root.obj);
    gScr.failed = true;  // не собирать заново каждый кадр
  }
  auto pivot = NewGameObject("Skins Hologram", t);
  gHolo.pivot = PartHeld(pivot);
  gHolo.pt = HoldPart(TransformOf(pivot));
  gHolo.start = Now();
  if (!BuildProjection(t)) LOG(ANDROID_LOG_ERROR, "терминал: эффекты пьедестала не собрались");
}

// Терминал уничтожен (смена сцены): отпустить меню, голограмму, эффекты и все их handle
void FreeTerminalParts() {
  ReleaseHubRay();
  ClearHologram();
  gScr = Screen{};
  gFx = Projection{};
  gHolo.pivot = {};
  gHolo.pt = nullptr;
  for (auto& h : gTerminalHeld) Free(h);
  gTerminalHeld.clear();
}

void TickHub() {
  if (gHubFailed || !gSkinsLoaded || !InHubScene()) {
    // Вне хаба уголок не тикает — но камера зеркала и луч не должны остаться включёнными
    MirrorCamera(false);
    ReleaseHubRay();
    return;
  }
  if (!Alive(gTerminal.obj)) {
    Free(gTerminal);
    FreeTerminalParts();
    Free(gProbe);
    if (++gHubBuildFrames % 30 != 1) return;  // не каждый кадр, если что-то не готово
    gTerminal = Hold(BuildTerminal());
    if (!gTerminal.obj) return;
  }
  BuildTerminalParts();
  if (!gFurnitureFailed && !Alive(gFurniture.obj)) {
    Free(gFurniture);
    gFurniture = Hold(BuildFurniture());
    if (!gFurniture.obj) gFurnitureFailed = true;
  }
  if (!gMirrorFailed && !Alive(gMirror.obj)) {
    Free(gMirror);
    Free(gMirrorGlass);
    Free(gMirrorGlassT);
    MirrorCamera(false);
    gMirror = Hold(BuildMirror());
    if (!gMirror.obj) gMirrorFailed = true;
  }

  Vec3 camPos{};
  if (auto camera = Call<Il2CppObject*>(gGetMainCamera)) camPos = Call<Vec3>(gGetPosition, TransformOfComponent(camera));
  TickScreen();
  TickHologram(camPos);
  TickMirror();
}

bool InitHub(Il2CppImage const* core) {
  il2cpp_object_get_class_fn =
      reinterpret_cast<Il2CppObject* (*)(Il2CppObject*)>(dlsym(modloader_libil2cpp_handle, "il2cpp_object_get_class"));
  auto sceneManager = Class(core, "UnityEngine.SceneManagement", "SceneManager");
  auto scene = Class(core, "UnityEngine.SceneManagement", "Scene");
  auto object = Class(core, "UnityEngine", "Object");
  auto renderer = Class(core, "UnityEngine", "Renderer");
  auto meshRenderer = Class(core, "UnityEngine", "MeshRenderer");
  auto mesh = Class(core, "UnityEngine", "Mesh");
  auto material = Class(core, "UnityEngine", "Material");
  auto camera = Class(core, "UnityEngine", "Camera");
  auto behaviour = Class(core, "UnityEngine", "Behaviour");
  auto rectTransform = Class(core, "UnityEngine", "RectTransform");
  gRenderTextureClass = Class(core, "UnityEngine", "RenderTexture");
  if (!sceneManager || !scene || !meshRenderer || !material || !object || !renderer) return false;

  gActiveScene = Method(sceneManager, "GetActiveScene", 0);
  gSceneName = Method(scene, "get_name", 0);
  gGetActiveSelf = Method(gGameObjectClass, "get_activeSelf", 0);
  gRendererEnabled = Method(renderer, "get_enabled", 0, false);
  gSetReceiveShadows = Method(renderer, "set_receiveShadows", 1, false);
  gSetPropertyBlock = FindOverload(renderer, "SetPropertyBlock", 1, "MaterialPropertyBlock", nullptr, false);
  gSetLightProbeUsage = Method(renderer, "set_lightProbeUsage", 1, false);
  gPropertyBlockClass = il2cpp_class_from_name(core, "UnityEngine", "MaterialPropertyBlock");
  if (gPropertyBlockClass) {
    gPropertyBlockCtor = Method(gPropertyBlockClass, ".ctor", 0, false);
    gPropertyBlockSetVector = FindOverload(gPropertyBlockClass, "SetVector", 2, "String", "Vector4", false);
  }
  LOG(ANDROID_LOG_INFO, "хаб: свет мебели — блок %s, SetVector %s, SetPropertyBlock %s, lightProbeUsage %s",
      gPropertyBlockCtor ? "да" : "нет", gPropertyBlockSetVector ? "да" : "нет", gSetPropertyBlock ? "да" : "нет",
      gSetLightProbeUsage ? "да" : "нет");
  gMeshBoundsHub = mesh ? Method(mesh, "get_bounds", 0, false) : nullptr;
  if (!gMeshBoundsHub) Icall(gMeshBoundsIcall, "UnityEngine.Mesh::get_bounds_Injected");
  gInstantiateOne = FindOverload(object, "Instantiate", 1, "Object", nullptr, false);
  gSetMainTexture = Method(material, "set_mainTexture", 1, false);
  gMeshRendererType = TypeOf(meshRenderer);
  gMaterialType = TypeOf(material);
  gRectTransformType = rectTransform ? TypeOf(rectTransform) : nullptr;
  if (camera) {
    gCameraType = TypeOf(camera);
    gGetCullingMask = Method(camera, "get_cullingMask", 0);
    gSetCullingMask = Method(camera, "set_cullingMask", 1);
    gSetTargetTexture = Method(camera, "set_targetTexture", 1);
    gSetProjection = Method(camera, "set_projectionMatrix", 1, false);
    if (!gSetProjection) Icall(gSetProjectionIcall, "UnityEngine.Camera::set_projectionMatrix_Injected");
    gGetClearFlags = Method(camera, "get_clearFlags", 0, false);
    gSetClearFlags = Method(camera, "set_clearFlags", 1, false);
    gGetBackground = Method(camera, "get_backgroundColor", 0, false);
    gSetBackground = Method(camera, "set_backgroundColor", 1, false);
    gGetDepth = Method(camera, "get_depth", 0, false);
    gSetDepth = Method(camera, "set_depth", 1, false);
    gGetFarClip = Method(camera, "get_farClipPlane", 0, false);
    gSetStereoEye = Method(camera, "set_stereoTargetEye", 1, false);
  }
  gSetBehaviourEnabled = behaviour ? Method(behaviour, "set_enabled", 1) : nullptr;
  if (gRenderTextureClass) {
    gRenderTextureCtor = FindOverload(gRenderTextureClass, ".ctor", 3, "Int32", "Int32");
    gSetAntiAliasing = Method(gRenderTextureClass, "set_antiAliasing", 1, false);
  }
  if (auto physics = FindImage("UnityEngine.PhysicsModule.dll")) {
    if (auto meshCollider = il2cpp_class_from_name(physics, "UnityEngine", "MeshCollider")) gMeshColliderType = TypeOf(meshCollider);
    if (auto boxCollider = il2cpp_class_from_name(physics, "UnityEngine", "BoxCollider")) {
      gBoxColliderType = TypeOf(boxCollider);
      gBoxSetSize = Method(boxCollider, "set_size", 1, false);
      gBoxSetCenter = Method(boxCollider, "set_center", 1, false);
    }
  }
  if (auto dust = FindImage("Dust.dll"))
    if (auto scalar = il2cpp_class_from_name(dust, "Dust", "ScalarProceduralAnimation"))
      if (auto field = il2cpp_class_get_field_from_name(scalar, "animatedTransforms"))
        gAnimatedTransformsOffset = il2cpp_field_get_offset(field);

  LOG(ANDROID_LOG_INFO,
      "хаб: сцена %s, коллизия %s, зеркало %s, поза покоя %s, границы мешей %s",
      gActiveScene && gSceneName ? "да" : "нет", gMeshColliderType ? "да" : "нет",
      gCameraType && gRenderTextureCtor && gSetTargetTexture && (gSetProjection || gSetProjectionIcall) && gSetMainTexture &&
              gSetBehaviourEnabled
          ? "да"
          : "нет",
      gAnimatedTransformsOffset && il2cpp_object_get_class_fn ? "да" : "нет",
      gMeshBoundsHub ? "Mesh.bounds" : gMeshBoundsIcall ? "icall" : "нет");
  if (!gSetBehaviourEnabled) gMirrorFailed = true;
  return gActiveScene && gSceneName && gGetActiveSelf && gMeshRendererType;
}
