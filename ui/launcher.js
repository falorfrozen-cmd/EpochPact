/* Player setup is explicit. Opening this interface never installs; it launches the game only when the
   player turned on "Launch the game when EpochPact starts" (once per app start, with the mod installed
   and the game not running). */
(function(root){
 'use strict';
 let state=null,run,changed,isBusy,confirm,elevation=false,pickerBusy=false,pathDraft=null,modAfterUpdate=false;
 // The player's own choice, kept with the other interface choices (storage-sync mirrors it to the file).
 const autoKey='epochpact.auto-launch.v1';
 function autoLaunch(){try{return JSON.parse(root.localStorage?.getItem(autoKey)||'false')===true;}catch{return false;}}
 const escape=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
 function statusText(){return state.updateRequired?'Mod update required':state.melonLoader?(state.installed?'MelonLoader bridge installed':'MelonLoader detected'):state.foreignLoader?'Another mod loader is installed':state.disabled?'Mod disabled':state.installed?'Player mod installed':state.selected?'Ready to install':'Choose your game';}
 function section(){
  if(!state)return '';
  const status=statusText();
  return `<section class="panel game-setup"><div class="section-head"><h2>Game setup</h2><span class="badge">${status}</span></div><p class="muted">${state.updateRequired?'The installed mod belongs to another EpochPact package. Close Last Epoch and reinstall using this complete download.':'Select your Last Epoch executable once. Install the mod with the game closed, then launch offline.'}</p><label>Game executable<div class="game-path-row"><input data-game-path aria-label="Last Epoch executable path" value="${escape(state.executable)}" placeholder="Select Last Epoch.exe"><button data-game-choose>Browse…</button><button data-game-save>Save path</button></div></label><p data-game-problem class="muted">${escape(state.problem||'The file picker opens in your Steam folder. You can browse to any other installation.')}</p><div class="actions"><button data-game-install> ${state.installed||state.updateRequired?'Reinstall / update mod':'Install mod'}</button>${elevation?'<button data-game-elevate>Install as administrator</button>':''}${state.installed||state.updateRequired?'<button data-game-uninstall>Uninstall mod</button>':''}<button class="primary" data-game-launch>${state.running?'Game running':'Launch offline'}</button></div><label class="inline-check"><input type="checkbox" data-game-autolaunch ${autoLaunch()?'checked':''}> Launch the game when EpochPact starts</label><p class="muted">${state.running?'Close the game before installing or changing its location.':'Offline characters only. No gameplay settings are applied on startup.'}</p><p data-setup-feedback class="setup-feedback" role="status"></p><p class="muted">Need help? <a href="https://discord.gg/q6aexZZSAf" target="_blank" rel="noopener">EpochPact Discord</a></p></section>`;
 }
 // Overview: one line with the state and the next step; the full panel lives on Game setup.
 function summary(){
  if(!state)return '';
  const needsSetup=!state.selected||!state.installed||state.updateRequired||state.disabled||state.foreignLoader;
  return `<section class="panel game-setup setup-summary"><div class="section-head"><h2>Game</h2><span class="badge">${statusText()}</span></div><div class="actions">${needsSetup?'<a class="button primary" href="#game_setup">Open Game setup</a>':''}${state.installed&&!state.updateRequired?`<button ${needsSetup?'':'class="primary"'} data-game-launch>${state.running?'Game running':'Launch offline'}</button>`:''}</div><p data-setup-feedback class="setup-feedback" role="status"></p></section>`;
 }
 function render(){
  for(const node of document.querySelectorAll('[data-launcher-summary]'))node.innerHTML=summary();
  for(const node of document.querySelectorAll('[data-launcher-panel]')){
   const input=node.querySelector('[data-game-path]');
   if(input&&input.value!==input.defaultValue)pathDraft=input.value;
   node.innerHTML=section();
   if(pathDraft!==null)node.querySelector('[data-game-path]').value=pathDraft;
  }
  disable();
 }
 function disable(){
  for(const b of document.querySelectorAll('.game-setup button')){
   b.disabled=(isBusy?.()||false)||((b.hasAttribute('data-game-install')||b.hasAttribute('data-game-elevate')||b.hasAttribute('data-game-uninstall'))&&(!state?.selected||state.running||state.runningUnknown||state.foreignLoader&&!state.melonLoader))||
    (b.hasAttribute('data-game-launch')&&(!state?.installed||state.running||state.disabled));
  }
 }
 function feedback(message,error=false){
  for(const el of document.querySelectorAll('[data-setup-feedback]')){el.textContent=message;el.classList.toggle('setting-error',error);}
 }
 async function action(name,args={}){
  if(isBusy())return;
  let failure='';
  const result=await run({type:'launcher',action:name,args},{silent:true,onError:message=>{failure=message;}});
  if(!result){feedback(failure||'Could not complete game setup. No action was automatically retried.',true);return;}
  if(result.launcher)state=result.launcher;
  if(name==='select'){pathDraft=null;for(const input of document.querySelectorAll('[data-game-path]'))input.value=input.defaultValue=state.executable;}
  elevation=false;render();feedback(result.message||'Game setup refreshed.');
  if(name==='select')await changed();
  else if(name==='install'||name==='launch'||name==='elevated_install'||name==='uninstall')await changed(false);
 }
 // After Install update the mod in the game folder belongs to the old version: update it
 // once, as soon as Last Epoch is closed (the game holds its loaded mod files).
 function finishAppUpdate(){
  if(!modAfterUpdate||!state?.updateRequired||state.running||state.runningUnknown||isBusy?.())return;
  modAfterUpdate=false;feedback('Updating the mod in the game folder for the new EpochPact version…');void action('install');
 }
 async function browse(){
  if(isBusy())return;
  if(!root.pywebview?.api){feedback('Use the desktop app to browse, or enter the full path to Last Epoch.exe and press Save path.',true);return;}
  try{
   pickerBusy=true;
   const chosen=await root.pywebview.api.choose_game_executable();
   if(chosen)await action('select',{executable:chosen});
  }catch(error){feedback('Could not open the file picker. You can enter the full executable path instead.',true);}
  finally{pickerBusy=false;}
 }
 document.addEventListener('click',async event=>{
  const b=event.target.closest('button');if(!b||b.disabled)return;
  if(b.hasAttribute('data-game-choose'))await browse();
  else if(b.hasAttribute('data-game-save'))await action('select',{executable:b.closest('.game-setup').querySelector('[data-game-path]').value});
  else if(b.hasAttribute('data-game-install'))await action('install');
  else if(b.hasAttribute('data-game-elevate'))await action('elevated_install');
  else if(b.hasAttribute('data-game-launch'))await action('launch');
  else if(b.hasAttribute('data-game-uninstall')){
   const confirmed=await (confirm?.('Uninstall EpochPact?','This removes EpochPact’s loader and core from the game folder. Another loader and its mods, your saves, logs and recovery backups are kept.')??Promise.resolve(false));
   if(confirmed)await action('uninstall');
  }
  else if(b.hasAttribute('data-setup-done'))document.querySelector('#setup-dialog').close();
 });
 document.addEventListener('input',event=>{if(event.target.matches?.('[data-game-path]'))pathDraft=event.target.value;});
 document.addEventListener('change',event=>{
  if(!event.target.matches?.('[data-game-autolaunch]'))return;
  try{root.localStorage.setItem(autoKey,JSON.stringify(event.target.checked===true));}catch{feedback('This choice could not be saved on this device.',true);return;}
  feedback(event.target.checked?'Last Epoch will be launched offline each time EpochPact starts.':'EpochPact no longer launches the game when it starts.');
 });
 // Once per app start (a reload of the page does not launch again), only when it can launch now.
 function startGame(tries=0){
  if(!autoLaunch()||!state?.installed||state.running||state.runningUnknown||state.updateRequired||state.disabled)return;
  if(isBusy?.()){if(tries<20)root.setTimeout?.(()=>startGame(tries+1),500);return;}
  try{if(root.sessionStorage?.getItem('epochpact.auto-launched'))return;root.sessionStorage?.setItem('epochpact.auto-launched','1');}catch{}
  feedback('Launching Last Epoch offline (Launch the game when EpochPact starts).');
  void action('launch');
 }
 root.EpochLauncher={
  init(initial,options){state=initial;({run,changed,isBusy,confirm}=options);render();
   if(state&&!state.selected)document.querySelector('#setup-dialog').showModal();
   startGame();
  },
  update(next){if(next&&JSON.stringify(next)!==JSON.stringify(state)){state=next;render();}finishAppUpdate();},
  state:()=>state,
  updateModAfterAppUpdate(){
   modAfterUpdate=true;
   if(state?.updateRequired&&(state.running||state.runningUnknown))feedback('Close Last Epoch to finish the update: the mod files in the game folder are then updated automatically.');
   finishAppUpdate();
  },
  mount(page){if(!state)return;if(page==='game_setup')document.querySelector('#page-content').insertAdjacentHTML('afterbegin','<div data-launcher-panel></div>');else if(page==='overview')document.querySelector('#page-content').insertAdjacentHTML('afterbegin','<div data-launcher-summary></div>');else return;render();},
  disable,
  browsing:()=>pickerBusy,
  failure(result){if(result?.requiresElevation){elevation=true;if(result.launcher)state=result.launcher;render();}},
 };
})(window);
