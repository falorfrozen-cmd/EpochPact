// The character mirror of the Wardrobe (F6 Wardrobe page): a camera of EpochPact's own renders the
// loaded character into a texture the section shows. The camera stays disabled; the section's tick asks for one
// Render per frame while its page is on screen, so nothing is drawn otherwise. Only the layers of the character's
// own meshes are drawn, on a dark background, from the front of the character (turned by the section's yaw).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace ep::ingame::view {
namespace {
using managed::Get;
using managed::Invoke;
using managed::Root;
using managed::Value;

constexpr int kTextureW = 560, kTextureH = 700;
constexpr float kFov = 24;

struct Vec { float x, y, z; };
struct Bounds { Vec center, extents; };

Pin texture, camera, cameraObject;
// The character the framing was measured for, and when (frames): meshes change when items change.
void* framedActor = nullptr;
int framedAge = 0;
Bounds framed{};
int layerMask = 0;

const il2cpp::Method* C(const char* name, int n) { return U("Camera", name, n); }

void* Texture() {
    if (texture && game::IsAlive(texture.Get())) return texture.Get();
    auto& a = il2cpp::api();
    Root rt(a.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "RenderTexture")));
    int w = kTextureW, h = kTextureH, depth = 24;
    void* args[]{&w, &h, &depth};
    Invoke(Sig("UnityEngine.CoreModule.dll", "UnityEngine", "RenderTexture", ".ctor", {"System.Int32", "System.Int32", "System.Int32"}), rt.Get(), args);
    int aa = 4;
    void* aaArgs[]{&aa};
    Invoke(U("RenderTexture", "set_antiAliasing", 1), rt.Get(), aaArgs);
    Invoke(U("RenderTexture", "Create", 0), rt.Get());
    texture.Set(rt.Get());
    return texture.Get();
}

