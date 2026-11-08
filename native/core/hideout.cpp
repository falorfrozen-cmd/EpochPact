// The hideout: everything that stands in the place of our own. The list is the player's, in a file: anything of the game can be
// added, moved, turned, sized or taken away, the boss doors too.
//
// A prefab of any map: the catalog (made once per game build from the game's own bundles, tools/build_hideout_catalog.py) names its
// bundle and its asset. AssetBundlesManager.CreateBundleIdArrayLoadRef opens the bundle and all it depends on, counted like the game's own
// loads, and the entry's AssetBundle hands out the prefab. Each thing goes up inside an inactive carrier, so none of its scripts ever
// runs, and its scripts stay off: it is a look and, if it has colliders, something to walk around. Nothing of this is in a save.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "hideout.hpp"
#include "hideout_rules.hpp"
#include "hideout_history.hpp"
#include "hideout_tools.hpp"
#include "hideout_catalog.hpp"
#include "common.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "ingame_assets.hpp"
#include "ingame_window.hpp"
#include "managed.hpp"
#include "monolith.hpp"
#include "report.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <deque>
#include <cstring>

namespace ep::hideout {
namespace {
using namespace managed;
namespace rules = ep::hideout::rules;
enum Kind { kScene = 0, kPrefab = 1, kDoor = 2 };
constexpr int kNoDoor = -100;  // door 0 is the difficulty's
struct Item {
    int id = 0, kind = kPrefab, door = kNoDoor;  // door: the action of the door that goes with it
    std::string name, guid;
    float right = 0, up = 0, lift = 0, yaw = 0, size = 1;
    bool off = false;  // a door the player took away; it can come back
    std::unique_ptr<Root> carrier;
    bool waiting = false, failed = false, fit = false;  // fit: just added, made to fit and stand on the ground
    float maxSpan = 14;
    bool centered = false, walkable = false;
    bool locked=false;
    int group=0;
    int settle = 0, materialFixes = 0;
};
struct Entry { std::string name, guid; int file; char category;std::string lower; };
// A texture for the ground: a material of this scene by its name, or the first material of a prefab of any map. period: how many units one repeat covers.
struct GroundTexture { const char* label; const char* material; const char* prefab; float period; bool natural; float light; float u0 = 0, u1 = 1, v0 = 0, v1 = 1; };
constexpr GroundTexture kTextures[]{
    {"Cracked stone", nullptr, "Floor_Stone_Circle", 16, true, .7f}, {"Rocky ground", nullptr, "SandstoneRockyGroundVSPro", 24, true, .7f}, {"Dunes", "MA_Desert_SandDuneLarge Light", nullptr, 36, true, 1},
    {"Grey rubble", nullptr, "RubbleDenseVSPro", 24, true, 1}, {"Mossy earth", nullptr, "Mossy Embankment_titfbczfa", 12, true, 1}, {"Hive floor", nullptr, "DraalBiomassVSPro", 24, true, 1},
    {"Lava", nullptr, "Nordic Beach Rock Formation_Lava", 24, true, 1}, {"Flesh", nullptr, "FleshObjectVSPro", 24, true, 1},
    // a laid floor's picture centred on the middle of the hall, where a window starts at .5
    {"Council chamber", nullptr, "CouncilChamberFloor_Eterra_01", 8, false, 1}, {"Lagon", nullptr, "LagonFloor_Simple", 8, false, 1},
    {"Morditas arena", nullptr, "Morditas_ArenaFloor", 36, false, 1, .5f, 1, .5f, 1}, {"Ornate floor", nullptr, "Floor_01_I_Ang_18", 16, false, 1, .5f, 1, .5f, 1},
};
constexpr int kTextureCount = static_cast<int>(sizeof(kTextures) / sizeof(kTextures[0]));
int groundShape = rules::kRound, groundTexture = 0;
float groundSize = 38.f;
std::string groundGuid;  // a floor of another map on its way
std::vector<Item> items;
bool itemsLoaded = false;
int nextId = 1;
std::vector<Entry> catalog;
std::map<std::string, size_t> byGuid, byName;
int catalogBundles = 0;
bool catalogRead = false;
bool catalogVerified = false;
std::unique_ptr<Root> holderRoot;
std::map<int, std::unique_ptr<Root>> openBundles;  // file index -> the game's load reference, while the things of it stand
std::map<int, std::unique_ptr<Root>> previewBundles; // short-lived, separate ownership from placed decoration
std::map<std::string,std::unique_ptr<Root>> placedAssetRequests,previewAssetRequests;
void* OpenAndTakePrefab(const std::string& guid,bool* waiting,bool preview);
std::map<std::string, void*> sceneSources;
std::unique_ptr<Root> decorationStone;
std::unique_ptr<Root> editorInput;bool previousInputDisabled=false;
void* SceneMaterial(const char* wanted);
bool editing = false;
std::vector<int> editStartIds;
int held = -1;
int uncommittedNew = -1;
std::set<int> selection,uncommittedIds;
int selectionPivot=-1;
int nextGroupId=1;
float gridStep=0,angleStep=15;
bool selectFloors=false;
std::string placementIssue;
std::unique_ptr<Root> selectionMarkers,selectionMarkerMaterial;
uint64_t markerRevision=~uint64_t{0},markerRetry=0;
void DrawSelectionMarkers();
bool Uncommitted(int id){return id==uncommittedNew||uncommittedIds.count(id)!=0;}
std::string State();
void DeleteSelection();
void DuplicateSelection();
void PickSelection(int id,bool toggle=false);
void SaveEditorOptions();
void LoadEditorOptions();
void LoadLibrary();
void RememberModel(const std::string& guid);
bool LibraryCategory(const Entry& entry,const std::string& category);
std::optional<std::string> LibraryCommand(const std::vector<std::string>& args);
std::optional<std::string> ToolsCommand(const std::vector<std::string>& args);
bool wasDown = false;
bool pointerBlocked = false;
bool keyboardBlocked=false;
rules::LayoutHistory layoutHistory;
uint64_t historyRevision=0;
#ifdef EPOCHPACT_TESTING
bool testEditorInput=false;
std::array<SHORT,256> testEditorSamples{};
#endif
rules::EditorInputGate editorKeys;
rules::EditorRotation editorRotation;
std::array<rules::KeyLatch,256> editorKeyLatches{};
uint64_t navAt = 0, doorsAt = 0, keyAt = 0;
using ClickMoveFn = void (*)(void*, bool, float, void*, bool, bool, const il2cpp::Method*);
using MoveToFn = bool (*)(void*, void*, bool, bool, const il2cpp::Method*);
using MouseMoveFn = int (*)(void*,bool,bool,void*,void*,int,const il2cpp::Method*);
ClickMoveFn originalClickMove = nullptr;
MoveToFn originalMoveTo = nullptr;
MouseMoveFn originalMouseMove=nullptr;
game::MethodRef m_clickMove, m_moveTo,m_mouseMove;
void ClickMoveDetour(void* self, bool a, float b, void* c, bool d, bool e, const il2cpp::Method* m) { if (!editing) originalClickMove(self, a, b, c, d, e, m); }
bool MoveToDetour(void* self, void* at, bool a, bool b, const il2cpp::Method* m) { return editing ? false : originalMoveTo(self, at, a, b, m); }
int MouseMoveDetour(void* self,bool down,bool hold,void* point,void* hover,int previous,const il2cpp::Method* m){return editing?0:originalMouseMove(self,down,hold,point,hover,previous,m);}

struct Vec3 { float x, y, z; };
Vec3 PlayerPlace();
struct Quat { float x, y, z, w; };
game::MethodRef Unity(const char* cls, const char* name, int n) { return game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", cls, name, n); }
const il2cpp::Method* Signature(const char* cls, const char* name, std::initializer_list<const char*> parameters, const char* ns = "UnityEngine", const char* image = "UnityEngine.CoreModule.dll") {
    auto& a = il2cpp::api(); auto* c = game::FindClass(image, ns, cls); void* iterator = nullptr;
    if (!c) throw std::runtime_error(std::string("no class ") + cls);
    while (auto* m = a.class_get_methods(c, &iterator)) {
        if (std::string(a.method_get_name(m)) != name || a.method_get_param_count(m) != parameters.size()) continue;
        bool match = true; unsigned i = 0;
        for (const char* expected : parameters) { char* actual = a.type_get_name(a.method_get_param(m, i++)); match &= actual && std::string(actual) == expected; if (actual) a.free(actual); }
        if (match) return m;
    }
    throw std::runtime_error(std::string("no method ") + name);
}
void* TypeObject(const char* image, const char* ns, const char* name) {
    const auto* c = game::FindClass(image, ns, name);
    if (!c) throw std::runtime_error(std::string("no class ") + name);
    return il2cpp::api().type_get_object(il2cpp::api().class_get_type(c));
}
void* NewObject(const char* name) {
    auto& api = il2cpp::api();
    void* object = api.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"));
    void* args[]{api.string_new(name)};
    Invoke(Signature("GameObject", ".ctor", {"System.String"}), object, args);
    return object;
}
void SetChildren(void* object, const char* image, const char* kind, bool enabled) {
    bool all = true; Root type(TypeObject(image, "UnityEngine", kind)); void* args[]{type.Get(), &all};
    void* found = Invoke(Signature("GameObject", "GetComponentsInChildren", {"System.Type", "System.Boolean"}), object, args);
    if (!found) return;
    Root list(found);
    const auto n = Get<uintptr_t>(list.Get(), 0x18);
    for (uintptr_t i = 0; i < n; ++i) {
        void* c = Get<void*>(list.Get(), 0x20 + 8 * i); if (!c) continue;
        bool on = enabled; void* a[]{&on}; Invoke(Method(c, "set_enabled", 1), c, a);
    }
}
// The clone is inactive in the hierarchy. Remove scripts before it can receive Awake,
// not merely set enabled=false (disabled behaviours can still receive Awake).
void StripComponents(void* object, const char* image, const char* kind) {
    bool all = true; Root type(TypeObject(image, "UnityEngine", kind)); void* args[]{type.Get(), &all};
    Root list(Invoke(Signature("GameObject", "GetComponentsInChildren", {"System.Type", "System.Boolean"}), object, args));
    for (uintptr_t k = list.Get() ? Get<uintptr_t>(list.Get(), 0x18) : 0; k > 0; --k) {
        void* c = Get<void*>(list.Get(), 0x20 + 8 * (k - 1)); if (!c || !game::IsAlive(c)) continue;
        void* a[]{c}; Invoke(Signature("Object", "DestroyImmediate", {"UnityEngine.Object"}), nullptr, a);
    }
}
int Sanitize(void* copy, bool collision) {
    StripComponents(copy, "UnityEngine.CoreModule.dll", "MonoBehaviour");
    {
        bool all=true;Root type(TypeObject("UnityEngine.CoreModule.dll","UnityEngine","MonoBehaviour"));void* a[]{type.Get(),&all};
        Root left(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),copy,a));
        if(left.Get() && Get<uintptr_t>(left.Get(),0x18))throw std::runtime_error("This model cannot be made into script-free decoration.");
    }
    StripComponents(copy, "UnityEngine.AnimationModule.dll", "Animator");
    StripComponents(copy, "UnityEngine.AnimationModule.dll", "Animation");
    StripComponents(copy, "UnityEngine.AudioModule.dll", "AudioSource");
    StripComponents(copy, "UnityEngine.PhysicsModule.dll", "Rigidbody");
    // Scene NPCs can be culled by the game's actor visibility scripts. Those
    // scripts are deliberately absent on the copy, so restore its static skin
    // renderer explicitly and let its bounds settle even outside the camera.
    SetChildren(copy,"UnityEngine.CoreModule.dll","SkinnedMeshRenderer",true);
    {
        bool allSkins=true;Root type(TypeObject("UnityEngine.CoreModule.dll","UnityEngine","SkinnedMeshRenderer"));void* a[]{type.Get(),&allSkins};
        Root skins(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),copy,a));
        for(uintptr_t k=0,n=skins.Get()?Get<uintptr_t>(skins.Get(),0x18):0;k<n;++k){void* skin=Get<void*>(skins.Get(),0x20+k*8);bool on=true;void* b[]{&on};Invoke(Unity("SkinnedMeshRenderer","set_updateWhenOffscreen",1),skin,b);}
    }
    SetChildren(copy, "UnityEngine.CoreModule.dll", "Light", false);
    // FX are not reliable decoration. Never run simulations in the library or hideout.
    bool all = true; Root type(TypeObject("UnityEngine.ParticleSystemModule.dll", "UnityEngine", "ParticleSystem")); void* args[]{type.Get(), &all};
    Root list(Invoke(Signature("GameObject", "GetComponentsInChildren", {"System.Type", "System.Boolean"}), copy, args));
    for (uintptr_t k = 0, n = list.Get() ? Get<uintptr_t>(list.Get(), 0x18) : 0; k < n; ++k) {
        void* p = Get<void*>(list.Get(), 0x20 + 8 * k); if (!p) continue;
        bool children = false; int behavior = 0; void* a[]{&children, &behavior};
        Invoke(Signature("ParticleSystem", "Stop", {"System.Boolean", "UnityEngine.ParticleSystemStopBehavior"}, "UnityEngine", "UnityEngine.ParticleSystemModule.dll"), p, a);
    }
    if (!collision) SetChildren(copy, "UnityEngine.PhysicsModule.dll", "Collider", false);
    // Some catalog entries are model-only prefabs with Unity's placeholder
    // material, rather than a finished scene instance. Use an existing game
    // stone material for those surfaces; never invent/import a texture.
    int fixed=0;
    Root rendererType(TypeObject("UnityEngine.CoreModule.dll","UnityEngine","Renderer"));void* ra[]{rendererType.Get(),&all};
    Root renderers(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),copy,ra));
    for(uintptr_t k=0,n=renderers.Get()?Get<uintptr_t>(renderers.Get(),0x18):0;k<n;++k) {
        void* r=Get<void*>(renderers.Get(),0x20+8*k);if(!r)continue;
        Root materials(Invoke(Unity("Renderer","get_sharedMaterials",0),r));bool changed=false;
        for(uintptr_t m=0,count=materials.Get()?Get<uintptr_t>(materials.Get(),0x18):0;m<count;++m) {
            void* mat=Get<void*>(materials.Get(),0x20+8*m);if(!mat)continue;
            const auto name=Text(Invoke(Unity("Object","get_name",0),mat));void* shader=Invoke(Unity("Material","get_shader",0),mat);
            const bool missing=name=="Default-Material" || !shader || !Value<bool>(Unity("Shader","get_isSupported",0),shader);
            if(!missing)continue;
            if(!decorationStone || !game::IsAlive(decorationStone->Get())){void* stone=SceneMaterial("MonolithRocks_Sand");if(stone)decorationStone=std::make_unique<Root>(stone);}
            if(decorationStone && decorationStone->Get()) {
                int index=static_cast<int>(m);void* set[]{decorationStone->Get(),&index};
                Invoke(Signature("Array","SetValue",{"System.Object","System.Int32"},"System","mscorlib.dll"),materials.Get(),set);changed=true;++fixed;
            }
        }
        if(changed){void* a[]{materials.Get()};Invoke(Unity("Renderer","set_sharedMaterials",1),r,a);}
    }
    return fixed;
}
std::filesystem::path FilePath() {
    const auto root = ingame::view::PreferencesFolder();
#ifdef EPOCHPACT_TESTING
    return root.empty() ? root : root / L"hideout-test.txt";  // test games never touch the player's own
#else
    return root.empty() ? root : root / L"hideout.txt";
#endif
}
void ReadCatalog() {
    if (catalogRead) return;
    catalogRead = true;
    const auto bytes = ingame::assets::Bytes("hideout-catalog.tsv");
    std::istringstream in{std::string(bytes)};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("#bundles\t", 0) == 0) { catalogBundles = std::atoi(line.c_str() + 9); continue; }
        if (line.empty() || line[0] == '#') continue;
        const auto a = line.find('\t'), b = line.find('\t', a + 1), c = line.find('\t', b + 1);
        if (c == std::string::npos) continue;
        Entry e{line.substr(c + 1), line.substr(a + 1, b - a - 1), std::atoi(line.substr(0, a).c_str()), line[b + 1]};
        e.lower=rules::CatalogLower(e.name);
        byGuid.emplace(e.guid, catalog.size());
        byName.emplace(e.name, catalog.size());
        catalog.push_back(std::move(e));
    }
    Log("hideout: catalog of %zu things", catalog.size());
}
// The catalog fits this game's bundles only.
bool CatalogFits() {
    if(catalogVerified)return true;
    ReadCatalog();
    if (catalog.empty()) return false;
    void* manager = Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_InstanceOrNull", 0));
    if (!manager) return false;
    void* list = Invoke(Method(manager, "get_Catalog", 0), manager);
    void* files = list ? Get<void*>(list, game::FieldOffset("LE.Addressables.dll", "", "LEAssetBundlesCatalog", "bundleFileNames")) : nullptr;
    catalogVerified=files && static_cast<int>(Get<uintptr_t>(files, 0x18)) == catalogBundles;return catalogVerified;
}
void AddDefault(int kind, int door, const char* name, float right, float up, float yaw = 0, float size = 1) {
    Item item;
    item.id = nextId++; item.kind = kind; item.door = door; item.name = name; item.right = right; item.up = up; item.yaw = yaw; item.size = size;
    if (kind == kPrefab) {
        const auto it = byName.find(name);
        if (it == byName.end()) {
            if (door != kNoDoor) { item.kind = kDoor; item.name.clear(); } else return;  // a door keeps working as a plain rock
        } else item.guid = catalog[it->second].guid;
    }
    items.push_back(std::move(item));
}
using J = ingame::json::Value;
const J& Designs() {
    static J data;
    if (!data.IsObject() && !ingame::json::Parse(ingame::assets::Bytes("hideout-designs.json"), data)) throw std::runtime_error("Hideout designs are missing from this build.");
    return data["designs"];
}
int DesignShape(const J& d) {
    const std::string s = d["shape"].Str();
    return s == "square" ? rules::kSquare : s == "hall" ? rules::kHall : s == "cross" ? rules::kCross : s == "islands" ? rules::kIslands : rules::kRound;
}
void ApplyDesign(int index) {
    const auto& designs = Designs().items;
    if (index < 0 || index >= static_cast<int>(designs.size())) throw std::runtime_error("No such template.");
    const auto& d = designs[index];
    std::vector<Item> result;
    for (const auto& o : d["items"].items) {
        Item i; i.id = nextId++; i.kind = static_cast<int>(o["kind"].Num()); i.door = static_cast<int>(o["door"].Num(kNoDoor));
        i.name = o["name"].Str(); i.guid = o["guid"].Str(); i.right = static_cast<float>(o["right"].Num()); i.up = static_cast<float>(o["up"].Num());
        i.lift = static_cast<float>(o["lift"].Num()); i.yaw = static_cast<float>(o["yaw"].Num()); i.size = static_cast<float>(o["size"].Num(1));
        i.maxSpan = static_cast<float>(o["maxSpan"].Num(14)); i.walkable = o["walkable"].boolean; i.centered = true; i.fit = o["fit"].IsBool()?o["fit"].boolean:i.maxSpan>0;
        if (!rules::FinitePlacement(i.right,i.up,i.lift,i.yaw,i.size) || !rules::Inside(DesignShape(d),static_cast<float>(d["size"].Num()),i.right,i.up)) throw std::runtime_error("Invalid built-in placement.");
        if (i.kind == kPrefab && !byGuid.count(i.guid)) throw std::runtime_error("Template asset is missing from the catalog.");
        result.push_back(std::move(i));
    }
    if (result.empty()) throw std::runtime_error("Empty built-in template.");
    items = std::move(result); groundShape = DesignShape(d); groundSize = static_cast<float>(d["size"].Num(38));
    for (int k = 0; k < kTextureCount; ++k) if (d["texture"].Str() == kTextures[k].label) groundTexture = k;
}
void Defaults() {
    ApplyDesign(0);
}

