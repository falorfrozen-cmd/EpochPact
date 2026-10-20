#include "ingame_filter_io.hpp"
#include "ingame_browser_rules.hpp"
#include <shlobj.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <wrl/client.h>
#include <fstream>
#include <regex>
#include <sstream>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_STDIO
#include "../third_party/miniz/miniz.h"
#pragma comment(lib, "xmllite.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace ep::ingame::filters {
namespace {
using namespace model;
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
constexpr size_t kLimit = 2 * 1024 * 1024, kTotal = 64 * kLimit;
std::string Utf8(const wchar_t* text, size_t n) {
    std::string s(WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(n), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(n), s.data(), static_cast<int>(s.size()), nullptr, nullptr); return s;
}
std::string U8(const fs::path& p) { const auto s = p.wstring(); return Utf8(s.data(), s.size()); }
void Check(bool good, const char* reason) { if (!good) throw std::runtime_error(reason); }
void SafePath(const fs::path& path) {
    Check(path.is_absolute(), "An absolute filter path is required.");
    for (auto p = path; !p.empty(); p = p.parent_path()) {
        const auto attrs = GetFileAttributesW(p.c_str());
        Check(attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT), "Linked filter paths are not supported.");
        if (p == p.parent_path()) break;
    }
}
std::string Read(const fs::path& p, size_t limit = kLimit) {
    SafePath(p); const auto size = fs::file_size(p); Check(size <= limit, "This filter file is too large.");
    std::ifstream input(p, std::ios::binary); Check(static_cast<bool>(input), "The filter file cannot be read.");
    std::string out(static_cast<size_t>(size), '\0'); input.read(out.data(), static_cast<std::streamsize>(out.size())); Check(input.good() || (input.eof() && input.gcount() == out.size()), "The filter file changed while reading."); return out;
}
std::vector<fs::path> Files(const fs::path& root) {
    SafePath(root); std::vector<fs::path> files;
    if (fs::exists(root)) for (const auto& file : fs::directory_iterator(root)) {
        if (file.is_regular_file() && Fold(U8(file.path().extension())) == ".xml") { SafePath(file.path()); files.push_back(file.path()); }
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return Fold(U8(a.filename())) < Fold(U8(b.filename())); });
    Check(files.size() <= 500, "This folder has more than 500 loot filters."); return files;
}
fs::path Existing(const fs::path& root, const std::string& file) {
    Check(!file.empty() && file.find_first_of("/\\:") == std::string::npos && file.find('\0') == std::string::npos, "Choose a loot filter from the list.");
    for (const auto& p : Files(root)) if (U8(p.filename()) == file) return p;
    throw std::runtime_error("That loot filter no longer exists. Refresh the list.");
}
std::string SafeName(std::string name) {
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return c < 32 || std::string("<>:\"/\\|?*").find(c) != std::string::npos; }), name.end());
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    if (name.size() > 120) { name.resize(120); while (!name.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), nullptr, 0)) name.pop_back(); }
    const auto stem = Fold(name.substr(0, name.find('.')));
    static const std::set<std::string> reserved{"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    return name.empty() || reserved.count(stem) ? "Imported filter" : name;
}
fs::path Unique(const fs::path& root, const std::string& name, const std::string& extension) {
    SafePath(root); const auto clean = SafeName(name);
    for (int n = 1; n < 10000; ++n) { const auto p = root / fs::u8path(clean + (n == 1 ? "" : " (" + std::to_string(n) + ")") + extension); if (!fs::exists(p)) return p; }
    throw std::runtime_error("Too many filter copies have this name.");
}
void WriteNew(const fs::path& path, const std::string& data) {
    SafePath(path); fs::create_directories(path.parent_path()); SafePath(path);
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "Could not create a new filter file. Nothing was replaced.");
    DWORD written = 0; const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() && FlushFileBuffers(file);
    CloseHandle(file); if (!ok) { DeleteFileW(path.c_str()); throw std::runtime_error("Could not finish writing the new filter file."); }
}
J Done(const fs::path& root, const std::string& message) { auto r = List(root); Put(r, "message", Text(message)); return r; }
std::string Escaped(const std::string& s) { std::string out; for (char c : s) out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : std::string(1,c); return out; }
struct Zip { mz_zip_archive archive{}; bool writing = false, active = false; ~Zip() { if (active) { if (writing) mz_zip_writer_end(&archive); else mz_zip_reader_end(&archive); } } };
fs::path Known(REFKNOWNFOLDERID id) { PWSTR value = nullptr; Check(SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &value)), "The Windows user folder is unavailable."); fs::path p(value); CoTaskMemFree(value); return p; }
} // namespace
Summary Parse(const std::string& xml) {
    Check(!xml.empty() && xml.size() <= kLimit, "This file is not a Last Epoch loot filter (2 MB limit).");
    const auto lower = Fold(xml); Check(lower.find("<!doctype") == std::string::npos && lower.find("<!entity") == std::string::npos, "Document types and entities are not allowed in loot filters.");
    ComPtr<IStream> stream; stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(xml.data()), static_cast<UINT>(xml.size()))); Check(stream != nullptr, "Cannot read filter XML.");
    ComPtr<IXmlReader> reader; Check(SUCCEEDED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr)), "Cannot open the Windows XML reader.");
    reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit); reader->SetProperty(XmlReaderProperty_MaxElementDepth, 128);
    Check(SUCCEEDED(reader->SetInput(stream.Get())), "Cannot read filter XML.");
    Summary result; bool root = false, name = false, rules = false, ruleEnabled = false; std::vector<std::string> path; std::string enabledText; XmlNodeType type; HRESULT hr;
    while ((hr = reader->Read(&type)) == S_OK) {
        UINT depth = 0; reader->GetDepth(&depth);
        if (type == XmlNodeType_Element) {
            const wchar_t* local = nullptr; UINT n = 0; reader->GetLocalName(&local, &n); const auto tag = Utf8(local,n);
            const wchar_t* ns = nullptr; UINT nsN = 0; reader->GetNamespaceUri(&ns,&nsN); Check(nsN == 0, "Namespaced filter XML is not supported.");
            if (depth == 0) { Check(!root && tag == "ItemFilter", "This file is not a Last Epoch loot filter."); root = true; }
            path.resize(depth); path.push_back(tag);
            if (depth == 1 && tag == "name") { Check(!name,"A loot filter must have one name."); name = true; }
            if (depth == 1 && tag == "rules") { Check(!rules,"A loot filter must have one rules list."); rules = true; }
            if (depth == 2 && path[1] == "rules" && tag == "Rule") { ++result.rules; ruleEnabled = false; }
            if (depth == 3 && path[1] == "rules" && path[2] == "Rule" && tag == "isEnabled") enabledText.clear();
            if (reader->IsEmptyElement()) path.resize(depth);
        } else if (type == XmlNodeType_Text || type == XmlNodeType_CDATA || type == XmlNodeType_Whitespace) {
            const wchar_t* value = nullptr; UINT n = 0; reader->GetValue(&value,&n); const auto text = Utf8(value,n);
            if (path.size() == 2 && path[1] == "name") result.name += text;
            if (path.size() == 2 && path[1] == "lastModifiedInVersion") result.version += text;
            if (path.size() == 4 && path[1] == "rules" && path[2] == "Rule" && path[3] == "isEnabled") enabledText += text;
        } else if (type == XmlNodeType_EndElement) {
            // XmlLite reports an end tag one level deeper than its opening tag.
            if (depth) --depth;
            if (depth == 3 && path.size() == 4 && path[1] == "rules" && path[2] == "Rule" && path[3] == "isEnabled") {
                const auto a=enabledText.find_first_not_of(" \t\r\n"), b=enabledText.find_last_not_of(" \t\r\n");
                if (a!=std::string::npos && Fold(enabledText.substr(a,b-a+1))=="true") ruleEnabled=true;
            }
            if (depth == 2 && path.size() == 3 && path[1] == "rules" && path[2] == "Rule" && ruleEnabled) ++result.enabled;
            path.resize(depth);
        }
    }
    Check(hr == S_FALSE && root && name && rules, "This loot filter file is damaged or has an unexpected layout.");
    return result;
}
std::string Rename(const std::string& xml, const std::string& name) {
    Parse(xml); const std::regex expression(R"((<ItemFilter\b[^>]*>\s*)<name\s*(?:/>|>[^<]*</name>))"); std::smatch match;
    Check(std::regex_search(xml, match, expression), "This loot filter has an unexpected layout and was not copied.");
    const auto result = match.prefix().str() + match[1].str() + "<name>" + Escaped(name) + "</name>" + match.suffix().str(); Parse(result); return result;
}
json::Value List(const fs::path& root) {
    J r = Object(), list = model::List(); Put(r,"ok",Flag(true)); Put(r,"folder",Text(U8(root))); Put(r,"exists",Flag(fs::is_directory(root)));
    for (const auto& file : Files(root)) {
        J entry = Object(); Put(entry,"file",Text(U8(file.filename()))); Put(entry,"size",Number(static_cast<double>(fs::file_size(file))));
        try { const auto s = Parse(Read(file)); Put(entry,"valid",Flag(true)); Put(entry,"name",Text(s.name.empty() ? U8(file.stem()) : s.name)); Put(entry,"rules",Number(s.rules)); Put(entry,"enabled",Number(s.enabled)); Put(entry,"version",Text(s.version)); }
        catch (const std::exception& e) { Put(entry,"valid",Flag(false)); Put(entry,"name",Text(U8(file.stem()))); Put(entry,"error",Text(e.what())); }
        list.items.push_back(std::move(entry));
    }
    Put(r,"filters",list); return r;
}
json::Value Duplicate(const fs::path& root, const std::string& file) {
    const auto source = Existing(root,file); const auto data = Read(source); const auto parsed = Parse(data);
    const auto target = Unique(root,(parsed.name.empty() ? U8(source.stem()) : parsed.name) + " copy",".xml");
    WriteNew(target,Rename(data,U8(target.stem()))); return Done(root,"Created \"" + U8(target.stem()) + "\". The original filter was not changed.");
}
json::Value Export(const fs::path& root, const fs::path& output, const std::string& file) {
    const auto source = Existing(root,file); const auto data = Read(source); Parse(data);
    const auto target = Unique(output,U8(source.stem()),".xml"); WriteNew(target,data);
    auto r = Done(root,"Exported as " + U8(target.filename()) + ". Share this XML file with other players."); Put(r,"folder",Text(U8(output))); return r;
}
json::Value Import(const fs::path& root, const fs::path& source) {
    SafePath(source); Check(source.is_absolute(), "Choose a filter file using its full path.");
    const auto ext = Fold(U8(source.extension())); std::vector<std::pair<std::string,std::string>> payloads;
    if (ext == ".xml") { Check(fs::weakly_canonical(source.parent_path()) != fs::weakly_canonical(root), "That filter is already in your Last Epoch filter folder."); payloads.emplace_back(U8(source.stem()),Read(source)); }
    else if (ext == ".zip") {
        const auto data = Read(source,kTotal); Zip zip;
        Check(mz_zip_reader_init_mem(&zip.archive,data.data(),data.size(),0) != 0, "This ZIP is damaged or unsupported."); zip.active = true;
        size_t total = 0; const auto count = mz_zip_reader_get_num_files(&zip.archive); Check(count <= 1000,"This ZIP contains too many files.");
        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat s{}; Check(mz_zip_reader_file_stat(&zip.archive,i,&s) != 0,"Cannot read the ZIP directory.");
            const std::string file(s.m_filename); if (s.m_is_directory || Fold(file.substr(file.find_last_of('.') == std::string::npos ? file.size() : file.find_last_of('.'))) != ".xml") continue;
            Check(file.find_first_of("/\\:") == std::string::npos && !s.m_is_encrypted && s.m_is_supported, "Only unencrypted top-level filter XML files may be imported.");
            Check(s.m_uncomp_size <= kLimit && total + s.m_uncomp_size <= kTotal && payloads.size() < 500,"This ZIP exceeds the filter size limits."); total += static_cast<size_t>(s.m_uncomp_size);
            std::string xml(static_cast<size_t>(s.m_uncomp_size),'\0'); Check(mz_zip_reader_extract_to_mem(&zip.archive,i,xml.data(),xml.size(),0) != 0,"A ZIP member failed its integrity check.");
            payloads.emplace_back(file.substr(0,file.size()-4),std::move(xml));
        }
        Check(!payloads.empty(),"This ZIP does not contain loot filter XML files.");
    } else throw std::runtime_error("Choose a loot filter .xml file or an EpochPact backup .zip.");
    for (const auto& p : payloads) Parse(p.second); // validate every member before writing any
    std::vector<fs::path> created;
    try { for (const auto& [fallback,data] : payloads) {
        const auto parsed = Parse(data); const auto target = Unique(root,parsed.name.empty() ? fallback : parsed.name,".xml");
        WriteNew(target, U8(target.stem()) == parsed.name ? data : Rename(data,U8(target.stem()))); created.push_back(target);
    } } catch (...) { for (const auto& p : created) DeleteFileW(p.c_str()); throw; }
    return Done(root,"Imported " + std::to_string(created.size()) + " new filters; nothing was replaced. Reopen the game's loot filter window, or restart Last Epoch if a filter is not listed yet.");
}
json::Value Backup(const fs::path& root, const fs::path& output) {
    const auto files = Files(root); Check(!files.empty(),"There are no loot filters to back up yet.");
    Zip zip; zip.writing = true; Check(mz_zip_writer_init_heap(&zip.archive,0,0) != 0,"Could not create the filter backup."); zip.active = true;
    size_t total = 0; for (const auto& file : files) {
        const auto data = Read(file); total += data.size(); Check(total <= kTotal,"The filter backup exceeds 128 MB.");
        Check(mz_zip_writer_add_mem(&zip.archive,U8(file.filename()).c_str(),data.data(),data.size(),6) != 0,"Could not add a filter to the backup.");
    }
    void* bytes = nullptr; size_t size = 0; Check(mz_zip_writer_finalize_heap_archive(&zip.archive,&bytes,&size) != 0,"Could not finish the backup ZIP.");
    std::unique_ptr<void,decltype(&mz_free)> buffer(bytes,&mz_free);
    SYSTEMTIME t{}; GetLocalTime(&t); char stamp[64]; std::snprintf(stamp,sizeof stamp,"filters-%04u%02u%02u-%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    const auto target = Unique(output,stamp,".zip"); WriteNew(target,std::string(static_cast<const char*>(bytes),size));
    // Prune only this tool's generated ZIP names, after verifying the completed new file.
    Check(Read(target,kTotal)==std::string(static_cast<const char*>(bytes),size),"The new backup could not be verified. Older backups were kept.");
    const std::regex owned(R"(filters-[0-9]{8}-[0-9]{6}(?:-[0-9]+| \([0-9]+\))?\.zip)");
    std::vector<fs::path> backups;
    for(const auto& file:fs::directory_iterator(output))if(file.is_regular_file()&&std::regex_match(U8(file.path().filename()),owned)) {
        SafePath(file.path());backups.push_back(file.path());
    }
    std::stable_sort(backups.begin(),backups.end(),[&](const auto& a,const auto& b){if(a==b)return false;if(a==target)return true;if(b==target)return false;return fs::last_write_time(a)>fs::last_write_time(b);});
    for(size_t i=20;i<backups.size();++i){const auto& old=backups[i];
        if(old.parent_path().lexically_normal()!=output.lexically_normal()||old==target)continue;
        SafePath(old);std::error_code ignored;fs::remove(old,ignored);
    }
    auto r = Done(root,"Backed up " + std::to_string(files.size()) + " loot filters to " + U8(target.filename()) + ". The newest 20 backups are kept."); Put(r,"folder",Text(U8(output))); return r;
}
std::filesystem::path Root() { return Known(FOLDERID_LocalAppDataLow) / L"Eleventh Hour Games" / L"Last Epoch" / L"Filters"; }
std::filesystem::path UserRoot() { return Known(FOLDERID_LocalAppData) / L"EpochPact"; }
} // namespace ep::ingame::filters
