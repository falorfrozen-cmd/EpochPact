/* Player-facing crafting selections. Rendering never submits a game action. */
(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.EpochCrafting=api;})(typeof globalThis!=='undefined'?globalThis:this,()=>{
 'use strict';
 const sections=[{id:'forge',label:'Forge Settings',hint:'Costs, materials and glyph chances'},{id:'materials',label:'Materials',hint:'Stock your crafting stash'},{id:'custom',label:'Custom Items',hint:'Build an item step by step'}];
 const defaults=()=>({template_id:'-1',affixes:[],perfect:'1',modifiers:[]});
 function filtered(items,query='',kind='all'){
  const words=query.trim().toLowerCase().split(/\s+/).filter(Boolean);
  return items.filter(x=>(kind==='all'||(kind==='set'?x.set:!x.set))&&words.every(w=>String(x.uiLabel||x.name||'').toLowerCase().includes(w)));
 }
 function problem(draft,data,forge){
  if(!data?.items?.length)return 'Load item choices before creating custom gear.';
  if(!['0','1'].includes(String(draft.perfect)))return 'Choose maximum or midpoint rolls.';
  if(draft.template_id!=='-1'&&!data.items.some(x=>String(x.id)===String(draft.template_id)))return 'Choose a template from the current item list.';
  const affixes=draft.affixes||[],mods=draft.modifiers||[],limit=mods.length?6:7;
  if(new Set(affixes).size!==affixes.length||affixes.length>limit)return mods.length?`Choose at most ${limit} different T7 affixes when adding extra effects.`:`Choose at most ${limit} different T7 affixes.`;
  if(new Set(mods).size!==mods.length||mods.length>64)return 'Choose at most 64 different extra effects.';
  if(affixes.some(id=>!data.affixes?.some(x=>String(x.id)===String(id))))return 'An affix is no longer in the current game catalog. Review your selection.';
  if(mods.some(id=>!data.modifiers?.some(x=>String(x.id)===String(id))))return 'An extra effect is no longer in the current game catalog. Review your selection.';
  const currentRegular=forge?.affixes?.filter(x=>data.affixes?.some(a=>String(a.id)===String(x.id))).length??0;
  if(mods.length&&!affixes.length&&currentRegular>6)return 'This item has seven affixes. Choose up to six T7 affixes to leave room for extra effects.';
  return '';
 }
 function args(draft){return {template_id:Number(draft.template_id),affixes:draft.affixes.length?draft.affixes.join(','):'keep',perfect:Number(draft.perfect),modifiers:draft.modifiers.length?draft.modifiers.join(','):'none'};}
 function summary(draft,data){
  const template=data?.items?.find(x=>String(x.id)===String(draft.template_id));
  return {template:template?`${template.set?'Set':'Unique'} · ${template.name}`:'Exalted · keep this item’s base',affixes:draft.affixes?.length?`${draft.affixes.length} selected T7 affixes`:draft.modifiers?.length?'Current affixes rebuilt at T7':'Keep the current affixes',effects:draft.modifiers?.length?(draft.modifiers.length===1?'1 extra effect':`${draft.modifiers.length} extra effects`):'No extra effects',rolls:draft.perfect==='0'?(draft.affixes?.length||draft.modifiers?.length?'Midpoint rolls':'Midpoint for new effects · current affix rolls kept'):'Maximum rolls'};
 }
 return {sections,defaults,filtered,problem,args,summary};
});
