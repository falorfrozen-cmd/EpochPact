(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.EpochPreferences=api;})(typeof window==='object'?window:globalThis,function(){
 'use strict';
 const key='epochpact.display-preferences.v1';
 const defaults=()=>({favorites:[],hidden:[],summonColumns:[],hiddenStats:[],summonOptions:[]});
 function sanitize(input){
  const out=defaults();
  for(const name of Object.keys(out))out[name]=Array.isArray(input?.[name])?[...new Set(input[name].filter(x=>typeof x==='string'&&x.length<=120))].slice(0,1000):[];
  out.hidden=out.hidden.filter(x=>x!=='settings'&&x!=='overview');return out;
 }
 function load(storage){try{return sanitize(JSON.parse(storage.getItem(key)||'{}'));}catch{return defaults();}}
 function save(storage,value){try{storage.setItem(key,JSON.stringify(sanitize(value)));return true;}catch{return false;}}
 function toggle(value,name,id,enabled){const out=sanitize(value),set=new Set(out[name]||[]);if(enabled)set.add(id);else set.delete(id);out[name]=[...set];return sanitize(out);}
 function visiblePages(ids,value,current){const p=sanitize(value);return ids.filter(x=>x===current||!p.hidden.includes(x)).sort((a,b)=>Number(p.favorites.includes(b))-Number(p.favorites.includes(a)));}
 return {load,save,toggle,visiblePages,defaults,sanitize};
});
