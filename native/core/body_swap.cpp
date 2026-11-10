#include "body_swap.hpp"
#include "managed.hpp"
#include "common.hpp"
#include "hook.hpp"
#include "ingame_window.hpp"
#include "ingame_ui.hpp"
#include <filesystem>
#include <tuple>
#include <fstream>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <regex>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The Rogue with Zerrick's body. Zerrick's model comes from the game's own asset loader (ActorData "Zerrick Ally
// Monolith", VisualsSoftRef) and stands where the Rogue model stands. The Rogue model keeps playing the game's
// animations, hidden; after the animation of each frame (CanvasUpdateRegistry.PerformUpdate runs between LateUpdate
// and rendering) Zerrick's bones take the Rogue's rotation change from rest, bone for bone. Both models stand in the
// same A pose at rest (measured in the bundles: arm and leg directions within 6 degrees, heads at 1.86 and 1.90), so
// the change carries over. The rest of each bone comes from its mesh's bind pose. Weapons stay on the Rogue's weapon
// joints, moved to Zerrick's hands; armour pieces are made for the Rogue's body and stay hidden.
namespace ep::bodyswap {
namespace {
using namespace managed;
struct Q { float x, y, z, w; };
struct V { float x, y, z; };
Q Mul(const Q& a, const Q& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Q Inv(const Q& a) { return {-a.x, -a.y, -a.z, a.w}; }
Q Norm(Q q) { const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); if (n < 1e-6f) return {0, 0, 0, 1}; return {q.x / n, q.y / n, q.z / n, q.w / n}; }
V Rot(const Q& q, const V& v) { const Q r = Mul(Mul(q, Q{v.x, v.y, v.z, 0}), Inv(q)); return {r.x, r.y, r.z}; }
V Add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V Sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V Scale(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
// rows r, columns c of a rotation (orthonormal) matrix
Q FromRows(const float m[3][3]) {
    const float t = m[0][0] + m[1][1] + m[2][2];
    Q q;
    if (t > 0) { const float s = std::sqrt(t + 1.f) * 2; q = {(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25f * s}; }
    else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) { const float s = std::sqrt(1.f + m[0][0] - m[1][1] - m[2][2]) * 2; q = {0.25f * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s}; }
    else if (m[1][1] > m[2][2]) { const float s = std::sqrt(1.f + m[1][1] - m[0][0] - m[2][2]) * 2; q = {(m[0][1] + m[1][0]) / s, 0.25f * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s}; }
    else { const float s = std::sqrt(1.f + m[2][2] - m[0][0] - m[1][1]) * 2; q = {(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25f * s, (m[1][0] - m[0][1]) / s}; }
    return Norm(q);
}
// A bind pose (Matrix4x4, column-major m00 m10 m20 m30 m01 ...) is the bone's world-to-local times the mesh's
// local-to-world at bind time; its inverse is the bone at rest in the mesh's space.
void RestFromBindPose(const float* b, Q* rotation, V* position) {
    float a[3][3];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) a[r][c] = b[c * 4 + r];
    float s2 = a[0][0] * a[0][0] + a[1][0] * a[1][0] + a[2][0] * a[2][0];
    if (s2 < 1e-12f) s2 = 1;
    float inv[3][3];  // (sR)^-1 = (sR)^T / s^2
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) inv[r][c] = a[c][r] / s2;
    const float t[3]{b[12], b[13], b[14]};
    *position = {-(inv[0][0] * t[0] + inv[0][1] * t[1] + inv[0][2] * t[2]), -(inv[1][0] * t[0] + inv[1][1] * t[1] + inv[1][2] * t[2]),
                 -(inv[2][0] * t[0] + inv[2][1] * t[1] + inv[2][2] * t[2])};
    const float s = std::sqrt(s2);
    float rot[3][3];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) rot[r][c] = inv[r][c] * s;
    *rotation = FromRows(rot);
}

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
    const auto* cls = game::FindClass(image, ns, name);
    if (!cls) throw std::runtime_error(std::string("no class ") + name);
    return il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls));
}
std::string NameOf(void* object) { return object ? Text(Invoke(Unity("Object", "get_name", 0), object)) : std::string(); }
void* TransformOf(void* component) { return Invoke(Unity("Component", "get_transform", 0), component); }
void* TransformOfObject(void* gameObject) { return Invoke(Unity("GameObject", "get_transform", 0), gameObject); }
void* Parent(void* transform) { return Invoke(Unity("Transform", "get_parent", 0), transform); }
Q Rotation(void* t) { return Value<Q>(Unity("Transform", "get_rotation", 0), t); }
V Position(void* t) { return Value<V>(Unity("Transform", "get_position", 0), t); }
void SetRotation(void* t, Q q) { void* a[]{&q}; Invoke(Unity("Transform", "set_rotation", 1), t, a); }
void SetPosition(void* t, V v) { void* a[]{&v}; Invoke(Unity("Transform", "set_position", 1), t, a); }
std::vector<void*> ArrayItems(void* array) {
    std::vector<void*> out;
    if (!array) return out;
    const auto n = Get<uintptr_t>(array, 0x18);
    for (uintptr_t i = 0; i < n && i < 100000; ++i) out.push_back(Get<void*>(array, 0x20 + 8 * i));
    return out;
}
std::vector<void*> ComponentsUnder(void* gameObject, const char* image, const char* kind) {
    bool all = true; Root type(TypeObject(image, "UnityEngine", kind)); void* args[]{type.Get(), &all};
    Root list(Invoke(Signature("GameObject", "GetComponentsInChildren", {"System.Type", "System.Boolean"}), gameObject, args));
    return ArrayItems(list.Get());
}
void* GameObjectOf(void* component) { return Invoke(Unity("Component", "get_gameObject", 0), component); }
void Walk(void* transform, std::map<std::string, void*>& out, int depth = 0) {
    if (!transform || depth > 64) return;
    out.emplace(NameOf(transform), transform);
    const int n = Value<int>(Unity("Transform", "get_childCount", 0), transform);
    for (int i = 0; i < n; ++i) { int k = i; void* a[]{&k}; Walk(Invoke(Unity("Transform", "GetChild", 1), transform, a), out, depth + 1); }
}
bool Under(void* transform, const char* fragment) {
    for (void* t = transform; t; t = Parent(t)) if (NameOf(t).find(fragment) != std::string::npos) return true;
    return false;
}

// Rogue bone -> Zerrick bone (the rest pose, its children and the twist bones follow their parents)
const std::vector<std::pair<std::string, std::string>>& Map() {
    static const std::vector<std::pair<std::string, std::string>> map = [] {
        std::vector<std::pair<std::string, std::string>> m{{"Pelvis_JNT", "Pelvis_JNT"}, {"Spine_0_JNT", "Spine_JNT"}, {"Spine_1_JNT", "Spine_1_JNT"},
            {"Spine_2_JNT", "Spine_2_JNT"}, {"Spine_3_JNT", "Spine_3_JNT"}, {"Spine_4_JNT", "Spine_4_JNT"}, {"Chest_JNT", "Chest_JNT"},
            {"Neck_JNT", "Neck_JNT"}, {"Head_JNT", "Head_JNT"}};
        for (const char* s : {"L_", "R_"}) {
            const std::string p = s;
            for (const auto& [r, z] : std::vector<std::pair<std::string, std::string>>{{"Leg_JNT", "Thigh_JNT"}, {"Foreleg_JNT", "Knee_JNT"}, {"Ankle_JNT", "Ankle_JNT"},
                     {"Foot_JNT", "Foot_JNT"}, {"Clavicle_JNT", "Clavicle_JNT"}, {"Arm_JNT", "Arm_JNT"}, {"Forearm_JNT", "Forearm_JNT"}, {"Hand_JNT", "Hand_JNT"}})
                m.emplace_back(p + r, p + z);
            for (const char* f : {"Index", "Mid", "Pinky", "Ring"})
                for (int k = 0; k < 4; ++k) m.emplace_back(p + f + "_" + std::to_string(k) + "_JNT", p + f + "_" + std::to_string(k + 1) + "_JNT");
            for (int k = 0; k < 3; ++k) m.emplace_back(p + "Thumb_" + std::to_string(k) + "_JNT", p + "Thumb_" + std::to_string(k + 1) + "_JNT");
            // the twist bones along the limbs carry the turn of the hand and foot (Zerrick's rig spells it "Forerm")
            for (int k = 0; k < 5; ++k) {
                const std::string a = std::to_string(k), b = std::to_string(k + 1);
                m.emplace_back(p + "Leg_Twist_" + a + "_JNT", p + "Leg_Twist_" + b + "_JNT");
                m.emplace_back(p + "Foreleg_" + a + "_Twist_JNT", p + "Foreleg_Twist_" + b + "_JNT");
                m.emplace_back(p + "Arm_Twist_" + a + "_JNT", p + "Arm_Twist_" + b + "_JNT");
                m.emplace_back(p + "Forearm_Twist_" + a + "_JNT", p + "Forerm_Twist_" + b + "_JNT");
            }
        }
        return m;
    }();
    return map;
}