void WriteLayout(std::ostream& out) {
    out << "EpochPact hideout 1\n";
    out << "ground\t" << groundShape << '\t' << groundSize << '\t' << kTextures[groundTexture].label << '\n';
    for (const auto& i : items) if(!Uncommitted(i.id))
        out << i.kind << '\t' << i.door << '\t' << i.right << '\t' << i.up << '\t' << i.lift << '\t' << i.yaw << '\t' << i.size << '\t' << (i.off ? 1 : 0) << '\t' << i.guid << '\t' << i.name << '\t' << i.maxSpan << '\t' << i.centered << '\t' << i.walkable << '\t' << i.fit << '\t' << i.locked << '\t' << i.group << '\n';
}
std::vector<int> CompletedItemIds(){std::vector<int> out;for(const auto& i:items)if(!Uncommitted(i.id))out.push_back(i.id);return out;}
std::string LayoutSnapshot(){std::ostringstream out;WriteLayout(out);return out.str();}
std::filesystem::path UndoPath();
void RollbackFailedSave();
bool WriteFile(const std::filesystem::path& path,const std::string* snapshot=nullptr) {
    if (path.empty()) return false;
    std::error_code folderError;std::filesystem::create_directories(path.parent_path(),folderError);if(folderError)return false;
    auto temp = path; temp += L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if(snapshot)out<<*snapshot;else WriteLayout(out);
        if (!out) return false;
    }
    return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
