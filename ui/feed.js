/* Always-on-top loot feed window: reads the loot log only; never sends a game action. */
(function(){
 'use strict';
 const $=s=>document.querySelector(s);
 const key='epochpact.loot-feed.v1';
 let options={};
 try{options={...options,...JSON.parse(localStorage.getItem(key)||'{}')};}catch{}
 // The old whole-window opacity becomes the background opacity (overlay.js).
 Object.assign(options,window.EpochOverlay.normalise(options));delete options.opacity;
 // The old Gold and Materials switches become categories.
 options.show=window.EpochLootLog.shown(options);delete options.gold;delete options.materials;
 const save=()=>{try{localStorage.setItem(key,JSON.stringify(options));}catch{}};
 let entries=[],busy=false;
 // Affix shard descriptions, read once per affix ID and kept for this window
 // (a failed read is tried again at most three times).
 const affixes=new Map(),tries=new Map();
 const transport=window.EpochSession.create({fetch:(...a)=>fetch(...a),sleep:ms=>new Promise(r=>setTimeout(r,ms)),isReadOnly:()=>true});
 function render(){
  $('#feed-list').innerHTML=window.EpochLootLog.compact(entries,{show:options.show,affixes});
  const gold=window.EpochLootLog.totalGold(entries);$('#feed-gold-total').textContent=gold?`Gold: ${gold.toLocaleString('en-US')}`:'';
 }
 async function describeShards(){
  const missing=window.EpochLootLog.shardIds(entries).filter(id=>!affixes.has(id)&&(tries.get(id)||0)<3).slice(0,4);
  if(!missing.length)return;
  for(const id of missing){
   tries.set(id,(tries.get(id)||0)+1);
   try{const r=await transport.run({type:'control',id:'affix_info',operation:'read',args:{id}});if(r?.ok)affixes.set(id,r);}catch{}
  }
  render();
 }
 async function poll(){
  if(busy||document.hidden)return;busy=true;
  try{
   const r=await transport.run({type:'control',id:'loot_log',operation:'read'});
   if(r?.ok){entries=window.EpochLootLog.merge(entries,r.entries);$('#feed-status').textContent=r.ready===false?'Loot log unavailable in this game build':'';render();await describeShards();}
   else $('#feed-status').textContent=r?.error||'Waiting for an offline character…';
  }catch{$('#feed-status').textContent='EpochPact is not responding.';}
  finally{busy=false;}
 }
 function showFilters(){for(const box of document.querySelectorAll('[data-feed-show]'))box.checked=options.show[box.dataset.feedShow]!==false;}
 function setAll(value){for(const [id] of window.EpochLootLog.categories)options.show[id]=value;save();showFilters();render();}
 async function start(){
  window.EpochOverlay.install(window,{options,save,minimum:[220,120]});
  try{const boot=await (await fetch('/api/bootstrap')).json();transport.setSession(boot);}catch{$('#feed-status').textContent='Open EpochPact first.';return;}
  showFilters();
  for(const box of document.querySelectorAll('[data-feed-show]'))box.addEventListener('change',e=>{options.show[e.target.dataset.feedShow]=e.target.checked;save();render();});
  $('[data-feed-all]').addEventListener('click',()=>setAll(true));
  $('[data-feed-none]').addEventListener('click',()=>setAll(false));
  render();await poll();setInterval(()=>void poll(),2000);
 }
 void start();
})();
