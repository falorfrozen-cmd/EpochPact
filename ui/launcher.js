/* Player setup is explicit. Opening this interface never installs or launches. */
(function(root){
 'use strict';
 let state=null,run,changed,isBusy,elevation=false,pickerBusy=false,pathDraft=null;
 const escape=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
 function section(){
  if(!state)return '';
  const status=state.foreignLoader?'Another mod loader is installed':state.disabled?'Mod disabled':state.installed?'Player mod installed':state.selected?'Ready to install':'Choose your game';
  return `<section class="panel game-setup"><div class="section-head"><h2>Game setup</h2><span class="badge">${status}</span></div><p class="muted">Select your Last Epoch executable once. Install the mod with the game closed, then launch offline.</p><label>Game executable<div class="game-path-row"><input data-game-path aria-label="Last Epoch executable path" value="${escape(state.executable)}" placeholder="Select Last Epoch.exe"><button data-game-choose>Browse…</button><button data-game-save>Save path</button></div></label><p data-game-problem class="muted">${escape(state.problem||'The file picker opens in your Steam folder. You can browse to any other installation.')}</p><div class="actions"><button data-game-install> ${state.installed?'Reinstall / update mod':'Install mod'}</button>${elevation?'<button data-game-elevate>Install as administrator</button>':''}<button class="primary" data-game-launch>${state.running?'Game running':'Launch offline'}</button></div><p class="muted">${state.running?'Close the game before installing or changing its location.':'Offline characters only. No gameplay settings are applied on startup.'}</p><p data-setup-feedback class="setup-feedback" role="status"></p></section>`;
 }
 function render(){
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
   b.disabled=(isBusy?.()||false)||((b.hasAttribute('data-game-install')||b.hasAttribute('data-game-elevate'))&&(!state?.selected||state.running||state.foreignLoader))||
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
  else if(name==='install'||name==='launch'||name==='elevated_install')await changed(false);
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
  else if(b.hasAttribute('data-setup-done'))document.querySelector('#setup-dialog').close();
 });
 document.addEventListener('input',event=>{if(event.target.matches?.('[data-game-path]'))pathDraft=event.target.value;});
 root.EpochLauncher={
  init(initial,options){state=initial;({run,changed,isBusy}=options);render();
   if(state&&!state.selected)document.querySelector('#setup-dialog').showModal();
  },
  update(next){if(next){state=next;render();}},
  mount(page){if(state&&['overview','settings','game_setup'].includes(page)){document.querySelector('#page-content').insertAdjacentHTML('afterbegin','<div data-launcher-panel></div>');render();}},
  disable,
  browsing:()=>pickerBusy,
  failure(result){if(result?.requiresElevation){elevation=true;if(result.launcher)state=result.launcher;render();}},
 };
})(window);
