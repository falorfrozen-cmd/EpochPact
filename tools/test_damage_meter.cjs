const test=require('node:test'),assert=require('node:assert/strict');
const D=require('../ui/damage-meter.js');
const state={ok:true,active:true,elapsedSeconds:125,combatSeconds:80,hits:42,
 types:[{name:'You',live:1,total:900,hits:10,maxHit:200,dps10:5,dps60:4,dps600:3,dps3600:2,average:7.2,combatAverage:11.25},
  {name:'SummonedSkeleton',live:10,total:5000,hits:20,maxHit:300,dps10:80,dps60:60,dps600:40,dps3600:40,average:40,combatAverage:62.5},
  {name:'Giant T_Rex Minion',live:1,total:8000,hits:12,maxHit:900,dps10:120,dps60:100,dps600:64,dps3600:64,average:64,combatAverage:100}],
 allies:{total:13000,hits:32,maxHit:900,dps10:200,dps60:160,dps600:104,dps3600:104,average:104,combatAverage:162.5}};
test('rows put allies by damage first, then the allies total, then the character',()=>{
 assert.deepEqual(D.rows(state).map(r=>r.label),['Giant T_Rex Minion','Summoned Skeleton ×10','Allies total','You (character)']);
});
test('table escapes names and compact mode keeps the key columns',()=>{
 const html=D.table({...state,types:[{...state.types[1],name:'<b>x</b>'}]});
 assert.ok(html.includes('&lt;b&gt;x&lt;/b&gt; ×10'));assert.ok(!html.includes('<b>x'));
 const compact=D.table(state,{compact:true});assert.ok(compact.includes('DPS 60 s'));assert.ok(!compact.includes('Biggest hit'));
 assert.ok(D.table(null).includes('No damage recorded'));
});
test('status and csv describe the session for analysis',()=>{
 assert.equal(D.status(state),'Measuring · 2:05 session · 1:20 in combat · 42 hits');
 assert.equal(D.status(null),'Not started.');
 const csv=D.csv(state).trim().split('\n');
 assert.match(csv[0],/^# EpochPact damage session/);
 assert.equal(csv[1],'Source,DPS 10 s,DPS 60 s,DPS 10 min,DPS 60 min,Avg in combat,Avg session,Total damage,Hits,Biggest hit,Physical damage,Fire damage,Cold damage,Lightning damage,Necrotic damage,Void damage,Poison damage');
 assert.equal(csv[2],'Giant T_Rex Minion,120.0,100.0,64.0,64.0,100.0,64.0,8000.0,12.0,900.0,,,,,,,','a session without damage types leaves those columns empty');
 assert.equal(csv.length,6);
});
// Each row also carries its damage per type (the native session splits each hit).
const el=(total,extra={})=>({total,hits:total.map(v=>v?1:0),maxHit:total,dps10:total.map(v=>v/10),dps60:total.map(v=>v/60),dps600:total.map(v=>v/600),dps3600:total.map(v=>v/3600),...extra});
const typed={...state,types:[{...state.types[0],elements:el([900,0,0,0,0,0,0])},{...state.types[1],elements:el([1000,0,0,0,4000,0,0])},{...state.types[2],elements:el([0,6000,0,2000,0,0,0])}],
 allies:{...state.allies,elements:el([1000,6000,0,2000,4000,0,0])}};
test('one damage type shows that part of every row, with averages from the session time',()=>{
 const fire=D.rows(typed,{element:1});
 assert.deepEqual(fire.map(r=>[r.label,r.total]),[['Giant T_Rex Minion',6000],['Summoned Skeleton ×10',0],['Allies total',6000],['You (character)',0]]);
 const necrotic=D.rows(typed,{element:'4'});
 assert.equal(necrotic[0].label,'Summoned Skeleton ×10','allies are ordered by the chosen type');
 assert.equal(necrotic[0].dps60,4000/60);assert.equal(necrotic[0].average,4000/125);assert.equal(necrotic[0].combatAverage,4000/80);
 assert.ok(D.table(typed,{element:3}).includes('<th>Source · Lightning</th>'));
 for(const bad of [-1,7,'x',null,undefined,''])assert.equal(D.rows(typed,{element:bad})[0].total,8000,'anything else is all damage');
 const csv=D.csv(typed).trim().split('\n');
 assert.equal(csv[2],'Giant T_Rex Minion,120.0,100.0,64.0,64.0,100.0,64.0,8000.0,12.0,900.0,0.0,6000.0,0.0,2000.0,0.0,0.0,0.0');
});
test('rows can be hidden by ally type, the allies total or the character',()=>{
 assert.deepEqual(D.sources(typed).map(r=>r.key),['Giant T_Rex Minion','SummonedSkeleton','__total','You']);
 assert.deepEqual(D.sources(typed).map(r=>r.label),['Giant T_Rex Minion','Summoned Skeleton','Allies total','You (character)']);
 assert.deepEqual(D.rows(typed,{hidden:['SummonedSkeleton','__total']}).map(r=>r.label),['Giant T_Rex Minion','You (character)']);
 const none=D.table(typed,{hidden:D.sources(typed).map(r=>r.key)});
 assert.match(none,/Every row is hidden/);assert.match(D.table(null,{hidden:['You']}),/No damage recorded/);
});
test('an ally type shows the game\'s own name; hiding and the CSV keep working by type',()=>{
 const named={...state,types:[state.types[0],{...state.types[1],label:'Skeleton Warrior'},{...state.types[2],label:'  '}]};
 assert.deepEqual(D.rows(named).map(r=>r.label),['Giant T_Rex Minion','Skeleton Warrior ×10','Allies total','You (character)'],'a blank name falls back to the object name');
 assert.deepEqual(D.sources(named).map(r=>[r.key,r.label]),[['Giant T_Rex Minion','Giant T_Rex Minion'],['SummonedSkeleton','Skeleton Warrior'],['__total','Allies total'],['You','You (character)']]);
 assert.deepEqual(D.rows(named,{hidden:['SummonedSkeleton']}).map(r=>r.key),['Giant T_Rex Minion','__total','You'],'hidden rows stay keyed by the object name');
 assert.match(D.csv(named),/\nSkeleton Warrior ×10,/);
 assert.ok(D.table(named).includes('<td data-no-translate>Skeleton Warrior ×10</td>'),'game names are not translated');
 assert.ok(D.table(named).includes('<td>You (character)</td>'));
 for(const [name,readable] of [['SummonedSkeletonArcher','Summoned Skeleton Archer'],['RogueBallista','Rogue Ballista'],['Bone_Golem','Bone Golem'],['AbominationXL','Abomination XL']])
  assert.equal(D.allyName({name}),readable);
});
