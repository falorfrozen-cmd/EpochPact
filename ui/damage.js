/* Always-on-top damage window: reads the damage session once a second while it
   runs (every few seconds otherwise, so a session started or stopped in
   EpochPact's main window shows up here too) and can start, stop or reset it. A session left running when the window closed
   starts again once the game is ready the next time the window opens.
   Styles: Quiet HUD (default), Pact Bars, Split Meter (damage-styles.js) and
   the full table. Each source's icon is the game's own skill or class icon,
   read once per source. Nothing else is sent to the game. */
(function(){
 'use strict';
 const $=s=>document.querySelector(s);
 const key='epochpact.damage-window.v1';
 const Styles=window.EpochDamageStyles;
 let options={compact:true,running:false,element:-1,hidden:[],style:'quiet',metric:'dps10',roster:false};
 try{options={...options,...JSON.parse(localStorage.getItem(key)||'{}')};}catch{}
 // The old whole-window opacity becomes the background opacity (overlay.js).
 Object.assign(options,window.EpochOverlay.normalise(options));delete options.opacity;
 if(!Number.isInteger(options.element)||options.element<-1||options.element>6)options.element=-1;
 if(!Array.isArray(options.hidden))options.hidden=[];
 options.hidden=options.hidden.filter(k=>typeof k==='string').slice(0,100);
 options.style=Styles.styleOf(options.style);options.metric=Styles.metricOf(options.metric);options.roster=options.roster===true;
 const save=()=>{try{localStorage.setItem(key,JSON.stringify(options));}catch{}};
 let state=null,busy=false,lastRead=0,failed=false;
 // The source whose damage types are shown (null: You); not saved.
 let selected=null,model=null,drawn='',footDrawn='',shownActive=null;
 const icons=new Map(),iconTries=new Map();
 const transport=window.EpochSession.create({fetch:(...a)=>fetch(...a),sleep:ms=>new Promise(r=>setTimeout(r,ms)),isReadOnly:p=>p.operation==='read'});
 const t=text=>window.EpochI18n?.t?.(text)??text;
 const table=()=>options.style==='table';
 // One switch per row the session has (ally types, the allies' total in the
 // table, you); rebuilt only when that list changes, so an open menu keeps its focus.
 let rowKeys='';
 function renderRows(){
  const list=window.EpochDamage.sources(state).filter(r=>table()||r.key!=='__total'),keys=JSON.stringify(list.map(r=>r.key));
  if(!list.length||keys===rowKeys)return;rowKeys=keys;
  const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  $('#damage-rows').innerHTML=list.map(r=>`<label><input type="checkbox" data-damage-row="${esc(r.key)}" ${options.hidden.includes(r.key)?'':'checked'}> ${esc(r.label)}</label>`).join('');
 }
 const view=()=>({style:options.style,roster:options.roster,selected,icons,lang:document.documentElement?.lang||'en',youLabel:t('You'),alliesLabel:t('All allies')});
 function render(){
  const main=$('#damage-table'),foot=$('#damage-total');
  if(table()){
   drawn='';footDrawn='';foot.hidden=true;
   main.innerHTML=window.EpochDamage.table(state,{compact:options.compact,element:options.element,hidden:options.hidden});
  }else{
   model=Styles.model(state,{metric:options.metric,element:options.element,hidden:options.hidden});
   const v=view(),structure=Styles.structureKey(model,v),live=typeof main.querySelectorAll==='function';
   // The layout is drawn again only when it changes; otherwise only numbers change.
   if(structure!==drawn||!live){drawn=structure;main.innerHTML=Styles.html(model,v);}
   const totalKey=Styles.totalKey(model);
   if(totalKey!==footDrawn||!live){footDrawn=totalKey;foot.innerHTML=Styles.totalHtml(model);}
   foot.hidden=false;
   if(live)Styles.update(main,model,v,foot);
  }
  renderRows();
  status(failed?$('#damage-status').textContent:window.EpochDamage.status(state));
  if(shownActive!==!!state?.active){
   shownActive=!!state?.active;const b=$('#damage-toggle'),label=shownActive?'Stop measuring':'Start measuring';
   b.textContent=shownActive?'Ⅱ':'▶';b.title=label;b.setAttribute?.('aria-label',label);
  }
 }
 // The status line: always with the table; with the other styles only for problems.
 function status(text){const s=$('#damage-status');s.textContent=text;s.hidden=!table()&&!failed;}
 function chrome(){
  $('#damage-metric').hidden=table();$('#damage-compact-row').hidden=!table();
  document.body?.setAttribute?.('data-damage-style',options.style);
 }
 async function call(action){
  if(busy)return;busy=true;
  try{
   const r=await transport.run({type:'control',id:action?'damage_session':'damage_session_read',operation:action?'set':'read',args:action?{action}:{}});
   if(r?.ok){state=r;failed=false;render();}
   else{failed=true;status(r?.error||'Waiting for an offline character…');}
  }catch{failed=true;status('EpochPact is not responding.');}
  finally{busy=false;lastRead=Date.now();}
  if(!action)await icon();
 }
 // After a read, up to four missing icons are read one after another, each
 // at most three times; a source the game has no icon for keeps its plain marker.
 async function icon(){
  if(busy||table()||!model)return;
  busy=true;let added=false;
  try{
   for(let n=0;n<4;n++){
    const wanted=model.sources.map(s=>s.icon).find(k=>k&&!icons.has(k)&&(iconTries.get(k)||0)<3);
    if(!wanted)break;
    iconTries.set(wanted,(iconTries.get(wanted)||0)+1);
    let r=null;try{r=await transport.run({type:'control',id:'damage_icon',operation:'read',args:{key:wanted}});}catch{break;}
    if(r?.ok&&typeof r.png==='string'&&/^[A-Za-z0-9+/]+=*$/.test(r.png)&&r.png.length<200000){icons.set(wanted,'data:image/png;base64,'+r.png);added=true;}
    else if(r?.ok)iconTries.set(wanted,3);
    else break;
   }
  }finally{busy=false;}
  if(added)render();
 }
 async function start(){
  window.EpochOverlay.install(window,{options,save,minimum:[260,120]});
  try{const boot=await (await fetch('/api/bootstrap')).json();transport.setSession(boot);}catch{failed=true;status('Open EpochPact first.');return;}
  chrome();
  $('#damage-style').value=options.style;
  $('#damage-style').addEventListener('change',e=>{options.style=Styles.styleOf(e.target.value);rowKeys='';drawn='';save();chrome();render();});
  $('#damage-metric').value=options.metric;
  $('#damage-metric').addEventListener('change',e=>{options.metric=Styles.metricOf(e.target.value);save();render();});
  $('#damage-compact').checked=options.compact;
  $('#damage-compact').addEventListener('change',e=>{options.compact=e.target.checked;save();render();});
  $('#damage-element').value=String(options.element);
  $('#damage-element').addEventListener('change',e=>{options.element=Number(e.target.value);save();render();});
  $('#damage-rows').addEventListener('change',e=>{
   const row=e.target.dataset?.damageRow;if(row===undefined)return;
   options.hidden=options.hidden.filter(k=>k!==row);if(!e.target.checked)options.hidden.push(row);save();render();
  });
  // Choosing a source only changes which damage types are shown, never the total.
  $('#damage-table').addEventListener('click',e=>{
   const el=e.target?.closest?.('[data-src],[data-back],[data-roster]');if(!el||!model)return;
   if(el.hasAttribute('data-roster')){options.roster=!options.roster;save();render();return;}
   if(el.hasAttribute('data-back')){selected=null;render();return;}
   const source=el.dataset.src,shown=options.style==='split'?selected:Styles.selectedSource(model,selected)?.key;
   selected=source===shown||source==='You'?null:source;render();
  });
  $('#damage-toggle').addEventListener('click',()=>{const action=state?.active?'stop':'start';options.running=action==='start';save();void call(action);});
  $('#damage-reset').addEventListener('click',()=>void call('reset'));
  await call();
  setInterval(()=>{if(!document.hidden&&(state?.active||Date.now()-lastRead>=3000))void call();},1000);
  // Resume once per window opening; a later Stop elsewhere is respected.
  let resumed=false;
  const resume=async()=>{if(resumed||!options.running||busy)return;if(!state)await call();if(!state)return;resumed=true;if(!state.active)await call('start');};
  void resume();setInterval(()=>void resume(),3000);
 }
 void start();
})();
