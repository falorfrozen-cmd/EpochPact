// See ingame_assets.hpp.
#include "ingame_assets.hpp"
#include "common.hpp"
#include "managed.hpp"
#include <windows.h>
#include <wincodec.h>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace ep::ingame::assets {
namespace {
using namespace managed;

struct Pixels { int width = 0, height = 0; std::vector<uint8_t> rgba; };  // rows bottom-up, as Unity reads them

std::atomic<int> state{0};  // 0 not started, 1 working, 2 ready, 3 failed
json::Value data;
std::mutex pixelsLock;
std::map<std::string, Pixels> pixels;   // decoded, waiting for the main thread
std::map<std::string, game::Handle> sprites;
game::Handle fonts[6];
Metrics metrics[6];
bool metricsKnown[6] = {};

HMODULE Self() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Self), &module);
    return module;
}
// RCDATA EP_<FILE NAME> (build_ingame_assets.resource_name).
std::string_view Raw(const std::string& file) {
    std::wstring name = L"EP_";
    for (char c : file) name += static_cast<wchar_t>(isalnum(static_cast<unsigned char>(c)) ? toupper(static_cast<unsigned char>(c)) : '_');
    const HMODULE module = Self();
    HRSRC found = FindResourceW(module, name.c_str(), RT_RCDATA);
    if (!found) return {};
    HGLOBAL loaded = LoadResource(module, found);
    const DWORD size = SizeofResource(module, found);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    return bytes ? std::string_view(static_cast<const char*>(bytes), size) : std::string_view();
}

template <class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};
bool Decode(IWICImagingFactory* factory, std::string_view bytes, Pixels& out) {
    Com<IWICStream> stream;
    Com<IWICBitmapDecoder> decoder;
    Com<IWICBitmapFrameDecode> frame;
    Com<IWICFormatConverter> converter;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())), static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return false;
    UINT w = 0, h = 0;
    if (FAILED(converter->GetSize(&w, &h)) || !w || !h || w > 8192 || h > 8192) return false;
    std::vector<uint8_t> topDown(static_cast<size_t>(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(topDown.size()), topDown.data()))) return false;
    out.width = static_cast<int>(w);
    out.height = static_cast<int>(h);
    out.rgba.resize(topDown.size());
    const size_t row = static_cast<size_t>(w) * 4;
    for (UINT y = 0; y < h; ++y) memcpy(out.rgba.data() + (h - 1 - y) * row, topDown.data() + y * row, row);
    return true;
}

std::string FontFile(const std::string& file);

void Prepare() {
    std::string why;
    bool ok = false;
    try {
        const auto text = Raw("ui.json");
        if (text.empty()) throw std::runtime_error("ui.json is not embedded");
        json::Value parsed;
        if (!json::Parse(text, parsed, &why)) throw std::runtime_error("ui.json: " + why);
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Com<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
            throw std::runtime_error("Windows Imaging Component unavailable");
        std::map<std::string, Pixels> decoded;
        for (const auto& file : parsed["files"].members) {
            const auto& name = file.first;
            if (name.size() < 4 || (name.compare(name.size() - 4, 4, ".png") && name.compare(name.size() - 4, 4, ".jpg"))) continue;
            Pixels p;
            if (!Decode(factory.p, Raw(name), p)) throw std::runtime_error("cannot decode " + name);
            decoded.emplace(name, std::move(p));
        }
        factory.p->Release();  // before CoUninitialize
        factory.p = nullptr;
        if (SUCCEEDED(com)) CoUninitialize();
        {
            std::lock_guard<std::mutex> hold(pixelsLock);
            pixels = std::move(decoded);
        }
        data = std::move(parsed);
        // The Inter files are written now, at game start, long before a character loads: a file
        // just written can be held by a virus scanner for a moment, and TMP then cannot load it.
        for (const char* font : {"inter-regular.ttf", "inter-semibold.ttf"}) {
            try { FontFile(font); }
            catch (const std::exception& e) { Log("in-game window: font file: %s", e.what()); }
        }
        ok = true;
        Log("in-game window: data and %zu images prepared", pixels.size());
    } catch (const std::exception& e) {
        Log("in-game window: preparation failed: %s", e.what());
    }
    state = ok ? 2 : 3;
}

