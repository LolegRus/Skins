// Skins для автономного Quest (Scotland2).
// Тот же смысл, что у ПК-версии: меняет меши персонажа на меши скина из AssetBundle. Бандлы — свои,
// собранные под Android (Unity-проект Skins\Unity, Tools → Skins → Build All), лежат в kSkinDir.
// Сборщик уже подогнал меши под оси и масштаб узлов игры, поэтому модель скина — плоский список
// детей корня (как и на ПК):
//   «путь узла» с MeshFilter — заменить MeshFilter.sharedMesh на этом узле (рендерер, материал,
//                              аниматоры и коллайдеры остаются игры);
//   «путь узла» без MeshFilter — спрятать меш на этом узле (например, «Witch Hat»);
//   «+путь узла» — новые детали: дети вешаются на этот узел, материал повторяют за ближайшим рендерером.
// Путь — от корня класса, без его имени: «Anchors/Head Anchor/Head».
//
// Меню — нажатие левого стика (как у TimeControl — вместе их не ставить): панель над левой рукой,
// у каждого VR-класса со скинами «< скин >» (Scarab и Drone — только ПК, их в меню нет),
// наведи правый контроллер и жми курок. Выбор пишется в
// kSkinDir/skins.cfg («KOBOLD=Cowboy», пусто — родной вид); без строки — первый скин класса.
// Свой класс — CombatClass, у которого в имени объекта «Local» («Kobold (Local) Variant(Clone)»,
// «Grappler Local Variant(Clone)»), ключ класса — CombatClass.classType, как на ПК.
// Скины других игроков с модом — RPC-пакетами, как на ПК (см. «сеть»).
// Вместо OnUpdate — хук на RenderPipelineManager.DoRenderLoop_Internal.

#include <android/log.h>
#include <dirent.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "flamingo-capi.h"
#include "modloader.h"

#define LOG(prio, ...) __android_log_print(prio, "Skins", __VA_ARGS__)

namespace {

struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct Il2CppType;
struct FieldInfo;
struct MethodInfo;
struct Il2CppObject;
struct Il2CppString;
struct Il2CppArray;

Il2CppDomain* (*il2cpp_domain_get)();
Il2CppAssembly const** (*il2cpp_domain_get_assemblies)(Il2CppDomain const*, size_t*);
Il2CppImage const* (*il2cpp_assembly_get_image)(Il2CppAssembly const*);
char const* (*il2cpp_image_get_name)(Il2CppImage const*);
Il2CppClass* (*il2cpp_class_from_name)(Il2CppImage const*, char const*, char const*);
MethodInfo const* (*il2cpp_class_get_method_from_name)(Il2CppClass*, char const*, int);
MethodInfo const* (*il2cpp_class_get_methods)(Il2CppClass*, void**);
char const* (*il2cpp_method_get_name)(MethodInfo const*);
uint32_t (*il2cpp_method_get_param_count)(MethodInfo const*);
Il2CppType const* (*il2cpp_method_get_param)(MethodInfo const*, uint32_t);
bool (*il2cpp_method_is_generic)(MethodInfo const*);
Il2CppClass* (*il2cpp_class_from_type)(Il2CppType const*);
char const* (*il2cpp_class_get_name)(Il2CppClass*);
Il2CppType const* (*il2cpp_class_get_type)(Il2CppClass*);
Il2CppObject* (*il2cpp_type_get_object)(Il2CppType const*);
FieldInfo* (*il2cpp_class_get_field_from_name)(Il2CppClass*, char const*);
size_t (*il2cpp_field_get_offset)(FieldInfo*);
Il2CppObject* (*il2cpp_object_new)(Il2CppClass*);
Il2CppObject* (*il2cpp_runtime_invoke)(MethodInfo const*, void*, void**, Il2CppObject**);
Il2CppString* (*il2cpp_string_new)(char const*);
char16_t* (*il2cpp_string_chars)(Il2CppString*);
int32_t (*il2cpp_string_length)(Il2CppString*);
// В Unity 6 GC handle — указатель (Il2CppGCHandle), а не uint32_t
void* (*il2cpp_gchandle_new)(Il2CppObject*, bool);
void (*il2cpp_gchandle_free)(void*);
Il2CppObject* (*il2cpp_gchandle_get_target)(void*);
uint32_t (*il2cpp_array_length)(Il2CppArray*);
void* (*il2cpp_resolve_icall)(char const*);

constexpr char kModVersion[] = "1.0.0";  // как "version" в mod.json; её же показывает терминал в хабе
constexpr char kSkinDir[] = "/sdcard/ModData/org.VRDojo.Dust/Mods/Skins/";
constexpr char kExtension[] = ".skin";
constexpr char kConfig[] = "skins.cfg";
constexpr char kLocalMark[] = "Local";  // «Kobold (Local) Variant», «Grappler Local Variant»
constexpr int kScanFrames = 30;  // свой класс ищем раз в столько кадров

// ---------- вызовы il2cpp ----------

// Прямой вызов: MethodInfo начинается с methodPointer, последним аргументом идёт сам MethodInfo
// (у методов экземпляра первым — this). Только для объектов, которые точно живы: исключение
// движка (уничтоженный объект) через наш C++ не пройдёт.
template <typename R, typename... Args>
R Call(MethodInfo const* m, Args... args) {
  auto fn = *reinterpret_cast<R (* const*)(Args..., MethodInfo const*)>(m);
  return fn(args..., m);
}

// Через il2cpp_runtime_invoke — исключение игры не роняет её, а пишется в лог.
// Аргументы-значения передаются указателями
template <typename... Args>
Il2CppObject* Invoke(MethodInfo const* m, void* self, Args*... args) {
  void* argv[] = {const_cast<void*>(static_cast<void const*>(args))..., nullptr};
  Il2CppObject* exc = nullptr;
  auto result = il2cpp_runtime_invoke(m, self, argv, &exc);
  if (exc) {
    LOG(ANDROID_LOG_WARN, "исключение в %s", il2cpp_method_get_name(m));
    return nullptr;
  }
  return result;
}

// Значение из упакованного результата runtime_invoke (данные идут после klass и monitor)
template <typename T>
T Unbox(Il2CppObject* obj) {
  return obj ? *reinterpret_cast<T*>(reinterpret_cast<char*>(obj) + 2 * sizeof(void*)) : T{};
}

// GC il2cpp не видит нашу память, поэтому объекты, которые храним, держим через GC handle.
// Обёртки Unity-объектов тоже: без ссылок из C# их соберут, и указатель повиснет
struct Held {
  Il2CppObject* obj = nullptr;
  void* handle = nullptr;
};
Held Hold(Il2CppObject* obj) { return {obj, obj ? il2cpp_gchandle_new(obj, false) : nullptr}; }
void Free(Held& h) {
  if (h.handle) il2cpp_gchandle_free(h.handle);
  h = {};
}

template <typename T>
T* Keep(T* obj) {
  if (obj) il2cpp_gchandle_new(reinterpret_cast<Il2CppObject*>(obj), false);
  return obj;
}

// Куда NewGameObject / NewText кладут handle новых объектов: nullptr — навсегда (Keep), иначе — в список,
// который освободят вместе с этими объектами (детали терминала и голограммы в хабе строятся заново при каждом заходе)
std::vector<Held>* gKeepInto;
Il2CppObject* KeepNew(Il2CppObject* obj) {
  if (!gKeepInto) return Keep(obj);
  if (obj) gKeepInto->push_back(Hold(obj));
  return obj;
}
struct KeepInto {  // на время блока новые объекты держит pool
  std::vector<Held>* was;
  explicit KeepInto(std::vector<Held>& pool) : was(gKeepInto) { gKeepInto = &pool; }
  ~KeepInto() { gKeepInto = was; }
};

Il2CppObject* TypeOf(Il2CppClass* klass) { return Keep(il2cpp_type_get_object(il2cpp_class_get_type(klass))); }

uint32_t Length(Il2CppArray* a) { return a ? il2cpp_array_length(a) : 0; }
// Данные Il2CppArray идут после klass, monitor, bounds и max_length
Il2CppObject* At(Il2CppArray* a, uint32_t i) {
  return reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(a) + 4 * sizeof(void*))[i];
}

std::string ToUtf8(Il2CppString* s) {
  std::string out;
  if (!s) return out;
  auto chars = il2cpp_string_chars(s);
  for (int32_t i = 0, n = il2cpp_string_length(s); i < n; i++) {
    char16_t c = chars[i];
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

// Строка для внутренних вызовов движка: в Unity 6 они берут ManagedSpanWrapper с UTF-16
struct Span {
  void const* ptr;
  int32_t length;
};
std::u16string ToUtf16(std::string const& s) {
  std::u16string out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    if (c < 0x80) {
      out += char16_t(c);
      i += 1;
    } else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
      out += char16_t(((c & 0x1F) << 6) | (s[i + 1] & 0x3F));
      i += 2;
    } else if ((c >> 4) == 0xE && i + 2 < s.size()) {
      out += char16_t(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F));
      i += 3;
    } else {
      out += u'?';
      i += 1;
    }
  }
  return out;
}

template <typename T>
bool Resolve(void* lib, T& fn, char const* name) {
  fn = reinterpret_cast<T>(dlsym(lib, name));
  if (!fn) LOG(ANDROID_LOG_ERROR, "il2cpp: нет %s", name);
  return fn != nullptr;
}

template <typename T>
bool Icall(T& fn, char const* name) {
  fn = reinterpret_cast<T>(il2cpp_resolve_icall(name));
  if (!fn) LOG(ANDROID_LOG_ERROR, "нет внутреннего вызова %s", name);
  return fn != nullptr;
}

Il2CppImage const* FindImage(char const* name) {
  size_t count = 0;
  auto assemblies = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &count);
  for (size_t i = 0; i < count; i++) {
    auto image = il2cpp_assembly_get_image(assemblies[i]);
    if (image && strcmp(il2cpp_image_get_name(image), name) == 0) return image;
  }
  return nullptr;
}

Il2CppClass* Class(Il2CppImage const* image, char const* ns, char const* name) {
  auto klass = image ? il2cpp_class_from_name(image, ns, name) : nullptr;
  if (!klass) LOG(ANDROID_LOG_ERROR, "нет класса %s.%s", ns, name);
  return klass;
}

// il2cpp_class_get_method_from_name, который пишет в лог, чего не нашлось (log = false — молча)
MethodInfo const* Method(Il2CppClass* klass, char const* name, int params, bool log = true) {
  auto m = klass ? il2cpp_class_get_method_from_name(klass, name, params) : nullptr;
  if (!m && log) LOG(ANDROID_LOG_ERROR, "нет метода %s.%s/%d", klass ? il2cpp_class_get_name(klass) : "?", name, params);
  return m;
}

char const* ParamClass(MethodInfo const* m, uint32_t i) {
  auto klass = il2cpp_class_from_type(il2cpp_method_get_param(m, i));
  return klass ? il2cpp_class_get_name(klass) : "";
}

// Негенерик-перегрузка с заданными классами первых параметров (second = nullptr — не проверять)
MethodInfo const* FindOverload(Il2CppClass* klass, char const* name, uint32_t params, char const* first,
                               char const* second = nullptr, bool log = true) {
  void* iter = nullptr;
  while (auto m = klass ? il2cpp_class_get_methods(klass, &iter) : nullptr) {
    if (strcmp(il2cpp_method_get_name(m), name) != 0 || il2cpp_method_get_param_count(m) != params ||
        il2cpp_method_is_generic(m))
      continue;
    if (strcmp(ParamClass(m, 0), first) != 0) continue;
    if (second && strcmp(ParamClass(m, 1), second) != 0) continue;
    return m;
  }
  if (log)
    LOG(ANDROID_LOG_ERROR, "нет метода %s.%s(%s%s%s, ...)", klass ? il2cpp_class_get_name(klass) : "?", name, first,
        second ? ", " : "", second ? second : "");
  return nullptr;
}

// ---------- математика (Unity: левая система координат) ----------

