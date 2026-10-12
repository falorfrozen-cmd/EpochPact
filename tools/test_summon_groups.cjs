const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const Preferences=require('../ui/preferences.js');

// Execute the shipping summon grouping helpers from app.js without a DOM.
function load(){
 const context=vm.createContext({document:{querySelector:()=>({addEventListener(){},classList:{toggle(){},remove(){}}}),querySelectorAll:()=>[],addEventListener(){},documentElement:{dataset:{}}},
  window:{EpochSession:require('../ui/session.js'),EpochStats:{},EpochCrafting:require('../ui/crafting-model.js'),EpochAffixes:require('../ui/affix-picker.js'),addEventListener(){}},location:{hash:''},setTimeout:()=>0,setInterval:()=>0,clearTimeout(){},console});
 const source=fs.readFileSync(path.join(__dirname,'../ui/app.js'),'utf8');
 vm.runInContext(source.replace('void init();',''),context);
 return context;
}
const ally=(name,instance,extra={})=>({name,instance,measuredDps:10,lastDamage:4,health:50,maxHealth:100,armour:20,damageIncreased:10,
 resistances:{Physical:10,Fire:20,Cold:0,Lightning:0,Void:0,Necrotic:0,Poison:0},...extra});

test('allies of one type share a row; different types and variants stay separate',()=>{
 const c=load();
 const rows=c.summonRows([ally('Skeleton(Clone)',1),ally('Skeleton(Clone)',2),ally('Skeleton(Clone)',3),
  ally('Bone Golem(Clone)',4),ally('Blood Golem(Clone)',5),ally('Fire Golem',6),ally('Fire Golem',7),ally('Ice Golem',8)],false);
 const byName=Object.fromEntries(rows.map(r=>[r.label,r]));
 assert.deepEqual(JSON.parse(JSON.stringify(rows.map(r=>r.label))),['Skeleton','Bone Golem','Blood Golem','Fire Golem','Ice Golem']);
 assert.equal(byName.Skeleton.count,3);assert.equal(byName['Fire Golem'].count,2);assert.equal(byName['Ice Golem'].count,1);
 assert.equal(byName.Skeleton.measuredDps,30);assert.equal(byName.Skeleton.health,150);assert.equal(byName.Skeleton.maxHealth,300);
 assert.equal(byName.Skeleton.armour,20);assert.equal(byName.Skeleton.resistances.Fire,20);
});

test('averages ignore missing values and totals ignore non-numbers',()=>{
 const c=load();
 const [row]=c.summonRows([ally('Wraith',1,{armour:10,measuredDps:null}),ally('Wraith',2,{armour:30,measuredDps:6})],false);
 assert.equal(row.armour,20);assert.equal(row.measuredDps,6);assert.equal(row.lastDamage,4);
});

test('individual mode keeps one row per ally',()=>{
 const c=load();
 const rows=c.summonRows([ally('Skeleton',1),ally('Skeleton',2)],true);
 assert.equal(rows.length,2);assert.ok(rows.every(r=>r.count===1));
});

test('group DPS change uses only allies with a saved baseline',()=>{
 const c=load();
 vm.runInContext('summonBaseline={1:4,2:6}',c);
 const [row]=c.summonRows([ally('Skeleton',1,{measuredDps:10}),ally('Skeleton',2,{measuredDps:10}),ally('Skeleton',3,{measuredDps:50})],false);
 assert.equal(c.summonChange(row),10);
 vm.runInContext('summonBaseline={}',c);
 assert.equal(c.summonChange(row),null);
});

test('the separate-allies choice survives a reload',()=>{
 const store=new Map();const storage={getItem:k=>store.get(k)??null,setItem:(k,v)=>store.set(k,v)};
 const saved=Preferences.toggle(Preferences.defaults(),'summonOptions','individual',true);
 Preferences.save(storage,saved);
 assert.deepEqual(Preferences.load(storage).summonOptions,['individual']);
});