void Save(bool record=true) {
    const auto snapshot=LayoutSnapshot();
    if(record&&!layoutHistory.Empty()&&snapshot!=layoutHistory.Current()){
        const auto before=layoutHistory.Current();
        if(!WriteFile(UndoPath(),&before)){RollbackFailedSave();throw std::runtime_error("Could not save the recovery layout; the edit was reverted.");}
    }
    if(!WriteFile(FilePath(),&snapshot)){
        if(record)RollbackFailedSave();
        throw std::runtime_error("The hideout could not be saved. Check the local preferences folder permissions.");
    }
    if(record){if(layoutHistory.Record(snapshot,CompletedItemIds()))++historyRevision;}
    else layoutHistory.ReplaceCurrent(snapshot,CompletedItemIds());
}
// A saved layout, the player's own or a friend's. Nothing changes unless the whole first line is ours and a door or a thing can be read.
bool ReadLayout(std::istream& in,bool apply=true,std::vector<Item>* output=nullptr,int* outShape=nullptr,float* outSize=nullptr,int* outTexture=nullptr) {
    std::string line;
    if (!in || !std::getline(in, line) || line.rfind("EpochPact hideout 1", 0) != 0) return false;
    std::vector<Item> read;
    int next=nextId;
    int shape = rules::kRound, texture = 0;
    float size = 38.f;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> p; std::string part; std::istringstream row(line);
        while (std::getline(row, part, '\t')) p.push_back(part);
        if (p.size() >= 4 && p[0] == "ground") {
            shape = std::clamp(std::atoi(p[1].c_str()), 0, rules::kShapes - 1);
            size = std::clamp(std::strtof(p[2].c_str(), nullptr), rules::kMinSize, rules::kMaxSize);
            for (int k = 0; k < kTextureCount; ++k) if (p[3] == kTextures[k].label) texture = k;
            continue;
        }
        if (p.size() < 8) continue;
        Item item;
        item.id = next++; item.kind = std::atoi(p[0].c_str()); item.door = std::atoi(p[1].c_str());
        item.right = std::strtof(p[2].c_str(), nullptr); item.up = std::strtof(p[3].c_str(), nullptr); item.lift = std::strtof(p[4].c_str(), nullptr);
        item.yaw = std::strtof(p[5].c_str(), nullptr); item.size = std::strtof(p[6].c_str(), nullptr); item.off = p[7] == "1";
        if (p.size() > 8) item.guid = p[8];
        if (p.size() > 9) item.name = p[9];
        if (p.size() > 10) item.maxSpan = std::strtof(p[10].c_str(),nullptr);
        if (p.size() > 11) item.centered = p[11] == "1";
        if (p.size() > 12) item.walkable = p[12] == "1";
        if (p.size() > 13) item.fit = p[13] == "1";
        if(p.size()>14)item.locked=p[14]=="1";
        if(p.size()>15)item.group=std::atoi(p[15].c_str());
        if (!rules::FinitePlacement(item.right,item.up,item.lift,item.yaw,item.size) || !std::isfinite(item.maxSpan) || item.maxSpan < 0 || item.maxSpan > 100 || read.size() >= rules::kMaxItems || item.kind < 0 || item.kind > 2||item.group<0||item.group>1000000) return false;
        if (item.kind == kPrefab && !byGuid.count(item.guid)) return false;
        read.push_back(std::move(item));
    }
    if (read.empty() || !std::isfinite(size)) return false;
    if (std::none_of(read.begin(),read.end(),[](const Item& i){return i.door == monolith::kHallOut && !i.off;})) return false;
    if(outShape)*outShape=shape;if(outSize)*outSize=size;if(outTexture)*outTexture=texture;
    if(output)*output=std::move(read);
    if(apply){items=std::move(read);nextId=next;groundShape=shape;groundSize=size;groundTexture=texture;selection.clear();nextGroupId=1;for(const auto& i:items)nextGroupId=std::max(nextGroupId,i.group+1);}
    return true;
}
bool ReadFile(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return ReadLayout(in);}
void RollbackFailedSave(){
    if(layoutHistory.Empty())return;
    const auto before=layoutHistory.Current();Clear();std::istringstream in(before);
    if(!ReadLayout(in))throw std::runtime_error("Could not restore the last completed layout.");
    monolith::HallRebuildAll();layoutHistory.ReplaceCurrent(before,CompletedItemIds());++historyRevision;
}
void LoadItems() {
    if (itemsLoaded) return;
    itemsLoaded = true;
    ReadCatalog();
    if (!ReadFile(FilePath())) Defaults();
    LoadEditorOptions();
    LoadLibrary();
    const auto current=LayoutSnapshot();layoutHistory.Reset(current,CompletedItemIds());
    std::ifstream before(UndoPath(),std::ios::binary);
    const std::string prior((std::istreambuf_iterator<char>(before)),std::istreambuf_iterator<char>());
    std::istringstream check(prior);
    if(prior!=current&&ReadLayout(check,false)){layoutHistory.Reset(prior);layoutHistory.Record(current);}
}
std::filesystem::path TemplatesFolder() {
    const auto root = ingame::view::PreferencesFolder();
#ifdef EPOCHPACT_TESTING
    return root.empty() ? root : root / L"hideout-templates-test";
#else
    return root.empty() ? root : root / L"hideout-templates";
#endif
}
std::filesystem::path TemplatePath(const std::string& name) {
    if (!rules::ValidName(name.c_str())) throw std::runtime_error("A name is letters, digits, spaces, - and _, up to 40.");
    const auto folder = TemplatesFolder();
    if (folder.empty()) throw std::runtime_error("No place to keep templates.");
    return folder / std::filesystem::path(name + ".txt");
}
#include "hideout_templates.inl"
std::filesystem::path UndoPath() {
    auto path = FilePath();
    if (!path.empty()) path += L".before";
    return path;
}
Item* Find(int id) { for (auto& i : items) if (i.id == id) return &i; return nullptr; }
bool Alive(const Item& i) { return i.carrier && i.carrier->Get() && game::IsAlive(i.carrier->Get()); }
Vec3 World(const Item& i) {
    float x = 0, z = 0; monolith::HallWorld(i.right, i.up, &x, &z);
    return {x, monolith::HallGroundY() + i.lift, z};
}
void PlaceCarrier(const Item& i) {
    if (!Alive(i)) return;
    Root tr(Invoke(Method(i.carrier->Get(), "get_transform", 0), i.carrier->Get()));
    Vec3 at = World(i); void* pa[]{&at}; Invoke(Unity("Transform", "set_position", 1), tr.Get(), pa);
    const float half = i.yaw * 3.14159265f / 360.f;
    Quat turn{0, std::sin(half), 0, std::cos(half)}; void* ra[]{&turn}; Invoke(Unity("Transform", "set_rotation", 1), tr.Get(), ra);
    Vec3 sized{i.size, i.size, i.size}; void* sa[]{&sized}; Invoke(Unity("Transform", "set_localScale", 1), tr.Get(), sa);
}
struct VisualBounds { Vec3 c{}, e{}; bool valid = false; };
VisualBounds Measure(void* object) {
    if(!object||!game::IsAlive(object))return {};
    Vec3 lo{1e9f,1e9f,1e9f}, hi{-1e9f,-1e9f,-1e9f}; bool seen = false;
    for (const char* kind : {"MeshRenderer","SkinnedMeshRenderer"}) {
        bool all = false; Root type(TypeObject("UnityEngine.CoreModule.dll","UnityEngine",kind)); void* a[]{type.Get(),&all};
        Root list(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),object,a));
        for (uintptr_t k=0,n=list.Get()?Get<uintptr_t>(list.Get(),0x18):0;k<n && k<4096;++k) {
            void* r=Get<void*>(list.Get(),0x20+8*k); if (!r || !Value<bool>(Unity("Renderer","get_enabled",0),r)) continue;
            struct B { Vec3 c,e; }; const auto b=Value<B>(Unity("Renderer","get_bounds",0),r);
            if (!std::isfinite(b.c.x+b.c.y+b.c.z+b.e.x+b.e.y+b.e.z) || std::max({b.e.x,b.e.y,b.e.z}) <= 0) continue;
            lo={std::min(lo.x,b.c.x-b.e.x),std::min(lo.y,b.c.y-b.e.y),std::min(lo.z,b.c.z-b.e.z)};
            hi={std::max(hi.x,b.c.x+b.e.x),std::max(hi.y,b.c.y+b.e.y),std::max(hi.z,b.c.z+b.e.z)}; seen=true;
        }
    }
    if(!seen)return {};
    return {{(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f},{(hi.x-lo.x)*.5f,(hi.y-lo.y)*.5f,(hi.z-lo.z)*.5f},true};
}
void CenterCopy(void* copy,void* carrier) {
    const auto b=Measure(copy); if (!b.valid) return;
    Vec3 foot{b.c.x,b.c.y-b.e.y,b.c.z}; void* a[]{&foot};
    const auto local=Value<Vec3>(Signature("Transform","InverseTransformPoint",{"UnityEngine.Vector3"}),carrier,a);
    Root tr(Invoke(Unity("GameObject","get_transform",0),copy));
    auto position=Value<Vec3>(Unity("Transform","get_localPosition",0),tr.Get());
    position={position.x-local.x,position.y-local.y,position.z-local.z}; void* p[]{&position};
    Invoke(Unity("Transform","set_localPosition",1),tr.Get(),p);
}
// Preserve the user's lift. Normalize the prefab pivot separately and persist the
// resolved scale after asynchronous loading, so re-entering produces the same object.
bool FitToGround(Item& i,void* copy) {
    const auto b=Measure(copy); if (!b.valid) return false;
    const float largest=2*std::max({b.e.x,b.e.y,b.e.z});
    if (i.fit && i.maxSpan>0 && largest>0) i.size*=std::min(1.f,i.maxSpan/largest);
    if (i.centered) {Root tr(Invoke(Unity("GameObject","get_transform",0),i.carrier->Get()));CenterCopy(copy,tr.Get());}
    i.fit=false;i.failed=false; PlaceCarrier(i);return true;
}
// Under a new carrier that stays inactive until every script is off.
void PutUp(Item& i, void* source, bool keepWorldTurn) {
    void* holder = holderRoot ? holderRoot->Get() : nullptr;
    if (!holder || !game::IsAlive(holder)) return;
    void* carrier = NewObject(i.door != kNoDoor ? "EP station" : "EP thing");
    i.carrier = std::make_unique<Root>(carrier);
    bool off = false; void* oa[]{&off}; Invoke(Method(carrier, "SetActive", 1), carrier, oa);
    Root parent(Invoke(Method(holder, "get_transform", 0), holder));
    Root carrierTransform(Invoke(Method(carrier, "get_transform", 0), carrier));
    void* pa[]{parent.Get()}; Invoke(Signature("Transform", "SetParent", {"UnityEngine.Transform"}), carrierTransform.Get(), pa);
    Quat turned{0, 0, 0, 1}; Vec3 scale{1, 1, 1};
    if (keepWorldTurn) {
        Root st(Invoke(Method(source, "get_transform", 0), source));
        turned = Value<Quat>(Unity("Transform", "get_rotation", 0), st.Get());
        scale = Value<Vec3>(Unity("Transform", "get_lossyScale", 0), st.Get());
    }
    bool world = false; void* args[]{source, carrierTransform.Get(), &world};
    void* copy = Invoke(Signature("Object", "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform", "System.Boolean"}), nullptr, args);
    Root tr(Invoke(Method(copy, "get_transform", 0), copy));
    Vec3 zero{}; void* za[]{&zero}; Invoke(Unity("Transform", "set_localPosition", 1), tr.Get(), za);
    if (keepWorldTurn) {
        void* ra[]{&turned}; Invoke(Unity("Transform", "set_localRotation", 1), tr.Get(), ra);
        void* sa[]{&scale}; Invoke(Unity("Transform", "set_localScale", 1), tr.Get(), sa);
    }
    i.materialFixes=Sanitize(copy, i.door == kNoDoor && !i.walkable);
    PlaceCarrier(i);
    bool on = true; void* na[]{&on}; Invoke(Method(copy, "SetActive", 1), copy, na); Invoke(Method(carrier, "SetActive", 1), carrier, na);
    if ((i.fit || i.centered) && !FitToGround(i, copy)) i.settle=8;
}
void StartLoad(Item& i) {
    ReadCatalog();
    const auto it = byGuid.find(i.guid);
    if (it == byGuid.end() || !CatalogFits()) { i.failed = true; return; }
    const int file = catalog[it->second].file;
    i.waiting = true; i.failed = false;
    if (openBundles.count(file)) return;
    void* manager = Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_InstanceOrNull", 0));
    void* list = Invoke(Method(manager, "get_Catalog", 0), manager);
    const uint16_t begin = Value<uint16_t>(Method(list, "get_AllBundleIdsBegin", 0), list);
    const uint16_t id = static_cast<uint16_t>(begin + file);
    Root ids(il2cpp::api().array_new(game::FindClass("LE.Addressables.dll", "LE.AssetBundles", "BundleId"), 1));
    *reinterpret_cast<uint16_t*>(static_cast<char*>(ids.Get()) + 0x20) = id;
    void* caller = il2cpp::api().string_new("epochpact"); int line = 1; void* ca[]{ids.Get(), caller, &line};
    openBundles[file] = std::make_unique<Root>(Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "CreateBundleIdArrayLoadRef", 3), nullptr, ca));
}
// A prefab whose bundle has opened goes up; returns whether anything is still on its way.
bool StepLoads() {
    bool waiting = false; int placed = 0;
    for (auto& i : items) {
        if(i.settle && Alive(i)) {
            --i.settle;
            Root tr(Invoke(Unity("GameObject","get_transform",0),i.carrier->Get()));int child=0;void* a[]{&child};
            void* ct=Invoke(Unity("Transform","GetChild",1),tr.Get(),a);void* copy=ct?Invoke(Unity("Component","get_gameObject",0),ct):nullptr;
            if(copy && FitToGround(i,copy)){i.settle=0;if(!Uncommitted(i.id))Save(false);navAt=GetTickCount64()+400;if(i.door!=kNoDoor)doorsAt=GetTickCount64()+50;}
            else if(!i.settle)i.failed=true;
        }
        if (!i.waiting || Alive(i)) { i.waiting = false; continue; }
        const auto it = byGuid.find(i.guid);
        if (it == byGuid.end()) { i.waiting = false; i.failed = true; continue; }
        if (placed >= 4) { i.waiting = true; waiting = true; continue; }  // a few each frame
        bool pending=false;void* prefab=OpenAndTakePrefab(i.guid,&pending,false);
        if(pending){waiting=true;continue;}i.waiting=false;
        if (!prefab) { i.failed = true; Log("hideout: %s is not in its bundle", i.name.c_str()); continue; }
        const bool fit = i.fit;
        try {PutUp(i, prefab, false);}
        catch(const std::exception& e) {
            i.failed=true;Log("hideout placement %s: %s",i.name.c_str(),e.what());
            if(Alive(i)){void* a[]{i.carrier->Get()};Invoke(Unity("Object","Destroy",1),nullptr,a);i.carrier.reset();}
        }
        if(fit && !i.failed && !Uncommitted(i.id))try{Save(false);}catch(const std::exception& e){Log("hideout save: %s",e.what());}
        ++placed;
        navAt = GetTickCount64() + 400;
        if(i.door!=kNoDoor)doorsAt=GetTickCount64()+50;
    }
    return waiting;
}
void Install() {
    if (!m_clickMove) m_clickMove = game::FindMethod("LE.dll", "", "MovingPlayer", "MouseClickMoveCommand", 5);
    if (!m_moveTo) m_moveTo = game::FindMethod("LE.dll", "", "MovingPlayer", "MoveToPoint", 3);
    if(!m_mouseMove)m_mouseMove=game::FindMethod("LE.dll","","MouseMovementProcessor","ProcessMovement",5);
    if(!m_clickMove || !m_moveTo || !m_mouseMove)throw std::runtime_error("This game build does not support hideout edit input.");
    std::string why;
    if (m_clickMove && !hook::IsInstalled(m_clickMove.code) && !hook::Install(m_clickMove.code, reinterpret_cast<void*>(&ClickMoveDetour), reinterpret_cast<void**>(&originalClickMove), &why)) throw std::runtime_error(why);
    if (m_moveTo && !hook::IsInstalled(m_moveTo.code) && !hook::Install(m_moveTo.code, reinterpret_cast<void*>(&MoveToDetour), reinterpret_cast<void**>(&originalMoveTo), &why)) throw std::runtime_error(why);
    if(!hook::IsInstalled(m_mouseMove.code)&&!hook::Install(m_mouseMove.code,reinterpret_cast<void*>(&MouseMoveDetour),reinterpret_cast<void**>(&originalMouseMove),&why))throw std::runtime_error(why);
    void* input=game::StaticObject(game::FindStaticField("LE.dll","","EpochInputManager","instance"));
    if(!input)throw std::runtime_error("Game input is not ready for hideout editing.");
    if(void* processor=Get<void*>(input,game::FieldOffset("LE.dll","","EpochInputManager","movementProcessor")))Invoke(Method(processor,"StopActiveMovement",0),processor);
    const auto offset=Offset("EpochInputManager","forceDisableInput");previousInputDisabled=Get<bool>(input,offset);editorInput=std::make_unique<Root>(input);
    *reinterpret_cast<bool*>(static_cast<char*>(input)+offset)=true;
}
void Uninstall() {
    if(editorInput){if(void* input=editorInput->Get())*reinterpret_cast<bool*>(static_cast<char*>(input)+Offset("EpochInputManager","forceDisableInput"))=previousInputDisabled;editorInput.reset();}
    std::string why;
    if (m_clickMove && hook::IsInstalled(m_clickMove.code)) hook::Remove(m_clickMove.code, &why);
    if (m_moveTo && hook::IsInstalled(m_moveTo.code)) hook::Remove(m_moveTo.code, &why);
    if(m_mouseMove && hook::IsInstalled(m_mouseMove.code))hook::Remove(m_mouseMove.code,&why);
}
void EndPlacement(bool commit);
void BeginPlacement(int id,bool added=false);
void SetEditing(bool on) {
    if (on == editing) return;
    if (on) { if (!PlayerInside()) throw std::runtime_error("Enter the hideout before editing."); auto start=FilePath();start+=L".edit-start";if (!WriteFile(start))throw std::runtime_error("Could not back up the edit session."); try{Install();}catch(...){Uninstall();throw;}editStartIds.clear();for(const auto& i:items)if(!Uncommitted(i.id))editStartIds.push_back(i.id);editing = true; wasDown = true; editorKeys={}; editorRotation={}; editorKeyLatches={}; }
    else { EndPlacement(true);editing = false; held = -1; Uninstall(); Save();editStartIds.clear(); }
}
void Changed(Item& i, bool doors) {
    if (!rules::FinitePlacement(i.right,i.up,i.lift,i.yaw,i.size)) throw std::runtime_error("Invalid object transform.");
    PlaceCarrier(i);
    Save();
    navAt = GetTickCount64() + 400;
    if (i.door != kNoDoor) doorsAt = GetTickCount64() + 50;
    (void)doors;
}
bool GameInFront() {
#ifdef EPOCHPACT_TESTING
    if(testEditorInput)return true;
#endif
    HWND window = GetForegroundWindow();
    DWORD pid = 0; if (window) GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}