// ---- Unity objects (main thread) -----------------------------------------------------------
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
    throw std::runtime_error("In-game window method unavailable: " + key);
}
const il2cpp::Method* Core(const char* cls, const char* name, std::initializer_list<const char*> params) {
    return Signature("UnityEngine.CoreModule.dll", "UnityEngine", cls, name, params);
}
void KeepLoaded(void* object) {
    int dontUnload = 32;  // HideFlags.DontUnloadUnusedAsset
    void* a[]{&dontUnload};
    Invoke(Core("Object", "set_hideFlags", {"UnityEngine.HideFlags"}), object, a);
}
void* NewObject(const char* image, const char* ns, const char* cls) {
    auto* c = game::FindClass(image, ns, cls);
    if (!c) throw std::runtime_error(std::string("In-game window class unavailable: ") + cls);
    return il2cpp::api().object_new(c);
}
void* Texture(const std::string& file) {
    Pixels p;
    {
        std::lock_guard<std::mutex> hold(pixelsLock);
        auto it = pixels.find(file);
        if (it == pixels.end()) throw std::runtime_error("In-game window image missing: " + file);
        p = std::move(it->second);
        pixels.erase(it);  // the CPU copy is not needed once uploaded
    }
    Root texture(NewObject("UnityEngine.CoreModule.dll", "UnityEngine", "Texture2D"));
    int w = p.width, h = p.height, format = 4;  // TextureFormat.RGBA32
    bool mipChain = false;
    void* ctor[]{&w, &h, &format, &mipChain};
    Invoke(Core("Texture2D", ".ctor", {"System.Int32", "System.Int32", "UnityEngine.TextureFormat", "System.Boolean"}), texture.Get(), ctor);
    auto* byteClass = game::FindClass("mscorlib.dll", "System", "Byte");
    Root bytes(il2cpp::api().array_new(byteClass, p.rgba.size()));
    memcpy(static_cast<char*>(bytes.Get()) + 0x20, p.rgba.data(), p.rgba.size());
    void* load[]{bytes.Get()};
    Invoke(Core("Texture2D", "LoadRawTextureData", {"System.Byte[]"}), texture.Get(), load);
    int clamp = 1, bilinear = 1;
    void* wa[]{&clamp}, *fa[]{&bilinear};
    Invoke(Core("Texture", "set_wrapMode", {"UnityEngine.TextureWrapMode"}), texture.Get(), wa);
    Invoke(Core("Texture", "set_filterMode", {"UnityEngine.FilterMode"}), texture.Get(), fa);
    bool mips = false, readOnly = true;
    void* apply[]{&mips, &readOnly};
    Invoke(Core("Texture2D", "Apply", {"System.Boolean", "System.Boolean"}), texture.Get(), apply);
    KeepLoaded(texture.Get());
    return texture.Get();
}

