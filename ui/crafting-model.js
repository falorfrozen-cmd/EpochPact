/* Player-facing crafting selections. Rendering never submits a game action. */
(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.EpochCrafting=api;})(typeof globalThis!=='undefined'?globalThis:this,()=>{
 'use strict';
 const sections=[{id:'forge',label:'Forge Settings',hint:'Costs, materials and glyph chances'},{id:'materials',label:'Materials',hint:'Stock your crafting stash'},{id:'custom',label:'Custom Items',hint:'Build an item step by step'},{id:'spawn',label:'Spawn Items',hint:'Drop any item at your feet'}];
 // settings: per chosen affix {tier:1-8, roll:1-100 or 'random'}. T8 is the
 // game's primordial tier: one per item, only for affixes that have it.
 const defaults=()=>({template_id:'-1',affixes:[],perfect:'1',modifiers:[],settings:{}});
 const PRIMORDIAL=8;
 function setting(draft,id){
  const s=draft.settings?.[id]||{};
  // No roll chosen yet: the item-wide maximum (100%) or midpoint (50%). A
  // typed value that is not 1-100 stays as it is, so the check names it.
  return {tier:s.tier===undefined?7:s.tier,roll:s.roll===undefined?(String(draft.perfect)==='0'?50:100):s.roll};
 }
 function tiersOf(data,id){const n=Number(data?.affixes?.find(x=>String(x.id)===String(id))?.tiers);return Number.isInteger(n)&&n>0?Math.min(n,PRIMORDIAL):7;}
 // An affix the game turns into another one on an item type it cannot roll on (lootread convertTo, rollsOn):
 // the affix it becomes on the forge item, else null.
 function convertsTo(data,id,forge){
  const a=data?.affixes?.find(x=>String(x.id)===String(id));
  if(a?.convertTo===undefined||!Number.isInteger(forge?.itemType)||(a.rollsOn||[]).includes(forge.itemType))return null;
  return data.affixes.find(x=>x.id===a.convertTo)||{id:a.convertTo,name:'affix '+a.convertTo};
 }
 function filtered(items,query='',kind='all'){
  const words=query.trim().toLowerCase().split(/\s+/).filter(Boolean);
  return items.filter(x=>(kind==='all'||(kind==='set'?x.set:!x.set))&&words.every(w=>String(x.uiLabel||x.name||'').toLowerCase().includes(w)));
 }
 function problem(draft,data,forge){
  if(!data?.items?.length)return 'Load item choices before creating custom gear.';
  if(!['0','1'].includes(String(draft.perfect)))return 'Choose maximum or midpoint rolls.';
  if(draft.template_id!=='-1'&&!data.items.some(x=>String(x.id)===String(draft.template_id)))return 'Choose a template from the current item list.';
  const affixes=draft.affixes||[],mods=draft.modifiers||[],limit=mods.length?6:7;
  if(new Set(affixes).size!==affixes.length||affixes.length>limit)return mods.length?`Choose at most ${limit} different affixes when adding extra effects.`:`Choose at most ${limit} different affixes.`;
  if(new Set(mods).size!==mods.length||mods.length>64)return 'Choose at most 64 different extra effects.';
  if(affixes.some(id=>!data.affixes?.some(x=>String(x.id)===String(id))))return 'An affix is no longer in the current game catalog. Review your selection.';
  if(mods.some(id=>!data.modifiers?.some(x=>String(x.id)===String(id))))return 'An extra effect is no longer in the current game catalog. Review your selection.';
  for(const id of affixes){
   const to=convertsTo(data,id,forge);
   const plain=s=>String(s).replace(/<[^>]*>/g,'');
   if(to)return `${plain(data.affixes.find(x=>String(x.id)===String(id)).name)} cannot be on this item type: the game turns it into ${plain(to.name)}. Choose another affix.`;
  }
  for(const id of affixes){
   const s=setting(draft,id);
   if(!Number.isInteger(s.tier)||s.tier<1||s.tier>PRIMORDIAL)return 'Choose a tier from T1 to T8.';
   if(s.tier>tiersOf(data,id))return 'An affix has no T8 in the game. Choose T1 to T7 for it.';
   if(s.roll!=='random'&&!(Number.isInteger(s.roll)&&s.roll>=1&&s.roll<=100))return 'Choose a roll from 1 to 100%, or Random.';
  }
  if(affixes.filter(id=>setting(draft,id).tier===PRIMORDIAL).length>1)return 'An item can have one T8 (primordial) affix.';
  const currentRegular=forge?.affixes?.filter(x=>data.affixes?.some(a=>String(a.id)===String(x.id))).length??0;
  if(mods.length&&!affixes.length&&currentRegular>6)return 'This item has seven affixes. Choose up to six affixes to leave room for extra effects.';
  return '';
 }
 // Each affix as ID:tier:roll (roll 1-100 or r for random).
 function affixToken(draft,id){const s=setting(draft,id);return `${id}:${s.tier}:${s.roll==='random'?'r':s.roll}`;}
 function args(draft){return {template_id:Number(draft.template_id),affixes:draft.affixes.length?draft.affixes.map(id=>affixToken(draft,id)).join(','):'keep',perfect:Number(draft.perfect),modifiers:draft.modifiers.length?draft.modifiers.join(','):'none'};}
 function summary(draft,data){
  const template=data?.items?.find(x=>String(x.id)===String(draft.template_id));
  const n=draft.affixes?.length||0,tiers=(draft.affixes||[]).map(id=>'T'+setting(draft,id).tier).join(', ');
  return {template:template?`${template.set?'Set':'Unique'} · ${template.name}`:'Exalted · keep this item’s base',affixes:n?(n===1?'1 selected affix':`${n} selected affixes`)+' · '+tiers:draft.modifiers?.length?'Current affixes rebuilt at T7':'Keep the current affixes',effects:draft.modifiers?.length?(draft.modifiers.length===1?'1 extra effect':`${draft.modifiers.length} extra effects`):'No extra effects',rolls:draft.perfect==='0'?(draft.affixes?.length||draft.modifiers?.length?'Midpoint rolls':'Midpoint for new effects · current affix rolls kept'):'Maximum rolls'};
 }
 return {sections,defaults,setting,tiersOf,convertsTo,filtered,problem,args,affixToken,summary,PRIMORDIAL};
});
