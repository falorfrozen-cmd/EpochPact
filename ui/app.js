"use strict";
const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const clean = s => String(s ?? "").replace(/<[^>]*>/g, "");
const preferencesApi=window.EpochPreferences;
let displayPreferences;
try{displayPreferences=preferencesApi?.load(localStorage);}catch{}
displayPreferences??={favorites:[],hidden:[],summonColumns:[],hiddenStats:[],summonOptions:[]};
let showHiddenStats=false;
function saveDisplayPreference(name,id,enabled){
 if(!preferencesApi)return;
 displayPreferences=preferencesApi.toggle(displayPreferences,name,id,enabled);
 try{if(!preferencesApi.save(localStorage,displayPreferences))notify("Display preferences could not be saved on this device.",true);}catch{notify("Display preferences could not be saved on this device.",true);}
}
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const pages = {
 overview: ["Overview", "◈", "Your timeline. Your rules.", "Shape your journey, your loot and your character."],
 game_setup: ["Game setup", "⌁", "Connect your game.", "Choose Last Epoch.exe, install the player mod and launch offline."],
 general: ["General settings", "⚙", "Your journey. Your rules.", "Experience, loot, enemies and everyday conveniences."],
 loot: ["Loot & Pickup", "▣", "Keep what matters.", "Automatic collection, loot filters and crafting material pickup."],
 crafting: ["Crafting", "⚒", "Make every craft count.", "Forge your selected item, preserve materials and customize crafting costs."],
 stats: ["Character", "♜", "Refine your character.", "Find a stat and customize its bonus."],
 skills: ["Skills & Trees", "✧", "Build beyond the branches.", "Skill experience, point budgets, tree connections and projectiles."],
 equipment: ["Equipment", "⚔", "Equip your imagination.", "Offhand combinations and full set bonuses."],
 summons: ["Summons", "♧", "Command your companions.", "Minion bonuses and temporary summon duration."],
 display: ["Display & Camera", "◉", "See Eterra your way.", "Camera pitch, zoom and temporary graphics overrides."],
 world: ["World & Exploration", "⌖", "Explore without friction.", "Map visibility and nearby chest, shrine and portal interactions."],
 inventory: ["Inventory", "▤", "Your workshop, anywhere.", "Open your stash or vendor and transfer inventory items safely."],
 atlas: ["Unique Atlas", "✦", "Every relic has a story.", "Your collection, missing treasures and the next item to chase."],
 stash: ["Stash Assistant", "▤", "Find the keeper.", "Compare your copies and mark what matters."],
 campaign: ["Campaign", "⌘", "Begin a new chapter.", "Quests, their normal rewards and waypoints across the world."],
 monolith: ["Monolith", "◇", "Reshape your destiny.", "Timelines, Normal / Empowered progression and Stability."],
 cof: ["Circle of Fortune", "✧", "Forge your fortune.", "Favor, Reputation, prophecies, lenses and loot quality."],
 factions: ["Factions", "♧", "The factions of Eterra.", "Circle of Fortune, Merchant’s Guild, Knights and Weaver."],
 settings: ["Settings", "☷", "Make this workshop yours.", "Themes, saved profiles and recovery backups."]
};
const lifetime = {session:"Session",actor:"Character",game_save:"Game save",read_only:"Read only",view:"View",recovery:"Recovery"};
const description = c => c.description;
let catalog, token, preview = false, developer = false, page = "overview", busy = false;
const developerControls=new Set(["density_read","map_read","pickup_radius_read","tier_read","map_settings_read","font_read","sheet_catalog","sheet_open","resistance_labels","session_read","status","player_read","characters","play_offline","load_character"]);
function playerControl(c){return developer||!developerControls.has(c.id);}
let connected = {connected:false,offline:false}, values = {}, appliedValues = {}, dirty = new Set(), cache = {}, history = [], actorValues = {};
const craftingModel=window.EpochCrafting;
const affixPicker=window.EpochAffixes;
let wantedAffixes=affixPicker.create();
let craftingSection='forge',workshopAdvanced=false;
const craftDisclosure={advanced:false,defaults:false};
let workshopDraft=craftingModel.defaults(),workshopOwner=null;
let workshopModifierFilter="";
let workshopFilters={template:'',affixes:'',modifiers:''},workshopTemplateKind='all',workshopListLimits={affixes:40,modifiers:40};
let workshopEntryCache={};
let materialTarget="10000";
// Only direct edits in General/Overview auto-apply. Reads, profiles and save
// actions never enter this queue. One job at a time; latest unsent value wins.
const autoPending=new Map(),autoErrors=new Map(),autoApplied=new Set();
let autoTimer,autoSending=null,autoRunning=false,autoGeneration=0;
let liveReading=false;
const convenienceNextRead={};
let connectionChecking=false,connectionNextCheck=0,lastGameSession=null;
// Startup profile: off by default, so opening EpochPact still applies nothing.
// When chosen, it is applied once per game session and character after that
// offline character is ready. Only session/actor settings are stored; quests,
// rewards and other save actions never are.
const startupApplied=new Set();
function storageKey(name){return `epochpact.${name}.${preview?"preview":"live"}`;}
function readStored(name,fallback){try{const v=JSON.parse(localStorage.getItem(storageKey(name))??"null");return v??fallback;}catch{return fallback;}}
function writeStored(name,value){try{localStorage.setItem(storageKey(name),JSON.stringify(value));return true;}catch{return false;}}
function profileSetting(c){return Boolean(c&&["session","actor"].includes(c.lifetime)&&["number","toggle"].includes(c.widget));}
function rememberLastSettings(){
 if(preview||readStored("startup-profile","")!=="__last__")return;
 const last={};for(const c of catalog.controls)if(profileSetting(c)&&appliedValues[c.id]!=null&&!Object.is(appliedValues[c.id],c.default))last[c.id]=appliedValues[c.id];
 writeStored("last-settings",last);
}
function startupProfile(){
 const name=readStored("startup-profile","");if(!name)return null;
 if(name==="__last__")return {name:"your last used settings",values:readStored("last-settings",{})};
 let profiles={};try{profiles=JSON.parse(localStorage.getItem(profileKey)||"{}");}catch{}
 return Object.hasOwn(profiles,name)?{name:`profile “${name}”`,values:profiles[name]}:null;
}
function applyStartupProfile(){
 const owner=automaticOwner();if(preview||!owner||startupApplied.has(owner)||busy||autoRunning||autoPending.size)return;
 const profile=startupProfile();if(!profile)return;
 startupApplied.add(owner);
 let changes=0;
 for(const [id,v] of Object.entries(profile.values||{})){const c=controlById(id);
  if(profileSetting(c)&&eligible(c)&&!Object.is(appliedValues[id],v)){values[id]=v;dirty.add(id);changes++;}}
 if(!changes)return;
 notify(`Applying ${profile.name} (${changes} setting${changes===1?"":"s"}).`);
 setTimeout(()=>void applyDraft(),0);
}
function automaticSetting(c){return Boolean(((["general","overview"].includes(page)&&c?.group==="general")||/^(buildset|qolset|pickupradius|pickupinterval|autopickup|mapreveal|mapset|tierset|fontset) /.test(c?.commandTemplate||""))&&["number","toggle"].includes(c?.widget)&&["session","actor"].includes(c.lifetime));}
function automaticOwner(){
 if(preview)return "preview";
 const p=connected.player;
 return connected.connected&&connected.offline&&connected.state==="InGame"&&!connected.transitioning&&p?.id!=null?JSON.stringify([String(p.id),p.name??"",connected.sessionKey??null]):null;
}
function autoManaged(id){return autoPending.has(id)||autoErrors.has(id)||autoSending?.id===id;}
function cancelAutomatic(){
 clearTimeout(autoTimer);autoGeneration++;
 for(const id of new Set([...autoPending.keys(),...autoErrors.keys(),...(autoSending?[autoSending.id]:[])])){values[id]=appliedValues[id];dirty.delete(id);}
 autoPending.clear();autoErrors.clear();autoApplied.clear();
}
function armAutomatic(){
 clearTimeout(autoTimer);
 if(busy||autoRunning||liveReading||!autoPending.size)return;
 const next=Math.min(...[...autoPending.values()].map(x=>x.readyAt));
 autoTimer=setTimeout(()=>void flushAutomatic(),Math.max(0,next-Date.now()));
}
function settingFeedback(id){
 if(autoErrors.has(id)){const e=autoErrors.get(id);return `<span class="setting-error">${esc(e.message)}</span>${e.retry?` <button class="quiet" data-auto-retry="${esc(id)}">Retry</button>`:""}`;}
 if(autoPending.has(id)||autoSending?.id===id)return '<span class="pending">Applying…</span>';
 if(dirty.has(id))return '<span class="pending">Draft</span>';
 return autoApplied.has(id)?'<span class="setting-applied">Applied</span>':"";
}
function syncSettingUi(id){
 for(const article of document.querySelectorAll(`[data-setting="${id}"]`)){
  for(const input of article.querySelectorAll('[data-setting-number],[data-setting-range]'))if(input!==document.activeElement)input.value=settingDisplayValue(id,values[id])??"";
  const toggle=article.querySelector('[data-setting-toggle]');
  if(toggle){toggle.checked=!!values[id];toggle.parentNode.firstChild.textContent=values[id]?"On":"Off";}
  const feedback=article.querySelector('[data-setting-feedback]');if(feedback)feedback.innerHTML=settingFeedback(id);
 }
 updateDisabled();
}
function queueAutomatic(id,value,delay=250){
 const c=controlById(id),owner=automaticOwner();
 if(!owner||!eligible(c)){notify("Load an offline character before changing this setting.",true);return;}
 values[id]=value;autoErrors.delete(id);autoApplied.delete(id);
 if(autoSending?.id===id&&Object.is(autoSending.value,value)){autoPending.delete(id);}
 else if(Object.is(appliedValues[id],value)&&autoSending?.id!==id){autoPending.delete(id);dirty.delete(id);}
 else{
  dirty.add(id);const old=autoPending.get(id);
  autoPending.set(id,{value,owner,generation:autoGeneration,readyAt:old&&Object.is(old.value,value)?Math.min(old.readyAt,Date.now()+delay):Date.now()+delay});
 }
 syncSettingUi(id);armAutomatic();
}
async function flushAutomatic(force=false){
 if(busy||autoRunning||liveReading){armAutomatic();return;}
 const entry=[...autoPending].find(([,edit])=>force||edit.readyAt<=Date.now());
 if(!entry){armAutomatic();return;}
 const [id,edit]=entry;
 if(edit.generation!==autoGeneration||edit.owner!==automaticOwner()){cancelAutomatic();renderPage();return;}
 autoPending.delete(id);autoSending={id,...edit};autoRunning=true;syncSettingUi(id);
 let errorMessage="Could not apply this setting. Check the connection before retrying.";
 try{
  const args={value:edit.value};
  if(!preview){const [expected_id,expected_name,expected_session]=JSON.parse(edit.owner);Object.assign(args,{expected_id,expected_name});if(expected_session!=null)args.expected_session=expected_session;}
  const result=await runTask({type:"control",id,args},{silent:true,onError:message=>{errorMessage=message;}});
  if(edit.generation!==autoGeneration||edit.owner!==automaticOwner())return;
  if(!result){
   // A lost response is not permission to retry a mutation. Keep the failed
   // value visible and require a deliberate Retry/new edit.
   for(const [pendingId,pending] of autoPending){autoErrors.set(pendingId,{message:"Not applied after a failed operation.",retry:true});values[pendingId]=pending.value;}
   autoPending.clear();autoErrors.set(id,{message:errorMessage,retry:true});return;
  }
  syncLootCraft(id,result);appliedValues[id]=edit.value;autoApplied.add(id);
  if(Object.is(values[id],edit.value)&&!autoPending.has(id)&&!autoErrors.has(id))dirty.delete(id);
 }finally{autoSending=null;autoRunning=false;syncSettingUi(id);armAutomatic();}
}
function invalidateLive({render=true}={}){
 cancelAutomatic();
 wantedAffixes=affixPicker.create();
 cache={};actorValues={};history=[];connected={connected:false,offline:false};
 if(activeStat){activeStat=null;$("#stat-dialog").close();}
 $("#connection").textContent="○ Game disconnected";$("#connection").classList.remove("connected");
 if(catalog&&render)renderPage();
}
const transport=window.EpochSession.create({fetch:(...args)=>fetch(...args),sleep,warn:message=>notify(message),
 isReadOnly:payload=>payload.type==="connection"||payload.type==="control"&&
  (payload.operation==="read"||["read","preview"].includes(controlById(payload.id)?.widget)),
 onSessionChanged:boot=>{catalog=boot.catalog;token=boot.token;preview=boot.preview;developer=boot.developer===true;window.EpochLauncher?.update(boot.launcher);invalidateLive();}
});
let statsTab = "sheet", statsQuery = "", statsPage = 0, activeStat = null;
let statsCategory = "All", statsFavoritesOnly = false, statFavorites = new Set();
const ST = window.EpochStats;
let monoTarget = {timeline:null,difficulty:"normal"}, monoDrafts = {}, monoDraftOwner = null, confirmationResolve;
let profileKey;
const content = $("#page-content");

// Keep browser validation messages English even on a non-English OS.
document.addEventListener("invalid", e => {
 const input=e.target;
 if(!input.setCustomValidity)return;
 input.setCustomValidity("");
 const v=input.validity;
 const message=v.valueMissing?"Complete this field.":v.badInput||v.typeMismatch?"Enter a valid value.":
  v.rangeUnderflow?`Enter a value of at least ${input.min}.`:
  v.rangeOverflow?`Enter a value no greater than ${input.max}.`:
  v.stepMismatch?`Use increments of ${input.step}.`:
  v.patternMismatch?"Use the required format.":"Enter a valid value.";
 input.setCustomValidity(message);
},true);
for(const event of ["input","change"])document.addEventListener(event,e=>e.target.setCustomValidity?.(""),true);