Item* Nearest(float right, float up, float reach) {
    Item* best = nullptr; float bestDistance = reach * reach;
    for (auto& i : items) {
        if (i.off) continue;
        const float d = (i.right - right) * (i.right - right) + (i.up - up) * (i.up - up);
        if (d <= bestDistance) { bestDistance = d; best = &i; }
    }
    return best;
}
SHORT EditorKeyState(int key){
#ifdef EPOCHPACT_TESTING
    if(testEditorInput)return testEditorSamples[key];
#endif
    return GetAsyncKeyState(key);
}
bool Tapped(int key, bool repeat = true) {
    const SHORT state = EditorKeyState(key);
    // The high-bit edge works even if another reader consumed the unreliable
    // recent-press bit. Retain that bit only as a best-effort short-tap fallback.
    const bool down=(state&0x8000)!=0;
    const bool pressed=editorKeyLatches[key].Pressed(down,(state&1)!=0);
    const uint64_t now = GetTickCount64();
    if (pressed || (down && repeat && now >= keyAt)) { keyAt = now + 110; return true; }
    return false;
}
// A door can come back; anything else is gone from the list.
void RemoveItem(int id,bool persist=true) {
    Item* i = Find(id);
    if (!i) return;
    if(i->door==monolith::kHallOut)throw std::runtime_error("The way out stays.");
    if(i->locked)throw std::runtime_error("Unlock this object before deleting it.");
    if(id==held){EndPlacement(false);i=Find(id);if(!i)return;}
    if (Alive(*i)) { void* a[]{i->carrier->Get()}; Invoke(Unity("Object", "Destroy", 1), nullptr, a); i->carrier.reset(); }
    if (i->door != kNoDoor) { i->off = true; doorsAt = GetTickCount64() + 50; }
    else items.erase(std::remove_if(items.begin(), items.end(), [id](const Item& x) { return x.id == id; }), items.end());
    selection.erase(id);uncommittedIds.erase(id);
    navAt = GetTickCount64() + 400;
    if(persist)Save();
}
#include "hideout_placement.inl"
void RestoreEditItems(std::vector<Item> target,bool useEditStartIds);
void ApplyHistory(bool redo){
    EndPlacement(false);
    if(redo?!layoutHistory.CanRedo():!layoutHistory.CanUndo())throw std::runtime_error(redo?"Nothing to redo.":"Nothing to undo.");
    const auto before=LayoutSnapshot(),target=redo?layoutHistory.RedoTarget():layoutHistory.UndoTarget();
    const auto beforeIds=CompletedItemIds(),targetIds=redo?layoutHistory.RedoIds():layoutHistory.UndoIds();
    struct Parsed {std::vector<Item> items;int shape=0,texture=0;float size=0;};
    const auto parse=[&](const std::string& text,const std::vector<int>& ids){
        Parsed p;std::istringstream input(text);if(!ReadLayout(input,false,&p.items,&p.shape,&p.size,&p.texture))throw std::runtime_error("The history layout is invalid.");
        if(ids.size()==p.items.size())for(size_t k=0;k<ids.size();++k)p.items[k].id=ids[k];return p;
    };
    auto desired=parse(target,targetIds);
    const auto apply=[&](Parsed p){
        if(p.shape==groundShape&&p.size==groundSize&&p.texture==groundTexture){RestoreEditItems(std::move(p.items),false);return;}
        items=std::move(p.items);groundShape=p.shape;groundSize=p.size;groundTexture=p.texture;selection.clear();selectionPivot=-1;nextGroupId=1;
        for(const auto& i:items){nextId=std::max(nextId,i.id+1);nextGroupId=std::max(nextGroupId,i.group+1);}
        Clear();monolith::HallRebuildAll();
    };
    try{
        apply(std::move(desired));
        if(!WriteFile(FilePath()))throw std::runtime_error("Could not save the restored layout; history has not advanced.");
    }catch(...){
        try{apply(parse(before,beforeIds));}
        catch(...){std::istringstream rollback(before);ReadLayout(rollback);Clear();monolith::HallRebuildAll();}
        layoutHistory.ReplaceCurrent(before,CompletedItemIds());
        throw;
    }
    if(redo)layoutHistory.CommitRedo();else layoutHistory.CommitUndo();
    layoutHistory.ReplaceCurrent(LayoutSnapshot(),CompletedItemIds());++historyRevision;
}
// Restore just edited objects, preserving the floor, loaded bundles and native
// instances of every surviving object. Ground changes still need a new mesh.
void RestoreEditItems(std::vector<Item> target,bool useEditStartIds){
    std::vector<rules::ObjectIdentity> wanted,current;
    for(size_t k=0;k<target.size();++k){auto& i=target[k];if(useEditStartIds&&editStartIds.size()==target.size())i.id=editStartIds[k];wanted.push_back({i.id,i.kind,i.door,i.guid,i.name});}
    for(const auto& i:items)current.push_back({i.id,i.kind,i.door,i.guid,i.name});
    const auto matches=rules::MatchRestoredObjects(wanted,current);std::vector<bool> kept(items.size());std::vector<Item> restored;restored.reserve(target.size());
    const auto remove=[](Item& i){if(Alive(i)){void* a[]{i.carrier->Get()};Invoke(Unity("Object","Destroy",1),nullptr,a);}i.carrier.reset();i.waiting=false;i.settle=0;};
    for(size_t k=0;k<target.size();++k){auto& desired=target[k];const int at=matches[k];
        if(at<0){desired.id=nextId++;restored.push_back(std::move(desired));continue;}
        kept[at]=true;auto result=std::move(items[at]);
        if(desired.off||desired.fit||desired.walkable!=result.walkable||desired.centered!=result.centered)remove(result);
        result.kind=desired.kind;result.door=desired.door;result.name=desired.name;result.guid=desired.guid;
        result.right=desired.right;result.up=desired.up;result.lift=desired.lift;result.yaw=desired.yaw;result.size=desired.size;result.off=desired.off;
        result.maxSpan=desired.maxSpan;result.centered=desired.centered;result.walkable=desired.walkable;result.fit=desired.fit;result.locked=desired.locked;result.group=desired.group;
        restored.push_back(std::move(result));
    }
    for(size_t k=0;k<items.size();++k)if(!kept[k])remove(items[k]);
    items=std::move(restored);selection.clear();selectionPivot=-1;nextGroupId=1;
    for(auto& i:items){nextGroupId=std::max(nextGroupId,i.group+1);if(i.off||i.kind==kDoor)continue;
        if(Alive(i)){PlaceCarrier(i);continue;}
        if(i.waiting)continue;
        if(i.kind==kScene){const auto source=sceneSources.find(i.name);if(source!=sceneSources.end())PutUp(i,source->second,true);else i.failed=true;}
        else StartLoad(i);
    }
    doorsAt=GetTickCount64()+50;navAt=GetTickCount64()+400;++historyRevision;
}
float RotateSelection(bool active,unsigned down,unsigned tapped,uint64_t now) {
    if(!active||!editing||held<0||!Find(held)){editorRotation={};return 0;}
    const float degrees=editorRotation.Step(down,tapped,now)*angleStep/15.f;
    placement.yaw+=degrees;return degrees;
}
// Project the cursor onto the hideout plane, independent of hover tags,
// scenery colliders and the player's current mouse target.
void EditFrame() {
    const bool swapped=GetSystemMetrics(SM_SWAPBUTTON)!=0;
    const int primary=rules::PrimaryMouse(swapped),cancel=rules::CancelMouse(swapped);
    void* manager=editorInput?editorInput->Get():nullptr;
    static const auto typingOffset=Offset("EpochInputManager","isInputFieldActive");
    const bool typing=manager&&Get<bool>(manager,typingOffset);
    const bool active=PlayerInside()&&!keyboardBlocked&&!typing&&!ingame::view::WindowOpen()&&GameInFront();
    bool anyHeld=false;
    if(!active||!editorKeys.armed)for(int key:{primary,cancel,VK_LEFT,VK_RIGHT,VK_UP,VK_DOWN,VK_PRIOR,VK_NEXT,VK_DELETE,VK_END,int('Q'),int('E'),int('Z'),int('Y'),int('D'),int('G')}){
        const auto state=EditorKeyState(key);const bool held=(state&0x8000)!=0;anyHeld|=held;editorKeyLatches[key].down=held;if(key==primary)wasDown=held;
    }
    if(!editorKeys.Allow(active,anyHeld)){editorRotation={};return;}
    const bool z=Tapped('Z',false),y=Tapped('Y',false),control=(EditorKeyState(VK_CONTROL)&0x8000)!=0;
    if(control&&(z||y)){
        const bool redo=y||(z&&(EditorKeyState(VK_SHIFT)&0x8000)!=0);
        if(redo?layoutHistory.CanRedo():layoutHistory.CanUndo())ApplyHistory(redo);
        return;
    }
    const bool duplicate=Tapped('D',false),group=Tapped('G',false);
    if(control&&duplicate&&!selection.empty()){DuplicateSelection();return;}
    if(control&&group&&!selection.empty()){ToolsCommand({"selection",(EditorKeyState(VK_SHIFT)&0x8000)?"ungroup":"group"});return;}
    if(Tapped(VK_END,false)){SetEditing(false);return;}
    Vec3 origin{},direction{};float right=0,up=0;bool valid=!pointerBlocked&&CursorGround(origin,direction,right,up);
    if(gridStep>0){right=rules::Snap(right,gridStep);up=rules::Snap(up,gridStep);}
    if(held>=0&&!pointerBlocked)UpdatePlacement(right,up,valid);
    const bool down = (EditorKeyState(primary) & 0x8000) != 0;
    if (down && !wasDown && valid) {
        if(control){EndPlacement(false);if(Item* closest=PickAtRay(origin,direction))PickSelection(closest->id,true);}
        else if (held>=0) {if(PlacementReady())EndPlacement(true);}
        else if (Item* closest = PickAtRay(origin,direction)) {if(!selection.count(closest->id))PickSelection(closest->id);BeginPlacement(closest->id);UpdatePlacement(right,up,valid);}
        else if(!selection.empty()){selection.clear();++historyRevision;}
    }
    wasDown = down;
    if(Tapped(cancel,false)){EndPlacement(false);selection.clear();++historyRevision;return;}
    unsigned rotateDown=0,rotateTapped=0;
    for(unsigned k=0;k<rules::kRotationKeys.size();++k){
        const int key=rules::kRotationKeys[k];
        if(Tapped(key,false))rotateTapped|=1u<<k;
        if(editorKeyLatches[key].down)rotateDown|=1u<<k;
    }
    RotateSelection(true,rotateDown,rotateTapped,GetTickCount64());
    if(Tapped(VK_DELETE,false)&&CanRemoveHeld()){DeleteSelection();return;}
    Item* target = held >= 0 ? Find(held) : nullptr;
    if (!target) return;
    if (Tapped(VK_UP)) placement.size=std::min(100.f,placement.size*1.1f);
    if (Tapped(VK_DOWN)) placement.size=std::max(.001f,placement.size/1.1f);
    if (Tapped(VK_PRIOR)) placement.lift=std::min(40.f,placement.lift+.25f);
    if (Tapped(VK_NEXT)) placement.lift=std::max(-40.f,placement.lift-.25f);
}
#include "hideout_tools.inl"
#include "hideout_library.inl"
// A material by name among what this scene shows.
void* SceneMaterial(const char* wanted) {
    Root type(TypeObject("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer"));
    bool hiddenToo = true; void* args[]{type.Get(), &hiddenToo};
    Root found(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, args));
    for (uintptr_t k = 0, n = found.Get() ? Get<uintptr_t>(found.Get(), 0x18) : 0; k < n; ++k) {
        void* r = Get<void*>(found.Get(), 0x20 + 8 * k); if (!r) continue;
        void* m = Invoke(Method(r, "get_sharedMaterial", 0), r);
        if (m && Text(Invoke(Unity("Object", "get_name", 0), m)) == wanted) return m;
    }
    return nullptr;
}
// Things stay on the ground when the ground changes: what fell outside comes in towards the middle.
void BringInside() {
    for (auto& i : items) {
        for (int step = 0; step < 80 && rules::Outline(groundShape, groundSize, i.right, i.up) < 3; ++step) { i.right *= .96f; i.up *= .96f; }
    }
}
#ifdef EPOCHPACT_TESTING
std::string floorTried;
#endif
void WarmBundle(const std::string& guid,bool preview) {
    const auto it = byGuid.find(guid);
    if (it == byGuid.end() || !CatalogFits()) return;
    const int file = catalog[it->second].file;
    auto& refs = preview ? previewBundles : openBundles;
    if(refs.count(file))return;
    void* manager = Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "get_InstanceOrNull", 0));
    void* list = Invoke(Method(manager, "get_Catalog", 0), manager);
    uint16_t id = static_cast<uint16_t>(Value<uint16_t>(Method(list, "get_AllBundleIdsBegin", 0), list) + file);
    if (!refs.count(file)) {
        Root ids(il2cpp::api().array_new(game::FindClass("LE.Addressables.dll", "LE.AssetBundles", "BundleId"), 1));
        *reinterpret_cast<uint16_t*>(static_cast<char*>(ids.Get()) + 0x20) = id;
        void* caller = il2cpp::api().string_new("epochpact"); int line = 1; void* ca[]{ids.Get(), caller, &line};
        refs[file] = std::make_unique<Root>(Invoke(game::FindMethod("LE.Addressables.dll", "LE.AssetBundles", "AssetBundlesManager", "CreateBundleIdArrayLoadRef", 3), nullptr, ca));
    }
}
void* OpenAndTakePrefab(const std::string& guid, bool* waiting, bool preview = false) {
    *waiting=false;const auto it=byGuid.find(guid);if(it==byGuid.end()||!CatalogFits())return nullptr;
    const int file=catalog[it->second].file;auto& refs=preview?previewBundles:openBundles;WarmBundle(guid,preview);
    if (!refs[file] || !refs[file]->Get()) return nullptr;
    const int status = Value<int>(Method(refs[file]->Get(), "get_Status", 0), refs[file]->Get());
    if (status == 1) { *waiting = true; return nullptr; }
    if (status != 2) return nullptr;
    void* manager=Invoke(game::FindMethod("LE.Addressables.dll","LE.AssetBundles","AssetBundlesManager","get_InstanceOrNull",0));
    void* list=Invoke(Method(manager,"get_Catalog",0),manager);
    uint16_t id=static_cast<uint16_t>(Value<uint16_t>(Method(list,"get_AllBundleIdsBegin",0),list)+file);
    void* ia[]{&id};
    void* entry = Invoke(Method(manager, "GetAssetBundleEntry", 1), manager, ia);
    void* bundle = entry ? Get<void*>(entry, game::FieldOffset("LE.Addressables.dll", "LE.AssetBundles", "BundleEntry", "assetBundle")) : nullptr;
    if (!bundle) return nullptr;
    Root goType(TypeObject("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"));
    auto& requests=preview?previewAssetRequests:placedAssetRequests;
    if(!requests.count(guid)) {
        if(requests.size()>=128){for(auto at=requests.begin();at!=requests.end();++at)if(Value<bool>(Unity("AsyncOperation","get_isDone",0),at->second->Get())){requests.erase(at);break;}}
        void* la[]{il2cpp::api().string_new(guid.c_str()),goType.Get()};
        requests[guid]=std::make_unique<Root>(Invoke(Signature("AssetBundle","LoadAssetAsync",{"System.String","System.Type"},"UnityEngine","UnityEngine.AssetBundleModule.dll"),bundle,la));
    }
    void* request=requests[guid]->Get();if(!Value<bool>(Unity("AsyncOperation","get_isDone",0),request)){*waiting=true;return nullptr;}
    return Invoke(game::FindMethod("UnityEngine.AssetBundleModule.dll","UnityEngine","AssetBundleRequest","get_asset",0),request);
}
#include "hideout_preview.inl"
// A floor of another map: its first material once its bundle is open.
void StepGround() {
    if (groundGuid.empty()) return;
    bool waiting = false;
    void* prefab = OpenAndTakePrefab(groundGuid, &waiting);
    if (waiting) return;
    groundGuid.clear();
    if (!prefab) { Log("hideout: the ground's floor could not be loaded"); return; }
    // the first part with a real material: some floors begin with one that has none
    Root rendererType(TypeObject("UnityEngine.CoreModule.dll", "UnityEngine", "Renderer"));
    bool all = true; void* args[]{rendererType.Get(), &all};
    Root renderers(Invoke(Signature("GameObject", "GetComponentsInChildren", {"System.Type", "System.Boolean"}), prefab, args));
    void* material = nullptr;
    for (uintptr_t k = 0, n = renderers.Get() ? Get<uintptr_t>(renderers.Get(), 0x18) : 0; k < n && !material; ++k) {
        void* r = Get<void*>(renderers.Get(), 0x20 + 8 * k); if (!r) continue;
        void* m = Invoke(Method(r, "get_sharedMaterial", 0), r);
        if (m && Text(Invoke(Unity("Object", "get_name", 0), m)) != "Default-Material") material = m;
    }
    if (material) monolith::HallSetGroundMaterial(material); else Log("hideout: the floor has no material");
#ifdef EPOCHPACT_TESTING
    floorTried = "none";
    if (material) {
        void* shader = Invoke(Method(material, "get_shader", 0), material);
        void* texture = Invoke(Method(material, "get_mainTexture", 0), material);
        floorTried = Text(Invoke(Unity("Object", "get_name", 0), material)) + " | " + (shader ? Text(Invoke(Unity("Object", "get_name", 0), shader)) : "?") + " | " + (texture ? Text(Invoke(Unity("Object", "get_name", 0), texture)) : "no texture");
    }
#endif
}
std::string ItemJson(const Item& i) {
    std::ostringstream out; out.setf(std::ios::fixed); out.precision(2);
    const auto it = i.guid.empty() ? byGuid.end() : byGuid.find(i.guid);
    out << "{\"id\":" << i.id << ",\"kind\":" << i.kind << ",\"door\":" << i.door << ",\"name\":" << Json(i.name) << ",\"right\":" << i.right << ",\"up\":" << i.up << ",\"lift\":" << i.lift
        << ",\"yaw\":" << i.yaw << ",\"size\":" << i.size << ",\"off\":" << Boolean(i.off) << ",\"standing\":" << Boolean(Alive(i)) << ",\"waiting\":" << Boolean(i.waiting || i.settle>0) << ",\"materialFixes\":" << i.materialFixes << ",\"failed\":" << Boolean(i.failed) << ",\"known\":" << Boolean(it != byGuid.end()) << ",\"locked\":"<<Boolean(i.locked)<<",\"group\":"<<i.group<<",\"selected\":"<<Boolean(selection.count(i.id)!=0)<<",\"walkable\":"<<Boolean(i.walkable)<<",\"pending\":"<<Boolean(Uncommitted(i.id))<<'}';
    return out.str();
}
std::string State() {
    LoadItems();
    std::ostringstream out;
    out << "{\"ok\":true,\"hall\":" << Boolean(monolith::HallIsUp()) << ",\"inside\":" << Boolean(PlayerInside()) << ",\"editing\":" << Boolean(editing) << ",\"held\":" << held << ",\"catalog\":" << catalog.size() << ",\"catalogFits\":" << Boolean(CatalogFits())
        << ",\"shape\":" << groundShape << ",\"size\":" << groundSize << ",\"texture\":" << groundTexture << ",\"grid\":"<<gridStep<<",\"angle\":"<<angleStep<<",\"selectFloors\":"<<Boolean(selectFloors)<<",\"selectionCount\":"<<selection.size()<<",\"preview\":{\"id\":"<<placement.id<<",\"right\":"<<placement.right<<",\"up\":"<<placement.up<<",\"lift\":"<<placement.lift<<",\"yaw\":"<<placement.yaw<<",\"size\":"<<placement.size<<"},\"textures\":[";
    for (int k = 0; k < kTextureCount; ++k) out << (k ? "," : "") << Json(kTextures[k].label);
    out << "],\"ready\":[";
    for (size_t k = 0; k < Designs().items.size(); ++k) out << (k ? "," : "") << Json(Designs().items[k]["label"].Str());
    out << "],\"saved\":[";const auto names=SavedNames();
    for(size_t k=0;k<names.size();++k)out<<(k?",":"")<<Json(names[k]);
    out<<"],\"savedDetails\":[";for(size_t k=0;k<names.size();++k){if(k)out<<',';out<<"{\"name\":"<<Json(names[k])<<",\"preview\":";try{out<<Json(SavedKey(names[k]));}catch(...){out<<"\"\"";}out<<'}';}
    out << "],\"canUndo\":" << Boolean(layoutHistory.CanUndo()) << ",\"canRedo\":" << Boolean(layoutHistory.CanRedo()) << ",\"undoCount\":" << layoutHistory.UndoCount()<<",\"redoCount\":"<<layoutHistory.RedoCount()<<",\"templatesFolder\":" << Json(std::string(reinterpret_cast<const char*>(TemplatesFolder().u8string().data()), TemplatesFolder().u8string().size())) << ",\"items\":[";
    bool first = true;
    for (const auto& i : items) { if (!first) out << ','; first = false; out << ItemJson(i); }
    return out.str() + "]}";
}
Vec3 PlayerPlace() {
    void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
    if (!actor || !game::IsAlive(actor)) throw std::runtime_error("Load an offline character first.");
    Root tr(Invoke(Unity("Component", "get_transform", 0), actor));
    return Value<Vec3>(Unity("Transform", "get_position", 0), tr.Get());
}
#ifdef EPOCHPACT_TESTING
#include "hideout_audit.inl"
#endif
std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s; }
}  // namespace