struct Vec3 {
  float x, y, z;
};
struct Quat {
  float x, y, z, w;
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 Normalize(Vec3 v) {
  float len = std::sqrt(Dot(v, v));
  return len > 1e-6f ? v * (1.f / len) : Vec3{0, 0, 1};
}
Quat operator*(Quat a, Quat b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Quat Conj(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
Vec3 Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
Quat Nlerp(Quat a, Quat b, float t) {
  if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.f) b = {-b.x, -b.y, -b.z, -b.w};
  Quat q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
  float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return len > 1e-6f ? Quat{q.x / len, q.y / len, q.z / len, q.w / len} : a;
}
Vec3 Rotate(Quat q, Vec3 v) {
  Vec3 u{q.x, q.y, q.z};
  Vec3 t = Cross(u, v) * 2.f;
  return v + t * q.w + Cross(u, t);
}
Quat LookRotation(Vec3 f, Vec3 up) {
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

// ---------- Unity ----------

MethodInfo const* gGetFrameCount;
MethodInfo const* gObjectName;
MethodInfo const* gSetObjectName;
MethodInfo const* gDestroy;
MethodInfo const* gFindObjects;
MethodInfo const* gInstantiate;       // Object.Instantiate(Object, Transform, bool)
MethodInfo const* gInstantiatePlain;  // Object.Instantiate(Object) + SetParent — если первого нет
MethodInfo const* gSetParent;
MethodInfo const* gComponentGameObject;
MethodInfo const* gComponentTransform;
MethodInfo const* gGameObjectTransform;
MethodInfo const* gGetComponent;
MethodInfo const* gAddComponent;
MethodInfo const* gGetLayer;
MethodInfo const* gSetLayer;
MethodInfo const* gSetActive;
MethodInfo const* gGameObjectCtor;
MethodInfo const* gDontDestroyOnLoad;
MethodInfo const* gFindAllOfType;
MethodInfo const* gChildCount;
MethodInfo const* gGetChild;
MethodInfo const* gGetParent;
MethodInfo const* gGetPosition;
MethodInfo const* gGetRotation;
MethodInfo const* gSetPosition;
MethodInfo const* gSetRotation;
MethodInfo const* gSetLocalPosition;
MethodInfo const* gSetLocalScale;
MethodInfo const* gGetLossyScale;
// Детали скина, которые ездят вместе с анимацией игры (ребёнок «@end»): поза узла и время анимации
MethodInfo const* gGetLocalPosition;
MethodInfo const* gGetLocalRotation;
MethodInfo const* gSetLocalRotation;
MethodInfo const* gGetLocalScale;
Il2CppObject* gScalarAnimType;  // Dust.ScalarProceduralAnimation
size_t gCurrentTimeOffset;      // его поле currentTime (0…1)
// Брошенные снаряды класса (колесо Crossbolt) лежат не внутри него: находим по ProjectileTeleporter.teleportProjectile
Il2CppObject* gTeleporterType;
size_t gTeleportProjectileOffset;
MethodInfo const* gGetMainCamera;
MethodInfo const* gGetSharedMesh;
MethodInfo const* gSetSharedMesh;
MethodInfo const* gGetSharedMaterial;
MethodInfo const* gSetSharedMaterial;
MethodInfo const* gLoadBundle;  // AssetBundle.LoadFromFile(string), если не вырезан
void* (*gLoadBundleIcall)(Span*, uint32_t, uint64_t);
MethodInfo const* gLoadAsset;   // AssetBundle.LoadAsset(string, Type) или LoadAsset_Internal, если не вырезаны
void* (*gLoadAssetIcall)(void*, Span*, Il2CppObject*);
size_t gCachedPtrOffset;

// Сеть: RPC-пакеты со своего CombatClass и хук на NetworkComponent.OnReceive (как на ПК)
MethodInfo const* gHasId;
MethodInfo const* gIsOwned;
MethodInfo const* gGetClient;
MethodInfo const* gOfflineMode;  // Matchmaker.offlineMode — статическое; геттер в сборке для Quest вырезан,
FieldInfo* gOfflineField;        // тогда читаем само поле
void (*il2cpp_field_static_get_value)(FieldInfo*, void*);

// XR и TextMeshPro — для меню
MethodInfo const* gGetDeviceId;
MethodInfo const* gGetButton;
MethodInfo const* gGetVec3;
MethodInfo const* gGetQuat;
MethodInfo const* gHaptic;
MethodInfo const* gSetText;
MethodInfo const* gSetFontSize;
MethodInfo const* gSetAlignment;
MethodInfo const* gSetWrapping;
MethodInfo const* gSetFont;
MethodInfo const* gDefaultFont;

Il2CppClass* gGameObjectClass;
Il2CppObject* gMeshFilterType;
Il2CppObject* gRendererType;
Il2CppObject* gGameObjectType;
Il2CppObject* gCombatClassType;
Il2CppObject* gNetworkPrefabType;
Il2CppObject* gNetworkManagerType;
Il2CppObject* gMatchmakerType;
Il2CppObject* gTextMeshProType;
Il2CppObject* gFontAssetType;
Il2CppString* gTriggerButton;
Il2CppString* gStickClick;
Il2CppString* gDevicePosition;
Il2CppString* gDeviceRotation;
Il2CppString* gPointerPosition;
Il2CppString* gPointerRotation;

bool Alive(Il2CppObject* obj) {
  return obj && *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + gCachedPtrOffset);
}

void* NativePtr(Il2CppObject* obj) { return *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + gCachedPtrOffset); }

std::string NameOf(Il2CppObject* obj) {
  return Alive(obj) ? ToUtf8(reinterpret_cast<Il2CppString*>(Invoke(gObjectName, obj))) : "<null>";
}

Il2CppObject* TransformOf(Il2CppObject* gameObject) { return Call<Il2CppObject*>(gGameObjectTransform, gameObject); }
Il2CppObject* GameObjectOf(Il2CppObject* component) { return Call<Il2CppObject*>(gComponentGameObject, component); }
Il2CppObject* Component(Il2CppObject* gameObject, Il2CppObject* type) { return Invoke(gGetComponent, gameObject, type); }
int ChildCount(Il2CppObject* t) { return Call<int32_t>(gChildCount, t); }
Il2CppObject* Child(Il2CppObject* t, int i) { return Call<Il2CppObject*>(gGetChild, t, int32_t(i)); }

Il2CppObject* Instantiate(Il2CppObject* original, Il2CppObject* parent) {
  bool worldSpace = false;
  if (gInstantiate) return Invoke(gInstantiate, nullptr, original, parent, &worldSpace);
  if (!gInstantiatePlain) return nullptr;
  auto copy = Invoke(gInstantiatePlain, nullptr, original);
  if (copy) Invoke(gSetParent, TransformOf(copy), parent, &worldSpace);  // локальная поза сохраняется
  return copy;
}

Il2CppObject* FindFirst(Il2CppObject* type) {
  int sortNone = 0;
  auto all = reinterpret_cast<Il2CppArray*>(Invoke(gFindObjects, nullptr, type, &sortNone));
  for (uint32_t i = 0; i < Length(all); i++)
    if (Alive(At(all, i))) return At(all, i);
  return nullptr;
}

// ---------- скины ----------

// Ключи классов игры (у ПК-версии они из реестра классов; на Quest — по дампу ПК)
char const* const kClassKeys[] = {"ASSASSIN", "BOULDER", "CROSSBOLT", "DRONE",   "GOLEM",  "GRAPPLER", "JACKET",
                                  "KOBOLD",   "REAPER",  "ROLLER",    "SCARAB", "SERAPH", "SPARROW",  "STYX"};

struct SkinFile {
  std::string name;  // имя файла без .skin — так скин зовут в skins.cfg и в сети
  std::string path;
  Held bundle;
  std::map<std::string, Held> prefabs;  // ключ класса → модель (пустой Held — модели нет)
};
std::vector<SkinFile> gSkins;
bool gSkinsLoaded;
std::map<std::string, std::string> gConfig;  // ключ класса → имя скина ("" — родной вид)

Il2CppObject* LoadBundle(std::string const& path) {
  if (gLoadBundle) return Invoke(gLoadBundle, nullptr, il2cpp_string_new(path.c_str()));
  if (!gLoadBundleIcall) return nullptr;
  // Как на ПК: внутренний вызов возвращает GC handle бандла
  auto wide = ToUtf16(path);
  Span span{wide.data(), static_cast<int32_t>(wide.size())};
  void* handle = gLoadBundleIcall(&span, 0, 0);
  return handle ? il2cpp_gchandle_get_target(handle) : nullptr;
}

Il2CppObject* LoadAsset(Il2CppObject* bundle, std::string const& name, Il2CppObject* type = nullptr) {
  if (!type) type = gGameObjectType;
  if (gLoadAsset) return Invoke(gLoadAsset, bundle, il2cpp_string_new(name.c_str()), type);
  if (!gLoadAssetIcall) return nullptr;
  auto wide = ToUtf16(name);
  Span span{wide.data(), static_cast<int32_t>(wide.size())};
  void* handle = gLoadAssetIcall(NativePtr(bundle), &span, type);
  return handle ? il2cpp_gchandle_get_target(handle) : nullptr;
}

std::string ConfigPath() { return std::string(kSkinDir) + kConfig; }

void ReadConfig() {
  std::ifstream in(ConfigPath());
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    for (auto& c : key) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    gConfig[key] = value;
  }
}

void WriteConfig() {
  std::ofstream out(ConfigPath(), std::ios::trunc);
  if (!out) {
    LOG(ANDROID_LOG_ERROR, "не удалось записать %s", ConfigPath().c_str());
    return;
  }
  out << "# Skins: класс=скин (имя файла без .skin), пусто — родной вид. Меню — левый стик.\n";
  for (auto const& [key, value] : gConfig) out << key << '=' << value << '\n';
}

// Модель скина для класса. При смене сцены игра выгружает ассеты (и, бывает, бандлы): тогда модель
// уничтожена — грузим заново (бандл — тоже, если выгружен и он). Модели нет в бандле — nullptr.
Il2CppObject* PrefabFor(SkinFile& skin, std::string const& key) {
  auto it = skin.prefabs.find(key);
  if (it != skin.prefabs.end() && (!it->second.obj || Alive(it->second.obj))) return it->second.obj;
  if (it != skin.prefabs.end()) {
    Free(it->second);
    skin.prefabs.erase(it);
  }
  if (!Alive(skin.bundle.obj)) {
    Free(skin.bundle);
    skin.bundle = Hold(LoadBundle(skin.path));
    if (!skin.bundle.obj) {
      LOG(ANDROID_LOG_ERROR, "бандл скина '%s' не загрузился заново", skin.name.c_str());
      return nullptr;
    }
  }
  return skin.prefabs.emplace(key, Hold(LoadAsset(skin.bundle.obj, key))).first->second.obj;
}

void LoadSkins() {
  gSkinsLoaded = true;
  ReadConfig();

  std::vector<std::string> files;
  if (DIR* dir = opendir(kSkinDir)) {
    while (dirent* e = readdir(dir)) {
      std::string name = e->d_name;
      size_t n = strlen(kExtension);
      if (name.size() > n && name.compare(name.size() - n, n, kExtension) == 0) files.push_back(name);
    }
    closedir(dir);
  } else {
    LOG(ANDROID_LOG_WARN, "нет папки %s", kSkinDir);
  }
  std::sort(files.begin(), files.end());

  for (auto const& file : files) {
    auto bundle = LoadBundle(std::string(kSkinDir) + file);
    if (!bundle) {
      LOG(ANDROID_LOG_ERROR, "%s: бандл не загрузился (собран не под Android или не той версией Unity?)", file.c_str());
      continue;
    }
    SkinFile skin;
    skin.name = file.substr(0, file.size() - strlen(kExtension));
    skin.path = std::string(kSkinDir) + file;
    skin.bundle = Hold(bundle);
    gSkins.push_back(std::move(skin));
    std::string classes;
    for (auto key : kClassKeys)
      if (PrefabFor(gSkins.back(), key)) classes += std::string(classes.empty() ? "" : ", ") + key;
    LOG(ANDROID_LOG_INFO, "скин '%s': %s", gSkins.back().name.c_str(), classes.empty() ? "нет моделей" : classes.c_str());
  }
}

SkinFile* FindSkin(std::string const& key, std::string const& name) {
  for (auto& skin : gSkins)
    if (skin.name == name && PrefabFor(skin, key)) return &skin;
  return nullptr;
}

// Скины для класса по алфавиту
std::vector<SkinFile*> SkinsFor(std::string const& key) {
  std::vector<SkinFile*> out;
  for (auto& skin : gSkins)
    if (PrefabFor(skin, key)) out.push_back(&skin);
  return out;
}

// Какой скин надеть на свой класс: из skins.cfg, иначе первый скин для класса ("" — родной вид)
std::string Selected(std::string const& key) {
  auto cfg = gConfig.find(key);
  if (cfg != gConfig.end()) return FindSkin(key, cfg->second) ? cfg->second : "";
  auto skins = SkinsFor(key);
  return skins.empty() ? "" : skins.front()->name;
}

// ---------- надетый скин ----------

struct Swap {
  Held filter, original, wanted;  // wanted пустой — меш спрятан
};
struct Extra {
  Held root;
  Held source;  // рендерер игры, чей материал повторяем
  std::vector<Held> renderers;
  Il2CppObject* applied = nullptr;
};
// Новая деталь с ребёнком «@end»: ездит между своей позой и позой «@end» по анимации игры у предка
struct Moving {
  Held part;    // Transform копии
  Held driver;  // ScalarProceduralAnimation
  Vec3 startPos, endPos;
  Quat startRot, endRot;
  float full = 1.f;  // currentTime, при котором деталь доходит до «@end» («@end 0.3»: отдача арбалета доводит его до ~0.3–0.4)
};
// Скин на одном экземпляре класса — своём или чужом
struct Applied {
  Held cls;          // GameObject класса
  std::string key;   // ключ класса
  std::string skin;  // "" — родной вид
  std::vector<Swap> swaps;
  std::vector<Extra> extras;
  std::vector<Moving> moving;
  bool reportedRestore = false;
  std::string net = "\x01";  // у чужого класса — последнее прочитанное из сети («» — родной вид; \x01 — ещё ничего)
};

Applied gLocal;
std::map<Il2CppObject*, Applied> gRemotes;  // по GameObject чужого класса
int gLastFrame = -1;

// Снять скин: restore — вернуть меши игры и удалить новые детали (класс жив, скин сменили);
// иначе только отпустить объекты (класс уже уничтожен)
void Strip(Applied& a, bool restore) {
  for (auto& s : a.swaps) {
    if (restore && Alive(s.filter.obj)) Invoke(gSetSharedMesh, s.filter.obj, s.original.obj);
    Free(s.filter);
    Free(s.original);
    Free(s.wanted);
  }
  for (auto& e : a.extras) {
    if (restore && Alive(e.root.obj) && gDestroy) Invoke(gDestroy, nullptr, e.root.obj);
    Free(e.root);
    Free(e.source);
    for (auto& r : e.renderers) Free(r);
  }
  for (auto& m : a.moving) {
    Free(m.part);
    Free(m.driver);
  }
  a.swaps.clear();
  a.extras.clear();
  a.moving.clear();
  a.reportedRestore = false;
  a.skin.clear();
}

void Forget(Applied& a) {
  Strip(a, false);
  Free(a.cls);
  a.key.clear();
}

void Index(Il2CppObject* t, std::string const& prefix, std::unordered_map<std::string, Il2CppObject*>& out) {
  for (int i = 0, n = ChildCount(t); i < n; i++) {
    auto c = Child(t, i);
    if (!c) continue;
    std::string path = prefix.empty() ? NameOf(c) : prefix + "/" + NameOf(c);
    out.emplace(path, c);
    Index(c, path, out);
  }
}

// Снаряды, которые класс бросает (колесо Crossbolt «03_BIGWHEEL (Projectile)»), во время игры лежат не внутри
// класса: берём их по ссылке ProjectileTeleporter.teleportProjectile из узлов класса и добавляем под их именем
void IndexProjectiles(Il2CppObject* root, std::unordered_map<std::string, Il2CppObject*>& nodes) {
  if (!gTeleporterType) return;
  std::vector<Il2CppObject*> found;
  for (auto const& [p, t] : nodes) {
    auto teleporter = Component(GameObjectOf(t), gTeleporterType);
    if (!teleporter) continue;
    auto projectile = *reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(teleporter) + gTeleportProjectileOffset);
    if (Alive(projectile)) found.push_back(Call<Il2CppObject*>(gComponentTransform, projectile));
  }
  for (auto t : found) {
    bool inside = false;
    for (auto p = Call<Il2CppObject*>(gGetParent, t); p && !inside; p = Call<Il2CppObject*>(gGetParent, p))
      inside = p == root;
    std::string name = NameOf(t);
    if (inside || !nodes.emplace(name, t).second) continue;
    Index(t, name, nodes);
  }
}

// Узел по пути; если точного нет — по концу пути, когда он однозначен (копия у других игроков может
// отличаться промежуточными узлами)
Il2CppObject* FindNode(std::unordered_map<std::string, Il2CppObject*> const& nodes, std::string const& path) {
  auto exact = nodes.find(path);
  if (exact != nodes.end()) return exact->second;
  for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) {
    std::string tail = path.substr(slash);  // «/…»
    Il2CppObject* found = nullptr;
    int count = 0;
    for (auto const& [p, t] : nodes)
      if (p == tail.substr(1) || (p.size() > tail.size() && p.compare(p.size() - tail.size(), tail.size(), tail) == 0)) {
        found = t;
        count++;
      }
    if (count == 1) return found;
    if (count > 1) return nullptr;
  }
  return nullptr;
}

void SetLayers(Il2CppObject* t, int32_t layer, std::vector<Held>& renderers) {
  auto go = GameObjectOf(t);
  Invoke(gSetLayer, go, &layer);
  if (auto r = Component(go, gRendererType)) renderers.push_back(Hold(r));
  for (int i = 0, n = ChildCount(t); i < n; i++)
    if (auto c = Child(t, i)) SetLayers(c, layer, renderers);
}

Il2CppObject* NearestRenderer(Il2CppObject* t) {
  for (; t; t = Call<Il2CppObject*>(gGetParent, t))
    if (auto r = Component(GameObjectOf(t), gRendererType)) return r;
  return nullptr;
}

void SyncExtra(Extra& e) {
  if (!Alive(e.source.obj)) return;
  auto m = Call<Il2CppObject*>(gGetSharedMaterial, e.source.obj);
  if (!m || m == e.applied) return;
  for (auto& r : e.renderers)
    if (Alive(r.obj)) Call<void>(gSetSharedMaterial, r.obj, m);
  e.applied = m;
}

bool CanMove() {
  return gScalarAnimType && gGetLocalPosition && gGetLocalRotation && gSetLocalRotation && gGetLocalScale &&
         gSetLocalPosition;
}