function notify(message, error=false) {
 const box=$("#notification"); box.textContent=message; box.classList.toggle("error",error);box.hidden=false;
 clearTimeout(notify.timer);notify.timer=setTimeout(()=>box.hidden=true,error?12000:6500);
}
function playerError(message,payload){
 const text=String(message||"The game could not complete this action.");
 // A file path is useful context for setup failures. It is not a failed game
 // command, and refreshing the game connection cannot repair installation.
 if(payload?.type==="launcher")return /traceback|actorValues|statraw|[{}]/i.test(text)?
  "Game setup could not finish. Check operations.log in LocalAppData/EpochPact/live/ui for the error details. No action was automatically retried.":text;
 if(/main thread|pick the job|paused or minimi/i.test(text))return "The game did not respond. Return to the game, then refresh and try again.";
 if(/traceback|actorValues|statraw|[{}]|[a-z]:[\\/]/i.test(text))return "The game could not complete this action. Refresh the connection and try again. Technical details were saved in the local log.";
 return text;
}
function playerResult(payload,result){
 if(payload?.type==="connection")return result.preview?"Preview connection refreshed. The game was unchanged.":"Game connection refreshed.";
 const id=payload?.id,a=payload?.args||{},label=controlById(id)?.label||"Action";
 const target=cache.monolith_read?.timelines?.find(t=>t.id===Number(a.timeline));
 const where=target?` ${clean(target.name||target.label)} (${a.difficulty==="empowered"?"Empowered":"Normal"}).`:"";
 const messages={
  stability:`Stability set to ${a.value}.${where}`,corruption:`Corruption set to ${a.value}.${where}`,
  campaign_complete:result.mastery?.needsSelection?"Campaign completed with normal rewards. Use Choose Mastery to finish your character.":"Campaign completed with normal rewards.",waypoints_unlock:"All waypoints unlocked.",
  mastery_choose:"Mastery selection opened in the game. Choose and confirm your mastery there.",
  inventory_stash:result.opened===false?"Stash closed in the game.":"Stash opened in the game.",inventory_vendor:result.opened===false?"Vendor closed in the game.":"Vendor opened in the game.",
  loot_log_clear:"Recent pickups cleared. Items and gold are unchanged.",
  pickup_now:`Picked up ${result.picked??0} drops.${result.inventoryFull?" Your inventory is full: equipment waits until you open and close your inventory.":""}`,
  inventory_stashall:(result.remaining??0)>0?`Moved ${result.moved??0} items to your stash. ${result.remaining} items stayed in your inventory: the stash had no room for them. Free stash space or add a stash tab, then try again.`:`Moved ${result.moved??0} items to your stash. Your inventory is empty; equipped items were not moved.`,
  monolith_unlock:"Normal and Empowered timelines unlocked.",monolith_select:`Timeline selected.${where}`,
  cof_rank:`Circle of Fortune rank set to ${a.value}.`,cof_favor:`Favor set to ${a.value}.`,
  cof_reputation_grant:`Added ${a.value} Reputation.`,cof_join:"Joined Circle of Fortune.",cof_lenses_unlock:"Available lenses unlocked.",
  cofprophecy:`Prophecy slot ${Number(a.slot)+1} updated.`,cof_charges:`Prophecy slot ${Number(a.slot)+1} charges set to ${a.value}.`,
  craft_forge:"Item forged once.",craft_materials:`${result.updatedKinds??0} material types stocked to at least ${result.target??a.value}. Saved in your crafting stash.`,craft_reset:"Normal crafting restored.",loot_reset:"Pickup rules reset.",stat_reset:"All character bonuses reset.",
  undo:"Selected backup restored. You can reopen the game."
 };
 if(result.preview)return "Preview complete. The game and saves were unchanged.";
 return (messages[id]||`${label} updated.`)+(result.backup?" A recovery backup is available in Settings.":"");
}
function resultDialog(result,title,payload) {
 if(!developer&&payload?.id!=="cofpreview"){
  notify(result.ok?playerResult(payload,result):playerError(result.error||result.text)+(result.backup?" A recovery backup is available in Settings.":""),!result.ok);return;
 }
 $("#result-title").textContent=developer?title:"Prophecy preview";
 if(developer)$("#result-text").innerHTML=`<pre class="readout">${esc(result.text||JSON.stringify(result,null,2))}</pre>`;
 else{
  const a=payload.args||{},live=cache.cof_read;
  const reward=live?.rewards?.find(r=>r.id===a.reward),lens=live?.lenses?.find(l=>l.id===a.lens);
  const chargesBefore=result.currentCharges,chargesAfter=result.chargesAfter;
  $("#result-text").innerHTML=`<p class="note">Preview only. No changes have been applied.</p><dl class="result-summary"><dt>Slot</dt><dd>${Number(a.slot)+1}</dd><dt>Reward</dt><dd>${esc(clean(reward?.uiLabel||reward?.name||reward?.label||"None"))}</dd><dt>Lens</dt><dd>${esc(clean(lens?.uiLabel||lens?.name||"None"))}</dd>${chargesBefore!=null&&chargesAfter!=null?`<dt>Charges</dt><dd>${chargesBefore} → ${chargesAfter}</dd>`:""}</dl>`;
 }
 if (!$("#result-dialog").open) $("#result-dialog").showModal();
}
function confirmAction(title,message) {
 $("#confirm-title").textContent=title;$("#confirm-message").textContent=message;
 $("#confirm-dialog").showModal();return new Promise(resolve=>confirmationResolve=resolve);
}
function finishConfirm(value) {$("#confirm-dialog").close();if(confirmationResolve){confirmationResolve(value);confirmationResolve=null;}}
function controlById(id){return catalog.controls.find(x=>x.id===id);}
function saveSnapshot(c){return c.group==="inventory"?cache.inventory_read:c.group==="skills"?cache.build_read:c.id==="craft_materials"?cache.craft_materials_read:["craft_forge","craft_custom"].includes(c.id)?cache.craft_read?.forgePreview:cache[c.group==="campaign"?"progress_read":c.group==="monolith"?"monolith_read":"cof_read"];}
function eligible(control) {
 if(preview)return true;
 if(!connected.connected)return false;
 const buildKey=control.commandTemplate?.startsWith("buildset ")?control.commandTemplate.split(" ")[1]:null;
 if(buildKey && cache.build_read?.settings?.[buildKey]?.available===false)return false;
 if(control.requirements.includes("offline") && (!connected.offline || connected.state!=="InGame" || connected.transitioning)) return false;
 for(const state of ["Login","CharacterSelect"]) if(control.requirements.includes(state+"_and_not_transitioning") && (connected.state!==state||connected.transitioning))return false;
 return true;
}
function updateDisabled() {
 window.EpochLauncher?.disable();
 document.documentElement.dataset.uiMode=developer?"developer":"player";
 document.querySelectorAll('[data-developer-only]').forEach(el=>el.hidden=!developer);
 $("#reconcile").hidden=!developer;
 document.querySelectorAll("[data-control-id]").forEach(button=>{
  const c=controlById(button.dataset.controlId);if(!c)return;
  const reconnectingRead=button.dataset.refresh===c.id&&c.widget==="read";
  let reason = !eligible(c)&&!reconnectingRead?"Load an offline character and wait for the area to finish loading.":"";
  if(c.id==="undo"&&!history.length)reason="No saved operation is available to restore.";
  if(c.lifetime==="game_save"||c.id==="mastery_choose") {
   const data=saveSnapshot(c);
   reason ||= window.EpochSession.saveActionReason(data,connected,{preview,monolith:c.group==="monolith"});
  }
  if(c.id==="mastery_choose"&&!preview&&cache.progress_read?.mastery?.available!==true)reason ||= cache.progress_read?.mastery?.reason||"Refresh live to check whether mastery selection is available.";
  if(button.dataset.unavailable==="true")reason ||= "This action is unavailable for the current selection.";
  if(c.id==="craft_forge"&&cache.craft_read?.forgePreview?.canForge!==true)reason ||= cache.craft_read?.forgePreview?.message||"Select an eligible item and material in the game forge, then read live.";
  if(c.id==="craft_custom"){
   reason ||= cache.craft_read?.forgePreview?.canCustom!==true?"Put an equipment item in the game's forge, then refresh the selected item.":craftingModel.problem(workshopDraft,cache.craft_custom_catalog,cache.craft_read?.forgePreview);
   if(!preview&&cache.craft_custom_catalog?.player&&String(cache.craft_custom_catalog.player.id)!==String(connected.player?.id))reason ||= "Load item choices for the current character.";
  }
  if(preview&&["craft_materials","craft_materials_read"].includes(c.id))reason ||= "Load an offline character to read or stock real crafting materials.";
  button.disabled=busy||!!reason;
  button.title=busy?"Waiting for the current operation.":reason||(!eligible(c)&&reconnectingRead?"Refresh game connection and live data.":"");
  const hint=button.closest("form")?.querySelector("[data-action-reason]");
  if(hint){hint.textContent=reason;hint.hidden=!reason||busy;}
 });
 const autoPage=["general","overview"].includes(page),setupPage=page==="game_setup",manualDirty=[...dirty].filter(id=>!autoManaged(id));
 const craftingActionPage=page==='crafting'&&craftingSection!=='forge';
 $("#apply-draft").hidden=autoPage||setupPage||craftingActionPage;$("#reset-draft").hidden=autoPage||setupPage||craftingActionPage;
 $("#apply-draft").disabled=busy||!manualDirty.length||!connected.connected||(!preview&&!connected.offline);
 $("#reconcile").disabled=busy||!Object.keys(actorValues).length||(!preview&&!connected.offline);
 $("#refresh-connection").disabled=busy;
 $("#reset-draft").disabled=busy;
 document.querySelectorAll('[data-setting-number],[data-setting-range],[data-setting-toggle],[data-setting-choice],[data-setting-reset],[data-step]').forEach(input=>{
  const id=input.dataset.settingNumber||input.dataset.settingRange||input.dataset.settingToggle||input.dataset.settingChoice||input.dataset.settingReset||input.dataset.settingId,c=controlById(id);
  input.disabled=automaticSetting(c)?!eligible(c)||busy&&!autoRunning:busy;
 });
 document.querySelectorAll('[data-craft-glyph-mode],[data-workshop-pick],[data-workshop-template]').forEach(input=>input.disabled=busy);
 $("#draft-label").innerHTML=(busy?"Working…":setupPage?"Game setup":autoErrors.size?"A setting needs attention":autoPending.size?"Applying changes…":autoPage?"Changes apply automatically":manualDirty.length?`${manualDirty.length} settings in draft`:"Ready")+"<span>"+(preview?"Preview; no changes to the game.":setupPage?"Choose your executable, then use the setup buttons.":"Save actions use their own buttons.")+"</span>";
 document.querySelectorAll("#stat-dialog button:not(#stat-close), #stat-dialog input, #stat-dialog select").forEach(b=>b.disabled=busy);
 const statAllowed=activeStat&&eligible(controlById(activeStat.tab==="aliases"?"stat_alias":activeStat.tab==="sheet"?"sheet_row":"raw_stat"));
 for(const id of ["stat-apply","stat-reset","stat-read"])$("#"+id).disabled=busy||!statAllowed;
 $("#stat-mode").disabled=busy||activeStat?.tab==="aliases";
}
async function job(payload) {
 return transport.run(payload);
}
async function runTask(payload,{show=false,title="Operation result",silent=false,onError,background=false}={}) {
 if(busy)return null;if(!background)busy=true;let errorBackup=false;if(!background)updateDisabled();
 if(payload.type==="control"&&connected.sessionKey&&!preview)payload={...payload,args:{expected_session:connected.sessionKey,...payload.args}};
 try{
  const result=await job(payload);
  if(payload.type==="control"&&result.ok&&profileSetting(controlById(payload.id)))setTimeout(rememberLastSettings,0);
  if(payload.type==="control"&&payload.id==="loot_log_clear"&&result.ok){lootEntries=[];const node=$("#loot-log-live");if(node)node.innerHTML=lootTable();}
  if(result.launcher)window.EpochLauncher?.update(result.launcher);
  if(result.requiresElevation)window.EpochLauncher?.failure(result);
  if(result.history)history=result.history;
  if(result.actorValues){actorValues=result.actorValues;syncSpeed();}
  if(!result.ok){errorBackup=!!result.backup;if(result.backup&&developer)resultDialog(result,"Operation error / backup",payload);throw new Error(result.error||result.text||"The action was rejected.");}
  if(show)resultDialog(result,title,payload);
  else if(!silent)notify(developer?result.preview?"Preview complete; the game was unchanged.":"Operation complete.":playerResult(payload,result));
  return result;
 }catch(error){const message=developer?error.message:playerError(error.message,payload)+(errorBackup?" A recovery backup is available in Settings.":"");onError?.(message);if(!background)notify(message,true);return null;}
 finally{if(!background)busy=false;updateDisabled();armAutomatic();}
}
async function refreshConnection(silent=true,refreshCurrent=true,{preservePage=false,background=false}={}) {
 if(busy||connectionChecking)return;
 connectionChecking=true;
 try{
 const previous=connected;
 rememberMonolithOwner(connected);
 const result=await runTask({type:"connection"},{silent,background});if(!result){invalidateLive();updateDisabled();return;}
 const restarted=lastGameSession!=null&&result.sessionKey!=null&&lastGameSession!==result.sessionKey;
 if(restarted){invalidateLive({render:false});monoDrafts={};monoDraftOwner=null;monoTarget={timeline:null,difficulty:"normal"};workshopOwner=null;workshopDraft=craftingModel.defaults();}
 if(result.sessionKey!=null)lastGameSession=result.sessionKey;
 const ownerChanged=rememberMonolithOwner(result);
 if(!preview&&(ownerChanged||!window.EpochSession.current({player:result.player},result)))invalidateLive({render:false});
 connected=result;
 // An editable snapshot belongs to the area where it was read, even when
 // the same character moves between an Echo and a Monolith hub.
 if(!preview&&cache.monolith_read&&!window.EpochSession.currentArea(cache.monolith_read,connected)){
  delete cache.monolith_read;delete cache.echo_read;
 }
 const label=preview?"● Preview":result.phase==="update_required"?"◌ Mod update required":result.phase==="setup"?"○ Install the player mod":result.phase==="starting"?"◌ Connecting to game…":result.phase==="waiting_frame"?"● Mod connected · Waiting for game updates…":["character_loading","loading"].includes(result.phase)?"● Mod connected · Waiting for character…":result.phase==="read_error"?"● Mod connected · Game data unavailable":result.phase==="unavailable"?"◌ Reconnecting to game…":result.connected?(result.offline?"● Offline · "+result.state:"● "+result.state):"○ Game disconnected";
 $("#connection").textContent=label;$("#connection").title=result.error||result.text||label;
 $("#connection").classList.toggle("connected",result.connected);
 let valuesChanged=false;
 for(const [id,value] of Object.entries(result.values||{})){appliedValues[id]=value;if(!dirty.has(id)){valuesChanged ||= values[id]!==value;values[id]=value;}}
 const sameArea=preview||!restarted&&!ownerChanged&&window.EpochSession.current({player:previous.player},previous)&&window.EpochSession.currentArea({player:previous.player},connected);
 const unchangedPending=!result.offline&&!previous.offline&&result.phase===previous.phase&&result.state===previous.state&&result.sessionKey===previous.sessionKey;
 // A focus read should not rebuild unchanged forms, steal input focus or lose
 // partially typed fields elsewhere in the interface.
 if(!preservePage||!sameArea&&!unchangedPending||valuesChanged)renderPage();else updateDisabled();
 const readId=pageReadId();
 const masteryMayHaveChanged=page==="campaign"&&cache.progress_read?.mastery?.panelOpen===true;
 const recovered=background&&(!previous.offline||restarted)&&result.offline;
 if((refreshCurrent||recovered)&&readId&&(!cache[readId]||["general","loot","crafting"].includes(page)||masteryMayHaveChanged)&&eligible(controlById(readId)))await refreshSection(readId,{refreshConnectionFirst:false});
 if(result.offline){applyStartupProfile();setTimeout(()=>void applyTierPalette(),0);}
 }finally{connectionChecking=false;connectionNextCheck=Date.now()+(connected.offline?10000:["stopped","setup","update_required","waiting_frame","read_error"].includes(connected.phase)?5000:2000);}
}
async function pollConnection(){
 if(!catalog||preview||busy||connectionChecking||liveReading||autoRunning||autoPending.size||Date.now()<connectionNextCheck||window.EpochLauncher?.browsing())return;
 // Only a read-only handshake is retried. Never repeat an accepted action or
 // rebuild unchanged forms while the player types or compares values.
 await refreshConnection(true,false,{preservePage:true,background:true});
}
setInterval(()=>void pollConnection(),1000);
function syncSpeed(){
 const modes=actorValues["9:0:0:0"]?.modes;if(modes){appliedValues.speed=1+(modes.increased||0);if(!dirty.has("speed"))values.speed=appliedValues.speed;}
}
function unitFor(c){return c.id==="pickup_radius"?" units":c.id==="pickup_interval"?" s":c.id.startsWith("map_")&&c.id.endsWith("opacity")?"":c.id.startsWith("map_")?"×":c.id==="build_cameraangle"?"°":c.id.startsWith("cof_double_")?"%":c.id==="loot_lp"?" LP":["craft_fp","build_weaverflat","build_projectileflat","build_blessingoptions"].includes(c.id)?"":c.widget==="toggle"?"":"×";}
function settingDisplayValue(id,value){return id==="speed"&&Number.isFinite(value)?Number(value.toFixed(6)):value;}
function settingCard(c) {
 const p=c.parameters.find(x=>x.name==="value"),v=settingDisplayValue(c.id,values[c.id]??c.default),unit=unitFor(c);
 const isToggle=c.widget==="toggle";
 if(p.choices)return `<article class="control-card" data-setting="${esc(c.id)}"><div class="control-top"><h3>${esc(c.label)}</h3><span class="badge">${lifetime[c.lifetime]}</span></div><p class="control-description">${esc(description(c))}</p><div class="control-tools"><select data-setting-choice="${c.id}" aria-label="${esc(c.label)}">${p.choices.map((x,i)=>`<option value="${x}" ${x===v?"selected":""}>${esc(p.choiceLabels?.[i]??(x===0?"Normal game choices":x+" choices"))}</option>`).join("")}</select><button class="quiet" data-setting-reset="${c.id}">Default</button></div><div class="limits"><span data-setting-feedback="${c.id}" role="status">${settingFeedback(c.id)}</span></div></article>`;
 return `<article class="control-card" data-setting="${esc(c.id)}"><div class="control-top"><span class="icon ${esc(c.id)}" aria-hidden="true"></span><div><div class="control-title">${esc(c.label)}</div></div><span class="badge">${lifetime[c.lifetime]}</span></div><p class="control-description">${esc(description(c))}</p><div class="control-tools">${isToggle?`<label class="toggle">${v?"On":"Off"}<input aria-label="${esc(c.label)}" type="checkbox" data-setting-toggle="${c.id}" ${v?"checked":""}></label>`:`<input type="range" aria-label="${esc(c.label)} slider" data-setting-range="${c.id}" min="${p.minimum}" max="${p.maximum}" step="${p.type==="integer"?1:0.1}" value="${v??p.minimum}"><div class="stepper"><button data-step="-1" data-setting-id="${c.id}" aria-label="${esc(c.label)} decrease">−</button><input aria-label="${esc(c.label)} value" data-setting-number="${c.id}" type="number" min="${p.minimum}" max="${p.maximum}" step="${p.type==="integer"?1:"any"}" value="${v??""}" placeholder="Rank" ${c.default===null?'':'required'}><button data-step="1" data-setting-id="${c.id}" aria-label="${esc(c.label)} increase">+</button></div><span class="unit">${unit}</span>`}<button class="quiet" data-setting-reset="${c.id}">Default</button></div><div class="limits">${isToggle?`Default: ${c.default?"on":"off"}`:`${p.minimum}–${p.maximum}${unit} · Default: ${c.default??"rank bonus"}`} <span class="setting-feedback" data-setting-feedback="${c.id}" role="status">${settingFeedback(c.id)}</span></div></article>`;
}
function settingPanel(title,controls){return `<section class="panel"><div class="section-head"><h2>${title}</h2><span class="badge">${controls.every(automaticSetting)?"Changes apply automatically":"Use Apply changes below"}</span></div><div class="control-grid">${controls.map(settingCard).join("")}</div></section>`;}
function actionCard(c, fields="", extra=""){
 const caption=c.id==="craft_materials"?"Stock all materials":c.id==="craft_forge"?"Craft once":c.id==="craft_reset"?"Restore game defaults":c.id==="mastery_choose"?"Choose Mastery":c.widget==="read"?"Read live":c.lifetime==="game_save"?"Apply action":c.widget==="preview"?"Preview":"Apply";
 return `<article class="control-card ${c.id==='loot_affixes'?'affix-card':''}"><div class="control-top"><h3 class="control-title">${esc(c.label)}</h3><span class="badge ${c.lifetime==="game_save"?"save":""}">${lifetime[c.lifetime]}</span></div><p class="control-description">${esc(description(c))}</p><form class="control-form" data-id="${c.id}"><div class="form-grid">${fields||parameterFields(c)}</div>${extra}<p class="action-reason" data-action-reason role="status" hidden></p><div class="actions"><button type="submit" data-control-id="${c.id}" class="${c.lifetime==="game_save"||c.id==="mastery_choose"?"primary":""}">${c.id==='loot_affixes'?'Apply selection':caption}</button></div></form></article>`;
}
function wantedAffixFields(){
 const owner=preview?'preview':automaticOwner();
 if(owner&&owner!==wantedAffixes.owner)affixPicker.receive(wantedAffixes,[],owner);
 const lists=affixPicker.lists(wantedAffixes,cache.loot_read?.affixes);
 return `<div class="affix-picker"><div class="affix-tools"><label>Find an affix<input type="search" data-affix-search value="${esc(wantedAffixes.query)}" placeholder="Search name or ID…"></label><label>Sort<select data-affix-sort><option value="az" ${wantedAffixes.order==='az'?'selected':''}>Name · A–Z</option><option value="za" ${wantedAffixes.order==='za'?'selected':''}>Name · Z–A</option></select></label></div><div class="affix-lists"><div><label>Available affixes <span data-affix-available-count>${lists.available.length}</span><select size="10" data-affix-list="available" aria-label="Available T7 affixes">${options(lists.available)}</select></label><button type="button" data-affix-add>Add selected →</button></div><div><label>Wanted affixes <span data-affix-selected-count>${lists.selected.length} / 64</span><select size="10" data-affix-list="selected" aria-label="Selected wanted T7 affixes">${options(lists.selected)}</select></label><div class="actions"><button type="button" data-affix-remove>← Remove selected</button><button type="button" data-affix-clear>Clear all</button></div></div></div><p class="note" data-affix-status role="status">${wantedAffixes.dirty?'Selection changed · use Apply selection.':wantedAffixes.selected.length?'Selection applied.':'No selection accepts any T7 affix.'} Double-click an affix or press Enter to move it. Search only filters the available list.</p></div>`;
}
function updateWantedAffixLists(){
 const lists=affixPicker.lists(wantedAffixes,cache.loot_read?.affixes);
 for(const side of ['available','selected']){const node=$(`[data-affix-list="${side}"]`);if(node)node.innerHTML=options(lists[side]);}
 $('[data-affix-available-count]').textContent=lists.available.length;
 $('[data-affix-selected-count]').textContent=lists.selected.length+' / 64';
 $('[data-affix-status]').textContent=wantedAffixes.dirty?'Selection changed · use Apply selection.':wantedAffixes.selected.length?'Selection applied.':'No selection accepts any T7 affix.';
}
function moveWantedAffixes(side,one){
 const select=$(`[data-affix-list="${side}"]`);
 const ids=one!==undefined?[one]:Array.from(select?.selectedOptions||[],x=>x.value);
 if(side==='available'){const known=new Set(affixPicker.lists(wantedAffixes,cache.loot_read?.affixes).available.map(x=>x.id));if(ids.some(x=>!known.has(String(x))))return;}
 if(!affixPicker.move(wantedAffixes,ids,side==='available')){notify('Choose at most 64 wanted affixes.',true);return;}
 updateWantedAffixLists();
}
function options(list,selected,none=false){const picked=Array.isArray(selected)?selected.map(String):[String(selected)];return (none?`<option value="none" ${selected==null?'selected':''}>Clear selection (none)</option>`:"")+list.map(x=>`<option value="${esc(x.id)}" ${picked.includes(String(x.id))?"selected":""}>${esc(clean(x.uiLabel||x.name||x.id))}</option>`).join("");}
function workshopModifierEntries(){
 const meta=catalog.characterStats||{};
 const source=cache.craft_custom_catalog?.modifiers||[];
 if(workshopEntryCache.source===source&&workshopEntryCache.meta===meta&&workshopEntryCache.perfect===workshopDraft.perfect)return workshopEntryCache.entries;
 const names=value=>String(value||"").replace(/([a-z])([A-Z])/g,"$1 $2");
 const entries=source.map(x=>{
  const stat=names(meta.properties?.find(p=>p.id===x.property)?.name||"Modifier "+x.property);
  const tags=(meta.tags||[]).filter(t=>t.id>0&&(x.tags&t.id)===t.id).map(t=>names(t.name));
  const ailment=names(meta.ailments?.find(a=>a.id===x.special&&a.id!==0)?.name);
  const value=workshopDraft.perfect==="0"?(Number(x.value)+Number(x.maximum))/2:Number(x.maximum);
  const amount=x.mode===0?Number(value.toPrecision(6)):Number((value*100).toPrecision(6))+"% "+(x.mode===1?"increased":"more");
  return {...x,name:[x.source,stat,...tags,ailment,x.extra?"Variant "+x.extra:"",amount].filter(Boolean).join(" · ")};
 });
 workshopEntryCache={source,meta,perfect:workshopDraft.perfect,entries};return entries;
}
function workshopModifierOptions(){
 const selected=workshopDraft.modifiers||[],query=workshopModifierFilter.trim().toLowerCase(),list=workshopModifierEntries();
 return options(list.filter(x=>selected.includes(String(x.id))||!query||x.name.toLowerCase().includes(query)),selected);
}
function craftingControl(id,label,text){return {...controlById(id),label,description:text};}
function glyphChanceCard(id,title,text){
 const value=values[id]===undefined?(cache.craft_read?.[id==='craft_hope'?'hopePercent':'despairPercent']??null):values[id];
 return `<article class="control-card" data-setting="${id}"><div class="control-top"><h3>${title}</h3><span class="badge">Session</span></div><p class="control-description">${text}</p><label class="craft-mode-label">Chance<select data-craft-glyph-mode="${id}"><option value="default" ${value==null?'selected':''}>Game default</option><option value="custom" ${value!=null?'selected':''}>Custom percentage</option></select></label>${value==null?'<p class="craft-default">Uses the game’s normal chance.</p>':`<div class="control-tools"><input type="range" min="0" max="100" step="1" value="${esc(value)}" data-setting-range="${id}" aria-label="${title} slider"><div class="stepper"><input type="number" min="0" max="100" step="any" value="${esc(value)}" required data-setting-number="${id}" aria-label="${title} percentage"></div><span class="unit">%</span></div>`}<div class="limits"><span data-setting-feedback="${id}" role="status">${settingFeedback(id)}</span></div></article>`;
}
function workshopPicker(kind){
 const entries=kind==='affixes'?(cache.craft_custom_catalog?.affixes||[]).map(x=>({...x,name:clean(x.name)})):workshopModifierEntries();
 const matches=craftingModel.filtered(entries,workshopFilters[kind]),selected=workshopDraft[kind],shown=matches.slice(0,workshopListLimits[kind]);
 return `<div class="craft-pick-count">${matches.length} matches · ${selected.length} selected</div><div class="craft-pick-list">${shown.map(x=>`<label><input type="checkbox" data-workshop-pick="${kind}" value="${esc(x.id)}" ${selected.includes(String(x.id))?'checked':''}><span>${esc(x.name)}</span></label>`).join('')||`<p class="muted">${cache.craft_custom_catalog?.items?.length?'No matches. Try another search.':'Load item choices above to see the available options.'}</p>`}</div>${matches.length>shown.length?`<button type="button" class="quiet" data-workshop-more="${kind}">Show more (${shown.length} of ${matches.length})</button>`:''}`;
}
function workshopChips(kind){
 const entries=kind==='affixes'?(cache.craft_custom_catalog?.affixes||[]):workshopModifierEntries();
 return workshopDraft[kind].map(id=>`<button type="button" class="craft-chip" data-workshop-remove="${kind}" data-choice="${esc(id)}" aria-label="Remove ${esc(clean(entries.find(x=>String(x.id)===id)?.name||id))}">${esc(clean(entries.find(x=>String(x.id)===id)?.name||id))}<span aria-hidden="true"> ×</span></button>`).join('')||`<span class="muted">${kind==='affixes'?'No replacements selected. Current affixes are kept.':'No extra effects selected.'}</span>`;
}
function workshopTemplateOptions(){
 const entries=(cache.craft_custom_catalog?.items||[]).map(x=>({...x,uiLabel:(x.set?'Set · ':'Unique · ')+clean(x.name)})),matches=craftingModel.filtered(entries,workshopFilters.template,workshopTemplateKind);
 const selected=entries.find(x=>String(x.id)===workshopDraft.template_id);
 if(selected&&!matches.includes(selected))matches.unshift(selected);
 return `<option value="-1" ${workshopDraft.template_id==='-1'?'selected':''}>Exalted · no Unique / Set template</option>`+options(matches,workshopDraft.template_id);
}
function workshopSummary(){
 const summary=craftingModel.summary(workshopDraft,cache.craft_custom_catalog),problem=craftingModel.problem(workshopDraft,cache.craft_custom_catalog,cache.craft_read?.forgePreview);
 return `<dl class="craft-summary">${Object.entries({Result:summary.template,Affixes:summary.affixes,'Extra effects':summary.effects,Quality:summary.rolls}).map(([k,v])=>`<dt>${k}</dt><dd>${esc(clean(v))}</dd>`).join('')}</dl>${problem?`<p class="action-reason">${esc(problem)}</p>`:''}${workshopDraft.modifiers.length?'<p class="craft-portability">Extra effects require EpochPact and <strong>workshop-recipes.txt</strong> when reloading or moving this item to another PC. Keep that file with your saves.</p>':''}`;
}
function updateWorkshopUi(){
 const focused=document.activeElement,focusKind=focused?.dataset?.workshopPick,focusValue=focused?.value;
 for(const kind of ['affixes','modifiers']){
  const list=$(`[data-workshop-list="${kind}"]`),chips=$(`[data-workshop-chips="${kind}"]`);
  if(list)list.innerHTML=workshopPicker(kind);if(chips)chips.innerHTML=workshopChips(kind);
 }
 const summary=$('#workshop-summary');if(summary)summary.innerHTML=workshopSummary();updateDisabled();
 if(focusKind)Array.from(document.querySelectorAll('[data-workshop-pick]')).find(x=>x.dataset.workshopPick===focusKind&&x.value===focusValue)?.focus();
}
function customWorkshop(){
 const forge=cache.craft_read?.forgePreview,loaded=!!cache.craft_custom_catalog?.items?.length;
 return `<section class="panel craft-workshop"><div class="section-head"><h2>Custom Items</h2><button data-refresh="craft_custom_catalog" data-control-id="craft_custom_catalog">${loaded?'Refresh item choices':'Load item choices'}</button></div><p class="muted">Keep your item’s equipment base. Choose its template and affixes, then review the result before creating it.</p><form class="control-form" data-id="craft_custom"><section class="craft-step"><h3><span>1</span>Select the item in the game</h3><p>Open the game’s forge and place the equipment item you want to change.</p><div class="craft-item-state"><span>${forge?.canCustom?`Equipment selected · ${forge.forgingPotential??'—'} Forging Potential · ${forge.affixes?.length??0} affixes`:'Waiting for an equipment item in the game’s forge.'}</span><button type="button" data-refresh="craft_read" data-control-id="craft_read">Refresh selected item</button></div></section><section class="craft-step"><h3><span>2</span>Choose a template</h3><p>Use Exalted for affixes only, or copy one Unique / Set template onto this base.</p><div class="craft-template-search"><label>Find a template<input type="search" data-workshop-search="template" value="${esc(workshopFilters.template)}" placeholder="Search item names..."></label><label>Type<select data-workshop-kind><option value="all" ${workshopTemplateKind==='all'?'selected':''}>All templates</option><option value="unique" ${workshopTemplateKind==='unique'?'selected':''}>Unique</option><option value="set" ${workshopTemplateKind==='set'?'selected':''}>Set</option></select></label></div><label>Template<select name="template_id" data-workshop-template>${workshopTemplateOptions()}</select></label></section><section class="craft-step"><h3><span>3</span>Choose T7 affixes</h3><p>Click to add or remove an affix. Choose up to ${workshopDraft.modifiers.length?6:7}; leave all unchecked to keep the item’s current affixes.</p><label>Find an affix<input type="search" data-workshop-search="affixes" value="${esc(workshopFilters.affixes)}" placeholder="Search health, damage, resistance..."></label><div data-workshop-list="affixes">${workshopPicker('affixes')}</div><div class="craft-chips" data-workshop-chips="affixes">${workshopChips('affixes')}</div></section><details class="craft-step craft-extra" data-workshop-advanced ${workshopAdvanced?'open':''}><summary>Add extra Unique / Set effects <span>Advanced · optional</span></summary><p>Mix individual effects from different items. Choose up to 64 lines. They share one affix slot, so only six regular affixes can be selected.</p><label>Find an effect<input type="search" data-workshop-search="modifiers" value="${esc(workshopFilters.modifiers)}" placeholder="Search a source item or effect..."></label><div data-workshop-list="modifiers">${workshopPicker('modifiers')}</div><div class="craft-chips" data-workshop-chips="modifiers">${workshopChips('modifiers')}</div></details><section class="craft-step"><h3><span>4</span>Review and create</h3><label>Roll quality<select name="perfect"><option value="1" ${workshopDraft.perfect==='1'?'selected':''}>Maximum rolls</option><option value="0" ${workshopDraft.perfect==='0'?'selected':''}>Midpoint rolls</option></select></label><p class="muted">Midpoint applies to newly chosen affixes and effects. Without extra effects, keeping current affixes preserves their tiers and midpoint mode keeps their existing rolls. Adding extra effects rebuilds kept affixes at T7 with the chosen rolls.</p><div id="workshop-summary">${workshopSummary()}</div><p class="muted">Replaces the selected item once and saves a recovery backup. This custom action does not consume crafting materials.</p><p class="action-reason" data-action-reason role="status" hidden></p><div class="actions"><button type="submit" class="primary" data-control-id="craft_custom">Create custom item</button></div></section></form></section>`;
}
function renderCrafting(){
 const forge=cache.craft_read?.forgePreview,stock=cache.craft_materials_read,stockCurrent=window.EpochSession.current(stock,connected,preview);
 const tabs=`<div class="craft-categories" role="group" aria-label="Crafting categories">${craftingModel.sections.map(x=>`<button type="button" data-craft-section="${x.id}" aria-pressed="${craftingSection===x.id}" aria-controls="craft-category-content"><strong>${x.label}</strong><span>${x.hint}</span></button>`).join('')}</div>`;
 const pending=[...dirty].filter(id=>id.startsWith('craft_')).length;
 const pendingNote=craftingSection!=='forge'&&pending?`<div class="note">${pending} Forge settings have unapplied changes. <button class="link-button" data-craft-section="forge">Review Forge Settings</button></div>`:'';
 let html;
 if(craftingSection==='materials'){
  const info=stockCurrent?`${stock.kinds?.shards??0} shard types · ${stock.kinds?.runes??0} rune types · ${stock.kinds?.glyphs??0} glyph types`:'Load an offline character and refresh to read your crafting stash.';
  html=`<section class="panel"><div class="section-head"><h2>Stock your materials</h2><button data-refresh="craft_materials_read" data-control-id="craft_materials_read">Refresh materials</button></div><p class="muted">Add shards, runes and glyphs to your saved crafting stash in one action.</p><p class="note">${esc(info)}</p>${actionCard(craftingControl('craft_materials','Minimum stock for every material','Raises each material to your chosen minimum. Higher quantities are kept. This adds a saved quantity once; it does not enable infinite materials.'))}<p class="muted">To keep materials while crafting, use Preserve materials in Forge Settings.</p></section>`;
 }else if(craftingSection==='custom')html=customWorkshop();
 else html=`<p class="note">These settings affect the game’s normal forge. Edit them here, then use <strong>Apply changes</strong> below. No item is crafted until you craft in the game or press Craft once.</p>`+
  settingPanel('Affix upgrade limit',[controlById('craft_t7')])+settingPanel('Preserve materials',[
   craftingControl('craft_shards','Keep affix shards','Keep the shard used for a craft. You need at least one to begin.'),
   craftingControl('craft_runes','Keep runes','Keep the rune used for a craft. You need at least one to begin.'),
   craftingControl('craft_glyphs','Keep glyphs','Keep the glyph used for a craft. You need at least one to begin.')])+settingPanel('Forging Potential cost',[craftingControl('craft_fp','Forging Potential cost','0 = no FP cost · 0.5 = half cost · 1 = normal cost. Does not add FP to an item.')])+
  `<section class="panel"><div class="section-head"><h2>Glyph chances</h2><span class="badge">Use Apply changes below</span></div><div class="control-grid">${glyphChanceCard('craft_hope','Glyph of Hope','Chance to prevent FP loss. Requires Basic Glyph of Hope.')}${glyphChanceCard('craft_despair','Glyph of Despair','Chance to seal an eligible T1–T4 affix. Other game requirements still apply.')}</div></section>`+
  `<details class="panel craft-advanced" data-craft-disclosure="advanced" ${craftDisclosure.advanced?'open':''}><summary>Advanced forge options</summary>${settingPanel('Level requirement',[craftingControl('craft_level','Ignore affix crafting level','Skip the selected affix upgrade’s level check. Equipment level requirements stay unchanged.')])}<div class="section-head"><h2>Craft from EpochPact</h2><button data-refresh="craft_read" data-control-id="craft_read">Refresh selected craft</button></div><p class="note">${esc(forge?.canForge?'The selected craft is ready.':forge?.message||'Select an item and a shard or rune in the game’s forge, then refresh.')}</p>${actionCard(controlById('craft_forge'))}${developer?actionCard(controlById('craft_read')):''}</details>`+
  `<details class="panel craft-advanced" data-craft-disclosure="defaults" ${craftDisclosure.defaults?'open':''}><summary>Restore game defaults</summary>${actionCard(craftingControl('craft_reset','Restore normal crafting','Reset all Forge settings. Crafted items and stocked materials are kept.'))}</details>`;
 return tabs+pendingNote+`<div id="craft-category-content">${html}</div>`;
}
function field(name,label,html){return `<label>${esc(label)}${html}</label>`;}
function parameterFields(c){
 if(c.id==='loot_affixes')return wantedAffixFields();
 return c.parameters.filter(p=>p.name!=="save_id").map(p=>{
  const name=p.name;let html;
  if(name==="timeline")html=`<select name="timeline">${options(cache.monolith_read?.timelines||[],monoTarget.timeline)}</select>`;
  else if(name==="difficulty")html=`<select name="difficulty"><option value="normal" ${monoTarget.difficulty==="normal"?"selected":""}>Normal</option><option value="empowered" ${monoTarget.difficulty==="empowered"?"selected":""}>Empowered</option></select>`;
  else if(name==="slot")html=`<select name="slot">${(cache.cof_read?.slots||[]).filter(x=>!x.locked).map(x=>`<option value="${x.index}">Slot ${x.index+1} · Rank ${x.rankRequired}</option>`).join("")}</select>`;
  else if(name==="reward")html=`<select name="reward">${options((cache.cof_read?.rewards||[]).filter(x=>x.available),cache.cof_read?.slots.find(s=>!s.locked)?.rewardId,true)}</select>`;
  else if(name==="lens")html=`<select name="lens">${options((cache.cof_read?.lenses||[]).filter(x=>x.purchased&&x.rankRequired<=cache.cof_read.cof.rank),cache.cof_read?.slots.find(s=>!s.locked)?.lens,true)}</select>`;
  else if(name==="backup")html=`<select name="backup">${history.map(x=>`<option value="${esc(x.backup)}">${esc(controlById(x.control)?.label)} · ${new Date(x.time*1000).toLocaleTimeString("en-GB")}</option>`).join("")}</select>`;
  else if(c.id==="craft_custom"&&name==="template_id")html=`<select name="template_id"><option value="-1" ${workshopDraft.template_id==="-1"?"selected":""}>Exalted · affixes only</option>${options((cache.craft_custom_catalog?.items||[]).map(x=>({...x,uiLabel:(x.set?"Set · ":"Unique · ")+x.name})),workshopDraft.template_id)}</select>`;
  else if(c.id==="craft_custom"&&name==="affixes")html=`<select name="affixes" multiple size="8">${options(cache.craft_custom_catalog?.affixes||[],workshopDraft.affixes)}</select><span class="note">Choose up to seven T7 affixes. No selection keeps the current affixes.</span>`;
  else if(c.id==="craft_custom"&&name==="perfect")html=`<select name="perfect"><option value="1" ${workshopDraft.perfect==="1"?"selected":""}>Maximum rolls</option><option value="0" ${workshopDraft.perfect==="0"?"selected":""}>Midpoint rolls</option></select>`;
  else if(c.id==="craft_custom"&&name==="modifiers")html=`<input type="search" data-workshop-filter placeholder="Search item names or modifier effects..." value="${esc(workshopModifierFilter)}"><select name="modifiers" multiple size="10">${workshopModifierOptions()}</select><span class="note">Mix up to 64 individual Unique and Set modifier lines. Uses one affix slot: select at most six regular affixes. Mixed items require EpochPact and its workshop-recipes.txt file after restarting or moving to another PC.</span>`;
  else if(c.id==="craft_materials"&&name==="value")html=`<input name="value" type="number" min="${p.minimum}" max="${p.maximum}" step="1" value="${esc(materialTarget)}" required><span class="note">Higher existing quantities are kept. Range: 1–1,000,000 per material.</span>`;
  else if(p.choices){const selected=c.id==="loot_mode"?cache.loot_read?.mode??c.default:null;html=`<select name="${name}">${p.choices.map(x=>`<option value="${x}" ${x===selected?"selected":""}>${x}</option>`).join("")}</select>`;}
  else if(c.id==="loot_affixes")html=`<select name="ids" multiple size="8" aria-label="Wanted T7 affixes">${options(cache.loot_read?.affixes||[],cache.loot_read?.affixIds||[])}</select><span class="note">Refresh pickup rules to load affix names. Hold Ctrl to select several; no selection accepts any T7.</span>`;
 else {const liveDefault=name==="value"?({craft_hope:cache.craft_read?.hopePercent,craft_despair:cache.craft_read?.despairPercent,cof_rank:cache.cof_read?.cof?.rank,cof_favor:cache.cof_read?.cof?.favor,cof_charges:cache.cof_read?.slots?.find(s=>!s.locked)?.charges}[c.id]??c.default):c.default;html=`<input name="${name}" ${p.type==="single_token"?'type="text"':'type="number"'} ${p.minimum!=null?`min="${p.minimum}"`:""} ${p.maximum!=null?`max="${p.maximum}"`:""} step="${p.type.includes("integer")?"1":"any"}" ${liveDefault!=null?`value="${liveDefault}"`:""} ${p.type==="number_or_reset"?'placeholder="Normal chance (blank resets)"':""} ${["optional_integer","number_or_reset"].includes(p.type)?"":"required"}>`;}
  return field(name,c.id==="craft_materials"&&name==="value"?"Minimum quantity per material":{value:"Value",name:"Character name",level:"Level (optional)",slot:"Prophecy slot",reward:"Reward",lens:"Lens",backup:"Operation backup",mode:"Selection",ids:"Affixes",category:"Pickup category",template_id:"Whole Unique / Set template",affixes:"T7 affixes",perfect:"Roll quality",modifiers:"Additional modifier lines"}[name]||name,html);
 }).join("");
}
function cachedSummary(id){
 const d=cache[id];if(!window.EpochSession.current(d,connected,preview))return '<div class="note">Refresh live data. Load an offline character, then refresh to see current values.</div>';
 const p=d.player;return `<div class="note">${preview?"Catalog snapshot · ":"Live reading · "}${esc(p?.name)}${developer?` · Save ${esc(p?.id)}`:""}${p?.level!=null?` · Level ${p.level}`:""}${developer&&p?.scene?` · ${esc(p.scene)}`:""}${d.editable===false?' · Monolith editing is unavailable in this area.':''}</div>`;
}
function navigation(){
 const ids=preferencesApi?preferencesApi.visiblePages(Object.keys(pages),displayPreferences,page):Object.keys(pages);
 $("#nav").innerHTML=ids.map(id=>{const p=pages[id];return `<a class="nav-item" href="#${id}" ${page===id?'aria-current="page"':''}><span class="nav-symbol" aria-hidden="true">${displayPreferences.favorites.includes(id)?"★":p[1]}</span>${p[0]}</a>`;}).join("")+`<a class="nav-customize" href="#settings">Customize navigation</a>`;
}
function go(id){
 if(!(id in pages))id="overview";
 $("#global-search").value="";
 if(location.hash==="#"+id)void openPage();else location.hash=id;
}
function pageReadId(){return {general:"build_read",display:"qol_read",world:"qol_read",inventory:"inventory_read",skills:"build_read",equipment:"build_read",summons:"build_read",loot:"loot_read",crafting:"craft_read",campaign:"progress_read",monolith:"monolith_read",cof:"cof_read",factions:"factions_read"}[page];}
// Each page reopens where it was left (Settings → Navigation; on by default).
const pageScroll={};let scrollPage=null;
function rememberScroll(){return readStored("remember-scroll",true)!==false;}
function restoreScroll(){const box=$("#main-scroll");if(!box)return;box.scrollTop=rememberScroll()?pageScroll[page]||0:0;scrollPage=page;}
async function openPage(){
 if(scrollPage&&$("#main-scroll"))pageScroll[scrollPage]=$("#main-scroll").scrollTop;
 page=location.hash.slice(1)||"overview";if(!(page in pages))page="overview";
 $("#global-search").value="";
 if(page==="atlas"||page==="stash"){collectionState.filter="all";collectionState.page=0;collectionState.selected=null;}
 statsPage=0;renderPage();restoreScroll();
 const readId=pageReadId();
 if(readId&&eligible(controlById(readId))&&(!cache[readId]||["general","loot","crafting","display","world","inventory"].includes(page)))await refreshSection(readId);
 if(page==="loot"&&!preview)await refreshSection("pickup_radius_read",{refreshConnectionFirst:false});
 if(page==="display")await refreshSection("build_read",{refreshConnectionFirst:false});
 if(page==="settings"&&!preview&&connected.connected&&controlById("report_read"))await refreshSection("report_read",{refreshConnectionFirst:false});
 if(page==="summons"&&!preview&&connected.connected&&controlById("damage_session_read")){const r=await job({type:"control",id:"damage_session_read",operation:"read"}).catch(()=>null);if(r?.ok){cache.damage_session_read=r;renderDamage();}}
 if(page==="world")await refreshSection("map_read",{refreshConnectionFirst:false});
 if(page==="crafting"&&!preview&&eligible(controlById("craft_materials_read")))await refreshSection("craft_materials_read",{refreshConnectionFirst:false});
 if(["general","monolith"].includes(page)&&!cache.build_read&&controlById("build_read")&&eligible(controlById("build_read")))await refreshSection("build_read",{refreshConnectionFirst:false});
 if(rememberScroll()&&scrollPage===page&&pageScroll[page]&&$("#main-scroll").scrollTop<pageScroll[page])$("#main-scroll").scrollTop=pageScroll[page];
}
function renderPage(){
 navigation();const p=pages[page];$("#breadcrumb").textContent="EpochPact / "+p[0];$("#page-title").textContent=p[2];$("#page-subtitle").textContent=p[3];
 if(page==="overview"){
  content.innerHTML=`<div class="summary-grid"><div class="summary"><strong>${catalog.controls.filter(playerControl).length}</strong><span>Available tools</span></div><div class="summary"><strong>${catalog.counts.sheetRows}</strong><span>Character bonuses</span></div><div class="summary"><strong>${catalog.counts.timelines}</strong><span>Monolith timelines</span></div><div class="summary"><strong>${catalog.counts.statAliases}</strong><span>Common stats</span></div></div>`+settingPanel("Quick controls",["xp","speed","drops","gold"].map(controlById))+`<section class="panel"><h2>Explore your workshop</h2><div class="control-grid">${["stats","loot","crafting","campaign","monolith","cof"].map(id=>`<button data-go="${id}" class="settings-card"><h3>${pages[id][1]} ${pages[id][0]}</h3><p class="muted">${pages[id][3]}</p></button>`).join("")}</div></section>`;
 }else if(page==="game_setup"){
  content.innerHTML='';
 }else if(page==="general"){
  content.innerHTML=settingPanel("General game settings",catalog.controls.filter(c=>c.group==="general"&&["number","toggle"].includes(c.widget)&&!c.id.startsWith("loot_")&&!c.id.startsWith("craft_")&&!c.id.startsWith("map_")));
 }else if(page==="display"){
  content.innerHTML=settingPanel("Camera",["build_zoom","build_cameraangle"].map(controlById))+settingPanel("Minimap and overlay",["map_minimapsize","map_minimapzoom","map_minimapopacity","map_overlayopacity","map_overlayzoom"].map(controlById))+settingPanel("Item tiers",["tier_icons","tier_tooltips","tier_lines","tier_rules","tier_rule_position","tier_rule_plain"].map(controlById))+tierPalette()+fontPanel()+settingPanel("Graphics effects",catalog.controls.filter(c=>c.group==="display"&&c.id.startsWith("qol_")))+`<p class="note">Graphics overrides last for this character session. Turning them off restores the previous renderer values. Your saved graphics preferences are kept.</p>`;
 }else if(page==="world"){
  content.innerHTML=settingPanel("Map visibility",[controlById("map_reveal")])+settingPanel("Nearby interactions",["qol_chests","qol_shrines","qol_portals"].map(controlById));
 }else if(page==="inventory"){
  content.innerHTML=`<section class="panel"><div class="section-head"><h2>Inventory shortcuts</h2><button data-refresh="inventory_read" data-control-id="inventory_read">Refresh inventory</button></div><p class="note">${esc(cache.inventory_read?.inventoryItems!=null?`${cache.inventory_read.inventoryItems} inventory items. Stash all uses the game's placement: tabs whose stash priority matches an item first, otherwise the selected tab; items that do not fit stay in your inventory.`:"Refresh inventory for the loaded character.")}</p><div class="control-grid">${["inventory_stash","inventory_vendor","inventory_stashall"].map(id=>actionCard(controlById(id))).join("")}</div></section>`+stashRulesPanel();
 }else if(page==="loot"){
  content.innerHTML=settingPanel("Automatic collection",[controlById("autopickup"),controlById("pickup_radius"),controlById("pickup_interval")])+lootPanel()+`<section class="panel"><h2>Collect now</h2>${cache.pickup_radius_read?.inventoryFull?`<p class="note">Your inventory is full. Equipment pickup is paused until you open and close your inventory; gold, potions, tomes, bones and materials are still collected.</p>`:""}<div class="control-grid">${actionCard(controlById("pickup_now"))}</div></section>`+settingPanel("Quality rules",["loot_lp","loot_t7","loot_filter"].map(controlById))+filterFilesPanel()+`<section class="panel"><h2>Pickup selections</h2><div class="control-grid">${["loot_mode","loot_category","loot_affixes"].map(id=>actionCard(controlById(id))).join("")}</div></section><section class="panel"><div class="section-head"><h2>Pickup rules</h2><button data-refresh="loot_read" data-control-id="loot_read">Refresh rules</button></div><div class="note">Current selection: ${esc({all:"All items",filter:"Game loot filter",quality:"LP and T7 quality rules",materials:"Crafting materials"}[cache.loot_read?.mode]||"Refresh to read your rules")}.</div>${developer?actionCard(controlById("loot_read"))+actionCard(controlById("pickup_radius_read")):""}${actionCard(controlById("loot_reset"))}</section>`;
 }else if(page==="crafting"){
  content.innerHTML=renderCrafting();
 }else if(page==="skills"){
  const ids=keys=>keys.map(key=>controlById("build_"+key));
  content.innerHTML=cachedSummary("build_read")+`<section class="panel"><div class="section-head"><h2>Skill and point budgets</h2><button data-refresh="build_read" data-control-id="build_read">Refresh live</button></div><div class="summary-grid"><div class="summary"><strong>${cache.build_read?.passivePoints??"—"}</strong><span>Passive budget</span></div><div class="summary"><strong>${cache.build_read?.weaverPoints??"—"}</strong><span>Weaver budget</span></div></div><p class="note">Point multipliers are session settings. Individual node caps still apply. Existing allocations stay when you lower a budget. Available points stop at zero until the budget covers those allocations.</p></section>`+settingPanel("Experience and skill points",ids(["skillxp","skillpoints","passivepoints"]))+settingPanel("Weaver points",ids(["weaverpoints","weaverflat"]))+settingPanel("Tree connections",ids(["skillnodes","passivenodes","weavernodes"]))+settingPanel("Projectiles",ids(["projectiles","projectileflat"]))+`<section class="panel"><h2>Free respec</h2><p class="note">Refund points from the selected tree group. Levels and earned budgets stay. Each action creates a recovery backup and runs only after confirmation.</p><div class="control-grid">${["respec_skills","respec_passives","respec_weaver"].map(id=>actionCard(controlById(id))).join("")}</div></section>`;
 }else if(page==="equipment"){
  content.innerHTML=cachedSummary("build_read")+settingPanel("Equipment rules",["offhand","fullsets"].map(key=>controlById("build_"+key)));
 }else if(page==="summons"){
  const aliases=catalog.characterStats.aliases;
  const summonStats=aliases.map((row,index)=>({row,index})).filter(x=>/^minion|^maxcompanions/.test(x.row.name));
  content.innerHTML=damagePanel()+`<section class="panel"><div class="section-head"><h2>Live summoned allies</h2><button data-refresh="summons_read" data-control-id="summons_read">Refresh allies</button></div>${summonColumnPicker()}<div class="actions"><button id="summon-baseline">Save damage baseline</button><button id="summon-baseline-clear">Clear baseline</button></div><div id="summon-live">${summonTable()}</div></section>`+settingPanel("Summon lifetime",[controlById("build_summonduration")])+`<section class="panel"><h2>Minion and companion bonuses</h2><p class="muted">These controls use the existing character stat editor. Minion tags keep these bonuses separate from your own damage and health.</p><div class="control-grid">${summonStats.map(x=>`<button class="settings-card" data-summon-stat="${x.index}"><h3>${esc(ST.name(x.row,"aliases"))}</h3><p class="muted">Edit bonus →</p></button>`).join("")}</div></section>`;
 }else if(page==="stats")renderStats();
 else if(page==="campaign"){
  const mastery=window.EpochSession.current(cache.progress_read,connected,preview)?cache.progress_read?.mastery:null;
  const masteryStatus=mastery?.available?"Your character has no mastery yet. Open the selection screen, then choose your mastery in the game.":mastery?.reason||"Refresh live to check mastery selection for your character.";
  content.innerHTML=cachedSummary("progress_read")+`<section class="panel"><div class="section-head"><h2>Campaign and waypoints</h2><button data-refresh="progress_read" data-control-id="progress_read">Refresh live</button></div><div class="control-grid">${["campaign_complete","waypoints_unlock"].map(id=>actionCard(controlById(id))).join("")}</div><details><summary>Quest rewards and coverage (${catalog.counts.eligibleCampaignQuests} quests / ${catalog.counts.waypoints} waypoints)</summary><p class="muted">Completion grants normal rewards and raises the character to at least level 55. Completed quests do not grant rewards again. Test, repeatable and endgame quests are excluded.</p>${questTable()}</details></section><section class="panel"><h2>Mastery</h2><p class="note">${esc(masteryStatus)}</p>${actionCard(controlById("mastery_choose"))}</section>`;
 }else if(page==="atlas"||page==="stash")renderCollection();
 else if(page==="monolith")renderMonolith();
 else if(page==="cof")renderCof();
 else if(page==="factions"){
  const factions=cache.factions_read?.factions||[];
  content.innerHTML=`<section class="panel"><div class="section-head"><h2>Factions</h2><button data-refresh="factions_read" data-control-id="factions_read">Refresh live</button></div><div class="note">Merchant’s Guild and Weaver management actions are not implemented. This section reads information only.</div><div class="control-grid">${factions.map(f=>`<article class="control-card"><h3>${esc(f.name)}</h3><span class="badge">Information only</span><p class="muted">${f.member?"Member":"Not a member"} · Rank ${f.rank} · Favor ${f.favor} · Reputation ${f.reputation}</p>${f.weaverPoints?`<p>Weaver points: ${f.weaverPoints.earned} / ${f.weaverPoints.maxTotal} (${f.weaverPoints.maxRank} rank + ${f.weaverPoints.maxEchoes} echo)</p>`:""}<details><summary>Rank rewards</summary>${f.ranks.map(r=>`<p><b>${r.index+1} · ${esc(r.title)}</b><br>${esc(clean(r.description))}</p>`).join("")}</details></article>`).join("")||'<p class="empty">Live faction information has not been read yet.</p>'}</div>${developer?`<details><summary>Management features in development</summary>${catalog.researchOnly.map(r=>`<p><b>${esc(r.label)}</b><br>${esc(r.details||r.availability)}</p>`).join("")}</details>`:""}</section>`;
 }else renderSettings();
 if(page==="monolith"){content.insertAdjacentHTML("beforeend",navigatorPanel());renderEchoResults();}
 if(page==="stash")content.insertAdjacentHTML("beforeend",stashSearchHelp());
 window.EpochLauncher?.mount(page);
 updateDisabled();
}
function questTable(){
 const live=cache.progress_read;const quests=live?.quests?.filter(x=>x.eligible)||catalog.optionCatalogs.campaignQuests;
 return `<p class="muted">${live?"Live quest data":"Static coverage catalog"}</p><div class="table-wrap"><table><thead><tr><th>Quest</th><th>Type</th><th>XP</th><th>Gold</th><th>Passive</th><th>Idol</th><th>Attribute</th></tr></thead><tbody>${quests.map(q=>`<tr><td>${esc(q.name)}</td><td>${q.main?"Main":"Side"}</td><td>${q.xp}</td><td>${q.gold}</td><td>${q.passive}</td><td>${q.idol}</td><td>${q.attributes}</td></tr>`).join("")}</tbody></table></div>`;
}
function monolithDensityPanel(){
 const density={...controlById("density"),label:"Monolith monster density",
  description:"Increases the average size of new regular enemy packs in Normal and Empowered Echoes. Apply before entering the next Echo; existing packs and bosses are unchanged."};
 return settingPanel("Echo modifiers",[density,controlById("stability_multiplier")])+`<p class="note">Monster density is shared with General settings and applies to eligible packs in other combat areas too. ×1 uses normal pack sizes. Apply before entering the next Echo; restarting the game resets these session settings.</p>`;
}
function renderMonolith(){
 const live=cache.monolith_read, timelines=live?.timelines||[];
 if(monoTarget.timeline==null&&timelines.length)monoTarget.timeline=live.selected?.timeline||timelines[0].id;
 const t=timelines.find(x=>x.id===Number(monoTarget.timeline));const d=t?.difficulties.find(x=>x.index===(monoTarget.difficulty==="empowered"?1:0));
 const targetFields=`<input type="hidden" name="timeline" value="${monoTarget.timeline||""}"><input type="hidden" name="difficulty" value="${monoTarget.difficulty}">`;
 function absolute(id,label,max,min,current){const draft=monoDrafts[monoDraftKey(id)];return actionCard(controlById(id),targetFields+field("value",`${label} · ${min??"?"}–${max??"?"}`,`<input name="value" type="number" step="1" ${min!=null?`min="${min}"`:""} ${max!=null?`max="${max}"`:""} value="${esc(draft??current??"")}" required>`));}
 content.innerHTML=cachedSummary("monolith_read")+monolithDensityPanel()+settingPanel("Boss blessing rewards",["blessingoptions","blessingrolls"].map(key=>controlById("build_"+key)))+`<section class="panel"><div class="section-head"><h2>Monolith management</h2><button data-refresh="monolith_read" data-control-id="monolith_read">Refresh live</button></div><p class="muted">Opening a timeline or showing an Echo travels to Traveler's Rest first. Refreshing data does not travel or start an Echo.</p><div class="form-grid"><label>Timeline<select id="timeline-target">${options(timelines,monoTarget.timeline)}</select></label><label>Difficulty<select id="difficulty-target"><option value="normal" ${monoTarget.difficulty==="normal"?"selected":""}>Normal</option><option value="empowered" ${monoTarget.difficulty==="empowered"?"selected":""}>Empowered</option></select></label></div><p class="muted">${d?`Level ${d.level} · ${d.unlocked?"Unlocked":"Locked"} · Corruption ${d.minCorruption}–${d.maxCorruption} · Stability 0–${d.maxStability}`:"Refresh the live timeline catalog."}</p><div class="control-grid">${actionCard(controlById("monolith_unlock"))}${actionCard(controlById("monolith_select"),targetFields)}${absolute("corruption","Corruption",d?.maxCorruption,d?.minCorruption,d?.run?.corruption)}${absolute("stability","Stability",d?.maxStability,0,d?.run?.stability)}</div></section>`;
}
function renderCof(){
 const live=cache.cof_read,cof=live?.cof;
 content.innerHTML=cachedSummary("cof_read")+`<section class="panel"><div class="section-head"><h2>Circle of Fortune</h2><button data-refresh="cof_read" data-control-id="cof_read">Refresh live</button></div>${cof?`<div class="summary-grid"><div class="summary"><strong>${cof.rank}</strong><span>Rank / ${live.maxRank}</span></div><div class="summary"><strong>${cof.favor.toLocaleString("en-US")}</strong><span>Favor balance</span></div><div class="summary"><strong>${cof.reputation.toLocaleString("en-US")}</strong><span>Reputation within rank</span></div><div class="summary"><strong>${cof.member?"Member":"—"}</strong><span>Circle of Fortune</span></div></div>`:""}<div class="control-grid">${actionCard(controlById("cof_join"),"",'<label class="inline-check"><input type="checkbox" name="switch_from_merchant"> Leave Merchant’s Guild and join CoF</label>')}${["cof_rank","cof_favor","cof_reputation_grant","cof_lenses_unlock"].map(id=>actionCard(controlById(id))).join("")}</div></section><section class="panel"><h2>Prophecies and lenses</h2><p class="muted">Slots unlock at ranks 1 / 3 / 6 / 9. Choose “none” to clear a reward or lens. Changing a reward resets its charges.</p>${live?`<div class="table-wrap"><table><thead><tr><th>Slot</th><th>Reward</th><th>Lens</th><th>Charges</th><th>Status</th></tr></thead><tbody>${live.slots.map(s=>`<tr><td>${s.index+1}</td><td>${esc(clean(live.rewards.find(r=>r.id===s.rewardId)?.uiLabel||live.rewards.find(r=>r.id===s.rewardId)?.name||live.rewards.find(r=>r.id===s.rewardId)?.label||"None"))}</td><td>${esc(clean(live.lenses.find(l=>l.id===s.lens)?.uiLabel||live.lenses.find(l=>l.id===s.lens)?.name||"None"))}</td><td>${s.charges}</td><td>${s.locked?"Locked":"Unlocked"}</td></tr>`).join("")}</tbody></table></div>`:""}<div class="control-grid">${actionCard(controlById("cofprophecy"),"",'<div class="actions"><button type="button" data-prophecy-preview data-control-id="cofpreview">Preview changes</button></div>')}${actionCard(controlById("cof_charges"))}</div><details><summary>Lens catalog · ${catalog.counts.lenses} lens</summary>${(live?.lenses||catalog.optionCatalogs.lenses).map(l=>`<p><b>${esc(clean(l.uiLabel||l.name))}</b> · Rank ${l.rankRequired}${live?l.purchased?" · Purchased":" · Not purchased":" · Static catalog"}<br>${esc(clean(l.uiEffect||l.effect))}</p>`).join("")}</details></section>`+settingPanel("Gain, drop and quality multipliers",catalog.controls.filter(c=>c.group==="cof"&&c.lifetime==="session"));
}
function statSource(){return statsTab==="sheet"?catalog.characterStats.sheetRows:statsTab==="aliases"?catalog.characterStats.aliases:catalog.characterStats.properties;}
function statName(row){return ST.name(row,"sheet");}
function renderStats(){
 const source=statSource(),categories=["All",...new Set(source.map(r=>ST.category(r,statsTab)))];
 if(!categories.includes(statsCategory))statsCategory="All";
 content.innerHTML=`<section class="panel stat-browser"><div class="section-head"><div><h2>Character bonuses</h2><p class="muted">Choose a stat, adjust your bonus, then apply.</p></div><button class="quiet" data-control-id="stat_reset" id="all-stats-reset">Reset all bonuses</button></div><div class="tabs">${[["sheet","Character sheet",catalog.counts.sheetRows],["aliases","Common stats",catalog.counts.statAliases],["raw",developer?"Advanced types":"All stats",catalog.counts.properties]].map(([id,name,count])=>`<button data-stat-tab="${id}" class="${statsTab===id?"selected":""}" aria-pressed="${statsTab===id}">${name}<small>${count}</small></button>`).join("")}</div><div class="filter-row"><input id="stat-search" aria-label="Search stats" placeholder="Find health, damage, resistance…" value="${esc(statsQuery)}"><button id="stat-favorites-filter" class="${statsFavoritesOnly?"selected":""}" aria-pressed="${statsFavoritesOnly}">★ Favorites</button></div><div class="stat-categories" role="group" aria-label="Stat categories">${categories.map(name=>`<button data-stat-category="${esc(name)}" aria-pressed="${name===statsCategory}" class="${name===statsCategory?"selected":""}">${esc(name)}</button>`).join("")}</div><div class="stat-results-head"><span id="stat-count"></span><span>☆ Save your most-used stats</span></div><div id="stat-results"></div></section>${developer?`<details class="panel stat-tools"><summary>Advanced game tools</summary><div class="control-grid">${["sheet_catalog","resistance_labels","sheet_open"].map(id=>actionCard(controlById(id))).join("")}</div><p class="muted">Sheet labels can be stale while the game's character panel is closed. Stat editing uses verified property keys.</p></details>`:""}`;
 renderStatResults();
}
function renderStatResults(){
 const rows=statSource().map((row,index)=>({row,index})).filter(({row})=>{
  const match=(ST.name(row,statsTab)+" "+ST.category(row,statsTab)+" "+JSON.stringify(row)).toLowerCase().includes(statsQuery.toLowerCase());
  return match&&(statsCategory==="All"||ST.category(row,statsTab)===statsCategory)&&(!statsFavoritesOnly||statFavorites.has(ST.favoriteId(row,statsTab)))&&(showHiddenStats||!displayPreferences.hiddenStats.includes(ST.favoriteId(row,statsTab)));
 });
 const totalPages=Math.max(1,Math.ceil(rows.length/24));statsPage=Math.min(statsPage,totalPages-1);
 $("#stat-count").textContent=rows.length+(rows.length===1?" stat":" stats");
 $("#stat-results").innerHTML=`<div class="actions"><button id="stat-hidden-filter" aria-pressed="${showHiddenStats}">${showHiddenStats?"Hide hidden stats":"Show hidden stats"} (${displayPreferences.hiddenStats.length})</button></div><div class="stat-list">${rows.slice(statsPage*24,statsPage*24+24).map(({row:r,index:i})=>{
  const name=ST.name(r,statsTab),favorite=statFavorites.has(ST.favoriteId(r,statsTab));
  const hidden=displayPreferences.hiddenStats.includes(ST.favoriteId(r,statsTab));
  return `<div class="stat-entry"><button class="stat-open" data-stat-index="${i}"><strong>${esc(name)}</strong><small>${ST.category(r,statsTab)}${r.derived?" · Calculated by the game":""}</small></button>${r.modifierKey?`<button class="secondary" data-stat-index="${i}" data-secondary="true" title="Edit the secondary modifier">Extra</button>`:""}<button class="stat-star ${favorite?"selected":""}" data-stat-favorite="${i}" aria-pressed="${favorite}" aria-label="${favorite?"Remove":"Add"} ${esc(name)} ${favorite?"from":"to"} favorites">${favorite?"★":"☆"}</button><button class="quiet" data-stat-hide="${i}" aria-label="${hidden?"Show":"Hide"} ${esc(name)}">${hidden?"Show":"Hide"}</button></div>`;
 }).join("")||`<div class="empty"><strong>${statsFavoritesOnly?"No matching favorites":"No matching stats"}</strong><p>${statsFavoritesOnly?"Use the star beside a stat to save it here.":"Try another name or choose All categories."}</p><button id="stat-clear-filters">Show all stats</button></div>`}</div><div class="pagination"><button data-stat-page="-1" ${statsPage===0?"disabled":""} aria-label="Previous stat page">←</button><span>${statsPage+1} / ${totalPages}</span><button data-stat-page="1" ${statsPage===totalPages-1?"disabled":""} aria-label="Next stat page">→</button></div>`;
}
function toggleStatFavorite(index){
 const id=ST.favoriteId(statSource()[index],statsTab);
 if(statFavorites.has(id))statFavorites.delete(id);else statFavorites.add(id);
 try{localStorage.setItem("epochpact.stat-favorites.v1",JSON.stringify([...statFavorites]));}catch{notify("Favorites could not be saved on this device.",true);}
 renderStatResults();
}
function statInfo(){return ST.unit(catalog.characterStats,activeStat.key,activeStat.mode,activeStat.tab==="aliases"?activeStat.row:null);}
function statLimits(){return ST.bounds(catalog.characterStats,activeStat.mode,statInfo(),activeStat.key);}
function statText(value,mode=activeStat.mode){
 const info=ST.unit(catalog.characterStats,activeStat.key,mode,activeStat.tab==="aliases"?activeStat.row:null);
 return ST.signed(value,info.suffix)+(mode==="increased"?" increased":mode==="more"?" more":"");
}
function renderStatControls(){
 const a=activeStat,info=statInfo(),bounds=statLimits(),input=$("#stat-value");
 $("#stat-mode").value=a.mode;
 const flatInfo=ST.unit(catalog.characterStats,a.key,"added",a.tab==="aliases"?a.row:null);
 const basic=a.modes.filter(m=>m!=="more");
 $("#stat-basic-modes").innerHTML=basic.map(mode=>`<button type="button" data-bonus-mode="${mode}" aria-pressed="${mode===a.mode}" class="${mode===a.mode?"selected":""}">${mode==="added"?(flatInfo.points?"Add percentage points":"Flat bonus"):"Increase %"}</button>`).join("");
 input.min=bounds.minimum;input.max=bounds.maximum;input.step="any";
 $("#stat-range").min=bounds.sliderMin;$("#stat-range").max=bounds.sliderMax;$("#stat-range").step=bounds.step;
 $("#stat-value-unit").textContent=info.points?"pp":info.suffix;
 $("#stat-unit-help").textContent=a.mode==="more"?"A separate multiplier. +50% More multiplies this stat by ×1.5.":info.points?"Percentage points add directly: a 5% stat with +10 points becomes 15%, before game caps.":a.mode==="increased"?"Adds to this stat's existing percentage increases. Equipment bonuses still apply.":"Adds this amount to the stat. Zero removes this bonus amount.";
 $("#stat-range-help").textContent=`Slider ${bounds.sliderMin}–${bounds.sliderMax}${info.suffix}. You can type values outside this slider range.`;
 $("#stat-advanced-note").textContent="More applies a separate multiplier. Reset bonus removes all EpochPact bonuses affecting this same stat. The game still handles derived values and caps.";
 const current=a.current;
 $("#stat-current-value").textContent=current?ST.bonusSummary(catalog.characterStats,a.key,current)+(current.attached?"":" · Not active"):a.needsRead?"Refresh to read this key":a.readError?"Not available":"Reading…";
 if(a.gameValue!==undefined&&a.gameValue!==null)$("#stat-current-value").textContent+=` · In game: ${a.gameValue}`;
 updateStatPreview();updateDisabled();
}
function updateStatPreview(){
 if(!activeStat)return;
 const input=$("#stat-value"),value=input.value===""?NaN:Number(input.value),bounds=statLimits();
 $("#stat-preview").textContent=Number.isFinite(value)?statText(value)+" "+activeStat.name:"Enter a bonus";
 if(Number.isFinite(value))$("#stat-range").value=Math.max(bounds.sliderMin,Math.min(bounds.sliderMax,value));
}
function setStatValue(value,edited=true){
 const input=$("#stat-value");input.value=value;input.setCustomValidity("");
 if(edited)activeStat.drafts[activeStat.mode]=Number(value);
 updateStatPreview();
}
function changeStatMode(mode){
 const a=activeStat;if(!a||!a.modes.includes(mode))return;
 a.mode=mode;a.selectAppliedMode=false;
 setStatValue(a.drafts[mode]??(a.current?.modes[mode]||0)*statInfo().scale,false);
 renderStatControls();
}
function openStat(index,secondary=false){
 const row=statSource()[index],key=statsTab==="raw"?{sp:row.id,tags:0,special:0,extra:0,property:row.name}:secondary?row.modifierKey:row.key;
 const modes=key.sp===46?["added"]:statsTab==="aliases"?[row.mode]:row.modes||["added","increased","more"];
 const mode=modes.includes(ST.preferredMode(catalog.characterStats,row,key,statsTab))?ST.preferredMode(catalog.characterStats,row,key,statsTab):modes[0];
 const a={row,index,secondary,key:{...key},tab:statsTab,keyId:ST.keyId(key),name:ST.name(row,statsTab)+(secondary?" · Extra":""),mode,modes,drafts:{},current:null,readError:false,selectAppliedMode:true};
 activeStat=a;
 $("#stat-title").textContent=a.name;
 const shared=statsTab==="sheet"&&!secondary?catalog.characterStats.sheetRows.filter(r=>r.sharedKey===a.keyId&&r.objectName!==row.objectName).map(r=>statName(r)):[];
 $("#stat-note").textContent=shared.length?"Shares its bonus with "+shared.slice(0,3).join(", ")+(shared.length>3?" and other linked stats.":"."):"Customize this stat without changing your equipment.";
 $("#stat-mode").innerHTML=modes.map(m=>`<option value="${m}">${m==="added"?"Flat / direct bonus":m==="increased"?"Increase %":"More % · separate multiplier"}</option>`).join("");
 $("#stat-key").textContent=`SP ${key.sp} · tags ${key.tags} · special ${key.special} · extra ${key.extra}`;
 if(developer&&statsTab==="raw")$("#stat-key").innerHTML=`<div class="form-grid">${["tags","special","extra"].map(name=>{const p=controlById("raw_stat").parameters.find(p=>p.name===name);return field(name,name,`<input id="raw-${name}" type="number" step="1" min="${p.minimum}" max="${p.maximum}" value="0" required>`);}).join("")}</div><details><summary>Tag and ailment reference</summary><p>Some properties use indices instead of tag flags. Use the catalog's exact keys.</p><div class="table-wrap"><table><tbody>${catalog.characterStats.tags.map(t=>`<tr><td>${esc(t.name)}</td><td>${esc(t.value??t.id)}</td></tr>`).join("")}</tbody></table></div><select aria-label="Ailment reference">${catalog.characterStats.ailments.map(x=>`<option>${esc(x.name)} · ${x.id}</option>`).join("")}</select></details>`;
 $("#stat-live").textContent="Live bonus has not been read yet.";
 $("#stat-advanced").open=false;setStatValue(0,false);renderStatControls();$("#stat-dialog").showModal();
 void (async()=>{while(busy&&activeStat===a&&$("#stat-dialog").open)await sleep(100);if(activeStat===a&&$("#stat-dialog").open)await statAction("read",a);})();
}
function statPayload(operation){
 const a=activeStat,args={mode:a.mode,unit:statInfo().unit};
 if(operation==="set")args.value=Number($("#stat-value").value);
 let id;
 if(a.tab==="aliases"){id="stat_alias";delete args.mode;args.name=a.row.name;}
 else if(a.tab==="sheet"){id="sheet_row";args.catalog_index=a.index;args.secondary=a.secondary;}
 else{id="raw_stat";for(const name of ["sp","tags","special","extra"])args[name]=a.key[name];}
 return {type:"control",id,args,operation};
}
function syncRawKey(){
 const a=activeStat;if(!developer||!a||a.tab!=="raw")return;
 const fields=["tags","special","extra"];
 if(!fields.every(n=>$("#raw-"+n).value!==""&&$("#raw-"+n).validity.valid))return;
 const key={...a.key};for(const n of fields)key[n]=Number($("#raw-"+n).value);
 if(ST.keyId(key)===a.keyId)return;
 a.key=key;a.keyId=ST.keyId(key);a.current=null;a.gameValue=null;a.drafts={};a.readError=false;a.needsRead=true;a.selectAppliedMode=true;
 setStatValue(0,false);renderStatControls();
}
async function statAction(operation,context=activeStat){
 if(!context||context!==activeStat||busy)return;
 if(!eligible(controlById(context.tab==="aliases"?"stat_alias":context.tab==="sheet"?"sheet_row":"raw_stat"))){
  context.current=null;context.gameValue=null;context.readError=true;renderStatControls();return;
 }
 syncRawKey();
 if(operation==="set"&&!$("#stat-form").reportValidity())return;
 // Invalid advanced keys must never be submitted, even for reads or resets.
 if(developer&&context.tab==="raw"&&!["tags","special","extra"].every(n=>$("#raw-"+n).reportValidity()))return;
 const requestedKey=context.keyId;
 const r=await runTask(statPayload(operation),{silent:operation==="read"});
 if(context!==activeStat||context.keyId!==requestedKey||!$("#stat-dialog").open)return;
 if(!r){if(operation==="read"){context.current=null;context.gameValue=null;context.readError=true;}renderStatControls();return;}
 $("#stat-live").textContent=r.text||JSON.stringify(r,null,2);
 const gameValue=String(r.text||"").match(/\bgameValue=(-?\d+)/);
 context.gameValue=gameValue?Number(gameValue[1]):null;
 if(operation==="read"){
  const current=ST.contributions(r.text);
  context.current=current;context.readError=!current;context.needsRead=false;
  if(current&&context.selectAppliedMode){context.mode=ST.appliedMode(current,context.modes,context.mode);context.selectAppliedMode=false;}
  if(current&&context.drafts[context.mode]===undefined)setStatValue(current.modes[context.mode]*statInfo().scale,false);
 }else if(operation==="reset"){
  context.current={modes:{added:0,increased:0,more:0},attached:true};context.drafts={};setStatValue(0,false);
 }else{
  context.current=ST.contributions(r.text)||{modes:{...(context.current?.modes||{}),[context.mode]:r.rawValue},attached:true};delete context.drafts[context.mode];
 }
 if(context.keyId==="9:0:0:0"&&context.current){values.speed=appliedValues.speed=1+(context.current.modes.increased||0);dirty.delete("speed");}
 renderStatControls();
}
function renderSettings(){
 let profiles={};try{profiles=JSON.parse(localStorage.getItem(profileKey)||"{}");}catch{}
 const theme=document.documentElement.dataset.theme;
 content.innerHTML=`<section class="panel"><h2>Appearance · Theme</h2><p class="muted">Both themes use the same settings. Switching themes sends no game commands, and your choice is remembered.</p><div class="control-grid theme-grid">${[["chronoforge","Chronoforge","Jade time crystal · aged gold · ancient temple"],["void-atlas","Void Atlas","Violet time rift · horizontal navigation · Void observatory"]].map(([id,title,note])=>`<button data-theme-pick="${id}" class="settings-card ${theme===id?"selected":""}" aria-pressed="${theme===id}"><img src="/ui/assets/${id}-reference.png" alt="${title} design reference"><h3>${title}</h3><p class="muted">${note}</p></button>`).join("")}</div></section><section class="panel"><h2>Draft profiles</h2><div class="note">Profiles store session settings only. Quests, rewards, balances and save actions are never stored or automatically applied.</div><div class="form-grid"><label>Profile name<input id="profile-name" placeholder="Example: Balanced"></label><label>Saved draft<select id="profile-select">${Object.keys(profiles).map(name=>`<option>${esc(name)}</option>`).join("")}</select></label></div><div class="actions"><button id="profile-save">Save draft</button><button id="profile-load">Load draft</button><button id="profile-delete">Delete profile</button></div><label class="startup-profile">Apply at startup<select id="startup-profile">${(()=>{const chosen=readStored("startup-profile","");return [["","Off — start with the game's normal values"],["__last__","My last used settings"],...Object.keys(profiles).map(n=>[n,`Profile: ${n}`])].map(([v,l])=>`<option value="${esc(v)}" ${chosen===v?"selected":""}>${esc(l)}</option>`).join("");})()}</select></label><p class="muted">Applied once each time an offline character is ready, after you start EpochPact or the game. Closing EpochPact still returns the game to its normal values.</p></section>${developer?`<section class="panel"><h2>Game connection</h2><div class="control-grid">${["session_read","status","player_read","characters","play_offline","load_character"].map(id=>actionCard(controlById(id))).join("")}</div></section>`:""}<section class="panel"><h2>Version</h2><p class="muted">EpochPact ${esc(appVersion?"v"+appVersion:"")}. At startup EpochPact can read the latest release on GitHub (no player data is sent) and show a notice when a newer version exists.</p><label class="inline-check"><input type="checkbox" id="update-check" ${readStored("update-check",true)!==false?"checked":""}> Check for EpochPact updates at startup</label></section><section class="panel" id="ingame-report"><h2>In-game report</h2>${ingameReportPanel()}</section><section class="panel"><h2>Support report</h2><p class="muted">Create a ZIP for the EpochPact Discord: your description, versions, setup state and recent EpochPact logs, with an optional screenshot of the game window. Nothing is uploaded; the folder opens so you can attach the file. Your Windows user name is masked; logs can contain character names.</p><label class="wide">What happened?<textarea id="report-description" rows="4" maxlength="4000" placeholder="What did you do, what did you expect, and what happened instead?"></textarea></label><label class="inline-check"><input type="checkbox" id="report-screenshot"> Include a screenshot of the game window</label><div class="actions"><button id="report-create">Create support report</button></div></section><section class="panel"><h2>Recovery backups</h2><p class="muted">Changes to your game save create a backup. Restore a selected backup to undo that change; the game closes normally first.</p>${history.length?`<div class="table-wrap"><table><thead><tr><th>Action</th><th>Time</th><th>Result</th></tr></thead><tbody>${history.map(x=>`<tr><td>${esc(controlById(x.control)?.label)}</td><td>${new Date(x.time*1000).toLocaleTimeString("en-GB")}</td><td>${x.ok?"Complete":"Error / backup available"}</td></tr>`).join("")}</tbody></table></div>`:'<p class="empty">No save actions have been performed in this session.</p>'}${actionCard(controlById("undo"))}</section>`;
 content.insertAdjacentHTML("beforeend",navigationPicker());
}
async function refreshSection(id,{refreshConnectionFirst=true}={}){
 if(busy)return;
 // Refresh live updates both identity and the section. A stale connection
 // must not keep fresh Monolith data disabled until a second Refresh click.
 if(refreshConnectionFirst)await refreshConnection(true,false);
 if(!eligible(controlById(id)))return;
 const r=await runTask({type:"control",id,operation:"read"},{silent:true});if(!r)return;
 if(!syncLootCraft(id,r))cache[id]=r;
 if(id==="monolith_read"){appliedValues.stability_multiplier=r.stabilityMultiplier;if(!dirty.has("stability_multiplier"))values.stability_multiplier=r.stabilityMultiplier;}
 if(id==="cof_read"){
  const map={cof_favor_multiplier:r.favorMultiplier,cof_reputation_multiplier:r.reputationMultiplier};
  for(const c of catalog.controls.filter(c=>c.group==="cof"&&c.lifetime==="session")){
   const tuningKey=c.currentSource?.split(".").pop();map[c.id]??=r.tuning?.settings?.[tuningKey];
  }
  for(const [key,value] of Object.entries(map))if(value!==undefined){appliedValues[key]=value;if(!dirty.has(key))values[key]=value;}
 }
 renderPage();
}
function syncLootCraft(id,result){
 if(["pickup_radius","pickup_radius_read","pickup_interval"].includes(id)){cache.pickup_radius_read={...cache.pickup_radius_read,...result};
  if(result.radius!=null){appliedValues.pickup_radius=result.radius;if(!dirty.has("pickup_radius")&&!autoManaged("pickup_radius"))values.pickup_radius=result.radius;}
  if(result.intervalSeconds!=null){appliedValues.pickup_interval=result.intervalSeconds;if(!dirty.has("pickup_interval")&&!autoManaged("pickup_interval"))values.pickup_interval=result.intervalSeconds;}
  return true;}
 if(id==="qol_read"||id.startsWith("qol_")){
  cache.qol_read={...cache.qol_read,...result};
  for(const [key,item] of Object.entries(result.settings||{})){const cid="qol_"+key;appliedValues[cid]=item.value;if(!dirty.has(cid)&&!autoManaged(cid))values[cid]=item.value;}
  return true;
 }
 if(["craft_materials","craft_materials_read"].includes(id)){cache.craft_materials_read=result;return true;}
 if(id==="craft_custom_catalog"){
  const owner=result.player?.id;
  if(owner!=null&&workshopOwner!=null&&String(owner)!==String(workshopOwner))workshopDraft=craftingModel.defaults();
  workshopOwner=owner??workshopOwner;cache[id]=result;return true;
 }
 if(id==="build_read"||id.startsWith("build_")){
  cache.build_read={...cache.build_read,...result};
  for(const [key,item] of Object.entries(result.settings||{})){const cid="build_"+key;appliedValues[cid]=item.value;if(!dirty.has(cid)&&!autoManaged(cid))values[cid]=item.value;}
  return true;
 }
 const readId=id.startsWith("map_")?"map_read":id.startsWith("loot_")?"loot_read":id.startsWith("craft_")?"craft_read":null;
 if(!readId)return false;
 if(id==="craft_forge")result={...result.crafting,...result};
 cache[readId]={...cache[readId],...result};
 if(readId==='loot_read'&&Array.isArray(result.affixIds))affixPicker.receive(wantedAffixes,result.affixIds,preview?'preview':automaticOwner()||null,{force:id==='loot_reset'});
 const toggle=x=>x===undefined?undefined:Number(x);
 const fresh=readId==="map_read"?{map_reveal:toggle(result.enabled)}:readId==="loot_read"?{loot_lp:result.minimumLP,loot_t7:toggle(result.t7),loot_filter:toggle(result.respectFilter)}:
  {craft_fp:result.fpFactor,craft_t7:toggle(result.allowT7),craft_shards:toggle(result.preserveShards),craft_runes:toggle(result.preserveRunes),craft_glyphs:toggle(result.preserveGlyphs),craft_level:toggle(result.bypassLevel),craft_hope:result.hopePercent,craft_despair:result.despairPercent};
 for(const [key,value] of Object.entries(fresh))if(value!==undefined){appliedValues[key]=value;if(!dirty.has(key))values[key]=value;}
 return true;
}
// One row per ally type: "Skeleton ×5". Damage and health are totals for the
// group; armour, damage increase and resistances are averages.
function summonName(s){return clean(s.name).replace(/\(Clone\)/g,"").trim()||"Ally";}
function summonRows(summons,individual){
 if(individual)return summons.map(s=>({...s,label:summonName(s),count:1,members:[s]}));
 const groups=new Map();
 for(const s of summons){const key=summonName(s);if(!groups.has(key))groups.set(key,[]);groups.get(key).push(s);}
 const sum=(list,f)=>list.reduce((t,s)=>t+(Number.isFinite(Number(f(s)))?Number(f(s)):0),0);
 const avg=(list,f)=>{const v=list.map(f).map(Number).filter(Number.isFinite);return v.length?v.reduce((a,b)=>a+b,0)/v.length:null;};
 return [...groups].map(([label,list])=>({label,count:list.length,members:list,measuredDps:sum(list,s=>s.measuredDps),
  lastDamage:list.reduce((m,s)=>Math.max(m,Number(s.lastDamage)||0),0),health:sum(list,s=>s.health),maxHealth:sum(list,s=>s.maxHealth),
  armour:avg(list,s=>s.armour),damageIncreased:avg(list,s=>s.damageIncreased),
  resistances:Object.fromEntries(["Physical","Fire","Cold","Lightning","Void","Necrotic","Poison"].map(n=>[n,avg(list,s=>s.resistances?.[n])]))}));
}
function summonChange(row){
 const tracked=row.members.filter(s=>summonBaseline[s.instance]!=null);
 if(!tracked.length)return null;
 return tracked.reduce((t,s)=>t+(Number(s.measuredDps)||0)-summonBaseline[s.instance],0);
}
function summonTable(){
 const data=cache.summons_read;
 if(preview)return '<p class="note">Load an offline character to inspect real summoned allies.</p>';
 if(!window.EpochSession.current(data,connected))return '<p class="note">Waiting for live summoned allies…</p>';
 if(data.readError)return '<p class="note">Live summon data is unavailable. Previous values are not shown as current.</p>';
 if(!data.summons?.length)return '<p class="note">No active summoned allies found.</p>';
 if(summonBaselineOwner&&summonBaselineOwner!==automaticOwner()){summonBaseline={};summonBaselineOwner="";}
 const fmt=n=>Number.isFinite(Number(n))?Number(n).toLocaleString("en-US",{maximumFractionDigits:1}):"—";
 const columns=summonColumns.filter(n=>!displayPreferences.summonColumns.includes(n));
 const individual=displayPreferences.summonOptions?.includes("individual");
 const rows=summonRows(data.summons,individual);
 const cell=(s,n)=>n==="Measured DPS"?fmt(s.measuredDps):n==="Last damage"?fmt(s.lastDamage):n==="DPS change"?(c=>c==null?"—":`${c>=0?"+":""}${fmt(c)}`)(summonChange(s)):n==="Health"?`${fmt(s.health)} / ${fmt(s.maxHealth)}`:n==="Armour"?fmt(s.armour):n==="Damage increase"?fmt(s.damageIncreased)+"%":fmt(s.resistances?.[n])+"%";
 return `${data.damageSampleClipped?'<p class="note">Damage event capacity was reached. These numbers are a lower bound until a fresh five-second window is collected.</p>':""}<div class="table-scroll"><table class="summon-table"><thead><tr><th>${individual?"Ally":"Ally type"}</th>${columns.map(n=>`<th>${n}</th>`).join("")}</tr></thead><tbody>${rows.map(s=>`<tr><td>${esc(s.label)}${s.count>1?` <span class="muted">×${s.count}</span>`:""}</td>${columns.map(n=>`<td>${cell(s,n)}</td>`).join("")}</tr>`).join("")}</tbody></table></div><p class="muted">Measured DPS uses damage actually applied in a rolling five-second window (${fmt(data.damageWindowSeconds)} seconds collected), including mitigation and critical hits. Attack a target to measure damage; changing gear alone cannot reveal exact combat DPS. Compare on the same target and encounter. Damage increase remains a separate bonus. ${individual?"":"Grouped rows add damage and health across the group and average armour, damage increase and resistances. "}Updates every two seconds while visible.</p>`;
}
const defaultTierPalette=["FFFFFF","2E8BFF","FFD700","FF5500","AA55FF","00FF00","FF0044","00FFFF"];
function storedTierPalette(){try{const p=JSON.parse(localStorage.getItem("epochpact.tier-palette.v1")||"null");if(Array.isArray(p)&&p.length===8&&p.every(x=>/^[0-9A-F]{6}$/i.test(x)))return p.map(x=>x.toUpperCase());}catch{}return defaultTierPalette.slice();}
function tierPalette(){
 const palette=storedTierPalette(),c=controlById("tier_color");
 return `<section class="panel"><div class="section-head"><h2>Tier colours</h2><button data-tier-reset data-control-id="tier_color">Reset colours</button></div><p class="muted">Used for tier digits on icons and coloured tooltip lines. Your colours are remembered on this device and applied when a character is ready.</p><div class="tier-palette">${palette.map((hex,i)=>`<label>T${i+1}<input type="color" data-tier-color="${i+1}" value="#${hex}" ${c&&!eligible(c)?"disabled":""}></label>`).join("")}</div><div data-id="tier_color" hidden></div></section>`;
}
const tierPaletteApplied=new Set();
async function applyTierPalette(force=false){
 const owner=automaticOwner();if(preview||!owner||(!force&&tierPaletteApplied.has(owner)))return;
 const palette=storedTierPalette();tierPaletteApplied.add(owner);
 for(let i=0;i<8;i++)if(force||palette[i]!==defaultTierPalette[i]){
  if(owner!==automaticOwner())return;
  try{await job({type:"control",id:"tier_color",operation:"set",args:{expected_session:connected.sessionKey,tier:i+1,color:palette[i]}});}catch{}
 }
}
let filterFiles=null,filterFilesLoading=false;
function filterFilesPanel(){
 if(filterFiles===null&&!filterFilesLoading&&!preview)setTimeout(()=>void loadFilterFiles(),0);
 const list=filterFiles?.filters||[],pick=window.pywebview?.api?.choose_filter_file?'<button id="filter-import">Import filter…</button>':"";
 const rows=list.map(f=>`<tr><td>${esc(f.name)}${f.valid?"":` <span class="badge">${esc(f.error||"Unreadable")}</span>`}</td><td>${f.valid?`${f.enabled} / ${f.rules}`:"—"}</td><td>${new Date(f.modified*1000).toLocaleDateString("en-GB")}</td><td class="actions">${f.valid?`<button data-filter-export="${esc(f.file)}">Export</button><button data-filter-duplicate="${esc(f.file)}">Duplicate</button>`:""}</td></tr>`).join("");
 return `<section class="panel" id="filter-files"><div class="section-head"><h2>Loot filter files</h2><div class="actions">${pick}<button id="filter-backup" ${list.length?"":"disabled"}>Back up all</button><button id="filter-folder">Open folder</button><button id="filter-refresh">Refresh</button></div></div><p class="muted">Your Last Epoch loot filters are shared by every character and season. Export a filter to share it, import a filter someone sent you, or restore an EpochPact backup ZIP. Imports and duplicates are added as new filters; existing filters are never changed or deleted.</p>${preview?'<p class="empty">Loot filter files are listed in the desktop app.</p>':filterFiles===null?'<p class="empty">Reading loot filters…</p>':filterFiles.error?`<p class="note">${esc(filterFiles.error)}</p>`:list.length?`<div class="table-wrap"><table class="filter-table"><thead><tr><th>Filter</th><th>Enabled rules</th><th>Modified</th><th></th></tr></thead><tbody>${rows}</tbody></table></div>`:'<p class="empty">No loot filters yet. Create one in the game\'s loot filter window first.</p>'}</section>`;
}
async function loadFilterFiles(){
 filterFilesLoading=true;
 try{const r=await job({type:"launcher",action:"filters",args:{}});filterFiles=r.ok?r:{filters:[],error:r.error||"Loot filters could not be read."};}
 catch(e){filterFiles={filters:[],error:e.message||"Loot filters could not be read."};}
 finally{filterFilesLoading=false;}
 const node=$("#filter-files");if(node)node.outerHTML=filterFilesPanel();
}
async function filterAction(action,args={}){
 const r=await runTask({type:"launcher",action,args},{silent:true});
 if(r?.ok){notify(r.message);if(r.filters){filterFiles={...(filterFiles||{}),filters:r.filters};const node=$("#filter-files");if(node)node.outerHTML=filterFilesPanel();}}
 return r;
}
// ---- stash tabs from the loot filter
let stashRules=null;
const ruleTypes=["Show","Hide","Highlight"];
function stashRulesArgs(extra={}){return {expected_id:connected.player?.id,expected_name:connected.player?.name,...extra};}
function stashRulesPanel(){
 const c=controlById("stash_rules_read");if(!c)return "";
 const s=stashRules&&String(stashRules.player?.id)===String(connected.player?.id)?stashRules:null;
 const head=`<section class="panel" id="stash-rules"><div class="section-head"><h2>Stash tabs from loot filter</h2><button data-stash-rules="read" data-control-id="stash_rules_read">${s?"Refresh":"Read filter and tabs"}</button></div><p class="muted">One stash tab per loot filter rule, free of gold, in a folder named after the filter. Each tab gets the rule's rarity, type, class, item level, potential, faction, unique and corruption conditions as its stash priority, so the game's own stashing puts matching items there. Rules with affix or character-level conditions match more broadly in the tab priority; <b>Sort inventory by filter rule</b> uses the exact filter match. Every change creates a recovery backup.</p>`;
 if(!s)return head+`<p class="empty">Select a loot filter in the game, then read it here.</p></section>`;
 if(!s.filter)return head+`<p class="note">No loot filter is active. Select one in the game's loot filter window.</p></section>`;
 const tabName=id=>esc(s.tabs.find(t=>t.id===id)?.name??"");
 const tabOptions=sel=>`<option value="0">—</option>`+s.tabs.map(t=>`<option value="${t.id}" ${t.id===sel?"selected":""}>${esc(t.name)}</option>`).join("");
 const rows=s.rules.map(r=>`<tr class="${r.enabled?"":"muted"}"><td>${r.number}</td><td>${ruleTypes[r.type]??r.type}${r.enabled?"":" · off"}</td><td>${esc(r.name||"—")}</td><td><select data-stash-rule-link="${r.number}" data-control-id="stash_rule_assign" aria-label="Tab for rule ${r.number}">${tabOptions(r.tab)}</select></td><td>${r.unmapped.length?`<span class="badge" title="${esc(r.unmapped.join(", "))}">Sort only: ${esc(r.unmapped.join(", "))}</span>`:r.mapped.length?"Tab priority":"—"}</td></tr>`).join("");
 return head+`<p class="note">Filter: <b>${esc(s.filter)}</b> · ${s.rules.length} rules · ${s.tabs.length} stash tabs${s.canAddTab?"":" · the game's tab limit is reached"}.</p><div class="table-wrap"><table class="stash-rules-table"><thead><tr><th>Rule</th><th>Type</th><th>Name</th><th>Stash tab</th><th>Matching</th></tr></thead><tbody>${rows}</tbody></table></div><div class="actions"><label class="inline-check"><input type="checkbox" id="stash-rules-hidden"> Include Hide rules</label><label class="inline-check"><input type="checkbox" id="stash-rules-disabled"> Include disabled rules</label></div><div class="actions"><button class="primary" data-stash-rules="create" data-control-id="stash_rules_create">Create tabs for rules without a tab</button><button data-stash-rules="sort" data-control-id="stash_rules_sort">Sort inventory by filter rule</button></div></section>`;
}
function renderStashRules(){const node=$("#stash-rules");if(node)node.outerHTML=stashRulesPanel();updateDisabled();}
async function stashRulesAction(kind,extra={}){
 const id={read:"stash_rules_read",create:"stash_rules_create",sort:"stash_rules_sort",assign:"stash_rule_assign"}[kind];
 if(kind!=="read"){
  const name=connected.player?.name||"the loaded character";
  const text={create:`Create one free stash tab for each ${extra.hidden?"":"Show/Highlight "}rule of “${stashRules?.filter}” that has no tab yet, in a folder named after the filter. A recovery backup is created first.`,
   sort:`Move ${name}'s inventory items into the tabs linked to the loot filter rule they match. Items without a linked rule stay in the inventory. A recovery backup is created first.`,
   assign:extra.rule?`Give this stash tab the conditions of rule ${extra.rule} as its stash priority. A recovery backup is created first.`:`Remove the rule link and stash priority from this tab. A recovery backup is created first.`}[kind];
  if(!await confirmAction(controlById(id).label,text)){renderStashRules();return;}
 }
 const r=await runTask({type:"control",id,operation:kind==="read"?"read":"set",args:stashRulesArgs(extra)},{silent:kind==="read"});
 if(r?.ok&&r.rules){stashRules=r;
  if(kind==="create")notify(`${r.created} tab${r.created===1?"":"s"} created${r.existing?`, ${r.existing} already linked`:""}${r.atLimit?`, ${r.atLimit} over the game's tab limit`:""}.`);
  if(kind==="sort")notify(`${r.moved} item${r.moved===1?"":"s"} moved${r.full?`, ${r.full} did not fit`:""}${r.unlinked?`, ${r.unlinked} match a rule without a tab`:""}.`);
  if(kind==="assign")notify(extra.rule?`Tab linked to rule ${extra.rule}.`:"Tab link removed.");
 }
 renderStashRules();
}
// ---- interface font
function fontPanel(){
 const c=controlById("font_family");if(!c)return "";
 const families=cache.font_read?.families||[],current=values.font_family??"game";
 const options=[`<option value="game" ${current==="game"?"selected":""}>Game font</option>`,...families.map(f=>`<option value="${esc(f)}" ${current===f?"selected":""}>${esc(f)}</option>`)].join("");
 return `<section class="panel"><div class="section-head"><h2>Text and font</h2>${families.length?"":`<button data-refresh="font_read" data-control-id="font_read">Load Windows fonts</button>`}</div><p class="muted">${esc(c.description)} The choice is undone when you choose Game font or close EpochPact.</p><label>Interface font<select data-font-family data-control-id="font_family" ${families.length?"":"disabled"}>${options}</select></label></section>`+settingPanel("Text size",[controlById("font_scale")]);
}
// ---- version and update notice: one read of the latest GitHub release.
let appVersion="";
async function checkForUpdate(attempt=0){
 let r=null;try{r=await (await fetch("/api/update")).json();}catch{return;}
 if(r?.pending&&attempt<10){setTimeout(()=>void checkForUpdate(attempt+1),1500);return;}
 const node=$("#update-notice");if(!node||!r?.ok||!r.updateAvailable)return;
 node.innerHTML=`New version <b>v${esc(r.latest)}</b> · <a href="${esc(r.url)}" target="_blank" rel="noopener">Download</a>`;node.hidden=false;
}
// ---- in-game report (hotkey in the game, saved in the game folder)
function ingameReportPanel(){
 const r=cache.report_read,key=r?.key||"F8",c=controlById("report_key");
 if(!c)return "";
 return `<p class="muted">While playing, press <b>${esc(key==="off"?"the hotkey":key)}</b> to save a report in <code>Last Epoch\\EpochPact\\reports</code>: a screenshot of that moment, game and EpochPact state, recent logs and a <code>description.txt</code> to fill in. The game shows a short notice. Nothing is uploaded; zip the folder and share it in the EpochPact Discord. Windows user and computer names are masked.</p><div class="form-grid"><label>Hotkey<select data-report-key data-control-id="report_key">${c.parameters[0].choices.map(k=>`<option value="${k}" ${k===key?"selected":""}>${k==="off"?"Off":k}</option>`).join("")}</select></label></div><div class="actions"><button data-report-now data-control-id="report_now">Create report now</button><button data-refresh="report_read" data-control-id="report_read">Refresh</button>${r?.last?`<span class="muted">Last: ${esc(r.last.split(/[\\/]/).pop())}</span>`:""}</div>`;
}
// ---- damage session (on only while the player wants it)
function damagePanel(){
 if(!controlById("damage_session"))return "";
 const s=cache.damage_session_read,D=window.EpochDamage,win=window.pywebview?.api?.open_damage_window?'<button data-damage-window>Open damage window</button>':"";
 return `<section class="panel" id="damage-panel"><div class="section-head"><h2>Damage meter</h2><div class="actions"><button class="${s?.active?"":"primary"}" data-damage-action="${s?.active?"stop":"start"}" data-control-id="damage_session">${s?.active?"Stop":"Start"}</button><button data-damage-action="reset" data-control-id="damage_session">Reset</button><button data-damage-csv ${s?.types?.length?"":"disabled"}>Copy as CSV</button>${win}</div></div><p class="muted">Records damage by your allies (grouped by type) and your character only while it runs, so it costs nothing when stopped. Values stay after Stop until Reset, also when you switch windows. The damage window stays on top of a windowed or borderless game.</p><p class="note" id="damage-status">${esc(D?D.status(s):"")}</p><div id="damage-live">${D?D.table(s):""}</div></section>`;
}
function renderDamage(){const node=$("#damage-panel");if(node)node.outerHTML=damagePanel();updateDisabled();}
async function damageAction(action){
 const r=await runTask({type:"control",id:"damage_session",args:{action}},{silent:true});
 if(r?.ok){cache.damage_session_read=r;renderDamage();}
}
let lootEntries=[],lootOwner=null;
function lootTable(){return window.EpochLootLog?window.EpochLootLog.table(lootEntries):'<p class="empty">No pickups yet.</p>';}
function lootPanel(){
 const feed=window.pywebview?.api?.open_loot_feed?'<button id="loot-feed-open">Open loot window</button>':"";
 return `<section class="panel"><div class="section-head"><h2>Recent pickups</h2><div class="actions">${feed}<button id="loot-log-refresh" data-refresh="loot_log" data-control-id="loot_log">Refresh</button></div></div><p class="muted">Items and gold you or Auto Pickup collect, with the loot filter rule that matched. "Rule" uses the same number as the game's Item rule numbering setting. The loot window stays on top of a windowed or borderless game.</p><div id="loot-log-live">${lootTable()}</div><div class="control-grid">${actionCard(controlById("loot_log_clear"))}</div></section>`;
}
let summonBaseline={};
let summonBaselineOwner="";
const summonColumns=["Measured DPS","Last damage","DPS change","Health","Armour","Damage increase","Physical","Fire","Cold","Lightning","Void","Necrotic","Poison"];
function summonColumnPicker(){return `<details class="display-options"><summary>Visible columns</summary><div class="column-options"><label><input type="checkbox" data-summon-option="individual" ${displayPreferences.summonOptions?.includes("individual")?"checked":""}>Show each ally separately</label>${summonColumns.map(n=>`<label><input type="checkbox" data-summon-column="${esc(n)}" ${displayPreferences.summonColumns.includes(n)?"":"checked"}>${n}</label>`).join("")}</div></details>`;}
function navigationPicker(){return `<section class="panel"><h2>Navigation and favorites</h2><p class="muted">Choose visible sections and pin favorites to the top. These choices stay on this device.</p><div class="navigation-options">${Object.entries(pages).map(([id,p])=>`<div><strong>${p[0]}</strong><label><input type="checkbox" data-nav-visible="${id}" ${displayPreferences.hidden.includes(id)?"":"checked"} ${["overview","settings"].includes(id)?"disabled":""}>Visible</label><label><input type="checkbox" data-nav-favorite="${id}" ${displayPreferences.favorites.includes(id)?"checked":""}>Favorite</label></div>`).join("")}</div><label class="inline-check"><input type="checkbox" id="remember-scroll" ${rememberScroll()?"checked":""}> Return to the same place when switching sections</label><button id="display-preferences-reset">Restore all sections and stats</button></section>`;}
const stashQueries=[
 ["LP2+","Uniques with at least 2 Legendary Potential"],
 ["WW18+","Weaver uniques with at least 18 Weaver’s Will"],
 ["FP30+","Equipment with at least 30 Forging Potential"],
 ["exalted&2T6+","Exalted equipment with at least two tier 6 or higher affixes"],
 ["2T7","Items with at least two tier 7 affixes"],
 ["LP1+&boots","Boots with at least 1 Legendary Potential"],
 ["CoF&unique","Circle of Fortune tagged uniques"],
 ["2hsword&exalted","Exalted two-handed swords"]
];
function stashSearchHelp(){return `<section class="panel"><h2>In-game stash search</h2><p class="muted">Copy a query and paste it into the game’s stash search box. These are search expressions, not gameplay commands.</p><div class="search-recipes">${stashQueries.map(([query,note],i)=>`<article><code>${esc(query)}</code><span>${esc(note)}</span><button type="button" data-copy-stash-query="${i}">Copy</button></article>`).join("")}</div><p class="muted"><code>&amp;</code> combines requirements; <code>|</code> accepts either condition. Tooltip text searches use the game’s current language.</p><a href="https://support.lastepoch.com/hc/en-us/articles/46361652732955-Stash-Searching" target="_blank" rel="noopener">Official stash search reference</a></section>`;}
async function pollConveniences(){
 if(!catalog||preview||document.hidden||busy||liveReading||autoRunning||autoPending.size||activeStat)return;
 const id=page==="summons"?"summons_read":page==="world"?"qol_read":page==="loot"?"loot_log":null;
 if(!id||!eligible(controlById(id)))return;
 // Each read runs on the game's main thread: summons DPS every 2 s, world counters every 5 s.
 const now=Date.now();if(now<(convenienceNextRead[id]||0))return;convenienceNextRead[id]=now+(id==="summons_read"||id==="loot_log"?2000:5000);
 liveReading=true;const owner=automaticOwner();
 try{
  const r=await job({type:"control",id,operation:"read"});
  if(owner!==automaticOwner())return;
  if(id==="loot_log"){if(r.ok){if(lootOwner!==owner){lootEntries=[];lootOwner=owner;}lootEntries=window.EpochLootLog.merge(lootEntries,r.entries);const node=$("#loot-log-live");if(node)node.innerHTML=lootTable();}return;}
  if(r.ok){if(!syncLootCraft(id,r))cache[id]=r;}
  else if(id==="summons_read")cache[id]={player:connected.player,readError:true};
  if(page==="summons"){const node=$("#summon-live");if(node)node.innerHTML=summonTable();}
  else if(page==="world")for(const c of catalog.controls.filter(c=>c.id.startsWith("qol_")))syncSettingUi(c.id);
 }catch{if(id==="summons_read"){cache[id]={player:connected.player,readError:true};const node=$("#summon-live");if(node)node.innerHTML=summonTable();}}
 finally{liveReading=false;armAutomatic();}
}
setInterval(()=>void pollConveniences(),1000);
setInterval(()=>{if(page==="summons"&&!preview&&!document.hidden&&!busy&&cache.damage_session_read?.active)void job({type:"control",id:"damage_session_read",operation:"read"}).then(r=>{if(r?.ok&&page==="summons"){cache.damage_session_read=r;const live=$("#damage-live"),status=$("#damage-status");if(live)live.innerHTML=window.EpochDamage.table(r);if(status)status.textContent=window.EpochDamage.status(r);}}).catch(()=>{});},1000);
function formArgs(form,c){
 const data=new FormData(form),args={};
 for(const p of c.parameters){if(p.name==="save_id")continue;let v=data.get(p.name);if(p.type==="optional_integer"&&(v===""||v===null))continue;
  args[p.name]=c.id==="loot_affixes"?wantedAffixes.selected.join(",")||"none":p.type==="number_or_reset"?(v===""?null:Number(v)):p.type==="integer_or_none"&&v==="none"?null:p.type.includes("integer")||p.type==="number"?Number(v):v;
  if(c.id==="craft_custom"&&p.name==="affixes")args.affixes=data.getAll("affixes").join(",")||"keep";
  if(c.id==="craft_custom"&&p.name==="modifiers")args.modifiers=data.getAll("modifiers").join(",")||"none";
 }
 if(c.parameters.some(p=>p.name==="save_id"))args.expected_id=saveSnapshot(c)?.player?.id;
 if(!preview&&connected.sessionKey)args.expected_session=connected.sessionKey;
 if(c.id==="craft_materials"||c.group==="inventory")args.expected_name=saveSnapshot(c)?.player?.name;
 if(c.id==="cof_join")args.switch_from_merchant=data.get("switch_from_merchant")==="on";
 if(c.id==='craft_custom')Object.assign(args,craftingModel.args(workshopDraft));
 return args;
}
async function formAction(form,override){
 const c=controlById(override||form.dataset.id);if(!form.reportValidity())return;
 if(c.id==='loot_affixes'&&wantedAffixes.owner!==(preview?'preview':automaticOwner())){notify('Refresh pickup rules for the loaded character before applying this selection.',true);return;}
 if(c.id==='craft_custom'){
  const problem=craftingModel.problem(workshopDraft,cache.craft_custom_catalog,cache.craft_read?.forgePreview);
  if(problem){notify(problem,true);return;}
 }
 // Capture this form's target before a confirmation or refresh can replace it.
 const args=formArgs(form,c),draftKey=monoDraftKey(c.id),submittedDraft=monoDrafts[draftKey],submittedName=connected.player?.name;
 if(c.lifetime==="game_save"||c.lifetime==="recovery"){
  const summary=c.id==='craft_custom'?craftingModel.summary(workshopDraft,cache.craft_custom_catalog):null;
  const message=summary?`Character: ${submittedName||'Loaded offline character'}\n${clean(summary.template)}\n${summary.affixes} · ${summary.effects} · ${summary.rolls}\n\nReplaces the item currently selected in the game’s forge. No materials are consumed. A recovery backup is created.${args.modifiers!=='none'?'\nExtra effects require EpochPact and workshop-recipes.txt when reloading or moving this item.':''}`:c.id==='craft_materials'?`Character: ${submittedName||'Loaded offline character'}\nMinimum stock: ${Number(args.value).toLocaleString()} of each shard, rune and glyph.\n\nHigher quantities are kept. Adds materials once to the shared crafting stash and creates a recovery backup. This does not enable infinite materials.`:c.description+(c.lifetime==="recovery"?" The game will close normally and the selected operation backup will be restored.":" This action changes the game save. The backend creates a backup before applying it.");
  const accepted=await confirmAction(summary?'Create this custom item?':c.id==='craft_materials'?'Stock all materials?':c.label,message);if(!accepted)return;
 }
 if((c.lifetime==="game_save"||c.id==="mastery_choose")&&!preview){
  const snapshot=saveSnapshot(c);
  const reason=window.EpochSession.saveActionReason(snapshot,connected,{monolith:c.group==="monolith"});
  if(reason||args.expected_session&&args.expected_session!==connected.sessionKey||String(args.expected_id)!==String(connected.player?.id)||submittedName&&connected.player?.name&&submittedName!==connected.player.name){notify(reason||"The game or character changed. Review the action for the loaded character.",true);return;}
  if(c.id==="mastery_choose"&&snapshot.mastery?.available!==true){notify(snapshot.mastery?.reason||"Refresh live to check mastery selection.",true);return;}
 }
 const result=await runTask({type:"control",id:c.id,args,operation:c.widget==="read"||c.widget==="preview"?"read":"set"},{show:true,title:c.label});
 if(!result)return;
 if(c.id==='craft_reset')for(const id of [...dirty])if(id.startsWith('craft_')){dirty.delete(id);values[id]=appliedValues[id];}
 if(c.id==="mastery_choose"){
  if(cache.progress_read&&result.mastery)cache.progress_read.mastery=result.mastery;
  renderPage();
 }
 else if(syncLootCraft(c.id,result))renderPage();
 else if(c.widget==="read"&&["progress_read","monolith_read","cof_read","factions_read"].includes(c.id)){
  cache[c.id]=result;
  renderPage();
 }
 else if(c.lifetime==="game_save"){
  if(["corruption","stability"].includes(c.id)&&monoDrafts[draftKey]===submittedDraft)delete monoDrafts[draftKey];
  const readId={campaign:"progress_read",monolith:"monolith_read",cof:"cof_read",skills:"build_read"}[c.group];delete cache[readId];if(readId)await refreshSection(readId);
 }
}
function settingChanged(id,value,delay=0){
 const c=controlById(id),p=c.parameters.find(p=>p.name==="value");
 if(value===null&&c.default!==null||value!==null&&(!Number.isFinite(value)||value<p.minimum||value>p.maximum||p.type==="integer"&&!Number.isInteger(value))){notify(`Enter a value between ${p.minimum} and ${p.maximum}.`,true);return;}
 if(automaticSetting(c)){queueAutomatic(id,value,delay);return;}
 values[id]=value;dirty.add(id);renderPage();
}
async function applyDraft(){
 if(busy||!dirty.size)return;
 for(const input of document.querySelectorAll('[data-setting-number]'))if(!input.reportValidity())return;
 const ids=[...dirty].filter(id=>!autoManaged(id)),expected_session=connected.sessionKey;let applied=0;
 let refreshForge=false;
 for(const id of ids){
  const c=controlById(id);if(!c||!["actor","session"].includes(c.lifetime))continue;
  const submitted=values[id],args={value:submitted??"reset"};if(expected_session&&!preview)args.expected_session=expected_session;
  const r=await runTask({type:"control",id,args},{silent:true});
  if(!r){notify(`${applied} settings applied; stopped after an error. Remaining drafts are kept.`,true);renderPage();return;}
  syncLootCraft(id,r);
  if(id.startsWith("craft_"))refreshForge=true;
  appliedValues[id]=submitted;if(values[id]===submitted)dirty.delete(id);applied++;
 }
 // A changed level/cost control changes normal forge eligibility. Read it once
 // after this batch; never invoke Forge while applying session settings.
 if(refreshForge)await refreshSection("craft_read",{refreshConnectionFirst:false});
 notify(`${applied} settings ${preview?"simulated in preview":"applied in order"}.`);renderPage();
}
function setTheme(theme){
 if(!["chronoforge","void-atlas"].includes(theme))theme="chronoforge";
 document.documentElement.dataset.theme=theme;$("#theme-label").textContent=theme==="chronoforge"?"CHRONOFORGE":"VOID ATLAS";
 try{localStorage.setItem("epochpact.theme",theme);}catch{}
 if(catalog)renderPage();
}
content.addEventListener("submit",e=>{if(e.target.matches(".control-form")){e.preventDefault();void formAction(e.target);}});
content.addEventListener('toggle',e=>{if(e.target.hasAttribute?.('data-workshop-advanced'))workshopAdvanced=e.target.open;if(e.target.dataset?.craftDisclosure)craftDisclosure[e.target.dataset.craftDisclosure]=e.target.open;},true);
content.addEventListener("click",async e=>{
 const b=e.target.closest("button");if(!b||b.disabled)return;
 if(b.dataset.go){if(b.dataset.craftTarget)craftingSection=b.dataset.craftTarget;go(b.dataset.go);}
 else if(b.hasAttribute?.('data-affix-add'))moveWantedAffixes('available');
 else if(b.hasAttribute?.('data-affix-remove'))moveWantedAffixes('selected');
 else if(b.hasAttribute?.('data-affix-clear')){affixPicker.move(wantedAffixes,[...wantedAffixes.selected],false);updateWantedAffixLists();}
 else if(b.dataset.craftSection){craftingSection=b.dataset.craftSection;renderPage();}
 else if(b.dataset.workshopMore){workshopListLimits[b.dataset.workshopMore]+=40;updateWorkshopUi();}
 else if(b.dataset.workshopRemove){workshopDraft[b.dataset.workshopRemove]=workshopDraft[b.dataset.workshopRemove].filter(x=>x!==b.dataset.choice);updateWorkshopUi();}
 else if(b.id==="summon-baseline"){
  const data=cache.summons_read;if(!window.EpochSession.current(data,connected)||!data.summons?.length){notify("Refresh live summons before saving a damage baseline.",true);return;}
  summonBaselineOwner=automaticOwner();summonBaseline=Object.fromEntries(data.summons.map(s=>[s.instance,s.measuredDps]));$("#summon-live").innerHTML=summonTable();notify("Damage baseline saved for these active allies. Compare against the same target.");
 }
 else if(b.dataset.tierReset!==undefined){try{localStorage.removeItem("epochpact.tier-palette.v1");}catch{}const r=await runTask({type:"control",id:"tier_color",operation:"reset"},{silent:true});if(r?.ok){notify("Tier colours reset.");renderPage();}}
 else if(b.dataset.stashRules)await stashRulesAction(b.dataset.stashRules,b.dataset.stashRules==="create"?{hidden:$("#stash-rules-hidden")?.checked?1:0,disabled:$("#stash-rules-disabled")?.checked?1:0}:{});
 else if(b.dataset.reportNow!==undefined){const r=await runTask({type:"control",id:"report_now"},{silent:true});if(r?.ok){notify(`Report saved: ${r.folder.split(/[\\/]/).pop()} (in the game's EpochPact\\reports folder).`);void refreshSection("report_read");}}
 else if(b.dataset.damageAction)await damageAction(b.dataset.damageAction);
 else if(b.dataset.damageCsv!==undefined){const text=window.EpochDamage.csv(cache.damage_session_read);try{await navigator.clipboard.writeText(text);notify("Damage table copied as CSV.");}catch{const area=document.createElement("textarea");area.value=text;document.body.appendChild(area);area.select();document.execCommand("copy");area.remove();notify("Damage table copied as CSV.");}}
 else if(b.dataset.damageWindow!==undefined){try{await window.pywebview.api.open_damage_window();}catch{notify("Could not open the damage window.",true);}}
 else if(b.id==="report-create"){
  const r=await runTask({type:"launcher",action:"report",args:{description:$("#report-description").value,screenshot:$("#report-screenshot").checked}},{silent:true});
  if(r?.ok){notify(r.message);$("#report-description").value="";}
 }
 else if(b.id==="filter-refresh"){filterFiles=null;await loadFilterFiles();}
 else if(b.id==="filter-backup")await filterAction("filter_backup");
 else if(b.id==="filter-folder")await filterAction("filter_folder");
 else if(b.id==="filter-import"){let path=null;try{path=await window.pywebview.api.choose_filter_file();}catch{notify("Could not open the file picker.",true);}if(path)await filterAction("filter_import",{path});}
 else if(b.dataset.filterExport)await filterAction("filter_export",{file:b.dataset.filterExport});
 else if(b.dataset.filterDuplicate)await filterAction("filter_duplicate",{file:b.dataset.filterDuplicate});
 else if(b.id==="loot-feed-open"){try{await window.pywebview.api.open_loot_feed();}catch{notify("Could not open the loot window.",true);}}
 else if(b.id==="loot-log-refresh"){convenienceNextRead.loot_log=0;await pollConveniences();}
 else if(b.id==="summon-baseline-clear"){summonBaseline={};summonBaselineOwner="";$("#summon-live").innerHTML=summonTable();}
 else if(b.dataset.refresh)await refreshSection(b.dataset.refresh);
 else if(b.dataset.themePick)setTheme(b.dataset.themePick);
 else if(b.dataset.autoRetry){const id=b.dataset.autoRetry;queueAutomatic(id,values[id],0);}
 else if(b.dataset.settingReset){const c=controlById(b.dataset.settingReset);settingChanged(c.id,c.default);}
 else if(b.dataset.step){const id=b.dataset.settingId,c=controlById(id),p=c.parameters[0];settingChanged(id,Math.min(p.maximum,Math.max(p.minimum,Number(((values[id]??p.minimum)+Number(b.dataset.step)*(p.type==="integer"?1:.1)).toFixed(2)))));}
 else if(b.dataset.statTab){statsTab=b.dataset.statTab;statsPage=0;renderStats();updateDisabled();}
 else if(b.dataset.statCategory){statsCategory=b.dataset.statCategory;statsPage=0;renderStats();updateDisabled();}
 else if(b.dataset.statFavorite!==undefined)toggleStatFavorite(Number(b.dataset.statFavorite));
 else if(b.dataset.statHide!==undefined){const id=ST.favoriteId(statSource()[Number(b.dataset.statHide)],statsTab);saveDisplayPreference("hiddenStats",id,!displayPreferences.hiddenStats.includes(id));renderStatResults();}
 else if(b.id==="stat-hidden-filter"){showHiddenStats=!showHiddenStats;statsPage=0;renderStatResults();}
 else if(b.id==="display-preferences-reset"){displayPreferences=preferencesApi?.defaults()||displayPreferences;try{preferencesApi?.save(localStorage,displayPreferences);}catch{}renderSettings();navigation();notify("All sections and stats are visible again.");}
 else if(b.dataset.copyStashQuery!==undefined){const query=stashQueries[Number(b.dataset.copyStashQuery)]?.[0];if(query)try{await navigator.clipboard.writeText(query);notify("Copied. Paste into the game’s stash search box.");}catch{notify("Clipboard unavailable. Select the query text and copy it manually.",true);}}
 else if(b.id==="stat-favorites-filter"){statsFavoritesOnly=!statsFavoritesOnly;statsPage=0;renderStats();updateDisabled();}
 else if(b.id==="stat-clear-filters"){statsFavoritesOnly=false;statsCategory="All";statsQuery="";statsPage=0;renderStats();updateDisabled();}
 else if(b.dataset.statIndex!==undefined)openStat(Number(b.dataset.statIndex),b.dataset.secondary==="true");
 else if(b.dataset.summonStat!==undefined){statsTab="aliases";openStat(Number(b.dataset.summonStat));}
 else if(b.dataset.statPage){statsPage+=Number(b.dataset.statPage);renderStatResults();}
 else if(b.hasAttribute("data-prophecy-preview"))await formAction(b.closest("form"),"cofpreview");
 else if(b.id==="all-stats-reset"){
  if(await confirmAction("Remove all contributions","Removes only EpochPact stat contributions. Equipment stats are kept. Controls sharing the same full stat key, including movement speed, are refreshed.")){
   const r=await runTask({type:"control",id:"stat_reset"},{show:true});if(r){actorValues={};values.speed=1;dirty.delete("speed");renderPage();}
  }
 }else if(b.id==="profile-save"){
  const name=$("#profile-name").value.trim();if(!name){notify("Enter a profile name.",true);return;}
  let profiles={};try{profiles=JSON.parse(localStorage.getItem(profileKey)||"{}");}catch{}
  const profile={};for(const c of catalog.controls)if(c.lifetime==="session"&&["number","toggle"].includes(c.widget)||c.id==="autopickup"||c.id==="speed")profile[c.id]=values[c.id]??c.default;
  Object.defineProperty(profiles,name,{value:profile,enumerable:true,configurable:true,writable:true});localStorage.setItem(profileKey,JSON.stringify(profiles));notify("Draft saved; not applied to the game.");renderSettings();updateDisabled();
 }else if(b.id==="profile-load"){
  const profiles=JSON.parse(localStorage.getItem(profileKey)||"{}");const profile=profiles[$("#profile-select").value];if(!profile)return;
  cancelAutomatic();
  for(const [id,v] of Object.entries(profile)){const c=controlById(id);if(c&&["session","actor"].includes(c.lifetime)&&["number","toggle"].includes(c.widget)){values[id]=v;dirty.add(id);}}
  notify("Profile loaded into the draft. Use Apply changes to update the game.");updateDisabled();
 }else if(b.id==="profile-delete"){
  const profiles=JSON.parse(localStorage.getItem(profileKey)||"{}");const name=$("#profile-select").value;
  if(name&&await confirmAction("Delete draft profile",`The local draft “${name}” will be deleted. Game settings are unchanged.`)){delete profiles[name];localStorage.setItem(profileKey,JSON.stringify(profiles));renderSettings();updateDisabled();notify("Local draft deleted.");}
 }
});
function rememberMonolithOwner(connection){
 if(preview)return false;
 // Missing identity during pause, minimisation, travel or a failed read is not
 // evidence of a character switch. Only a settled offline identity owns drafts.
 if(!window.EpochSession.current({player:connection.player},connection))return false;
 const player=connection.player,owner={id:String(player.id),name:typeof player.name==="string"&&player.name.trim()?player.name:undefined};
 const changed=!!monoDraftOwner&&(monoDraftOwner.id!==owner.id||
  monoDraftOwner.name!=null&&owner.name!=null&&monoDraftOwner.name!==owner.name);
 if(changed){monoDrafts={};monoTarget={timeline:null,difficulty:"normal"};}
 monoDraftOwner={...owner,name:owner.name??(changed?undefined:monoDraftOwner?.name)};
 return changed;
}
function monoDraftKey(id){return `${preview?"preview":monoDraftOwner?.id??connected.player?.id}:${monoTarget.timeline}:${monoTarget.difficulty}:${id}`;}
function rememberMonolithInput(input){
 const form=input.closest("form");
 if(input.name!=="value"||!["corruption","stability"].includes(form?.dataset.id))return;
 rememberMonolithOwner(connected);
 if(preview||monoDraftOwner)monoDrafts[monoDraftKey(form.dataset.id)]=input.value;
}
content.addEventListener("input",e=>{
 const t=e.target;
 if(t.hasAttribute?.('data-affix-search')){wantedAffixes.query=t.value;updateWantedAffixLists();return;}
 if(t.dataset.workshopSearch){
  const kind=t.dataset.workshopSearch;workshopFilters[kind]=t.value;
  if(kind==='template'){const select=$('[data-workshop-template]');if(select)select.innerHTML=workshopTemplateOptions();}
  else{workshopListLimits[kind]=40;updateWorkshopUi();}return;
 }
 if(t.hasAttribute?.("data-workshop-filter")){workshopModifierFilter=t.value;const select=t.closest('form')?.querySelector('[name=modifiers]');if(select)select.innerHTML=workshopModifierOptions();return;}
 if(t.name==="value"&&t.closest('form')?.dataset.id==="craft_materials")materialTarget=t.value;
 rememberMonolithInput(t);
 if(t.id==="stat-search"){statsQuery=t.value;statsPage=0;renderStatResults();}
 else if(t.dataset.settingRange&&automaticSetting(controlById(t.dataset.settingRange))){settingChanged(t.dataset.settingRange,Number(t.value),250);}
 else if(t.dataset.settingNumber){
  const id=t.dataset.settingNumber,c=controlById(id),p=c.parameters.find(p=>p.name==="value");
  const v=t.value===""&&c.default===null?null:Number(t.value);
  if(!t.validity.valid||!Number.isFinite(v)&&v!==null||t.value===""&&c.default!==null){
   if(automaticSetting(c)){autoPending.delete(id);autoErrors.set(id,{message:"Enter a valid value.",retry:false});syncSettingUi(id);armAutomatic();}return;
  }
  if(automaticSetting(c)){settingChanged(id,v,300);return;}
  values[id]=v;dirty.add(id);const range=t.closest('article').querySelector('[data-setting-range]');if(range)range.value=v??p.minimum;
  const feedback=t.closest('article').querySelector('[data-setting-feedback]');if(feedback)feedback.innerHTML=settingFeedback(id);updateDisabled();
 }
});
content.addEventListener("change",e=>{
 if(e.target.dataset.reportKey!==undefined){void (async()=>{const r=await runTask({type:"control",id:"report_key",args:{key:e.target.value}},{silent:true});if(r?.ok){cache.report_read=r;notify(r.key==="off"?"In-game report hotkey turned off.":`In-game report hotkey: ${r.key}.`);}})();return;}
 if(e.target.id==="remember-scroll"){writeStored("remember-scroll",e.target.checked);return;}
 if(e.target.id==="update-check"){writeStored("update-check",e.target.checked);return;}
 if(e.target.dataset.stashRuleLink){
  const number=Number(e.target.dataset.stashRuleLink),tab=Number(e.target.value),previous=stashRules?.rules.find(r=>r.number===number)?.tab;
  if(tab)void stashRulesAction("assign",{tab,rule:number});else if(previous!=null)void stashRulesAction("assign",{tab:previous,rule:0});return;
 }
 if(e.target.dataset.fontFamily!==undefined){const family=e.target.value;values.font_family=family;void runTask({type:"control",id:"font_family",args:{family:family.replace(/ /g,"%20")}},{silent:true});return;}
 if(e.target.dataset.tierColor){
  const i=Number(e.target.dataset.tierColor)-1,hex=e.target.value.replace("#","").toUpperCase(),palette=storedTierPalette();palette[i]=hex;
  try{localStorage.setItem("epochpact.tier-palette.v1",JSON.stringify(palette));}catch{}
  void runTask({type:"control",id:"tier_color",args:{tier:i+1,color:hex}},{silent:true});return;
 }
 if(e.target.id==="startup-profile"){
  const value=e.target.value;writeStored("startup-profile",value);
  if(value==="__last__")rememberLastSettings();
  notify(value?"Startup profile saved. It applies when an offline character is ready.":"Startup profile turned off. EpochPact starts with the game's normal values.");
  if(value)applyStartupProfile();return;
 }
 if(e.target.dataset.navVisible){saveDisplayPreference("hidden",e.target.dataset.navVisible,!e.target.checked);navigation();return;}
 if(e.target.dataset.navFavorite){saveDisplayPreference("favorites",e.target.dataset.navFavorite,e.target.checked);navigation();return;}
 if(e.target.dataset.summonOption){saveDisplayPreference("summonOptions",e.target.dataset.summonOption,e.target.checked);$("#summon-live").innerHTML=summonTable();return;}
 if(e.target.dataset.summonColumn){saveDisplayPreference("summonColumns",e.target.dataset.summonColumn,!e.target.checked);$("#summon-live").innerHTML=summonTable();return;}
 const t=e.target;rememberMonolithInput(t);
 if(t.hasAttribute?.('data-affix-sort')){wantedAffixes.order=t.value==='za'?'za':'az';updateWantedAffixLists();return;}
 if(t.dataset.craftGlyphMode){settingChanged(t.dataset.craftGlyphMode,t.value==='default'?null:50);return;}
 if(t.hasAttribute?.('data-workshop-kind')){workshopTemplateKind=t.value;const select=$('[data-workshop-template]');if(select)select.innerHTML=workshopTemplateOptions();return;}
 if(t.dataset.workshopPick){const selected=workshopDraft[t.dataset.workshopPick],id=String(t.value);workshopDraft[t.dataset.workshopPick]=t.checked?[...new Set([...selected,id])]:selected.filter(x=>x!==id);updateWorkshopUi();return;}
 if(t.name==="value"&&t.closest('form')?.dataset.id==="craft_materials")materialTarget=t.value;
 if(t.name&&t.closest('form')?.dataset.id==="craft_custom")workshopDraft[t.name]=["affixes","modifiers"].includes(t.name)?Array.from(t.selectedOptions,x=>x.value):t.value;
 if(t.closest('form')?.dataset.id==='craft_custom')updateWorkshopUi();
 if(t.name==="perfect"&&t.closest('form')?.dataset.id==="craft_custom"){const select=t.closest('form').querySelector('[name=modifiers]');if(select)select.innerHTML=workshopModifierOptions();}
 if(t.dataset.settingRange)settingChanged(t.dataset.settingRange,Number(t.value));
 else if(t.dataset.settingNumber&&t.validity.valid)settingChanged(t.dataset.settingNumber,t.value===""?null:Number(t.value));
 else if(t.dataset.settingToggle)settingChanged(t.dataset.settingToggle,t.checked?1:0);
 else if(t.dataset.settingChoice)settingChanged(t.dataset.settingChoice,Number(t.value));
 else if(t.id==="timeline-target"){monoTarget.timeline=Number(t.value);renderPage();}
 else if(t.id==="difficulty-target"){monoTarget.difficulty=t.value;renderPage();}
 else if(t.name==="slot"&&t.closest('form')?.dataset.id==="cof_charges"){t.closest('form').querySelector('[name=value]').value=cache.cof_read?.slots.find(s=>s.index===Number(t.value))?.charges??"";}
 else if(t.name==="slot"&&t.closest('form')?.dataset.id==="cofprophecy"){const s=cache.cof_read?.slots.find(s=>s.index===Number(t.value));for(const name of ['reward','lens']){const select=t.closest('form').querySelector('[name='+name+']');const v=s?.[name==='reward'?'rewardId':'lens'];select.value=v==null?'none':String(v);if(select.selectedIndex<0)select.value='none';}}
});
content.addEventListener('dblclick',e=>{const option=e.target.closest('option'),select=e.target.closest('[data-affix-list]');if(select&&!busy)moveWantedAffixes(select.dataset.affixList,option?.value);});
content.addEventListener('keydown',e=>{if(e.key!=='Enter')return;if(e.target.hasAttribute?.('data-affix-search')){e.preventDefault();return;}const select=e.target.closest('[data-affix-list]');if(select){e.preventDefault();if(!busy)moveWantedAffixes(select.dataset.affixList);}});
$("#stat-form").addEventListener("submit",e=>{e.preventDefault();void statAction("set");});
$("#stat-read").onclick=()=>void statAction("read");$("#stat-reset").onclick=()=>void statAction("reset");
$("#stat-close").onclick=()=>$("#stat-dialog").close();$("#stat-mode").onchange=e=>changeStatMode(e.target.value);
$("#stat-basic-modes").onclick=e=>{const b=e.target.closest("[data-bonus-mode]");if(b&&!b.disabled)changeStatMode(b.dataset.bonusMode);};
$("#stat-range").oninput=e=>setStatValue(Number(e.target.value));
$("#stat-value").oninput=e=>{if(e.target.value!==""&&Number.isFinite(Number(e.target.value)))activeStat.drafts[activeStat.mode]=Number(e.target.value);updateStatPreview();};
for(const [id,step] of [["stat-minus",-1],["stat-plus",1]])$("#"+id).onclick=()=>{const b=statLimits();setStatValue(Math.max(b.minimum,Math.min(b.maximum,Number($("#stat-value").value||0)+step)));};
$("#stat-form").addEventListener("input",e=>{if(e.target.id.startsWith("raw-"))syncRawKey();});
$("#stat-form").addEventListener("change",e=>{if(e.target.id.startsWith("raw-")){
 syncRawKey();
 if(["tags","special","extra"].every(n=>$("#raw-"+n).value!==""&&$("#raw-"+n).validity.valid))void statAction("read");
}});
$("#confirm-yes").onclick=()=>finishConfirm(true);$("#confirm-cancel").onclick=()=>finishConfirm(false);$("#confirm-close").onclick=()=>finishConfirm(false);
$("#confirm-dialog").addEventListener("cancel",e=>{e.preventDefault();finishConfirm(false);});$("#result-close").onclick=()=>$("#result-dialog").close();
$("#apply-draft").onclick=()=>void applyDraft();$("#refresh-connection").onclick=()=>void refreshConnection(false);
$("#reset-draft").onclick=()=>{cancelAutomatic();for(const id of [...dirty])values[id]=appliedValues[id];dirty.clear();renderPage();notify("Draft discarded; applied values kept.");};
$("#reconcile").onclick=()=>void runTask({type:"reconcile"},{show:true,title:"Reconcile actor contributions"});
$("#global-search").addEventListener("input",e=>{
 const q=e.target.value.trim().toLowerCase();if(!q){renderPage();return;}
 const matches=catalog.controls.filter(c=>playerControl(c)&&(c.label+" "+c.description+" "+c.id).toLowerCase().includes(q));
 content.innerHTML=`<section class="panel"><h2>Setting search · ${matches.length} results</h2><div class="control-grid">${matches.map(c=>`<button class="settings-card" data-go="${pages[c.group]?c.group:c.group==="session"||c.group==="recovery"?"settings":"stats"}" ${c.group==='crafting'?`data-craft-target="${c.id.startsWith('craft_materials')?'materials':c.id.startsWith('craft_custom')?'custom':'forge'}"`:''}><h3>${esc(c.label)}</h3><p class="muted">${esc(c.description)}</p><span class="badge">${lifetime[c.lifetime]}</span></button>`).join("")}</div></section>`;
});
window.addEventListener("hashchange",()=>void openPage());
async function init(){
 try{
  const r=await fetch("/api/bootstrap");if(!r.ok)throw new Error("Could not load the catalog.");const boot=await r.json();
  catalog=boot.catalog;token=boot.token;preview=boot.preview;developer=boot.developer===true;transport.setSession(boot);
  appVersion=boot.version||"";const versionNode=$("#app-version");if(versionNode)versionNode.textContent=appVersion?`v${appVersion}`:"";
  if(!preview&&readStored("update-check",true)!==false)setTimeout(()=>void checkForUpdate(),1500);
  window.EpochLauncher?.init(boot.launcher,{run:runTask,isBusy:()=>busy,confirm:confirmAction,changed:async(reset=true)=>{if(reset){invalidateLive();dirty.clear();monoDrafts={};}await refreshConnection();}});
  try{const saved=JSON.parse(localStorage.getItem("epochpact.stat-favorites.v1")||"[]");if(Array.isArray(saved))statFavorites=new Set(saved.filter(x=>typeof x==="string"));}catch{}
  profileKey="epochpact.profiles."+(preview?"preview":"live");
  for(const c of catalog.controls)if(["session","actor"].includes(c.lifetime)&&(["number","toggle"].includes(c.widget)||['craft_hope','craft_despair'].includes(c.id)))values[c.id]=appliedValues[c.id]=c.default;
  $("#preview-banner").hidden=!preview;let theme="chronoforge";try{theme=localStorage.getItem("epochpact.theme")||theme;}catch{}setTheme(theme);
  // Show the interface before waiting for the game's first Unity frame.
  await openPage();await refreshConnection();
 }catch(error){content.innerHTML='<p class="empty">Could not start the interface.</p>';notify(error.message,true);}
}
void init();
// Focus refresh complements automatic startup/restart recovery. Both paths
// read state only; opening the UI never submits campaign or reward mutations.
window.addEventListener("focus",()=>{if(catalog&&!busy&&!window.EpochLauncher?.browsing())void refreshConnection(true,true,{preservePage:true});});