std::wstring Wide(const std::string& s) {
    std::wstring out(MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), static_cast<int>(out.size()));
    return out;
}
// TMP reads a font face from a file path: the embedded Inter is written once to the mod folder.
std::string FontFile(const std::string& file) {
    const auto bytes = Raw(file);
    if (bytes.empty()) throw std::runtime_error("In-game window font missing: " + file);
    const std::string hash = data["files"][file].Str().substr(0, 12);
    const std::filesystem::path dir = std::filesystem::path(PluginDir()) / L"cache" / L"fonts";
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    const auto path = dir / Wide(file.substr(0, file.size() - 4) + "-" + hash + ".ttf");
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path, ignored) != bytes.size()) {
        const auto temp = path.wstring() + L".tmp";
        { std::ofstream(temp, std::ios::binary | std::ios::trunc).write(bytes.data(), static_cast<std::streamsize>(bytes.size())); }
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) throw std::runtime_error("cannot write the interface font");
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8.data(), n, nullptr, nullptr);
    return utf8;
}
void* CreateFromFile(const std::string& path) {
    Root text(il2cpp::api().string_new(path.c_str()));
    int face = 0, sampling = 90, padding = 9, mode = 4165, size = 1024;  // GlyphRenderMode.SDFAA
    void* a[]{text.Get(), &face, &sampling, &padding, &mode, &size, &size};
    return Invoke(Signature("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "CreateFontAsset",
                            {"System.String", "System.Int32", "System.Int32", "System.Int32", "UnityEngine.TextCore.LowLevel.GlyphRenderMode", "System.Int32", "System.Int32"}),
                  nullptr, a);
}
void* CreateFromSystem(const char* family, const char* styleName = "Regular") {
    Root name(il2cpp::api().string_new(family)), style(il2cpp::api().string_new(styleName));
    int size = 90;
    void* a[]{name.Get(), style.Get(), &size};
    return Invoke(Signature("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "CreateFontAsset", {"System.String", "System.String", "System.Int32"}), nullptr, a);
}
// Glyphs the face lacks (symbols such as ◈ ⚙ ♜) come from Segoe UI Symbol, as in the browser.
void AddFallback(void* asset, void* fallback) {
    if (!asset || !fallback || asset == fallback) throw std::runtime_error("font fallback: no font");
    void* list = Invoke(Method(asset, "get_fallbackFontAssetTable", 0), asset);
    if (!list) {
        // A new asset may have no list yet: a new one of the field's own type (List<TMP_FontAsset>).
        const auto* field = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(asset), "m_FallbackFontAssetTable");
        if (!field) throw std::runtime_error("font fallback: TMP_FontAsset.m_FallbackFontAssetTable unavailable");
        void* a[]{il2cpp::api().type_get_object(il2cpp::api().field_get_type(field))};
        Root made(Invoke(Signature("mscorlib.dll", "System", "Activator", "CreateInstance", {"System.Type"}), nullptr, a));
        void* b[]{made.Get()};
        Invoke(Method(asset, "set_fallbackFontAssetTable", 1), asset, b);
        list = Invoke(Method(asset, "get_fallbackFontAssetTable", 0), asset);
        if (!list) throw std::runtime_error("font fallback: the list was not kept");
    }
    void* a[]{fallback};
    Invoke(Method(list, "Add", 1), list, a);
}
// The face behind a new font asset really loaded (TMP only warns "Unable to load font face").
std::string FaceName(void* asset) {
    Root info(Invoke(Method(asset, "get_faceInfo", 0), asset));
    const auto* field = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(info.Get()), "m_FamilyName");
    return field ? Text(Get<void*>(info.Get(), il2cpp::api().field_get_offset(field))) : std::string();
}
// TMP_FontAsset.HasCharacter(<character>, searchFallbacks, tryAddCharacter): the character's type
// differs between TMP versions (uint, char).
bool HasCharacter(void* asset, uint32_t character, bool fallbacks) {
    bool tryAdd = true;
    const auto* klass = il2cpp::api().object_get_class(asset);
    void* it = nullptr;
    while (const auto* m = il2cpp::api().class_get_methods(klass, &it)) {
        if (std::string(il2cpp::api().method_get_name(m)) != "HasCharacter" || il2cpp::api().method_get_param_count(m) != 3) continue;
        char* type = il2cpp::api().type_get_name(il2cpp::api().method_get_param(m, 0));
        const std::string first = type ? type : "";
        if (type) il2cpp::api().free(type);
        uint16_t narrow = static_cast<uint16_t>(character);
        int wide = static_cast<int>(character);
        void* value = first == "System.Char" ? static_cast<void*>(&narrow) : first == "System.Int32" ? static_cast<void*>(&wide) : static_cast<void*>(&character);
        void* a[]{value, &fallbacks, &tryAdd};
        return Value<bool>(m, asset, a);
    }
    throw std::runtime_error("TMP_FontAsset.HasCharacter unavailable");
}
}  // namespace

bool Start(std::string* why) {
    int expected = 0;
    if (!state.compare_exchange_strong(expected, 1)) return state.load() != 3;
    if (Raw("ui.json").empty()) {
        state = 3;
        if (why) *why = "the in-game window's data is not embedded in this build";
        return false;
    }
    std::thread(Prepare).detach();
    return true;
}

