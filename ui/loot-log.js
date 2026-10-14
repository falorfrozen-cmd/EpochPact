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
  const t=globalThis.EpochI18n?.t??(x=>x);
  return `${t('Rule')} ${e.ruleNumber}${outcome?' · '+t(outcome):''}${name?' · '+name:''}`;
 }
 function tierLabel(e){return e.tiers?String(e.tiers).split(' ').filter(Boolean).map(t=>'T'+t).join(' '):'';}
 function itemLabel(e){
  if(e.kind==='gold')return `+${Number(e.gold||0).toLocaleString('en-US')} Gold`;
  if(e.kind==='amber')return `${String(e.name||'Amber')}${e.gold?` ×${Number(e.gold).toLocaleString('en-US')}`:''}`;
  if(e.kind==='material'&&e.count>1)return `${String(e.name||'Material')} ×${e.count}`;
  return String(e.name||'Item');
 }
 // What the feed can show or hide: equipment by rarity, the resources and the
 // crafting materials by their game item type (101 shard, 102 rune, 103 glyph).
 const categories=[
  ['normal','Normal','items'],['magic','Magic','items'],['rare','Rare','items'],['exalted','Exalted','items'],
  ['unique','Unique','items'],['set','Set','items'],['legendary','Legendary','items'],
  ['gold','Gold','resources'],['amber','Memory Amber','resources'],['ember','Soul Ember','resources'],['xp','XP motes','resources'],
  ['shard','Affix shards','materials'],['rune','Runes','materials'],['glyph','Glyphs','materials'],['material','Other materials','materials']];
 const rarities=new Set(categories.filter(c=>c[2]==='items').map(c=>c[0]));
 function category(e){
  if(e?.kind==='gold')return 'gold';
  if(e?.kind==='amber')return e.name==='Soul Ember'?'ember':e.name==='XP mote'?'xp':'amber';
  if(e?.kind==='material')return {101:'shard',102:'rune',103:'glyph'}[e.type]||'material';
  const r=String(e?.rarity||'').toLowerCase();
  return rarities.has(r)?r:'normal';
 }
 // Every category is shown unless switched off; the old Gold and Materials
 // switches carry over to the new categories.
 function shown(options={}){
  const show=Object.fromEntries(categories.map(([id])=>[id,true]));
  if(options.gold===false)show.gold=false;
  if(options.materials===false)for(const id of ['shard','rune','glyph','material'])show[id]=false;
  for(const [id] of categories)if(typeof options.show?.[id]==='boolean')show[id]=options.show[id];
  return show;
 }
 function filter(entries,options={}){
  const show=shown(options);
  return (entries||[]).filter(e=>show[category(e)]!==false);
 }
 // Affix shards carry their affix ID as the item subtype; affixinfo describes it.
 function shardIds(entries){return [...new Set((entries||[]).filter(e=>e?.kind==='material'&&e.type===101&&Number.isInteger(e.subtype)&&e.subtype>=0).map(e=>e.subtype))];}
 function affixText(info){
  if(!info?.ok)return '';
  const t=globalThis.EpochI18n?.t??(x=>x);
  const range=(a,b)=>a&&b&&a!==b?`${a} – ${b}`:a||b||'';
  const lines=[[info.name,info.type?t(info.type):'',Number(info.level)>0?`${t('Level')} ${Number(info.level)}`:''].filter(Boolean).join(' · ')];
  if(info.rollsOn?.length)lines.push(`${t('Rolls on')}: ${info.rollsOn.join(', ')}`);
  for(const tier of info.tiers||[]){
   const first=range(tier.min,tier.max),second=range(tier.min2,tier.max2);
   if(first||second)lines.push(`T${tier.tier}  ${[first,second].filter(Boolean).join(' / ')}`);
  }
  // Some item types scale an affix (two-handed weapons); the values are the first type's.
  if(info.rollsOn?.length>1&&info.valuesOn&&info.tiers?.length)lines.push(`(${t(`Values for ${info.valuesOn}`)})`);
  return lines.join('\n');
 }
 function shardText(e,affixes){
  if(!affixes||e?.kind!=='material'||e.type!==101)return '';
  return affixText(affixes instanceof Map?affixes.get(e.subtype):affixes[e.subtype]);
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
 function piles(e){const t=globalThis.EpochI18n?.t??(x=>x);return e.count>1&&(e.kind==='gold'||e.kind==='amber')?` <small class="muted">${escape(t(`(${e.count} pickups)`))}</small>`:'';}
 function hover(e,options){const text=shardText(e,options.affixes);return text?` title="${escape(text)}"`:'';}
 function table(entries,options={}){
  const list=rows(entries,options),now=options.now??Date.now();
  if(!list.length)return '<p class="empty">No pickups yet. Items and gold you or Auto Pickup collect appear here.</p>';
  return `<div class="table-scroll"><table class="loot-table"><thead><tr><th>When</th><th>Pickup</th><th>Tiers</th><th>Rarity</th><th>Loot filter</th><th>By</th></tr></thead><tbody>${list.map(e=>
   `<tr><td>${escape(age(e.at,now))}</td><td class="${rarityClass(e.rarity)}"${hover(e,options)}>${escape(itemLabel(e))}${piles(e)}</td><td>${escape(tierLabel(e))}</td><td>${escape(e.rarity)}</td><td data-no-translate>${escape(ruleLabel(e))}</td><td>${e.automatic?'Auto Pickup':'You'}</td></tr>`).join('')}</tbody></table></div>`;
 }
 function compact(entries,options={}){
  const list=rows(entries,{limit:options.limit??40,...options});
  if(!list.length)return (entries||[]).length?'<p class="empty">No pickups in the categories you show.</p>':'<p class="empty">Waiting for pickups…</p>';
  const t=globalThis.EpochI18n?.t??(x=>x);
  return `<ul class="loot-feed">${list.map(e=>{const rule=ruleLabel(e),tiers=tierLabel(e),item=t(itemLabel(e)),shard=shardText(e,options.affixes);
   return `<li class="${rarityClass(e.rarity)}" data-no-translate title="${escape([item,tiers,rule,shard].filter(Boolean).join('\n'))}"><span>${escape(item)}${piles(e)}${tiers?` <em class="loot-tiers">${escape(tiers)}</em>`:''}</span>${rule?`<small>${escape(rule)}</small>`:''}</li>`;}).join('')}</ul>`;
 }
 function totalGold(entries){return (entries||[]).reduce((t,e)=>t+(e.kind==='gold'?Number(e.gold)||0:0),0);}
 return {merge,newestSeq,ruleLabel,tierLabel,itemLabel,rows,table,compact,totalGold,capacity,categories,category,shown,filter,shardIds,affixText};
});
