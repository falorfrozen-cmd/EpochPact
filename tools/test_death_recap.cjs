// The death recap and the Taken view: what the panel shows from the core's reply, and damage taken in the
// shape the damage tables and styles read.
const test=require('node:test'),assert=require('node:assert/strict');
const R=require('../ui/death-recap.js'),D=require('../ui/damage-meter.js'),S=require('../ui/damage-styles.js');
const at=(i,v)=>{const a=[0,0,0,0,0,0,0];a[i]=v;return a;};
// A death as the core reports it (deathrecap): newest first.
const death={number:3,secondsAgo:12.4,maxHealth:712,total:1000,dot:50,unsplit:0,byType:[200,0,650,0,0,0,150],
 killer:{attacker:'Frost Wraith',what:'Frost Bolt',dot:false,damage:650},
 attackers:[{name:'Frost Wraith',damage:650,dot:0,hits:1},{name:'Skeleton Archer',damage:300,dot:0,hits:2},{name:'Plague Rat',damage:50,dot:50,hits:1}],
 hits:[{t:-5.5,attacker:'Skeleton Archer',what:'Bow Attack',dot:false,damage:100,health:612,ward:0,parts:at(0,100)},
  {t:-2.5,attacker:'Skeleton Archer',what:'Bow Attack',dot:false,damage:200,health:412,ward:0,parts:[1,0,0,0,0,0,1]},
  {t:-1.5,attacker:'Plague Rat',what:'Poison',dot:true,damage:50,health:362,ward:12,parts:at(6,50)},
  {t:0,attacker:'Frost Wraith',what:'Frost Bolt',dot:false,damage:650,health:-288,ward:0,parts:at(2,1)}],
 effects:[{name:'Poison',stacks:3,buff:false},{name:'Haste',stacks:1,buff:true}]};
const older={...death,number:2,secondsAgo:300,killer:null,attackers:[],hits:[],effects:[],byType:at(0,0),total:0,dot:0};
const reply={ok:true,enabled:true,recording:true,recentHits:4,deaths:[death,older]};

test('the newest death: killer, totals, types with shares, attackers, the last hits newest first and the effects',()=>{
 const html=R.html(reply);
 assert.match(html,/<span>Death<\/span> <b>3<\/b>/);
 assert.match(html,/12 s ago/);
 assert.match(html,/Killed by<\/span> <b><span data-no-translate>Frost Wraith<\/span><\/b> · <span data-no-translate>Frost Bolt<\/span> · <span>Hit<\/span> <b>650<\/b>/);
 assert.match(html,/Damage taken in the last 10 s<\/span> <b>1,000<\/b>/);
 assert.match(html,/damage over time<\/span> <b>50<\/b>/);
 assert.match(html,/maximum health<\/span> <b>712<\/b>/);
 // Damage types in the game's order with their share of the total; types with no damage are left out.
 assert.match(html,/data-type="0">.*Physical<\/span> <b>200<\/b> <small>20%<\/small>/);
 assert.match(html,/data-type="2">.*Cold<\/span> <b>650<\/b> <small>65%<\/small>/);
 assert.match(html,/data-type="6">.*Poison<\/span> <b>150<\/b> <small>15%<\/small>/);
 assert.doesNotMatch(html,/data-type="1"/);
 // Attackers in the core's order (most damage first); a damage over time part is named.
 const rows=[...html.matchAll(/<tr><td><span data-no-translate>([^<]+)<\/span><\/td><td>([^<]+)/g)].map(m=>m[1]);
 assert.deepEqual(rows,['Frost Wraith','Skeleton Archer','Plague Rat']);
 // The last hits, newest first; the killing blow is marked and health below 0 shows as 0.
 const times=[...html.matchAll(/<td>(-?\d+\.\d) s<\/td>/g)].map(m=>m[1]);
 assert.deepEqual(times,['0.0','-1.5','-2.5','-5.5']);
 assert.match(html,/<tr class="dr-fatal"><td>0\.0 s<\/td>.*?<td>0<\/td><\/tr>/);
 assert.match(html,/Poison<\/span> <small>DoT<\/small>/);
 assert.match(html,/362 <small>\+ 12 <span>ward<\/span><\/small>/);
 assert.match(html,/<li class="dr-debuff"><span data-no-translate>Poison<\/span> <small>×3<\/small><\/li><li class="dr-buff"><span data-no-translate>Haste<\/span><\/li>/);
 assert.match(html,/data-death-step="1" >/);assert.match(html,/data-death-step="-1" disabled/);
});

