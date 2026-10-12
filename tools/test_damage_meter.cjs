const test=require('node:test'),assert=require('node:assert/strict');
const D=require('../ui/damage-meter.js');
const state={ok:true,active:true,elapsedSeconds:125,combatSeconds:80,hits:42,
 types:[{name:'You',live:1,total:900,hits:10,maxHit:200,dps10:5,dps60:4,dps600:3,dps3600:2,average:7.2,combatAverage:11.25},
  {name:'SummonedSkeleton',live:10,total:5000,hits:20,maxHit:300,dps10:80,dps60:60,dps600:40,dps3600:40,average:40,combatAverage:62.5},
  {name:'Giant T_Rex Minion',live:1,total:8000,hits:12,maxHit:900,dps10:120,dps60:100,dps600:64,dps3600:64,average:64,combatAverage:100}],
 allies:{total:13000,hits:32,maxHit:900,dps10:200,dps60:160,dps600:104,dps3600:104,average:104,combatAverage:162.5}};
test('rows put allies by damage first, then the allies total, then the character',()=>{
 assert.deepEqual(D.rows(state).map(r=>r.label),['Giant T_Rex Minion','SummonedSkeleton ×10','Allies total','You (character)']);
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
 assert.equal(csv[1],'Source,DPS 10 s,DPS 60 s,DPS 10 min,DPS 60 min,Avg in combat,Avg session,Total damage,Hits,Biggest hit');
 assert.equal(csv[2],'Giant T_Rex Minion,120.0,100.0,64.0,64.0,100.0,64.0,8000.0,12.0,900.0');
 assert.equal(csv.length,6);
});
