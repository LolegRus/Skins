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
// у каждого класса со скинами «< скин >», наведи правый контроллер и жми курок. Выбор пишется в
// kSkinDir/skins.cfg («KOBOLD=Cowboy», пусто — родной вид); без строки — первый скин класса.
// Свой класс — CombatClass, у которого в имени объекта «Local» («Kobold (Local) Variant(Clone)»,
// «Grappler Local Variant(Clone)»), ключ класса — первые буквы имени.
// Скины других игроков с модом — через свойства сессии, как на ПК (см. «сеть»).
// Вместо OnUpdate — хук на RenderPipelineManager.DoRenderLoop_Internal.

#include <android/log.h>
#include <dirent.h>
#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
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

// Сеть: свойства сессии и сетевой ID экземпляра класса (как на ПК)
MethodInfo const* gHasId;
MethodInfo const* gObjectIdString;
MethodInfo const* gIsOwned;
MethodInfo const* gGetClient;
MethodInfo const* gOfflineMode;  // Matchmaker.offlineMode — статическое; геттер в сборке для Quest вырезан,
FieldInfo* gOfflineField;        // тогда читаем само поле
void (*il2cpp_field_static_get_value)(FieldInfo*, void*);
MethodInfo const* gSetProperty;
MethodInfo const* gTryGetProperty;

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
char const* const kClassKeys[] = {"ASSASSIN", "BOULDER", "CROSSBOLT", "GOLEM", "GRAPPLER", "JACKET", "KOBOLD",
                                  "REAPER",   "ROLLER",  "SCARAB",    "SERAPH", "SPARROW", "STYX"};

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

Il2CppObject* LoadAsset(Il2CppObject* bundle, std::string const& name) {
  if (gLoadAsset) return Invoke(gLoadAsset, bundle, il2cpp_string_new(name.c_str()), gGameObjectType);
  if (!gLoadAssetIcall) return nullptr;
  auto wide = ToUtf16(name);
  Span span{wide.data(), static_cast<int32_t>(wide.size())};
  void* handle = gLoadAssetIcall(NativePtr(bundle), &span, gGameObjectType);
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
// Скин на одном экземпляре класса — своём или чужом
struct Applied {
  Held cls;          // GameObject класса
  std::string key;   // ключ класса
  std::string skin;  // "" — родной вид
  std::vector<Swap> swaps;
  std::vector<Extra> extras;
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
  a.swaps.clear();
  a.extras.clear();
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
      }
      continue;
    }

    auto filter = Component(GameObjectOf(node), gMeshFilterType);
    if (!filter) {
      missing.push_back(path + " (без MeshFilter)");
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
  return true;
}

