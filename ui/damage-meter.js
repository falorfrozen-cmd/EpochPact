/* Damage session table and CSV, shared by the Summons page and the damage window. */
(function(root,factory){if(typeof module==='object'&&module.exports)module.exports=factory();else root.EpochDamage=factory();})(this,function(){
 'use strict';
 const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
 const fmt=v=>{const n=Number(v);if(!Number.isFinite(n))return '—';if(Math.abs(n)>=1e6)return (n/1e6).toFixed(2)+'M';if(Math.abs(n)>=1e4)return (n/1e3).toFixed(1)+'k';return Math.round(n).toLocaleString('en-US');};
 const duration=s=>{s=Math.max(0,Math.floor(Number(s)||0));const h=Math.floor(s/3600),m=Math.floor(s%3600/60),r=s%60;return (h?h+':'+String(m).padStart(2,'0'):m)+':'+String(r).padStart(2,'0');};
 const columns=[['dps10','DPS 10 s'],['dps60','DPS 60 s'],['dps600','DPS 10 min'],['dps3600','DPS 60 min'],['combatAverage','Avg in combat'],['average','Avg session'],['total','Total damage'],['hits','Hits'],['maxHit','Biggest hit']];
 // The game's damage types, in its own order (the native session reports them so).
 const elements=['Physical','Fire','Cold','Lightning','Necrotic','Void','Poison'];
 const elementOf=e=>{const n=Number(e);return e!==''&&e!==null&&Number.isInteger(n)&&n>=0&&n<elements.length?n:-1;};
 // One damage type's part of a row: each hit is split by its damage per type.
 function part(row,element,state){
  const e=row.elements||{},v=k=>Number(e[k]?.[element])||0,total=v('total');
  return {...row,total,hits:v('hits'),maxHit:v('maxHit'),dps10:v('dps10'),dps60:v('dps60'),dps600:v('dps600'),dps3600:v('dps3600'),
   average:state?.elapsedSeconds>0?total/state.elapsedSeconds:0,combatAverage:state?.combatSeconds>0?total/state.combatSeconds:0};
 }
 // Allies first (largest damage first), then the allies' total, then the player.
 // Each row has a key for hiding it: the ally type's name, __total or You.
 function rows(state,{element=-1,hidden=[]}={}){
  const el=elementOf(element),pick=t=>el<0?t:part(t,el,state);
  const types=(state?.types||[]).map(pick);
  const allies=types.filter(t=>t.name!=='You').sort((a,b)=>(b.total||0)-(a.total||0));
  const you=types.find(t=>t.name==='You');
  const out=allies.map(t=>({...t,key:t.name,label:t.name+(t.live>1?` ×${t.live}`:'')}));
  if(allies.length>1&&state.allies)out.push({...pick(state.allies),key:'__total',label:'Allies total',isTotal:true});
  if(you)out.push({...you,key:'You',label:'You (character)'});
  const hide=new Set(hidden||[]);
  return out.filter(r=>!hide.has(r.key));
 }
 // Every row the session has, for the show/hide choices.
 function sources(state){return rows(state).map(r=>({key:r.key,label:r.isTotal||r.key==='You'?r.label:r.key}));}
 function status(state){
  if(!state)return 'Not started.';
  const time=duration(state.elapsedSeconds),combat=duration(state.combatSeconds);
  return `${state.active?'Measuring':'Stopped'} · ${time} session · ${combat} in combat · ${fmt(state.hits)} hits`;
 }
 function table(state,{compact=false,element=-1,hidden=[]}={}){
  const list=rows(state,{element,hidden}),el=elementOf(element);
  if(!list.length)return state?.types?.length?'<p class="empty">Every row is hidden. Choose the rows to show.</p>':'<p class="empty">No damage recorded yet. Start the session and fight.</p>';
  const cols=compact?columns.filter(([k])=>['dps10','dps60','combatAverage','total'].includes(k)):columns;
  const source=el<0?'Source':`Source · ${elements[el]}`;
  return `<div class="table-scroll"><table class="damage-table"><thead><tr><th>${source}</th>${cols.map(([,l])=>`<th>${l}</th>`).join('')}</tr></thead><tbody>${list.map(r=>`<tr class="${r.isTotal?'damage-total':''}"><td>${esc(r.label)}</td>${cols.map(([k])=>`<td>${fmt(r[k])}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`;
 }
 // All damage, then each damage type's total, for every row.
 function csv(state){
  const list=rows(state);
  const quote=v=>{const s=String(v??'');return /[",\n]/.test(s)?'"'+s.replace(/"/g,'""')+'"':s;};
  const number=v=>Number.isFinite(Number(v))?Number(v).toFixed(1):'';
  const head=['Source',...columns.map(([,l])=>l),...elements.map(e=>e+' damage')];
  const lines=[`# EpochPact damage session; ${status(state)}`,head.join(','),...list.map(r=>[quote(r.label),...columns.map(([k])=>number(r[k])),...elements.map((_,i)=>number(r.elements?.total?.[i]))].join(','))];
  return lines.join('\n')+'\n';
 }
 return {rows,sources,status,table,csv,duration,fmt,elements};
});
