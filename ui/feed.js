/* Always-on-top loot feed window: reads the loot log only; never sends a game action. */
(function(){
 'use strict';
 const $=s=>document.querySelector(s);
 const key='epochpact.loot-feed.v1';
 let options={gold:true,materials:true};
 try{options={...options,...JSON.parse(localStorage.getItem(key)||'{}')};}catch{}
 // The old whole-window opacity becomes the background opacity (overlay.js).
 Object.assign(options,window.EpochOverlay.normalise(options));delete options.opacity;
 const save=()=>{try{localStorage.setItem(key,JSON.stringify(options));}catch{}};
 let entries=[],busy=false;
 const transport=window.EpochSession.create({fetch:(...a)=>fetch(...a),sleep:ms=>new Promise(r=>setTimeout(r,ms)),isReadOnly:()=>true});
 function render(){
  $('#feed-list').innerHTML=window.EpochLootLog.compact(entries,{gold:options.gold,materials:options.materials});
  const gold=window.EpochLootLog.totalGold(entries);$('#feed-gold-total').textContent=gold?`Gold: ${gold.toLocaleString('en-US')}`:'';
 }
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
  window.EpochOverlay.install(window,{options,save,minimum:[220,120]});
  try{const boot=await (await fetch('/api/bootstrap')).json();transport.setSession(boot);}catch{$('#feed-status').textContent='Open EpochPact first.';return;}
  $('#feed-gold').checked=options.gold;$('#feed-materials').checked=options.materials;
  $('#feed-gold').addEventListener('change',e=>{options.gold=e.target.checked;save();render();});
  $('#feed-materials').addEventListener('change',e=>{options.materials=e.target.checked;save();render();});
  render();await poll();setInterval(()=>void poll(),2000);
 }
 void start();
})();