// Деталь copy с ребёнком «@end» ездит по анимации ближайшего к node предка (до корня класса root)
void AddMoving(Applied& a, Il2CppObject* copy, Il2CppObject* node, Il2CppObject* root) {
  auto part = TransformOf(copy);
  Il2CppObject* end = nullptr;
  std::string endName;
  for (int i = 0, n = ChildCount(part); i < n && !end; i++)
    if (auto c = Child(part, i)) {
      std::string name = NameOf(c);
      if (name == "@end" || name.rfind("@end ", 0) == 0) {
        end = c;
        endName = name;
      }
    }
  if (!end) return;
  float full = 1.f;
  if (endName.size() > 5) {
    full = std::strtof(endName.c_str() + 5, nullptr);
    if (!(full > 0.f)) {
      LOG(ANDROID_LOG_WARN, "'%s': в «%s» не число больше 0 — конец при 1", NameOf(copy).c_str(), endName.c_str());
      full = 1.f;
    }
  }
  if (!CanMove()) {
    LOG(ANDROID_LOG_WARN, "'%s': движение деталей не поддержано (нет API) — стоит на месте", NameOf(copy).c_str());
    return;
  }
  Il2CppObject* driver = nullptr;
  for (auto t = node; t && !driver; t = Call<Il2CppObject*>(gGetParent, t)) {
    driver = Component(GameObjectOf(t), gScalarAnimType);
    if (t == root) break;
  }
  if (!driver) {
    LOG(ANDROID_LOG_WARN, "'%s': над '%s' нет анимации игры — деталь стоит на месте", NameOf(copy).c_str(),
        NameOf(node).c_str());
    return;
  }
  Moving m;
  m.part = Hold(part);
  m.driver = Hold(driver);
  m.startPos = Call<Vec3>(gGetLocalPosition, part);
  m.startRot = Call<Quat>(gGetLocalRotation, part);
  Vec3 s = Call<Vec3>(gGetLocalScale, part), e = Call<Vec3>(gGetLocalPosition, end);
  m.endPos = m.startPos + Rotate(m.startRot, Vec3{s.x * e.x, s.y * e.y, s.z * e.z});
  m.endRot = m.startRot * Call<Quat>(gGetLocalRotation, end);
  m.full = full;
  a.moving.push_back(std::move(m));
}

void SyncMoving(Moving& m) {
  if (!Alive(m.part.obj) || !Alive(m.driver.obj)) return;
  float t = *reinterpret_cast<float*>(reinterpret_cast<char*>(m.driver.obj) + gCurrentTimeOffset) / m.full;
  t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
  Vec3 p = Lerp(m.startPos, m.endPos, t);
  Quat r = Nlerp(m.startRot, m.endRot, t);
  Call<void>(gSetLocalPosition, m.part.obj, p);
  Call<void>(gSetLocalRotation, m.part.obj, r);
}

// Надеть скин skinName на класс a (он уже держит cls и key); "" — родной вид
void Wear(Applied& a, std::string const& skinName, char const* owner) {
  Strip(a, true);
  if (skinName.empty() || !Alive(a.cls.obj)) return;
  SkinFile* skin = FindSkin(a.key, skinName);
  if (!skin) {
    LOG(ANDROID_LOG_INFO, "%s: скина '%s' у тебя нет — родной вид (%s)", a.key.c_str(), skinName.c_str(), owner);
    return;
  }
  a.skin = skinName;

  auto root = TransformOf(a.cls.obj);
  std::unordered_map<std::string, Il2CppObject*> nodes;
  Index(root, "", nodes);
  IndexProjectiles(root, nodes);

  auto skinRoot = TransformOf(PrefabFor(*skin, a.key));
  std::vector<std::string> missing;
  for (int i = 0, n = ChildCount(skinRoot); i < n; i++) {
    auto entry = Child(skinRoot, i);
    std::string name = NameOf(entry);
    bool extra = !name.empty() && name[0] == '+';
    std::string path = extra ? name.substr(1) : name;

    Il2CppObject* node = path.empty() ? root : FindNode(nodes, path);
    if (!node) {
      missing.push_back(path);
      continue;
    }

    if (extra) {
      int32_t layer = Unbox<int32_t>(Invoke(gGetLayer, GameObjectOf(node)));
      for (int j = 0, m = ChildCount(entry); j < m; j++) {
        auto part = Child(entry, j);
        auto copy = Instantiate(GameObjectOf(part), node);
        if (!copy) {
          LOG(ANDROID_LOG_ERROR, "не удалось скопировать деталь '%s'", NameOf(part).c_str());
          continue;
        }
        Invoke(gSetObjectName, copy, il2cpp_string_new(("Skin " + NameOf(part)).c_str()));
        Extra e;
        e.root = Hold(copy);
        e.source = Hold(NearestRenderer(node));
        SetLayers(TransformOf(copy), layer, e.renderers);
        SyncExtra(e);
        a.extras.push_back(std::move(e));
        AddMoving(a, copy, node, root);
      }
      continue;
    }

    auto filter = Component(GameObjectOf(node), gMeshFilterType);
    if (!filter) {
      missing.push_back(path + " (без MeshFilter)");
      continue;
    }
    // Один меш на деталь: второй путь к ней (поиск по концу пути) сломал бы и вид, и возврат меша игры
    if (std::any_of(a.swaps.begin(), a.swaps.end(), [&](Swap const& s) { return s.filter.obj == filter; })) {
      LOG(ANDROID_LOG_WARN, "'%s': меш скина на эту деталь уже стоит — второй пропущен", path.c_str());
      continue;
    }
    auto skinFilter = Component(GameObjectOf(entry), gMeshFilterType);
    auto mesh = skinFilter ? Invoke(gGetSharedMesh, skinFilter) : nullptr;
    Swap s;
    s.filter = Hold(filter);
    s.original = Hold(Invoke(gGetSharedMesh, filter));
    s.wanted = Hold(mesh);
    Invoke(gSetSharedMesh, filter, mesh);
    a.swaps.push_back(std::move(s));
  }

  if (!missing.empty()) {
    LOG(ANDROID_LOG_WARN, "скин '%s' на %s (%s): в классе нет узлов (%zu) — на Quest он устроен иначе? Пропущены:",
        skin->name.c_str(), a.key.c_str(), owner, missing.size());
    for (auto const& p : missing) LOG(ANDROID_LOG_WARN, "  %s", p.c_str());
    static std::vector<std::string> dumped;  // список узлов класса — по разу на класс
    if (std::find(dumped.begin(), dumped.end(), a.key) == dumped.end()) {
      dumped.push_back(a.key);
      LOG(ANDROID_LOG_INFO, "узлы класса %s (%zu):", a.key.c_str(), nodes.size());
      for (auto const& [p, t] : nodes) LOG(ANDROID_LOG_INFO, "  %s", p.c_str());
    }
  }
}

// Меши, которые игра могла вернуть, ставим снова; новые детали повторяют материал (цвет команды, вспышка).
// false — игра выгрузила меши самого скина (смена сцены): его надо надеть заново
bool Sync(Applied& a) {
  for (auto& s : a.swaps)
    if (s.wanted.obj && !Alive(s.wanted.obj)) return false;

  int restored = 0;
  for (auto& s : a.swaps) {
    if (!Alive(s.filter.obj)) continue;
    if (Call<Il2CppObject*>(gGetSharedMesh, s.filter.obj) == s.wanted.obj) continue;
    Call<void>(gSetSharedMesh, s.filter.obj, s.wanted.obj);
    restored++;
  }
  if (restored > 0 && !a.reportedRestore) {
    LOG(ANDROID_LOG_INFO, "игра вернула свои меши на %s (%d) — ставлю скин снова", a.key.c_str(), restored);
    a.reportedRestore = true;
  }
  for (auto& e : a.extras) SyncExtra(e);
  for (auto& m : a.moving) SyncMoving(m);
  return true;
}

// Ключ класса — первые буквы имени объекта: «Kobold (Local) Variant(Clone)» → KOBOLD
// Ключ класса — как на ПК: имя значения CombatClass.classType заглавными (KOBOLD, DRONE…). Имя объекта
// не годится: у дрона оно «Spectator Drone (Local) Variant». classType = Custom (так у Styx) или поле не
// нашлось — тогда первое слово имени объекта («Styx (Local) Variant» → STYX)
size_t gClassTypeOffset;
std::map<int32_t, std::string> gClassTypeNames;

std::string ClassKeyOf(Il2CppObject* go) {
  if (gClassTypeOffset)
    if (auto cc = Component(go, gCombatClassType)) {
      int32_t value = *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(cc) + gClassTypeOffset);
      auto it = gClassTypeNames.find(value);
      if (it != gClassTypeNames.end() && it->second != "CUSTOM") return it->second;
    }
  std::string name = NameOf(go), key;
  for (char c : name) {
    if (!isalpha(static_cast<unsigned char>(c))) break;
    key += static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  return key;
}

// Имена значений enum ClassType — из метаданных (статические поля-константы)
void LoadClassTypeNames(Il2CppClass* combatClass) {
  auto lib = modloader_libil2cpp_handle;
  auto getFields = reinterpret_cast<FieldInfo* (*)(Il2CppClass*, void**)>(dlsym(lib, "il2cpp_class_get_fields"));
  auto fieldName = reinterpret_cast<char const* (*)(FieldInfo*)>(dlsym(lib, "il2cpp_field_get_name"));
  auto fieldFlags = reinterpret_cast<int (*)(FieldInfo*)>(dlsym(lib, "il2cpp_field_get_flags"));
  auto fieldType = reinterpret_cast<Il2CppType const* (*)(FieldInfo*)>(dlsym(lib, "il2cpp_field_get_type"));
  auto staticValue = reinterpret_cast<void (*)(FieldInfo*, void*)>(dlsym(lib, "il2cpp_field_static_get_value"));
  auto field = il2cpp_class_get_field_from_name(combatClass, "classType");
  if (!getFields || !fieldName || !fieldFlags || !fieldType || !staticValue || !field) {
    LOG(ANDROID_LOG_WARN, "нет CombatClass.classType — ключ класса по имени объекта (дрон не распознается)");
    return;
  }
  auto enumClass = il2cpp_class_from_type(fieldType(field));
  void* iter = nullptr;
  while (auto f = enumClass ? getFields(enumClass, &iter) : nullptr) {
    constexpr int kStatic = 0x10, kLiteral = 0x40;
    if ((fieldFlags(f) & (kStatic | kLiteral)) != (kStatic | kLiteral)) continue;  // value__ — не константа
    int32_t value = 0;
    staticValue(f, &value);
    std::string name = fieldName(f);
    for (auto& c : name) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    gClassTypeNames[value] = name;
  }
  if (gClassTypeNames.empty()) return;
  gClassTypeOffset = il2cpp_field_get_offset(field);
}

bool IsOwned(Il2CppObject* classObject);
float DistanceToCamera(Il2CppObject* classObject);

// Свой класс. В хабе у него «Local» в имени объекта, а в матче класс создаётся из другого префаба
// и такого слова нет — поэтому главное: класс, которым владеем мы (NetworkPrefab.IsOwned); из
// нескольких (боты у хоста) — ближайший к камере. «Local» в имени — запасной признак (без сети).
Il2CppObject* FindLocalClass() {
  int sortNone = 0;
  auto all = reinterpret_cast<Il2CppArray*>(Invoke(gFindObjects, nullptr, gCombatClassType, &sortNone));
  Il2CppObject* best = nullptr;
  float bestDistance = 1e9f;
  for (uint32_t i = 0; i < Length(all); i++) {
    auto cc = At(all, i);
    if (!Alive(cc)) continue;
    auto go = GameObjectOf(cc);
    bool owned = IsOwned(go), local = NameOf(go).find(kLocalMark) != std::string::npos;
    if (!owned && !local) continue;
    float distance = DistanceToCamera(go);
    if (distance < bestDistance) {
      best = go;
      bestDistance = distance;
    }
  }
  return best;
}

// ---------- сеть: RPC между игроками с модом ----------
// Как на ПК (SkinNet.cs): пакет со своего CombatClass через NetworkComponent.SendRpc / SendRpcTo с номером
// RPC 255 — у компонентов их не больше 254, и игра без мода такой пакет молча выбрасывает
// (NetworkComponent.OnReceive: индекс ≥ числа RPC или > 254 — return). Мод ловит его хуком на OnReceive
// до этой проверки. Пакет: [255]['S']['K'][версия][флаги][длина имени][имя UTF-8], пустое имя — родной вид.
// Пришёл на чужой CombatClass — это скин этого экземпляра класса.
// Кто с модом: раз в kHelloSeconds «привет» всем (со своим скином; внеочередной — через секунду после
// появления своего класса или нового чужого, не чаще kHelloMinGap), на «привет» нового игрока отвечаем
// ему лично; известным игрокам с модом шлём скин лично — сразу при смене (несколько раз) и раз в kPollSeconds.

constexpr int kNetFrames = 10;  // сеть — раз в столько кадров
constexpr uint8_t kRpcIndex = 255;
constexpr uint8_t kProtocol = 1;
constexpr uint8_t kFlagHello = 1;
constexpr size_t kHeaderBytes = 6;
constexpr size_t kMaxSkinBytes = 64;
constexpr double kHelloSeconds = 30, kPollSeconds = 5, kPeerTimeout = 40, kReplyCooldown = 2;
constexpr double kBurstSeconds = 1, kScanSeconds = 1;
constexpr double kHelloMinGap = 3, kHelloAfterSpawn = 1;
constexpr int kBurstSends = 3;
constexpr char kNothing[] = "\x01";  // «ещё ничего не слали»

// ReadOnlySpan<byte> и DID — по 16 байт, в il2cpp передаются по значению
struct NetSpan {
  uint16_t start;
  uint16_t length;
  Il2CppObject* items;  // упакованный ByteList: после klass и monitor — uint8_t* ptr, int count
};
struct NetDid {
  uint8_t b[16];
};

struct Peer {
  NetDid id;
  double lastHeard = 0;
  double lastReply = -1e9;
};
struct Incoming {
  Il2CppObject* component;  // чужой CombatClass
  NetDid sender;
  std::string skin;
  bool hello;
};

Held gNetworkManager, gMatchmaker;
bool gNetFailed;
std::string gNetState;  // последнее состояние сети в логе — пишем только смены
std::mutex gInboxLock;
std::vector<Incoming> gInbox;
std::map<std::string, Peer> gPeers;               // по 16 байтам DID
// CombatClass → скин, который объявил владелец. Забываем, только если класса не видно kAnnouncedTtl:
// FindObjectsByType не отдаёт выключенные объекты, а класс бывает выключен на миг
struct Announced {
  std::string skin;
  double seen;
};
constexpr double kAnnouncedTtl = 60;
std::map<Il2CppObject*, Announced> gAnnounced;
std::atomic<int> gOtherVersion{-1};  // версия чужого протокола из пакета (-1 — не было)
int gReportedVersion = -1;
Il2CppObject* gSentInstance;
std::string gSentSkin = kNothing, gLoggedSkin = kNothing;
double gNextHello, gNextPoll, gNextBurst, gNextScan;
double gLastHello = -1e9;

// Внеочередной «привет» не позже at (kHelloMinGap держит Publish)
void HelloSoon(double at) { gNextHello = std::min(gNextHello, at); }
int gBurstLeft;

MethodInfo const* gSendRpc;
MethodInfo const* gSendRpcTo;
MethodInfo const* gByteBufferCtor;
MethodInfo const* gByteBufferAdd;  // Add(byte[])
Il2CppClass* gByteClass;
Il2CppArray* (*il2cpp_array_new)(Il2CppClass*, uintptr_t);
Il2CppClass* gByteBufferClass;
Il2CppClass* gByteListClass;
uint8_t gPriorityLow;

double Now() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void NetState(std::string const& state) {
  if (state == gNetState) return;
  gNetState = state;
  LOG(ANDROID_LOG_INFO, "сеть: %s", state.c_str());
}

bool IsOwned(Il2CppObject* classObject) {
  if (gNetFailed) return false;
  auto np = Component(classObject, gNetworkPrefabType);
  return np && Unbox<bool>(Invoke(gIsOwned, np));
}

float DistanceToCamera(Il2CppObject* classObject) {
  if (!gGetMainCamera || !gGetPosition) return 0.f;
  auto camera = Call<Il2CppObject*>(gGetMainCamera);
  if (!camera) return 0.f;
  Vec3 d = Call<Vec3>(gGetPosition, Call<Il2CppObject*>(gComponentTransform, camera)) -
           Call<Vec3>(gGetPosition, TransformOf(classObject));
  return std::sqrt(d.x * d.x + d.z * d.z);  // по горизонтали: камера — в голове, корень класса — у ног
}