struct Rest { Q rotation{0, 0, 0, 1}; V position{0, 0, 0}; bool known = false; };
// Each skinned mesh's bones at rest in the model's space.
std::map<std::string, Rest> RestOf(void* renderer, void* model) {
    std::map<std::string, Rest> out;
    void* mesh = Invoke(Method(renderer, "get_sharedMesh", 0), renderer);
    if (!mesh) return out;
    Root poses(Invoke(Method(mesh, "get_bindposes", 0), mesh));
    Root bones(Invoke(Method(renderer, "get_bones", 0), renderer));
    const auto n = std::min(Get<uintptr_t>(poses.Get(), 0x18), Get<uintptr_t>(bones.Get(), 0x18));
    void* rt = TransformOf(renderer);
    const Q toModel = Mul(Inv(Rotation(model)), Rotation(rt));
    const V meshAt = Rot(Inv(Rotation(model)), Sub(Position(rt), Position(model)));
    for (uintptr_t i = 0; i < n; ++i) {
        void* bone = Get<void*>(bones.Get(), 0x20 + 8 * i);
        if (!bone) continue;
        const float* m = reinterpret_cast<const float*>(static_cast<char*>(poses.Get()) + 0x20 + 64 * i);
        Rest r; RestFromBindPose(m, &r.rotation, &r.position);
        r.rotation = Norm(Mul(toModel, r.rotation));
        r.position = Add(meshAt, Rot(toModel, r.position));
        r.known = true;
        out[NameOf(bone)] = r;
    }
    return out;
}

float Angle(const Q& a, const Q& b) {
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.f * std::acos(std::min(1.f, d)) * 57.2958f;
}
// A Rogue bone at rest. The limb bones (Leg, Foreleg, Arm, Forearm) and Spine_4 carry no skin, so their meshes give
// no bind pose; their first twist bone (or, for Spine_4, the Chest) has one and sits on them with no turn at rest
// (game 1.5.4 bundles: within 0.05 degrees).
bool RogueRest(const std::map<std::string, Rest>& rest, void* bone, Q* out) {
    if (auto it = rest.find(NameOf(bone)); it != rest.end()) { *out = it->second.rotation; return true; }
    const int n = Value<int>(Unity("Transform", "get_childCount", 0), bone);
    const Rest* pick = nullptr;
    for (int i = 0; i < n; ++i) {
        int k = i; void* a[]{&k};
        const std::string child = NameOf(Invoke(Unity("Transform", "GetChild", 1), bone, a));
        auto it = rest.find(child);
        if (it == rest.end()) continue;
        if (child.find("Twist") != std::string::npos) { pick = &it->second; break; }
        if (!pick) pick = &it->second;
    }
    if (!pick) return false;
    *out = pick->rotation;
    return true;
}

struct Bone { std::unique_ptr<Root> rogue, zerrick; Q rogueRest, zerrickRest; int depth = 0; };
struct Puppet {
    std::unique_ptr<Root> rogueBody;   // the Rogue's body mesh (Rogue_Body_GEO), the sign of a Rogue model
    std::unique_ptr<Root> rogueModel;  // MODEL_Rogue_*
    std::unique_ptr<Root> rogueRoot;   // the Rogue's visuals (the model's parent)
    std::unique_ptr<Root> holder;      // EpochPact's object holding Zerrick
    std::unique_ptr<Root> zerrickModel;
    std::unique_ptr<Root> zerrickHips, roguePelvis;
    V zerrickHipsRest{}, roguePelvisRest{};
    float scale = 1;
    std::vector<Bone> bones;
    std::vector<std::unique_ptr<Root>> hidden;  // the Rogue's renderers EpochPact turned off
    std::unique_ptr<Root> rogueWeapon[2], rogueHand[2], zerrickHand[2];
    V weaponLocal[2]{};  // the weapon joint's place on the hand (the animation may leave it alone)
    std::vector<std::string> zerrickRenderers;
    int hiddenCount = 0;
    float restCheck = 0;  // the largest gap between Zerrick's fresh pose and his bind poses, in degrees
};

std::atomic<bool> on{false}, currentWanted{false};
bool hooked = false;
// The choices, one per character (CharacterData.Id); a Rogue with no choice of its own takes the default (off).
// The creation screen always shows the game's own Rogue.
struct Choices { bool fallback = false; std::map<std::string, bool> characters; };
Choices choices;
using PerformFn = void (*)(void*, const il2cpp::Method*);
PerformFn originalPerform = nullptr;
std::unique_ptr<Root> loadRef, prefab;
std::vector<std::unique_ptr<Puppet>> puppets;
uint64_t frames = 0, faults = 0, scans = 0;
double lastMs = 0;
std::string lastError, phase = "off";
int loadStatus = -1;

std::filesystem::path ChoiceFile() {
    const auto root = ingame::view::PreferencesFolder();
    if (root.empty()) return {};
#ifdef EPOCHPACT_TESTING
    return root / L"zerrick-body-test.txt";  // test games never touch the player's own
#else
    return root / L"zerrick-body.txt";
#endif
}
bool ValidId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (const char ch : id) if (!isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') return false;
    return true;
}
void LoadChoices() {
    choices = {};
    const auto path = ChoiceFile();
    if (path.empty()) return;
    std::ifstream in(path, std::ios::binary);
    std::string word, value, id;
    if (!(in >> word)) return;
    // the first test builds stored one word for every Rogue: not carried over (nothing in the window could undo it)
    if (word != "epochpact-zerrick" || !(in >> value)) return;
    while (in >> word) {
        if (word == "default" && in >> value) choices.fallback = value == "on";
        else if (word == "new" && in >> value) {}  // the creation screen's old switch: no longer used
        else if (word == "char" && in >> id >> value) { if (ValidId(id)) choices.characters[id] = value == "on"; }
        else break;
    }
}
void SaveChoices() {
    try {
        const auto path = ChoiceFile();
        if (path.empty()) return;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "epochpact-zerrick 2\n" << "default " << (choices.fallback ? "on" : "off") << '\n';
        for (const auto& [id, value] : choices.characters) out << "char " << id << ' ' << (value ? "on" : "off") << '\n';
    } catch (...) {}
}
bool ChoiceOf(const std::string& id) {
    const auto it = choices.characters.find(id);
    return it != choices.characters.end() ? it->second : choices.fallback;
}
bool AnyOn() {
    if (choices.fallback) return true;
    for (const auto& [id, value] : choices.characters) if (value) return true;
    return false;
}
// The character screen (CharacterSelect), while it is up.
void* SelectScreen() {
    // the screen's object outlives it (CharacterSelect.instance is still set in play): the client state decides
    try { if (SessionState().state != 3) return nullptr; } catch (const std::exception&) {}
    void* select = game::StaticObject(game::FindStaticField("LE.dll", "", "CharacterSelect", "instance"));
    return select && game::IsAlive(select) ? select : nullptr;
}
void* PlayerData() {
    void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
    if (!actor || !game::IsAlive(actor)) return nullptr;
    void* tracker = Get<void*>(actor, Offset("Actor", "characterDataTracker"));
    return tracker ? Get<void*>(tracker, Offset("CharacterDataTracker", "charData")) : nullptr;
}
std::string IdOf(void* data) { return data ? Text(Invoke(Method(data, "get_Id", 0), data)) : std::string(); }
std::string NameOfCharacter(void* data) { return data ? Text(Get<void*>(data, Offset("CharacterData", "<CharacterName>k__BackingField", "LE.Data"))) : std::string(); }
// The character whose tile is chosen on the character screen.
std::string SelectedId(void* select) {
    const int index = Value<int>(Method(select, "get_SelectedCharacterIndex", 0), select);
    void* list = Get<void*>(select, Offset("CharacterSelect", "availableCharacterTiles"));
    if (!list) return {};
    for (void* tile : Entries(list))
        if (tile && game::IsAlive(tile) && Get<int>(tile, Offset("CharacterTile", "IndexInList")) == index)
            return IdOf(Get<void*>(tile, Offset("CharacterTile", "characterData")));
    return {};
}
// The creation screen's Rogue (a new character's model); *selected when it is the chosen class.
void* CreationRogue(bool* selected) {
    *selected = false;
    Root type(TypeObject("LE.dll", "", "CharacterCreationSelection"));
    bool inactive = false; void* a[]{type.Get(), &inactive};
    Root found(Invoke(Signature("Object", "FindObjectsOfType", {"System.Type", "System.Boolean"}), nullptr, a));
    for (void* c : ArrayItems(found.Get())) {
        if (!c || Get<int>(c, Offset("CharacterCreationSelection", "characterContext")) != 0) continue;
        if (Get<uint8_t>(c, Offset("CharacterCreationSelection", "characterClassID")) != 4) continue;  // CharacterClassID is a byte: Rogue 4
        // chosen: SELECTED, or SINGLE_DISPLAY once the game shows the class alone (name entry)
        const int position = Get<int>(c, Offset("CharacterCreationSelection", "positionState"));
        *selected = position == 2 || position == 3;
        return c;
    }
    return nullptr;
}
// Whose Rogue a body is: nobody's on the creation screen (a new character keeps the game's Rogue), the chosen tile's
// on the character screen, else the character in play (its mirror copy and its shadows too).
struct Wearer { bool creation = false; std::string id; };
// The creation screen's model a body belongs to: the one whose animator holds it (the model need not sit under
// the CharacterCreationSelection itself).
void* ModelOwner(void* body) {
    Root type(TypeObject("LE.dll", "", "CharacterCreationSelection"));
    bool inactive = false; void* a[]{type.Get(), &inactive};
    Root found(Invoke(Signature("Object", "FindObjectsOfType", {"System.Type", "System.Boolean"}), nullptr, a));
    void* bodyTransform = TransformOf(body);
    for (void* c : ArrayItems(found.Get())) {
        if (!c) continue;
        void* animator = Get<void*>(c, Offset("CharacterCreationSelection", "characterAnimator"));
        for (void* root : {animator && game::IsAlive(animator) ? TransformOf(animator) : nullptr, TransformOf(c)}) {
            if (!root) continue;
            void* ia[]{root};
            if (Value<bool>(Unity("Transform", "IsChildOf", 1), bodyTransform, ia)) return c;
        }
    }
    return nullptr;
}
Wearer WhoWears(void* body, void* select) {
    if (!select) return {false, IdOf(PlayerData())};
    void* owner = ModelOwner(body);
    if (owner && Get<int>(owner, Offset("CharacterCreationSelection", "characterContext")) == 0) return {true, {}};
    return {false, SelectedId(select)};
}
bool Wants(const Wearer& w) { return w.creation ? false : w.id.empty() ? choices.fallback : ChoiceOf(w.id); }
void* FindZerrickData() {
    Root type(TypeObject("LE.dll", "", "ActorData"));
    void* a[]{type.Get()};
    Root all(Invoke(Signature("Resources", "FindObjectsOfTypeAll", {"System.Type"}), nullptr, a));
    for (void* d : ArrayItems(all.Get())) if (d && NameOf(d) == "Zerrick Ally Monolith") return d;
    return nullptr;
}
void StartLoad() {
    void* data = FindZerrickData();
    if (!data) throw std::runtime_error("no Zerrick Ally Monolith actor data");
    void* soft = Get<void*>(data, Offset("ActorData", "VisualsSoftRef"));
    if (!soft) throw std::runtime_error("Zerrick has no visuals reference");
    void* file = il2cpp::api().string_new("epochpact-bodyswap"); int line = 1; void* la[]{file, &line};
    loadRef = std::make_unique<Root>(Invoke(Method(soft, "CreateLoadRefTypelessImpl", 2), soft, la));
}
void* LoadedPrefab() {
    if (prefab && prefab->Get()) return prefab->Get();
    if (!loadRef) StartLoad();
    loadStatus = Value<int>(Method(loadRef->Get(), "get_Status", 0), loadRef->Get());
    void* asset = Invoke(Method(loadRef->Get(), "get_TypelessAssetOrNull", 0), loadRef->Get());
    if (!asset) return nullptr;
    Log("bodyswap: loaded %s", il2cpp::api().class_get_name(il2cpp::api().object_get_class(asset)));
    const std::string cls = il2cpp::api().class_get_name(il2cpp::api().object_get_class(asset));
    if (cls != "GameObject") asset = GameObjectOf(asset);
    prefab = std::make_unique<Root>(asset);
    return asset;
}

