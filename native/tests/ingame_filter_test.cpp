#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../core/ingame_filter_io.hpp"
#include "../core/ingame_browser_rules.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <objbase.h>
using namespace ep::ingame;
namespace fs = std::filesystem;
template<class F> void Refuse(F f) { bool refused=false; try { f(); } catch (const std::exception&) { refused=true; } assert(refused); }
int main() {
    const auto init = CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const auto base = fs::temp_directory_path() / (L"EpochPact-filter-test-"+std::to_wstring(GetCurrentProcessId()));
    assert(!fs::exists(base)); fs::create_directory(base); const auto root=base/L"Filters", output=base/L"exports", imported=base/L"Imported"; fs::create_directory(root);
    const std::string xml="<?xml version=\"1.0\"?><ItemFilter><name>Écho &amp; Relic</name><rules><Rule><isEnabled>true</isEnabled></Rule><Rule><isEnabled>false</isEnabled></Rule></rules><lastModifiedInVersion>1.5.2</lastModifiedInVersion></ItemFilter>";
    { std::ofstream file(root/L"original.xml",std::ios::binary); file<<xml; }
    const auto parsed=filters::Parse(xml);
    if(parsed.name!="Écho & Relic" || parsed.rules!=2 || parsed.enabled!=1)std::cerr<<"XML summary: "<<parsed.name<<" rules="<<parsed.rules<<" enabled="<<parsed.enabled<<std::endl;
    assert(parsed.name=="Écho & Relic" && parsed.rules==2 && parsed.enabled==1);
    const auto nested=filters::Parse("<ItemFilter><name>x</name><other><Rule><isEnabled>true</isEnabled></Rule></other><rules><Rule><isEnabled> \n true \t </isEnabled></Rule><Rule/></rules></ItemFilter>");
    assert(nested.rules==2 && nested.enabled==1);
    Refuse([] { filters::Parse("<ItemFilter><name>a</name><name>b</name><rules/></ItemFilter>"); });
    assert(filters::Parse(filters::Rename(xml,"New <name> & copy")).name=="New <name> & copy");
    Refuse([] { filters::Parse("<!DOCTYPE ItemFilter [<!ENTITY x SYSTEM 'file:///C:/secret'>]><ItemFilter><name>&x;</name><rules/></ItemFilter>"); });
    Refuse([] { filters::Parse("<ItemFilter><name>broken</name></ItemFilter>"); }); Refuse([] { filters::Parse("<evil><name>x</name><rules/></evil>"); });
    Refuse([&] { filters::Duplicate(root,"../original.xml"); });
    filters::Duplicate(root,"original.xml"); assert(filters::List(root)["filters"].items.size()==2);
    filters::Export(root,output,"original.xml"); assert(filters::List(output)["filters"].items.size()==1);
    filters::Backup(root,output); fs::path zip; for(const auto& f:fs::directory_iterator(output)) if(f.path().extension()==L".zip")zip=f.path(); assert(!zip.empty());
    filters::Import(imported,zip); assert(filters::List(imported)["filters"].items.size()==2);
    filters::Import(imported,zip); assert(filters::List(imported)["filters"].items.size()==4);
    {std::ofstream unrelated(output/L"unrelated.zip");unrelated<<"keep";}
    for(int i=0;i<22;++i)filters::Backup(root,output);
    int kept=0;for(const auto& file:fs::directory_iterator(output))if(file.path().filename().wstring().starts_with(L"filters-"))++kept;
    assert(kept==20 && fs::exists(output/L"unrelated.zip") && fs::exists(output/L"original.xml"));
    std::ifstream original(root/L"original.xml",std::ios::binary); std::stringstream bytes;bytes<<original.rdbuf();assert(bytes.str()==xml); original.close();
    fs::remove_all(base); if(SUCCEEDED(init))CoUninitialize();
    std::cout<<"native filters: strict XML, escaped names, preserved originals, ZIP roundtrip and collision-safe imports passed\n";
}
