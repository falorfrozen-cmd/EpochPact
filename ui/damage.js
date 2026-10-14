/* Always-on-top damage window: reads the damage session once a second while it
   runs (every few seconds otherwise, so a session started or stopped in
   EpochPact's main window shows up here too) and can start, stop or reset it. A session left running when the window closed
   starts again once the game is ready the next time the window opens.
   Nothing else is sent to the game. */
(function(){
 'use strict';
 const $=s=>document.querySelector(s);
 const key='epochpact.damage-window.v1';
 let options={compact:true,running:false,element:-1,hidden:[]};
 try{options={...options,...JSON.parse(localStorage.getItem(key)||'{}')};}catch{}
 // The old whole-window opacity becomes the background opacity (overlay.js).
 Object.assign(options,window.EpochOverlay.normalise(options));delete options.opacity;
 if(!Number.isInteger(options.element)||options.element<-1||options.element>6)options.element=-1;
 if(!Array.isArray(options.hidden))options.hidden=[];
 options.hidden=options.hidden.filter(k=>typeof k==='string').slice(0,100);
 const save=()=>{try{localStorage.setItem(key,JSON.stringify(options));}catch{}};
 let state=null,busy=false,lastRead=0;
 const transport=window.EpochSession.create({fetch:(...a)=>fetch(...a),sleep:ms=>new Promise(r=>setTimeout(r,ms)),isReadOnly:p=>p.operation==='read'});
 // One switch per row the session has (ally types, the allies' total, you);
 // rebuilt only when that list changes, so an open menu keeps its focus.
 let rowKeys='';
 function renderRows(){
  const list=window.EpochDamage.sources(state),keys=JSON.stringify(list.map(r=>r.key));
  if(!list.length||keys===rowKeys)return;rowKeys=keys;
  const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  $('#damage-rows').innerHTML=list.map(r=>`<label><input type="checkbox" data-damage-row="${esc(r.key)}" ${options.hidden.includes(r.key)?'':'checked'}> ${esc(r.label)}</label>`).join('');
 }
 function render(){
  $('#damage-table').innerHTML=window.EpochDamage.table(state,{compact:options.compact,element:options.element,hidden:options.hidden});
  renderRows();
  $('#damage-status').textContent=window.EpochDamage.status(state);
  $('#damage-toggle').textContent=state?.active?'Stop':'Start';
 }
 async function call(action){
  if(busy)return;busy=true;
  try{
   const r=await transport.run({type:'control',id:action?'damage_session':'damage_session_read',operation:action?'set':'read',args:action?{action}:{}});
   if(r?.ok){state=r;render();}
   else $('#damage-status').textContent=r?.error||'Waiting for an offline character…';
  }catch{$('#damage-status').textContent='EpochPact is not responding.';}
  finally{busy=false;lastRead=Date.now();}
 }
 async function start(){
  window.EpochOverlay.install(window,{options,save,minimum:[260,120]});
  try{const boot=await (await fetch('/api/bootstrap')).json();transport.setSession(boot);}catch{$('#damage-status').textContent='Open EpochPact first.';return;}
  $('#damage-compact').checked=options.compact;
  $('#damage-compact').addEventListener('change',e=>{options.compact=e.target.checked;save();render();});
  $('#damage-element').value=String(options.element);
  $('#damage-element').addEventListener('change',e=>{options.element=Number(e.target.value);save();render();});
  $('#damage-rows').addEventListener('change',e=>{
   const row=e.target.dataset?.damageRow;if(row===undefined)return;
   options.hidden=options.hidden.filter(k=>k!==row);if(!e.target.checked)options.hidden.push(row);save();render();
  });
  $('#damage-toggle').addEventListener('click',()=>{const action=state?.active?'stop':'start';options.running=action==='start';save();void call(action);});
  $('#damage-reset').addEventListener('click',()=>void call('reset'));
  await call();
  setInterval(()=>{if(!document.hidden&&(state?.active||Date.now()-lastRead>=3000))void call();},1000);
  // Resume once per window opening; a later Stop elsewhere is respected.
  let resumed=false;
  const resume=async()=>{if(resumed||!options.running||busy)return;if(!state)await call();if(!state)return;resumed=true;if(!state.active)await call('start');};
  void resume();setInterval(()=>void resume(),3000);
 }
 void start();
})();