bool Prepared(bool* ok) {
    const int s = state.load();
    if (ok) *ok = s == 2;
    return s >= 2;
}

const json::Value& Data() { return data; }

void* Sprite(const std::string& file, bool sliced) {
    const std::string key = file + (sliced ? "#sliced" : "");
    if (auto it = sprites.find(key); it != sprites.end())
        if (void* s = it->second.Get()) return s;
    void* texture = nullptr;
    // One texture per file: a sliced and a plain sprite of the same image share it.
    const std::string other = file + (sliced ? "" : "#sliced");
    if (auto it = sprites.find(other); it != sprites.end())
        if (void* s = it->second.Get()) texture = Invoke(Core("Sprite", "get_texture", {}), s);
    if (!texture) texture = Texture(file);
    const int w = Value<int>(Core("Texture", "get_width", {}), texture), h = Value<int>(Core("Texture", "get_height", {}), texture);
    struct Rect { float x, y, w, h; } rect{0, 0, static_cast<float>(w), static_cast<float>(h)};
    struct V2 { float x, y; } pivot{0.5f, 0.5f};
    struct V4 { float l, b, r, t; } border{};
    if (sliced) {
        const std::string name = file.substr(0, file.find('.'));
        const float edge = static_cast<float>(data["slices"][name].Num(0)) * 2;  // css px at 2x
        border = {edge, edge, edge, edge};
    }
    float ppu = 200;      // 2x images: one css pixel is one canvas unit (reference 100 per unit)
    unsigned extrude = 0;
    int fullRect = 0;
    void* a[]{texture, &rect, &pivot, &ppu, &extrude, &fullRect, &border};
    Root sprite(Invoke(Core("Sprite", "Create", {"UnityEngine.Texture2D", "UnityEngine.Rect", "UnityEngine.Vector2", "System.Single", "System.UInt32",
                                                 "UnityEngine.SpriteMeshType", "UnityEngine.Vector4"}), nullptr, a));
    KeepLoaded(sprite.Get());
    sprites[key].Set(sprite.Get());
    return sprite.Get();
}

const char* FaceLabel(Face face) {
    switch (face) {
    case Face::Body: return "Inter";
    case Face::BodySemi: return "Inter SemiBold";
    case Face::Serif: return "Georgia";
    case Face::Symbol: return "Segoe UI Symbol";
    case Face::SerifBold: return "Georgia Bold";
    case Face::BodyBold: return "Inter Bold";
    }
    return "?";
}
// TMP_FontAsset.fontWeightTable[weight / 100].regularTypeface = typeface (the table is an array of
// TMP_FontWeightPair { regular, italic }; both faces stay loaded, so no reference is lost to the GC).
void Weight(void* asset, int index, void* typeface) {
    void* table = Invoke(Method(asset, "get_fontWeightTable", 0), asset);
    if (!table) return;
    const auto length = Get<uintptr_t>(table, 0x18);
    if (static_cast<uintptr_t>(index) >= length) return;
    *reinterpret_cast<void**>(static_cast<char*>(table) + 0x20 + index * 2 * sizeof(void*)) = typeface;
}
// Kept only once complete: its face loaded and, for the text faces, the symbol fallback attached.
// A font that is not ready is dropped and asked for again later.
void* Font(Face face) {
    const int i = static_cast<int>(face);
    if (void* f = fonts[i].Get()) return f;
    void* asset = nullptr;
    switch (face) {
    case Face::Body: asset = CreateFromFile(FontFile("inter-regular.ttf")); break;
    case Face::BodySemi: asset = CreateFromFile(FontFile("inter-semibold.ttf")); break;
    case Face::Serif: asset = CreateFromSystem("Georgia"); break;
    case Face::Symbol: asset = CreateFromSystem("Segoe UI Symbol"); break;
    case Face::SerifBold: asset = CreateFromSystem("Georgia", "Bold"); break;
    case Face::BodyBold: asset = CreateFromFile(FontFile("inter-bold.ttf")); break;
    }
    if (!asset) throw std::runtime_error(std::string("font not ready: ") + FaceLabel(face) + " unavailable");
    if (FaceName(asset).empty()) {
        void* a[]{asset};
        Invoke(Core("Object", "Destroy", {"UnityEngine.Object"}), nullptr, a);
        throw std::runtime_error(std::string("font not ready: ") + FaceLabel(face) + " did not load");
    }
    KeepLoaded(asset);
    if (face != Face::Symbol) AddFallback(asset, Font(Face::Symbol));
    // <b> and font-weight 600: the faces the browser draws them with (TMP's own would leave Inter
    // for the game's fallback font)
    if (face == Face::Body) { Weight(asset, 6, Font(Face::BodySemi)); Weight(asset, 7, Font(Face::BodyBold)); }
    if (face == Face::Serif) Weight(asset, 7, Font(Face::SerifBold));
    fonts[i].Set(asset);
    return asset;
}

