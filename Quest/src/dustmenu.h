// dustmenu.h — общее меню Quest-модов LolegRus: панель над левой рукой (стиль меню Skins: графитовая панель,
// плитки, шрифт Pixelify Sans, тающий луч, курсор-точка) и список «Mods», через который моды делят левый стик.
//
// Файл ОДИН И ТОТ ЖЕ во всех модах с этим меню (копия в <Мод>\Quest\src каждого) — правишь один,
// копируй во все (build-quest.ps1 предупредит, если копии разные). Меняешь протокол — подними kProtocol.
//
// Как моды делят стик. Каждый мод экспортирует C-функции dustmenu_* (ниже, в конце файла) и при первом кадре
// находит остальных: modloader_get_loaded() + dlsym по хэндлу каждого мода. «Главный» — мод с наименьшим id;
// только он читает левый стик и рисует список «Mods»:
//   стик → список модов → курок по строке → открывается меню этого мода;
//   стик при открытом меню мода → назад к списку; стик при открытом списке → закрыть.
// Мод без соседей (стоит один) открывает своё меню сразу, без списка. В меню мода слева сверху — название,
// справа — кнопка «< Mods» (если есть соседи). Курок — правый, луч — от правого контроллера.
//
// Подключение: #include "dustmenu.h" после своих #include (до своего кода), в Init — dustmenu::Init(host)
// после il2cpp_init, каждый кадр (один раз) — dustmenu::Tick() до своего меню. Своё меню рисуется так:
//   if (!dustmenu::IsOpen()) { panel.Hide(); return; }
//   if (!panel.Begin(width, height)) return;
//   if (panel.Header("Title")) { dustmenu::Back(); ... }
//   if (panel.Button(cx, cy, w, h)) ...;  panel.Text(x, y, "<color=#FFFFFF>...</color>");
//   panel.End();
// Координаты панели — в метрах: начало — середина верхнего края, x вправо, y вверх (содержимое — y < 0).

#pragma once

#include <android/log.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "modloader.h"