void SetLayer(void* transform, int layer, int depth = 0) {
    if (!transform || depth > 64) return;
    void* go = GameObjectOf(transform);
    int l = layer; void* a[]{&l}; Invoke(Unity("GameObject", "set_layer", 1), go, a);
    const int n = Value<int>(Unity("Transform", "get_childCount", 0), transform);
    for (int i = 0; i < n; ++i) { int k = i; void* ga[]{&k}; SetLayer(Invoke(Unity("Transform", "GetChild", 1), transform, ga), layer, depth + 1); }
}
// The creation screen's Rogue holds her bow, arrow and daggers as meshes of her own model, not on weapon joints.
bool WeaponMesh(const std::string& name) {
    for (const char* w : {"Bow", "Arrow", "Dagger", "Quiver"}) if (name.find(w) != std::string::npos) return true;
    return false;
}
// The Rogue model above a mesh (MODEL_Rogue_2025 in play and on the character screen, MODEL_Rogue_2023_CS on the
// creation screen).
void* RogueModelOf(void* renderer) {
    void* model = TransformOf(renderer);
    while (model && NameOf(model).rfind("MODEL_Rogue", 0) != 0) model = Parent(model);
    return model;
}
void Hide(Puppet& p) {
    // everything the Rogue draws but its weapons and effects: body, hair, armour, back slot
    for (const char* kind : {"SkinnedMeshRenderer", "MeshRenderer"}) {
        for (void* r : ComponentsUnder(GameObjectOf(p.rogueRoot->Get()), "UnityEngine.CoreModule.dll", kind)) {
            if (!r) continue;
            void* t = TransformOf(r);
            // the creation screen's bow and arrow stay (they sit in his hands); its daggers are skinned to the hidden
            // Rogue's own hand bones, so they would hang in the air beside him: they go with her
            const std::string mesh = NameOf(r);
            const bool held = WeaponMesh(mesh) && mesh.find("Dagger") == std::string::npos;
            if (Under(t, "Weapon_JNT") || Under(t, "FX") || Under(t, "vfx") || held) continue;
            // another model beside the Rogue (the creation screen keeps all five classes under one group) stays
            void* owner = t;
            while (owner && NameOf(owner).rfind("MODEL_", 0) != 0) owner = Parent(owner);
            if (owner && owner != p.rogueModel->Get()) continue;
            if (!Value<bool>(Unity("Renderer", "get_enabled", 0), r)) continue;
            bool off = false; void* a[]{&off}; Invoke(Unity("Renderer", "set_enabled", 1), r, a);
            p.hidden.push_back(std::make_unique<Root>(r));
            ++p.hiddenCount;
        }
    }
}
std::unique_ptr<Puppet> Make(void* body) {
    void* prefabObject = LoadedPrefab();
    if (!prefabObject) return nullptr;
    auto p = std::make_unique<Puppet>();
    p->rogueBody = std::make_unique<Root>(body);
    void* model = RogueModelOf(body);
    if (!model) throw std::runtime_error("the Rogue body has no MODEL_Rogue above it");
    void* root = Parent(model);
    if (!root) throw std::runtime_error("the Rogue model has no visuals object");
    p->rogueModel = std::make_unique<Root>(model);
    p->rogueRoot = std::make_unique<Root>(root);
    // Zerrick, built inside an inactive holder (none of his scripts wake), then his scripts and animator off
    Log("bodyswap: make on %s", NameOf(model).c_str());
    auto& api = il2cpp::api();
    void* holder = api.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"));
    { void* na[]{api.string_new("EP Zerrick")}; Invoke(Signature("GameObject", ".ctor", {"System.String"}), holder, na); }
    p->holder = std::make_unique<Root>(holder);
    { bool off = false; void* a[]{&off}; Invoke(Unity("GameObject", "SetActive", 1), holder, a); }
    void* ht = TransformOfObject(holder);
    { bool keep = false; void* a[]{Parent(root), &keep}; Invoke(Signature("Transform", "SetParent", {"UnityEngine.Transform", "System.Boolean"}), ht, a); }
    for (const char* prop : {"localPosition", "localScale"}) {
        V v = Value<V>(Unity("Transform", (std::string("get_") + prop).c_str(), 0), root); void* a[]{&v};
        Invoke(Unity("Transform", (std::string("set_") + prop).c_str(), 1), ht, a);
    }
    { Q q = Value<Q>(Unity("Transform", "get_localRotation", 0), root); void* a[]{&q}; Invoke(Unity("Transform", "set_localRotation", 1), ht, a); }
    Log("bodyswap: holder ready");
    bool world = false; void* ia[]{prefabObject, ht, &world};
    void* copy = Invoke(Signature("Object", "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform", "System.Boolean"}), nullptr, ia);
    Log("bodyswap: instantiated %s", NameOf(copy).c_str());
    for (void* b : ComponentsUnder(copy, "UnityEngine.CoreModule.dll", "MonoBehaviour")) if (b) { bool off = false; void* a[]{&off}; Invoke(Method(b, "set_enabled", 1), b, a); }
    for (void* b : ComponentsUnder(copy, "UnityEngine.AnimationModule.dll", "Animator")) if (b) { bool off = false; void* a[]{&off}; Invoke(Method(b, "set_enabled", 1), b, a); }
    const int layer = Value<int>(Unity("GameObject", "get_layer", 0), GameObjectOf(body));
    SetLayer(TransformOfObject(copy), layer);
    std::map<std::string, void*> z, r;
    Walk(TransformOfObject(copy), z);
    Walk(model, r);
    void* zModel = z.count("MODEL_Zerrick") ? z["MODEL_Zerrick"] : nullptr;
    if (!zModel) throw std::runtime_error("Zerrick's visuals have no MODEL_Zerrick");
    p->zerrickModel = std::make_unique<Root>(zModel);
    // the same size as the Rogue model (the creation screen's models are scaled), and in its place (Drive keeps it there)
    {
        const V rs = Value<V>(Unity("Transform", "get_lossyScale", 0), model), zs = Value<V>(Unity("Transform", "get_lossyScale", 0), zModel);
        if (rs.x > 1e-4f && zs.x > 1e-4f && std::fabs(rs.x / zs.x - 1.f) > 0.001f) {
            V hs = Scale(Value<V>(Unity("Transform", "get_localScale", 0), ht), rs.x / zs.x); void* a[]{&hs};
            Invoke(Unity("Transform", "set_localScale", 1), ht, a);
        }
        SetPosition(zModel, Position(model));
        SetRotation(zModel, Rotation(model));
    }
    // his own weapons (if any) stay out: the player's weapons show
    for (const char* kind : {"SkinnedMeshRenderer", "MeshRenderer"})
        for (void* rr : ComponentsUnder(copy, "UnityEngine.CoreModule.dll", kind)) {
            if (!rr) continue;
            const std::string n = NameOf(rr);
            // only his body and its cloth: his own weapons (a sword over his shoulder too) stay out
            const bool body = n == "Zerrick_Body_GEO" || n == "Zerrick_Skirt_Cloth_GEO";
            void* up = Parent(TransformOf(rr));
            p->zerrickRenderers.push_back(n + "@" + (up ? NameOf(up) : std::string()) + (body ? "" : " (off)"));
            if (!body) { bool off = false; void* a[]{&off}; Invoke(Unity("Renderer", "set_enabled", 1), rr, a); }
        }
    Log("bodyswap: scripts off, %d renderers", static_cast<int>(p->zerrickRenderers.size()));
    { bool onNow = true; void* a[]{&onNow}; Invoke(Unity("GameObject", "SetActive", 1), holder, a); }
    Log("bodyswap: shown");
    // rest poses from the bind poses
    void* zBody = nullptr;
    for (void* s : ComponentsUnder(copy, "UnityEngine.CoreModule.dll", "SkinnedMeshRenderer")) if (s && NameOf(s) == "Zerrick_Body_GEO") zBody = s;
    if (!zBody) throw std::runtime_error("no Zerrick_Body_GEO");
    auto rogueRest = RestOf(body, model);
    // bones the anchor mesh does not carry come from the model's other meshes (the creation screen's Rogue has no
    // body mesh: head, hair, armour, gloves and boots); the anchor's own bind poses come first
    for (void* s : ComponentsUnder(GameObjectOf(model), "UnityEngine.CoreModule.dll", "SkinnedMeshRenderer")) {
        if (!s || s == body || WeaponMesh(NameOf(s))) continue;
        for (auto& [name, rest] : RestOf(s, model)) rogueRest.emplace(name, rest);
    }
    Log("bodyswap: rogue rest %d", static_cast<int>(rogueRest.size()));
    const auto zerrickRest = RestOf(zBody, zModel);
    // Zerrick's copy is fresh and his animator never ran: every bone stands at rest, the bare ones too (his bind
    // poses agree within 0.04 degrees in the 1.5.4 bundles; restCheck measures it on the live copy)
    const Q zInv = Inv(Rotation(zModel));
    for (const auto& [name, rest] : zerrickRest)
        if (z.count(name)) p->restCheck = std::max(p->restCheck, Angle(rest.rotation, Norm(Mul(zInv, Rotation(z[name])))));
    for (const auto& [rn, zn] : Map()) {
        if (!r.count(rn) || !z.count(zn)) continue;
        Q rogueAtRest;
        if (!RogueRest(rogueRest, r[rn], &rogueAtRest)) continue;
        Bone b;
        b.rogue = std::make_unique<Root>(r[rn]); b.zerrick = std::make_unique<Root>(z[zn]);
        b.rogueRest = rogueAtRest; b.zerrickRest = Norm(Mul(zInv, Rotation(z[zn])));
        for (void* t = z[zn]; t && t != zModel; t = Parent(t)) ++b.depth;
        p->bones.push_back(std::move(b));
    }
    std::sort(p->bones.begin(), p->bones.end(), [](const Bone& a, const Bone& b) { return a.depth < b.depth; });
    // the hips carry the body up and down: Zerrick's hips move as the Rogue's pelvis does, scaled to his height
    if (z.count("Hips_JNT") && r.count("Pelvis_JNT") && rogueRest.count("Pelvis_JNT") && zerrickRest.count("Pelvis_JNT")) {
        p->zerrickHips = std::make_unique<Root>(z["Hips_JNT"]);
        p->roguePelvis = std::make_unique<Root>(r["Pelvis_JNT"]);
        p->zerrickHipsRest = Rot(Inv(Rotation(zModel)), Sub(Position(z["Hips_JNT"]), Position(zModel)));
        p->roguePelvisRest = rogueRest.at("Pelvis_JNT").position;
        const float rh = rogueRest.at("Pelvis_JNT").position.y, zh = zerrickRest.at("Pelvis_JNT").position.y;
        p->scale = (rh > 0.1f && zh > 0.1f) ? zh / rh : 1.f;
    }
    for (int s = 0; s < 2; ++s) {
        const std::string side = s ? "R_" : "L_";
        if (r.count(side + "Weapon_JNT") && r.count(side + "Hand_JNT") && z.count(side + "Hand_JNT")) {
            p->rogueWeapon[s] = std::make_unique<Root>(r[side + "Weapon_JNT"]);
            p->rogueHand[s] = std::make_unique<Root>(r[side + "Hand_JNT"]);
            p->zerrickHand[s] = std::make_unique<Root>(z[side + "Hand_JNT"]);
            p->weaponLocal[s] = Value<V>(Unity("Transform", "get_localPosition", 0), r[side + "Weapon_JNT"]);
        }
    }
    Log("bodyswap: %d bones, rest check %.2f deg", static_cast<int>(p->bones.size()), p->restCheck);
    Hide(*p);
    Log("bodyswap: hidden %d", p->hiddenCount);
    return p;
}
void Drive(Puppet& p) {
    void* rm = p.rogueModel->Get(); void* zm = p.zerrickModel->Get();
    // Zerrick stands where the Rogue model stands, facing the same way
    const Q rq = Rotation(rm), zq = rq;
    SetPosition(zm, Position(rm));
    SetRotation(zm, rq);
    if (p.zerrickHips) {
        const V now = Rot(Inv(rq), Sub(Position(p.roguePelvis->Get()), Position(rm)));
        const V moved = Scale(Sub(now, p.roguePelvisRest), p.scale);
        SetPosition(p.zerrickHips->Get(), Add(Position(zm), Rot(zq, Add(p.zerrickHipsRest, moved))));
    }
    // the change from rest in the Rogue model's space, put on Zerrick's rest in his model's space
    const Q rqInv = Inv(rq);
    for (auto& b : p.bones) {
        const Q change = Mul(Mul(rqInv, Rotation(b.rogue->Get())), Inv(b.rogueRest));
        SetRotation(b.zerrick->Get(), Norm(Mul(zq, Mul(change, b.zerrickRest))));
    }
    for (int s = 0; s < 2; ++s) {
        if (!p.rogueWeapon[s]) continue;
        { V local = p.weaponLocal[s]; void* a[]{&local}; Invoke(Unity("Transform", "set_localPosition", 1), p.rogueWeapon[s]->Get(), a); }
        const V shift = Sub(Position(p.zerrickHand[s]->Get()), Position(p.rogueHand[s]->Get()));
        SetPosition(p.rogueWeapon[s]->Get(), Add(Position(p.rogueWeapon[s]->Get()), shift));
    }
}
bool Alive(const std::unique_ptr<Root>& r) { return r && r->Get() && game::IsAlive(r->Get()); }
void Drop(Puppet& p) {
    for (auto& h : p.hidden) if (Alive(h)) { bool v = true; void* a[]{&v}; try { Invoke(Unity("Renderer", "set_enabled", 1), h->Get(), a); } catch (...) {} }
    if (Alive(p.holder)) { void* a[]{p.holder->Get()}; try { Invoke(Unity("Object", "Destroy", 1), nullptr, a); } catch (...) {} }
}
void Scan(void* select) {
    ++scans;
    // gone: the Rogue model left or was put away (another scene, the creation screen over the character screen)
    for (auto it = puppets.begin(); it != puppets.end();) {
        const bool shown = Alive((*it)->rogueBody) && Alive((*it)->zerrickModel) &&
                           Value<bool>(Unity("GameObject", "get_activeInHierarchy", 0), GameObjectOf((*it)->rogueBody->Get()));
        if (!shown) { Drop(**it); it = puppets.erase(it); }
        else ++it;
    }
    // the Rogue bodies on screen, each with its own character's choice (the character screen shows one at a time)
    Root type(TypeObject("UnityEngine.CoreModule.dll", "UnityEngine", "SkinnedMeshRenderer"));
    bool inactive = false; void* a[]{type.Get(), &inactive};
    Root found(Invoke(Signature("Object", "FindObjectsOfType", {"System.Type", "System.Boolean"}), nullptr, a));
    for (void* r : ArrayItems(found.Get())) {
        if (!r) continue;
        const std::string name = NameOf(r);
        if (name != "Rogue_Body_GEO") continue;  // the creation screen's Rogue (no body mesh) keeps her own look
        void* model = RogueModelOf(r);
        if (!model) continue;
        const bool want = Wants(WhoWears(r, select));
        auto it = std::find_if(puppets.begin(), puppets.end(), [&](const std::unique_ptr<Puppet>& p) { return p->rogueModel->Get() == model; });
        if (it != puppets.end()) {
            if (!want) { Drop(**it); puppets.erase(it); }
            continue;
        }
        if (!want) continue;
        auto p = Make(r);
        if (!p) { phase = "loading"; return; }
        puppets.push_back(std::move(p));
    }
    // armour equipped later: hidden too
    for (auto& p : puppets) Hide(*p);
    phase = "on";
}
void Fault(const std::exception& e) {
    lastError = e.what();
    Log("bodyswap: %s", e.what());
    if (++faults > 20) { on = false; phase = "stopped after faults"; Log("bodyswap: stopped"); }
}
// After the animation of the frame: the pose only (nothing made, shown or hidden here).
void Late() {
    if (!on.load() || puppets.empty()) return;
    LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    try {
        ++frames;
        for (auto& p : puppets) if (Alive(p->zerrickModel) && Alive(p->rogueModel)) Drive(*p);
    } catch (const std::exception& e) { Fault(e); }
    QueryPerformanceCounter(&b);
    lastMs = double(b.QuadPart - a.QuadPart) * 1000.0 / double(f.QuadPart);
}
uint64_t ticks = 0;
void Off() {
    on = false;
    for (auto& p : puppets) Drop(*p);
    puppets.clear();
    phase = "off";
}
// Zerrick's voice. The Rogue's voice lines and efforts are FMOD events; every event the game plays is looked up by
// its GUID in RuntimeManager.GetEventDescription(GUID), so with Zerrick's look on the character in play each Rogue
// voice event is looked up as its male counterpart instead (the creation and character screens keep her voice).
struct Guid { uint32_t d[4]{}; };
bool operator<(const Guid& a, const Guid& b) { return std::memcmp(a.d, b.d, sizeof a.d) < 0; }
bool operator==(const Guid& a, const Guid& b) { return std::memcmp(a.d, b.d, sizeof a.d) == 0; }
std::string GuidText(const Guid& g) {
    char b[48]; std::snprintf(b, sizeof b, "%08x-%08x-%08x-%08x", g.d[0], g.d[1], g.d[2], g.d[3]);
    return b;
}
struct Fmod {
    bool ready = false;
    int (*bankCount)(void*, int*) = nullptr;
    int (*bankList)(void*, void**, int, int*) = nullptr;
    int (*bankPath)(void*, char*, int, int*) = nullptr;
    int (*eventCount)(void*, int*) = nullptr;
    int (*eventList)(void*, void**, int, int*) = nullptr;
    int (*eventPath)(void*, char*, int, int*) = nullptr;
    int (*eventId)(void*, Guid*) = nullptr;
    int (*lookupId)(void*, const char*, Guid*) = nullptr;
    int (*lookupPath)(void*, const Guid*, char*, int, int*) = nullptr;
};
Fmod& Api() {
    static Fmod f;
    if (f.ready) return f;
    HMODULE m = GetModuleHandleW(L"fmodstudio.dll");
    if (!m) return f;
    auto get = [m](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(m, name)); return fn != nullptr; };
    f.ready = get(f.bankCount, "FMOD_Studio_System_GetBankCount") && get(f.bankList, "FMOD_Studio_System_GetBankList") &&
              get(f.bankPath, "FMOD_Studio_Bank_GetPath") && get(f.eventCount, "FMOD_Studio_Bank_GetEventCount") &&
              get(f.eventList, "FMOD_Studio_Bank_GetEventList") && get(f.eventPath, "FMOD_Studio_EventDescription_GetPath") &&
              get(f.eventId, "FMOD_Studio_EventDescription_GetID") && get(f.lookupId, "FMOD_Studio_System_LookupID") &&
              get(f.lookupPath, "FMOD_Studio_System_LookupPath");
    return f;
}
void* StudioSystem() { return Value<void*>(game::FindMethod("FMODUnity.dll", "FMODUnity", "RuntimeManager", "get_StudioSystem", 0)); }
std::string PathOf(const Guid& g) {
    auto& f = Api();
    void* system = f.ready ? StudioSystem() : nullptr;
    if (!system) return {};
    char path[512]{}; int got = 0;
    return f.lookupPath(system, &g, path, sizeof path, &got) == 0 ? std::string(path) : std::string();
}
bool IdOfPath(const std::string& path, Guid* out) {
    auto& f = Api();
    void* system = f.ready ? StudioSystem() : nullptr;
    return system && f.lookupId(system, path.c_str(), out) == 0;
}
// Every event in the loaded banks: path -> GUID.
std::vector<std::pair<std::string, Guid>> AllEvents() {
    std::vector<std::pair<std::string, Guid>> out;
    auto& f = Api();
    void* system = f.ready ? StudioSystem() : nullptr;
    if (!system) throw std::runtime_error("FMOD is not ready");
    int banks = 0;
    if (f.bankCount(system, &banks) != 0 || banks <= 0) return out;
    std::vector<void*> bankList(static_cast<size_t>(banks));
    int gotBanks = 0;
    f.bankList(system, bankList.data(), banks, &gotBanks);
    for (int b = 0; b < gotBanks; ++b) {
        int n = 0;
        if (f.eventCount(bankList[b], &n) != 0 || n <= 0) continue;
        std::vector<void*> events(static_cast<size_t>(n));
        int got = 0;
        f.eventList(bankList[b], events.data(), n, &got);
        for (int i = 0; i < got; ++i) {
            char path[512]{}; int len = 0; Guid id;
            if (f.eventPath(events[i], path, sizeof path, &len) != 0) path[0] = 0;
            f.eventId(events[i], &id);
            out.emplace_back(path, id);
        }
    }
    return out;
}