bool Session() {
  if (!Alive(gNetworkManager.obj)) {
    Free(gNetworkManager);
    gNetworkManager = Hold(FindFirst(gNetworkManagerType));
  }
  if (!Alive(gMatchmaker.obj)) {
    Free(gMatchmaker);
    gMatchmaker = Hold(FindFirst(gMatchmakerType));
  }
  if (!Alive(gNetworkManager.obj) || !Alive(gMatchmaker.obj)) return false;

  // HasSession для этого не годится (в матче он false — похоже, это вход в аккаунт), поэтому смотрим,
  // есть ли сетевой клиент и не офлайн ли Matchmaker
  bool offline = false;
  if (gOfflineMode) offline = Unbox<bool>(Invoke(gOfflineMode, nullptr));
  else if (gOfflineField && il2cpp_field_static_get_value) il2cpp_field_static_get_value(gOfflineField, &offline);
  return !offline && Invoke(gGetClient, gNetworkManager.obj) != nullptr;
}

void ResetSession() {
  gPeers.clear();
  gAnnounced.clear();
  {
    std::lock_guard<std::mutex> lock(gInboxLock);
    gInbox.clear();
  }
  gSentInstance = nullptr;
  gSentSkin = kNothing;
  gNextHello = gNextPoll = 0;
  gLastHello = -1e9;
  gBurstLeft = 0;
}

// Свой CombatClass, если с него можно слать: сетевой, с ID. nullptr — нельзя
Il2CppObject* SendingClass() {
  if (!Alive(gLocal.cls.obj)) return nullptr;  // между смертью и появлением — обычное дело
  auto np = Component(gLocal.cls.obj, gNetworkPrefabType);
  if (!np) {
    NetState("у своего класса нет NetworkPrefab — другие игроки скин не увидят");
    return nullptr;
  }
  if (!Unbox<bool>(Invoke(gHasId, np))) return nullptr;  // сетевой ID выдают не сразу после появления
  auto cc = Component(gLocal.cls.obj, gCombatClassType);
  if (cc) NetState("работает");
  return cc;
}

std::string LocalSkinForNet() { return gLocal.skin.size() <= kMaxSkinBytes ? gLocal.skin : ""; }

// target — лично этому игроку, nullptr — всем
void SendSkin(Il2CppObject* cc, NetDid const* target, bool hello, std::string const& skin) {
  int capacity = static_cast<int>(kHeaderBytes + skin.size());
  int headroom = 64;  // запас спереди — под номер компонента
  auto packet = il2cpp_array_new(gByteClass, capacity);
  auto buf = il2cpp_object_new(gByteBufferClass);
  if (!packet || !buf) return;
  auto data = reinterpret_cast<uint8_t*>(packet) + 4 * sizeof(void*);  // после klass, monitor, bounds, max_length
  data[0] = kRpcIndex;
  data[1] = 'S';
  data[2] = 'K';
  data[3] = kProtocol;
  data[4] = hello ? kFlagHello : 0;
  data[5] = static_cast<uint8_t>(skin.size());
  memcpy(data + kHeaderBytes, skin.data(), skin.size());
  Invoke(gByteBufferCtor, buf, &capacity, &headroom);
  Invoke(gByteBufferAdd, buf, packet);  // одним вызовом

  bool reliable = false;
  if (target) Invoke(gSendRpcTo, cc, target, &gPriorityLow, &reliable, buf);
  else Invoke(gSendRpc, cc, &gPriorityLow, &reliable, buf);
}

std::string PeerKey(NetDid const& id) { return std::string(reinterpret_cast<char const*>(id.b), sizeof(id.b)); }

void Drain(Il2CppObject* cc, double now) {
  std::vector<Incoming> batch;
  {
    std::lock_guard<std::mutex> lock(gInboxLock);
    batch.swap(gInbox);
  }
  for (auto& m : batch) {
    auto key = PeerKey(m.sender);
    auto it = gPeers.find(key);
    if (it == gPeers.end()) {
      it = gPeers.emplace(key, Peer{m.sender}).first;
      LOG(ANDROID_LOG_INFO, "найден игрок с модом (всего %zu)", gPeers.size());
    }
    it->second.lastHeard = now;
    gAnnounced[m.component] = {m.skin, now};
    if (!m.hello || !cc || now - it->second.lastReply < kReplyCooldown) continue;
    it->second.lastReply = now;
    SendSkin(cc, &m.sender, false, LocalSkinForNet());
  }
}

void Publish(Il2CppObject* cc, double now) {
  std::string skin = LocalSkinForNet();
  bool newInstance = cc != gSentInstance;
  if (newInstance || skin != gSentSkin) {
    gBurstLeft = kBurstSends;
    gNextBurst = now;
    if (skin != gLoggedSkin)  // новый экземпляр класса с тем же скином — не пишем
      LOG(ANDROID_LOG_INFO, "другим игрокам с модом: %s", skin.empty() ? "родной вид" : skin.c_str());
    gLoggedSkin = skin;
    gSentInstance = cc;
    gSentSkin = skin;
    // Копия нового экземпляра у других появляется не сразу — «привет» в первый миг теряется
    if (newInstance) HelloSoon(now + kHelloAfterSpawn);
  }

  if (now >= gNextHello && now - gLastHello >= kHelloMinGap) {
    SendSkin(cc, nullptr, true, skin);
    gLastHello = now;
    gNextHello = now + kHelloSeconds;
  }

  bool due = false;
  if (gBurstLeft > 0 && now >= gNextBurst) {
    gBurstLeft--;
    gNextBurst = now + kBurstSeconds;
    due = true;
  }
  if (now >= gNextPoll) due = true;
  if (!due) return;
  gNextPoll = now + kPollSeconds;
  for (auto& [key, peer] : gPeers) SendSkin(cc, &peer.id, false, skin);
}

void ExpirePeers(double now) {
  for (auto it = gPeers.begin(); it != gPeers.end();) {
    if (now - it->second.lastHeard > kPeerTimeout) {
      it = gPeers.erase(it);
      LOG(ANDROID_LOG_INFO, "игрок с модом пропал (осталось %zu)", gPeers.size());
    } else {
      ++it;
    }
  }
}