namespace {
namespace dustmenu {

constexpr int kProtocol = 1;

char const* gTag = "dustmenu";
#define DM_LOG(prio, ...) __android_log_print(prio, ::dustmenu::gTag, __VA_ARGS__)

// ---------- il2cpp (свои указатели: файл не зависит от кода мода) ----------

struct Domain;
struct Assembly;
struct Image;
struct Klass;
struct Type;
struct Field;
struct Method;
struct Object;
struct String;
struct Array;

namespace api {
Domain* (*domain_get)();
Assembly const** (*domain_get_assemblies)(Domain const*, size_t*);
Image const* (*assembly_get_image)(Assembly const*);
char const* (*image_get_name)(Image const*);
Klass* (*class_from_name)(Image const*, char const*, char const*);
Method const* (*class_get_method_from_name)(Klass*, char const*, int);
Method const* (*class_get_methods)(Klass*, void**);
char const* (*class_get_name)(Klass*);
Type const* (*class_get_type)(Klass*);
char const* (*method_get_name)(Method const*);
uint32_t (*method_get_param_count)(Method const*);
Type const* (*method_get_param)(Method const*, uint32_t);
bool (*method_is_generic)(Method const*);
Klass* (*class_from_type)(Type const*);
Object* (*type_get_object)(Type const*);
Field* (*class_get_field_from_name)(Klass*, char const*);
size_t (*field_get_offset)(Field*);
Object* (*object_new)(Klass*);
Object* (*runtime_invoke)(Method const*, void*, void**, Object**);
String* (*string_new)(char const*);
char16_t* (*string_chars)(String*);
int32_t (*string_length)(String*);
// В Unity 6 GC handle — указатель, а не uint32_t
void* (*gchandle_new)(Object*, bool);
void (*gchandle_free)(void*);
Object* (*gchandle_get_target)(void*);
uint32_t (*array_length)(Array*);
void* (*resolve_icall)(char const*);
}  // namespace api

// Прямой вызов: MethodInfo начинается с methodPointer, последний аргумент — сам MethodInfo
template <typename R, typename... Args>
R Call(Method const* m, Args... args) {
  auto fn = *reinterpret_cast<R (* const*)(Args..., Method const*)>(m);
  return fn(args..., m);
}

// Через runtime_invoke — исключение игры пишется в лог, а не роняет её. Значения — указателями
template <typename... Args>
Object* Invoke(Method const* m, void* self, Args*... args) {
  void* argv[] = {const_cast<void*>(static_cast<void const*>(args))..., nullptr};
  Object* exc = nullptr;
  auto result = api::runtime_invoke(m, self, argv, &exc);
  if (exc) {
    DM_LOG(ANDROID_LOG_WARN, "меню: исключение в %s", api::method_get_name(m));
    return nullptr;
  }
  return result;
}

// GC il2cpp не видит нашу память: объекты, которые храним, держим через GC handle
struct Held {
  Object* obj = nullptr;
  void* handle = nullptr;
};
Held Hold(Object* obj) { return {obj, obj ? api::gchandle_new(obj, false) : nullptr}; }
void Free(Held& h) {
  if (h.handle) api::gchandle_free(h.handle);
  h = {};
}
template <typename T>
T* Keep(T* obj) {
  if (obj) api::gchandle_new(reinterpret_cast<Object*>(obj), false);
  return obj;
}

// ---------- математика (Unity: левая система координат) ----------

struct Vec3 {
  float x, y, z;
};
struct Quat {
  float x, y, z, w;
};
struct Bounds {
  Vec3 center, extents;
};
struct Vec2 {
  float x, y;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 Normalize(Vec3 v) {
  float len = std::sqrt(Dot(v, v));
  return len > 1e-6f ? v * (1.f / len) : Vec3{0, 0, 1};
}
inline Quat operator*(Quat a, Quat b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Quat Conj(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Vec3 Rotate(Quat q, Vec3 v) {
  Vec3 u{q.x, q.y, q.z};
  Vec3 t = Cross(u, v) * 2.f;
  return v + t * q.w + Cross(u, t);
}
inline Quat LookRotation(Vec3 f, Vec3 up) {
  f = Normalize(f);
  Vec3 r = Normalize(Cross(up, f));
  Vec3 u = Cross(f, r);
  float m00 = r.x, m01 = u.x, m02 = f.x, m10 = r.y, m11 = u.y, m12 = f.y, m20 = r.z, m21 = u.z, m22 = f.z;
  float t = m00 + m11 + m22;
  if (t > 0.f) {
    float s = 0.5f / std::sqrt(t + 1.f);
    return {(m21 - m12) * s, (m02 - m20) * s, (m10 - m01) * s, 0.25f / s};
  }
  if (m00 > m11 && m00 > m22) {
    float s = 2.f * std::sqrt(1.f + m00 - m11 - m22);
    return {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
  }
  if (m11 > m22) {
    float s = 2.f * std::sqrt(1.f + m11 - m00 - m22);
    return {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
  }
  float s = 2.f * std::sqrt(1.f + m22 - m00 - m11);
  return {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
}

// ---------- вид (как меню Skins, вариант «графит») ----------

constexpr float kFontSize = 0.12f;      // размер шрифта TMP; 3D-надпись: em = 0.1 размера
constexpr float kPanelAboveHand = 0.08f;  // м от левой руки до низа панели
constexpr float kPad = 0.012f;          // поля панели по бокам
constexpr float kHeaderY = -0.014f;     // строка заголовка
constexpr float kContentTop = -0.028f;  // верх первой строки плиток
constexpr float kBottomPad = 0.010f;    // под последней строкой
constexpr float kRowHeight = 0.030f, kRowGap = 0.006f;
constexpr float kBorder = 0.0015f;      // контур панели и выбранной плитки
constexpr float kPanelRadius = 0.008f, kTileRadius = 0.005f;
constexpr float kTextBox = 0.3f;        // рамка надписи, выровненной по краю
constexpr char const* kColPanel = "121314FF";
constexpr char const* kColPanelLine = "FFFFFF70";
constexpr char const* kColTile = "222427FF";
constexpr char const* kColTileHover = "2E3135FF";
constexpr char const* kColSelected = "3A3D42FF";
constexpr char const* kColSelectedLine = "FFFFFFFF";
constexpr char const* kColCursor = "FFFFFFFF";
constexpr char const* kColCursorLine = "121314D9";
constexpr float kCursorRadius = 0.0028f, kCursorBorder = 0.0008f;
// Луч, как в меню Meta: белая линия от руки к курсору, тает к концу и не доходит до курсора
constexpr int kRaySegments = 8;
constexpr float kRayStart = 0.04f, kRayGap = 0.03f, kRayIdleLength = 0.30f;
constexpr float kRayWidthStart = 0.0024f, kRayWidthEnd = 0.0012f, kRayAlpha = 0.55f;
// Порядок отрисовки (sortingOrder): прозрачное по расстоянию сортируется ненадёжно
enum Order { kOrderPanelLine = 10, kOrderPanel = 11, kOrderTileLine = 12, kOrderTile = 13, kOrderText = 14,
             kOrderCursorLine = 15, kOrderCursor = 16, kOrderRay = 17 };
// Выравнивание надписи (TextAlignmentOptions): по краю — от x, по центру — вокруг x
enum Align { kLeft = 513, kCenter = 514, kRight = 516 };
// Вид плитки
enum Look { kNormal, kSelected };

// Шрифт меню — Pixelify Sans (лицензия OFL): бандл MenuFont.font в папке мода (собирает Skins\Unity,
// Tools → Skins → Build Menu Font). Один бандл дважды Unity не грузит — сначала ищем шрифт, уже загруженный
// другим модом. Нет — шрифт игры. В атласе только ASCII: остальные надписи — шрифтом игры
constexpr char kFontFile[] = "MenuFont.font";
constexpr char kFontAsset[] = "PixelifySans SDF";

// ---------- Unity API ----------

struct Span {  // строка внутренних вызовов Unity 6 (ManagedSpanWrapper, UTF-16)
  void const* ptr;
  int32_t length;
};

Method const* gGetMainCamera;
Method const* gComponentTransform;
Method const* gGetPosition;
Method const* gGetRotation;
Method const* gSetPosition;
Method const* gSetRotation;
Method const* gSetLocalPosition;
Method const* gSetLocalScale;
Method const* gGetLossyScale;
Method const* gSetParent;
Method const* gGameObjectCtor;
Method const* gGameObjectTransform;
Method const* gAddComponent;
Method const* gSetActive;
Method const* gDontDestroyOnLoad;
Method const* gFindAllOfType;
Method const* gObjectName;
Method const* gGetDeviceId;
Method const* gGetButton;
Method const* gGetVec3;
Method const* gGetQuat;
Method const* gHaptic;
Method const* gSetText;
Method const* gSetFontSize;
Method const* gSetAlignment;
Method const* gSetWrapping;
Method const* gSetFont;
Method const* gDefaultFont;
Method const* gTextBounds;
Method const* gSortingOrder;
Method const* gSetSizeDelta;
Method const* gFontMaterial;
Method const* gGetShader;
Method const* gSetShader;
Method const* gLoadBundle;
Method const* gLoadAsset;
void* (*gLoadBundleIcall)(Span*, uint32_t, uint64_t);
void* (*gLoadAssetIcall)(void*, Span*, Object*);
Klass* gGameObjectClass;
Object* gTextMeshProType;
Object* gFontAssetType;
size_t gCachedPtrOffset;
String* gTriggerButton;
String* gStickClick;
String* gDevicePosition;
String* gDeviceRotation;
String* gPointerPosition;
String* gPointerRotation;
bool gReady;  // всё нашлось — меню есть

constexpr int kHead = 3, kLeftHand = 4, kRightHand = 5;  // XRNode

inline bool Alive(Object* obj) {
  return obj && *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + gCachedPtrOffset);
}
inline void* NativePtr(Object* obj) { return *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + gCachedPtrOffset); }
inline uint32_t Length(Array* a) { return a ? api::array_length(a) : 0; }
// Данные Il2CppArray идут после klass, monitor, bounds и max_length
inline Object* At(Array* a, uint32_t i) {
  return reinterpret_cast<Object**>(reinterpret_cast<char*>(a) + 4 * sizeof(void*))[i];
}

std::u16string Ascii16(std::string const& s) {
  std::u16string out;
  for (unsigned char c : s) out += char16_t(c < 0x80 ? c : '?');
  return out;
}

bool NameIs(Object* obj, char const* name) {
  auto s = reinterpret_cast<String*>(Invoke(gObjectName, obj));
  if (!s) return false;
  auto chars = api::string_chars(s);
  int32_t n = api::string_length(s);
  if (n != static_cast<int32_t>(strlen(name))) return false;
  for (int32_t i = 0; i < n; i++)
    if (chars[i] != char16_t(static_cast<unsigned char>(name[i]))) return false;
  return true;
}

uint64_t Device(int node) { return Call<uint64_t>(gGetDeviceId, node); }
bool Pressed(int node, String* usage) {
  bool value = false;
  return Call<bool>(gGetButton, Device(node), usage, &value) && value;
}
bool Pose(int node, String* posUsage, String* rotUsage, Vec3& pos, Quat& rot) {
  auto id = Device(node);
  return Call<bool>(gGetVec3, id, posUsage, &pos) && Call<bool>(gGetQuat, id, rotUsage, &rot);
}
inline void Buzz(int node, float amplitude, float duration) {
  if (gHaptic) Call<bool>(gHaptic, Device(node), uint32_t{0}, amplitude, duration);
}

Object* NewGameObject(char const* name, Object* parent) {
  auto go = Keep(api::object_new(gGameObjectClass));
  Invoke(gGameObjectCtor, go, api::string_new(name));
  if (parent) {
    bool keepWorld = false;
    Invoke(gSetParent, Call<Object*>(gGameObjectTransform, go), parent, &keepWorld);
  }
  return go;
}

// ---------- шрифт ----------

std::string gDataDir;  // папка мода в ModData, с «/» на конце
Held gFontBundle, gPixelFont;
Object* gGameFont;
int gFrame;  // счётчик кадров (Tick)
int gFontTries, gNextFontTry;
bool gBundleTried;

Object* GameFont() {
  if (Alive(gGameFont)) return gGameFont;
  Object* font = gDefaultFont ? Invoke(gDefaultFont, nullptr) : nullptr;
  if (!font) {  // нет шрифта в настройках TMP — любой загруженный
    auto all = reinterpret_cast<Array*>(Invoke(gFindAllOfType, nullptr, gFontAssetType));
    if (Length(all) > 0) font = At(all, 0);
  }
  gGameFont = Keep(font);
  return font;
}

Object* LoadBundle(std::string const& path) {
  if (gLoadBundle) return Invoke(gLoadBundle, nullptr, api::string_new(path.c_str()));
  if (!gLoadBundleIcall) return nullptr;
  auto wide = Ascii16(path);
  Span span{wide.data(), static_cast<int32_t>(wide.size())};
  void* handle = gLoadBundleIcall(&span, 0, 0);
  return handle ? api::gchandle_get_target(handle) : nullptr;
}

Object* LoadAsset(Object* bundle, char const* name, Object* type) {
  if (gLoadAsset) return Invoke(gLoadAsset, bundle, api::string_new(name), type);
  if (!gLoadAssetIcall) return nullptr;
  auto wide = Ascii16(name);
  Span span{wide.data(), static_cast<int32_t>(wide.size())};
  void* handle = gLoadAssetIcall(NativePtr(bundle), &span, type);
  return handle ? api::gchandle_get_target(handle) : nullptr;
}

// Pixelify Sans или nullptr (тогда — шрифт игры). Не нашёлся — ищем ещё (раз в ~5 с, до ~2 минут):
// другой мод мог загрузить его позже. Свой бандл грузим один раз: второй раз Unity его не загрузит
inline Object* PixelFont() {
  if (Alive(gPixelFont.obj)) return gPixelFont.obj;
  Free(gPixelFont);
  if (!gReady || gFontTries >= 24 || gFrame < gNextFontTry || !gFontMaterial || !gGetShader || !gSetShader)
    return nullptr;
  gNextFontTry = gFrame + 360;
  gFontTries++;

  Object* font = nullptr;
  auto all = reinterpret_cast<Array*>(Invoke(gFindAllOfType, nullptr, gFontAssetType));
  for (uint32_t i = 0; i < Length(all) && !font; i++)
    if (Alive(At(all, i)) && NameIs(At(all, i), kFontAsset)) font = At(all, i);
  char const* from = "уже загружен";
  if (!font) {
    if (!gBundleTried) {
      gBundleTried = true;
      std::string path = gDataDir + kFontFile;
      struct stat st;
      if (stat(path.c_str(), &st) == 0) gFontBundle = Hold(LoadBundle(path));
    }
    if (Alive(gFontBundle.obj)) font = LoadAsset(gFontBundle.obj, kFontAsset, gFontAssetType);
    from = "из MenuFont.font";
  }
  if (!font) {
    DM_LOG(ANDROID_LOG_WARN, "меню: нет шрифта Pixelify Sans (попытка %d) — шрифт игры", gFontTries);
    return nullptr;
  }
  // Шейдер — у шрифта игры: в бандле он без стерео-вариантов и рисовался бы одним глазом
  auto ours = Invoke(gFontMaterial, font);
  auto game = GameFont();
  auto theirs = game ? Invoke(gFontMaterial, game) : nullptr;
  auto shader = theirs ? Invoke(gGetShader, theirs) : nullptr;
  if (!ours || !shader) return nullptr;
  Invoke(gSetShader, ours, shader);
  gPixelFont = Hold(font);
  DM_LOG(ANDROID_LOG_INFO, "меню: шрифт Pixelify Sans (%s)", from);
  return font;
}

bool IsAscii(std::string const& s) {
  for (unsigned char c : s)
    if (c >= 0x80) return false;
  return true;
}

// ---------- подложка <mark> ----------
// Шейдеров, кроме TextMeshPro, в сборке для Quest нет (квад с Unlit не виден), поэтому прямоугольник —
// надпись TMP из одного невидимого символа в <mark>: подложку TMP рисует своим шейдером. Её размер меряем
// один раз по границам меша и дальше тянем масштабом
bool gHasBackground;  // есть TMP_Text.bounds и sortingOrder
bool gMarkMeasured;
Vec3 gMarkCenter, gMarkSize;
int gMeasureFrames;

std::string MarkText(char const* color) {
  return std::string("<mark=#") + color + "><color=#00000000>.</color></mark>";
}

// ---------- соседи (протокол) ----------

struct Host {
  char const* id;       // как в setup(): "skins"
  char const* title;    // в списке «Mods» и заголовке: "Skins"
  char const* tag;      // тег лога
  char const* dataDir;  // папка мода в ModData, с «/» на конце (там MenuFont.font)
  std::string (*status)();  // что сейчас включено (справа в списке): "1.0x"; может быть nullptr
};
Host gHost;
bool gOpen;         // своё меню открыто
bool gListVisible;  // список «Mods» (только у главного)

struct Peer {
  std::string id;
  bool self = false;
  char const* (*title)() = nullptr;
  char const* (*status)() = nullptr;
  void (*open)() = nullptr;
  void (*close)() = nullptr;
  bool (*isOpen)() = nullptr;
  void (*showList)() = nullptr;
};
std::vector<Peer> gPeers;  // и мы сами; по названию
bool gDiscovered;
bool gLeader;
size_t gLeaderIndex;
bool gStickWas;

std::string PeerTitle(Peer const& p) { return p.self ? gHost.title : p.title(); }
std::string PeerStatus(Peer const& p) {
  if (p.self) return gHost.status ? gHost.status() : "";
  char const* s = p.status();
  return s ? s : "";
}
bool PeerOpen(Peer const& p) { return p.self ? gOpen : p.isOpen(); }
void PeerSetOpen(Peer const& p, bool open) {
  if (p.self) gOpen = open;
  else if (open) p.open();
  else p.close();
}

template <typename T>
bool Sym(void* handle, char const* name, T& fn) {
  fn = reinterpret_cast<T>(dlsym(handle, name));
  return fn != nullptr;
}

// Кто ещё из модов умеет панель «Mods». Зовётся на первом кадре: к нему загружены все моды
void Discover() {
  gDiscovered = true;
  gPeers.clear();
  CModResults loaded = modloader_get_loaded();
  for (size_t i = 0; i < loaded.size; i++) {
    auto const& r = loaded.array[i];
    if (!r.handle || !r.info.id || strcmp(r.info.id, gHost.id) == 0) continue;
    int (*version)() = nullptr;
    if (!Sym(r.handle, "dustmenu_version", version)) continue;
    if (version() != kProtocol) {
      DM_LOG(ANDROID_LOG_WARN, "меню: у %s протокол %d, у нас %d — обнови моды, стик будет общий", r.info.id,
             version(), kProtocol);
      continue;
    }
    Peer p;
    p.id = r.info.id;
    if (Sym(r.handle, "dustmenu_title", p.title) && Sym(r.handle, "dustmenu_status", p.status) &&
        Sym(r.handle, "dustmenu_open", p.open) && Sym(r.handle, "dustmenu_close", p.close) &&
        Sym(r.handle, "dustmenu_is_open", p.isOpen) && Sym(r.handle, "dustmenu_show_list", p.showList))
      gPeers.push_back(p);
  }
  modloader_free_results(&loaded);
  Peer self;
  self.id = gHost.id;
  self.self = true;
  gPeers.push_back(self);
  std::sort(gPeers.begin(), gPeers.end(), [](Peer const& a, Peer const& b) { return PeerTitle(a) < PeerTitle(b); });
  gLeaderIndex = 0;
  for (size_t i = 0; i < gPeers.size(); i++)
    if (gPeers[i].id < gPeers[gLeaderIndex].id) gLeaderIndex = i;
  gLeader = gPeers[gLeaderIndex].self;
  std::string names;
  for (auto& p : gPeers) names += (names.empty() ? "" : ", ") + p.id;
  DM_LOG(ANDROID_LOG_INFO, "меню: моды с панелью Mods — %s; стик у %s", names.c_str(),
         gPeers[gLeaderIndex].id.c_str());
}

// ---------- панель ----------

class Panel {
 public:
  explicit Panel(char const* name) : name_(name) {}

  // Начать кадр панели шириной width и высотой height (м). false — рисовать нечего (нет камеры, трекинга)
  bool Begin(float width, float height) {
    if (!gReady || failed_) return false;
    if (!Alive(root_.obj) && !Build()) return false;

    auto camera = Call<Object*>(gGetMainCamera);
    if (!camera) return false;
    auto camT = Call<Object*>(gComponentTransform, camera);
    camPos_ = Call<Vec3>(gGetPosition, camT);
    Quat camRot = Call<Quat>(gGetRotation, camT);
    // Масштаб игрока: у больших тел (голем) вся система трекинга увеличена
    scale_ = gGetLossyScale ? Call<Vec3>(gGetLossyScale, camT).x : 1.f;
    if (!(scale_ > 0.01f)) scale_ = 1.f;

    // Позы — в пространстве трекинга; в мир через камеру: rig = camera * inverse(head)
    Vec3 headPos{}, handPos{};
    Quat headRot{0, 0, 0, 1}, handRot{0, 0, 0, 1};
    if (!Pose(kHead, gDevicePosition, gDeviceRotation, headPos, headRot) ||
        !Pose(kLeftHand, gDevicePosition, gDeviceRotation, handPos, handRot)) {
      Hide();
      return false;
    }
    Quat rigRot = camRot * Conj(headRot);
    Vec3 rigPos = camPos_ - Rotate(rigRot, headPos * scale_);

    // Панель над левой рукой смотрит на игрока и растёт вместе с ним
    width_ = width;
    height_ = height;
    Vec3 hand = rigPos + Rotate(rigRot, handPos * scale_);
    Vec3 center = hand + Vec3{0, (kPanelAboveHand + height / 2) * scale_, 0};
    rot_ = LookRotation(center - camPos_, {0, 1, 0});
    pos_ = center + Rotate(rot_, Vec3{0, height / 2, 0}) * scale_;
    Call<void>(gSetPosition, rootT_, pos_);
    Call<void>(gSetRotation, rootT_, rot_);
    if (std::fabs(scale_ - shownScale_) > 1e-3f) {
      shownScale_ = scale_;
      Vec3 s{scale_, scale_, scale_};
      Invoke(gSetLocalScale, rootT_, &s);
    }
    if (!visible_) {
      bool on = true;
      Invoke(gSetActive, root_.obj, &on);
    }

    // Курок: только что открытая панель не ловит курок, которым её открыли
    bool trigger = Pressed(kRightHand, gTriggerButton);
    if (!visible_) triggerWas_ = trigger;
    triggerDown_ = trigger && !triggerWas_;
    triggerWas_ = trigger;
    trigger_ = trigger;
    if (!trigger) pressed_ = -1;
    visible_ = true;

    // Луч из правого контроллера (pointer-поза, если есть, иначе поза рукоятки)
    onPanel_ = false;
    rayLength_ = 0;
    Vec3 rayPos;
    Quat rayRot;
    if (usePointer_ && !Pose(kRightHand, gPointerPosition, gPointerRotation, rayPos, rayRot)) {
      usePointer_ = false;
      DM_LOG(ANDROID_LOG_INFO, "меню: нет PointerPosition — луч от рукоятки");
    }
    if (usePointer_ || Pose(kRightHand, gDevicePosition, gDeviceRotation, rayPos, rayRot)) {
      rayOrigin_ = rigPos + Rotate(rigRot, rayPos * scale_);
      rayDir_ = Rotate(rigRot * rayRot, {0, 0, 1});
      rayLength_ = kRayIdleLength * scale_;
      Vec3 normal = Rotate(rot_, {0, 0, 1});
      float denom = Dot(rayDir_, normal);
      if (std::fabs(denom) > 1e-4f) {
        float t = Dot(pos_ - rayOrigin_, normal) / denom;
        Vec3 hit = rayOrigin_ + rayDir_ * t - pos_;
        float x = Dot(hit, Rotate(rot_, {1, 0, 0})) / scale_;
        float y = Dot(hit, Rotate(rot_, {0, 1, 0})) / scale_;
        if (t > 0.f && std::fabs(x) < width / 2 && y < 0.f && y > -height) {
          onPanel_ = true;
          cursor_ = {x, y};
          rayLength_ = t - kRayGap * scale_;
        }
      }
    }

    textUsed_ = rectUsed_ = buttons_ = 0;
    hover_ = -1;
    if (gHasBackground && !gMarkMeasured) Measure();
    if (Background()) {
      float x = width / 2, b = kBorder;
      Shape(kOrderPanelLine, kColPanelLine, -x - b, -height - b, x + b, b, kPanelRadius + b);
      Shape(kOrderPanel, kColPanel, -x, -height, x, 0, kPanelRadius);
    }
    return true;
  }

  // Надпись (TMP rich text). x — левый край (kLeft), правый (kRight) или середина (kCenter)
  void Text(float x, float y, std::string const& text, Align align = kCenter) {
    auto& s = Slot(texts_, textUsed_++, kOrderText);
    if (!s.text) return;
    auto pixel = PixelFont();
    auto font = pixel && IsAscii(text) ? pixel : GameFont();
    if (font && font != s.font) {
      s.font = font;
      Invoke(gSetFont, s.text, font);
    }
    int a = gSetSizeDelta ? align : kCenter;
    if (a != s.align) {
      s.align = a;
      Invoke(gSetAlignment, s.text, &a);
    }
    float px = a == kLeft ? x + kTextBox / 2 : a == kRight ? x - kTextBox / 2 : x;
    Place(s, {px, y, 0}, {1, 1, 1});
    SetText(s, text);
  }

  // Плитка от (cx ± w/2, cy ± h/2). true — по ней нажали курок. Потом: Hovered() — луч на ней,
  // Holding() — курок держат с нажатия на ней (для «+»/«−», которые работают, пока держишь)
  bool Button(float cx, float cy, float w, float h, Look look = kNormal) {
    int id = buttons_++;
    hovered_ = onPanel_ && std::fabs(cursor_.x - cx) < w / 2 && std::fabs(cursor_.y - cy) < h / 2;
    if (hovered_) hover_ = id;
    bool click = hovered_ && triggerDown_;
    if (click) {
      pressed_ = id;
      Buzz(kRightHand, 0.4f, 0.05f);
    }
    held_ = trigger_ && pressed_ == id && hovered_;
    if (Background()) {
      float x0 = cx - w / 2, y0 = cy - h / 2, x1 = cx + w / 2, y1 = cy + h / 2, b = kBorder;
      if (look == kSelected)
        Shape(kOrderTileLine, kColSelectedLine, x0 - b, y0 - b, x1 + b, y1 + b, kTileRadius + b);
      Shape(kOrderTile, look == kSelected ? kColSelected : hovered_ ? kColTileHover : kColTile, x0, y0, x1, y1,
            kTileRadius);
    }
    return click;
  }
  bool Hovered() const { return hovered_; }
  bool Holding() const { return held_; }

  // Заголовок: название слева; справа — кнопка «< Mods» (если есть другие моды с панелью) или подсказка.
  // true — нажали «< Mods» (мод зовёт dustmenu::Back())
  bool Header(std::string const& title);

  // Закончить кадр: спрятать лишнее, курсор и луч
  void End() {
    for (int i = textUsed_; i < static_cast<int>(texts_.size()); i++) SetText(texts_[i], "");
    if (Background()) {
      Vec2 c = cursor_;
      float r = kCursorRadius;
      Circle(kOrderCursorLine, onPanel_ ? kColCursorLine : nullptr, c.x, c.y, r + kCursorBorder);
      Circle(kOrderCursor, onPanel_ ? kColCursor : nullptr, c.x, c.y, r);
    } else {
      Text(cursor_.x, cursor_.y, onPanel_ ? "<color=#FFFFFF>o</color>" : "");
    }
    for (int i = rectUsed_; i < static_cast<int>(rects_.size()); i++) SetText(rects_[i], "");
    float start = kRayStart * scale_;
    DrawRay(rayOrigin_ + rayDir_ * start, rayDir_, rayLength_ - start);
    if (hover_ >= 0 && hover_ != lastHover_) Buzz(kRightHand, 0.1f, 0.01f);
    lastHover_ = hover_;
  }

  void Hide() {
    if (!visible_) return;
    visible_ = false;
    lastHover_ = -1;
    bool off = false;
    if (Alive(root_.obj)) Invoke(gSetActive, root_.obj, &off);
    DrawRay({}, {0, 0, 1}, 0);
  }

  bool Visible() const { return visible_; }

 private:
  struct TextSlot {
    Object* text = nullptr;  // TextMeshPro
    Object* t = nullptr;     // его Transform
    Object* font = nullptr;
    std::string shown;
    Vec3 pos{0, 0, 0}, scale{1, 1, 1};
    int order = 0;
    int align = kCenter;
  };

  bool Background() const { return gHasBackground && gMarkMeasured; }

  Object* NewText(Object* parent, int order) {
    auto go = NewGameObject("DustMenuText", parent);
    auto text = Keep(Invoke(gAddComponent, go, gTextMeshProType));
    if (!text) return nullptr;
    auto font = GameFont();
    if (font) Invoke(gSetFont, text, font);
    float size = kFontSize;
    int noWrap = 0;  // TextWrappingModes.NoWrap
    int center = kCenter;
    Invoke(gSetFontSize, text, &size);
    Invoke(gSetWrapping, text, &noWrap);
    Invoke(gSetAlignment, text, &center);
    if (gSetSizeDelta) {  // у 3D-надписи TMP рамка по умолчанию 20 × 5 м
      Vec2 box{kTextBox, 0.02f};
      Invoke(gSetSizeDelta, Call<Object*>(gGameObjectTransform, go), &box);
    }
    if (gSortingOrder) Invoke(gSortingOrder, text, &order);
    return text;
  }

  TextSlot& Slot(std::vector<TextSlot>& pool, int i, int order) {
    while (static_cast<int>(pool.size()) <= i) {
      TextSlot s;
      s.text = NewText(rootT_, order);
      if (!s.text) {
        failed_ = true;
        DM_LOG(ANDROID_LOG_ERROR, "меню: не удалось добавить TextMeshPro — меню не будет");
      } else {
        s.t = Keep(Call<Object*>(gComponentTransform, s.text));
        s.font = GameFont();
        s.order = order;
      }
      pool.push_back(s);
    }
    auto& s = pool[i];
    if (s.text && s.order != order && gSortingOrder) {
      s.order = order;
      Invoke(gSortingOrder, s.text, &order);
    }
    return s;
  }

  static bool Same(Vec3 a, Vec3 b) {
    return std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z) < 1e-6f;
  }

  void Place(TextSlot& s, Vec3 pos, Vec3 scale) {
    if (!Same(scale, s.scale)) {
      s.scale = scale;
      Invoke(gSetLocalScale, s.t, &scale);
    }
    if (!Same(pos, s.pos)) {
      s.pos = pos;
      Invoke(gSetLocalPosition, s.t, &pos);
    }
  }

  static void SetText(TextSlot& s, std::string const& text) {
    if (!s.text || s.shown == text) return;
    s.shown = text;
    Invoke(gSetText, s.text, api::string_new(text.c_str()));
  }

  // Прямоугольник цвета color (#RRGGBBAA без «#»; nullptr — спрятать) от (x0, y0) до (x1, y1)
  void Rect(int order, char const* color, float x0, float y0, float x1, float y1) {
    auto& s = Slot(rects_, rectUsed_++, order);
    if (!s.text) return;
    SetText(s, color ? MarkText(color) : "");
    if (!color) return;
    Vec3 scale{(x1 - x0) / gMarkSize.x, (y1 - y0) / gMarkSize.y, 1};
    Place(s, {(x0 + x1) / 2 - gMarkCenter.x * scale.x, (y0 + y1) / 2 - gMarkCenter.y * scale.y, 0}, scale);
  }

  // Скруглённая фигура: крест из двух прямоугольников и третий со срезом 0.29·r (точка дуги на 45°)
  void Shape(int order, char const* color, float x0, float y0, float x1, float y1, float r) {
    float c = r * 0.29f;
    Rect(order, color, x0, y0 + r, x1, y1 - r);
    Rect(order, color, x0 + r, y0, x1 - r, y1);
    Rect(order, color, x0 + c, y0 + c, x1 - c, y1 - c);
  }

  // Кружок: три прямоугольника, вписанных в окружность
  void Circle(int order, char const* color, float cx, float cy, float r) {
    float a = r * 0.92f, b = r * 0.40f, c = r * 0.71f;
    Rect(order, color, cx - a, cy - b, cx + a, cy + b);
    Rect(order, color, cx - b, cy - a, cx + b, cy + a);
    Rect(order, color, cx - c, cy - c, cx + c, cy + c);
  }

  // Подложка <mark> из одной точки — по границам меша надписи (TMP строит меш при отрисовке, поэтому не сразу)
  void Measure() {
    auto& s = Slot(rects_, 0, kOrderPanel);
    rectUsed_ = 1;
    if (!s.text) return;
    SetText(s, MarkText("00000000"));
    Bounds b = Call<Bounds>(gTextBounds, s.text);
    if (b.extents.x > 1e-5f && b.extents.y > 1e-5f) {
      gMarkMeasured = true;
      gMarkCenter = b.center;
      gMarkSize = b.extents * 2.f;
      rectUsed_ = 0;
      DM_LOG(ANDROID_LOG_INFO, "меню: подложка <mark> %.4f × %.4f", gMarkSize.x, gMarkSize.y);
    } else if (++gMeasureFrames > 300) {
      gHasBackground = false;
      DM_LOG(ANDROID_LOG_WARN, "меню: не удалось измерить подложку <mark> — меню без фона");
    }
  }

  // Линия от a по направлению dir длиной len (0 — спрятать): отрезки тоньше и прозрачнее к концу,
  // каждый повёрнут вдоль луча лицом к камере. Отдельный корень в мире (у панели свой поворот и масштаб)
  void DrawRay(Vec3 a, Vec3 dir, float len) {
    bool show = len > 0.02f * scale_ && Background() && Alive(rayRoot_.obj) && visible_;
    if (show != rayShown_ && Alive(rayRoot_.obj)) {
      rayShown_ = show;
      Invoke(gSetActive, rayRoot_.obj, &show);
    }
    if (!show) return;
    float step = len / kRaySegments;
    for (int i = 0; i < kRaySegments; i++) {
      auto& s = ray_[i];
      if (!s.text) return;
      Vec3 c = a + dir * (step * (i + 0.5f));
      Vec3 f = c - camPos_;
      f = Normalize(f - dir * Dot(f, dir));
      Quat rot = LookRotation(f, Cross(f, dir));
      float width = scale_ * (kRayWidthStart + (kRayWidthEnd - kRayWidthStart) * i / (kRaySegments - 1));
      Vec3 sc{step / gMarkSize.x, width / gMarkSize.y, 1};
      Vec3 p = c - Rotate(rot, Vec3{gMarkCenter.x * sc.x, gMarkCenter.y * sc.y, 0});
      Call<void>(gSetPosition, s.t, p);
      Call<void>(gSetRotation, s.t, rot);
      Invoke(gSetLocalScale, s.t, &sc);
    }
  }

  bool Build() {
    if (!GameFont()) return false;  // сцена ещё грузится — попробуем позже
    Free(root_);
    texts_.clear();
    rects_.clear();
    auto root = NewGameObject(name_, nullptr);
    root_ = Hold(root);
    Invoke(gDontDestroyOnLoad, nullptr, root);
    rootT_ = Keep(Call<Object*>(gGameObjectTransform, root));
    bool off = false;
    Invoke(gSetActive, root, &off);
    visible_ = false;
    shownScale_ = -1;

    Free(rayRoot_);
    std::string rayName = std::string(name_) + "Ray";
    auto ray = NewGameObject(rayName.c_str(), nullptr);
    rayRoot_ = Hold(ray);
    Invoke(gDontDestroyOnLoad, nullptr, ray);
    auto rayT = Call<Object*>(gGameObjectTransform, ray);
    for (int i = 0; i < kRaySegments; i++) {
      auto& s = ray_[i];
      s = {};
      s.text = NewText(rayT, kOrderRay);
      if (!s.text) break;
      s.t = Keep(Call<Object*>(gComponentTransform, s.text));
      char color[16];
      snprintf(color, sizeof color, "FFFFFF%02X", static_cast<int>(255 * kRayAlpha * (1.f - float(i) / kRaySegments)));
      SetText(s, MarkText(color));
    }
    Invoke(gSetActive, ray, &off);
    rayShown_ = false;
    DM_LOG(ANDROID_LOG_INFO, "меню: панель %s создана", name_);
    return true;
  }

  char const* name_;
  Held root_, rayRoot_;
  Object* rootT_ = nullptr;
  std::vector<TextSlot> texts_, rects_;
  TextSlot ray_[kRaySegments];
  int textUsed_ = 0, rectUsed_ = 0, buttons_ = 0;
  int hover_ = -1, lastHover_ = -1, pressed_ = -1;
  bool failed_ = false, visible_ = false, rayShown_ = false, usePointer_ = true;
  bool trigger_ = false, triggerWas_ = false, triggerDown_ = false;
  bool hovered_ = false, held_ = false, onPanel_ = false;
  float width_ = 0, height_ = 0, scale_ = 1, shownScale_ = -1, rayLength_ = 0;
  Vec3 pos_{}, camPos_{}, rayOrigin_{}, rayDir_{0, 0, 1};
  Quat rot_{0, 0, 0, 1};
  Vec2 cursor_{0, 0};
};

// ---------- для мода ----------

// Строка плиток номер row (0 — первая): центр по y
inline float RowY(int row) { return kContentTop - kRowHeight / 2 - row * (kRowHeight + kRowGap); }
// Высота панели, у которой rows строк плиток (и extra метров под ними)
inline float PanelHeight(int rows, float extra = 0) {
  return -kContentTop + rows * (kRowHeight + kRowGap) - kRowGap + extra + kBottomPad;
}

inline bool HasPeers() { return gPeers.size() > 1; }
inline bool IsOpen() { return gOpen; }
inline void SetOpen(bool open) { gOpen = open; }

// Другие моды показывают своё меню (или список «Mods»)
inline bool OthersOpen() {
  if (gListVisible) return true;
  for (auto const& p : gPeers)
    if (!p.self && p.isOpen()) return true;
  return false;
}
// Какое-нибудь меню на руке (своё, чужое, список) — курок занят им
inline bool AnyOpen() { return gOpen || OthersOpen(); }

// «< Mods»: закрыть своё меню и показать список
inline void Back() {
  gOpen = false;
  if (!gDiscovered || gPeers.empty()) return;
  auto const& leader = gPeers[gLeaderIndex];
  if (leader.self) gListVisible = true;
  else leader.showList();
}

inline bool Panel::Header(std::string const& title) {
  float x = width_ / 2 - kPad;
  Text(-x, kHeaderY, "<color=#FFFFFF>" + title + "</color>", kLeft);
  if (!HasPeers()) {
    Text(x, kHeaderY, "<color=#8C8F94><size=70%>aim + trigger</size></color>", kRight);
    return false;
  }
  constexpr float w = 0.044f, h = 0.018f;
  bool back = Button(x - w / 2, kHeaderY, w, h);
  Text(x - w / 2, kHeaderY, std::string(Hovered() ? "<color=#FFFFFF>" : "<color=#C8CACD>") +
                                "<size=80%><noparse><</noparse> Mods</size></color>");
  return back;
}

Panel gList("DustModsMenu");

void DrawList() {
  if (!gListVisible) {
    gList.Hide();
    return;
  }
  constexpr float kWidth = 0.21f;
  int rows = static_cast<int>(gPeers.size());
  if (!gList.Begin(kWidth, PanelHeight(rows))) return;
  float x = kWidth / 2 - kPad;
  gList.Text(-x, kHeaderY, "<color=#FFFFFF>Mods</color>", kLeft);
  gList.Text(x, kHeaderY, "<color=#8C8F94><size=70%>aim + trigger</size></color>", kRight);
  int open = -1;
  for (int i = 0; i < rows; i++) {
    auto const& p = gPeers[i];
    float y = RowY(i);
    if (gList.Button(0, y, kWidth - 2 * kPad, kRowHeight)) open = i;
    std::string color = gList.Hovered() ? "<color=#FFFFFF>" : "<color=#E6E7E9>";
    gList.Text(-x + 0.008f, y, color + PeerTitle(p) + "</color>", kLeft);
    std::string status = PeerStatus(p);
    if (!status.empty()) gList.Text(x - 0.008f, y, "<color=#8C8F94><size=80%>" + status + "</size></color>", kRight);
  }
  gList.End();
  if (open >= 0) {
    gListVisible = false;
    gList.Hide();
    PeerSetOpen(gPeers[open], true);
    DM_LOG(ANDROID_LOG_INFO, "меню: открыт %s", gPeers[open].id.c_str());
  }
}

// Каждый кадр, один раз, до своего меню
inline void Tick() {
  if (!gReady) return;
  gFrame++;
  if (!gDiscovered) Discover();
  if (!gLeader) return;

  // Левый стик — только у главного
  bool stick = Pressed(kLeftHand, gStickClick);
  bool press = stick && !gStickWas;
  gStickWas = stick;
  if (press) {
    Buzz(kLeftHand, 0.3f, 0.05f);
    if (gListVisible) {
      gListVisible = false;
    } else {
      bool closed = false;
      for (auto const& p : gPeers)
        if (PeerOpen(p)) {
          PeerSetOpen(p, false);
          closed = true;
        }
      if (gPeers.size() <= 1) gOpen = !closed;  // мод один — стик открывает и закрывает его меню
      else gListVisible = true;                 // из меню мода — назад к списку
    }
  }
  DrawList();
}

template <typename T>
bool Resolve(void* lib, T& fn, char const* name) {
  fn = reinterpret_cast<T>(dlsym(lib, name));
  if (!fn) DM_LOG(ANDROID_LOG_ERROR, "меню: il2cpp: нет %s", name);
  return fn != nullptr;
}

Image const* FindImage(char const* name) {
  size_t count = 0;
  auto assemblies = api::domain_get_assemblies(api::domain_get(), &count);
  for (size_t i = 0; i < count; i++) {
    auto image = api::assembly_get_image(assemblies[i]);
    if (image && strcmp(api::image_get_name(image), name) == 0) return image;
  }
  return nullptr;
}

Klass* FindClass(Image const* image, char const* ns, char const* name) {
  auto klass = image ? api::class_from_name(image, ns, name) : nullptr;
  if (!klass) DM_LOG(ANDROID_LOG_WARN, "меню: нет класса %s.%s", ns, name);
  return klass;
}

Method const* FindMethod(Klass* klass, char const* name, int params) {
  auto m = klass ? api::class_get_method_from_name(klass, name, params) : nullptr;
  if (!m) DM_LOG(ANDROID_LOG_WARN, "меню: нет метода %s/%d", name, params);
  return m;
}

// Негенерик-перегрузка с первым параметром класса first (nullptr — любой) и вторым second (nullptr — любой)
Method const* FindOverload(Klass* klass, char const* name, uint32_t params, char const* first,
                           char const* second = nullptr) {
  void* iter = nullptr;
  while (klass) {
    auto m = api::class_get_methods(klass, &iter);
    if (!m) break;
    if (strcmp(api::method_get_name(m), name) != 0 || api::method_get_param_count(m) != params ||
        api::method_is_generic(m))
      continue;
    auto p0 = params > 0 ? api::class_from_type(api::method_get_param(m, 0)) : nullptr;
    auto p1 = params > 1 ? api::class_from_type(api::method_get_param(m, 1)) : nullptr;
    if (first && (!p0 || strcmp(api::class_get_name(p0), first) != 0)) continue;
    if (second && (!p1 || strcmp(api::class_get_name(p1), second) != 0)) continue;
    return m;
  }
  return nullptr;
}

Object* TypeOf(Klass* klass) { return Keep(api::type_get_object(api::class_get_type(klass))); }
String* KeepString(char const* s) { return Keep(api::string_new(s)); }

// После il2cpp_init (в late_load). false — меню не будет (мод работает без него)
inline bool Init(Host const& host) {
  gHost = host;
  gTag = host.tag;
  gDataDir = host.dataDir;
  void* lib = modloader_libil2cpp_handle;
  if (!lib) return false;
  if (!(Resolve(lib, api::domain_get, "il2cpp_domain_get") &&
        Resolve(lib, api::domain_get_assemblies, "il2cpp_domain_get_assemblies") &&
        Resolve(lib, api::assembly_get_image, "il2cpp_assembly_get_image") &&
        Resolve(lib, api::image_get_name, "il2cpp_image_get_name") &&
        Resolve(lib, api::class_from_name, "il2cpp_class_from_name") &&
        Resolve(lib, api::class_get_method_from_name, "il2cpp_class_get_method_from_name") &&
        Resolve(lib, api::class_get_methods, "il2cpp_class_get_methods") &&
        Resolve(lib, api::class_get_name, "il2cpp_class_get_name") &&
        Resolve(lib, api::class_get_type, "il2cpp_class_get_type") &&
        Resolve(lib, api::method_get_name, "il2cpp_method_get_name") &&
        Resolve(lib, api::method_get_param_count, "il2cpp_method_get_param_count") &&
        Resolve(lib, api::method_get_param, "il2cpp_method_get_param") &&
        Resolve(lib, api::method_is_generic, "il2cpp_method_is_generic") &&
        Resolve(lib, api::class_from_type, "il2cpp_class_from_type") &&
        Resolve(lib, api::type_get_object, "il2cpp_type_get_object") &&
        Resolve(lib, api::class_get_field_from_name, "il2cpp_class_get_field_from_name") &&
        Resolve(lib, api::field_get_offset, "il2cpp_field_get_offset") &&
        Resolve(lib, api::object_new, "il2cpp_object_new") &&
        Resolve(lib, api::runtime_invoke, "il2cpp_runtime_invoke") &&
        Resolve(lib, api::string_new, "il2cpp_string_new") &&
        Resolve(lib, api::string_chars, "il2cpp_string_chars") &&
        Resolve(lib, api::string_length, "il2cpp_string_length") &&
        Resolve(lib, api::gchandle_new, "il2cpp_gchandle_new") &&
        Resolve(lib, api::gchandle_free, "il2cpp_gchandle_free") &&
        Resolve(lib, api::gchandle_get_target, "il2cpp_gchandle_get_target") &&
        Resolve(lib, api::array_length, "il2cpp_array_length") &&
        Resolve(lib, api::resolve_icall, "il2cpp_resolve_icall")))
    return false;

  auto core = FindImage("UnityEngine.CoreModule.dll");
  auto xr = FindImage("UnityEngine.XRModule.dll");
  auto tmp = FindImage("Unity.TextMeshPro.dll");
  auto bundles = FindImage("UnityEngine.AssetBundleModule.dll");
  if (!core || !xr || !tmp) {
    DM_LOG(ANDROID_LOG_ERROR, "меню: нет сборок CoreModule/XRModule/TextMeshPro");
    return false;
  }
  auto object = FindClass(core, "UnityEngine", "Object");
  auto camera = FindClass(core, "UnityEngine", "Camera");
  auto component = FindClass(core, "UnityEngine", "Component");
  auto transform = FindClass(core, "UnityEngine", "Transform");
  auto resources = FindClass(core, "UnityEngine", "Resources");
  auto material = FindClass(core, "UnityEngine", "Material");
  auto rectTransform = FindClass(core, "UnityEngine", "RectTransform");
  gGameObjectClass = FindClass(core, "UnityEngine", "GameObject");
  auto tracking = FindClass(xr, "UnityEngine.XR", "InputTracking");
  auto devices = FindClass(xr, "UnityEngine.XR", "InputDevices");
  auto tmpText = FindClass(tmp, "TMPro", "TMP_Text");
  auto textMeshPro = FindClass(tmp, "TMPro", "TextMeshPro");
  auto fontAsset = FindClass(tmp, "TMPro", "TMP_FontAsset");
  auto tmpAsset = api::class_from_name(tmp, "TMPro", "TMP_Asset");
  auto tmpSettings = api::class_from_name(tmp, "TMPro", "TMP_Settings");
  auto assetBundle = bundles ? api::class_from_name(bundles, "UnityEngine", "AssetBundle") : nullptr;
  if (!object || !camera || !component || !transform || !resources || !gGameObjectClass || !tracking || !devices ||
      !tmpText || !textMeshPro || !fontAsset)
    return false;

  gGetMainCamera = FindMethod(camera, "get_main", 0);
  gComponentTransform = FindMethod(component, "get_transform", 0);
  gGetPosition = FindMethod(transform, "get_position", 0);
  gGetRotation = FindMethod(transform, "get_rotation", 0);
  gSetPosition = FindMethod(transform, "set_position", 1);
  gSetRotation = FindMethod(transform, "set_rotation", 1);
  gSetLocalPosition = FindMethod(transform, "set_localPosition", 1);
  gSetLocalScale = FindMethod(transform, "set_localScale", 1);
  gGetLossyScale = FindMethod(transform, "get_lossyScale", 0);
  gSetParent = FindOverload(transform, "SetParent", 2, "Transform");
  gGameObjectCtor = FindOverload(gGameObjectClass, ".ctor", 1, "String");
  gGameObjectTransform = FindMethod(gGameObjectClass, "get_transform", 0);
  gAddComponent = FindOverload(gGameObjectClass, "AddComponent", 1, "Type");
  gSetActive = FindMethod(gGameObjectClass, "SetActive", 1);
  gDontDestroyOnLoad = FindMethod(object, "DontDestroyOnLoad", 1);
  gObjectName = FindMethod(object, "get_name", 0);
  gFindAllOfType = FindOverload(resources, "FindObjectsOfTypeAll", 1, "Type");
  gGetDeviceId = FindMethod(tracking, "GetDeviceIdAtXRNode", 1);
  gGetButton = FindMethod(devices, "TryGetFeatureValue_bool", 3);
  gGetVec3 = FindMethod(devices, "TryGetFeatureValue_Vector3f", 3);
  gGetQuat = FindMethod(devices, "TryGetFeatureValue_Quaternionf", 3);
  gHaptic = FindMethod(devices, "SendHapticImpulse", 4);
  gSetText = FindMethod(tmpText, "set_text", 1);
  gSetFontSize = FindMethod(tmpText, "set_fontSize", 1);
  gSetAlignment = FindMethod(tmpText, "set_alignment", 1);
  gSetWrapping = FindMethod(tmpText, "set_textWrappingMode", 1);
  gSetFont = FindMethod(tmpText, "set_font", 1);
  gTextBounds = FindMethod(tmpText, "get_bounds", 0);
  gSortingOrder = FindMethod(textMeshPro, "set_sortingOrder", 1);
  gDefaultFont = tmpSettings ? api::class_get_method_from_name(tmpSettings, "get_defaultFontAsset", 0) : nullptr;
  gSetSizeDelta = rectTransform ? FindMethod(rectTransform, "set_sizeDelta", 1) : nullptr;
  gFontMaterial = tmpAsset ? FindMethod(tmpAsset, "get_material", 0) : nullptr;
  gGetShader = material ? FindMethod(material, "get_shader", 0) : nullptr;
  gSetShader = material ? FindMethod(material, "set_shader", 1) : nullptr;
  for (auto m : {gGetMainCamera, gComponentTransform, gGetPosition, gGetRotation, gSetPosition, gSetRotation,
                 gSetLocalPosition, gSetLocalScale, gSetParent, gGameObjectCtor, gGameObjectTransform, gAddComponent,
                 gSetActive, gDontDestroyOnLoad, gObjectName, gFindAllOfType, gGetDeviceId, gGetButton, gGetVec3,
                 gGetQuat, gSetText, gSetFontSize, gSetAlignment, gSetWrapping, gSetFont})
    if (!m) {
      DM_LOG(ANDROID_LOG_ERROR, "меню: не найдено что-то из Unity API — меню не будет");
      return false;
    }
  // Бандл шрифта: методы, если не вырезаны, иначе внутренние вызовы движка
  if (assetBundle) {
    gLoadBundle = FindOverload(assetBundle, "LoadFromFile", 1, "String");
    gLoadAsset = FindOverload(assetBundle, "LoadAsset", 2, "String", "Type");
    if (!gLoadAsset) gLoadAsset = FindOverload(assetBundle, "LoadAsset_Internal", 2, "String", "Type");
  }
  if (!gLoadBundle)
    gLoadBundleIcall = reinterpret_cast<decltype(gLoadBundleIcall)>(
        api::resolve_icall("UnityEngine.AssetBundle::LoadFromFile_Internal_Injected"));
  if (!gLoadAsset)
    gLoadAssetIcall = reinterpret_cast<decltype(gLoadAssetIcall)>(
        api::resolve_icall("UnityEngine.AssetBundle::LoadAsset_Internal_Injected"));

  auto cachedPtr = api::class_get_field_from_name(object, "m_CachedPtr");
  if (!cachedPtr) return false;
  gCachedPtrOffset = api::field_get_offset(cachedPtr);
  gTextMeshProType = TypeOf(textMeshPro);
  gFontAssetType = TypeOf(fontAsset);
  gTriggerButton = KeepString("TriggerButton");
  gStickClick = KeepString("Primary2DAxisClick");
  gDevicePosition = KeepString("DevicePosition");
  gDeviceRotation = KeepString("DeviceRotation");
  gPointerPosition = KeepString("PointerPosition");
  gPointerRotation = KeepString("PointerRotation");
  gHasBackground = gTextBounds && gSortingOrder;
  if (!gHasBackground) DM_LOG(ANDROID_LOG_WARN, "меню: нет TMP_Text.bounds или sortingOrder — меню без фона");
  gReady = true;
  return true;
}

}  // namespace dustmenu
}  // namespace

// ---------- экспорт для других модов (протокол kProtocol) ----------
// Свои функции мод через эти не зовёт: при одинаковых именах в разных .so вызов мог бы уйти в чужой мод

#define DUSTMENU_EXPORT extern "C" __attribute__((visibility("default")))

DUSTMENU_EXPORT int dustmenu_version() { return dustmenu::kProtocol; }
DUSTMENU_EXPORT char const* dustmenu_title() { return dustmenu::gHost.title; }
DUSTMENU_EXPORT char const* dustmenu_status() {
  static std::string status;
  status = dustmenu::gHost.status ? dustmenu::gHost.status() : "";
  return status.c_str();
}
DUSTMENU_EXPORT void dustmenu_open() { dustmenu::gOpen = true; }
DUSTMENU_EXPORT void dustmenu_close() { dustmenu::gOpen = false; }
DUSTMENU_EXPORT bool dustmenu_is_open() { return dustmenu::gOpen || dustmenu::gListVisible; }
DUSTMENU_EXPORT void dustmenu_show_list() {
  if (dustmenu::gLeader) dustmenu::gListVisible = true;
}