void* Camera() {
    if (camera && game::IsAlive(camera.Get())) return camera.Get();
    auto& a = il2cpp::api();
    Root go(a.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject")));
    Root name(a.string_new("EpochPact mirror camera"));
    void* ctor[]{name.Get()};
    Invoke(Sig("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject", ".ctor", {"System.String"}), go.Get(), ctor);
    void* cam = Add(go.Get(), "UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
    Enable(cam, false);  // rendered on request only
    void* t[]{Texture()};
    Invoke(C("set_targetTexture", 1), cam, t);
    int clear = 2;  // CameraClearFlags.SolidColor
    void* ca[]{&clear};
    Invoke(C("set_clearFlags", 1), cam, ca);
    Color back = Hex("#0b1312");
    void* ba[]{&back};
    Invoke(C("set_backgroundColor", 1), cam, ba);
    float fov = kFov, nearPlane = .05f, farPlane = 80;
    void* fa[]{&fov};
    Invoke(C("set_fieldOfView", 1), cam, fa);
    void* na[]{&nearPlane};
    Invoke(C("set_nearClipPlane", 1), cam, na);
    void* xa[]{&farPlane};
    Invoke(C("set_farClipPlane", 1), cam, xa);
    bool off = false;
    void* oa[]{&off};
    Invoke(C("set_useOcclusionCulling", 1), cam, oa);
    Invoke(C("set_allowHDR", 1), cam, oa);
    cameraObject.Set(go.Get());
    camera.Set(cam);
    return cam;
}

Vec Position(void* transform) { return Value<Vec>(U("Transform", "get_position", 0), transform); }

// The character's meshes (skinned and plain; no particles): their layers, and their bounds (kept for the framing's
// width; a mesh's bounds can be far larger than what it shows, so the height comes from the character's scale).
bool Measure(void* actor) {
    Bounds all{};
    bool any = false;
    int mask = 0;
    for (const char* kind : {"SkinnedMeshRenderer", "MeshRenderer"}) {
        bool inactive = false;
        void* a[]{Type("UnityEngine.CoreModule.dll", "UnityEngine", kind), &inactive};
        Root found(Invoke(Sig("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject", "GetComponentsInChildren", {"System.Type", "System.Boolean"}), GameObjectOf(actor), a));
        const auto n = Get<uintptr_t>(found.Get(), 0x18);
        for (uintptr_t i = 0; i < n && i < 512; ++i) {
            void* r = Get<void*>(found.Get(), 0x20 + i * sizeof(void*));
            if (!r || !Value<bool>(U("Renderer", "get_enabled", 0), r)) continue;
            void* go = GameObjectOf(r);
            const int layer = Value<int>(U("GameObject", "get_layer", 0), go);
            if (layer == 5) continue;  // UI
            const Bounds b = Value<Bounds>(U("Renderer", "get_bounds", 0), r);
            if (!(b.extents.y > 0) || b.extents.y > 6) continue;
            mask |= 1 << layer;
            if (!any) { all = b; any = true; continue; }
            const float lox = std::min(all.center.x - all.extents.x, b.center.x - b.extents.x), hix = std::max(all.center.x + all.extents.x, b.center.x + b.extents.x);
            const float loy = std::min(all.center.y - all.extents.y, b.center.y - b.extents.y), hiy = std::max(all.center.y + all.extents.y, b.center.y + b.extents.y);
            const float loz = std::min(all.center.z - all.extents.z, b.center.z - b.extents.z), hiz = std::max(all.center.z + all.extents.z, b.center.z + b.extents.z);
            all = {{(lox + hix) / 2, (loy + hiy) / 2, (loz + hiz) / 2}, {(hix - lox) / 2, (hiy - loy) / 2, (hiz - loz) / 2}};
        }
    }
    if (!any || !mask) return false;
    void* transform = TransformOf(actor);
    const Vec feet = Position(transform);
    // relative to the character's feet, so a moving character keeps its framing between measures
    framed = {{all.center.x - feet.x, all.center.y - feet.y, all.center.z - feet.z}, all.extents};
    layerMask = mask;
    return true;
}
}  // namespace

void* MirrorTexture() { return Texture(); }

bool MirrorRender(float yaw, bool closeUp) {
    void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0).info);
    if (!actor || !game::IsAlive(actor)) return false;
    if (actor != framedActor || ++framedAge > 30) {
        if (!Measure(actor)) return false;
        framedActor = actor;
        framedAge = 0;
    }
    void* cam = Camera();
    void* actorTransform = TransformOf(actor);
    const Vec feet = Position(actorTransform);
    const Vec forward = Value<Vec>(U("Transform", "get_forward", 0), actorTransform);
    const float turn = yaw * 3.14159265f / 180, fx = forward.x * std::cos(turn) + forward.z * std::sin(turn),
                fz = -forward.x * std::sin(turn) + forward.z * std::cos(turn), len = std::max(1e-3f, std::sqrt(fx * fx + fz * fz));
    // a character is about 2 units tall at scale 1: framed from the feet up, with a little room above the head
    const Vec scale = Value<Vec>(U("Transform", "get_lossyScale", 0), actorTransform);
    const float height = std::clamp(2.25f * scale.y, .8f, 6.f);
    // close-up: the head and chest, where most looks show
    const Vec target{feet.x, feet.y + height * (closeUp ? .74f : .47f), feet.z};
    const float distance = height * (closeUp ? .3f : .64f) / std::tan(kFov * 3.14159265f / 360);
    Vec at{target.x + fx / len * distance, target.y + height * (closeUp ? .02f : .05f), target.z + fz / len * distance};
    // only what stands within reach of the character: nothing behind it (other people in town) or in front
    float nearPlane = std::max(.05f, distance - 2.5f * scale.y), farPlane = distance + 2.5f * scale.y;
    void* na[]{&nearPlane};
    Invoke(C("set_nearClipPlane", 1), cam, na);
    void* fa[]{&farPlane};
    Invoke(C("set_farClipPlane", 1), cam, fa);
    void* camTransform = TransformOf(cam);
    void* pa[]{&at};
    Invoke(U("Transform", "set_position", 1), camTransform, pa);
    Vec look = target;
    void* la[]{&look};
    Invoke(Sig("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "LookAt", {"UnityEngine.Vector3"}), camTransform, la);
    int mask = layerMask;
    void* ma[]{&mask};
    Invoke(C("set_cullingMask", 1), cam, ma);
    Invoke(C("Render", 0), cam);
    return true;
}

std::string Base64(const unsigned char* data, size_t n) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const unsigned v = data[i] << 16 | (i + 1 < n ? data[i + 1] << 8 : 0) | (i + 2 < n ? data[i + 2] : 0);
        out += abc[v >> 18 & 63];
        out += abc[v >> 12 & 63];
        out += i + 1 < n ? abc[v >> 6 & 63] : '=';
        out += i + 2 < n ? abc[v & 63] : '=';
    }
    return out;
}

