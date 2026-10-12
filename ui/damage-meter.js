/* Damage session table and CSV, shared by the Summons page and the damage window. */
(function(root,factory){if(typeof module==='object'&&module.exports)module.exports=factory();else root.EpochDamage=factory();})(this,function(){
 'use strict';
 const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
 const fmt=v=>{const n=Number(v);if(!Number.isFinite(n))return '—';if(Math.abs(n)>=1e6)return (n/1e6).toFixed(2)+'M';if(Math.abs(n)>=1e4)return (n/1e3).toFixed(1)+'k';return Math.round(n).toLocaleString('en-US');};
 const duration=s=>{s=Math.max(0,Math.floor(Number(s)||0));const h=Math.floor(s/3600),m=Math.floor(s%3600/60),r=s%60;return (h?h+':'+String(m).padStart(2,'0'):m)+':'+String(r).padStart(2,'0');};
 const columns=[['dps10','DPS 10 s'],['dps60','DPS 60 s'],['dps600','DPS 10 min'],['dps3600','DPS 60 min'],['combatAverage','Avg in combat'],['average','Avg session'],['total','Total damage'],['hits','Hits'],['maxHit','Biggest hit']];
 // Allies first (largest damage first), then the allies' total, then the player.
 function rows(state){
  const types=(state?.types||[]).slice();
  const allies=types.filter(t=>t.name!=='You').sort((a,b)=>(b.total||0)-(a.total||0));
  const you=types.find(t=>t.name==='You');
  const out=allies.map(t=>({...t,label:t.name+(t.live>1?` ×${t.live}`:'')}));
  if(allies.length>1&&state.allies)out.push({...state.allies,label:'Allies total',isTotal:true});
  if(you)out.push({...you,label:'You (character)'});
  return out;
 }
 function status(state){
  if(!state)return 'Not started.';
  const time=duration(state.elapsedSeconds),combat=duration(state.combatSeconds);
  return `${state.active?'Measuring':'Stopped'} · ${time} session · ${combat} in combat · ${fmt(state.hits)} hits`;
 }
 function table(state,{compact=false}={}){
  const list=rows(state);
  if(!list.length)return '<p class="empty">No damage recorded yet. Start the session and fight.</p>';
  const cols=compact?columns.filter(([k])=>['dps10','dps60','combatAverage','total'].includes(k)):columns;
  return `<div class="table-scroll"><table class="damage-table"><thead><tr><th>Source</th>${cols.map(([,l])=>`<th>${l}</th>`).join('')}</tr></thead><tbody>${list.map(r=>`<tr class="${r.isTotal?'damage-total':''}"><td>${esc(r.label)}</td>${cols.map(([k])=>`<td>${fmt(r[k])}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`;
 }
 function csv(state){
  const list=rows(state);
  const quote=v=>{const s=String(v??'');return /[",\n]/.test(s)?'"'+s.replace(/"/g,'""')+'"':s;};
  const head=['Source',...columns.map(([,l])=>l)];
  const lines=[`# EpochPact damage session; ${status(state)}`,head.join(','),...list.map(r=>[quote(r.label),...columns.map(([k])=>Number.isFinite(Number(r[k]))?Number(r[k]).toFixed(1):'')].join(','))];
  return lines.join('\n')+'\n';
 }
 return {rows,status,table,csv,duration,fmt};
});