test('ticks of one ailment from one attacker in a row are one line with their count, sum and time span',()=>{
 const tick=(t,what,damage,health,attacker='Plague Rat')=>({t,attacker,what,dot:true,damage,health,ward:0,parts:at(6,1)});
 const many={...death,hits:[tick(-6,'Poison',5,700),tick(-5.5,'Poison',5,695),tick(-5,'Ignite',3,692),tick(-4.5,'Poison',5,687),tick(-4,'Poison',5,682),tick(-3.5,'Poison',5,677,'Other Rat'),
  {t:-1,attacker:'Frost Wraith',what:'Frost Bolt',dot:false,damage:600,health:77,ward:0,parts:at(2,1)},{t:0,attacker:'Frost Wraith',what:'Frost Bolt',dot:false,damage:650,health:-573,ward:0,parts:at(2,1)}]};
 const html=R.html({ok:true,deaths:[many]});
 const rows=[...html.matchAll(/<tr(?: class="dr-fatal")?><td>([^<]+)<\/td><td><span data-no-translate>([^<]+)<\/span><\/td><td><span data-no-translate>([^<]+)<\/span>((?: <small>[^<]+<\/small>)*)<\/td><td>([^<]+)<\/td><td>([^<]+)/g)].map(m=>m.slice(1).map(v=>(v||'').replace(/<[^>]+>/g,'').trim()).join('|'));
 assert.deepEqual(rows,['0.0 s|Frost Wraith|Frost Bolt||650|0','-1.0 s|Frost Wraith|Frost Bolt||600|77','-3.5 s|Other Rat|Poison|DoT|5|677',
  '-4.5 … -4.0 s|Plague Rat|Poison|DoT ×2|10|682','-5.0 s|Plague Rat|Ignite|DoT|3|692','-6.0 … -5.5 s|Plague Rat|Poison|DoT ×2|10|695']);
 // At most 15 lines: a long run of ticks takes one.
 const ticks=Array.from({length:40},(_,i)=>tick(-9+i*0.2,'Poison',1,600-i));
 assert.equal([...R.html({ok:true,deaths:[{...death,hits:ticks}]}).matchAll(/DoT<\/small> <small>×40/g)].length,1);
});

test('an older death, and one whose killing blow was not recorded',()=>{
 const html=R.html(reply,{index:1});
 assert.match(html,/<b>2<\/b>/);assert.match(html,/5 min ago/);
 assert.match(html,/The killing blow was not recorded\./);
 assert.match(html,/None shown by the game\./);
 assert.match(html,/data-death-step="1" disabled/);
 assert.equal(R.html(reply,{index:9}),R.html(reply,{index:1}),'an index past the end shows the oldest');
});

test('off, nothing recorded, not connected',()=>{
 assert.match(R.html({ok:true,enabled:false,deaths:[]}),/The death recap is off/);
 assert.match(R.html({ok:true,enabled:true,deaths:[]}),/No deaths recorded since the game started\./);
 assert.match(R.html(null),/Waiting for an offline character…/);
 assert.match(R.html({ok:false,error:'Load an offline character first.'}),/Load an offline character first\./);
});

test('game text is escaped and malformed values never show NaN',()=>{
 const bad={ok:true,deaths:[{number:'x',secondsAgo:-5,total:'1e400',byType:'no',killer:{attacker:'<b>Boss</b>',what:42,damage:NaN},
  attackers:[{name:'<i>x</i>',damage:'NaN',hits:-3},null],hits:[{t:'abc',attacker:null,damage:Infinity,health:null}],effects:[{name:'<script>',stacks:'2'},{name:''}]}]};
 const html=R.html(bad);
 assert.doesNotMatch(html,/NaN|Infinity|undefined|<script>|<b>Boss<\/b>|<i>x<\/i>/);
 assert.match(html,/&lt;b&gt;Boss&lt;\/b&gt;/);assert.match(html,/&lt;script&gt;/);
 const d=R.deaths(bad)[0];
 assert.equal(d.number,0);assert.equal(d.secondsAgo,0);assert.deepEqual(d.byType,[0,0,0,0,0,0,0]);assert.equal(d.attackers.length,1);assert.equal(d.effects.length,1);
 assert.deepEqual(R.deaths({ok:true,deaths:'no'}),[]);
});