void ScanRemotes(double now) {
  int sortNone = 0;
  auto all = reinterpret_cast<Il2CppArray*>(Invoke(gFindObjects, nullptr, gCombatClassType, &sortNone));
  std::vector<Il2CppObject*> seenGo;
  for (uint32_t i = 0; i < Length(all); i++) {
    auto cc = At(all, i);
    if (!Alive(cc)) continue;
    auto go = GameObjectOf(cc);
    if (go == gLocal.cls.obj) continue;
    auto np = Component(go, gNetworkPrefabType);
    if (!np || Unbox<bool>(Invoke(gIsOwned, np))) continue;  // свой (или не сетевой) класс
    seenGo.push_back(go);

    auto it = gRemotes.find(go);
    if (it == gRemotes.end()) {
      it = gRemotes.emplace(go, Applied{}).first;
      it->second.cls = Hold(go);
      it->second.key = ClassKeyOf(go);
      // Новый чужой класс — может, это игрок с модом, который о нас ещё не знает
      if (!gAnnounced.count(cc)) HelloSoon(now + kHelloAfterSpawn);
    }
    auto announced = gAnnounced.find(cc);
    if (announced == gAnnounced.end()) continue;  // игрок без мода или ещё не прислал
    announced->second.seen = now;
    std::string skin = announced->second.skin;
    if (it->second.net == skin) continue;
    bool news = !skin.empty() || (it->second.net != kNothing && !it->second.net.empty());  // первое «родной вид» — не новость
    it->second.net = skin;  // скина нет у нас — тоже запоминаем, чтобы не пробовать каждый раз заново
    if (news) LOG(ANDROID_LOG_INFO, "другой игрок на %s: %s", it->second.key.c_str(), skin.empty() ? "родной вид" : skin.c_str());
    Wear(it->second, skin, "другой игрок");
  }
  for (auto it = gRemotes.begin(); it != gRemotes.end();) {
    if (std::find(seenGo.begin(), seenGo.end(), it->first) == seenGo.end()) {
      Forget(it->second);
      it = gRemotes.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = gAnnounced.begin(); it != gAnnounced.end();) {
    if (now - it->second.seen > kAnnouncedTtl) it = gAnnounced.erase(it);
    else ++it;
  }
}

void TickNetwork() {
  if (gNetFailed) return;
  if (!Session()) {
    ResetSession();  // новая сессия — всё узнаем и отправим заново
    return;
  }
  double now = Now();
  int other = gOtherVersion.load();
  if (other >= 0 && other != gReportedVersion) {
    gReportedVersion = other;
    LOG(ANDROID_LOG_WARN, "игрок с другой версией мода (протокол %d, у нас %d) — скины друг друга не видно", other,
        kProtocol);
  }
  auto cc = SendingClass();
  Drain(cc, now);
  if (cc) Publish(cc, now);
  ExpirePeers(now);
  if (now < gNextScan) return;
  gNextScan = now + kScanSeconds;
  ScanRemotes(now);
}

// NetworkComponent.OnReceive(ReadOnlySpan<byte> message, DID sender) — для каждого RPC игры, поэтому
// проверяем дёшево: наш пакет начинается с 255 'S' 'K'
using OnReceiveFn = void (*)(Il2CppObject*, NetSpan, NetDid, MethodInfo const*);
OnReceiveFn gOrigOnReceive;

bool TakePacket(Il2CppObject* self, NetSpan const& message, NetDid const& sender) {
  if (message.length < kHeaderBytes || !message.items ||
      *reinterpret_cast<Il2CppClass**>(message.items) != gByteListClass)
    return false;
  auto raw = reinterpret_cast<char*>(message.items) + 2 * sizeof(void*);
  auto data = *reinterpret_cast<uint8_t**>(raw);
  int count = *reinterpret_cast<int32_t*>(raw + sizeof(void*));
  if (!data || message.start + message.length > count) return false;
  auto p = data + message.start;
  if (p[0] != kRpcIndex || p[1] != 'S' || p[2] != 'K') return false;
  if (p[3] != kProtocol) {
    gOtherVersion.store(p[3]);  // пакет наш, но разобрать не можем — скажем в TickNetwork
  } else {
    size_t n = p[5];
    if (kHeaderBytes + n <= message.length && n <= kMaxSkinBytes) {
      std::lock_guard<std::mutex> lock(gInboxLock);
      gInbox.push_back({self, sender, std::string(reinterpret_cast<char const*>(p + kHeaderBytes), n), (p[4] & kFlagHello) != 0});
    }
  }
  return true;
}

void HookOnReceive(Il2CppObject* self, NetSpan message, NetDid sender, MethodInfo const* method) {
  if (TakePacket(self, message, sender)) return;  // наш пакет — игре он не нужен
  gOrigOnReceive(self, message, sender, method);
}

// ---------- меню (левый стик) ----------
// Панель над левой рукой: только класс, за который играешь, его скины — плитками по две в ряд
// (первая — родной вид). Наведи правый контроллер на плитку и жми курок. Тексты — по-английски.
// Позы рук — как в TimeControl: InputDevices дают их в пространстве трекинга,
// в мир переводим через камеру: rig = camera * inverse(head).

constexpr int kHead = 3;       // XRNode.Head
constexpr int kLeftHand = 4;   // XRNode.LeftHand
constexpr int kRightHand = 5;  // XRNode.RightHand
constexpr float kFontSize = 0.12f;
constexpr float kPanelWidth = 0.21f;
constexpr float kPanelTop = 0.097f, kPanelMargin = 0.010f, kPadX = 0.012f;
constexpr float kHeaderY = 0.083f;
constexpr float kPanelAboveHand = 0.15f;  // м над левой рукой (было 0.13 — низковато)
constexpr int kColumns = 2;
// Плитки — страницами по kMaxTiles (2 × 2); первая плитка первой страницы — родной вид.
// Страниц больше одной — под плитками «<», номер страницы и «>»
constexpr int kMaxTiles = 4;
constexpr float kTileWidth = 0.090f, kTileHeight = 0.030f, kTileGap = 0.006f;
constexpr float kNavY = 0.054f - 2 * (kTileHeight + kTileGap);  // ряд стрелок — на месте третьего ряда плиток
constexpr float kNavX = 0.066f, kNavWidth = 0.036f, kNavHeight = 0.022f;
// ТЕСТ: столько кнопок-заглушек («Test 1»…) добавляется к скинам — проверить страницы. Перед релизом — 0
constexpr int kTestTiles = 0;
constexpr float kBorder = 0.0015f;  // толщина контура панели и надетой плитки
constexpr float kPanelRadius = 0.008f, kTileRadius = 0.005f;

// Цвета (вариант «графит»), #RRGGBBAA
constexpr char const* kColPanel = "121314FF";
constexpr char const* kColPanelLine = "FFFFFF70";
constexpr char const* kColTile = "222427FF";
constexpr char const* kColTileHover = "2E3135FF";
constexpr char const* kColWorn = "3A3D42FF";
constexpr char const* kColWornLine = "FFFFFFFF";

// Надписи: класс (слева сверху), подсказка (справа сверху), заметка, по одной на плитку, курсор
enum { kClassText, kHintText, kNoteText, kPrevText, kNextText, kPageText, kTileTexts };
constexpr int kCursorText = kTileTexts + kMaxTiles;
constexpr int kTextCount = kCursorText + 1;

// Фон и контуры. Шейдеров, кроме TextMeshPro, в сборке для Quest нет (квад с Unlit не виден),
// поэтому прямоугольник — надпись TMP из одного невидимого символа в <mark>: подложку TMP рисует
// своим шейдером. Её размер зависит от шрифта — меряем один раз по границам меша и дальше тянем
// масштабом. Скруглённая фигура — три прямоугольника крестом и «ступенькой» на месте углов,
// контур — такая же фигура побольше под непрозрачной заливкой. Порядок отрисовки — sortingOrder:
// прозрачное по расстоянию до камеры сортируется ненадёжно (плитки ушли бы под панель).
// Курсор — такой же кружок из трёх прямоугольников (белая точка на тёмной обводке), поверх надписей;
// без фона — буква «o».
enum { kPanelOutline, kPanelFill, kWornOutline, kTileFills };
constexpr int kPrevFill = kTileFills + kMaxTiles, kNextFill = kPrevFill + 1;  // кнопки страниц
constexpr int kCursorOutline = kNextFill + 1, kCursorDot = kCursorOutline + 1;
constexpr int kShapeCount = kCursorDot + 1;
constexpr int kParts = 3;  // прямоугольников в фигуре
constexpr int kRectCount = kShapeCount * kParts;
// Порядок отрисовки фигур: панель, контур надетой, плитки; надписи — 14; курсор — 15 и 16
int ShapeOrder(int shape) {
  if (shape == kCursorOutline) return 15;
  if (shape == kCursorDot) return 16;
  return shape < kTileFills ? 10 + shape : 13;
}
constexpr float kCursorRadius = 0.0028f, kCursorBorder = 0.0008f;
constexpr char const* kColCursor = "FFFFFFFF";
constexpr char const* kColCursorLine = "121314D9";
Vec3 gCursorLocal;  // курсор на панели (локальные x, y)

// Луч, как в меню Meta: белая линия от руки к курсору, тает к концу и не доходит до курсора — только
// показывает направление. Отрезки — такие же подложки <mark>, каждый повёрнут вдоль луча лицом к камере.
// Отдельный корень в мире (не под панелью: у панели свой поворот и масштаб).
constexpr int kRaySegments = 8;
constexpr float kRayStart = 0.04f;        // м от руки до начала линии (из середины руки не торчит)
constexpr float kRayGap = 0.03f;          // м, на сколько линия не доходит до курсора
constexpr float kRayIdleLength = 0.30f;   // м — длина, когда луч мимо панели
constexpr float kRayWidthStart = 0.0024f, kRayWidthEnd = 0.0012f;
constexpr float kRayAlpha = 0.55f;        // у руки; к концу — почти 0
Held gRayRoot;
Il2CppObject* gRaySegments[kRaySegments];
Il2CppObject* gRayTransforms[kRaySegments];
bool gRayShown;
bool gHubRay;  // луч сейчас рисует экран терминала в хабе (hub.inc) — меню на руке его не прячет

// Видимая правая рука класса — откуда идёт луч. У «физических» классов (Golem, Boulder, Roller…) рука
// стоит далеко от контроллера, и луч от контроллера шёл «не оттуда». Узел руки — с PoseInterpolator
// (у трекера контроллера «Right Hand Pose» — TransformTracker); имена — по дампам классов.
Il2CppObject* gPoseInterpolatorType;
Held gHandClass;  // класс, для которого искали руку
Held gRightHand;
bool gHandLogged;

std::string MarkText(char const* color);

struct Bounds {
  Vec3 center, extents;
};

Il2CppObject* gTexts[kTextCount];
std::string gShown[kTextCount];
Vec3 gTextPos[kTextCount];
Il2CppObject* gRects[kRectCount];
Il2CppObject* gRectTransforms[kRectCount];
std::string gRectShown[kRectCount];
Vec3 gRectScale[kRectCount], gRectPos[kRectCount];
MethodInfo const* gTextBounds;            // TMP_Text.get_bounds
MethodInfo const* gMeshBounds;            // Mesh.get_bounds — если первого нет
MethodInfo const* gTmpSortingOrder;       // TextMeshPro.set_sortingOrder
MethodInfo const* gRendererSortingOrder;  // Renderer.set_sortingOrder — если первого нет
MethodInfo const* gSetSizeDelta;          // RectTransform.set_sizeDelta — для надписей у краёв панели
float gTileFont = kFontSize;
bool gHasBackground;                      // фон рисуем (нашлись границы меша и порядок отрисовки)
bool gMarkMeasured;
Vec3 gMarkCenter, gMarkSize;  // подложка <mark> в локальных координатах надписи
int gMeasureFrames;
Held gMenuRoot;
Il2CppObject* gMenuTransform;
Il2CppObject* gCursorTransform;
bool gMenuFailed, gMenuVisible, gStickWas, gTriggerWas, gUsePointerPose = true;
float gMenuScale = 1.f;
std::vector<SkinFile*> gTiles;       // скины на плитках 1..N (плитка 0 — родной вид)
std::vector<std::string> gTestNames;  // ТЕСТ: заглушки после скинов (kTestTiles)
std::string gTestPick;               // ТЕСТ: выбранная заглушка — только подсветка, в skins.cfg не пишется
int gPage;                           // текущая страница
int gHovered = -1;                   // ячейка страницы под лучом (0…kMaxTiles-1), -1 — нет
int gHoverNav;                       // кнопка страниц под лучом: -1 «<», +1 «>», 0 — нет
bool gOnPanel;                  // луч на панели — показываем курсор

uint64_t Device(int node) { return Call<uint64_t>(gGetDeviceId, node); }

bool Button(int node, Il2CppString* usage) {
  bool value = false;
  return Call<bool>(gGetButton, Device(node), usage, &value) && value;
}

bool Pose(int node, Il2CppString* posUsage, Il2CppString* rotUsage, Vec3& pos, Quat& rot) {
  auto id = Device(node);
  return Call<bool>(gGetVec3, id, posUsage, &pos) && Call<bool>(gGetQuat, id, rotUsage, &rot);
}

void Buzz(int node, float amplitude, float duration) {
  if (gHaptic) Call<bool>(gHaptic, Device(node), uint32_t{0}, amplitude, duration);
}

Il2CppObject* NewGameObject(char const* name, Il2CppObject* parent) {
  auto go = KeepNew(il2cpp_object_new(gGameObjectClass));
  Invoke(gGameObjectCtor, go, il2cpp_string_new(name));
  if (parent) {
    bool keepWorld = false;
    Invoke(gSetParent, Call<Il2CppObject*>(gGameObjectTransform, go), parent, &keepWorld);
  }
  return go;
}

Il2CppObject* FindFont() {
  if (gDefaultFont)
    if (auto font = Invoke(gDefaultFont, nullptr)) return font;
  // Нет шрифта в настройках TMP — берём любой загруженный
  auto all = reinterpret_cast<Il2CppArray*>(Invoke(gFindAllOfType, nullptr, gFontAssetType));
  return Length(all) > 0 ? At(all, 0) : nullptr;
}

// Пиксельный шрифт меню — Pixelify Sans (лицензия OFL): бандл MenuFont.font рядом со скинами,
// собирает Skins\Unity (Tools → Skins → Build Menu Font). Нет файла или не загрузился — шрифт игры.
// Шейдер материала берём у шрифта игры: в бандле он без стерео-вариантов и рисовался бы одним глазом.
// В атласе только ASCII — имена с другими буквами пишем шрифтом игры.
constexpr char kMenuFontFile[] = "MenuFont.font";
constexpr char kMenuFontAsset[] = "PixelifySans SDF";
MethodInfo const* gFontMaterial;  // TMP_Asset.get_material
MethodInfo const* gGetShader;     // Material.get_shader / set_shader
MethodInfo const* gSetShader;
Held gFontBundle, gMenuFont;
bool gMenuFontFailed;
Il2CppObject* gGameFont;  // шрифт игры — для имён не латиницей
Il2CppObject* gTileFontObj[kMaxTiles];

Il2CppObject* MenuFont(Il2CppObject* gameFont) {
  if (gMenuFontFailed) return nullptr;
  if (Alive(gMenuFont.obj)) return gMenuFont.obj;
  Free(gMenuFont);
  auto fail = [](char const* why) -> Il2CppObject* {
    gMenuFontFailed = true;
    LOG(ANDROID_LOG_WARN, "шрифт меню: %s — шрифт игры", why);
    return nullptr;
  };
  if (!gFontMaterial || !gGetShader || !gSetShader) return fail("нет get_material/shader");
  if (!Alive(gFontBundle.obj)) {
    Free(gFontBundle);
    std::string path = std::string(kSkinDir) + kMenuFontFile;
    if (FILE* f = fopen(path.c_str(), "rb")) {
      fclose(f);
    } else {
      return fail("нет файла MenuFont.font");
    }
    gFontBundle = Hold(LoadBundle(path));
    if (!gFontBundle.obj) return fail("бандл не загрузился");
  }
  auto font = LoadAsset(gFontBundle.obj, kMenuFontAsset, gFontAssetType);
  if (!font) return fail("нет шрифта в бандле");
  auto ours = Invoke(gFontMaterial, font);
  auto theirs = gameFont ? Invoke(gFontMaterial, gameFont) : nullptr;
  auto shader = theirs ? Invoke(gGetShader, theirs) : nullptr;
  if (!ours || !shader) return fail("нет материала или шейдера");
  Invoke(gSetShader, ours, shader);
  gMenuFont = Hold(font);
  LOG(ANDROID_LOG_INFO, "шрифт меню: Pixelify Sans");
  return font;
}

bool IsAscii(std::string const& s) {
  for (unsigned char c : s)
    if (c >= 0x80) return false;
  return true;
}

// Центр плитки на панели
Vec3 TilePos(int i) {
  float x = (i % kColumns - (kColumns - 1) / 2.f) * (kTileWidth + kTileGap);
  float y = 0.054f - (i / kColumns) * (kTileHeight + kTileGap);
  return {x, y, 0};
}

int PageCount(int count) { return std::max(1, (count + kMaxTiles - 1) / kMaxTiles); }

// Низ панели: под рядом стрелок, если страниц несколько, иначе под последним рядом плиток
// (без класса — под заметкой). Высота не меняется от страницы к странице
float PanelBottom(int count) {
  if (count == 0) return 0.040f;
  if (PageCount(count) > 1) return kNavY - kNavHeight / 2 - kPanelMargin;
  return TilePos(std::min(count, kMaxTiles) - 1).y - kTileHeight / 2 - kPanelMargin;
}

void SetSorting(Il2CppObject* text, int32_t order) {
  if (gTmpSortingOrder) {
    Invoke(gTmpSortingOrder, text, &order);
  } else if (auto renderer = Component(GameObjectOf(text), gRendererType)) {
    Invoke(gRendererSortingOrder, renderer, &order);
  }
}

constexpr int kAlignCenter = 514, kAlignLeft = 513, kAlignRight = 516;  // TextAlignmentOptions

struct Vec2 {
  float x, y;
};

// Надпись TMP с общими настройками меню. Выравнивание по краю — внутри рамки width × 0.02 с центром в pos
// (у 3D-надписи TMP рамка по умолчанию 20 × 5 м); без set_sizeDelta — по центру
Il2CppObject* NewText(Il2CppObject* font, Vec3 const& pos, int32_t order, int align = kAlignCenter, float width = 0,
                      Il2CppObject* parent = nullptr) {
  int noWrap = 0;  // TextWrappingModes.NoWrap
  float size = kFontSize;
  if (align != kAlignCenter && !gSetSizeDelta) align = kAlignCenter;
  auto go = NewGameObject("SkinsText", parent ? parent : gMenuTransform);
  auto text = KeepNew(Invoke(gAddComponent, go, gTextMeshProType));
  if (!text) return nullptr;
  auto t = Call<Il2CppObject*>(gGameObjectTransform, go);
  Invoke(gSetFont, text, font);
  Invoke(gSetFontSize, text, &size);
  Invoke(gSetAlignment, text, &align);
  Invoke(gSetWrapping, text, &noWrap);
  if (align != kAlignCenter) {
    Vec2 box{width, 0.02f};
    Invoke(gSetSizeDelta, t, &box);
  }
  Invoke(gSetLocalPosition, t, &pos);
  if (gHasBackground) SetSorting(text, order);
  return text;
}

bool BuildMenu() {
  gGameFont = FindFont();
  if (!gGameFont) return false;  // сцена ещё грузится, попробуем позже
  Keep(gGameFont);
  auto pixel = MenuFont(gGameFont);
  auto font = pixel ? pixel : gGameFont;
  for (auto& f : gTileFontObj) f = font;

  auto root = NewGameObject("SkinsMenu", nullptr);
  gMenuRoot = Hold(root);
  Invoke(gDontDestroyOnLoad, nullptr, root);
  gMenuTransform = Keep(Call<Il2CppObject*>(gGameObjectTransform, root));

  // Класс и подсказка — у левого и правого края, по половине ширины панели на каждую
  float half = kPanelWidth / 2 - kPadX;
  gTextPos[kClassText] = {-half / 2, kHeaderY, 0};
  gTextPos[kHintText] = {half / 2, kHeaderY, 0};
  if (!gSetSizeDelta) gTextPos[kClassText].x = 0;  // по центру; подсказки тогда нет — места на обе не хватит
  gTileFont = kFontSize;
  gTextPos[kNoteText] = {0, 0.062f, 0};
  for (int i = 0; i < kMaxTiles; i++) gTextPos[kTileTexts + i] = TilePos(i);
  gTextPos[kPrevText] = {-kNavX, kNavY, 0};
  gTextPos[kNextText] = {kNavX, kNavY, 0};
  gTextPos[kPageText] = {0, kNavY, 0};
  gTextPos[kCursorText] = {0, 0, 0};

  if (gHasBackground)
    for (int i = 0; i < kRectCount; i++) {
      int shape = i / kParts;
      gRects[i] = NewText(font, {0, 0, 0}, ShapeOrder(shape));
      if (!gRects[i]) break;
      gRectTransforms[i] = Keep(Call<Il2CppObject*>(gComponentTransform, gRects[i]));
      gRectShown[i].clear();
      gRectScale[i] = {1, 1, 1};
      gRectPos[i] = {0, 0, 0};
    }
  for (int i = 0; i < kTextCount; i++) {
    int align = i == kClassText ? kAlignLeft : i == kHintText ? kAlignRight : kAlignCenter;
    gTexts[i] = NewText(font, gTextPos[i], i == kCursorText ? 15 : 14, align, half);
    if (!gTexts[i]) {
      gMenuFailed = true;
      LOG(ANDROID_LOG_ERROR, "не удалось добавить TextMeshPro — меню не будет");
      return false;
    }
    gShown[i].clear();
  }
  gCursorTransform = Keep(Call<Il2CppObject*>(gComponentTransform, gTexts[kCursorText]));
  bool hidden = false;
  if (gHasBackground && !Alive(gRayRoot.obj)) {
    Free(gRayRoot);
    auto ray = NewGameObject("SkinsRay", nullptr);
    gRayRoot = Hold(ray);
    Invoke(gDontDestroyOnLoad, nullptr, ray);
    auto rayTransform = Call<Il2CppObject*>(gGameObjectTransform, ray);
    for (int i = 0; i < kRaySegments; i++) {
      gRaySegments[i] = NewText(font, {0, 0, 0}, 17, kAlignCenter, 0, rayTransform);  // поверх панели
      if (!gRaySegments[i]) break;
      gRayTransforms[i] = Keep(Call<Il2CppObject*>(gComponentTransform, gRaySegments[i]));
      char color[16];
      snprintf(color, sizeof color, "FFFFFF%02X", static_cast<int>(255 * kRayAlpha * (1.f - float(i) / kRaySegments)));
      Invoke(gSetText, gRaySegments[i], il2cpp_string_new(MarkText(color).c_str()));
    }
    Invoke(gSetActive, ray, &hidden);
    gRayShown = false;
  }
  Invoke(gSetActive, root, &hidden);
  gMenuVisible = false;
  return true;
}

void SetText(int i, std::string const& text) {
  if (gShown[i] == text) return;
  gShown[i] = text;
  Invoke(gSetText, gTexts[i], il2cpp_string_new(text.c_str()));
}

bool Same(Vec3 a, Vec3 b) { return std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z) < 1e-6f; }

void SetRectText(int i, std::string const& text) {
  if (gRectShown[i] == text) return;
  gRectShown[i] = text;
  Invoke(gSetText, gRects[i], il2cpp_string_new(text.c_str()));
}

std::string MarkText(char const* color) {
  return std::string("<mark=#") + color + "><color=#00000000>.</color></mark>";
}

// Прямоугольник цвета color (#RRGGBBAA без «#»; nullptr — спрятать) от (x0, y0) до (x1, y1)
void PlaceRect(int i, char const* color, float x0, float y0, float x1, float y1) {
  SetRectText(i, color ? MarkText(color) : "");
  if (!color) return;
  Vec3 s{(x1 - x0) / gMarkSize.x, (y1 - y0) / gMarkSize.y, 1};
  Vec3 p{(x0 + x1) / 2 - gMarkCenter.x * s.x, (y0 + y1) / 2 - gMarkCenter.y * s.y, 0};
  if (!Same(s, gRectScale[i])) {
    gRectScale[i] = s;
    Invoke(gSetLocalScale, gRectTransforms[i], &s);
  }
  if (!Same(p, gRectPos[i])) {
    gRectPos[i] = p;
    Invoke(gSetLocalPosition, gRectTransforms[i], &p);
  }
}

// Скруглённая фигура: крест из двух прямоугольников (без углов радиуса r) и третий — со срезом 0.29·r
// (точка дуги на 45°): на месте каждого угла две маленькие ступеньки
void PlaceShape(int shape, char const* color, float x0, float y0, float x1, float y1, float r) {
  float c = r * 0.29f;
  int i = shape * kParts;
  PlaceRect(i + 0, color, x0, y0 + r, x1, y1 - r);
  PlaceRect(i + 1, color, x0 + r, y0, x1 - r, y1);
  PlaceRect(i + 2, color, x0 + c, y0 + c, x1 - c, y1 - c);
}