// Ключ класса — первые буквы имени объекта: «Kobold (Local) Variant(Clone)» → KOBOLD
std::string ClassKeyOf(Il2CppObject* go) {
  std::string name = NameOf(go), key;
  for (char c : name) {
    if (!isalpha(static_cast<unsigned char>(c))) break;
    key += static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  return key;
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

// ---------- сеть: свойства сессии ----------
// Как на ПК (SkinNet.cs): свой скин — в свойство «sk» + FNV-1a(сетевой ID экземпляра класса),
// у чужих классов читаем это же свойство. Игроки без мода свойства не читают.
// Пустую строку сервер не принимает (остаётся старое значение), поэтому родной вид — «-», а пустое
// при чтении значит «данных нет»: чтение иногда на миг отдаёт пусто — скин от этого не снимаем,
// иначе он мигает. Запись повторяем раз в kResendTicks: первая после появления класса бывает, что теряется.

constexpr int kNetFrames = 60;      // сеть — раз в столько кадров
constexpr size_t kMaxValueBytes = 63;  // буфер Matchmaker.TryGetProperty — 64 байта
constexpr char kNormalValue[] = "-";   // родной вид
constexpr int kResendTicks = 12;       // ~10 с

constexpr int kSelfCheckTicks = 3;  // через столько тиков сети читаем своё свойство обратно

Held gNetworkManager, gMatchmaker;
std::string gSentKey, gSentValue;
int gResend;  // тиков до повтора записи
bool gNetFailed;
std::string gNetState;  // последнее состояние сети в логе — пишем только смены
int gSelfCheck = -1;    // тиков до самопроверки (-1 — не нужна)
int gSelfCheckFails;
constexpr int kSelfCheckTries = 3;
std::string gLoggedValue;  // что последним написали в лог

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

// Ключ свойства для класса; "" — сетевого ID ещё нет
std::string NetKey(Il2CppObject* classObject) {
  auto np = Component(classObject, gNetworkPrefabType);
  if (!np || !Unbox<bool>(Invoke(gHasId, np))) return "";
  auto id = reinterpret_cast<Il2CppString*>(Invoke(gObjectIdString, np));
  if (!id || il2cpp_string_length(id) == 0) return "";
  uint32_t h = 2166136261u;  // FNV-1a по UTF-16, как string в C# на ПК
  auto chars = il2cpp_string_chars(id);
  for (int32_t i = 0, n = il2cpp_string_length(id); i < n; i++) h = (h ^ chars[i]) * 16777619u;
  char buf[16];
  snprintf(buf, sizeof(buf), "sk%08x", h);
  return buf;
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

std::string ReadProperty(std::string const& key) {
  Il2CppString* value = nullptr;
  auto ok = Invoke(gTryGetProperty, gMatchmaker.obj, il2cpp_string_new(key.c_str()), &value);
  return ok && Unbox<bool>(ok) && value ? ToUtf8(value) : "";
}

void Publish() {
  if (!Alive(gLocal.cls.obj)) return;  // между смертью и появлением — обычное дело
  if (!Component(gLocal.cls.obj, gNetworkPrefabType)) return NetState("у своего класса нет NetworkPrefab — другие игроки скин не увидят");
  std::string key = NetKey(gLocal.cls.obj);
  if (key.empty()) return;  // сетевой ID выдают не сразу после появления
  NetState("работает");

  // Самопроверка: держит ли сервер наше значение; не держит — сразу отправляем ещё раз.
  // Первая запись после появления класса бывает, что теряется, — ругаемся, только если не выходит подряд
  if (gSelfCheck > 0 && --gSelfCheck == 0) {
    if (ReadProperty(gSentKey) == gSentValue) {
      gSelfCheckFails = 0;
    } else {
      if (++gSelfCheckFails == kSelfCheckTries)
        LOG(ANDROID_LOG_WARN, "сервер не сохраняет выбор скина (%d раза подряд) — другие игроки могут его не увидеть", kSelfCheckTries);
      gResend = 0;
      gSelfCheck = kSelfCheckTicks;
    }
  }

  std::string skin = gLocal.skin.size() <= kMaxValueBytes ? gLocal.skin : "";
  std::string value = skin.empty() ? kNormalValue : skin;
  bool changed = key != gSentKey || value != gSentValue;
  if (!changed && --gResend > 0) return;
  if (!gSentKey.empty() && gSentKey != key)
    Invoke(gSetProperty, gNetworkManager.obj, il2cpp_string_new(gSentKey.c_str()), il2cpp_string_new(kNormalValue));
  Invoke(gSetProperty, gNetworkManager.obj, il2cpp_string_new(key.c_str()), il2cpp_string_new(value.c_str()));
  gResend = kResendTicks;
  if (!changed) return;  // повтор того же — без записи в лог
  if (value != gLoggedValue)  // новый экземпляр класса с тем же скином — не пишем
    LOG(ANDROID_LOG_INFO, "другим игрокам с модом: %s", skin.empty() ? "родной вид" : skin.c_str());
  gLoggedValue = value;
  gSentKey = key;
  gSentValue = value;
  gSelfCheck = kSelfCheckTicks;
}

void ScanRemotes() {
  int sortNone = 0;
  auto all = reinterpret_cast<Il2CppArray*>(Invoke(gFindObjects, nullptr, gCombatClassType, &sortNone));
  std::vector<Il2CppObject*> seen;
  for (uint32_t i = 0; i < Length(all); i++) {
    auto cc = At(all, i);
    if (!Alive(cc)) continue;
    auto go = GameObjectOf(cc);
    if (go == gLocal.cls.obj) continue;
    auto np = Component(go, gNetworkPrefabType);
    if (!np || Unbox<bool>(Invoke(gIsOwned, np))) continue;  // свой (или не сетевой) класс
    std::string key = NetKey(go);
    if (key.empty()) continue;
    seen.push_back(go);

    auto it = gRemotes.find(go);
    if (it == gRemotes.end()) {
      it = gRemotes.emplace(go, Applied{}).first;
      it->second.cls = Hold(go);
      it->second.key = ClassKeyOf(go);
    }
    std::string value = ReadProperty(key);
    if (value.empty()) continue;  // данных нет (игрок без мода или свойство на миг не читается) — не трогаем
    std::string skin = value == kNormalValue ? "" : value;
    if (it->second.net == skin) continue;
    bool news = !skin.empty() || (it->second.net != "\x01" && !it->second.net.empty());  // первое «-» — не новость
    it->second.net = skin;  // скина нет у нас — тоже запоминаем, чтобы не пробовать каждый раз заново
    if (news) LOG(ANDROID_LOG_INFO, "другой игрок на %s: %s", it->second.key.c_str(), skin.empty() ? "родной вид" : skin.c_str());
    Wear(it->second, skin, "другой игрок");
  }
  for (auto it = gRemotes.begin(); it != gRemotes.end();) {
    if (std::find(seen.begin(), seen.end(), it->first) == seen.end()) {
      Forget(it->second);
      it = gRemotes.erase(it);
    } else {
      ++it;
    }
  }
}

void TickNetwork() {
  if (gNetFailed) return;
  if (!Session()) {
    gSentKey.clear();  // новая сессия — всё отправим заново
    gSentValue.clear();
    return;
  }
  Publish();
  ScanRemotes();
}

// ---------- меню (левый стик) ----------
// Панель над левой рукой: строка на каждый класс, у которого есть скины (и на текущий класс).
// Наведи правый контроллер на «<» или «>» и жми курок. Позы рук — как в TimeControl:
// InputDevices дают их в пространстве трекинга, в мир переводим через камеру: rig = camera * inverse(head).

constexpr int kHead = 3;       // XRNode.Head
constexpr int kLeftHand = 4;   // XRNode.LeftHand
constexpr int kRightHand = 5;  // XRNode.RightHand
constexpr float kFontSize = 0.12f;
constexpr float kPanelWidth = 0.20f;
constexpr float kRowHeight = 0.026f;
constexpr float kPanelAboveHand = 0.13f;
constexpr int kMaxRows = sizeof(kClassKeys) / sizeof(kClassKeys[0]);

// Надписи: заголовок, подсказка, по 4 на строку (класс, «<», скин, «>»), курсор
enum { kTitleText, kHintText, kRowTexts };
constexpr int kTextsPerRow = 4;
constexpr int kCursorText = kRowTexts + kMaxRows * kTextsPerRow;
constexpr int kTextCount = kCursorText + 1;

struct Hit {
  int row;   // -1 — не кнопка
  int step;  // -1 «<», +1 «>»
};

Il2CppObject* gTexts[kTextCount];
std::string gShown[kTextCount];
Vec3 gTextPos[kTextCount];
Held gMenuRoot;
Il2CppObject* gMenuTransform;
Il2CppObject* gCursorTransform;
bool gMenuFailed, gMenuVisible, gStickWas, gTriggerWas, gUsePointerPose = true;
float gMenuScale = 1.f;
std::vector<std::string> gRows;  // ключи классов в строках меню
Hit gHovered{-1, 0};

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
  auto go = Keep(il2cpp_object_new(gGameObjectClass));
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

float RowY(int row) { return 0.040f - row * kRowHeight; }

bool BuildMenu() {
  auto font = FindFont();
  if (!font) return false;  // сцена ещё грузится, попробуем позже

  auto root = NewGameObject("SkinsMenu", nullptr);
  gMenuRoot = Hold(root);
  Invoke(gDontDestroyOnLoad, nullptr, root);
  gMenuTransform = Keep(Call<Il2CppObject*>(gGameObjectTransform, root));

  gTextPos[kTitleText] = {-0.045f, 0.075f, 0};
  gTextPos[kHintText] = {0.055f, 0.075f, 0};
  for (int r = 0; r < kMaxRows; r++) {
    float y = RowY(r);
    gTextPos[kRowTexts + r * kTextsPerRow + 0] = {-0.060f, y, 0};  // класс
    gTextPos[kRowTexts + r * kTextsPerRow + 1] = {-0.002f, y, 0};  // «<»
    gTextPos[kRowTexts + r * kTextsPerRow + 2] = {0.045f, y, 0};   // скин
    gTextPos[kRowTexts + r * kTextsPerRow + 3] = {0.090f, y, 0};   // «>»
  }
  gTextPos[kCursorText] = {0, 0, 0};

  int center = 514;  // TextAlignmentOptions.Center
  int noWrap = 0;    // TextWrappingModes.NoWrap
  float size = kFontSize;
  for (int i = 0; i < kTextCount; i++) {
    auto go = NewGameObject("SkinsText", gMenuTransform);
    auto text = gTexts[i] = Keep(Invoke(gAddComponent, go, gTextMeshProType));
    if (!text) {
      gMenuFailed = true;
      LOG(ANDROID_LOG_ERROR, "не удалось добавить TextMeshPro — меню не будет");
      return false;
    }
    Invoke(gSetFont, text, font);
    Invoke(gSetFontSize, text, &size);
    Invoke(gSetAlignment, text, &center);
    Invoke(gSetWrapping, text, &noWrap);
    Invoke(gSetLocalPosition, Call<Il2CppObject*>(gGameObjectTransform, go), &gTextPos[i]);
    gShown[i].clear();
  }
  gCursorTransform = Keep(Call<Il2CppObject*>(gComponentTransform, gTexts[kCursorText]));
  bool hidden = false;
  Invoke(gSetActive, root, &hidden);
  gMenuVisible = false;
  return true;
}

void SetText(int i, std::string const& text) {
  if (gShown[i] == text) return;
  gShown[i] = text;
  Invoke(gSetText, gTexts[i], il2cpp_string_new(text.c_str()));
}

std::string Title(std::string const& key) {
  std::string s = key;
  for (size_t i = 1; i < s.size(); i++) s[i] = static_cast<char>(tolower(static_cast<unsigned char>(s[i])));
  return s;
}

std::string Mark(std::string const& s, bool on) { return on ? "<mark=#FFFFFF50>" + s + "</mark>" : s; }

// Строки меню: классы со скинами, свой класс — первым
void RefreshRows() {
  gRows.clear();
  if (!gLocal.key.empty()) gRows.push_back(gLocal.key);
  for (auto key : kClassKeys)
    if (key != gLocal.key && !SkinsFor(key).empty()) gRows.push_back(key);
}

void UpdateTexts() {
  SetText(kTitleText, "<color=#42FFE0>Skins</color>");
  SetText(kHintText, "<color=#999999><size=70%>aim + trigger</size></color>");
  for (int r = 0; r < kMaxRows; r++) {
    int base = kRowTexts + r * kTextsPerRow;
    if (r >= static_cast<int>(gRows.size())) {
      for (int k = 0; k < kTextsPerRow; k++) SetText(base + k, "");
      continue;
    }
    std::string const& key = gRows[r];
    bool any = !SkinsFor(key).empty();
    std::string skin = Selected(key);
    bool current = key == gLocal.key;
    SetText(base + 0, current ? "<color=#FFD24A>" + Title(key) + "</color>" : Title(key));
    SetText(base + 1, any ? Mark("<color=#56EAFF><</color>", gHovered.row == r && gHovered.step < 0) : "");
    SetText(base + 2, !any ? "<color=#777777>no skins</color>" : skin.empty() ? "<color=#BBBBBB>normal</color>" : skin);
    SetText(base + 3, any ? Mark("<color=#56EAFF>></color>", gHovered.row == r && gHovered.step > 0) : "");
  }
  SetText(kCursorText, gHovered.row >= -1 && gHovered.step != 0 ? "<color=#FFD24A>o</color>" : "");
}

// Соседний скин класса; после последнего — родной вид
void Step(std::string const& key, int step) {
  auto skins = SkinsFor(key);
  if (skins.empty()) return;
  int n = static_cast<int>(skins.size()) + 1, index = 0;
  std::string current = Selected(key);
  for (int i = 0; i < static_cast<int>(skins.size()); i++)
    if (skins[i]->name == current) index = i + 1;
  index = ((index + step) % n + n) % n;
  gConfig[key] = index == 0 ? "" : skins[index - 1]->name;
  WriteConfig();
  LOG(ANDROID_LOG_INFO, "%s: %s", key.c_str(), gConfig[key].empty() ? "родной вид" : gConfig[key].c_str());
  if (key == gLocal.key) Wear(gLocal, gConfig[key], "свой");
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
    if (gMenuVisible) RefreshRows();
    Invoke(gSetActive, gMenuRoot.obj, &gMenuVisible);
    Buzz(kLeftHand, 0.3f, 0.05f);
  }
  gStickWas = stick;

  bool trigger = Button(kRightHand, gTriggerButton);
  bool triggerDown = trigger && !gTriggerWas;
  gTriggerWas = trigger;
  if (!gMenuVisible || !tracked) {
    gHovered = {-1, 0};
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
  Hit hovered{-2, 0};  // -2 — луч мимо панели
  Vec3 cursor{0, 0, 0};
  float top = 0.09f, bottom = RowY(static_cast<int>(gRows.size()) - 1) - kRowHeight / 2;
  if (gUsePointerPose || Pose(kRightHand, gDevicePosition, gDeviceRotation, rayPos, rayRot)) {
    Vec3 origin = rigPos + Rotate(rigRot, rayPos * scale);
    Vec3 dir = Rotate(rigRot * rayRot, {0, 0, 1});
    Vec3 normal = Rotate(panelRot, {0, 0, 1});
    float denom = Dot(dir, normal);
    if (std::fabs(denom) > 1e-4f) {
      float t = Dot(panelPos - origin, normal) / denom;
      Vec3 hit = origin + dir * t - panelPos;
      float x = Dot(hit, Rotate(panelRot, {1, 0, 0})) / scale;
      float y = Dot(hit, Rotate(panelRot, {0, 1, 0})) / scale;
      if (t > 0.f && std::fabs(x) < kPanelWidth / 2 && y < top && y > bottom) {
        cursor = {x, y, -0.003f};
        hovered = {-1, 1};  // на панели, но не на кнопке — курсор всё равно показываем
        for (int r = 0; r < static_cast<int>(gRows.size()); r++)
          for (int k : {1, 3}) {
            Vec3 p = gTextPos[kRowTexts + r * kTextsPerRow + k];
            if (std::fabs(x - p.x) < 0.016f && std::fabs(y - p.y) < kRowHeight / 2) hovered = {r, k == 1 ? -1 : 1};
          }
      }
    }
  }
  if (hovered.row >= 0 && (hovered.row != gHovered.row || hovered.step != gHovered.step)) Buzz(kRightHand, 0.1f, 0.01f);
  gHovered = hovered.row == -2 ? Hit{-1, 0} : hovered;
  if (gHovered.step != 0) Call<void>(gSetPosition, gCursorTransform, panelPos + Rotate(panelRot, cursor * scale));

  if (triggerDown && gHovered.row >= 0) {
    Buzz(kRightHand, 0.4f, 0.05f);
    Step(gRows[gHovered.row], gHovered.step);
  }
  UpdateTexts();
}

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
      if (gMenuVisible) RefreshRows();
    }
  }

  TickMenu();

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
  gObjectIdString = Method(networkPrefab, "get_ObjectIdString", 0);
  gIsOwned = Method(networkPrefab, "get_IsOwned", 0);
  gGetClient = Method(networkManager, "get_Client", 0);
  gOfflineMode = Method(matchmaker, "get_offlineMode", 0, false);
  if (!gOfflineMode) {
    gOfflineField = il2cpp_class_get_field_from_name(matchmaker, "offlineMode");
    il2cpp_field_static_get_value =
        reinterpret_cast<void (*)(FieldInfo*, void*)>(dlsym(modloader_libil2cpp_handle, "il2cpp_field_static_get_value"));
  }
  gSetProperty = FindOverload(networkManager, "SetProperty", 2, "String", "String");
  gTryGetProperty = Method(matchmaker, "TryGetProperty", 2);
  for (auto m : {gHasId, gObjectIdString, gIsOwned, gGetClient, gSetProperty, gTryGetProperty})
    if (!m) return false;

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

  if (!InitMenu(core)) {
    gMenuFailed = true;
    LOG(ANDROID_LOG_ERROR, "не найдено что-то для меню — меню не будет (скин выбирается в skins.cfg)");
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
  info->version = "0.2.0";
  info->version_long = 0;
}

extern "C" __attribute__((visibility("default"))) void late_load() {
  if (Init()) LOG(ANDROID_LOG_INFO, "загружен, хук на DoRenderLoop_Internal стоит");
}