using DescFn = void* (*)(Guid*, const il2cpp::Method*);
DescFn originalDesc = nullptr;
bool voiceHooked = false;
std::atomic<bool> voiceOn{false};
std::atomic<std::shared_ptr<const std::map<Guid, Guid>>> voiceMap;
std::atomic<uint64_t> voiceSwaps{0};
bool voiceBuilt = false;
std::string voiceError;
int voicePairs = 0, voiceMissing = 0;
#ifdef EPOCHPACT_TESTING
// Tests: the events the game looked up lately (GUID, swapped or not).
std::mutex heardLock;
std::deque<std::pair<Guid, bool>> heard;
void Heard(const Guid& g, bool swapped) {
    std::lock_guard<std::mutex> lock(heardLock);
    heard.emplace_back(g, swapped);
    while (heard.size() > 400) heard.pop_front();
}
#endif
void* DescHook(Guid* guid, const il2cpp::Method* method) {
    if (guid && voiceOn.load()) {
        if (const auto map = voiceMap.load()) {
            if (const auto it = map->find(*guid); it != map->end()) {
                Guid swapped = it->second;
                ++voiceSwaps;
#ifdef EPOCHPACT_TESTING
                Heard(swapped, true);
#endif
                return originalDesc(&swapped, method);
            }
        }
    }
#ifdef EPOCHPACT_TESTING
    if (guid) Heard(*guid, false);
#endif
    return originalDesc(guid, method);
}
// The Rogue's combat voice (event:/Voice Acting/Rogue/Combat_General) and the male voice it becomes: Zerrick's own
// combat lines (attack, hit, kill), and for what he never says (no mana, cannot equip, bags full) the Sentinel's.
#define EP_ROGUE "event:/Voice Acting/Rogue/Combat_General/va_rogue_combat_"
#define EP_ZERRICK "event:/Voice Acting/Zerrick_Human/va_zerrick_human_combat_"
#define EP_SENTINEL "event:/Voice Acting/Sentinel/Combat_General/va_sentinel_gencombat_"
const std::vector<std::pair<const char*, const char*>>& VoicePairs() {
    static const std::vector<std::pair<const char*, const char*>> pairs{
        {EP_ROGUE "notquiteright", EP_SENTINEL "notpossible"},   // cannot equip a weapon
        {EP_ROGUE "notagoodfit", EP_SENTINEL "notagoodfit"},     // cannot equip an item
        {EP_ROGUE "dropsomething", EP_SENTINEL "tooheavy"},      // no room in the bags
        {EP_ROGUE "outofmana", EP_SENTINEL "nomana"},            // no mana
        {EP_ROGUE "needmana", EP_SENTINEL "nomana"},
        {EP_ROGUE "nothappening", EP_SENTINEL "cannotdothat"},   // cannot use the skill
        {EP_ROGUE "gettinghairy", EP_SENTINEL "strengthfading"},
        {EP_ROGUE "thatwasclose", EP_SENTINEL "needtoholdon"},
        {EP_ROGUE "grunt", EP_ZERRICK "attack"},                 // attacks (and the hit line, see HitLine)
        {EP_ROGUE "tooeasy", EP_ZERRICK "rarekill"},             // a kill
        {EP_ROGUE "laugh", EP_ZERRICK "rarekill"},
        {EP_ROGUE "notachance", EP_ZERRICK "rarekill"},
        {EP_ROGUE "toogreedy", EP_ZERRICK "rarekill"},
        {EP_ROGUE "wontforget", EP_ZERRICK "rarekill"},
    };
    return pairs;
}
Guid rogueGrunt{}, zerrickHit{};
bool hitKnown = false;
void BuildVoice() {
    auto map = std::make_shared<std::map<Guid, Guid>>();
    voicePairs = voiceMissing = 0;
    for (const auto& [rogue, male] : VoicePairs()) {
        Guid a, b;
        if (IdOfPath(rogue, &a) && IdOfPath(male, &b)) { (*map)[a] = b; ++voicePairs; }
        else { ++voiceMissing; Log("bodyswap: voice event missing: %s -> %s", rogue, male); }
    }
    hitKnown = IdOfPath(EP_ROGUE "grunt", &rogueGrunt) && IdOfPath(EP_ZERRICK "onhit", &zerrickHit);
    voiceMap.store(std::shared_ptr<const std::map<Guid, Guid>>(map));
    voiceBuilt = true;
}
#undef EP_ROGUE
#undef EP_ZERRICK
#undef EP_SENTINEL
// The Rogue's WhenHit voice line plays the same grunt as her attacks: with his look on it points at his hit line.
int hitLineState = -1;  // -1 unknown, 0 hers, 1 his
void HitLine(bool his) {
    if (!hitKnown) return;
    void* manager = game::StaticObject(game::FindStaticField("LE.dll", "", "PlayerVoiceEventManager", "instance"));
    if (!manager) return;
    auto& api = il2cpp::api();
    const auto* listField = api.class_get_field_from_name(api.object_get_class(manager), "classVoiceLines");
    if (!listField) return;
    for (void* cls : Entries(Get<void*>(manager, api.field_get_offset(listField)))) {
        if (!cls) continue;
        const auto* classField = api.class_get_field_from_name(api.object_get_class(cls), "characterClassID");
        const auto* linesField = api.class_get_field_from_name(api.object_get_class(cls), "voiceLines");
        if (!classField || !linesField || Get<uint8_t>(cls, api.field_get_offset(classField)) != 4) continue;
        for (void* line : Entries(Get<void*>(cls, api.field_get_offset(linesField)))) {
            if (!line) continue;
            const auto* eventField = api.class_get_field_from_name(api.object_get_class(line), "voiceEvent");
            const auto* soundField = api.class_get_field_from_name(api.object_get_class(line), "soundEvent");
            if (!eventField || !soundField || Get<int>(line, api.field_get_offset(eventField)) != 8) continue;  // WhenHit
            Guid* guid = reinterpret_cast<Guid*>(static_cast<char*>(line) + api.field_get_offset(soundField));
            const Guid from = his ? rogueGrunt : zerrickHit, to = his ? zerrickHit : rogueGrunt;
            if (*guid == from) *guid = to;
        }
    }
    hitLineState = his ? 1 : 0;
}
void Voice(void* select) {
    const bool want = !select && currentWanted.load() && voiceHooked;
    if (want && !voiceBuilt && Api().ready) {
        try { BuildVoice(); } catch (const std::exception& e) { voiceError = e.what(); voiceBuilt = true; }
    }
    voiceOn = want;
    // the voice manager may be made again (a new scene): set the hit line each time
    if (voiceBuilt) { try { HitLine(want); } catch (const std::exception& e) { voiceError = e.what(); } }
}
void VoiceRelease() {
    voiceOn = false;
    if (hitLineState == 1) { try { HitLine(false); } catch (...) {} }
}
std::string VoiceRead() {
    std::ostringstream out;
    out << "{\"hooked\":" << Boolean(voiceHooked) << ",\"on\":" << Boolean(voiceOn.load()) << ",\"fmod\":" << Boolean(Api().ready) << ",\"pairs\":" << voicePairs
        << ",\"missing\":" << voiceMissing << ",\"hitLine\":" << hitLineState << ",\"swaps\":" << voiceSwaps.load() << ",\"error\":" << Json(voiceError) << '}';
    return out.str();
}
#ifdef EPOCHPACT_TESTING
void* FieldObject(void* object, const char* name, size_t* offset) {
    const auto* field = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(object), name);
    if (!field) throw std::runtime_error(std::string("no field ") + name);
    *offset = il2cpp::api().field_get_offset(field);
    return object;
}
template<class T> T FieldValue(void* object, const char* name) { size_t at = 0; FieldObject(object, name, &at); return Get<T>(object, at); }
// Tests: the voice lines of every class (PlayerVoiceEventManager), with their events' paths.
std::string VoiceLines() {
    void* manager = game::StaticObject(game::FindStaticField("LE.dll", "", "PlayerVoiceEventManager", "instance"));
    if (!manager) throw std::runtime_error("no PlayerVoiceEventManager");
    std::ostringstream out; out.setf(std::ios::fixed); out.precision(2);
    out << "{\"ok\":true,\"classes\":[";
    bool firstClass = true;
    for (void* cls : Entries(FieldValue<void*>(manager, "classVoiceLines"))) {
        if (!cls) continue;
        out << (firstClass ? "" : ",") << "{\"class\":" << static_cast<int>(FieldValue<uint8_t>(cls, "characterClassID")) << ",\"lines\":[";
        bool first = true;
        for (void* line : Entries(FieldValue<void*>(cls, "voiceLines"))) {
            if (!line) continue;
            size_t at = 0; FieldObject(line, "soundEvent", &at);
            const Guid g = Get<Guid>(line, at);
            void* path = Get<void*>(line, at + 16);
            out << (first ? "" : ",") << "{\"event\":" << FieldValue<int>(line, "voiceEvent") << ",\"chance\":" << FieldValue<float>(line, "chance")
                << ",\"cooldown\":" << FieldValue<float>(line, "cooldown") << ",\"guid\":" << Json(GuidText(g)) << ",\"path\":" << Json(path ? Text(path) : std::string())
                << ",\"bank\":" << Json(PathOf(g)) << '}';
            first = false;
        }
        out << "]}";
        firstClass = false;
    }
    out << "]}";
    return out.str();
}
std::string FmodList(const std::string& pattern) {
    const std::regex re(pattern, std::regex::icase);
    std::ostringstream out;
    out << "{\"ok\":true,\"events\":[";
    int n = 0, total = 0;
    for (const auto& [path, id] : AllEvents()) {
        ++total;
        if (!std::regex_search(path, re)) continue;
        if (n < 1500) out << (n ? "," : "") << '[' << Json(path) << ',' << Json(GuidText(id)) << ']';
        ++n;
    }
    out << "],\"matching\":" << n << ",\"total\":" << total << '}';
    return out.str();
}
std::string HeardList(bool clear) {
    std::vector<std::pair<Guid, bool>> copy;
    {
        std::lock_guard<std::mutex> lock(heardLock);
        copy.assign(heard.begin(), heard.end());
        if (clear) heard.clear();
    }
    std::ostringstream out;
    out << "{\"ok\":true,\"heard\":[";
    for (size_t i = 0; i < copy.size(); ++i) out << (i ? "," : "") << '[' << Json(PathOf(copy[i].first)) << ',' << Boolean(copy[i].second) << ']';
    out << "]}";
    return out.str();
}
// Tests: the Rogue's voice line for an event (VoiceLineEvent number), played on the player now (no chance, no cooldown).
std::string Say(int voiceEvent) {
    void* manager = game::StaticObject(game::FindStaticField("LE.dll", "", "PlayerVoiceEventManager", "instance"));
    void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
    if (!manager || !actor) throw std::runtime_error("no voice manager or player");
    for (void* cls : Entries(FieldValue<void*>(manager, "classVoiceLines"))) {
        if (!cls || FieldValue<uint8_t>(cls, "characterClassID") != 4) continue;
        for (void* line : Entries(FieldValue<void*>(cls, "voiceLines"))) {
            if (!line || FieldValue<int>(line, "voiceEvent") != voiceEvent) continue;
            void* a[]{TransformOf(actor)};
            Invoke(Method(line, "play", 1), line, a);
            return "{\"ok\":true,\"played\":true}";
        }
    }
    return "{\"ok\":true,\"played\":false}";
}
#endif
#ifdef EPOCHPACT_TESTING
// Tests: the player walks dx, dz metres as after a click on the ground (the game's own MovingPlayer.MoveToPoint).
std::string TestWalk(const std::string& spec) {
    float dx = 0, dz = 0;
    if (std::sscanf(spec.c_str(), "%f,%f", &dx, &dz) != 2) throw std::runtime_error("use bodyswap walk:dx,dz");
    void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
    if (!actor || !game::IsAlive(actor)) throw std::runtime_error("no player");
    Root type(TypeObject("LE.dll", "", "MovingPlayer"));
    bool inactive = true; void* ga[]{type.Get(), &inactive};
    void* moving = Invoke(Signature("GameObject", "GetComponentInChildren", {"System.Type", "System.Boolean"}), GameObjectOf(actor), ga);
    if (!moving) throw std::runtime_error("no MovingPlayer");
    const V from = Position(TransformOf(actor));
    V to{from.x + dx, from.y, from.z + dz};
    bool noUi = false, buffer = true; void* ma[]{&to, &noUi, &buffer};
    const bool ok = Value<bool>(Method(moving, "MoveToPoint", 3), moving, ma);
    std::ostringstream out; out.setf(std::ios::fixed); out.precision(2);
    out << "{\"ok\":true,\"moving\":" << Boolean(ok) << ",\"from\":[" << from.x << ',' << from.y << ',' << from.z << "]}";
    return out.str();
}
// Tests: how far each of Zerrick's limbs points from the Rogue's, in degrees (each in its own model's space), and
// where the Rogue's thighs point (a walk swings them).
std::string TestPose() {
    struct Segment { const char *name, *ra, *rb, *za, *zb; };
    static const Segment segments[] = {
        {"L_thigh", "L_Leg_JNT", "L_Foreleg_JNT", "L_Thigh_JNT", "L_Knee_JNT"}, {"L_shin", "L_Foreleg_JNT", "L_Ankle_JNT", "L_Knee_JNT", "L_Ankle_JNT"},
        {"R_thigh", "R_Leg_JNT", "R_Foreleg_JNT", "R_Thigh_JNT", "R_Knee_JNT"}, {"R_shin", "R_Foreleg_JNT", "R_Ankle_JNT", "R_Knee_JNT", "R_Ankle_JNT"},
        {"L_arm", "L_Arm_JNT", "L_Forearm_JNT", "L_Arm_JNT", "L_Forearm_JNT"}, {"L_forearm", "L_Forearm_JNT", "L_Hand_JNT", "L_Forearm_JNT", "L_Hand_JNT"},
        {"R_arm", "R_Arm_JNT", "R_Forearm_JNT", "R_Arm_JNT", "R_Forearm_JNT"}, {"R_forearm", "R_Forearm_JNT", "R_Hand_JNT", "R_Forearm_JNT", "R_Hand_JNT"},
        {"spine", "Spine_0_JNT", "Neck_JNT", "Spine_JNT", "Neck_JNT"}};
    auto direction = [](const Q& inverse, void* a, void* b) {
        const V d = Rot(inverse, Sub(Position(b), Position(a)));
        const float n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        return n > 1e-6f ? Scale(d, 1.f / n) : d;
    };
    std::ostringstream out; out.setf(std::ios::fixed); out.precision(2);
    out << "{\"ok\":true,\"puppets\":[";
    bool first = true;
    for (auto& p : puppets) {
        if (!Alive(p->rogueModel) || !Alive(p->zerrickModel)) continue;
        std::map<std::string, void*> r, z;
        Walk(p->rogueModel->Get(), r);
        Walk(p->zerrickModel->Get(), z);
        const Q ri = Inv(Rotation(p->rogueModel->Get())), zi = Inv(Rotation(p->zerrickModel->Get()));
        out << (first ? "" : ",") << "{\"gaps\":{";
        bool f2 = true;
        for (const auto& s : segments) {
            if (!r.count(s.ra) || !r.count(s.rb) || !z.count(s.za) || !z.count(s.zb)) continue;
            const V a = direction(ri, r[s.ra], r[s.rb]), b = direction(zi, z[s.za], z[s.zb]);
            const float dot = std::max(-1.f, std::min(1.f, a.x * b.x + a.y * b.y + a.z * b.z));
            out << (f2 ? "" : ",") << Json(s.name) << ':' << std::acos(dot) * 57.2958f;
            f2 = false;
        }
        // how far Zerrick's head is from the hidden Rogue's (he must stand in her place)
        float headGap = -1;
        if (r.count("Head_JNT") && z.count("Head_JNT")) { const V d = Sub(Position(r["Head_JNT"]), Position(z["Head_JNT"])); headGap = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); }
        // the Rogue's own daggers still drawn (they would float beside him)
        int daggers = 0;
        for (const char* kind : {"SkinnedMeshRenderer", "MeshRenderer"})
            for (void* m : ComponentsUnder(GameObjectOf(p->rogueModel->Get()), "UnityEngine.CoreModule.dll", kind))
                if (m && NameOf(m).find("Dagger") != std::string::npos && Value<bool>(Unity("Renderer", "get_enabled", 0), m) &&
                    Value<bool>(Unity("GameObject", "get_activeInHierarchy", 0), GameObjectOf(m)) && !Under(TransformOf(m), "Weapon_JNT")) ++daggers;
        out << "},\"headGap\":" << headGap << ",\"daggers\":" << daggers << ",\"model\":" << Json(NameOf(p->rogueModel->Get())) << ",\"rogueThighs\":[";
        for (int k = 0; k < 2; ++k) {
            const char* a = k ? "R_Leg_JNT" : "L_Leg_JNT"; const char* b = k ? "R_Foreleg_JNT" : "L_Foreleg_JNT";
            const V d = r.count(a) && r.count(b) ? direction(ri, r[a], r[b]) : V{};
            out << (k ? "," : "") << '[' << d.x << ',' << d.y << ',' << d.z << ']';
        }
        out << "]}";
        first = false;
    }
    out << "]}";
    return out.str();
}
#endif
}  // namespace
// The frame's update (main thread, EventSystem.Update): loading, making, hiding.
void Frame() {
    bool known = false;
    const bool offline = game::IsOfflinePlay(&known);
    try {
        if (known && !offline) { if (on.load()) Off(); return; }  // never in online play
        void* select = SelectScreen();
        if (ticks++ % (select ? 8 : 30) != 0) return;
        if (!select) currentWanted = ChoiceOf(IdOf(PlayerData()));
        Voice(select);
        if (!AnyOn() || faults > 20) { if (on.load()) Off(); return; }
        if (!on.load()) { on = true; phase = "starting"; }
        Scan(select);
    } catch (const std::exception& e) { Fault(e); }
}
namespace {
void PerformHook(void* self, const il2cpp::Method* method) {
    originalPerform(self, method);
    Late();
}
}  // namespace