// Кружок радиуса r: три прямоугольника, вписанных в окружность (полуширина и полувысота — точки дуги
// на ~24°, 45° и ~66°), — на размере курсора это ровная точка
void PlaceDot(int shape, char const* color, float cx, float cy, float r) {
  int i = shape * kParts;
  float a = r * 0.92f, b = r * 0.40f, c = r * 0.71f;
  PlaceRect(i + 0, color, cx - a, cy - b, cx + a, cy + b);
  PlaceRect(i + 1, color, cx - b, cy - a, cx + b, cy + a);
  PlaceRect(i + 2, color, cx - c, cy - c, cx + c, cy + c);
}

// Подложка <mark> из одной точки — по границам меша надписи (TMP строит меш при отрисовке, поэтому не сразу)
// Границы меша надписи в её локальных координатах (нули — меша ещё нет)
Bounds TextBounds(Il2CppObject* text) {
  if (gTextBounds) return Call<Bounds>(gTextBounds, text);
  if (!gMeshBounds) return {};
  auto filter = Component(GameObjectOf(text), gMeshFilterType);
  auto mesh = filter ? Call<Il2CppObject*>(gGetSharedMesh, filter) : nullptr;
  return mesh ? Call<Bounds>(gMeshBounds, mesh) : Bounds{};
}

bool MeasureMark() {
  Bounds b = TextBounds(gRects[kPanelFill * kParts]);
  if (!(b.extents.x > 1e-5f && b.extents.y > 1e-5f)) return false;
  gMarkCenter = b.center;
  gMarkSize = b.extents * 2.f;
  LOG(ANDROID_LOG_INFO, "подложка <mark>: %.4f × %.4f, центр (%.4f, %.4f)", gMarkSize.x, gMarkSize.y, gMarkCenter.x,
      gMarkCenter.y);
  return true;
}

void HideRects() {
  for (int i = 0; i < kRectCount; i++)
    if (gRects[i]) SetRectText(i, "");
}

void UpdateBackground(int count, int worn) {
  if (!gHasBackground || !gRects[kRectCount - 1]) return;
  if (!gMarkMeasured) {
    SetRectText(kPanelFill * kParts, MarkText("00000000"));  // невидимая — только чтобы измерить (остальные пустые)
    if (MeasureMark()) {
      gMarkMeasured = true;
    } else if (++gMeasureFrames > 300) {
      gHasBackground = false;
      HideRects();
      LOG(ANDROID_LOG_WARN, "не удалось измерить подложку <mark> — меню без фона");
      return;
    } else {
      return;
    }
  }

  float x = kPanelWidth / 2, bottom = PanelBottom(count), b = kBorder;
  PlaceShape(kPanelOutline, kColPanelLine, -x - b, bottom - b, x + b, kPanelTop + b, kPanelRadius + b);
  PlaceShape(kPanelFill, kColPanel, -x, bottom, x, kPanelTop, kPanelRadius);
  float w = kTileWidth / 2, h = kTileHeight / 2;
  if (worn >= 0 && worn < count) {
    Vec3 c = TilePos(worn);
    PlaceShape(kWornOutline, kColWornLine, c.x - w - b, c.y - h - b, c.x + w + b, c.y + h + b, kTileRadius + b);
  } else {
    PlaceShape(kWornOutline, nullptr, 0, 0, 0, 0, 0);
  }
  int onPage = std::min(kMaxTiles, count - gPage * kMaxTiles);  // плиток на этой странице
  for (int i = 0; i < kMaxTiles; i++) {
    Vec3 c = TilePos(i);
    char const* color = i >= onPage ? nullptr : i == worn ? kColWorn : i == gHovered ? kColTileHover : kColTile;
    PlaceShape(kTileFills + i, color, c.x - w, c.y - h, c.x + w, c.y + h, kTileRadius);
  }
  bool paged = PageCount(count) > 1;
  float nw = kNavWidth / 2, nh = kNavHeight / 2;
  for (int side : {-1, 1}) {
    char const* color = !paged ? nullptr : gHoverNav == side ? kColTileHover : kColTile;
    float cx = side * kNavX;
    PlaceShape(side < 0 ? kPrevFill : kNextFill, color, cx - nw, kNavY - nh, cx + nw, kNavY + nh, kTileRadius);
  }
  Vec3 p = gCursorLocal;
  PlaceDot(kCursorOutline, gOnPanel ? kColCursorLine : nullptr, p.x, p.y, kCursorRadius + kCursorBorder);
  PlaceDot(kCursorDot, gOnPanel ? kColCursor : nullptr, p.x, p.y, kCursorRadius);
}

std::string Title(std::string const& key) {
  std::string s = key;
  for (size_t i = 1; i < s.size(); i++) s[i] = static_cast<char>(tolower(static_cast<unsigned char>(s[i])));
  return s;
}

int TileCount() { return gLocal.key.empty() ? 0 : static_cast<int>(gTiles.size() + gTestNames.size()) + 1; }

bool IsTestTile(int i) { return i > static_cast<int>(gTiles.size()); }

std::string TileName(int i) {
  if (i == 0) return "Original";
  return IsTestTile(i) ? gTestNames[i - 1 - gTiles.size()] : gTiles[i - 1]->name;
}

// Что подсвечено как надетое: тестовая заглушка (если выбрана) или скин из skins.cfg
std::string ShownSelection() {
  if (!gTestPick.empty()) return gTestPick;
  return gLocal.key.empty() ? "" : Selected(gLocal.key);
}

// ---- шрифт плиток ----
// У всех плиток один размер шрифта — чтобы длинное имя рядом с коротким не выглядело другим шрифтом.
// Сначала прикидываем ширину по таблице букв Arial/Liberation Sans (em = 0.1 размера шрифта у 3D-надписи TMP),
// потом, когда TMP построит меш, сверяем с настоящей шириной и, если не влезло, уменьшаем.
constexpr float kTextRoom = kTileWidth - 2 * 0.008f;  // ширина плитки без полей
constexpr float kMinFont = kFontSize * 0.6f;
int gFitWait;  // кадров до сверки: границы меша отстают от нового размера шрифта

float CharEm(unsigned char c) {
  if (c == ' ' || strchr("iljtfrI.,'", c)) return 0.28f;
  if (strchr("mwMW", c)) return 0.85f;
  if (isupper(c)) return 0.68f;
  return 0.54f;  // строчные, цифры, прочее (и байты не-ASCII — с запасом)
}

float EstimateFont() {
  float font = kFontSize;
  for (int i = 0; i < TileCount(); i++) {
    float em = 0;
    for (unsigned char c : TileName(i)) em += CharEm(c);
    if (gMenuFont.obj) em *= 1.1f;  // Pixelify Sans чуть шире Arial
    if (em > 0) font = std::min(font, kTextRoom / (em * 0.1f));
  }
  return std::max(font, kMinFont);
}

void ApplyTileFont() {
  for (int i = 0; i < kMaxTiles; i++) Invoke(gSetFontSize, gTexts[kTileTexts + i], &gTileFont);
  gFitWait = 2;
}

// Сверка с настоящей шириной имён (только уменьшаем — иначе шрифт «прыгал» бы туда-сюда)
void FitTiles(int count) {
  if (!gTextBounds && !gMeshBounds) return;
  if (gFitWait > 0) {
    gFitWait--;
    return;
  }
  float font = gTileFont;
  for (int i = 0; i < std::min(kMaxTiles, count - gPage * kMaxTiles); i++) {
    float w = TextBounds(gTexts[kTileTexts + i]).extents.x * 2;
    if (w > kTextRoom) font = std::min(font, gTileFont * kTextRoom / w);
  }
  font = std::max(font, kMinFont);
  if (font < gTileFont - 1e-4f) {
    LOG(ANDROID_LOG_INFO, "шрифт плиток %.3f → %.3f (по ширине имён)", gTileFont, font);
    gTileFont = font;
    ApplyTileFont();
  }
}

// Скины текущего класса на плитках
void RefreshTiles() {
  gTiles.clear();
  gTestNames.clear();
  gTestPick.clear();
  if (!gLocal.key.empty()) {
    gTiles = SkinsFor(gLocal.key);
    // ТЕСТ: имена разной длины — проверить и страницы, и подгонку шрифта
    static char const* const kTestNames[] = {"Test 1", "Test Two", "T3", "Test Number 4", "Test 5"};
    for (int i = 0; i < kTestTiles; i++) {
      std::string name = kTestNames[i % 5];
      if (i >= 5) name += " b";
      gTestNames.push_back(name);
    }
  }
  // Открываем на странице надетого скина
  gPage = 0;
  std::string selected = ShownSelection();
  for (int i = 1; i < TileCount(); i++)
    if (TileName(i) == selected) gPage = i / kMaxTiles;
  if (Alive(gMenuRoot.obj)) {
    gTileFont = EstimateFont();
    ApplyTileFont();
  }
}

// Надпись плитки. С фоном — просто цвет текста (без жирного: ширина имени не меняется при выборе);
// без фона — полупрозрачная <mark> поверх (TMP рисует её над текстом, поэтому только слабая)
std::string Tile(std::string const& name, bool worn, bool hovered) {
  if (gHasBackground && gMarkMeasured)
    return (worn || hovered ? "<color=#FFFFFF>" : "<color=#E6E7E9>") + name + "</color>";
  std::string pad = "  ";
  std::string text = "<color=#E6E7E9>" + pad + name + pad + "</color>";
  if (worn) return "<mark=#FFFFFF40>" + text + "</mark>";
  if (hovered) return "<mark=#FFFFFF20>" + text + "</mark>";
  return text;
}

void UpdateTexts() {
  int count = TileCount();
  SetText(kClassText, count == 0 ? "<color=#FFFFFF>Skins</color>" : "<color=#FFFFFF>" + Title(gLocal.key) + "</color>");
  SetText(kHintText, gSetSizeDelta ? "<color=#8C8F94><size=70%>aim + trigger</size></color>" : "");
  SetText(kNoteText, count == 0   ? "<color=#8C8F94><size=80%>Spawn as a class\nto pick its skin</size></color>"
                     : count == 1 ? "<color=#8C8F94><size=70%>No skins yet</size></color>"
                                  : "");
  // Без класса заметка — посередине панели, с одной плиткой — на месте второй
  Vec3 notePos = count == 1 ? TilePos(1) : Vec3{0, 0.062f, 0};
  if (!Same(notePos, gTextPos[kNoteText])) {
    gTextPos[kNoteText] = notePos;
    Invoke(gSetLocalPosition, Call<Il2CppObject*>(gComponentTransform, gTexts[kNoteText]), &notePos);
  }
  // Стрелки и номер страницы; у края — тусклая стрелка, она не нажимается
  int pages = PageCount(count);
  if (count > 0 && gPage >= pages) gPage = pages - 1;
  auto arrow = [&](char const* glyph, bool enabled) {
    return pages > 1 ? std::string(enabled ? "<color=#FFFFFF>" : "<color=#55585D>") + glyph + "</color>" : "";
  };
  SetText(kPrevText, arrow("<", gPage > 0));
  SetText(kNextText, arrow(">", gPage < pages - 1));
  SetText(kPageText, pages > 1 ? "<color=#8C8F94><size=80%>" + std::to_string(gPage + 1) + "/" +
                                      std::to_string(pages) + "</size></color>"
                               : "");

  std::string selected = ShownSelection();
  int worn = -1;  // ячейка надетого на этой странице
  for (int i = 0; i < kMaxTiles; i++) {
    int index = gPage * kMaxTiles + i;
    if (index >= count) {
      SetText(kTileTexts + i, "");
      continue;
    }
    std::string name = TileName(index);
    bool on = index == 0 ? selected.empty() : name == selected;
    if (on) worn = i;
    auto font = Alive(gMenuFont.obj) && IsAscii(name) ? gMenuFont.obj : gGameFont;
    if (font != gTileFontObj[i]) {
      gTileFontObj[i] = font;
      Invoke(gSetFont, gTexts[kTileTexts + i], font);
      gFitWait = 2;
    }
    SetText(kTileTexts + i, Tile(name, on, gHovered == i));
  }
  bool dotCursor = gHasBackground && gMarkMeasured;  // кружок из подложек; без фона — буква
  SetText(kCursorText, gOnPanel && !dotCursor ? "<color=#FFFFFF>o</color>" : "");
  FitTiles(count);
  UpdateBackground(count, worn);
}

void Pick(int tile) {
  std::string const& key = gLocal.key;
  if (IsTestTile(tile)) {  // ТЕСТ: заглушка — только подсветить
    gTestPick = TileName(tile);
    LOG(ANDROID_LOG_INFO, "тест: выбрана заглушка «%s»", gTestPick.c_str());
    return;
  }
  gTestPick.clear();
  gConfig[key] = tile == 0 ? "" : gTiles[tile - 1]->name;
  WriteConfig();
  LOG(ANDROID_LOG_INFO, "%s: %s", key.c_str(), gConfig[key].empty() ? "родной вид" : gConfig[key].c_str());
  Wear(gLocal, gConfig[key], "свой");
}

struct HandNode {
  Il2CppObject* t;
  std::string name;
  int depth;
};

void WalkNodes(Il2CppObject* t, int depth, std::vector<HandNode>& out) {
  out.push_back({t, NameOf(t), depth});
  for (int i = 0, n = ChildCount(t); i < n; i++) WalkNodes(Child(t, i), depth + 1, out);
}

// Узел видимой правой руки: по имени (по порядку) и с PoseInterpolator; без типа PoseInterpolator —
// самый глубокий узел с таким именем (трекер контроллера с тем же именем у Styx лежит выше руки)
Il2CppObject* FindRightHand(Il2CppObject* classObject) {
  std::vector<HandNode> nodes;
  WalkNodes(TransformOf(classObject), 0, nodes);
  for (char const* name : {"Right Hand Anchor", "Railgun Pose", "Right Arm", "Right Hand"}) {
    Il2CppObject* best = nullptr;
    int bestDepth = -1;
    for (auto& n : nodes) {
      if (n.name != name) continue;
      if (gPoseInterpolatorType) {
        if (Component(GameObjectOf(n.t), gPoseInterpolatorType)) return n.t;
      } else if (n.depth > bestDepth) {
        best = n.t;
        bestDepth = n.depth;
      }
    }
    if (best) return best;
  }
  return nullptr;
}

// Рука текущего класса (ищем заново, когда класс сменился или рука уничтожена); nullptr — луч от контроллера
Il2CppObject* RightHand() {
  if (!Alive(gLocal.cls.obj)) return nullptr;
  if (gHandClass.obj != gLocal.cls.obj || (gRightHand.obj && !Alive(gRightHand.obj))) {
    Free(gHandClass);
    Free(gRightHand);
    gHandClass = Hold(gLocal.cls.obj);
    gRightHand = Hold(FindRightHand(gLocal.cls.obj));
    gHandLogged = false;
    LOG(ANDROID_LOG_INFO, "%s: рука для луча — %s", gLocal.key.c_str(),
        gRightHand.obj ? NameOf(gRightHand.obj).c_str() : "не найдена, луч от контроллера");
  }
  return gRightHand.obj;
}

