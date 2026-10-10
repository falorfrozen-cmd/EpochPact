/* Catalog-driven presentation contracts. No browser or game access. */
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const model=require('../ui/stat-model.js');
const stats=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8')).characterStats;
const alias=name=>stats.aliases.find(a=>a.name===name);
const guile=stats.sheetRows.find(r=>r.objectName==='GuileStat');

test('flat Guile and increased Guile use different units without altering the shared key',()=>{
 assert.equal(model.name(guile,'sheet'),'Guile');
 assert.equal(model.unit(stats,guile.key,'added').scale,1);
 assert.equal(model.unit(stats,guile.key,'increased').scale,100);
 assert.equal(model.preferredMode(stats,guile,guile.key,'sheet'),'added');
 assert.equal(model.keyId(guile.key),alias('dexterity').sharedKey);
});
test('direct fraction stats show percentage points; Increased and More show percentages',()=>{
 const allres=alias('allres');
 assert.deepEqual(model.unit(stats,allres.key,allres.mode,allres),{unit:'percent',scale:100,suffix:' pp',points:true});
 assert.equal(65/model.unit(stats,allres.key,'added').scale,0.65);
 for(const mode of ['increased','more']){
  const info=model.unit(stats,guile.key,mode);
  assert.equal(info.suffix,'%');assert.equal(info.points,false);assert.equal(25/info.scale,0.25);
 }
});
test('fraction classification matches exact full keys and never guesses from a chance name',()=>{
 const fire=alias('fire');
 assert.equal(model.unit(stats,fire.key,'added').unit,'percent');
 assert.equal(model.unit(stats,{...fire.key,tags:8192},'added').unit,'raw');
 assert.equal(model.unit(stats,{sp:999,tags:0,special:0,extra:0,property:'Chance'},'added').unit,'raw');
 for(const a of stats.aliases){
  assert.equal(model.unit(stats,a.key,a.mode,a).unit,a.unit==='fraction'?'percent':'raw',a.name);
  assert.equal(model.preferredMode(stats,a,a.key,'aliases'),a.mode,a.name);
 }
});
test('all catalog rows remain named, categorized and addressable, including secondary keys',()=>{
 for(const [tab,rows] of [['sheet',stats.sheetRows],['aliases',stats.aliases],['raw',stats.properties]]){
  for(const row of rows){
   assert.ok(model.name(row,tab).trim());assert.ok(model.category(row,tab));
   assert.ok(model.favoriteId(row,tab).startsWith(tab+':'));
   const key=tab==='raw'?{sp:row.id,tags:0,special:0,extra:0,property:row.name}:row.key;
   for(const mode of tab==='aliases'?[row.mode]:row.modes||['added','increased','more']){
    const info=model.unit(stats,key,mode,tab==='aliases'?row:null);
    const bounds=model.bounds(stats,mode,info,key);
    assert.equal(bounds.minimum,stats.modeLimits[mode].minimum*info.scale);
    assert.equal(bounds.maximum,stats.modeLimits[mode].maximum*info.scale);
    assert.ok(bounds.sliderMin>=bounds.minimum&&bounds.sliderMax<=bounds.maximum);
   }
   if(row.modifierKey)assert.ok(model.keyId(row.modifierKey));
  }
 }
});
test('slider uses a useful range while manual entry keeps catalog bounds and negative flat bonuses',()=>{
 const flat=model.bounds(stats,'added',model.unit(stats,guile.key,'added'),guile.key);
 assert.equal(flat.sliderMin,0);assert.equal(flat.sliderMax,200);
 assert.ok(flat.minimum<-1000&&flat.maximum>5000);
 const pct=model.bounds(stats,'increased',model.unit(stats,guile.key,'increased'),guile.key);
 assert.equal(pct.minimum,-100);assert.equal(pct.sliderMin,-100);assert.equal(pct.sliderMax,100);
 assert.equal(model.bounds(stats,'added',model.unit(stats,alias('health').key,'added'),alias('health').key).sliderMax,2000);
});
test('favorite identities preserve tabs and separate linked sheet rows',()=>{
 const dex=stats.sheetRows.find(r=>r.objectName==='DexterityStat');
 assert.notEqual(model.favoriteId(guile,'sheet'),model.favoriteId(dex,'sheet'));
 assert.notEqual(model.favoriteId(dex,'sheet'),model.favoriteId(alias('dexterity'),'aliases'));
});
test('live reads parse all modes, detached entries and scientific notation',()=>{
 assert.deepEqual(model.contributions('statraw: applicableMore=1; EpochPact added=10 increased=0.25 more=-5e-2 attached=0'),{modes:{added:10,increased:0.25,more:-0.05},attached:false});
 assert.deepEqual(model.contributions('statraw: base=23 applicableMore=1'),{modes:{added:0,increased:0,more:0},attached:true});
 assert.deepEqual(model.contributions('SP=22 (Dexterity) tags=0 special=0 extra=0: exact-key sum added=34 increased=0 moreMultiplier=1 entries=2; applicableAdded=34 applicableIncreased=0 applicableMore=1'),{modes:{added:0,increased:0,more:0},attached:true});
 assert.equal(model.contributions('statraw: error: unavailable'),null);
 assert.equal(model.contributions('unrelated reply'),null);
 assert.equal(model.contributions('SP=22 (Dexterity) tags=0 special=0 extra=0: exact-key sum applicableMore=1; EpochPact added=nan increased=0 more=0 attached=1'),null);
});

test('reopening a stat selects its applied percentage without hiding it behind a zero flat bonus',()=>{
 const current={modes:{added:0,increased:1,more:0},attached:true};
 assert.equal(model.appliedMode(current,['added','increased','more'],'added'),'increased');
 assert.equal(model.bonusSummary(stats,guile.key,current),'+100% increased');
 assert.equal(model.appliedMode(current,['added'],'added'),'added');
 assert.equal(model.bonusSummary(stats,guile.key,{modes:{added:0,increased:0,more:0}}),'No bonus');
});

test('current summary includes mixed contributions in their exact units independent of the editor tab',()=>{
 const current={modes:{added:12,increased:0.25,more:-0.05},attached:true};
 assert.equal(model.bonusSummary(stats,guile.key,current),'+12 · +25% increased · -5% more');
 assert.equal(model.appliedMode(current,['added','increased','more'],'increased'),'increased');
 assert.equal(model.bonusSummary(stats,alias('allres').key,{modes:{added:0.1,increased:0.25,more:0}}),'+10 pp · +25% increased');
});
