"use strict";
const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const clean = s => String(s ?? "").replace(/<[^>]*>/g, "");
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const pages = {
 overview: ["Overview", "◈", "Your timeline. Your rules.", "Shape your journey, your loot and your character."],
 game_setup: ["Game setup", "⌁", "Connect your game.", "Choose Last Epoch.exe, install the player mod and launch offline."],
 general: ["General settings", "⚙", "Your journey. Your rules.", "Experience, loot, enemies and everyday conveniences."],
 loot: ["Loot & Pickup", "▣", "Keep what matters.", "Automatic collection, loot filters and crafting material pickup."],
 crafting: ["Crafting", "⚒", "Make every craft count.", "Forge your selected item, preserve materials and customize crafting costs."],
 stats: ["Character", "♜", "Refine your character.", "Find a stat and customize its bonus."],
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
const developerControls=new Set(["density_read","map_read","sheet_catalog","sheet_open","resistance_labels","session_read","status","player_read","characters","play_offline","load_character"]);
function playerControl(c){return developer||!developerControls.has(c.id);}
let connected = {connected:false,offline:false}, values = {}, appliedValues = {}, dirty = new Set(), cache = {}, history = [], actorValues = {};
// Only direct edits in General/Overview auto-apply. Reads, profiles and save
// actions never enter this queue. One job at a time; latest unsent value wins.
const autoPending=new Map(),autoErrors=new Map(),autoApplied=new Set();
let autoTimer,autoSending=null,autoRunning=false,autoGeneration=0;
function automaticSetting(c){return ["general","overview"].includes(page)&&c?.group==="general"&&["number","toggle"].includes(c.widget)&&["session","actor"].includes(c.lifetime);}
function automaticOwner(){
 if(preview)return "preview";
 const p=connected.player;
 return connected.connected&&connected.offline&&connected.state==="InGame"&&!connected.transitioning&&p?.id!=null?JSON.stringify([String(p.id),p.name??""]):null;
}
function autoManaged(id){return autoPending.has(id)||autoErrors.has(id)||autoSending?.id===id;}
function cancelAutomatic(){
 clearTimeout(autoTimer);autoGeneration++;
 for(const id of new Set([...autoPending.keys(),...autoErrors.keys(),...(autoSending?[autoSending.id]:[])])){values[id]=appliedValues[id];dirty.delete(id);}
 autoPending.clear();autoErrors.clear();autoApplied.clear();
}
function armAutomatic(){
 clearTimeout(autoTimer);
 if(busy||autoRunning||!autoPending.size)return;
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
 if(busy||autoRunning)return;
 const entry=[...autoPending].find(([,edit])=>force||edit.readyAt<=Date.now());
 if(!entry){armAutomatic();return;}
 const [id,edit]=entry;
 if(edit.generation!==autoGeneration||edit.owner!==automaticOwner()){cancelAutomatic();renderPage();return;}
 autoPending.delete(id);autoSending={id,...edit};autoRunning=true;syncSettingUi(id);
 let errorMessage="Could not apply this setting. Check the connection before retrying.";
 try{
  const args={value:edit.value};
  if(!preview){const [expected_id,expected_name]=JSON.parse(edit.owner);Object.assign(args,{expected_id,expected_name});}
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
function invalidateLive(){
 cancelAutomatic();
 cache={};actorValues={};history=[];connected={connected:false,offline:false};
 if(activeStat){activeStat=null;$("#stat-dialog").close();}
 $("#connection").textContent="○ Game disconnected";$("#connection").classList.remove("connected");
 if(catalog)renderPage();
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
  campaign_complete:"Campaign completed with normal rewards.",waypoints_unlock:"All waypoints unlocked.",
  monolith_unlock:"Normal and Empowered timelines unlocked.",monolith_select:`Timeline selected.${where}`,
  cof_rank:`Circle of Fortune rank set to ${a.value}.`,cof_favor:`Favor set to ${a.value}.`,
  cof_reputation_grant:`Added ${a.value} Reputation.`,cof_join:"Joined Circle of Fortune.",cof_lenses_unlock:"Available lenses unlocked.",
  cofprophecy:`Prophecy slot ${Number(a.slot)+1} updated.`,cof_charges:`Prophecy slot ${Number(a.slot)+1} charges set to ${a.value}.`,
  craft_forge:"Item forged once.",craft_reset:"Normal crafting restored.",loot_reset:"Pickup rules reset.",stat_reset:"All character bonuses reset.",
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
function saveSnapshot(c){return c.id==="craft_forge"?cache.craft_read?.forgePreview:cache[c.group==="campaign"?"progress_read":c.group==="monolith"?"monolith_read":"cof_read"];}
function eligible(control) {
 if(preview)return true;
 if(!connected.connected)return false;
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
  if(c.lifetime==="game_save") {
   const data=saveSnapshot(c);
   reason ||= window.EpochSession.saveActionReason(data,connected,{preview,monolith:c.group==="monolith"});
  }
  if(button.dataset.unavailable==="true")reason ||= "This action is unavailable for the current selection.";
  if(c.id==="craft_forge"&&cache.craft_read?.forgePreview?.canForge!==true)reason ||= cache.craft_read?.forgePreview?.message||"Select an eligible item and material in the game forge, then read live.";
  button.disabled=busy||!!reason;
  button.title=busy?"Waiting for the current operation.":reason||(!eligible(c)&&reconnectingRead?"Refresh game connection and live data.":"");
  const hint=button.closest("form")?.querySelector("[data-action-reason]");
  if(hint){hint.textContent=reason;hint.hidden=!reason||busy;}
 });
 const autoPage=["general","overview"].includes(page),setupPage=page==="game_setup",manualDirty=[...dirty].filter(id=>!autoManaged(id));
 $("#apply-draft").hidden=autoPage||setupPage;$("#reset-draft").hidden=autoPage||setupPage;
 $("#apply-draft").disabled=busy||!manualDirty.length||!connected.connected||(!preview&&!connected.offline);
 $("#reconcile").disabled=busy||!Object.keys(actorValues).length||(!preview&&!connected.offline);
 $("#refresh-connection").disabled=busy;
 $("#reset-draft").disabled=busy;
 document.querySelectorAll('[data-setting-number],[data-setting-range],[data-setting-toggle],[data-setting-reset],[data-step]').forEach(input=>{
  const id=input.dataset.settingNumber||input.dataset.settingRange||input.dataset.settingToggle||input.dataset.settingReset||input.dataset.settingId,c=controlById(id);
  input.disabled=automaticSetting(c)?!eligible(c)||busy&&!autoRunning:busy;
 });
 $("#draft-label").innerHTML=(busy?"Working…":setupPage?"Game setup":autoErrors.size?"A setting needs attention":autoPending.size?"Applying changes…":autoPage?"Changes apply automatically":manualDirty.length?`${manualDirty.length} settings in draft`:"Ready")+"<span>"+(preview?"Preview; no changes to the game.":setupPage?"Choose your executable, then use the setup buttons.":"Save actions use their own buttons.")+"</span>";
 document.querySelectorAll("#stat-dialog button:not(#stat-close), #stat-dialog input, #stat-dialog select").forEach(b=>b.disabled=busy);
 const statAllowed=activeStat&&eligible(controlById(activeStat.tab==="aliases"?"stat_alias":activeStat.tab==="sheet"?"sheet_row":"raw_stat"));
 for(const id of ["stat-apply","stat-reset","stat-read"])$("#"+id).disabled=busy||!statAllowed;
 $("#stat-mode").disabled=busy||activeStat?.tab==="aliases";
}
async function job(payload) {
 return transport.run(payload);
}
async function runTask(payload,{show=false,title="Operation result",silent=false,onError}={}) {
 if(busy)return null;busy=true;let errorBackup=false;updateDisabled();
 try{
  const result=await job(payload);
  if(result.launcher)window.EpochLauncher?.update(result.launcher);
  if(result.requiresElevation)window.EpochLauncher?.failure(result);
  if(result.history)history=result.history;
  if(result.actorValues){actorValues=result.actorValues;syncSpeed();}
  if(!result.ok){errorBackup=!!result.backup;if(result.backup&&developer)resultDialog(result,"Operation error / backup",payload);throw new Error(result.error||result.text||"The action was rejected.");}
  if(show)resultDialog(result,title,payload);
  else if(!silent)notify(developer?result.preview?"Preview complete; the game was unchanged.":"Operation complete.":playerResult(payload,result));
  return result;
 }catch(error){const message=developer?error.message:playerError(error.message,payload)+(errorBackup?" A recovery backup is available in Settings.":"");onError?.(message);notify(message,true);return null;}
 finally{busy=false;updateDisabled();armAutomatic();}
}
async function refreshConnection(silent=true,refreshCurrent=true,{preservePage=false}={}) {
 if(busy)return;
 const previous=connected;
 rememberMonolithOwner(connected);
 const result=await runTask({type:"connection"},{silent});if(!result){invalidateLive();updateDisabled();return;}
 const ownerChanged=rememberMonolithOwner(result);
 if(!preview&&(ownerChanged||!window.EpochSession.current({player:result.player},result)))invalidateLive();
 connected=result;
 // An editable snapshot belongs to the area where it was read, even when
 // the same character moves between an Echo and a Monolith hub.
 if(!preview&&cache.monolith_read&&!window.EpochSession.currentArea(cache.monolith_read,connected)){
  delete cache.monolith_read;delete cache.echo_read;
 }
 const label=preview?"● Preview":result.connected?(result.offline?"● Offline · "+result.state:"● "+result.state):"○ Game disconnected";
 $("#connection").textContent=label;$("#connection").title=result.error||result.text||label;
 $("#connection").classList.toggle("connected",result.connected);
 let valuesChanged=false;
 for(const [id,value] of Object.entries(result.values||{})){appliedValues[id]=value;if(!dirty.has(id)){valuesChanged ||= values[id]!==value;values[id]=value;}}
 const sameArea=preview||!ownerChanged&&window.EpochSession.current({player:previous.player},previous)&&window.EpochSession.currentArea({player:previous.player},connected);
 // A focus read should not rebuild unchanged forms, steal input focus or lose
 // partially typed fields elsewhere in the interface.
 if(!preservePage||!sameArea||valuesChanged)renderPage();else updateDisabled();
 const readId=pageReadId();
 if(refreshCurrent&&readId&&(!cache[readId]||["general","loot","crafting"].includes(page))&&eligible(controlById(readId)))await refreshSection(readId,{refreshConnectionFirst:false});
}
function syncSpeed(){
 const modes=actorValues["9:0:0:0"]?.modes;if(modes){appliedValues.speed=1+(modes.increased||0);if(!dirty.has("speed"))values.speed=appliedValues.speed;}
}
function unitFor(c){return c.id.startsWith("cof_double_")?"%":c.id==="loot_lp"?" LP":c.id==="craft_fp"?"":c.widget==="toggle"?"":"×";}
function settingDisplayValue(id,value){return id==="speed"&&Number.isFinite(value)?Number(value.toFixed(6)):value;}
function settingCard(c) {
 const p=c.parameters.find(x=>x.name==="value"),v=settingDisplayValue(c.id,values[c.id]??c.default),unit=unitFor(c);
 const isToggle=c.widget==="toggle";
 return `<article class="control-card" data-setting="${esc(c.id)}"><div class="control-top"><span class="icon ${esc(c.id)}" aria-hidden="true"></span><div><div class="control-title">${esc(c.label)}</div></div><span class="badge">${lifetime[c.lifetime]}</span></div><p class="control-description">${esc(description(c))}</p><div class="control-tools">${isToggle?`<label class="toggle">${v?"On":"Off"}<input aria-label="${esc(c.label)}" type="checkbox" data-setting-toggle="${c.id}" ${v?"checked":""}></label>`:`<input type="range" aria-label="${esc(c.label)} slider" data-setting-range="${c.id}" min="${p.minimum}" max="${p.maximum}" step="${p.type==="integer"?1:0.1}" value="${v??p.minimum}"><div class="stepper"><button data-step="-1" data-setting-id="${c.id}" aria-label="${esc(c.label)} decrease">−</button><input aria-label="${esc(c.label)} value" data-setting-number="${c.id}" type="number" min="${p.minimum}" max="${p.maximum}" step="${p.type==="integer"?1:"any"}" value="${v??""}" placeholder="Rank" ${c.default===null?'':'required'}><button data-step="1" data-setting-id="${c.id}" aria-label="${esc(c.label)} increase">+</button></div><span class="unit">${unit}</span>`}<button class="quiet" data-setting-reset="${c.id}">Default</button></div><div class="limits">${isToggle?`Default: ${c.default?"on":"off"}`:`${p.minimum}–${p.maximum}${unit} · Default: ${c.default??"rank bonus"}`} <span class="setting-feedback" data-setting-feedback="${c.id}" role="status">${settingFeedback(c.id)}</span></div></article>`;
}
function settingPanel(title,controls){return `<section class="panel"><div class="section-head"><h2>${title}</h2><span class="badge">${controls.every(automaticSetting)?"Changes apply automatically":"Use Apply changes below"}</span></div><div class="control-grid">${controls.map(settingCard).join("")}</div></section>`;}
function actionCard(c, fields="", extra=""){
 return `<article class="control-card"><div class="control-top"><h3 class="control-title">${esc(c.label)}</h3><span class="badge ${c.lifetime==="game_save"?"save":""}">${lifetime[c.lifetime]}</span></div><p class="control-description">${esc(description(c))}</p><form class="control-form" data-id="${c.id}"><div class="form-grid">${fields||parameterFields(c)}</div>${extra}<p class="action-reason" data-action-reason role="status" hidden></p><div class="actions"><button type="submit" data-control-id="${c.id}" class="${c.lifetime==="game_save"?"primary":""}">${c.widget==="read"?"Read live":c.lifetime==="game_save"?"Apply action":c.widget==="preview"?"Preview":"Apply"}</button></div></form></article>`;
}
function options(list,selected,none=false){const picked=Array.isArray(selected)?selected.map(String):[String(selected)];return (none?`<option value="none" ${selected==null?'selected':''}>Clear selection (none)</option>`:"")+list.map(x=>`<option value="${esc(x.id)}" ${picked.includes(String(x.id))?"selected":""}>${esc(clean(x.uiLabel||x.name||x.id))}</option>`).join("");}
function field(name,label,html){return `<label>${esc(label)}${html}</label>`;}
function parameterFields(c){
 return c.parameters.filter(p=>p.name!=="save_id").map(p=>{
  const name=p.name;let html;
  if(name==="timeline")html=`<select name="timeline">${options(cache.monolith_read?.timelines||[],monoTarget.timeline)}</select>`;
  else if(name==="difficulty")html=`<select name="difficulty"><option value="normal" ${monoTarget.difficulty==="normal"?"selected":""}>Normal</option><option value="empowered" ${monoTarget.difficulty==="empowered"?"selected":""}>Empowered</option></select>`;
  else if(name==="slot")html=`<select name="slot">${(cache.cof_read?.slots||[]).filter(x=>!x.locked).map(x=>`<option value="${x.index}">Slot ${x.index+1} · Rank ${x.rankRequired}</option>`).join("")}</select>`;
  else if(name==="reward")html=`<select name="reward">${options((cache.cof_read?.rewards||[]).filter(x=>x.available),cache.cof_read?.slots.find(s=>!s.locked)?.rewardId,true)}</select>`;
  else if(name==="lens")html=`<select name="lens">${options((cache.cof_read?.lenses||[]).filter(x=>x.purchased&&x.rankRequired<=cache.cof_read.cof.rank),cache.cof_read?.slots.find(s=>!s.locked)?.lens,true)}</select>`;
  else if(name==="backup")html=`<select name="backup">${history.map(x=>`<option value="${esc(x.backup)}">${esc(controlById(x.control)?.label)} · ${new Date(x.time*1000).toLocaleTimeString("en-GB")}</option>`).join("")}</select>`;
  else if(p.choices){const selected=c.id==="loot_mode"?cache.loot_read?.mode??c.default:null;html=`<select name="${name}">${p.choices.map(x=>`<option value="${x}" ${x===selected?"selected":""}>${x}</option>`).join("")}</select>`;}
  else if(c.id==="loot_affixes")html=`<select name="ids" multiple size="8" aria-label="Wanted T7 affixes">${options(cache.loot_read?.affixes||[],cache.loot_read?.affixIds||[])}</select><span class="note">Refresh pickup rules to load affix names. Hold Ctrl to select several; no selection accepts any T7.</span>`;
 else {const liveDefault=name==="value"?({craft_hope:cache.craft_read?.hopePercent,craft_despair:cache.craft_read?.despairPercent,cof_rank:cache.cof_read?.cof?.rank,cof_favor:cache.cof_read?.cof?.favor,cof_charges:cache.cof_read?.slots?.find(s=>!s.locked)?.charges}[c.id]??c.default):c.default;html=`<input name="${name}" ${p.type==="single_token"?'type="text"':'type="number"'} ${p.minimum!=null?`min="${p.minimum}"`:""} ${p.maximum!=null?`max="${p.maximum}"`:""} step="${p.type.includes("integer")?"1":"any"}" ${liveDefault!=null?`value="${liveDefault}"`:""} ${p.type==="number_or_reset"?'placeholder="Normal chance (blank resets)"':""} ${["optional_integer","number_or_reset"].includes(p.type)?"":"required"}>`;}
  return field(name,{value:"Value",name:"Character name",level:"Level (optional)",slot:"Prophecy slot",reward:"Reward",lens:"Lens",backup:"Operation backup",mode:"Selection",ids:"Affixes",category:"Pickup category"}[name]||name,html);
 }).join("");
}
function cachedSummary(id){
 const d=cache[id];if(!window.EpochSession.current(d,connected,preview))return '<div class="note">Refresh live data. Load an offline character, then refresh to see current values.</div>';
 const p=d.player;return `<div class="note">${preview?"Catalog snapshot · ":"Live reading · "}${esc(p?.name)}${developer?` · Save ${esc(p?.id)}`:""}${p?.level!=null?` · Level ${p.level}`:""}${developer&&p?.scene?` · ${esc(p.scene)}`:""}${d.editable===false?' · Monolith editing is unavailable in this area.':''}</div>`;
}
function navigation(){
 $("#nav").innerHTML=Object.entries(pages).map(([id,p])=>`<a class="nav-item" href="#${id}" ${page===id?'aria-current="page"':''}><span class="nav-symbol" aria-hidden="true">${p[1]}</span>${p[0]}</a>`).join("");
}
function go(id){
 if(!(id in pages))id="overview";
 $("#global-search").value="";
 if(location.hash==="#"+id)void openPage();else location.hash=id;
}
function pageReadId(){return {general:"map_read",loot:"loot_read",crafting:"craft_read",campaign:"progress_read",monolith:"monolith_read",cof:"cof_read",factions:"factions_read"}[page];}
async function openPage(){
 page=location.hash.slice(1)||"overview";if(!(page in pages))page="overview";
 $("#global-search").value="";
 if(page==="atlas"||page==="stash"){collectionState.filter="all";collectionState.page=0;collectionState.selected=null;}
 statsPage=0;renderPage();$("#main-scroll").scrollTop=0;
 const readId=pageReadId();
 if(readId&&eligible(controlById(readId))&&(!cache[readId]||["general","loot","crafting"].includes(page)))await refreshSection(readId);
}
function renderPage(){
 navigation();const p=pages[page];$("#breadcrumb").textContent="EpochPact / "+p[0];$("#page-title").textContent=p[2];$("#page-subtitle").textContent=p[3];
 if(page==="overview"){
  content.innerHTML=`<div class="summary-grid"><div class="summary"><strong>${catalog.controls.filter(playerControl).length}</strong><span>Available tools</span></div><div class="summary"><strong>${catalog.counts.sheetRows}</strong><span>Character bonuses</span></div><div class="summary"><strong>${catalog.counts.timelines}</strong><span>Monolith timelines</span></div><div class="summary"><strong>${catalog.counts.statAliases}</strong><span>Common stats</span></div></div>`+settingPanel("Quick controls",["xp","speed","drops","gold"].map(controlById))+`<section class="panel"><h2>Explore your workshop</h2><div class="control-grid">${["stats","loot","crafting","campaign","monolith","cof"].map(id=>`<button data-go="${id}" class="settings-card"><h3>${pages[id][1]} ${pages[id][0]}</h3><p class="muted">${pages[id][3]}</p></button>`).join("")}</div></section>`;
 }else if(page==="game_setup"){
  content.innerHTML='';
 }else if(page==="general"){
  content.innerHTML=settingPanel("General game settings",catalog.controls.filter(c=>c.group==="general"&&["number","toggle"].includes(c.widget)&&!c.id.startsWith("loot_")&&!c.id.startsWith("craft_")&&!c.id.startsWith("map_")))+
   settingPanel("Map visibility",[controlById("map_reveal")])+(developer?`<details class="panel"><summary>Map and density diagnostics</summary>${["map_read","density_read"].map(id=>actionCard(controlById(id))).join("")}</details>`:"");
 }else if(page==="loot"){
  content.innerHTML=settingPanel("Automatic collection",[controlById("autopickup")])+settingPanel("Quality rules",["loot_lp","loot_t7","loot_filter"].map(controlById))+`<section class="panel"><h2>Pickup selections</h2><div class="control-grid">${["loot_mode","loot_affixes","loot_category"].map(id=>actionCard(controlById(id))).join("")}</div></section><section class="panel"><div class="section-head"><h2>Pickup rules</h2><button data-refresh="loot_read" data-control-id="loot_read">Refresh rules</button></div><div class="note">Current selection: ${esc({all:"All items",filter:"Game loot filter",quality:"LP and T7 quality rules",materials:"Crafting materials"}[cache.loot_read?.mode]||"Refresh to read your rules")}.</div>${developer?actionCard(controlById("loot_read")):""}${actionCard(controlById("loot_reset"))}</section>`;
 }else if(page==="crafting"){
  const forge=cache.craft_read?.forgePreview;
  content.innerHTML=`<section class="panel"><div class="section-head"><h2>Game forge</h2><button data-refresh="craft_read" data-control-id="craft_read">Refresh live</button></div><p class="note">${esc(forge?.canForge?"Selected craft is ready. Forge selected item once performs one craft and creates a backup.":forge?.message||"Select an item and materials in the game's forge, then refresh live.")}</p><div class="control-grid">${(developer?["craft_read","craft_forge"]:["craft_forge"]).map(id=>actionCard(controlById(id))).join("")}</div></section>`+
   settingPanel("Cost and material preservation",["craft_fp","craft_shards","craft_runes","craft_glyphs"].map(controlById))+
   `<section class="panel"><h2>Glyph chances</h2><div class="control-grid">${["craft_hope","craft_despair"].map(id=>actionCard(controlById(id))).join("")}</div></section>`+
   settingPanel("Crafting requirements",[controlById("craft_level")])+`<section class="panel"><h2>Restore normal crafting</h2>${actionCard(controlById("craft_reset"))}</section>`;
 }else if(page==="stats")renderStats();
 else if(page==="campaign"){
  content.innerHTML=cachedSummary("progress_read")+`<section class="panel"><div class="section-head"><h2>Campaign and waypoints</h2><button data-refresh="progress_read" data-control-id="progress_read">Refresh live</button></div><div class="control-grid">${["campaign_complete","waypoints_unlock"].map(id=>actionCard(controlById(id))).join("")}</div><details><summary>Quest rewards and coverage (${catalog.counts.eligibleCampaignQuests} quests / ${catalog.counts.waypoints} waypoints)</summary><p class="muted">Completion grants normal rewards and raises the character to at least level 55. Completed quests do not grant rewards again. Test, repeatable and endgame quests are excluded.</p>${questTable()}</details></section>`;
 }else if(page==="atlas"||page==="stash")renderCollection();
 else if(page==="monolith")renderMonolith();
 else if(page==="cof")renderCof();
 else if(page==="factions"){
  const factions=cache.factions_read?.factions||[];
  content.innerHTML=`<section class="panel"><div class="section-head"><h2>Factions</h2><button data-refresh="factions_read" data-control-id="factions_read">Refresh live</button></div><div class="note">Merchant’s Guild and Weaver management actions are not implemented. This section reads information only.</div><div class="control-grid">${factions.map(f=>`<article class="control-card"><h3>${esc(f.name)}</h3><span class="badge">Information only</span><p class="muted">${f.member?"Member":"Not a member"} · Rank ${f.rank} · Favor ${f.favor} · Reputation ${f.reputation}</p>${f.weaverPoints?`<p>Weaver points: ${f.weaverPoints.earned} / ${f.weaverPoints.maxTotal} (${f.weaverPoints.maxRank} rank + ${f.weaverPoints.maxEchoes} echo)</p>`:""}<details><summary>Rank rewards</summary>${f.ranks.map(r=>`<p><b>${r.index+1} · ${esc(r.title)}</b><br>${esc(clean(r.description))}</p>`).join("")}</details></article>`).join("")||'<p class="empty">Live faction information has not been read yet.</p>'}</div>${developer?`<details><summary>Management features in development</summary>${catalog.researchOnly.map(r=>`<p><b>${esc(r.label)}</b><br>${esc(r.details||r.availability)}</p>`).join("")}</details>`:""}</section>`;
 }else renderSettings();
 if(page==="monolith"){content.insertAdjacentHTML("beforeend",navigatorPanel());renderEchoResults();}
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
 content.innerHTML=cachedSummary("monolith_read")+monolithDensityPanel()+`<section class="panel"><div class="section-head"><h2>Monolith management</h2><button data-refresh="monolith_read" data-control-id="monolith_read">Refresh live</button></div><p class="muted">Opening a timeline or showing an Echo travels to Traveler's Rest first. Refreshing data does not travel or start an Echo.</p><div class="form-grid"><label>Timeline<select id="timeline-target">${options(timelines,monoTarget.timeline)}</select></label><label>Difficulty<select id="difficulty-target"><option value="normal" ${monoTarget.difficulty==="normal"?"selected":""}>Normal</option><option value="empowered" ${monoTarget.difficulty==="empowered"?"selected":""}>Empowered</option></select></label></div><p class="muted">${d?`Level ${d.level} · ${d.unlocked?"Unlocked":"Locked"} · Corruption ${d.minCorruption}–${d.maxCorruption} · Stability 0–${d.maxStability}`:"Refresh the live timeline catalog."}</p><div class="control-grid">${actionCard(controlById("monolith_unlock"))}${actionCard(controlById("monolith_select"),targetFields)}${absolute("corruption","Corruption",d?.maxCorruption,d?.minCorruption,d?.run?.corruption)}${absolute("stability","Stability",d?.maxStability,0,d?.run?.stability)}</div></section>`;
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
  return match&&(statsCategory==="All"||ST.category(row,statsTab)===statsCategory)&&(!statsFavoritesOnly||statFavorites.has(ST.favoriteId(row,statsTab)));
 });
 const totalPages=Math.max(1,Math.ceil(rows.length/24));statsPage=Math.min(statsPage,totalPages-1);
 $("#stat-count").textContent=rows.length+(rows.length===1?" stat":" stats");
 $("#stat-results").innerHTML=`<div class="stat-list">${rows.slice(statsPage*24,statsPage*24+24).map(({row:r,index:i})=>{
  const name=ST.name(r,statsTab),favorite=statFavorites.has(ST.favoriteId(r,statsTab));
  return `<div class="stat-entry"><button class="stat-open" data-stat-index="${i}"><strong>${esc(name)}</strong><small>${ST.category(r,statsTab)}${r.derived?" · Calculated by the game":""}</small></button>${r.modifierKey?`<button class="secondary" data-stat-index="${i}" data-secondary="true" title="Edit the secondary modifier">Extra</button>`:""}<button class="stat-star ${favorite?"selected":""}" data-stat-favorite="${i}" aria-pressed="${favorite}" aria-label="${favorite?"Remove":"Add"} ${esc(name)} ${favorite?"from":"to"} favorites">${favorite?"★":"☆"}</button><span aria-hidden="true">›</span></div>`;
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
 content.innerHTML=`<section class="panel"><h2>Appearance · Theme</h2><p class="muted">Both themes use the same settings. Switching themes sends no game commands, and your choice is remembered.</p><div class="control-grid theme-grid">${[["chronoforge","Chronoforge","Jade time crystal · aged gold · ancient temple"],["void-atlas","Void Atlas","Violet time rift · horizontal navigation · Void observatory"]].map(([id,title,note])=>`<button data-theme-pick="${id}" class="settings-card ${theme===id?"selected":""}" aria-pressed="${theme===id}"><img src="/ui/assets/${id}-reference.png" alt="${title} design reference"><h3>${title}</h3><p class="muted">${note}</p></button>`).join("")}</div></section><section class="panel"><h2>Draft profiles</h2><div class="note">Profiles load settings into a draft. Quests, rewards, balances and save actions are not stored or automatically applied.</div><div class="form-grid"><label>Profile name<input id="profile-name" placeholder="Example: Balanced"></label><label>Saved draft<select id="profile-select">${Object.keys(profiles).map(name=>`<option>${esc(name)}</option>`).join("")}</select></label></div><div class="actions"><button id="profile-save">Save draft</button><button id="profile-load">Load draft</button><button id="profile-delete">Delete profile</button></div></section>${developer?`<section class="panel"><h2>Game connection</h2><div class="control-grid">${["session_read","status","player_read","characters","play_offline","load_character"].map(id=>actionCard(controlById(id))).join("")}</div></section>`:""}<section class="panel"><h2>Recovery backups</h2><p class="muted">Changes to your game save create a backup. Restore a selected backup to undo that change; the game closes normally first.</p>${history.length?`<div class="table-wrap"><table><thead><tr><th>Action</th><th>Time</th><th>Result</th></tr></thead><tbody>${history.map(x=>`<tr><td>${esc(controlById(x.control)?.label)}</td><td>${new Date(x.time*1000).toLocaleTimeString("en-GB")}</td><td>${x.ok?"Complete":"Error / backup available"}</td></tr>`).join("")}</tbody></table></div>`:'<p class="empty">No save actions have been performed in this session.</p>'}${actionCard(controlById("undo"))}</section>`;
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
 const readId=id.startsWith("map_")?"map_read":id.startsWith("loot_")?"loot_read":id.startsWith("craft_")?"craft_read":null;
 if(!readId)return false;
 if(id==="craft_forge")result={...result.crafting,...result};
 cache[readId]={...cache[readId],...result};
 const toggle=x=>x===undefined?undefined:Number(x);
 const fresh=readId==="map_read"?{map_reveal:toggle(result.enabled)}:readId==="loot_read"?{loot_lp:result.minimumLP,loot_t7:toggle(result.t7),loot_filter:toggle(result.respectFilter)}:
  {craft_fp:result.fpFactor,craft_shards:toggle(result.preserveShards),craft_runes:toggle(result.preserveRunes),craft_glyphs:toggle(result.preserveGlyphs),craft_level:toggle(result.bypassLevel)};
 for(const [key,value] of Object.entries(fresh))if(value!==undefined){appliedValues[key]=value;if(!dirty.has(key))values[key]=value;}
 return true;
}
function formArgs(form,c){
 const data=new FormData(form),args={};
 for(const p of c.parameters){if(p.name==="save_id")continue;let v=data.get(p.name);if(p.type==="optional_integer"&&(v===""||v===null))continue;
  args[p.name]=c.id==="loot_affixes"?data.getAll("ids").join(",")||"none":p.type==="number_or_reset"?(v===""?null:Number(v)):p.type==="integer_or_none"&&v==="none"?null:p.type.includes("integer")||p.type==="number"?Number(v):v;
 }
 if(c.parameters.some(p=>p.name==="save_id"))args.expected_id=saveSnapshot(c)?.player?.id;
 if(c.id==="cof_join")args.switch_from_merchant=data.get("switch_from_merchant")==="on";
 return args;
}
async function formAction(form,override){
 const c=controlById(override||form.dataset.id);if(!form.reportValidity())return;
 // Capture this form's target before a confirmation or refresh can replace it.
 const args=formArgs(form,c),draftKey=monoDraftKey(c.id),submittedDraft=monoDrafts[draftKey],submittedName=connected.player?.name;
 if(c.lifetime==="game_save"||c.lifetime==="recovery"){
  const accepted=await confirmAction(c.label,c.description+(c.lifetime==="recovery"?" The game will close normally and the selected operation backup will be restored.":" This action changes the game save. The backend creates a backup before applying it."));if(!accepted)return;
 }
 if(c.lifetime==="game_save"&&!preview){
  const snapshot=saveSnapshot(c);
  const reason=window.EpochSession.saveActionReason(snapshot,connected,{monolith:c.group==="monolith"});
  if(reason||String(args.expected_id)!==String(connected.player?.id)||submittedName&&connected.player?.name&&submittedName!==connected.player.name){notify(reason||"The character changed. Review the action for the loaded character.",true);return;}
 }
 const result=await runTask({type:"control",id:c.id,args,operation:c.widget==="read"||c.widget==="preview"?"read":"set"},{show:true,title:c.label});
 if(!result)return;
 if(syncLootCraft(c.id,result))renderPage();
 else if(c.widget==="read"&&["progress_read","monolith_read","cof_read","factions_read"].includes(c.id)){
  cache[c.id]=result;
  renderPage();
 }
 else if(c.lifetime==="game_save"){
  if(["corruption","stability"].includes(c.id)&&monoDrafts[draftKey]===submittedDraft)delete monoDrafts[draftKey];
  const readId={campaign:"progress_read",monolith:"monolith_read",cof:"cof_read"}[c.group];delete cache[readId];if(readId)await refreshSection(readId);
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
 const ids=[...dirty].filter(id=>!autoManaged(id));let applied=0;
 let refreshForge=false;
 for(const id of ids){
  const c=controlById(id);if(!c||!["actor","session"].includes(c.lifetime))continue;
  const submitted=values[id];const r=await runTask({type:"control",id,args:{value:submitted??"reset"}},{silent:true});
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
content.addEventListener("click",async e=>{
 const b=e.target.closest("button");if(!b||b.disabled)return;
 if(b.dataset.go)go(b.dataset.go);
 else if(b.dataset.refresh)await refreshSection(b.dataset.refresh);
 else if(b.dataset.themePick)setTheme(b.dataset.themePick);
 else if(b.dataset.autoRetry){const id=b.dataset.autoRetry;queueAutomatic(id,values[id],0);}
 else if(b.dataset.settingReset){const c=controlById(b.dataset.settingReset);settingChanged(c.id,c.default);}
 else if(b.dataset.step){const id=b.dataset.settingId,c=controlById(id),p=c.parameters[0];settingChanged(id,Math.min(p.maximum,Math.max(p.minimum,Number(((values[id]??p.minimum)+Number(b.dataset.step)*(p.type==="integer"?1:.1)).toFixed(2)))));}
 else if(b.dataset.statTab){statsTab=b.dataset.statTab;statsPage=0;renderStats();updateDisabled();}
 else if(b.dataset.statCategory){statsCategory=b.dataset.statCategory;statsPage=0;renderStats();updateDisabled();}
 else if(b.dataset.statFavorite!==undefined)toggleStatFavorite(Number(b.dataset.statFavorite));
 else if(b.id==="stat-favorites-filter"){statsFavoritesOnly=!statsFavoritesOnly;statsPage=0;renderStats();updateDisabled();}
 else if(b.id==="stat-clear-filters"){statsFavoritesOnly=false;statsCategory="All";statsQuery="";statsPage=0;renderStats();updateDisabled();}
 else if(b.dataset.statIndex!==undefined)openStat(Number(b.dataset.statIndex),b.dataset.secondary==="true");
 else if(b.dataset.statPage){statsPage+=Number(b.dataset.statPage);renderStatResults();}
 else if(b.hasAttribute("data-prophecy-preview"))await formAction(b.closest("form"),"cofpreview");
 else if(b.id==="all-stats-reset"){
  if(await confirmAction("Remove all contributions","Removes only EpochPact stat contributions. Equipment stats are kept. Controls sharing the same full stat key, including movement speed, are refreshed.")){
   const r=await runTask({type:"control",id:"stat_reset"},{show:true});if(r){actorValues={};values.speed=1;dirty.delete("speed");renderPage();}
  }
 }else if(b.id==="profile-save"){
  const name=$("#profile-name").value.trim();if(!name){notify("Enter a profile name.",true);return;}
  let profiles={};try{profiles=JSON.parse(localStorage.getItem(profileKey)||"{}");}catch{}
  const profile={};for(const c of catalog.controls)if(c.lifetime==="session"&&c.widget==="number"||c.id==="autopickup"||c.id==="speed")profile[c.id]=values[c.id]??c.default;
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
 const t=e.target;rememberMonolithInput(t);if(t.dataset.settingRange)settingChanged(t.dataset.settingRange,Number(t.value));
 else if(t.dataset.settingNumber&&t.validity.valid)settingChanged(t.dataset.settingNumber,t.value===""?null:Number(t.value));
 else if(t.dataset.settingToggle)settingChanged(t.dataset.settingToggle,t.checked?1:0);
 else if(t.id==="timeline-target"){monoTarget.timeline=Number(t.value);renderPage();}
 else if(t.id==="difficulty-target"){monoTarget.difficulty=t.value;renderPage();}
 else if(t.name==="slot"&&t.closest('form')?.dataset.id==="cof_charges"){t.closest('form').querySelector('[name=value]').value=cache.cof_read?.slots.find(s=>s.index===Number(t.value))?.charges??"";}
 else if(t.name==="slot"&&t.closest('form')?.dataset.id==="cofprophecy"){const s=cache.cof_read?.slots.find(s=>s.index===Number(t.value));for(const name of ['reward','lens']){const select=t.closest('form').querySelector('[name='+name+']');const v=s?.[name==='reward'?'rewardId':'lens'];select.value=v==null?'none':String(v);if(select.selectedIndex<0)select.value='none';}}
});
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
 content.innerHTML=`<section class="panel"><h2>Setting search · ${matches.length} results</h2><div class="control-grid">${matches.map(c=>`<button class="settings-card" data-go="${pages[c.group]?c.group:c.group==="session"||c.group==="recovery"?"settings":"stats"}"><h3>${esc(c.label)}</h3><p class="muted">${esc(c.description)}</p><span class="badge">${lifetime[c.lifetime]}</span></button>`).join("")}</div></section>`;
});
window.addEventListener("hashchange",()=>void openPage());
async function init(){
 try{
  const r=await fetch("/api/bootstrap");if(!r.ok)throw new Error("Could not load the catalog.");const boot=await r.json();
  catalog=boot.catalog;token=boot.token;preview=boot.preview;developer=boot.developer===true;transport.setSession(boot);
  window.EpochLauncher?.init(boot.launcher,{run:runTask,isBusy:()=>busy,changed:async(reset=true)=>{if(reset){invalidateLive();dirty.clear();monoDrafts={};}await refreshConnection();}});
  try{const saved=JSON.parse(localStorage.getItem("epochpact.stat-favorites.v1")||"[]");if(Array.isArray(saved))statFavorites=new Set(saved.filter(x=>typeof x==="string"));}catch{}
  profileKey="epochpact.profiles."+(preview?"preview":"live");
  for(const c of catalog.controls)if(["session","actor"].includes(c.lifetime)&&["number","toggle"].includes(c.widget))values[c.id]=appliedValues[c.id]=c.default;
  $("#preview-banner").hidden=!preview;let theme="chronoforge";try{theme=localStorage.getItem("epochpact.theme")||theme;}catch{}setTheme(theme);
  await refreshConnection();await openPage();
 }catch(error){content.innerHTML='<p class="empty">Could not start the interface.</p>';notify(error.message,true);}
}
void init();
// Returning from the game refreshes its identity without a permanent polling
// watcher. Opening the UI never submits campaign or reward mutations.
window.addEventListener("focus",()=>{if(catalog&&!busy&&!window.EpochLauncher?.browsing())void refreshConnection(true,true,{preservePage:true});});