// Линия от a по направлению dir длиной len (0 — спрятать): отрезки тоньше и прозрачнее к концу
void DrawRay(Vec3 a, Vec3 dir, float len, Vec3 camPos, float scale) {
  if (!gRayRoot.obj || !gRaySegments[kRaySegments - 1] || !gMarkMeasured) return;
  bool show = len > 0.02f * scale && Alive(gRayRoot.obj);
  if (show != gRayShown && Alive(gRayRoot.obj)) {
    gRayShown = show;
    Invoke(gSetActive, gRayRoot.obj, &show);
  }
  if (!show) return;
  float step = len / kRaySegments;
  for (int i = 0; i < kRaySegments; i++) {
    Vec3 c = a + dir * (step * (i + 0.5f));
    Vec3 f = c - camPos;
    f = Normalize(f - dir * Dot(f, dir));  // лицом к камере, длиной — вдоль луча
    Quat rot = LookRotation(f, Cross(f, dir));
    float width = scale * (kRayWidthStart + (kRayWidthEnd - kRayWidthStart) * i / (kRaySegments - 1));
    Vec3 s{step / gMarkSize.x, width / gMarkSize.y, 1};
    Vec3 p = c - Rotate(rot, Vec3{gMarkCenter.x * s.x, gMarkCenter.y * s.y, 0});
    Call<void>(gSetPosition, gRayTransforms[i], p);
    Call<void>(gSetRotation, gRayTransforms[i], rot);
    Invoke(gSetLocalScale, gRayTransforms[i], &s);
  }
}

void TickMenu() {
  if (gMenuFailed || !gGetVec3 || !gGetQuat || !gSkinsLoaded) return;
  if (!Alive(gMenuRoot.obj)) {
    Free(gMenuRoot);
    if (!BuildMenu()) return;
  }

  auto camera = Call<Il2CppObject*>(gGetMainCamera);
  if (!camera) return;
  auto camTransform = Call<Il2CppObject*>(gComponentTransform, camera);
  Vec3 camPos = Call<Vec3>(gGetPosition, camTransform);
  Quat camRot = Call<Quat>(gGetRotation, camTransform);

  // Масштаб игрока: у больших тел (голем) вся система трекинга увеличена
  float scale = gGetLossyScale ? Call<Vec3>(gGetLossyScale, camTransform).x : 1.f;
  if (!(scale > 0.01f)) scale = 1.f;

  Vec3 headPos{}, leftPos{};
  Quat headRot{0, 0, 0, 1}, leftRot{0, 0, 0, 1};
  bool tracked = Pose(kHead, gDevicePosition, gDeviceRotation, headPos, headRot) &&
                 Pose(kLeftHand, gDevicePosition, gDeviceRotation, leftPos, leftRot);
  Quat rigRot = camRot * Conj(headRot);
  Vec3 rigPos = camPos - Rotate(rigRot, headPos * scale);

  // Нажатие левого стика — открыть / закрыть меню
  bool stick = Button(kLeftHand, gStickClick);
  if (stick && !gStickWas) {
    gMenuVisible = !gMenuVisible;
    if (gMenuVisible) RefreshTiles();
    Invoke(gSetActive, gMenuRoot.obj, &gMenuVisible);
    Buzz(kLeftHand, 0.3f, 0.05f);
  }
  gStickWas = stick;

  bool trigger = Button(kRightHand, gTriggerButton);
  bool triggerDown = trigger && !gTriggerWas;
  gTriggerWas = trigger;
  if (!gMenuVisible || !tracked) {
    gHovered = -1;
    gHoverNav = 0;
    gOnPanel = false;
    if (!gHubRay) DrawRay({}, {0, 0, 1}, 0, camPos, scale);
    return;
  }

  // Панель смотрит на игрока и растёт вместе с ним
  Vec3 panelPos = rigPos + Rotate(rigRot, leftPos * scale) + Vec3{0, kPanelAboveHand * scale, 0};
  Quat panelRot = LookRotation(Normalize(panelPos - camPos), {0, 1, 0});
  Call<void>(gSetPosition, gMenuTransform, panelPos);
  Call<void>(gSetRotation, gMenuTransform, panelRot);
  if (std::fabs(scale - gMenuScale) > 1e-3f) {
    gMenuScale = scale;
    Vec3 s{scale, scale, scale};
    Invoke(gSetLocalScale, gMenuTransform, &s);
  }

  // Луч из правого контроллера (pointer-поза, если есть, иначе поза рукоятки)
  Vec3 rayPos;
  Quat rayRot;
  if (gUsePointerPose && !Pose(kRightHand, gPointerPosition, gPointerRotation, rayPos, rayRot)) {
    gUsePointerPose = false;
    LOG(ANDROID_LOG_INFO, "нет PointerPosition — луч от рукоятки");
  }
  int count = TileCount();
  int hovered = -1, hoverNav = 0;
  bool onPanel = false;
  Vec3 cursor{0, 0, 0};
  float bottom = PanelBottom(count);
  float rayLength = 0;
  Vec3 origin{}, dir{0, 0, 1};
  if (gUsePointerPose || Pose(kRightHand, gDevicePosition, gDeviceRotation, rayPos, rayRot)) {
    origin = rigPos + Rotate(rigRot, rayPos * scale);
    dir = Rotate(rigRot * rayRot, {0, 0, 1});
    // Луч — из видимой руки класса, направление — как у контроллера
    if (auto hand = RightHand()) {
      Vec3 handPos = Call<Vec3>(gGetPosition, hand);
      if (!gHandLogged) {
        gHandLogged = true;
        Vec3 d = handPos - origin;
        LOG(ANDROID_LOG_INFO, "%s: рука от контроллера %.2f м, масштаб %.2f", gLocal.key.c_str(),
            std::sqrt(Dot(d, d)), scale);
      }
      origin = handPos;
    }
    rayLength = kRayIdleLength * scale;
    Vec3 normal = Rotate(panelRot, {0, 0, 1});
    float denom = Dot(dir, normal);
    if (std::fabs(denom) > 1e-4f) {
      float t = Dot(panelPos - origin, normal) / denom;
      Vec3 hit = origin + dir * t - panelPos;
      float x = Dot(hit, Rotate(panelRot, {1, 0, 0})) / scale;
      float y = Dot(hit, Rotate(panelRot, {0, 1, 0})) / scale;
      if (t > 0.f && std::fabs(x) < kPanelWidth / 2 && y < kPanelTop && y > bottom) {
        cursor = {x, y, -0.003f};
        onPanel = true;
        rayLength = t - kRayGap * scale;
        for (int i = 0; i < std::min(kMaxTiles, count - gPage * kMaxTiles); i++) {
          Vec3 p = TilePos(i);
          if (std::fabs(x - p.x) < kTileWidth / 2 && std::fabs(y - p.y) < kTileHeight / 2) hovered = i;
        }
        // Стрелки страниц — только те, что нажимаются (не у края)
        int pages = PageCount(count);
        if (pages > 1 && std::fabs(y - kNavY) < kNavHeight / 2)
          for (int side : {-1, 1})
            if (std::fabs(x - side * kNavX) < kNavWidth / 2 && gPage + side >= 0 && gPage + side < pages)
              hoverNav = side;
      }
    }
  }
  float start = kRayStart * scale;
  DrawRay(origin + dir * start, dir, rayLength - start, camPos, scale);
  if ((hovered >= 0 && hovered != gHovered) || (hoverNav && hoverNav != gHoverNav)) Buzz(kRightHand, 0.1f, 0.01f);
  gHovered = hovered;
  gHoverNav = hoverNav;
  gOnPanel = onPanel;
  if (onPanel) {
    gCursorLocal = cursor;
    Call<void>(gSetPosition, gCursorTransform, panelPos + Rotate(panelRot, cursor * scale));
  }

  if (triggerDown && gHovered >= 0) {
    Buzz(kRightHand, 0.4f, 0.05f);
    Pick(gPage * kMaxTiles + gHovered);
  } else if (triggerDown && gHoverNav) {
    Buzz(kRightHand, 0.3f, 0.04f);
    gPage += gHoverNav;
    gHovered = -1;
    gHoverNav = 0;  // у края стрелка гаснет — подсветку снимаем сразу
  }
  UpdateTexts();
}

#include "hub.inc"

// ---------- кадр ----------

void Tick() {
  // Хук может сработать несколько раз за кадр — работаем один раз
  int frame = Call<int>(gGetFrameCount);
  if (frame == gLastFrame) return;
  gLastFrame = frame;

  if (gLocal.cls.obj && !Alive(gLocal.cls.obj)) Forget(gLocal);  // класс исчез (смерть, смена класса)
  if (frame % kScanFrames == 0) {
    // Перепроверяем и живой: при входе в матч игра создаёт новый класс, а старый может ещё жить
    auto go = FindLocalClass();
    if (go && go != gLocal.cls.obj) {
      if (!gSkinsLoaded) LoadSkins();
      if (Alive(gLocal.cls.obj)) Strip(gLocal, true);
      Forget(gLocal);
      gLocal.cls = Hold(go);
      gLocal.key = ClassKeyOf(go);
      Wear(gLocal, Selected(gLocal.key), "свой");
      if (gMenuVisible) RefreshTiles();
    }
  }

  TickMenu();
  TickHub();

  if (Alive(gLocal.cls.obj) && !Sync(gLocal)) {
    std::string skin = gLocal.skin;
    Wear(gLocal, skin, "свой");
  }
  for (auto& [go, a] : gRemotes)
    if (Alive(a.cls.obj) && !Sync(a)) {
      std::string skin = a.skin;
      Wear(a, skin, "игрок");
    }

  if (gSkinsLoaded && frame % kNetFrames == 0) TickNetwork();
}

using RenderLoopFn = void (*)(void*, void*, void*, MethodInfo const*);
RenderLoopFn gOrigRenderLoop;

// Tick — до отрисовки: в многопроходном режиме цикл зовётся на каждый глаз,
// и если двигать меню после первого, глаза видят его в разных местах
void HookRenderLoop(void* pipe, void* loop, void* request, MethodInfo const* method) {
  Tick();
  gOrigRenderLoop(pipe, loop, request, method);
}

Il2CppString* KeepString(char const* s) { return Keep(il2cpp_string_new(s)); }

// Всё, что нужно для меню. Если чего-то нет — мод работает без меню
bool InitMenu(Il2CppImage const* core) {
  auto xr = FindImage("UnityEngine.XRModule.dll");
  auto tmp = FindImage("Unity.TextMeshPro.dll");
  if (!xr || !tmp) return false;

  auto object = Class(core, "UnityEngine", "Object");
  auto camera = Class(core, "UnityEngine", "Camera");
  auto transform = Class(core, "UnityEngine", "Transform");
  auto resources = Class(core, "UnityEngine", "Resources");
  auto tracking = Class(xr, "UnityEngine.XR", "InputTracking");
  auto devices = Class(xr, "UnityEngine.XR", "InputDevices");
  auto tmpText = Class(tmp, "TMPro", "TMP_Text");
  auto textMeshPro = Class(tmp, "TMPro", "TextMeshPro");
  auto fontAsset = Class(tmp, "TMPro", "TMP_FontAsset");
  auto tmpSettings = il2cpp_class_from_name(tmp, "TMPro", "TMP_Settings");
  if (!object || !camera || !transform || !resources || !tracking || !devices || !tmpText || !textMeshPro || !fontAsset)
    return false;

  gGetMainCamera = Method(camera, "get_main", 0);
  gGetPosition = Method(transform, "get_position", 0);
  gGetRotation = Method(transform, "get_rotation", 0);
  gSetPosition = Method(transform, "set_position", 1);
  gSetRotation = Method(transform, "set_rotation", 1);
  gSetLocalPosition = Method(transform, "set_localPosition", 1);
  gSetLocalScale = Method(transform, "set_localScale", 1);
  gGetLossyScale = Method(transform, "get_lossyScale", 0, false);
  gGameObjectCtor = FindOverload(gGameObjectClass, ".ctor", 1, "String");
  gAddComponent = FindOverload(gGameObjectClass, "AddComponent", 1, "Type");
  gSetActive = Method(gGameObjectClass, "SetActive", 1);
  gDontDestroyOnLoad = Method(object, "DontDestroyOnLoad", 1);
  gFindAllOfType = FindOverload(resources, "FindObjectsOfTypeAll", 1, "Type");
  gGetDeviceId = Method(tracking, "GetDeviceIdAtXRNode", 1);
  gGetButton = Method(devices, "TryGetFeatureValue_bool", 3);
  gGetVec3 = Method(devices, "TryGetFeatureValue_Vector3f", 3);
  gGetQuat = Method(devices, "TryGetFeatureValue_Quaternionf", 3);
  gHaptic = Method(devices, "SendHapticImpulse", 4, false);
  gSetText = Method(tmpText, "set_text", 1);
  gSetFontSize = Method(tmpText, "set_fontSize", 1);
  gSetAlignment = Method(tmpText, "set_alignment", 1);
  gSetWrapping = Method(tmpText, "set_textWrappingMode", 1);
  gSetFont = Method(tmpText, "set_font", 1);
  gDefaultFont = tmpSettings ? il2cpp_class_get_method_from_name(tmpSettings, "get_defaultFontAsset", 0) : nullptr;
  for (auto m : {gGetMainCamera, gGetPosition, gGetRotation, gSetPosition, gSetRotation, gSetLocalPosition,
                 gSetLocalScale, gGameObjectCtor, gAddComponent, gSetActive, gDontDestroyOnLoad, gFindAllOfType,
                 gGetDeviceId, gGetButton, gGetVec3, gGetQuat, gSetText, gSetFontSize, gSetAlignment, gSetWrapping,
                 gSetFont})
    if (!m) return false;

  // Фон меню — необязательный: без границ меша или порядка отрисовки остаются одни надписи
  auto mesh = il2cpp_class_from_name(core, "UnityEngine", "Mesh");
  auto renderer = il2cpp_class_from_name(core, "UnityEngine", "Renderer");
  gTextBounds = Method(tmpText, "get_bounds", 0, false);
  gMeshBounds = mesh ? Method(mesh, "get_bounds", 0, false) : nullptr;
  gTmpSortingOrder = Method(textMeshPro, "set_sortingOrder", 1, false);
  gRendererSortingOrder = renderer ? Method(renderer, "set_sortingOrder", 1, false) : nullptr;
  auto rectTransform = il2cpp_class_from_name(core, "UnityEngine", "RectTransform");
  gSetSizeDelta = rectTransform ? Method(rectTransform, "set_sizeDelta", 1) : nullptr;  // без него — класс по центру
  // Пиксельный шрифт меню — тоже необязательный
  auto tmpAsset = il2cpp_class_from_name(tmp, "TMPro", "TMP_Asset");
  auto material = il2cpp_class_from_name(core, "UnityEngine", "Material");
  gFontMaterial = tmpAsset ? Method(tmpAsset, "get_material", 0) : nullptr;
  gGetShader = material ? Method(material, "get_shader", 0) : nullptr;
  gSetShader = material ? Method(material, "set_shader", 1) : nullptr;
  // PoseInterpolator — компонент видимых рук классов; пространство имён в ПК-сборках не видно — перебираем
  if (auto dust = FindImage("Dust.dll"))
    for (char const* ns : {"Dust", "Dust.Combat", "Dust.Physics", "Dust.Networking", "Dust.Utilities", ""})
      if (auto pose = il2cpp_class_from_name(dust, ns, "PoseInterpolator")) {
        gPoseInterpolatorType = TypeOf(pose);
        LOG(ANDROID_LOG_INFO, "PoseInterpolator: %s", *ns ? ns : "(без пространства имён)");
        break;
      }
  if (!gPoseInterpolatorType) LOG(ANDROID_LOG_WARN, "нет PoseInterpolator — рука для луча по имени и глубине");
  gHasBackground = (gTextBounds || gMeshBounds) && (gTmpSortingOrder || gRendererSortingOrder);
  LOG(gHasBackground ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, "фон меню: границы %s, порядок %s",
      gTextBounds ? "TMP_Text.bounds" : gMeshBounds ? "Mesh.bounds" : "нет",
      gTmpSortingOrder ? "TextMeshPro.sortingOrder" : gRendererSortingOrder ? "Renderer.sortingOrder" : "нет");

  gTextMeshProType = TypeOf(textMeshPro);
  gFontAssetType = TypeOf(fontAsset);
  gTriggerButton = KeepString("TriggerButton");
  gStickClick = KeepString("Primary2DAxisClick");
  gDevicePosition = KeepString("DevicePosition");
  gDeviceRotation = KeepString("DeviceRotation");
  gPointerPosition = KeepString("PointerPosition");
  gPointerRotation = KeepString("PointerRotation");
  return true;
}