test('numbers in the window language',()=>{
 assert.match(R.html(reply,{lang:'tr'}),/<b>1\.000<\/b>/);
 assert.match(R.html(reply,{lang:'ru'}),/<b>1\s000<\/b>/);
 assert.match(R.html(reply,{lang:'tr'}),/-1,5 s/);
});

// Damage taken during the session, as the core reports it next to the damage dealt.
const wraith={name:'Frost Wraith',label:'Frost Wraith',icon:'',live:0,total:650,hits:1,maxHit:650,dot:0,dps10:65,dps60:10.8,dps600:1.1,dps3600:0.2,average:6.5,combatAverage:65,
 elements:{total:at(2,650),hits:at(2,1),maxHit:at(2,650),dps10:at(2,65),dps60:at(2,10.8),dps600:at(2,1.1),dps3600:at(2,0.2)}};
const rat={name:'Plague Rat',label:'Plague Rat',icon:'',live:0,total:50,hits:4,maxHit:12.5,dot:50,dps10:5,dps60:0.8,dps600:0.1,dps3600:0,average:0.5,combatAverage:5,
 elements:{total:at(6,50),hits:at(6,4),maxHit:at(6,12.5),dps10:at(6,5),dps60:at(6,0.8),dps600:at(6,0.1),dps3600:at(6,0)}};
const taken={types:[wraith,rat],total:700,hits:5,maxHit:650,dot:50,combatSeconds:10,dps10:70,dps60:11.6,dps600:1.2,dps3600:0.2,average:7,combatAverage:70,
 elements:{total:[0,0,650,0,0,0,50],hits:[0,0,1,0,0,0,4],maxHit:[0,0,650,0,0,0,12.5],dps10:[0,0,65,0,0,0,5],dps60:[0,0,10.8,0,0,0,0.8],dps600:[0,0,1.1,0,0,0,0.1],dps3600:[0,0,0.2,0,0,0,0]}};
const session={ok:true,active:true,hooked:true,elapsedSeconds:100,combatSeconds:40,hits:900,unsplitHits:0,types:[{name:'You',total:5,elements:{}}],allies:{total:0},taken,deaths:1,deathRecap:true};

test('damage taken has the damage session\'s shape: attackers as rows, their total as the aggregate',()=>{
 const t=R.takenState(session);
 assert.equal(t.taken,true);assert.equal(t.combatSeconds,10);assert.equal(t.elapsedSeconds,100);
 assert.deepEqual(t.types.map(x=>x.name),['Frost Wraith','Plague Rat']);assert.equal(t.allies,taken);
 const m=S.model(t,{metric:'dps10'});
 assert.equal(m.total,70);assert.equal(m.you,null);assert.deepEqual(m.allyValues,[0,0,65,0,0,0,5]);
 // Combat average uses the seconds the character was hit.
 assert.equal(S.model(t,{metric:'combatAverage'}).allyValues[2],65);
 assert.equal(R.takenState({ok:false,error:'x'}).ok,false);
 assert.deepEqual(R.takenState({ok:true}).types,[]);assert.equal(R.takenState({ok:true}).allies,null);
});

test('the Taken view draws Pact Bars by attacker with all attackers\' damage types until one is chosen',()=>{
 const t=R.takenState(session),m=S.model(t,{metric:'dps10'});
 const view={style:'quiet',taken:true,lang:'en',icons:new Map()};
 const html=S.html(m,view);
 assert.match(html,/dm-bars dm-taken/);assert.match(html,/Damage taken by attacker/);assert.match(html,/All attackers<\/span> ↶/);
 assert.doesNotMatch(html,/dm-quiet/);
 assert.match(S.totalHtml(m,view),/DAMAGE TAKEN.*per s/);
 assert.notEqual(S.totalKey(m,view),S.totalKey(m,{}));assert.notEqual(S.structureKey(m,view),S.structureKey(m,{style:'quiet'}));
 assert.match(S.html(S.model(R.takenState({ok:true,active:true,taken:{types:[]}})),view),/No damage taken yet/);
 // The table: attackers, their total and "Attacker" as the first column.
 const table=D.table(t,{taken:true});
 assert.match(table,/<th>Attacker<\/th>/);assert.match(table,/All attackers/);assert.doesNotMatch(table,/Allies total/);
 assert.match(D.table(R.takenState({ok:true,types:[]}),{taken:true}),/No damage taken yet/);
 assert.deepEqual(D.sources(t,{taken:true}).map(r=>r.key),['Frost Wraith','Plague Rat','__total']);
});
