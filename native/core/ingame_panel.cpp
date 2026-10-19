// In-game panel (experiment). See ingame_panel.hpp.
//
// The panel lives on a canvas of its own (screen-space overlay, kept across scene loads), so
// it never changes the game's own windows. The hotkey and the mouse are read with
// GetAsyncKeyState only while the game window is in front; no input is sent to the game.
// Buttons do not call game code themselves: each sends an ordinary command line through
// commands::SubmitUi, which the IPC worker runs exactly like a command from the app.
#include "ingame_panel.hpp"
#include "ingame_rules.hpp"
#include "commands.hpp"
#include "lifecycle.hpp"
#include "managed.hpp"
#include <windows.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace ep::ingame {
namespace {
using namespace managed;
namespace r = rules;

struct V2 { float x, y; };
struct V3 { float x, y, z; };
struct Color { float r, g, b, a; };
constexpr Color gold{.84f, .74f, .49f, 1}, ink{.035f, .075f, .055f, .96f}, white{.94f, .92f, .84f, 1},
                muted{.72f, .76f, .71f, 1}, buttonColor{.13f, .2f, .16f, 1}, onColor{.25f, .45f, .3f, 1};
constexpr int kTextLeft = 513, kTextCenter = 514;  // TMPro.TextAlignmentOptions MidlineLeft / Center

std::atomic<int> hotkey{VK_F6};
std::atomic<bool> toggleRequested{false};
std::atomic<uint64_t> toggles{0}, clicks{0}, actions{0}, refreshes{0}, faults{0};

// Shared with the IPC worker's completion callbacks (no Unity objects behind this lock).
std::mutex stateLock;
r::Values values;
std::string message = "Changes apply at once. Offline characters only.", lastError;
std::atomic<bool> refreshInFlight{false};
std::atomic<uint64_t> nextRefresh{0};

// Main thread only below.
struct Widget { game::Handle go, rect, graphic, label; };
struct Button { Widget box; r::Action action; std::string shown; };
struct Ui {
    game::Handle canvasGo, font;
    Widget frame, inner, title, hint, message;
    std::array<Widget, 3> names, values;
    std::vector<Button> buttons;
    bool built = false, shown = false, mouseDown = false;
    std::string titleText, hintText, messageText;
    std::array<std::string, 3> valueText;
    std::string pending;  // a pressed button's command waiting for the IPC worker
} ui;
std::map<std::string, std::unique_ptr<Root>> typeRoots;

const il2cpp::Method* M(const char* image, const char* ns, const char* cls, const char* name, int count) {
    static std::map<std::string, const il2cpp::Method*> cache;
    const std::string key = std::string(image) + ":" + ns + ":" + cls + ":" + name + ":" + std::to_string(count);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    auto m = game::FindMethod(image, ns, cls, name, count);
    if (!m) throw std::runtime_error("In-game panel method unavailable: " + key);
    return cache.emplace(key, m.info).first->second;
}
const il2cpp::Method* U(const char* cls, const char* name, int n) { return M("UnityEngine.CoreModule.dll", "UnityEngine", cls, name, n); }
const il2cpp::Method* T(const char* name, int n) { return M("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", name, n); }
const il2cpp::Method* G(const char* name, int n) { return M("UnityEngine.UI.dll", "UnityEngine.UI", "Graphic", name, n); }
const il2cpp::Method* Signature(const char* image, const char* ns, const char* cls, const char* name, std::initializer_list<const char*> params) {
    static std::map<std::string, const il2cpp::Method*> cache;
    std::string key = std::string(image) + ":" + ns + ":" + cls + ":" + name;
    for (auto p : params) key += std::string(":") + p;
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    auto& a = il2cpp::api();
    auto* c = game::FindClass(image, ns, cls);
    void* it = nullptr;
    if (c) while (auto* m = a.class_get_methods(c, &it)) {
        if (std::string(a.method_get_name(m)) != name || a.method_get_param_count(m) != params.size()) continue;
        bool match = true; unsigned n = 0;
        for (auto p : params) { char* actual = a.type_get_name(a.method_get_param(m, n++)); match &= actual && std::string(actual) == p; if (actual) a.free(actual); }
        if (match) return cache.emplace(key, m).first->second;
    }
    throw std::runtime_error("In-game panel overload unavailable: " + key);
}
void* Type(const char* image, const char* ns, const char* name) {
    const std::string key = std::string(image) + ":" + ns + ":" + name;
    if (auto it = typeRoots.find(key); it != typeRoots.end()) return it->second->Get();
    auto* c = game::FindClass(image, ns, name);
    if (!c) throw std::runtime_error("In-game panel type unavailable: " + key);
    return typeRoots.emplace(key, std::make_unique<Root>(il2cpp::api().type_get_object(il2cpp::api().class_get_type(c)))).first->second->Get();
}
void* Add(void* go, const char* image, const char* ns, const char* name) {
    void* a[]{Type(image, ns, name)};
    return Invoke(Signature("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject", "AddComponent", {"System.Type"}), go, a);
}
void SetActive(void* go, bool on) { void* a[]{&on}; Invoke(U("GameObject", "SetActive", 1), go, a); }
void Tint(void* graphic, Color c) { void* a[]{&c}; Invoke(G("set_color", 1), graphic, a); }
void Raycast(void* graphic, bool on) { void* a[]{&on}; Invoke(G("set_raycastTarget", 1), graphic, a); }

// A GameObject with a RectTransform, anchored at its parent's top-left corner.
Widget New(const char* name, void* parentTransform) {
    auto& a = il2cpp::api();
    auto* goClass = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
    auto* typeClass = game::FindClass("mscorlib.dll", "System", "Type");
    if (!goClass || !typeClass) throw std::runtime_error("In-game panel constructor types unavailable");
    Root go(a.object_new(goClass)), nameString(a.string_new(name)), types(a.array_new(typeClass, 1));
    int index = 0;
    void* element[]{Type("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform"), &index};
    Invoke(Signature("mscorlib.dll", "System", "Array", "SetValue", {"System.Object", "System.Int32"}), types.Get(), element);
    void* ctor[]{nameString.Get(), types.Get()};
    Invoke(Signature("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject", ".ctor", {"System.String", "System.Type[]"}), go.Get(), ctor);
    Widget w;
    w.go.Set(go.Get());
    w.rect.Set(Invoke(U("GameObject", "get_transform", 0), go.Get()));
    int layer = 5;  // UI
    void* la[]{&layer};
    Invoke(U("GameObject", "set_layer", 1), go.Get(), la);
    if (parentTransform) {
        bool world = false;
        void* p[]{parentTransform, &world};
        Invoke(Signature("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "SetParent", {"UnityEngine.Transform", "System.Boolean"}), w.rect.Get(), p);
    }
    V2 corner{0, 1};
    V3 unit{1, 1, 1};
    void* ca[]{&corner}, *sa[]{&unit};
    for (const char* field : {"set_anchorMin", "set_anchorMax", "set_pivot"}) Invoke(U("RectTransform", field, 1), w.rect.Get(), ca);
    Invoke(U("Transform", "set_localScale", 1), w.rect.Get(), sa);
    return w;
}
void Place(Widget& w, float x, float y, float width, float height) {
    V2 pos{x, -y}, size{width, height};
    void* a[]{&pos}, *b[]{&size};
    Invoke(U("RectTransform", "set_anchoredPosition", 1), w.rect.Get(), a);
    Invoke(U("RectTransform", "set_sizeDelta", 1), w.rect.Get(), b);
}
Widget Box(const char* name, void* parent, Color color, bool raycast, float x, float y, float width, float height) {
    Widget w = New(name, parent);
    w.graphic.Set(Add(w.go.Get(), "UnityEngine.UI.dll", "UnityEngine.UI", "Image"));
    Tint(w.graphic.Get(), color);
    Raycast(w.graphic.Get(), raycast);
    Place(w, x, y, width, height);
    return w;
}
void* Text(void* parent, const char* name, float x, float y, float width, float height, float size, int alignment, Color color) {
    Widget w = New(name, parent);
    void* text = Add(w.go.Get(), "Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI");
    Raycast(text, false);
    void* fa[]{ui.font.Get()}; Invoke(T("set_font", 1), text, fa);
    bool rich = false; void* ra[]{&rich}; Invoke(T("set_richText", 1), text, ra);
    void* sa[]{&size}; Invoke(T("set_fontSize", 1), text, sa);
    void* aa[]{&alignment}; Invoke(T("set_alignment", 1), text, aa);
    Tint(text, color);
    Place(w, x, y, width, height);
    return text;
}
void SetText(void* text, const std::string& value, std::string& previous) {
    if (!text || value == previous) return;
    Root s(il2cpp::api().string_new(value.c_str()));
    void* a[]{s.Get()};
    Invoke(T("set_text", 1), text, a);
    previous = value;
}
void* GameFont() {
    if (void* font = Invoke(M("Unity.TextMeshPro.dll", "TMPro", "TMP_Settings", "get_defaultFontAsset", 0))) return font;
    bool inactive = false;
    void* args[]{Type("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI"), &inactive};
    Root found(Invoke(Signature("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", {"System.Type", "System.Boolean"}), nullptr, args));
    const auto n = Get<uintptr_t>(found.Get(), 0x18);
    for (uintptr_t i = 0; i < n && i < 256; ++i)
        if (void* text = Get<void*>(found.Get(), 0x20 + 8 * i))
            if (void* font = Invoke(T("get_font", 0), text)) return font;
    throw std::runtime_error("No game font for the in-game panel");
}

void Build() {
    ui = Ui{};
    ui.font.Set(GameFont());
    Widget root = New("EpochPact In-game Panel", nullptr);
    ui.canvasGo.Set(root.go.Get());
    void* canvas = Add(root.go.Get(), "UnityEngine.UIModule.dll", "UnityEngine", "Canvas");
    int overlay = 0, order = 30000;
    void* ma[]{&overlay}, *oa[]{&order};
    Invoke(M("UnityEngine.UIModule.dll", "UnityEngine", "Canvas", "set_renderMode", 1), canvas, ma);
    Invoke(M("UnityEngine.UIModule.dll", "UnityEngine", "Canvas", "set_sortingOrder", 1), canvas, oa);
    void* scaler = Add(root.go.Get(), "UnityEngine.UI.dll", "UnityEngine.UI", "CanvasScaler");
    int scaleWithScreen = 1; V2 reference{1920, 1080}; float match = 0.5f;
    void* s1[]{&scaleWithScreen}, *s2[]{&reference}, *s3[]{&match};
    Invoke(M("UnityEngine.UI.dll", "UnityEngine.UI", "CanvasScaler", "set_uiScaleMode", 1), scaler, s1);
    Invoke(M("UnityEngine.UI.dll", "UnityEngine.UI", "CanvasScaler", "set_referenceResolution", 1), scaler, s2);
    Invoke(M("UnityEngine.UI.dll", "UnityEngine.UI", "CanvasScaler", "set_matchWidthOrHeight", 1), scaler, s3);
    // A raycaster makes the game's event system see the panel, as it sees its own windows.
    Add(root.go.Get(), "UnityEngine.UI.dll", "UnityEngine.UI", "GraphicRaycaster");
    void* keep[]{root.go.Get()};
    Invoke(U("Object", "DontDestroyOnLoad", 1), nullptr, keep);

    void* canvasRect = root.rect.Get();
    ui.frame = Box("Frame", canvasRect, gold, true, 40, 170, 384, 244);
    ui.inner = Box("Background", ui.frame.rect.Get(), ink, true, 2, 2, 380, 240);
    void* panel = ui.inner.rect.Get();
    ui.title.label.Set(Text(panel, "Title", 16, 10, 220, 30, 22, kTextLeft, gold));
    ui.hint.label.Set(Text(panel, "Hotkey", 236, 10, 128, 30, 15, kTextLeft, muted));
    const char* names[3]{"XP multiplier", "Monster density", "Auto pickup"};
    for (int row = 0; row < 3; ++row) {
        const float y = 52.f + 48.f * static_cast<float>(row);
        std::string written;
        ui.names[row].label.Set(Text(panel, "Name", 16, y, 170, 36, 17, kTextLeft, white));
        SetText(ui.names[row].label.Get(), names[row], written);
        if (row < 2) {
            const auto down = row == 0 ? r::Action::XpDown : r::Action::DensityDown, up = row == 0 ? r::Action::XpUp : r::Action::DensityUp;
            std::string caption;
            Button minus{Box("Minus", panel, buttonColor, true, 190, y, 40, 36), down, {}};
            // Plain ASCII: the game's font has no U+2212 minus sign (it drew an empty box).
            SetText(Text(minus.box.rect.Get(), "Label", 0, 0, 40, 36, 22, kTextCenter, white), "-", caption);
            ui.values[row].label.Set(Text(panel, "Value", 234, y, 84, 36, 19, kTextCenter, gold));
            Button plus{Box("Plus", panel, buttonColor, true, 322, y, 40, 36), up, {}};
            caption.clear();
            SetText(Text(plus.box.rect.Get(), "Label", 0, 0, 40, 36, 22, kTextCenter, white), "+", caption);
            ui.buttons.push_back(std::move(minus));
            ui.buttons.push_back(std::move(plus));
        } else {
            Button toggle{Box("Toggle", panel, buttonColor, true, 190, y, 172, 36), r::Action::AutoPickup, {}};
            ui.values[row].label.Set(Text(toggle.box.rect.Get(), "Value", 0, 0, 172, 36, 18, kTextCenter, white));
            ui.buttons.push_back(std::move(toggle));
        }
    }
    ui.message.label.Set(Text(panel, "Message", 16, 196, 348, 36, 13, kTextLeft, muted));
    ui.built = true;
    SetActive(root.go.Get(), false);
    Log("in-game panel built (%zu buttons)", ui.buttons.size());
}

bool GameInFront(HWND* window = nullptr) {
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (!w) return false;
    GetWindowThreadProcessId(w, &pid);
    if (window) *window = w;
    return pid == GetCurrentProcessId();
}
// The cursor in Unity's screen coordinates (origin bottom-left), or false.
bool Cursor(V2* out) {
    HWND window = nullptr;
    if (!GameInFront(&window)) return false;
    POINT p{};
    RECT client{};
    if (!GetCursorPos(&p) || !ScreenToClient(window, &p) || !GetClientRect(window, &client)) return false;
    const int width = client.right - client.left, height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || p.x < 0 || p.y < 0 || p.x >= width || p.y >= height) return false;
    const int sw = Value<int>(U("Screen", "get_width", 0)), sh = Value<int>(U("Screen", "get_height", 0));
    out->x = static_cast<float>(p.x) * sw / width;
    out->y = static_cast<float>(height - p.y) * sh / height;
    return true;
}
bool Contains(void* rect, V2 point) {
    void* a[]{rect, &point, nullptr};
    return Value<bool>(Signature("UnityEngine.UIModule.dll", "UnityEngine", "RectTransformUtility", "RectangleContainsScreenPoint",
                                 {"UnityEngine.RectTransform", "UnityEngine.Vector2", "UnityEngine.Camera"}), nullptr, a);
}
// Screen rectangle of a widget (overlay canvas: world corners are screen pixels).
std::array<float, 4> ScreenRect(void* rect) {
    Root corners(il2cpp::api().array_new(game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3"), 4));
    void* a[]{corners.Get()};
    Invoke(U("RectTransform", "GetWorldCorners", 1), rect, a);
    const auto* v = reinterpret_cast<const V3*>(reinterpret_cast<const char*>(corners.Get()) + 0x20);
    return {v[0].x, v[0].y, v[2].x - v[0].x, v[2].y - v[0].y};
}

void RequestStatus() {
    if (refreshInFlight.exchange(true)) return;
    if (!commands::SubmitUi("status", [](const std::string& reply) {
            {
                std::lock_guard<std::mutex> hold(stateLock);
                values = r::Parse(reply);
            }
            ++refreshes;
            refreshInFlight = false;
        })) refreshInFlight = false;
}
void Submit() {
    if (ui.pending.empty()) return;
    const std::string line = ui.pending;
    if (commands::SubmitUi(line, [line](const std::string& reply) {
            {
                std::lock_guard<std::mutex> hold(stateLock);
                message = "> " + line + ": " + r::Brief(reply);
            }
            ++actions;
            nextRefresh = 0;  // show the new value at once
        })) ui.pending.clear();
}
void Show(bool on) {
    void* go = ui.canvasGo.Get();
    if (!go) { ui.built = false; return; }
    SetActive(go, on);
    ui.shown = on;
    if (on) nextRefresh = 0;
}
void Render() {
    r::Values v;
    std::string text;
    {
        std::lock_guard<std::mutex> hold(stateLock);
        v = values;
        text = message;
    }
    SetText(ui.title.label.Get(), "EpochPact", ui.titleText);
    const int vk = hotkey.load();
    SetText(ui.hint.label.Get(), vk ? "F" + std::to_string(vk - VK_F1 + 1) + " to close" : "", ui.hintText);
    SetText(ui.values[0].label.Get(), r::Shown(v.xp), ui.valueText[0]);
    SetText(ui.values[1].label.Get(), r::Shown(v.density), ui.valueText[1]);
    SetText(ui.values[2].label.Get(), v.autopickup < 0 ? "..." : v.autopickup ? "On" : "Off", ui.valueText[2]);
    for (auto& b : ui.buttons)
        if (b.action == r::Action::AutoPickup) {
            const std::string state = v.autopickup == 1 ? "on" : "off";
            if (state != b.shown) { Tint(b.box.graphic.Get(), v.autopickup == 1 ? onColor : buttonColor); b.shown = state; }
        }
    if (!v.offline && v.xp >= 0) text = "Offline characters only: load an offline character.";
    SetText(ui.message.label.Get(), text, ui.messageText);
}
void Click() {
    // Physical left button (GetAsyncKeyState reads physical buttons; follow a swap).
    const int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool pressed = down && !ui.mouseDown;
    ui.mouseDown = down;
    if (!pressed) return;
    V2 point{};
    if (!Cursor(&point)) return;
    for (auto& b : ui.buttons) {
        if (!Contains(b.box.rect.Get(), point)) continue;
        ++clicks;
        r::Values v;
        { std::lock_guard<std::mutex> hold(stateLock); v = values; }
        ui.pending = r::Command(b.action, v);
        return;
    }
}
void Step() {
    if (toggleRequested.exchange(false)) {
        ++toggles;
        if (!ui.built || !ui.canvasGo.Get()) Build();
        Show(!ui.shown);
    }
    if (!ui.built || !ui.shown) return;
    if (!ui.canvasGo.Get()) { ui.built = ui.shown = false; return; }
    Click();
    Submit();
    const auto now = GetTickCount64();
    if (now >= nextRefresh.load()) { nextRefresh = now + 1000; RequestStatus(); }
    Render();
}

void Watch() {
    bool down = false;
    while (!lifecycle::Stopping()) {
        Sleep(30);
        const int vk = hotkey.load();
        if (!vk) { down = false; continue; }
        const bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (pressed && !down && GameInFront()) toggleRequested = true;
        down = pressed;
    }
}
std::filesystem::path KeyFile() { return std::filesystem::path(PluginDir()) / L"panel-key.txt"; }
std::string KeyName(int vk) { return vk >= VK_F5 && vk <= VK_F12 ? "F" + std::to_string(vk - VK_F1 + 1) : "off"; }
int KeyCode(const std::string& name) {
    if (name == "off") return 0;
    if (name.size() >= 2 && (name[0] == 'F' || name[0] == 'f')) {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 5 && n <= 12) return VK_F1 + n - 1;
    }
    return -1;
}
std::string State() {
    r::Values v;
    std::string text, error;
    { std::lock_guard<std::mutex> hold(stateLock); v = values; text = message; error = lastError; }
    std::ostringstream out;
    out << "{\"ok\":true,\"key\":" << Json(KeyName(hotkey.load())) << ",\"built\":" << Boolean(ui.built) << ",\"shown\":" << Boolean(ui.shown)
        << ",\"xp\":" << v.xp << ",\"density\":" << v.density << ",\"autopickup\":" << v.autopickup << ",\"offline\":" << Boolean(v.offline)
        << ",\"toggles\":" << toggles.load() << ",\"clicks\":" << clicks.load() << ",\"actions\":" << actions.load()
        << ",\"refreshes\":" << refreshes.load() << ",\"faults\":" << faults.load() << ",\"message\":" << Json(text) << ",\"error\":" << Json(error)
        << ",\"buttons\":[";
    bool comma = false;
    if (ui.built && ui.shown && ui.canvasGo.Get())
        for (auto& b : ui.buttons) {
            const auto s = ScreenRect(b.box.rect.Get());
            const char* name = b.action == r::Action::XpDown ? "xpdown" : b.action == r::Action::XpUp ? "xpup" : b.action == r::Action::DensityDown ? "densitydown"
                             : b.action == r::Action::DensityUp ? "densityup" : "autopickup";
            if (comma) out << ',';
            comma = true;
            out << "{\"action\":" << Json(name) << ",\"x\":" << s[0] << ",\"y\":" << s[1] << ",\"width\":" << s[2] << ",\"height\":" << s[3] << '}';
        }
    out << "],\"screen\":[" << Value<int>(U("Screen", "get_width", 0)) << ',' << Value<int>(U("Screen", "get_height", 0)) << "]}";
    return out.str();
}
}  // namespace

bool Init() {
    std::ifstream in(KeyFile());
    std::string saved;
    if (in >> saved) { const int code = KeyCode(saved); if (code >= 0) hotkey = code; }
    std::thread(Watch).detach();
    Log("in-game panel: hotkey %s", KeyName(hotkey.load()).c_str());
    return true;
}

void Frame() {
    if (lifecycle::Stopping() || (!ui.shown && !toggleRequested.load(std::memory_order_relaxed))) return;
    // Our own errors are caught inside; Guarded then only sees game faults and managed exceptions.
    std::string why, ours;
    const bool safe = game::Guarded([&] {
        try { Step(); }
        catch (const std::exception& e) { ours = e.what(); }
    }, &why);
    if (!safe || !ours.empty()) {
        if (!ours.empty()) why = ours;
        ++faults;
        { std::lock_guard<std::mutex> hold(stateLock); lastError = why; }
        ui.shown = false;
        if (void* go = ui.canvasGo.Get()) game::Guarded([go] { SetActive(go, false); }, nullptr);
        Log("in-game panel closed after a fault: %s", why.c_str());
    }
}

std::string Panel(const std::string& action) {
    if (action == "read") return Run([] { return State(); });
    if (action != "0" && action != "1") return "{\"ok\":false,\"error\":\"use panel read|0|1\"}";
    const bool open = action == "1";
    return Run([open] {
        if (open && (!ui.built || !ui.canvasGo.Get())) Build();
        if (ui.built) Show(open);
        return State();
    });
}

std::string SetKey(const std::string& key) {
    const int code = KeyCode(key);
    if (code < 0) return "{\"ok\":false,\"error\":\"use panelkey F5-F12|off\"}";
    hotkey = code;
    std::error_code ignored;
    std::filesystem::create_directories(KeyFile().parent_path(), ignored);
    std::ofstream(KeyFile(), std::ios::binary | std::ios::trunc) << KeyName(code) << "\n";
    return Run([] { return State(); });
}

// Runtime shutdown has already stopped frames. Do not call Unity Destroy here.
void ReleaseRoots() { ui = Ui{}; typeRoots.clear(); }

}  // namespace ep::ingame