// Свойства сессии и сетевой ID. Если чего-то нет — мод работает без синхронизации
bool InitNetwork() {
  auto net = FindImage("Dust.Networking.dll");
  if (!net) {
    LOG(ANDROID_LOG_ERROR, "нет сборки Dust.Networking");
    return false;
  }
  auto networkPrefab = Class(net, "Dust.Networking", "NetworkPrefab");
  auto networkManager = Class(net, "Dust.Networking", "NetworkManager");
  auto matchmaker = Class(net, "Dust.Servers", "Matchmaker");
  if (!networkPrefab || !networkManager || !matchmaker) return false;

  gHasId = Method(networkPrefab, "get_HasId", 0);
  gIsOwned = Method(networkPrefab, "get_IsOwned", 0);
  gGetClient = Method(networkManager, "get_Client", 0);
  gOfflineMode = Method(matchmaker, "get_offlineMode", 0, false);
  if (!gOfflineMode) {
    gOfflineField = il2cpp_class_get_field_from_name(matchmaker, "offlineMode");
    il2cpp_field_static_get_value =
        reinterpret_cast<void (*)(FieldInfo*, void*)>(dlsym(modloader_libil2cpp_handle, "il2cpp_field_static_get_value"));
  }
  auto networkComponent = Class(net, "Dust.Networking", "NetworkComponent");
  auto packetPriority = Class(net, "Dust.Networking", "PacketPriority");
  gByteListClass = Class(net, "", "ByteList");
  gByteBufferClass = Class(FindImage("Dust.Serialisation.dll"), "Dust.Serialisation", "ByteBuffer");
  if (!networkComponent || !packetPriority || !gByteListClass || !gByteBufferClass) return false;
  gSendRpc = Method(networkComponent, "SendRpc", 3);
  gSendRpcTo = Method(networkComponent, "SendRpcTo", 4);
  auto onReceive = Method(networkComponent, "OnReceive", 2);
  gByteBufferCtor = FindOverload(gByteBufferClass, ".ctor", 2, "Int32", "Int32");
  gByteBufferAdd = FindOverload(gByteBufferClass, "Add", 1, "Byte[]");
  gByteClass = Class(FindImage("mscorlib.dll"), "System", "Byte");
  il2cpp_array_new =
      reinterpret_cast<Il2CppArray* (*)(Il2CppClass*, uintptr_t)>(dlsym(modloader_libil2cpp_handle, "il2cpp_array_new"));
  if (!gByteClass || !il2cpp_array_new) return false;
  for (auto m : {gHasId, gIsOwned, gGetClient, gSendRpc, gSendRpcTo, onReceive, gByteBufferCtor, gByteBufferAdd})
    if (!m) return false;

  // PacketPriority.LOW — статическое поле byte; нет его — геттер Low
  if (!il2cpp_field_static_get_value)
    il2cpp_field_static_get_value =
        reinterpret_cast<void (*)(FieldInfo*, void*)>(dlsym(modloader_libil2cpp_handle, "il2cpp_field_static_get_value"));
  auto lowField = il2cpp_class_get_field_from_name(packetPriority, "LOW");
  if (lowField && il2cpp_field_static_get_value) {
    il2cpp_field_static_get_value(lowField, &gPriorityLow);
  } else if (auto getLow = Method(packetPriority, "get_Low", 0)) {
    gPriorityLow = Unbox<uint8_t>(Invoke(getLow, nullptr));
  } else {
    return false;
  }
  LOG(ANDROID_LOG_INFO, "сеть: приоритет пакетов %u", gPriorityLow);

  auto target = *reinterpret_cast<uint32_t* const*>(onReceive);
  auto result = flamingo_install_hook(reinterpret_cast<void*>(&HookOnReceive), target,
                                      reinterpret_cast<void**>(&gOrigOnReceive),
                                      flamingo_make_name("Skins::NetworkComponent.OnReceive"));
  if (result.result != FLAMINGO_INSTALL_OK) {
    char buf[512];
    flamingo_format_error(result.value.data, buf, sizeof(buf));
    LOG(ANDROID_LOG_ERROR, "хук на NetworkComponent.OnReceive не поставлен: %s", buf);
    return false;
  }

  gNetworkPrefabType = TypeOf(networkPrefab);
  gNetworkManagerType = TypeOf(networkManager);
  gMatchmakerType = TypeOf(matchmaker);
  return true;
}

bool Init() {
  void* lib = modloader_libil2cpp_handle;
  if (!lib) {
    LOG(ANDROID_LOG_ERROR, "нет хэндла libil2cpp.so");
    return false;
  }
  if (!(Resolve(lib, il2cpp_domain_get, "il2cpp_domain_get") &&
        Resolve(lib, il2cpp_domain_get_assemblies, "il2cpp_domain_get_assemblies") &&
        Resolve(lib, il2cpp_assembly_get_image, "il2cpp_assembly_get_image") &&
        Resolve(lib, il2cpp_image_get_name, "il2cpp_image_get_name") &&
        Resolve(lib, il2cpp_class_from_name, "il2cpp_class_from_name") &&
        Resolve(lib, il2cpp_class_get_method_from_name, "il2cpp_class_get_method_from_name") &&
        Resolve(lib, il2cpp_class_get_methods, "il2cpp_class_get_methods") &&
        Resolve(lib, il2cpp_method_get_name, "il2cpp_method_get_name") &&
        Resolve(lib, il2cpp_method_get_param_count, "il2cpp_method_get_param_count") &&
        Resolve(lib, il2cpp_method_get_param, "il2cpp_method_get_param") &&
        Resolve(lib, il2cpp_method_is_generic, "il2cpp_method_is_generic") &&
        Resolve(lib, il2cpp_class_from_type, "il2cpp_class_from_type") &&
        Resolve(lib, il2cpp_class_get_name, "il2cpp_class_get_name") &&
        Resolve(lib, il2cpp_class_get_type, "il2cpp_class_get_type") &&
        Resolve(lib, il2cpp_type_get_object, "il2cpp_type_get_object") &&
        Resolve(lib, il2cpp_class_get_field_from_name, "il2cpp_class_get_field_from_name") &&
        Resolve(lib, il2cpp_field_get_offset, "il2cpp_field_get_offset") &&
        Resolve(lib, il2cpp_object_new, "il2cpp_object_new") &&
        Resolve(lib, il2cpp_runtime_invoke, "il2cpp_runtime_invoke") &&
        Resolve(lib, il2cpp_string_new, "il2cpp_string_new") &&
        Resolve(lib, il2cpp_string_chars, "il2cpp_string_chars") &&
        Resolve(lib, il2cpp_string_length, "il2cpp_string_length") &&
        Resolve(lib, il2cpp_gchandle_new, "il2cpp_gchandle_new") &&
        Resolve(lib, il2cpp_gchandle_free, "il2cpp_gchandle_free") &&
        Resolve(lib, il2cpp_gchandle_get_target, "il2cpp_gchandle_get_target") &&
        Resolve(lib, il2cpp_array_length, "il2cpp_array_length") &&
        Resolve(lib, il2cpp_resolve_icall, "il2cpp_resolve_icall")))
    return false;

  auto core = FindImage("UnityEngine.CoreModule.dll");
  auto bundleModule = FindImage("UnityEngine.AssetBundleModule.dll");
  auto dust = FindImage("Dust.dll");
  if (!core || !bundleModule || !dust) {
    LOG(ANDROID_LOG_ERROR, "нет сборок CoreModule/AssetBundleModule/Dust");
    return false;
  }

  auto time = Class(core, "UnityEngine", "Time");
  auto object = Class(core, "UnityEngine", "Object");
  gGameObjectClass = Class(core, "UnityEngine", "GameObject");
  auto component = Class(core, "UnityEngine", "Component");
  auto transform = Class(core, "UnityEngine", "Transform");
  auto meshFilter = Class(core, "UnityEngine", "MeshFilter");
  auto renderer = Class(core, "UnityEngine", "Renderer");
  auto pipelineManager = Class(core, "UnityEngine.Rendering", "RenderPipelineManager");
  auto assetBundle = Class(bundleModule, "UnityEngine", "AssetBundle");
  auto combatClass = Class(dust, "Dust.Combat", "CombatClass");
  if (!time || !object || !gGameObjectClass || !component || !transform || !meshFilter || !renderer ||
      !pipelineManager || !assetBundle || !combatClass)
    return false;

  gGetFrameCount = Method(time, "get_frameCount", 0);
  gObjectName = Method(object, "get_name", 0);
  gSetObjectName = Method(object, "set_name", 1);
  gDestroy = FindOverload(object, "Destroy", 1, "Object");
  gFindObjects = FindOverload(object, "FindObjectsByType", 2, "Type");
  gInstantiate = FindOverload(object, "Instantiate", 3, "Object", "Transform", false);
  if (!gInstantiate) gInstantiatePlain = FindOverload(object, "Instantiate", 1, "Object");
  gSetParent = FindOverload(transform, "SetParent", 2, "Transform");
  gComponentGameObject = Method(component, "get_gameObject", 0);
  gComponentTransform = Method(component, "get_transform", 0);
  gGameObjectTransform = Method(gGameObjectClass, "get_transform", 0);
  gGetComponent = FindOverload(gGameObjectClass, "GetComponent", 1, "Type");
  gGetLayer = Method(gGameObjectClass, "get_layer", 0);
  gSetLayer = Method(gGameObjectClass, "set_layer", 1);
  gChildCount = Method(transform, "get_childCount", 0);
  gGetChild = Method(transform, "GetChild", 1);
  gGetParent = Method(transform, "get_parent", 0);
  gGetSharedMesh = Method(meshFilter, "get_sharedMesh", 0);
  gSetSharedMesh = Method(meshFilter, "set_sharedMesh", 1);
  gGetSharedMaterial = Method(renderer, "get_sharedMaterial", 0);
  gSetSharedMaterial = Method(renderer, "set_sharedMaterial", 1);

  gLoadBundle = FindOverload(assetBundle, "LoadFromFile", 1, "String", nullptr, false);
  if (!gLoadBundle) Icall(gLoadBundleIcall, "UnityEngine.AssetBundle::LoadFromFile_Internal_Injected");
  gLoadAsset = FindOverload(assetBundle, "LoadAsset", 2, "String", "Type", false);
  if (!gLoadAsset) gLoadAsset = FindOverload(assetBundle, "LoadAsset_Internal", 2, "String", "Type", false);
  if (!gLoadAsset) Icall(gLoadAssetIcall, "UnityEngine.AssetBundle::LoadAsset_Internal_Injected");

  auto renderLoop = Method(pipelineManager, "DoRenderLoop_Internal", 3);
  for (auto m : {gGetFrameCount, gObjectName, gSetObjectName, gFindObjects, gSetParent, gComponentGameObject,
                 gComponentTransform, gGameObjectTransform, gGetComponent, gGetLayer, gSetLayer, gChildCount,
                 gGetChild, gGetParent, gGetSharedMesh, gSetSharedMesh, gGetSharedMaterial, gSetSharedMaterial,
                 renderLoop})
    if (!m) {
      LOG(ANDROID_LOG_ERROR, "не найдено что-то из Unity API — мод выключен");
      return false;
    }
  if (!(gInstantiate || gInstantiatePlain)) LOG(ANDROID_LOG_WARN, "нет Object.Instantiate — новых деталей не будет");
  if (!gDestroy) LOG(ANDROID_LOG_WARN, "нет Object.Destroy — при смене скина новые детали останутся");
  if (!(gLoadBundle || gLoadBundleIcall) || !(gLoadAsset || gLoadAssetIcall)) {
    LOG(ANDROID_LOG_ERROR, "нет загрузки AssetBundle — мод выключен");
    return false;
  }

  auto cachedPtr = il2cpp_class_get_field_from_name(object, "m_CachedPtr");
  if (!cachedPtr) {
    LOG(ANDROID_LOG_ERROR, "нет Object.m_CachedPtr");
    return false;
  }
  gCachedPtrOffset = il2cpp_field_get_offset(cachedPtr);

  gMeshFilterType = TypeOf(meshFilter);
  gRendererType = TypeOf(renderer);
  gGameObjectType = TypeOf(gGameObjectClass);
  gCombatClassType = TypeOf(combatClass);
  LoadClassTypeNames(combatClass);

  // Движущиеся детали (ручки затвора Crossbolt Sunset): без них скин работает, детали просто стоят
  gGetLocalPosition = Method(transform, "get_localPosition", 0);
  gGetLocalRotation = Method(transform, "get_localRotation", 0);
  gSetLocalRotation = Method(transform, "set_localRotation", 1);
  gGetLocalScale = Method(transform, "get_localScale", 0);
  if (auto scalar = Class(dust, "Dust", "ScalarProceduralAnimation")) {
    if (auto field = il2cpp_class_get_field_from_name(scalar, "currentTime")) {
      gCurrentTimeOffset = il2cpp_field_get_offset(field);
      gScalarAnimType = TypeOf(scalar);
    } else {
      LOG(ANDROID_LOG_ERROR, "нет поля ScalarProceduralAnimation.currentTime");
    }
  }
  // Брошенное колесо Crossbolt: без этого его скин просто не надевается
  if (auto teleporter = Class(dust, "Dust.Combat", "ProjectileTeleporter")) {
    if (auto field = il2cpp_class_get_field_from_name(teleporter, "teleportProjectile")) {
      gTeleportProjectileOffset = il2cpp_field_get_offset(field);
      gTeleporterType = TypeOf(teleporter);
    } else {
      LOG(ANDROID_LOG_ERROR, "нет поля ProjectileTeleporter.teleportProjectile");
    }
  }

  if (!InitMenu(core)) {
    gMenuFailed = true;
    LOG(ANDROID_LOG_ERROR, "не найдено что-то для меню — меню не будет (скин выбирается в skins.cfg)");
  }
  if (!InitHub(core)) {
    gHubFailed = true;
    LOG(ANDROID_LOG_ERROR, "не найдено что-то для хаба — уголка скинов не будет");
  }
  if (!InitNetwork()) {
    gNetFailed = true;
    LOG(ANDROID_LOG_ERROR, "не найдено что-то для сети — скины других игроков видно не будет");
  }

  LOG(ANDROID_LOG_INFO, "API: бандл — %s, ассет — %s, копия деталей — %s, меню — %s, сеть — %s",
      gLoadBundle ? "метод" : "icall", gLoadAsset ? "метод" : "icall",
      gInstantiate ? "Instantiate(parent)" : gInstantiatePlain ? "Instantiate+SetParent" : "нет", gMenuFailed ? "нет" : "да",
      gNetFailed ? "нет" : "да");

  auto target = *reinterpret_cast<uint32_t* const*>(renderLoop);
  auto result = flamingo_install_hook(reinterpret_cast<void*>(&HookRenderLoop), target,
                                      reinterpret_cast<void**>(&gOrigRenderLoop),
                                      flamingo_make_name("Skins::DoRenderLoop_Internal"));
  if (result.result != FLAMINGO_INSTALL_OK) {
    char buf[512];
    flamingo_format_error(result.value.data, buf, sizeof(buf));
    LOG(ANDROID_LOG_ERROR, "хук не поставлен: %s", buf);
    return false;
  }
  return true;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) void setup(CModInfo* info) {
  info->id = "skins";
  info->version = kModVersion;
  info->version_long = 0;
}

extern "C" __attribute__((visibility("default"))) void late_load() {
  if (Init()) LOG(ANDROID_LOG_INFO, "загружен, хук на DoRenderLoop_Internal стоит");
}
