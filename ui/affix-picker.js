(function(root){
 'use strict';
 const LIMIT=64;
 const id=value=>/^(0|[1-9]\d*)$/.test(String(value))&&Number(value)<=65535?String(value):null;
 const normalize=values=>[...new Set((values||[]).map(id).filter(x=>x!==null))].slice(0,LIMIT);
 const equal=(a,b)=>a.length===b.length&&a.every(x=>b.includes(x));
 function create(){return {selected:[],applied:[],owner:null,query:'',order:'az',dirty:false};}
 function receive(state,values,owner,{force=false}={}){
  if(owner==null)return; // Temporary loss of identity is not a character change.
  if(state.owner!==owner){state.owner=owner;state.dirty=false;state.selected=[];}
  state.applied=normalize(values);
  if(force||!state.dirty)state.selected=[...state.applied];
  state.dirty=!equal(state.selected,state.applied);
 }
 function move(state,values,add){
  const incoming=normalize(values);
  if(add){const next=[...new Set([...state.selected,...incoming])];if(next.length>LIMIT)return false;state.selected=next;}
  else state.selected=state.selected.filter(x=>!incoming.includes(x));
  state.dirty=!equal(state.selected,state.applied);return true;
 }
 function entries(source){return (source||[]).filter(x=>id(x.id)!==null).map(x=>({id:String(x.id),name:String(x.uiLabel||x.name||'Affix '+x.id).replace(/<[^>]*>/g,'')}));}
 function lists(state,source){
  const all=entries(source),chosen=new Set(state.selected),q=state.query.trim().toLocaleLowerCase('en');
  const compare=(a,b)=>a.name.localeCompare(b.name,'en',{numeric:true,sensitivity:'base'})||Number(a.id)-Number(b.id);
  const available=all.filter(x=>!chosen.has(x.id)&&(!q||(x.name+' '+x.id).toLocaleLowerCase('en').includes(q)));
  const selected=state.selected.map(x=>all.find(a=>a.id===x)||{id:x,name:'Affix '+x+' (name unavailable)'});
  available.sort(compare);selected.sort(compare);
  if(state.order==='za'){available.reverse();selected.reverse();}
  return {available,selected,total:all.length};
 }
 const api={LIMIT,create,receive,move,lists,normalize};
 if(typeof module==='object'&&module.exports)module.exports=api;else root.EpochAffixes=api;
})(typeof window==='object'?window:globalThis);
