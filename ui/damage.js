/* Always-on-top damage window: reads the damage session once a second and can
   start, stop or reset it. A session left running when the window closed
   starts again once the game is ready the next time the window opens.
   Nothing else is sent to the game. */
(function(){
 'use strict';
 const $=s=>document.querySelector(s);
 const key='epochpact.damage-window.v1';
 let options={compact:true,opacity:100,running:false};
 try{options={...options,...JSON.parse(localStorage.getItem(key)||'{}')};}catch{}
 const save=()=>{try{localStorage.setItem(key,JSON.stringify(options));}catch{}};
 let state=null,busy=false;
 const transport=window.EpochSession.create({fetch:(...a)=>fetch(...a),sleep:ms=>new Promise(r=>setTimeout(r,ms)),isReadOnly:p=>p.operation==='read'});
 function applyOpacity(){
  const value=Math.min(100,Math.max(30,Number(options.opacity)||100));
  $('#damage-opacity').value=value;
  if(window.pywebview?.api?.set_feed_opacity)window.pywebview.api.set_feed_opacity(value).catch(()=>{});
  else document.body.style.opacity=String(value/100);
 }
 function render(){
  $('#damage-table').innerHTML=window.EpochDamage.table(state,{compact:options.compact});
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
  finally{busy=false;}
 }
 async function start(){
  try{const boot=await (await fetch('/api/bootstrap')).json();transport.setSession(boot);}catch{$('#damage-status').textContent='Open EpochPact first.';return;}
  $('#damage-compact').checked=options.compact;
  $('#damage-compact').addEventListener('change',e=>{options.compact=e.target.checked;save();render();});
  $('#damage-opacity').addEventListener('input',e=>{options.opacity=Number(e.target.value);save();applyOpacity();});
  $('#damage-toggle').addEventListener('click',()=>{const action=state?.active?'stop':'start';options.running=action==='start';save();void call(action);});
  $('#damage-reset').addEventListener('click',()=>void call('reset'));
  window.addEventListener('pywebviewready',applyOpacity);
  applyOpacity();await call();
  setInterval(()=>{if(!document.hidden&&state?.active)void call();},1000);
  // Resume once per window opening; a later Stop elsewhere is respected.
  let resumed=false;
  const resume=async()=>{if(resumed||!options.running||busy)return;if(!state)await call();if(!state)return;resumed=true;if(!state.active)await call('start');};
  void resume();setInterval(()=>void resume(),3000);
 }
 void start();
})();