#ifdef EPOCHPACT_TESTING
bool windowOn = false;
GroundChoice windowTry{};
#endif
GroundChoice Ground() {
    LoadItems();
    const auto& t = kTextures[groundTexture];
#ifdef EPOCHPACT_TESTING
    if (windowOn) { auto w = windowTry; w.shape = groundShape; w.size = groundSize; w.light = t.light; return w; }
#endif
    return {groundShape, groundSize, t.period, t.natural, t.light, t.u0, t.u1, t.v0, t.v1};
}
void* GroundMaterial() {
    LoadItems();
    const auto& texture = kTextures[groundTexture];
    groundGuid.clear();
    if (texture.material) if (void* m = SceneMaterial(texture.material)) return m;
    if (texture.prefab) {
        ReadCatalog();
        const auto it = byName.find(texture.prefab);
        if (it != byName.end()) groundGuid = catalog[it->second].guid;
    }
    return SceneMaterial("MonolithRocks_Sand");
}
void* CliffMaterial() { return SceneMaterial("MonolithRocks_Sand"); }
std::vector<DoorSpot> Doors() {
    LoadItems();
    std::vector<DoorSpot> out;
    for (const auto& i : items) if (i.door != kNoDoor && !i.off) out.push_back({i.door, i.right, i.up, i.kind != kDoor,i.yaw,i.size,i.lift});
    return out;
}
bool GetStationBounds(int action,StationBounds& bounds) {
    for(const auto& i:items)if(i.door==action&&!i.off&&Alive(i)){
        const auto b=Measure(i.carrier->Get());if(!b.valid)return false;
        bounds={b.c.x,b.c.y,b.c.z,2*b.e.x,2*b.e.y,2*b.e.z};return true;
    }
    return false;
}
void Populate(void* holder) {
    LoadItems();
    Clear();
    holderRoot = std::make_unique<Root>(holder);
    sceneSources.clear();
    std::set<std::string> wanted;
    for (const auto& i : items) if (i.kind == kScene && !i.off) wanted.insert(i.name);
    if (!wanted.empty()) {
        Root transformType(TypeObject("UnityEngine.CoreModule.dll", "UnityEngine", "Transform"));
        bool hiddenToo = true; void* args[]{transformType.Get(), &hiddenToo};
        Root all(Invoke(Unity("Object", "FindObjectsOfType", 2), nullptr, args));
        for (uintptr_t k = 0, n = all.Get() ? Get<uintptr_t>(all.Get(), 0x18) : 0; k < n; ++k) {
            void* o = Get<void*>(all.Get(), 0x20 + 8 * k); if (!o) continue;
            void* go = Invoke(Unity("Component", "get_gameObject", 0), o);
            const std::string name = Text(Invoke(Unity("Object", "get_name", 0), go));
            if (!wanted.count(name)) continue;
            bool ourCopy=false;void* ancestor=o;
            for(int depth=0;depth<24 && ancestor;++depth) {
                const auto parentName=Text(Invoke(Unity("Object","get_name",0),Invoke(Unity("Component","get_gameObject",0),ancestor)));
                if(parentName.rfind("EP ",0)==0 || parentName.rfind("EpochPact ",0)==0){ourCopy=true;break;}
                ancestor=Invoke(Unity("Transform","get_parent",0),ancestor);
            }
            if(ourCopy)continue;
            // a character is an object of its name inside another of the same name: the outer one has the body
            void* parentTransform = Invoke(Unity("Transform", "get_parent", 0), o);
            if (parentTransform && Text(Invoke(Unity("Object", "get_name", 0), Invoke(Unity("Component", "get_gameObject", 0), parentTransform))) == name) go = Invoke(Unity("Component", "get_gameObject", 0), parentTransform);
            const auto existing=sceneSources.find(name);
            const bool active=Value<bool>(Unity("GameObject","get_activeInHierarchy",0),go);
            if(existing!=sceneSources.end() && (!active || Value<bool>(Unity("GameObject","get_activeInHierarchy",0),existing->second)))continue;
            // Several name-label objects have the NPC's exact name too. Only
            // a body with a skinned mesh can be the blacksmith source.
            bool allRenderers=true;Root rendererType(TypeObject("UnityEngine.CoreModule.dll","UnityEngine",name=="Annya the Blacksmith"?"SkinnedMeshRenderer":"MeshRenderer"));void* ra[]{rendererType.Get(),&allRenderers};
            Root renderers(Invoke(Signature("GameObject","GetComponentsInChildren",{"System.Type","System.Boolean"}),go,ra));
            if(!renderers.Get() || !Get<uintptr_t>(renderers.Get(),0x18))continue;
            sceneSources[name] = go;
        }
    }
    for (auto& i : items) {
        if (i.off || i.kind == kDoor) continue;
        i.failed = false;
        if (i.kind == kScene) {
            const auto it = sceneSources.find(i.name);
            if (it != sceneSources.end()) PutUp(i, it->second, true); else i.failed = true;
        } else StartLoad(i);
    }
}
void Clear() {
    EndPlacement(false);
    for (auto& i : items) { i.carrier.reset(); i.waiting = false; }
    for (auto& [file, ref] : openBundles) {
        if (!ref || !ref->Get()) continue;
        try { Invoke(Method(ref->Get(), "Dispose", 0), ref->Get()); } catch (...) {}
    }
    openBundles.clear();
    placedAssetRequests.clear();
    holderRoot.reset();
    decorationStone.reset();
    sceneSources.clear();
    navAt = doorsAt = 0;
}
bool Editing() { return editing; }
int Held() { return held; }
bool CanRemoveHeld(){if(selection.empty())return false;for(int id:selection){auto* i=Find(id);if(!i||i->locked||i->door==monolith::kHallOut)return false;}return true;}
bool CanUndo(){return layoutHistory.CanUndo();}
bool CanRedo(){return layoutHistory.CanRedo();}
unsigned long long HistoryRevision(){return historyRevision;}
int SelectionCount(){return static_cast<int>(selection.size());}
int SelectionAnchor(){return held>=0?held:selection.count(selectionPivot)?selectionPivot:selection.empty()?-1:*selection.begin();}
bool SelectionPending(){for(int id:selection)if(Uncommitted(id))return true;return false;}
bool SelectionLocked(){for(int id:selection)if(auto* i=Find(id);i&&i->locked)return true;return false;}
bool SelectionDuplicable(){if(selection.empty())return false;for(int id:selection){auto* i=Find(id);if(!i||i->door!=kNoDoor||Uncommitted(id))return false;}return true;}
float GridStep(){return gridStep;}
float AngleStep(){return angleStep;}
bool FloorsSelectable(){return selectFloors;}
std::string HeldName() {auto* i=Find(held);return i?i->name:"";}
float HeldRotation() {return placement.yaw;}
bool PlacementValid() {return PlacementReady();}
std::string PlacementIssue(){return placementIssue;}
void RequestPreview(const std::string& key,bool priority) {
    if(key.rfind("s:",0)==0){if(!previews.count(key))try{SavedKeyName(key);}catch(...){return;}}
    else if (key.size()<3 || (key.rfind("a:",0)!=0 && key.rfind("t:",0)!=0) || !std::all_of(key.begin()+2,key.end(),[](unsigned char c){return std::isdigit(c)!=0;})) return;
    if(priority){previewPriority=key;previewPriorityUntil=GetTickCount64()+500;}
    auto it=previews.find(key);
    if (it==previews.end()) {
        if (previews.size()>=rules::kPreviewCache) {
            auto oldest=previews.end();
            for (auto p=previews.begin();p!=previews.end();++p) if (p->first!=previewKey && (oldest==previews.end() || p->second.touched<oldest->second.touched)) oldest=p;
            if (oldest==previews.end())return;
            DropPreviewTexture(oldest->second);
            const auto gone=oldest->first;previewQueue.erase(std::remove(previewQueue.begin(),previewQueue.end(),gone),previewQueue.end());previews.erase(oldest);
        }
        it=previews.emplace(key,Preview{}).first;
        try {LoadPreviewCache(key,it->second);}catch(const std::exception& e){Log("preview cache read: %s",e.what());}
    }
    it->second.touched=++previewSerial;
    if(it->second.texture&&!game::IsAlive(it->second.texture->Get())){
        DropPreviewTexture(it->second);it->second.failed=false;it->second.requestedAt=0;
        try{LoadPreviewCache(key,it->second);}catch(const std::exception& e){Log("preview cache read: %s",e.what());}
    }
    if (it->second.texture || key==previewKey)return;
    if(it->second.failed && !priority)return;
    if(priority && it->second.failed){it->second.failed=false;it->second.requestedAt=0;}
    if(!it->second.requestedAt)it->second.requestedAt=GetTickCount64();
    WarmPreviewKey(key,8);
    if(priority){previewQueue.erase(std::remove(previewQueue.begin(),previewQueue.end(),key),previewQueue.end());previewQueue.push_front(key);it->second.status="Queued";}
    else if(std::find(previewQueue.begin(),previewQueue.end(),key)==previewQueue.end()){previewQueue.push_back(key);it->second.status="Queued";}
}
void* PreviewTexture(const std::string& key) {const auto it=previews.find(key);if(it==previews.end() || !it->second.texture)return nullptr;void* texture=it->second.texture->Get();return game::IsAlive(texture)?texture:nullptr;}
std::string PreviewStatus(const std::string& key) {const auto it=previews.find(key);return it==previews.end()?"Hover to preview":it->second.status;}
void ReleasePreviews() {
    DropPreviewScene();previewQueue.clear();previewPriority.clear();previewPriorityUntil=0;
    previewAssetRequests.clear();
    for(auto& [_,r]:previewBundles)if(r&&r->Get())try{Invoke(Method(r->Get(),"Dispose",0),r->Get());}catch(...){}previewBundles.clear();
    for (auto& [_,p]:previews) DropPreviewTexture(p);
    previews.clear();
    if (previewCameraObject)DisposePreviewObject(previewCameraObject->Get());previewCamera.reset();previewCameraObject.reset();
}
// Runtime teardown: release GC handles before IL2CPP shuts down. Unity owns
// the scene objects and textures; invoking Destroy/Dispose here is unsafe.
void ReleaseRoots() {
    selectionMarkers.reset();selectionMarkerMaterial.reset();selection.clear();uncommittedIds.clear();
    if(editorInput){if(void* input=editorInput->Get())*reinterpret_cast<bool*>(static_cast<char*>(input)+Offset("EpochInputManager","forceDisableInput"))=previousInputDisabled;editorInput.reset();}
    editing=false;held=uncommittedNew=-1;placement=Placement{};items.clear();holderRoot.reset();decorationStone.reset();
    placedAssetRequests.clear();previewAssetRequests.clear();openBundles.clear();previewBundles.clear();
    previews.clear();previewOwned.clear();previewRoot.reset();previewCamera.reset();previewCameraObject.reset();previewQueue.clear();
    monolith::ReleaseHallRoots();
}
void BlockEditorPointer(bool blocked,bool keyboard) {pointerBlocked=blocked;keyboardBlocked=keyboard;}
bool PlayerInside() {
    if (!monolith::HallIsUp() || !game::IsOfflinePlay()) return false;
    try {
        const Vec3 p = PlayerPlace(); float x=0,z=0; monolith::HallLocal(p.x,p.z,&x,&z);
        return rules::Outline(groundShape,groundSize,x,z) > -2;
    } catch (...) { return false; }
}
void Frame() {
    // The corner travel shortcut must also exist before the hideout/page has
    // been visited. Its UI host enforces the normal offline/character gate.
    ingame::view::HideoutHudFrame();
    if (!itemsLoaded) return;
    PreviewStep();
    const uint64_t now = GetTickCount64();
    if (holderRoot && !monolith::HallIsUp()) {
        // the scene changed: its copies are gone with it
        Clear();
        if (editing) SetEditing(false);
        return;
    }
    if (!holderRoot) return;
    StepLoads();
    StepGround();
    if (editing) {
        EditFrame();
    }
    DrawSelectionMarkers();
    if (doorsAt && now >= doorsAt) { doorsAt = 0; monolith::HallRebuildDoors(); }
    if (navAt && now >= navAt) { navAt = 0; monolith::HallRebuildNavMesh(); }
}
std::string Command(const std::vector<std::string>& args) {
    return Run([args]() -> std::string {
        if (args.empty()) throw std::runtime_error("use hideout read|search|add|at|move|here|turn|size|lift|remove|show|hold|edit|go|reset");
        LoadItems();
        const std::string& what = args[0];
        if(auto result=ToolsCommand(args))return *result;
        if(auto result=LibraryCommand(args))return *result;
        if(what=="undo"||what=="redo"){ApplyHistory(what=="redo");return State();}
#ifdef EPOCHPACT_TESTING
        if(what=="testartpreview"){previewArtReview=args.at(1)=="1";return State();}
        if(what=="testartclear"){
            if(!previewArtReview)throw std::runtime_error("Isolated art review required.");
            DropPreviewScene();previewQueue.clear();
            for(auto& [_,p]:previews)DropPreviewTexture(p);previews.clear();
            return "{\"ok\":true}";
        }
#endif
        if (what == "read") return State();
        if (what == "preview") {
            const auto& key=args.at(1);
            if(key=="bounds") {
                std::ostringstream out;out<<"{\"ok\":true,\"models\":[";bool first=true;
                for(const auto& [name,b]:previewMeasured){if(!first)out<<',';first=false;const auto index=static_cast<size_t>(std::stoul(name.substr(2)));out<<"{\"index\":"<<index<<",\"name\":"<<Json(catalog[index].name)<<",\"width\":"<<2*b.e.x<<",\"height\":"<<2*b.e.y<<",\"depth\":"<<2*b.e.z<<'}';}
                return out.str()+"]}";
            }
            RequestPreview(key);
#ifdef EPOCHPACT_TESTING
            if(previewArtReview)PreviewStep();
#endif
            return "{\"ok\":true,\"status\":"+Json(PreviewStatus(key))+",\"ready\":"+Boolean(PreviewTexture(key)!=nullptr)+",\"cached\":"+std::to_string(previews.size())+"}";
        }
        if (what == "browse") {
            const std::string category=args.at(1), sort=args.at(2);
            int page=std::max(0,std::stoi(args.at(3)));
            std::vector<size_t> matches;
            for (size_t k=0;k<catalog.size();++k) {
                const auto& e=catalog[k];
                if (!LibraryCategory(e,category)) continue;
                const auto& name=e.lower; bool match=true;
                for (size_t w=4;w<args.size();++w) if (name.find(Lower(args[w]))==std::string::npos) {match=false;break;}
                if (match) matches.push_back(k);
            }
            std::stable_sort(matches.begin(),matches.end(),[&](size_t a,size_t b){if(sort=="recent"){auto x=std::find(recentModels.begin(),recentModels.end(),catalog[a].guid),y=std::find(recentModels.begin(),recentModels.end(),catalog[b].guid);if(x!=y)return x<y;}const auto& x=catalog[a].lower;const auto& y=catalog[b].lower;return sort=="za"?x>y:x<y;});
            const int pages=std::max(1,(static_cast<int>(matches.size())+rules::kSearchPage-1)/rules::kSearchPage);
            page=std::min(page,pages-1); std::ostringstream out;
            out << "{\"ok\":true,\"fits\":" << Boolean(CatalogFits()) << ",\"total\":" << matches.size() << ",\"page\":" << page << ",\"pages\":" << pages << ",\"rows\":[";
            for (size_t p=static_cast<size_t>(page)*rules::kSearchPage,last=std::min(matches.size(),p+rules::kSearchPage),n=0;p<last;++p,++n) {
                const auto k=matches[p];const auto& e=catalog[k];out << (n?",":"") << "{\"i\":" << k << ",\"n\":" << Json(e.name) << ",\"c\":\"" << e.category << "\",\"favorite\":"<<Boolean(favoriteModels.count(e.guid)!=0)<<"}";
            }
            return out.str()+"]}";
        }
        if (what == "search") {
            // search <category|all> <words>: every word in the name
            const char category = args.size() > 1 && args[1] != "all" ? args[1][0] : 0;
            std::vector<std::string> words;
            for (size_t k = 2; k < args.size(); ++k) words.push_back(Lower(args[k]));
            std::ostringstream out; out << "{\"ok\":true,\"rows\":["; int total = 0, shown = 0;
            for (size_t k = 0; k < catalog.size(); ++k) {
                const auto& e = catalog[k];
                if (category && e.category != category) continue;
                const std::string lower = Lower(e.name);
                if (!std::all_of(words.begin(), words.end(), [&](const std::string& w) { return lower.find(w) != std::string::npos; })) continue;
                ++total;
                if (shown < 40) { out << (shown ? "," : "") << "{\"i\":" << k << ",\"n\":" << Json(e.name) << ",\"c\":\"" << e.category << "\",\"favorite\":"<<Boolean(favoriteModels.count(e.guid)!=0)<<"}"; ++shown; }
            }
            out << "],\"total\":" << total << ",\"fits\":" << Boolean(CatalogFits()) << '}';
            return out.str();
        }
        if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
        if(what=="drop"){if(!PlacementValid())throw std::runtime_error(placementIssue.empty()?"Move the preview to a valid position first.":placementIssue);EndPlacement(true);return State();}
        if(what=="unhold"){EndPlacement(false);selection.clear();++historyRevision;return State();}
        if (what == "cancel") {
            auto start=FilePath();start+=L".edit-start";
            std::ifstream input(start,std::ios::binary);const std::string snapshot((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());std::istringstream check(snapshot);
            std::vector<Item> restored;int shape=0,texture=0;float size=0;
            if(!editing||!ReadLayout(check,false,&restored,&shape,&size,&texture))throw std::runtime_error("No active edit session to cancel.");
            // Dispose pending originals/ghosts before touching their IDs. A
            // no-op cancel leaves the floor, all models and door instances alone.
            EndPlacement(false);
            if(LayoutSnapshot()==snapshot){SetEditing(false);selection.clear();++historyRevision;return State();}
            if(shape==groundShape&&size==groundSize&&texture==groundTexture){
                const auto before=LayoutSnapshot();
                try{RestoreEditItems(std::move(restored),true);SetEditing(false);}
                catch(...){
                    // Save() already restores its history on persistence failure.
                    // Earlier Unity/load failures must not leave a partial cancel.
                    if(editing){Clear();std::istringstream rollback(before);ReadLayout(rollback);monolith::HallRebuildAll();}
                    throw;
                }
            }else{
                std::istringstream restore(snapshot);ReadLayout(restore);SetEditing(false);Clear();monolith::HallRebuildAll();
            }
            return State();
        }
        if (what == "add") {
            // add <catalog index> [right up]: near the player if no place is given
            const size_t k = std::stoul(args.at(1));
            if (k >= catalog.size()) throw std::runtime_error("no such thing");
            if (!PlayerInside()) throw std::runtime_error("Go to the hideout first.");
            if (items.size() >= rules::kMaxItems) throw std::runtime_error("The hideout has reached its 400 object limit.");
            SetEditing(true);
            Item item;
            item.id = nextId++; item.kind = kPrefab; item.name = catalog[k].name; item.guid = catalog[k].guid; item.fit = true; item.centered = true;
            RememberModel(item.guid);
            if (args.size() > 3) { item.right = std::stof(args[2]); item.up = std::stof(args[3]); }
            else { const Vec3 me = PlayerPlace(); monolith::HallLocal(me.x, me.z, &item.right, &item.up); item.up += 4; }
            if (!rules::Inside(groundShape,groundSize,item.right,item.up)) {item.right=0;item.up=0;}
            const int id = item.id;
            items.push_back(std::move(item));
            StartLoad(items.back());
            if (items.back().failed) { items.pop_back(); throw std::runtime_error("This game build does not have it."); }
            SetEditing(true);BeginPlacement(id,true);
            return "{\"ok\":true,\"id\":" + std::to_string(id) + "}";
        }
        if (what == "template") {
            // template ready <i> | save <name> | load <name> | remove <name> | undo. Before one replaces the layout, the layout is kept for undo.
            const std::string step = args.at(1);
            std::string name;
            for (size_t k = 2; k < args.size(); ++k) name += (k > 2 ? " " : "") + args[k];
            const auto rebuild = [] { held = -1; Clear(); monolith::HallRebuildAll(); Save(); };
            if (step == "save" || step=="overwrite") {
                const auto path = TemplatePath(name);
                if(step=="save"&&std::filesystem::exists(path))throw std::runtime_error("A template with this name already exists. Confirm overwrite first.");
                std::error_code why; std::filesystem::create_directories(path.parent_path(), why);
                if (!WriteFile(path)) throw std::runtime_error("The template could not be saved.");
            }else if(step=="rename"){
                const auto split=name.find('|');if(split==std::string::npos)throw std::runtime_error("Use old name|new name.");
                const auto from=TemplatePath(name.substr(0,split)),to=TemplatePath(name.substr(split+1));
                if(!std::filesystem::is_regular_file(from)||std::filesystem::is_symlink(from))throw std::runtime_error("Template is missing.");
                if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not rename template. Choose an unused name.");
            } else if (step == "remove") {
                std::error_code why; std::filesystem::remove(TemplatePath(name), why);
                if (why) throw std::runtime_error("The template could not be removed.");
            } else if (step == "load") {
                const auto path = TemplatePath(name);
                if (!ReadFile(path)) throw std::runtime_error("That file is not a hideout template.");
                BringInside();
                rebuild();
            } else if (step == "undo") {
                ApplyHistory(false);
            } else if (step == "ready") {
                const int k = std::stoi(args.at(2));
                if (k < 0 || k >= static_cast<int>(Designs().items.size())) throw std::runtime_error("No such template.");
                Clear(); ApplyDesign(k);
                rebuild();
            } else {
                throw std::runtime_error("No such step.");
            }
            return State();
        }
        if (what == "reset") {
            held = -1;
            Clear();
            items.clear(); Defaults();
            monolith::HallRebuildAll();
            Save();
            return State();
        }
        if (what == "edit") {
            if (args.at(1) == "1" && !monolith::HallIsUp()) throw std::runtime_error("Go to the hideout first.");
            SetEditing(args.at(1) == "1");
            return State();
        }
        if (what == "go") { monolith::HallGoIn(); return State(); }
        if (what == "ground") {
            // ground <shape> <size> <texture>
            const int shape = std::stoi(args.at(1)), texture = std::stoi(args.at(3));
            const float size = std::stof(args.at(2));
            if (shape < 0 || shape >= rules::kShapes || texture < 0 || texture >= kTextureCount || !std::isfinite(size) || size < rules::kMinSize || size > rules::kMaxSize) throw std::runtime_error("no such ground");
            EndPlacement(false);
            if((shape!=groundShape||size!=groundSize)&&monolith::HallIsUp())for(const auto& item:items)if(!item.off){
                if(item.waiting||item.settle)throw std::runtime_error("Wait for models to finish loading before changing the ground.");
                const auto pose=ItemPose(item);if(!FootprintInside(Measure(ItemVisual(item)),pose,pose,shape,size))throw std::runtime_error("This ground is too small for "+item.name+". Move it inward or choose a larger ground.");
            }
            const bool sameMesh = shape == groundShape && size == groundSize && kTextures[texture].period == kTextures[groundTexture].period && kTextures[texture].natural == kTextures[groundTexture].natural;
            groundShape = shape; groundSize = size; groundTexture = texture;
            if (sameMesh && monolith::HallIsUp()) {
                // only another texture: everything else stands where it is
                void* material = GroundMaterial();
                if (material && !groundGuid.size()) monolith::HallSetGroundMaterial(material);
                monolith::HallSetLight(kTextures[texture].light);
            } else {
                BringInside();
                if (monolith::HallIsUp()) monolith::HallRebuildAll();
            }
            Save();
            return State();
        }
#ifdef EPOCHPACT_TESTING
        if(what=="testeditkey"){
            if(!editing)throw std::runtime_error("Start the disposable editor first.");
            const bool oldPointer=pointerBlocked,oldKeyboard=keyboardBlocked;
            struct Restore {bool p,k;~Restore(){testEditorInput=false;testEditorSamples={};pointerBlocked=p;keyboardBlocked=k;editorKeys={};editorKeyLatches={};}} restore{oldPointer,oldKeyboard};
            testEditorInput=true;testEditorSamples={};pointerBlocked=true;keyboardBlocked=false;editorKeys={};editorKeyLatches={};
            EditFrame();EditFrame();
            const auto key=args.at(1);
            if(key=="delete")testEditorSamples[VK_DELETE]=static_cast<SHORT>(0x8001);
            else if(key=="undo"||key=="redo"||key=="duplicate"||key=="group"||key=="ungroup"){
                testEditorSamples[VK_CONTROL]=static_cast<SHORT>(0x8000);
                const int code=key=="undo"?'Z':key=="redo"?'Y':key=="duplicate"?'D':'G';testEditorSamples[code]=static_cast<SHORT>(0x8001);
                if(key=="ungroup")testEditorSamples[VK_SHIFT]=static_cast<SHORT>(0x8000);
            }else throw std::runtime_error("Unknown editor key.");
            if(args.size()>2&&args[2]=="ui")keyboardBlocked=true;
            EditFrame();return State();
        }
        if(what=="testrotation"){
            float degrees=0;
            if(args.size()>1&&args[1]!="read"){
                const auto down=std::stoul(args.at(1)),tapped=std::stoul(args.at(2));
                if(down>15||tapped>15)throw std::runtime_error("Invalid rotation key mask.");
                degrees=RotateSelection(PlayerInside()&&!ingame::view::WindowOpen(),static_cast<unsigned>(down),static_cast<unsigned>(tapped),std::stoull(args.at(3)));
            }
            void* input=game::StaticObject(game::FindStaticField("LE.dll","","EpochInputManager","instance"));
            return "{\"ok\":true,\"degrees\":"+std::to_string(degrees)+",\"yaw\":"+std::to_string(placement.yaw)+",\"editing\":"+Boolean(editing)+",\"inputDisabled\":"+Boolean(input&&Get<bool>(input,Offset("EpochInputManager","forceDisableInput")))+"}";
        }
        if(what=="hudaudit")return ingame::view::HideoutHudAudit();
        if(what=="testpreviewdelay"){
            const int delay=std::stoi(args.at(1));if(delay<0||delay>2000)throw std::runtime_error("Delay out of range.");
            previewTestDelay=delay;return "{\"ok\":true}";
        }
        if(what=="testpreviewqueue"){RequestPreview(args.at(1),args.size()>2&&args[2]=="priority");return "{\"ok\":true}";}
        if(what=="previewqueue"){
            std::ostringstream out;out<<"{\"ok\":true,\"active\":"<<Json(previewKey)<<",\"priority\":"<<Json(previewPriority)<<",\"preemptions\":"<<previewPreemptions<<",\"queued\":"<<previewQueue.size()<<",\"textures\":[";bool first=true;
            for(const auto& [key,p]:previews){if(!first)out<<',';first=false;out<<"{\"key\":"<<Json(key)<<",\"ready\":"<<Boolean(PreviewTexture(key)!=nullptr)<<",\"failed\":"<<Boolean(p.failed)<<'}';}return out.str()+"]}";
        }
        if(what=="testpreviewdestroy"||what=="testpreviewevict"){
            if(!PlayerInside())throw std::runtime_error("Disposable hideout required.");
            auto at=previews.find(args.at(1));if(at==previews.end()||!at->second.texture)throw std::runtime_error("Load the preview first.");
            DisposePreviewObject(at->second.texture->Get());
            if(what=="testpreviewevict"){DropPreviewTexture(at->second);previews.erase(at);}
            return "{\"ok\":true}";
        }
        if(what=="testmenus")return ingame::view::HideoutHudMenuTest(args.at(1));
        if(what=="auditgrid") return AuditGrid(std::stoi(args.at(1)),std::stoi(args.at(2)));
        if(what=="testsafetybox"){
            auto* item=Find(std::stoi(args.at(1)));if(!item||!Uncommitted(item->id)||!Alive(*item))throw std::runtime_error("Use an uncommitted disposable model.");
            Root type(TypeObject("UnityEngine.PhysicsModule.dll","UnityEngine","BoxCollider"));void* args[]{type.Get()};void* box=Invoke(Signature("GameObject","AddComponent",{"System.Type"}),item->carrier->Get(),args);
            Vec3 size{8,8,8},center{0,4,0};void* a[]{&size},*b[]{&center};Invoke(game::FindMethod("UnityEngine.PhysicsModule.dll","UnityEngine","BoxCollider","set_size",1),box,a);Invoke(game::FindMethod("UnityEngine.PhysicsModule.dll","UnityEngine","BoxCollider","set_center",1),box,b);
            return State();
        }
        if(what=="testpick"){
            auto* i=Find(std::stoi(args.at(1)));if(!i)throw std::runtime_error("No target object.");const auto b=Measure(ItemVisual(*i));
            if(!b.valid)throw std::runtime_error("Wait for the target model.");auto* picked=PickAtRay({b.c.x,b.c.y+b.e.y+100,b.c.z},{0,-1,0});
            return "{\"ok\":true,\"picked\":"+std::to_string(picked?picked->id:-1)+"}";
        }
        if(what=="testpoint") {if(!editing||held<0)throw std::runtime_error("Select a disposable test object first.");const float x=std::stof(args.at(1)),z=std::stof(args.at(2));UpdatePlacement(x,z,rules::Inside(groundShape,groundSize,x,z));return State();}
        if(what=="placementread") {
            std::ostringstream out;out<<"{\"ok\":true,\"id\":"<<placement.id<<",\"ghost\":"<<Boolean(placement.ghost!=nullptr)<<",\"valid\":"<<Boolean(PlacementReady())<<",\"right\":"<<placement.right<<",\"up\":"<<placement.up<<",\"yaw\":"<<placement.yaw<<",\"shader\":"<<Json(placement.shader)<<",\"members\":[";
            bool first=true;for(const auto& p:placement.members){if(!first)out<<',';first=false;out<<"{\"id\":"<<p.id<<",\"right\":"<<p.right<<",\"up\":"<<p.up<<",\"yaw\":"<<p.yaw<<",\"size\":"<<p.size<<",\"ghost\":"<<Boolean(p.ghost!=nullptr)<<'}';}return out.str()+"]}";
        }
        if(what=="audit") return AuditHideout();
        if (what == "floortry") {
            // floortry <name>: a floor of any map on the ground; floortry with nothing: how it went
            if (args.size() > 1) {
                std::string name;
                for (size_t k = 1; k < args.size(); ++k) name += (k > 1 ? " " : "") + args[k];
                ReadCatalog();
                const auto it = byName.find(name);
                if (it == byName.end()) throw std::runtime_error("no such floor");
                groundGuid = catalog[it->second].guid;
                floorTried.clear();
            }
            return "{\"ok\":true,\"waiting\":" + std::string(groundGuid.empty() ? "false" : "true") + ",\"floor\":" + Json(floorTried) + "}";
        }
        if (what == "windowtry") {
            // windowtry <period> <natural> <u0> <u1> <v0> <v1>; windowtry off
            windowOn = args.at(1) != "off";
            if (windowOn) { windowTry.period = std::stof(args.at(1)); windowTry.natural = args.at(2) == "1"; windowTry.u0 = std::stof(args.at(3)); windowTry.u1 = std::stof(args.at(4)); windowTry.v0 = std::stof(args.at(5)); windowTry.v1 = std::stof(args.at(6)); }
            monolith::HallRebuildAll();
            return "{\"ok\":true}";
        }
        if (what == "lighttry") { monolith::HallSetLight(std::stof(args.at(1))); return "{\"ok\":true}"; }
        if (what == "testplace") {
            // what a mouse drop does, without a mouse
            Item* i = Find(std::stoi(args.at(1)));
            if (!i) throw std::runtime_error("no such thing");
            i->right = std::stof(args.at(2)); i->up = std::stof(args.at(3)); Changed(*i, true);
            return State();
        }
#endif
        Item* i = args.size() > 1 ? Find(std::stoi(args[1])) : nullptr;
        if (!i) throw std::runtime_error("no such thing");
        if(i->locked&&what!="show")throw std::runtime_error("Unlock this object before editing it.");
        if(i->id==held&&(what=="at"||what=="move"||what=="here")){
            float x=placement.right,z=placement.up;
            if(what=="here"){const auto me=PlayerPlace();monolith::HallLocal(me.x,me.z,&x,&z);z+=3;}
            else{x=std::stof(args.at(2))+(what=="move"?x:0);z=std::stof(args.at(3))+(what=="move"?z:0);}
            if(!std::isfinite(x)||!std::isfinite(z))throw std::runtime_error("Use finite coordinates.");UpdatePlacement(x,z,true);return State();
        }
        if(i->id==held && (what=="turn"||what=="size"||what=="lift")) {
            const float value=std::stof(args.at(2));if(!std::isfinite(value))throw std::runtime_error("Use a finite transform value.");
            if(what=="turn")placement.yaw=std::fmod(placement.yaw+value,360.f);
            if(what=="size")placement.size=std::clamp(placement.size*value,.001f,100.f);
            if(what=="lift")placement.lift=std::clamp(placement.lift+value,-40.f,40.f);
            UpdatePlacement(placement.right,placement.up,true);return State();
        }
        if (what == "at" || what == "move") {
            const float x=std::stof(args.at(2))+(what=="move"?i->right:0),z=std::stof(args.at(3))+(what=="move"?i->up:0);
            if (!rules::Inside(groundShape,groundSize,x,z)) throw std::runtime_error("Place the object inside the hideout edge.");
            auto p=ItemPose(*i);p.x=x;p.z=z;ApplyPoses({{i->id,p}});
        }
        else if (what == "here") { const Vec3 me = PlayerPlace();auto p=ItemPose(*i);monolith::HallLocal(me.x,me.z,&p.x,&p.z);p.z+=3;ApplyPoses({{i->id,p}}); }
        else if (what == "turn" || what == "size" || what == "lift") {
            const float value=std::stof(args.at(2));if(!std::isfinite(value))throw std::runtime_error("Use a finite transform value.");
            const float angle=what=="turn"?std::fmod(i->yaw+value,360.f):i->yaw;
            const float scale=what=="size"?std::clamp(i->size*value,.001f,100.f):i->size;
            const float height=what=="lift"?i->lift+value:i->lift;
            if(!rules::FinitePlacement(i->right,i->up,height,angle,scale))throw std::runtime_error("Transform is outside the supported limits.");
            auto p=ItemPose(*i);p.yaw=angle;p.size=scale;p.lift=height;ApplyPoses({{i->id,p}});
        }
        else if (what == "hold") { SetEditing(true);BeginPlacement(i->id); }
        else if (what == "show") {
            // a door the player does not want, and back again
            if (i->door == kNoDoor) throw std::runtime_error("only a door can be shown or hidden");
            if (i->door == monolith::kHallOut) throw std::runtime_error("The way out stays.");
            const bool show = args.at(2) == "1";
            i->off = !show;
            if (!show && Alive(*i)) { void* a[]{i->carrier->Get()}; Invoke(Unity("Object", "Destroy", 1), nullptr, a); i->carrier.reset(); }
            if (show && !Alive(*i) && i->kind == kPrefab) StartLoad(*i);
            if (show && !Alive(*i) && i->kind == kScene) { const auto it = sceneSources.find(i->name); if (it != sceneSources.end()) PutUp(*i, it->second, true); }
            Save();
            doorsAt = GetTickCount64() + 50;
        }
        else if (what == "remove") {
            if (i->door == monolith::kHallOut) throw std::runtime_error("The way out stays.");
            RemoveItem(i->id);
        }
        else throw std::runtime_error("unknown hideout action");
        return State();
    });
}
}  // namespace ep::hideout
