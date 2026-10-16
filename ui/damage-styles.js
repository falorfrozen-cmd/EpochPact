/* The damage window's compact styles (Quiet HUD, Pact Bars, Split Meter).
   model() turns the damage session the core reports into numbers for one
   averaging window; html() draws a style once and update() only changes its
   numbers on the next reads, so buttons keep their focus. Rules:
   - TOTAL DPS is the sum of every source in the session (You and each ally
     type); the allies' aggregate is not added again and "×4" is a count, not a
     multiplier. Choosing a source or hiding a row never changes it.
   - Damage types keep the game's order (Physical … Poison). Session and Combat
     type values are each type's total divided by that time.
   - Missing or broken values show "—", never NaN. */
(function(root,factory){if(typeof module==='object'&&module.exports)module.exports=factory(require('./damage-meter.js'));else root.EpochDamageStyles=factory(root.EpochDamage);})(this,function(D){
 'use strict';
 const STYLES=['quiet','bars','split','table'];
 const METRICS=['dps10','dps60','dps600','dps3600','average','combatAverage'];
 const TYPES=['Physical','Fire','Cold','Lightning','Necrotic','Void','Poison'];
 const SHORT=['Phys.','Fire','Cold','Light.','Necro.','Void','Poison'];
 const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
 const num=v=>{if(v===null||v===undefined||v===''||typeof v==='boolean')return null;const n=Number(v);return Number.isFinite(n)?n:null;};
 const metricOf=m=>METRICS.includes(m)?m:'dps10';
 const styleOf=s=>STYLES.includes(s)?s:'quiet';

 // One source's seven damage-type values for the metric (null where unknown).
 function typeValues(source,metric,state){
  const e=source?.elements;if(!e||typeof e!=='object')return null;
  if(metric==='average'||metric==='combatAverage'){
   const seconds=num(metric==='average'?state?.elapsedSeconds:state?.combatSeconds),totals=e.total;
   if(!Array.isArray(totals))return null;
   return TYPES.map((_,i)=>{const v=num(totals[i]);return v===null?null:seconds>0?v/seconds:0;});
  }
  const list=e[metric];if(!Array.isArray(list))return null;
  return TYPES.map((_,i)=>num(list[i]));
 }
 const add=(a,b)=>a===null?b:b===null?a:a+b;
 const sum=list=>list.reduce((a,v)=>add(a,v),null);
 function model(state,{metric='dps10',element=-1,hidden=[]}={}){
  metric=metricOf(metric);
  const el=Number.isInteger(element)&&element>=0&&element<TYPES.length?element:-1;
  const ok=!!state&&typeof state==='object'&&state.ok!==false&&Array.isArray(state.types);
  const types=ok?state.types.filter(t=>t&&typeof t==='object'&&typeof t.name==='string'&&t.name):[];
  const hide=new Set(Array.isArray(hidden)?hidden:[]);
  const sources=types.map((t,index)=>{
   const values=typeValues(t,metric,state);
   const dps=el<0?num(t[metric]):values?values[el]:null;
   const live=Math.max(0,Math.floor(num(t.live)??0));
   return {key:t.name,name:t.name,label:t.name==='You'?'You':D.allyName(t),you:t.name==='You',live,icon:typeof t.icon==='string'?t.icon:'',dps,values,index,hidden:hide.has(t.name)};
  });
  const you=sources.find(s=>s.you)||null;
  const allies=sources.filter(s=>!s.you).sort((a,b)=>(b.dps??-1)-(a.dps??-1)||a.index-b.index);
  // The allies' aggregate from the core, or the same sum worked out here.
  const aggregate=ok&&state.allies&&typeof state.allies==='object'?state.allies:null;
  const own=allies.length?TYPES.map((_,i)=>sum(allies.map(a=>a.values?a.values[i]:null))):null;
  const allyValues=allies.length?typeValues(aggregate,metric,state)||own:null;
  let allyDps=null;
  if(allies.length){allyDps=el<0?num(aggregate?.[metric]):allyValues?allyValues[el]:null;if(allyDps===null)allyDps=sum(allies.map(a=>a.dps));}
  const total=sources.length?sum(sources.map(s=>s.dps)):null;
  return {ok,active:!!state?.active,hooked:state?.hooked!==false,metric,element:el,
   elapsed:num(state?.elapsedSeconds),unsplit:(num(state?.unsplitHits)||0)>0,
   sources,you,allies,allyDps,allyValues,total,
   empty:!sources.length,error:ok?'':String(state?.error||'')};
 }

 // Numbers in the window's language (54,240 / 54.240 / 54 240). Below a million every digit;
 // from a million three significant digits (2.90M, 17.9M, 236M, 1.25B); short ones as 18.4k.
 function formatter(lang){
  const locale=['tr','ru'].includes(lang)?lang:'en-US';
  const fixed=[0,1,2].map(d=>new Intl.NumberFormat(locale,{minimumFractionDigits:d,maximumFractionDigits:d}));
  const whole=fixed[0];
  const bad=v=>v===null||v===undefined||!Number.isFinite(v);
  const three=(v,unit,suffix)=>{const x=v/unit,a=Math.abs(x);return fixed[a>=99.95?0:a>=9.995?1:2].format(x)+suffix;};
  const full=v=>bad(v)?'—':Math.abs(v)>=999.5e6?three(v,1e9,'B'):Math.abs(v)>=1e6?three(v,1e6,'M'):whole.format(v);
  const short=v=>bad(v)?'—':Math.abs(v)>=999.5e6?three(v,1e9,'B'):Math.abs(v)>=999.5e3?three(v,1e6,'M'):Math.abs(v)>=999.5?three(v,1e3,'k'):whole.format(v);
  return {full,short};
 }
 const typeIcon=i=>`<i class="dm-ico dm-t${i}" aria-hidden="true"></i>`;
 function sourceIcon(source,icons){
  const url=source.icon&&icons&&typeof icons.get==='function'?icons.get(source.icon):'';
  return url?`<img class="dm-src-ico" src="${esc(url)}" alt="" aria-hidden="true">`:`<i class="dm-src-ico dm-dot" aria-hidden="true"></i>`;
 }
 const sourceName=s=>s.you?'<span class="dm-name"><span>You</span></span>':`<span class="dm-name"><span data-no-translate>${esc(s.label)}</span>${s.live>1?`<small data-no-translate>×${s.live}</small>`:''}</span>`;

 // The source whose damage types Quiet HUD and Pact Bars show: the chosen one
 // while it is still there and shown, else You, else the first shown source.
 function selectedSource(m,selected){
  return m.sources.find(s=>s.key===selected&&!s.hidden)||(m.you&&!m.you.hidden?m.you:null)||m.sources.find(s=>!s.hidden)||null;
 }
 function stripHtml(){
  return `<div class="dm-strip">${TYPES.map((t,i)=>`<span class="dm-type" data-type="${i}" title="${t}">${typeIcon(i)}<span class="dm-tl">${SHORT[i]}</span><b data-tv="${i}">—</b></span>`).join('')}</div>`;
 }
 function rowHtml(s,icons){
  return `<button type="button" class="dm-row${s.you?' dm-row-you':''}" data-src="${esc(s.key)}" aria-pressed="false"><span class="dm-fill" aria-hidden="true"></span>${sourceIcon(s,icons)}${sourceName(s)}<span class="dm-val" data-sv>—</span></button>`;
 }
 const unsplitNote='<p class="dm-note" data-unsplit hidden>Some hits are not split by damage type.</p>';

 // Everything that changes the layout rather than a number: when it changes, html() runs again.
 function structureKey(m,view={}){
  const visible=m.sources.filter(s=>!s.hidden).map(s=>`${s.key}\u0001${s.label}\u0001${s.icon}\u0001${s.live>1?s.live:''}`).join('\u0002');
  const icons=m.sources.map(s=>s.icon&&view.icons?.get?.(s.icon)?1:0).join('');
  return [styleOf(view.style),m.ok,m.empty,m.error,view.roster?1:0,visible,icons].join('\u0003');
 }
 function emptyHtml(m){
  if(!m.ok)return `<p class="empty">${m.error?esc(m.error):'Waiting for an offline character…'}</p>`;
  return '<p class="empty">No damage recorded yet. Start the session and fight.</p>';
 }
 function html(m,view={}){
  const style=styleOf(view.style),icons=view.icons;
  if(m.empty)return `<div class="dm dm-empty">${emptyHtml(m)}</div>`;
  const visible=m.sources.filter(s=>!s.hidden),allies=m.allies.filter(s=>!s.hidden);
  const youShown=m.you&&!m.you.hidden;
  if(!visible.length)return `<div class="dm dm-empty"><p class="empty">Every row is hidden. Choose the rows to show.</p></div>`;
  if(style==='bars'){
   const ranked=visible.slice().sort((a,b)=>(b.dps??-1)-(a.dps??-1)||a.index-b.index);
   return `<div class="dm dm-bars"><div class="dm-meta"><span>Damage by source</span><span data-v="clock">—</span></div><div class="dm-rows">${ranked.map(s=>rowHtml(s,icons)).join('')}</div>`+
    `<div class="dm-caption"><span><span>Damage types</span> · <span data-v="selname" data-no-translate></span></span><button type="button" class="dm-back" data-back hidden><span>You</span> ↶</button></div>${stripHtml()}${unsplitNote}</div>`;
  }
  if(style==='split'){
   return `<div class="dm dm-split">`+
    (youShown?`<div class="dm-col"><div class="dm-head"><span>You</span><strong data-v="you">—</strong></div><div class="dm-lines">${TYPES.map((t,i)=>`<div class="dm-line" data-type="${i}">${typeIcon(i)}<span>${t}</span><b data-tv="${i}">—</b></div>`).join('')}</div></div>`:'')+
    `<div class="dm-col"><div class="dm-head"><span>Allies</span><strong data-v="allies">—</strong></div>`+
    (allies.length?`<div class="dm-rows">${allies.map(s=>rowHtml(s,icons)).join('')}</div><div class="dm-inspect"><div class="dm-inspect-title"><span data-v="inspect" data-no-translate></span> · <span>damage types</span></div><div class="dm-lines" data-v="inspectlines"></div></div>`:'<p class="dm-none">No allies recorded.</p>')+
    `</div>${unsplitNote}</div>`;
  }
  // Quiet HUD: one source's damage types; the allies on request.
  return `<div class="dm dm-quiet"><div class="dm-sum"><span data-v="selname" data-no-translate></span><strong data-v="seldps">—</strong></div>${stripHtml()}`+
   (allies.length?`<button type="button" class="dm-allies" data-roster aria-expanded="${view.roster?'true':'false'}"><span><span>Allies</span> <span aria-hidden="true">${view.roster?'⌃':'⌄'}</span></span><strong data-v="allies">—</strong></button>`+
    `<div class="dm-rows"${view.roster?'':' hidden'}>${allies.map(s=>rowHtml(s,icons)).join('')}</div>`:'')+
   `${unsplitNote}</div>`;
 }
 // The footer: the session's total, outside the scrolling part.
 function totalKey(m){return [m.element,m.active].join(':');}
 function totalHtml(m){
  const label=m.element<0?'<span>TOTAL DPS</span>':`<span>TOTAL DPS</span> · <span>${TYPES[m.element]}</span>`;
  return `<span class="dm-total-label"><i class="dm-rec${m.active?' dm-on':''}" title="${m.active?'Measuring':'Stopped'}"></i>${label}</span><strong class="dm-total-number"><span data-v="total">—</span><small>DPS</small></strong>`;
 }

 // Numbers only: row values and bars, the type values, the selection, the total.
 function update(root,m,view={},foot=null){
  const f=formatter(view.lang),style=styleOf(view.style);
  const setText=(node,text)=>{if(node&&node.textContent!==text)node.textContent=text;};
  const q=s=>root.querySelector(s);
  if(foot)setText(foot.querySelector('[data-v="total"]'),f.full(m.total));
  if(m.empty)return;
  const selected=style==='split'?null:selectedSource(m,view.selected);
  const target=style==='split'?m.sources.find(s=>!s.you&&!s.hidden&&s.key===view.selected)||null:null;
  const group=style==='bars'?m.sources.filter(s=>!s.hidden):m.allies.filter(s=>!s.hidden);
  const largest=Math.max(0,...group.map(s=>s.dps||0));
  for(const node of root.querySelectorAll('[data-src]')){
   const s=m.sources.find(x=>x.key===node.dataset.src);if(!s)continue;
   setText(node.querySelector('[data-sv]'),f.full(s.dps));
   const fill=node.querySelector('.dm-fill');if(fill)fill.style.width=(largest>0&&s.dps>0?Math.min(100,100*s.dps/largest):0).toFixed(1)+'%';
   const pressed=String(style==='split'?target===s:selected===s);
   if(node.getAttribute('aria-pressed')!==pressed)node.setAttribute('aria-pressed',pressed);
  }
  const strip=style==='split'?m.you:selected;
  for(const node of root.querySelectorAll('[data-tv]')){
   const i=Number(node.dataset.tv),v=strip?.values?strip.values[i]:null;
   setText(node,style==='split'?f.full(v):f.short(v));
   const cell=node.closest('[data-type]');
   if(cell){cell.classList.toggle('dm-zero',!v);cell.classList.toggle('dm-chosen',m.element===i);}
  }
  const name=s=>s?(s.you?view.youLabel||'You':s.label):'—';
  setText(q('[data-v="selname"]'),name(selected));
  setText(q('[data-v="seldps"]'),f.full(selected?selected.dps:null));
  setText(q('[data-v="you"]'),f.full(m.you?m.you.dps:null));
  setText(q('[data-v="allies"]'),f.full(m.allyDps));
  const back=q('[data-back]');if(back)back.hidden=!selected||selected.you||!m.you||m.you.hidden;
  const clock=q('[data-v="clock"]');
  if(clock){const s=Math.max(0,Math.floor(m.elapsed||0)),h=Math.floor(s/3600),mm=Math.floor(s%3600/60);setText(clock,(h?h+':'+String(mm).padStart(2,'0'):mm)+':'+String(s%60).padStart(2,'0'));}
  const lines=q('[data-v="inspectlines"]');
  if(lines){
   const values=target?target.values:m.allyValues;
   setText(q('[data-v="inspect"]'),target?target.label:view.alliesLabel||'All allies');
   const shown=TYPES.map((t,i)=>({t,i,v:values?values[i]:null})).filter(x=>x.v>0);
   const key=shown.map(x=>x.i).join(',')||'none';
   if(lines.dataset.key!==key){lines.dataset.key=key;lines.innerHTML=shown.length?shown.map(x=>`<div class="dm-line">${typeIcon(x.i)}<span>${x.t}</span><b data-iv="${x.i}"></b></div>`).join(''):'<p class="dm-none">No damage yet.</p>';}
   for(const x of shown)setText(lines.querySelector(`[data-iv="${x.i}"]`),f.full(x.v));
  }
  const note=q('[data-unsplit]');if(note)note.hidden=!m.unsplit;
 }
 return {STYLES,METRICS,TYPES,SHORT,styleOf,metricOf,typeValues,model,formatter,selectedSource,structureKey,html,totalKey,totalHtml,update};
});