// The Wardrobe card: the character in play (main thread).
void Want(bool value) {
    const std::string id = IdOf(PlayerData());
    if (!ValidId(id)) { if (value) throw std::runtime_error("load your Rogue first"); return; }
    choices.characters[id] = value;
    currentWanted = value;
    SaveChoices();
    ticks = 0;
}
bool Wanted() { return currentWanted.load(); }
bool Init() {
    try {
        LoadChoices();
        const auto m = game::FindMethod("UnityEngine.UI.dll", "UnityEngine.UI", "CanvasUpdateRegistry", "PerformUpdate", 0);
        std::string why;
        if (!m || !hook::Install(m.code, reinterpret_cast<void*>(&PerformHook), reinterpret_cast<void**>(&originalPerform), &why)) {
            Log("bodyswap: no frame hook: %s", m ? why.c_str() : "PerformUpdate missing");
            return false;
        }
        hooked = true;
        // Zerrick's voice: the GUID lookup of every event the game plays
        try {
            const auto* lookup = Signature("RuntimeManager", "GetEventDescription", {"FMOD.GUID"}, "FMODUnity", "FMODUnity.dll");
            void* code = *reinterpret_cast<void* const*>(lookup);
            std::string whyVoice;
            voiceHooked = code && hook::Install(code, reinterpret_cast<void*>(&DescHook), reinterpret_cast<void**>(&originalDesc), &whyVoice);
            if (!voiceHooked) Log("bodyswap: no voice hook: %s", whyVoice.c_str());
        } catch (const std::exception& e) { Log("bodyswap: no voice hook: %s", e.what()); }
        Log("bodyswap: ready");
        return true;
    } catch (const std::exception& e) {
        Log("bodyswap: init failed: %s", e.what());
        return false;
    }
}
std::string Command(const std::string& action) {
    return Run([action]() -> std::string {
#ifdef EPOCHPACT_TESTING
        if (action.rfind("walk:", 0) == 0) return TestWalk(action.substr(5));
        if (action == "pose") return TestPose();
#endif
        // on/off: the default for every Rogue without a choice of its own (tests)
        if (action == "on") { if (!hooked) throw std::runtime_error("no frame hook"); faults = 0; lastError.clear(); choices.fallback = true; SaveChoices(); ticks = 0; }
        else if (action == "off") { choices = {}; SaveChoices(); Off(); }
#ifdef EPOCHPACT_TESTING
        else if (action == "next") {
            // the character screen's next tile, as the arrow key does
            void* select = SelectScreen();
            if (!select) throw std::runtime_error("no character screen");
            Invoke(Method(select, "SelectNextEntry", 0), select);
        }
        else if (action == "geo:0" || action == "geo:1") {
            // research: Zerrick's body mesh off/on, to see what else is drawn
            bool v = action == "geo:1";
            for (auto& p : puppets) {
                if (!Alive(p->zerrickModel)) continue;
                for (void* rr : ComponentsUnder(GameObjectOf(p->zerrickModel->Get()), "UnityEngine.CoreModule.dll", "SkinnedMeshRenderer"))
                    if (rr && NameOf(rr) == "Zerrick_Body_GEO") { void* a[]{&v}; Invoke(Unity("Renderer", "set_enabled", 1), rr, a); }
            }
        }
        else if (action == "visible") {
            // research: every renderer still drawn under the Rogue's visuals, with its parent
            std::ostringstream o; o << "{\"ok\":true,\"visible\":[";
            bool f = true;
            for (auto& p : puppets) {
                if (!Alive(p->rogueRoot)) continue;
                for (const char* kind : {"SkinnedMeshRenderer", "MeshRenderer"})
                    for (void* rr : ComponentsUnder(GameObjectOf(p->rogueRoot->Get()), "UnityEngine.CoreModule.dll", kind)) {
                        if (!rr || !Value<bool>(Unity("Renderer", "get_enabled", 0), rr) || !Value<bool>(Unity("GameObject", "get_activeInHierarchy", 0), GameObjectOf(rr))) continue;
                        void* up = Parent(TransformOf(rr));
                        o << (f ? "" : ",") << Json(NameOf(rr) + "@" + (up ? NameOf(up) : std::string()));
                        f = false;
                    }
            }
            o << "]}";
            return o.str();
        }
        else if (action == "back") {
            // the creation screen's Back: from the chosen class to the line of five
            void* select = SelectScreen();
            if (!select) throw std::runtime_error("no character screen");
            Invoke(Method(select, "BackButtonPress", 0), select);
        }
        else if (action == "prev") {
            void* select = SelectScreen();
            if (!select) throw std::runtime_error("no character screen");
            Invoke(Method(select, "SelectPreviousEntry", 0), select);
        }
        else if (action == "create") {
            // the character screen's New Character (not a cycle character)
            void* select = SelectScreen();
            if (!select) throw std::runtime_error("no character screen");
            bool cycle = false; void* a[]{&cycle};
            Invoke(Method(select, "NewCharacterClick", 1), select, a);
        }
        else if (action == "rogue") {
            // choose the Rogue on the creation screen, as a click on it does
            void* select = SelectScreen();
            bool selected = false;
            void* rogue = CreationRogue(&selected);
            if (!select || !rogue) throw std::runtime_error("no creation screen Rogue");
            Invoke(Method(rogue, "Select", 0), rogue);  // the model's own select, as a click on it
        }
        else if (action == "voicelines") return VoiceLines();
        else if (action.rfind("fmod:", 0) == 0) return FmodList(action.substr(5));
        else if (action == "heard" || action == "heard:clear") return HeardList(action == "heard:clear");
        else if (action.rfind("say:", 0) == 0) return Say(std::stoi(action.substr(4)));
        else if (action == "forget") {
            // the character in play has no choice of its own again
            choices.characters.erase(IdOf(PlayerData())); SaveChoices(); ticks = 0;
        }
#endif
        else if (action != "read") throw std::runtime_error("use bodyswap on|off|read");
        std::ostringstream out; out.setf(std::ios::fixed); out.precision(3);
        void* select = SelectScreen();
        bool creationRogue = false;
        if (select) CreationRogue(&creationRogue);
        out << "{\"ok\":true,\"default\":" << Boolean(choices.fallback) << ",\"characters\":{";
        bool firstChoice = true;
        for (const auto& [id, value] : choices.characters) { out << (firstChoice ? "" : ",") << Json(id) << ':' << Boolean(value); firstChoice = false; }
        out << "},\"current\":" << Json(select ? SelectedId(select) : IdOf(PlayerData())) << ",\"currentWanted\":" << Boolean(currentWanted.load())
            << ",\"screen\":" << (select ? Get<int>(select, Offset("CharacterSelect", "currentState")) : -1) << ",\"creationRogue\":" << Boolean(creationRogue)
            << ",\"voice\":" << VoiceRead();
#ifdef EPOCHPACT_TESTING
        // the creation screen's models: class, context (0 new), position (2 selected)
        out << ",\"models\":[";
        if (select) {
            Root type(TypeObject("LE.dll", "", "CharacterCreationSelection"));
            bool inactive = false; void* fa[]{type.Get(), &inactive};
            Root found(Invoke(Signature("Object", "FindObjectsOfType", {"System.Type", "System.Boolean"}), nullptr, fa));
            bool f = true;
            for (void* c : ArrayItems(found.Get())) {
                if (!c) continue;
                out << (f ? "" : ",") << '[' << static_cast<int>(Get<uint8_t>(c, Offset("CharacterCreationSelection", "characterClassID"))) << ','
                    << Get<int>(c, Offset("CharacterCreationSelection", "characterContext")) << ',' << Get<int>(c, Offset("CharacterCreationSelection", "positionState")) << ']';
                f = false;
            }
        }
        out << "],\"bodies\":[";
        {
            // the body meshes on screen and the creation model each belongs to (-1 none, 0 new, 1 existing)
            Root type(TypeObject("UnityEngine.CoreModule.dll", "UnityEngine", "SkinnedMeshRenderer"));
            bool inactive = false; void* fa[]{type.Get(), &inactive};
            Root found(Invoke(Signature("Object", "FindObjectsOfType", {"System.Type", "System.Boolean"}), nullptr, fa));
            bool f = true;
            for (void* r : ArrayItems(found.Get())) {
                if (!r) continue;
                const std::string n = NameOf(r);
                if (n.find("Body_GEO") == std::string::npos && n.find("Head_GEO") == std::string::npos) continue;
                void* owner = select ? ModelOwner(r) : nullptr;
                out << (f ? "" : ",") << '[' << Json(n) << ',' << (owner ? Get<int>(owner, Offset("CharacterCreationSelection", "characterContext")) : -1) << ']';
                f = false;
            }
        }
        out << ']';
#endif
        out << ",\"on\":" << Boolean(on.load()) << ",\"hooked\":" << Boolean(hooked) << ",\"phase\":" << Json(phase)
            << ",\"loaded\":" << Boolean(prefab && prefab->Get()) << ",\"loadStatus\":" << loadStatus << ",\"frames\":" << frames
            << ",\"scans\":" << scans << ",\"ms\":" << lastMs << ",\"faults\":" << faults << ",\"error\":" << Json(lastError) << ",\"puppets\":[";
        bool first = true;
        for (auto& p : puppets) {
            out << (first ? "" : ",") << "{\"bones\":" << p->bones.size() << ",\"restCheck\":" << p->restCheck << ",\"hidden\":" << p->hiddenCount << ",\"scale\":" << p->scale
                << ",\"hips\":" << Boolean(p->zerrickHips != nullptr) << ",\"weapons\":[" << Boolean(p->rogueWeapon[0] != nullptr) << ',' << Boolean(p->rogueWeapon[1] != nullptr)
                << "],\"rogue\":" << Json(Alive(p->rogueModel) ? NameOf(p->rogueModel->Get()) : "") << ",\"zerrickRenderers\":[";
            for (size_t i = 0; i < p->zerrickRenderers.size() && i < 20; ++i) out << (i ? "," : "") << Json(p->zerrickRenderers[i]);
            out << "]}";
            first = false;
        }
        out << "]}";
        return out.str();
    });
}
void ReleaseRoots() {
    on = false;
    VoiceRelease();
    puppets.clear();
    prefab.reset();
    loadRef.reset();
}
}  // namespace ep::bodyswap
