/* Interface language. English is the source: ui/i18n-<language>.json maps each
   English text (from tools/extract_ui_text.py) to its translation, with {0},
   {1}, ... for the parts that change ("{0} settings applied in order.").
   Loaded after storage-sync.js and before the page scripts; it translates the
   page as it is built and every text the scripts add later (notifications,
   dialogs, tables). Text that is not in the dictionary — character, item,
   stat and affix names from the game, numbers — stays as it is. Elements with
   data-no-translate (and their children) are left alone. */
(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.EpochI18n=api;})(typeof window==='object'?window:globalThis,function(){
 'use strict';
 const languages={en:'English',tr:'Türkçe',ru:'Русский'};
 const storageKey='epochpact.language';
 const attributes=['title','placeholder','aria-label','alt'];
 const escape=s=>s.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');
 function compile(dictionary){
  const exact=new Map(),templates=[];
  for(const [source,target] of Object.entries(dictionary||{})){
   if(typeof source!=='string'||typeof target!=='string'||!target)continue;
   if(/\{\d+\}/.test(source)){
    const order=[];let pattern='^';
    for(const part of source.split(/(\{\d+\})/)){const m=/^\{(\d+)\}$/.exec(part);if(m){pattern+='(.*?)';order.push(Number(m[1]));}else pattern+=escape(part);}
    // A template with little text of its own ("Level {0}", "Tab {0}") only
    // takes numbers, so game names such as "Level of Fireball" stay as they are.
    const letters=source.replace(/\{\d+\}/g,'').replace(/[^A-Za-z]/g,'').length;
    templates.push({re:new RegExp(pattern+'$','s'),order,target,first:source.split(/\{\d+\}/)[0],numeric:letters<6});
   }else exact.set(source,target);
  }
  // Longer literal prefixes first, so a specific template wins over a general one.
  templates.sort((a,b)=>b.first.length-a.first.length);
  return {exact,templates};
 }
 // decimalComma: a value that is a decimal number ("0.25") is written with a
 // decimal comma ("0,25"), as Turkish and Russian write numbers.
 function translator(dictionary,{decimalComma=false}={}){
  const {exact,templates}=compile(dictionary);
  const edges=text=>[text.slice(0,text.length-text.trimStart().length),text.slice(text.trimEnd().length)];
  const decimal=value=>decimalComma&&/^\s*[+\-−]?\d+\.\d+\s*$/.test(value)?value.replace('.',','):value;
  let nesting=0;
  function lookup(core){
   const hit=exact.get(core);
   if(hit!==undefined)return hit;
   for(const t of templates){
    if(t.first&&!core.startsWith(t.first))continue;
    const m=t.re.exec(core);if(!m||t.numeric&&m.slice(1).some(v=>!/\d/.test(v)))continue;
    // A value that is itself interface text ("Default: on", "Applying your
    // last used settings", "12 stash tabs · ") is translated as well.
    const values={};t.order.forEach((index,i)=>{
     const value=m[i+1];let inner=null;
     if(nesting<2&&/[A-Za-z]/.test(value)){nesting++;try{inner=translate(value);}finally{nesting--;}}
     values[index]=inner===null?decimal(value):inner;
    });
    return t.target.replace(/\{(\d+)\}/g,(_,n)=>values[n]??'');
   }
   return null;
  }
  // A count or value written in front of or after a dictionary text
  // ("12 shard types", "Rule 45") keeps its place around the translation.
  const before=/^([+\-−]?\d[\d.,]*\s*[%×]?\s*)([^\d].*)$/s,after=/^(.*[^\d\s])(\s*[:·]?\s*[+\-−]?\d[\d.,]*\s*%?)$/s;
  function phrase(core){
   let out=lookup(core);
   if(out===null){const m=before.exec(core);if(m){const rest=lookup(m[2]);if(rest!==null)out=m[1]+rest;}}
   if(out===null){const m=after.exec(core);if(m){const rest=lookup(m[1]);if(rest!==null)out=rest+m[2];}}
   // An icon in front ("♜ Character", "↻ Scan collection") keeps its place.
   if(out===null){const m=/^([^\p{L}\p{N}"“(+\-−]+)(\p{L}.*)$/su.exec(core);if(m){const rest=lookup(m[2]);if(rest!==null)out=m[1]+rest;}}
   // "Sort only: Rule A, Rule B": a known label, then game or player data.
   if(out===null){const m=/^([^:]{2,60}:)(\s+.*)$/s.exec(core);if(m){const label=exact.get(m[1]);if(label!==undefined)out=label+m[2];}}
   return out;
  }
  // Text joined from several interface texts (the lines of a confirmation,
  // parts separated by " · ") is translated part by part when the whole is
  // not in the dictionary; parts without a translation stay as they are.
  function parts(text,separator){
   const list=text.split(separator);
   let changed=false;
   const out=list.map(part=>{const t=translate(part);if(t===null)return part;changed=true;return t;});
   return changed?out.join(separator):null;
  }
  function translate(text){
   if(typeof text!=='string')return null;
   const core=text.trim().replace(/\s+/g,' ');
   if(!core||!/[A-Za-z]/.test(core))return null;
   const [lead,trail]=edges(text);
   // Several lines: a known whole text (HTML wrapped in the source), else
   // line by line, before a template could swallow the other lines.
   if(text.trim().includes('\n')){
    const whole=exact.get(core);if(whole!==undefined)return lead+whole+trail;
    const lines=parts(text,'\n');if(lines!==null)return lines;
   }
   const out=phrase(core);
   if(out!==null)return lead+out+trail;
   if(text.includes(' · '))return parts(text,' · ');
   const joined=sentences(core);
   return joined===null?null:lead+joined+trail;
  }
  // Sentences added to a known text ("<description> The choice is undone
  // when …") are matched as the longest known runs of whole sentences.
  function sentences(core){
   const list=core.split(/(?<=[.!?…])\s+(?=[A-Z“"(])/);
   if(list.length<2)return null;
   const out=[];let changed=false;
   for(let i=0;i<list.length;){
    let j=list.length,hit=null;
    for(;j>i;j--){hit=phrase(list.slice(i,j).join(' '));if(hit!==null)break;}
    if(hit!==null){out.push(hit);changed=true;i=j;}else{out.push(list[i]);i++;}
   }
   return changed?out.join(' '):null;
  }
  return translate;
 }
 function skipped(node){
  for(let el=node.nodeType===1?node:node.parentElement;el;el=el.parentElement){
   if(el.hasAttribute?.('data-no-translate')||['SCRIPT','STYLE','TEXTAREA','CODE'].includes(el.tagName)||el.isContentEditable)return true;
  }
  return false;
 }
 function translateNode(node,translate){
  if(node.nodeType===3){
   if(skipped(node))return;
   const out=translate(node.nodeValue);if(out!==null&&out!==node.nodeValue)node.nodeValue=out;return;
  }
  if(node.nodeType!==1||skipped(node))return;
  for(const name of attributes)if(node.hasAttribute(name)){const value=node.getAttribute(name),out=translate(value);if(out!==null&&out!==value)node.setAttribute(name,out);}
  if(node.tagName==='INPUT'&&['button','submit'].includes(node.type)&&node.value){const out=translate(node.value);if(out!==null&&out!==node.value)node.value=out;}
  for(const child of [...node.childNodes])translateNode(child,translate);
 }
 function readLanguage(storage){
  try{const v=JSON.parse(storage.getItem(storageKey)||'"en"');return Object.hasOwn(languages,v)?v:'en';}catch{return 'en';}
 }
 function install(win){
  const doc=win.document,language=readLanguage(win.localStorage);
  const api={language,languages,t:text=>text};
  doc.documentElement.lang=language;
  // Another window (loot feed, damage) changed the language: follow it.
  win.addEventListener('storage',e=>{if(e.key===storageKey&&readLanguage(win.localStorage)!==language)win.location.reload();});
  if(language==='en')return api;
  let dictionary=null;
  try{const request=new win.XMLHttpRequest();request.open('GET',`/ui/i18n-${language}.json`,false);request.send();if(request.status===200)dictionary=JSON.parse(request.responseText);}catch{}
  if(!dictionary)return api;
  const translate=translator(dictionary,{decimalComma:language==='tr'||language==='ru'});
  api.t=text=>translate(text)??text;
  const observer=new win.MutationObserver(records=>{
   for(const r of records){
    if(r.type==='childList')r.addedNodes.forEach(n=>translateNode(n,translate));
    else if(r.type==='characterData')translateNode(r.target,translate);
    // Only a changed value is written back: writing the same value is a new
    // mutation, and a translation equal to its source ("Void") would loop.
    else if(r.type==='attributes'&&!skipped(r.target)){const value=r.target.getAttribute(r.attributeName),out=translate(value);if(out!==null&&out!==value)r.target.setAttribute(r.attributeName,out);}
   }
  });
  observer.observe(doc.documentElement,{childList:true,subtree:true,characterData:true,attributes:true,attributeFilter:attributes});
  const whole=()=>{translateNode(doc.documentElement,translate);const title=translate(doc.title);if(title!==null)doc.title=title;};
  if(doc.readyState==='loading')doc.addEventListener('DOMContentLoaded',whole);else whole();
  return api;
 }
 const api={languages,storageKey,compile,translator,readLanguage,install};
 if(typeof window==='object'&&window.document&&window.localStorage)Object.assign(api,install(window));
 return api;
});
