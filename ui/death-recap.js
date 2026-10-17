/* Death recap and damage taken, shared by the Summons page and the damage window.
   The core keeps the hits the character took in the last 20 s; at a death it keeps
   the 10 s before it with the buffs and debuffs the game showed. html() draws one
   death (0 = the newest); takenState() turns the damage session's "taken" part into
   the shape the damage tables and styles read, so damage taken is drawn the same
   way as damage dealt. Names of attackers, skills, ailments and effects are the
   game's own text and are not translated. */
(function(root,factory){if(typeof module==='object'&&module.exports)module.exports=factory();else root.EpochDeathRecap=factory();})(this,function(){
 'use strict';
 const TYPES=['Physical','Fire','Cold','Lightning','Necrotic','Void','Poison'];
 const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
 const num=v=>{if(v===null||v===undefined||v===''||typeof v==='boolean')return null;const n=Number(v);return Number.isFinite(n)?n:null;};
 const text=(v,most=96)=>typeof v==='string'?v.slice(0,most):'';
 function formatter(lang){
  const locale=['tr','ru'].includes(lang)?lang:'en-US';
  const whole=new Intl.NumberFormat(locale,{maximumFractionDigits:0}),tenth=new Intl.NumberFormat(locale,{minimumFractionDigits:1,maximumFractionDigits:1});
  return {whole:v=>{const n=num(v);return n===null?'—':whole.format(n);},tenth:v=>{const n=num(v);return n===null?'—':tenth.format(n);}};
 }
 // The deaths the core reported, newest first, with every field checked.
 function deaths(reply){
  if(!reply||reply.ok===false||!Array.isArray(reply.deaths))return [];
  return reply.deaths.filter(d=>d&&typeof d==='object').slice(0,10).map(d=>({
   number:Math.max(0,Math.floor(num(d.number)||0)),secondsAgo:Math.max(0,num(d.secondsAgo)||0),maxHealth:num(d.maxHealth),
   total:num(d.total)||0,dot:num(d.dot)||0,unsplit:num(d.unsplit)||0,
   byType:TYPES.map((_,i)=>Math.max(0,num(Array.isArray(d.byType)?d.byType[i]:0)||0)),
   killer:d.killer&&typeof d.killer==='object'?{attacker:text(d.killer.attacker)||'Unknown',what:text(d.killer.what),dot:d.killer.dot===true,damage:num(d.killer.damage)||0}:null,
   attackers:(Array.isArray(d.attackers)?d.attackers:[]).filter(a=>a&&typeof a==='object').slice(0,40).map(a=>({name:text(a.name)||'Unknown',damage:num(a.damage)||0,dot:num(a.dot)||0,hits:Math.max(0,Math.floor(num(a.hits)||0))})),
   hits:(Array.isArray(d.hits)?d.hits:[]).filter(h=>h&&typeof h==='object').slice(-600).map(h=>({t:Math.min(0,num(h.t)||0),attacker:text(h.attacker)||'Unknown',what:text(h.what),dot:h.dot===true,damage:num(h.damage)||0,health:num(h.health),ward:num(h.ward)||0})),
   effects:(Array.isArray(d.effects)?d.effects:[]).filter(e=>e&&typeof e==='object'&&text(e.name)).slice(0,32).map(e=>({name:text(e.name,80),stacks:Math.max(0,Math.floor(num(e.stacks)||0)),buff:e.buff===true}))
  }));
 }
 const name=v=>`<span data-no-translate>${esc(v)}</span>`;
 function killerLine(d,f){
  if(!d.killer)return '<p class="dr-killer"><span>The killing blow was not recorded.</span></p>';
  const k=d.killer;
  return `<p class="dr-killer"><span>Killed by</span> <b>${name(k.attacker)}</b>${k.what?` · ${name(k.what)}`:''} · <span>${k.dot?'Damage over time':'Hit'}</span> <b>${f.whole(k.damage)}</b></p>`;
 }
 function html(reply,{index=0,lang='en'}={}){
  if(!reply||reply.ok===false)return `<p class="empty">${esc(reply?.error||'Waiting for an offline character…')}</p>`;
  const list=deaths(reply);
  if(!list.length)return reply.enabled===false?'<p class="empty">The death recap is off. Turn it on to keep the hits before each death.</p>':'<p class="empty">No deaths recorded since the game started.</p>';
  const at=Math.min(Math.max(0,Math.floor(Number(index)||0)),list.length-1),d=list[at],f=formatter(lang);
  const ago=d.secondsAgo<90?`${Math.round(d.secondsAgo)} s ago`:`${Math.round(d.secondsAgo/60)} min ago`;
  const nav=list.length>1?`<span class="dr-nav"><button type="button" data-death-step="1" ${at>=list.length-1?'disabled':''}>‹ <span>Older</span></button><button type="button" data-death-step="-1" ${at<=0?'disabled':''}><span>Newer</span> ›</button></span>`:'';
  const types=TYPES.map((t,i)=>({t,i,v:d.byType[i]})).filter(x=>x.v>0);
  const share=v=>d.total>0?Math.round(100*v/d.total)+'%':'—';
  // The last hits, newest first; ticks of one ailment from one attacker in a row are one line.
  const hits=[];
  for(const h of d.hits.slice().reverse()){
   const last=hits[hits.length-1];
   if(last&&h.dot&&last.dot&&last.attacker===h.attacker&&last.what===h.what){last.count++;last.damage+=h.damage;last.from=h.t;}
   else{if(hits.length>=15)break;hits.push({...h,count:1,from:h.t});}
  }
  return `<div class="dr" data-death="${at}"><div class="dr-head"><span><span>Death</span> <b>${d.number}</b></span><span class="muted">${ago}</span>${nav}</div>${killerLine(d,f)}`+
   `<p class="dr-sum"><span>Damage taken in the last 10 s</span> <b>${f.whole(d.total)}</b>${d.dot>0?` · <span>damage over time</span> <b>${f.whole(d.dot)}</b>`:''}${d.maxHealth>0?` · <span>maximum health</span> <b>${f.whole(d.maxHealth)}</b>`:''}</p>`+
   (types.length?`<div class="dr-types">${types.map(x=>`<span class="dr-type" data-type="${x.i}"><i class="dm-ico dm-t${x.i}" aria-hidden="true"></i><span>${x.t}</span> <b>${f.whole(x.v)}</b> <small>${share(x.v)}</small></span>`).join('')}</div>`:'')+
   (d.unsplit>0?'<p class="dm-note">Some hits are not split by damage type.</p>':'')+
   (d.attackers.length?`<h4>By attacker</h4><div class="table-scroll"><table class="dr-table"><thead><tr><th>Attacker</th><th>Damage</th><th>Share</th><th>Hits</th></tr></thead><tbody>${d.attackers.map(a=>`<tr><td>${name(a.name)}</td><td>${f.whole(a.damage)}${a.dot>0?` <small>(<span>damage over time</span> ${f.whole(a.dot)})</small>`:''}</td><td>${share(a.damage)}</td><td>${f.whole(a.hits)}</td></tr>`).join('')}</tbody></table></div>`:'')+
   (hits.length?`<h4>Last hits</h4><div class="table-scroll"><table class="dr-table dr-hits"><thead><tr><th>Time</th><th>Attacker</th><th>Skill or ailment</th><th>Damage</th><th>Health after</th></tr></thead><tbody>${hits.map(h=>`<tr${h.health!==null&&h.health<=0?' class="dr-fatal"':''}><td>${h.count>1?`${f.tenth(h.from)} … ${f.tenth(h.t)}`:f.tenth(h.t)} s</td><td>${name(h.attacker)}</td><td>${h.what?name(h.what):'—'}${h.dot?' <small>DoT</small>':''}${h.count>1?` <small>×${h.count}</small>`:''}</td><td>${f.whole(h.damage)}</td><td>${f.whole(h.health===null?null:Math.max(0,h.health))}${h.ward>0?` <small>+ ${f.whole(h.ward)} <span>ward</span></small>`:''}</td></tr>`).join('')}</tbody></table></div>`:'')+
   `<h4>Buffs and debuffs at death</h4>${d.effects.length?`<ul class="dr-effects">${d.effects.map(e=>`<li class="${e.buff?'dr-buff':'dr-debuff'}">${name(e.name)}${e.stacks>1?` <small>×${e.stacks}</small>`:''}</li>`).join('')}</ul>`:'<p class="muted">None shown by the game.</p>'}</div>`;
 }
 // The damage session's damage taken, in the shape of the session itself: one row per attacker type,
 // the attackers' total in place of the allies' total.
 function takenState(state){
  if(!state||typeof state!=='object'||state.ok===false)return state;
  const t=state.taken&&typeof state.taken==='object'?state.taken:{};
  const types=Array.isArray(t.types)?t.types.filter(x=>x&&typeof x==='object'&&typeof x.name==='string'&&x.name).map(x=>({...x,icon:'',live:0,label:x.name})):[];
  return {ok:true,active:!!state.active,hooked:state.hooked,elapsedSeconds:state.elapsedSeconds,combatSeconds:num(t.combatSeconds)||0,hits:num(t.hits)||0,unsplitHits:0,types,allies:types.length?t:null,taken:true};
 }
 return {TYPES,deaths,html,takenState,formatter};
});