// One frame of the mirror read back into a JPEG, for the desktop app (main thread).
std::string MirrorShot(float yaw) {
    return managed::Run([=]() -> std::string {
        if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
        if (!MirrorRender(yaw)) throw std::runtime_error("Load an offline character first.");
        void* rt = Texture();
        void* previous = Invoke(U("RenderTexture", "get_active", 0));
        void* sa[]{rt};
        Invoke(U("RenderTexture", "set_active", 1), nullptr, sa);
        struct Rect { float x, y, w, h; } area{0, 0, static_cast<float>(kTextureW), static_cast<float>(kTextureH)};
        auto& a = il2cpp::api();
        Root tex(a.object_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Texture2D")));
        std::string out;
        try {
            int w = kTextureW, h = kTextureH, format = 3;  // TextureFormat.RGB24
            bool mips = false;
            void* ctor[]{&w, &h, &format, &mips};
            Invoke(Sig("UnityEngine.CoreModule.dll", "UnityEngine", "Texture2D", ".ctor", {"System.Int32", "System.Int32", "UnityEngine.TextureFormat", "System.Boolean"}), tex.Get(), ctor);
            int zero = 0;
            void* ra[]{&area, &zero, &zero};
            Invoke(Sig("UnityEngine.CoreModule.dll", "UnityEngine", "Texture2D", "ReadPixels", {"UnityEngine.Rect", "System.Int32", "System.Int32"}), tex.Get(), ra);
            Invoke(U("Texture2D", "Apply", 0), tex.Get());
            int quality = 88;
            void* ea[]{tex.Get(), &quality};
            Root bytes(Invoke(game::FindMethod("UnityEngine.ImageConversionModule.dll", "UnityEngine", "ImageConversion", "EncodeToJPG", 2).info, nullptr, ea));
            const auto n = Get<uintptr_t>(bytes.Get(), 0x18);
            out = "{\"ok\":true,\"width\":" + std::to_string(kTextureW) + ",\"height\":" + std::to_string(kTextureH) + ",\"jpg\":\"" +
                  Base64(reinterpret_cast<const unsigned char*>(bytes.Get()) + 0x20, static_cast<size_t>(n)) + "\"}";
        } catch (...) {
            void* pa[]{previous};
            Invoke(U("RenderTexture", "set_active", 1), nullptr, pa);
            throw;
        }
        void* pa[]{previous};
        Invoke(U("RenderTexture", "set_active", 1), nullptr, pa);
        void* da[]{tex.Get()};
        Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "Destroy", 1).info, nullptr, da);
        return out;
    });
}

void ReleaseMirror(bool destroy) {
    if (destroy && cameraObject && game::IsAlive(cameraObject.Get())) {
        void* a[]{cameraObject.Get()};
        try { Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "Destroy", 1).info, nullptr, a); } catch (...) {}
    }
    camera.Reset();
    cameraObject.Reset();
    texture.Reset();
    framedActor = nullptr;
}

}  // namespace ep::ingame::view
