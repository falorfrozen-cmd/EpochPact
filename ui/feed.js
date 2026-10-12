/* Always-on-top loot feed window: reads the loot log only; never sends a game action. */
(function(){
 'use strict';
 const $=s=>document.querySelector(s);
 const key='epochpact.loot-feed.v1';
 let options={gold:true,materials:true,opacity:100};
 try{options={...options,...JSON.parse(localStorage.getItem(key)||'{}')};}catch{}
 const save=()=>{try{localStorage.setItem(key,JSON.stringify(options));}catch{}};
 let entries=[],busy=false;
 const transport=window.EpochSession.create({fetch:(...a)=>fetch(...a),sleep:ms=>new Promise(r=>setTimeout(r,ms)),isReadOnly:()=>true});
 function applyOpacity(){
  const value=Math.min(100,Math.max(30,Number(options.opacity)||100));
  $('#feed-opacity').value=value;
  // The desktop window applies real transparency; a browser tab dims instead.
  if(window.pywebview?.api?.set_feed_opacity)window.pywebview.api.set_feed_opacity(value).catch(()=>{});
  else document.body.style.opacity=String(value/100);
 }
 function render(){$('#feed-list').innerHTML=window.EpochLootLog.compact(entries,{gold:options.gold,materials:options.materials});}
 async function poll(){
  if(busy||document.hidden)return;busy=true;
  try{
   const r=await transport.run({type:'control',id:'loot_log',operation:'read'});
   if(r?.ok){entries=window.EpochLootLog.merge(entries,r.entries);$('#feed-status').textContent=r.ready===false?'Loot log unavailable in this game build':'';render();}
   else $('#feed-status').textContent=r?.error||'Waiting for an offline character…';
  }catch{$('#feed-status').textContent='EpochPact is not responding.';}
  finally{busy=false;}
 }
 async function start(){
  try{const boot=await (await fetch('/api/bootstrap')).json();transport.setSession(boot);}catch{$('#feed-status').textContent='Open EpochPact first.';return;}
  $('#feed-gold').checked=options.gold;$('#feed-materials').checked=options.materials;
  $('#feed-gold').addEventListener('change',e=>{options.gold=e.target.checked;save();render();});
  $('#feed-materials').addEventListener('change',e=>{options.materials=e.target.checked;save();render();});
  $('#feed-opacity').addEventListener('input',e=>{options.opacity=Number(e.target.value);save();applyOpacity();});
  window.addEventListener('pywebviewready',applyOpacity);
  applyOpacity();render();await poll();setInterval(()=>void poll(),2000);
 }
 void start();
})();
