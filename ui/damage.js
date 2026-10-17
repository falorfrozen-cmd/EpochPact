/* Always-on-top damage window: reads the damage session once a second while it
   runs (every few seconds otherwise, so a session started or stopped in
   EpochPact's main window shows up here too) and can start, stop or reset it. A session left running when the window closed
   starts again once the game is ready the next time the window opens.
   Styles: Quiet HUD (default), Pact Bars, Split Meter (damage-styles.js) and
   the full table. Each source's icon is the game's own skill or class icon,
   read once per source. Dealt/Taken switches between the damage you and your
   allies deal and the damage your character takes (per attacker type); ☠ opens
   the death recap (death-recap.js), and is marked when a new death was recorded.
   Nothing else is sent to the game. */
(function(){
 'use strict';
 const $=s=>document.querySelector(s);
 const key='epochpact.damage-window.v1';
 const Styles=window.EpochDamageStyles,Recap=window.EpochDeathRecap;
 let options={compact:true,running:false,element:-1,hidden:[],style:'quiet',metric:'dps10',roster:false,view:'dealt',takenHidden:[],deathsSeen:0};
 try{options={...options,...JSON.parse(localStorage.getItem(key)||'{}')};}catch{}
 // The old whole-window opacity becomes the background opacity (overlay.js).
 Object.assign(options,window.EpochOverlay.normalise(options));delete options.opacity;
 if(!Number.isInteger(options.element)||options.element<-1||options.element>6)options.element=-1;
 if(!Array.isArray(options.hidden))options.hidden=[];
 options.hidden=options.hidden.filter(k=>typeof k==='string').slice(0,100);
 if(!Array.isArray(options.takenHidden))options.takenHidden=[];
 options.takenHidden=options.takenHidden.filter(k=>typeof k==='string').slice(0,100);
 options.view=options.view==='taken'?'taken':'dealt';
 options.deathsSeen=Number.isInteger(options.deathsSeen)&&options.deathsSeen>=0?options.deathsSeen:0;
 options.style=Styles.styleOf(options.style);options.metric=Styles.metricOf(options.metric);options.roster=options.roster===true;
 const save=()=>{try{localStorage.setItem(key,JSON.stringify(options));}catch{}};
 let state=null,busy=false,lastRead=0,failed=false;
 // The source whose damage types are shown (null: You); not saved.
 let selected=null,model=null,drawn='',footDrawn='',shownActive=null;
 // The death recap panel: open or not, the death shown (0 = newest), the last reply.
 let recapOpen=false,recapIndex=0,recapReply=null,recapBusy=false,recapRead=0;
 const icons=new Map(),iconTries=new Map();
 const transport=window.EpochSession.create({fetch:(...a)=>fetch(...a),sleep:ms=>new Promise(r=>setTimeout(r,ms)),isReadOnly:p=>p.operation==='read'});
 const t=text=>window.EpochI18n?.t?.(text)??text;
 const table=()=>options.style==='table';
 const taken=()=>options.view==='taken';
 // What the window draws: the damage session, or its damage taken in the same shape.
 const shown=()=>taken()?Recap.takenState(state):state;
 const hiddenList=()=>taken()?options.takenHidden:options.hidden;
 // One switch per row the session has (ally types, the allies' total in the
 // table, you); rebuilt only when that list changes, so an open menu keeps its focus.
 let rowKeys='';
 function renderRows(){
  const list=window.EpochDamage.sources(shown(),{taken:taken()}).filter(r=>table()||r.key!=='__total'),keys=options.view+JSON.stringify(list.map(r=>r.key));
  if(keys===rowKeys)return;
  if(!list.length){if(rowKeys){rowKeys='';$('#damage-rows').innerHTML='<span class="muted">Rows appear once the session records damage.</span>';}return;}
  rowKeys=keys;
  const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  $('#damage-rows').innerHTML=list.map(r=>`<label><input type="checkbox" data-damage-row="${esc(r.key)}" ${hiddenList().includes(r.key)?'':'checked'}> <span${r.key==='__total'||r.key==='You'?'':' data-no-translate'}>${esc(r.label)}</span></label>`).join('');
 }
 const view=()=>({style:options.style,roster:options.roster,selected,icons,lang:document.documentElement?.lang||'en',youLabel:t('You'),alliesLabel:t('All allies'),taken:taken(),allLabel:t('All attackers')});
 function render(){
  const main=$('#damage-table'),foot=$('#damage-total');
  if(table()){
   drawn='';footDrawn='';foot.hidden=true;
   main.innerHTML=window.EpochDamage.table(shown(),{compact:options.compact,element:options.element,hidden:hiddenList(),taken:taken()});
  }else{
   model=Styles.model(shown(),{metric:options.metric,element:options.element,hidden:hiddenList()});
   const v=view(),structure=Styles.structureKey(model,v),live=typeof main.querySelectorAll==='function';
   // The layout is drawn again only when it changes; otherwise only numbers change.
   if(structure!==drawn||!live){drawn=structure;main.innerHTML=Styles.html(model,v);}
   const totalKey=Styles.totalKey(model,v);
   if(totalKey!==footDrawn||!live){footDrawn=totalKey;foot.innerHTML=Styles.totalHtml(model,v);}
   foot.hidden=false;
   if(live)Styles.update(main,model,v,foot);
  }
  renderRows();
  status(failed?$('#damage-status').textContent:window.EpochDamage.status(shown()));
  deathMark();
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
  document.body?.setAttribute?.('data-damage-view',options.view);
 }
 // ☠ is marked while a death the player has not looked at is recorded. The count starts again
 // with each game start, so a lower count than the one seen means a new game.
 function deathMark(){
  const count=Number(state?.deaths);if(!Number.isInteger(count)||count<0)return;
  if(count<options.deathsSeen){options.deathsSeen=0;save();}
  if(recapOpen&&count!==options.deathsSeen){options.deathsSeen=count;save();void readRecap(true);}
  const b=$('#damage-deaths'),fresh=count>options.deathsSeen,label=fresh?'Death recap (new death)':'Death recap';
  b.classList?.toggle?.('dm-alert',fresh);if(b.title!==label){b.title=label;b.setAttribute?.('aria-label',label);}
 }
 async function readRecap(force){
  if(recapBusy||!recapOpen||(!force&&Date.now()-recapRead<3000))return;
  recapBusy=true;
  try{const r=await transport.run({type:'control',id:'death_recap_read',operation:'read',args:{}});recapReply=r;}
  catch{recapReply={ok:false,error:'EpochPact is not responding.'};}
  finally{recapBusy=false;recapRead=Date.now();}
  drawRecap();
 }
 function drawRecap(){
  const body=$('#damage-recap-body');if(!body)return;
  const count=Recap.deaths(recapReply).length;recapIndex=Math.min(recapIndex,Math.max(0,count-1));
  body.innerHTML=Recap.html(recapReply,{index:recapIndex,lang:document.documentElement?.lang||'en'});
 }
 function showRecap(open){
  recapOpen=open;$('#damage-recap').hidden=!open;$('#damage-deaths').setAttribute?.('aria-expanded',String(open));
  if(open){recapIndex=0;const count=Number(state?.deaths);if(Number.isInteger(count)&&count>=0){options.deathsSeen=count;save();}deathMark();drawRecap();void readRecap(true);}
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
  $('#damage-view').value=options.view;
  $('#damage-view').addEventListener('change',e=>{options.view=e.target.value==='taken'?'taken':'dealt';selected=null;rowKeys='';drawn='';footDrawn='';save();chrome();render();});
  $('#damage-deaths').addEventListener('click',()=>showRecap(!recapOpen));
  $('#damage-recap-close').addEventListener('click',()=>showRecap(false));
  $('#damage-recap').addEventListener('click',e=>{
   const step=e.target?.closest?.('[data-death-step]');if(!step)return;
   recapIndex=Math.max(0,recapIndex+Number(step.dataset.deathStep||0));drawRecap();
  });
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
   const list=hiddenList().filter(k=>k!==row);if(!e.target.checked)list.push(row);
   if(taken())options.takenHidden=list;else options.hidden=list;save();render();
  });
  // Choosing a source only changes which damage types are shown, never the total.
  $('#damage-table').addEventListener('click',e=>{
   const el=e.target?.closest?.('[data-src],[data-back],[data-roster]');if(!el||!model)return;
   if(el.hasAttribute('data-roster')){options.roster=!options.roster;save();render();return;}
   if(el.hasAttribute('data-back')){selected=null;render();return;}
   const source=el.dataset.src;
   if(taken()){selected=source===selected?null:source;render();return;}
   const current=options.style==='split'?selected:Styles.selectedSource(model,selected)?.key;
   selected=source===current||source==='You'?null:source;render();
  });
  $('#damage-toggle').addEventListener('click',()=>{const action=state?.active?'stop':'start';options.running=action==='start';save();void call(action);});
  $('#damage-reset').addEventListener('click',()=>void call('reset'));
  await call();
  setInterval(()=>{if(!document.hidden&&(state?.active||Date.now()-lastRead>=3000))void call();if(recapOpen&&!document.hidden)void readRecap(false);},1000);
  // Resume once per window opening; a later Stop elsewhere is respected.
  let resumed=false;
  const resume=async()=>{if(resumed||!options.running||busy)return;if(!state)await call();if(!state)return;resumed=true;if(!state.active)await call('start');};
  void resume();setInterval(()=>void resume(),3000);
 }
 void start();
})();
