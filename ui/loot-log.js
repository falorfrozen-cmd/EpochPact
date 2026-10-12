/* Loot log presentation shared by the main window and the loot feed window. */
(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.EpochLootLog=api;})(typeof window==='object'?window:globalThis,function(){
 'use strict';
 const capacity=300;
 const escape=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
 const rarityClass=r=>'loot-'+String(r||'normal').toLowerCase().replace(/[^a-z]/g,'');
 // Entries arrive with increasing sequence numbers; keep each once, newest last.
 function merge(existing,incoming,now=Date.now()){
  const bySeq=new Map((existing||[]).map(e=>[e.seq,e]));
  // The game reports each entry's age at read time; store an absolute time.
  for(const e of incoming||[])if(e&&Number.isFinite(e.seq)&&!bySeq.has(e.seq))bySeq.set(e.seq,{...e,at:now-(Number(e.ageSeconds)||0)*1000});
  return [...bySeq.values()].sort((a,b)=>a.seq-b.seq).slice(-capacity);
 }
 function newestSeq(entries){return (entries||[]).reduce((m,e)=>Math.max(m,e.seq||0),0);}
 const outcomes=['Show','Hide','Highlight'];
 // The full rule: number, what it does and its name (or the game's description).
 function ruleLabel(e){
  if(e.kind==='gold'||e.kind==='amber'||e.kind==='material')return '';
  if(!e.ruleNumber)return e.outcome===undefined||e.outcome>=0?'No matching rule':'';
  // Rule descriptions carry Unity rich-text tags (<color>, <u>); show plain text.
  const name=String(e.rule||'').replace(/<[^>]*>/g,'').replace(/\s+/g,' ').trim();
  const outcome=outcomes[e.outcome]??'';
  return `Rule ${e.ruleNumber}${outcome?' · '+outcome:''}${name?' · '+name:''}`;
 }
 function tierLabel(e){return e.tiers?String(e.tiers).split(' ').filter(Boolean).map(t=>'T'+t).join(' '):'';}
 function itemLabel(e){
  if(e.kind==='gold')return `+${Number(e.gold||0).toLocaleString('en-US')} Gold`;
  if(e.kind==='amber')return `${String(e.name||'Amber')}${e.gold?` ×${Number(e.gold).toLocaleString('en-US')}`:''}`;
  if(e.kind==='material'&&e.count>1)return `${String(e.name||'Material')} ×${e.count}`;
  return String(e.name||'Item');
 }
 function filter(entries,{gold=true,materials=true}={}){
  return (entries||[]).filter(e=>(gold||e.kind!=='gold')&&(materials||e.kind!=='material'));
 }
 // Identical resources picked up one after another become one line: gold and
 // amber are summed, the same material is counted. Equipment stays one line each.
 function groupKey(e){return e.kind==='gold'?'gold':e.kind==='amber'||e.kind==='material'?e.kind+':'+String(e.name||''):null;}
 function grouped(list){
  const out=[];
  for(const e of list){
   const key=groupKey(e),last=out[out.length-1];
   if(key&&last&&last.groupKey===key){last.gold=(Number(last.gold)||0)+(Number(e.gold)||0);last.count+=1;continue;}
   out.push({...e,groupKey:key,count:1});
  }
  return out;
 }
 function rows(entries,options={}){
  const limit=options.limit??100;
  const list=filter(entries,options).slice().reverse();
  return (options.group===false?list.map(e=>({...e,count:1})):grouped(list)).slice(0,limit);
 }
 function age(at,now=Date.now()){
  const s=Math.max(0,Math.round((now-(Number(at)||now))/1000));
  return s<60?`${s}s`:s<3600?`${Math.floor(s/60)}m`:`${Math.floor(s/3600)}h`;
 }
 function piles(e){return e.count>1&&(e.kind==='gold'||e.kind==='amber')?` <small class="muted">(${e.count} pickups)</small>`:'';}
 function table(entries,options={}){
  const list=rows(entries,options),now=options.now??Date.now();
  if(!list.length)return '<p class="empty">No pickups yet. Items and gold you or Auto Pickup collect appear here.</p>';
  return `<div class="table-scroll"><table class="loot-table"><thead><tr><th>When</th><th>Pickup</th><th>Tiers</th><th>Rarity</th><th>Loot filter</th><th>By</th></tr></thead><tbody>${list.map(e=>
   `<tr><td>${escape(age(e.at,now))}</td><td class="${rarityClass(e.rarity)}">${escape(itemLabel(e))}${piles(e)}</td><td>${escape(tierLabel(e))}</td><td>${escape(e.rarity)}</td><td>${escape(ruleLabel(e))}</td><td>${e.automatic?'Auto Pickup':'You'}</td></tr>`).join('')}</tbody></table></div>`;
 }
 function compact(entries,options={}){
  const list=rows(entries,{limit:options.limit??40,...options});
  if(!list.length)return '<p class="empty">Waiting for pickups…</p>';
  return `<ul class="loot-feed">${list.map(e=>{const rule=ruleLabel(e),tiers=tierLabel(e);
   return `<li class="${rarityClass(e.rarity)}" title="${escape([itemLabel(e),tiers,rule].filter(Boolean).join('\n'))}"><span>${escape(itemLabel(e))}${piles(e)}${tiers?` <em class="loot-tiers">${escape(tiers)}</em>`:''}</span>${rule?`<small>${escape(rule)}</small>`:''}</li>`;}).join('')}</ul>`;
 }
 function totalGold(entries){return (entries||[]).reduce((t,e)=>t+(e.kind==='gold'?Number(e.gold)||0:0),0);}
 return {merge,newestSeq,ruleLabel,tierLabel,itemLabel,rows,table,compact,totalGold,capacity};
});
