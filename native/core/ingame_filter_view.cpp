#include "ingame_window.hpp"
#include "ingame_filter_io.hpp"
#include "ingame_browser_rules.hpp"
#include <future>
#include <chrono>
#include <shellapi.h>
#include <objbase.h>

namespace ep::ingame::view {
namespace {
using namespace model;
std::future<J> filterJob;
J filterState;
std::string importPath;
int filterSection = -1;
size_t filterPage=0;
bool busy = false;
bool attempted = false;
void Start(std::function<J()> job) {
    if (busy) return;
    busy = true;
    filterJob = std::async(std::launch::async, [job] {
        const auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        J result;
        try { result = job(); }
        catch (const std::exception& e) { Log("loot filter file operation: %s",e.what()); result = Object(); Put(result,"ok",Flag(false)); Put(result,"error",Text(e.what())); }
        if (SUCCEEDED(init)) CoUninitialize();
        return result;
    });
    if (filterSection >= 0) Rebuild(filterSection);
}
void Refresh() { attempted = true; Start([] { return filters::List(filters::Root()); }); }
void ImportDialog() {
    auto path = std::make_shared<std::string>(importPath);
    ShowCustomDialog(690, [path](const Ctx& c, float w) {
        float y = 27 + H2(c.parent,27,27,w-90,Tr("Import loot filter")) + 16;
        AddButton(c,w-27,24,"×",Look::Quiet,[] { CloseDialog(); },true,"filter-import-close");
        y += Muted(c.parent,27,y,w-54,Tr("Paste the full path to a loot filter XML file or an EpochPact backup ZIP. Every filter is checked first and imported as a new file. Existing filters are kept.")) + 16;
        y += InputField(c,27,y,w-54,Tr("XML or ZIP file"),*path,"C:\\Downloads\\My filter.xml",false,[path](const std::string& text) { *path = text; },"filter-import-path") + 20;
        auto cancel = AddButton(c,27,y,Tr("Cancel"),Look::Normal,[] { CloseDialog(); },false,"filter-import-cancel");
        const auto apply = AddButton(c,w-27,y,Tr("Import filter"),Look::Primary,[path] {
            importPath = *path; CloseDialog();
            Start([source = *path] { return filters::Import(filters::Root(),std::filesystem::u8path(source)); });
        },true,"filter-import-apply");
        return y + std::max(cancel.h,apply.h) + 27;
    });
}
void MakeFilters(Section& s) {
    filterSection = s.id;
    s.tick = [](Section&) { if (!attempted && !busy) Refresh(); };
    s.build = [](Section& sec, const Ctx& c) {
        filterSection = sec.id;
        auto panel = OpenPanel(c.parent,kPadX,0,kPanelW); const auto in = c.At(panel.rt.Get(),kPadX,0);
        float y = kPanelPad;
        y += SectionHead(in,kPanelPad,y,kPanelInner,Tr("Loot filter files"),{{Tr("Import filter…"),[] { if (!busy) ImportDialog(); }},
            {Tr("Back up all"),[] { Start([] { return filters::Backup(filters::Root(),filters::UserRoot()/L"filter-backups"); }); }},
            {Tr("Open folder"),[] { try { const auto root = filters::Root(); std::filesystem::create_directories(root); ShellExecuteW(nullptr,L"open",root.c_str(),nullptr,nullptr,SW_SHOWNORMAL); } catch (const std::exception& e) { Toast(PlayerError(e.what()),true); } }},
            {Tr("Refresh"),Refresh}}) + 16;
        y += Muted(in.parent,kPanelPad,y,kPanelInner,Tr("Back up and share your Last Epoch loot filters. Imports and copies are new files; existing filters are never replaced.")) + 14;
        if (busy) y += NoteBox(in.parent,kPanelPad,y,kPanelInner,Tr("Working…"));
        else if (filterState.IsNull()) y += AddButton(in,kPanelPad,y,Tr("Load loot filter files"),Look::Normal,Refresh,false,"filter-list").h;
        else if (filterState["filters"].items.empty()) y += EmptyText(in.parent,kPanelPad,y,kPanelInner,Tr("No loot filters found. Use Import filter to add an XML file or a filter backup ZIP."));
        else {
        const auto& files=filterState["filters"].items;const size_t pages=(files.size()+19)/20;filterPage=std::min(filterPage,pages-1);
        if(pages>1){auto previous=AddButton(in,kPanelPad,y,Tr("Previous"),Look::Small,[]{if(filterPage)--filterPage;Rebuild(filterSection);},false,"filter-previous");auto next=AddButton(in,kPanelPad+previous.w+10,y,Tr("Next"),Look::Small,[pages]{if(filterPage+1<pages)++filterPage;Rebuild(filterSection);},false,"filter-next");SetEnabled(in,previous,filterPage>0);SetEnabled(in,next,filterPage+1<pages);y+=std::max(previous.h,next.h)+10;y+=Muted(in.parent,kPanelPad,y,kPanelInner,Fmt("Page {0} of {1}",{std::to_string(filterPage+1),std::to_string(pages)}))+12;}
        for (size_t i=filterPage*20;i<std::min(files.size(),(filterPage+1)*20);++i) {
            const auto& file=files[i];
            Solid(in.parent,"Filter divider",kPanelPad,y,kPanelInner,1,C("line")); y += 15;
            y += H3(in.parent,kPanelPad,y,kPanelInner,file["name"].Str()) + 8;
            y += Muted(in.parent,kPanelPad,y,kPanelInner,file["file"].Str() + " · " + Num(file["size"].Num()/1024) + " KB · " +
                Fmt("{0} rules · {1} enabled",{Num(file["rules"].Num()),Num(file["enabled"].Num())}) + " · " + file["version"].Str(),12) + 12;
            if (!On(file["valid"])) y += NoteBox(in.parent,kPanelPad,y,kPanelInner,PlayerError(file["error"].Str())) + 12;
            auto duplicate = AddButton(in,kPanelPad,y,Tr("Duplicate"),Look::Normal,[name = file["file"].Str()] { Start([name] { return filters::Duplicate(filters::Root(),name); }); },false,"filter-copy:"+file["file"].Str());
            auto exportButton = AddButton(in,kPanelPad+duplicate.w+10,y,Tr("Export"),Look::Normal,[name = file["file"].Str()] { Start([name] { return filters::Export(filters::Root(),filters::UserRoot()/L"filter-exports",name); }); },false,"filter-export:"+file["file"].Str());
            SetEnabled(in,duplicate,On(file["valid"])); SetEnabled(in,exportButton,On(file["valid"])); y += duplicate.h + 20;
        }
        }
        y += kPanelPad; ClosePanel(panel,kPanelW,y); return y;
    };
}
} // namespace
void FilterFilesStep() {
    if (!busy || !filterJob.valid() || filterJob.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
    J result = filterJob.get(); busy = false;
    if (!On(result["ok"])) Toast(PlayerError(result["error"].Str()),true);
    else { filterState = result; if (!result["message"].Str().empty()) Toast(result["message"].Str()); }
    if (auto* s = FindSection(filterSection); s && s->kind == "filter_files") Rebuild(filterSection);
}
void ReleaseFilterFiles() { if (filterJob.valid()) { filterJob.wait(); filterJob.get(); } busy = false; filterSection = -1; }
void RegisterFilterSections() { RegisterSection("filter_files",MakeFilters); }
} // namespace ep::ingame::view