// TMP_FontAsset.TryAddCharacters(string, bool): a few glyphs into a face's atlas now, in the
// background, so a text never waits for a glyph to be drawn into the atlas.
bool Warm(Face face, const std::string& characters) {
    static const il2cpp::Method* add = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        try { add = Signature("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "TryAddCharacters", {"System.String", "System.Boolean"}); }
        catch (const std::exception&) { add = nullptr; }
    }
    if (!add || characters.empty()) return false;
    void* font = Font(face);
    Root text(il2cpp::api().string_new(characters.c_str()));
    bool features = false;
    void* a[]{text.Get(), &features};
    Invoke(add, font, a);
    return true;
}
bool FontsReady(std::string* why) {
    try {
        for (Face face : {Face::Symbol, Face::BodySemi, Face::BodyBold, Face::SerifBold, Face::Body, Face::Serif}) Font(face);
        return true;
    } catch (const std::exception& e) {
        if (why) *why = e.what();
        return false;
    }
}

std::string FontReport() {
    try {
    std::string out = "{";
    for (Face face : {Face::Body, Face::BodySemi, Face::Serif, Face::Symbol}) {
        void* asset = fonts[static_cast<int>(face)].Get();
        if (out.size() > 1) out += ',';
        out += Json(FaceLabel(face)) + ":";
        if (!asset) { out += "null"; continue; }
        void* list = Invoke(Method(asset, "get_fallbackFontAssetTable", 0), asset);
        const int fallbacks = list ? static_cast<int>(Entries(list).size()) : -1;
        out += "{\"face\":" + Json(FaceName(asset)) + ",\"fallbacks\":" + std::to_string(fallbacks) +
               ",\"gear\":" + Boolean(HasCharacter(asset, 0x2699, false)) + ",\"gearWithFallbacks\":" + Boolean(HasCharacter(asset, 0x2699, true)) + "}";
    }
    return out + "}";
    } catch (const std::exception& e) {
        return "{\"error\":" + Json(e.what()) + "}";  // a report never breaks "panel read"
    }
}

Metrics FontMetrics(Face face) {
    const int i = static_cast<int>(face);
    if (metricsKnown[i]) return metrics[i];
    void* font = Font(face);
    Root info(Invoke(Method(font, "get_faceInfo", 0), font));
    const auto field = [&](const char* name) {
        auto* f = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(info.Get()), name);
        return f ? Get<float>(info.Get(), il2cpp::api().field_get_offset(f)) : 0.f;
    };
    // m_PointSize is an int in older TextCore versions and a float in newer ones.
    float point = field("m_PointSize");
    uint32_t bits = 0;
    std::memcpy(&bits, &point, sizeof bits);
    if (bits && bits < 0x10000) point = static_cast<float>(bits);
    if (point > 0) metrics[i] = {field("m_LineHeight") / point, field("m_AscentLine") / point, field("m_DescentLine") / point};
    metricsKnown[i] = true;
    return metrics[i];
}

void Release() {
    sprites.clear();
    for (auto& f : fonts) f.Reset();
}

}  // namespace ep::ingame::assets
