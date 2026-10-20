'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const ST=require('../ui/stat-model.js');
const {result}=require('./ingame_stat_catalog.js');
const stats=require('../ui/catalog.json').characterStats;
test('all native stat rows retain desktop identity, category, key and search data',()=>{
 for(const [tab,source] of [['sheet',stats.sheetRows],['aliases',stats.aliases],['raw',stats.properties]]){
  assert.equal(result[tab].length,source.length);
  source.forEach((row,i)=>{
   const actual=result[tab][i];
   assert.equal(actual.name,ST.name(row,tab));assert.equal(actual.category,ST.category(row,tab));
   assert.equal(actual.favorite,ST.favoriteId(row,tab));assert.equal(actual.index,i);
   assert.equal(actual.tab,tab);assert.equal(actual.alias,tab==='aliases'?row.name:null);
   assert(actual.search.includes(JSON.stringify(row).toLowerCase()));
   assert.deepEqual(actual.editor.key,tab==='raw'?{sp:row.id,tags:0,special:0,extra:0,property:row.name}:row.key);
   assert.equal(!!actual.extra,!!row.modifierKey);
   if(actual.extra)assert.deepEqual(actual.extra.key,row.modifierKey);
  });
 }
});
test('native dialog modes, percentage scales and legal bounds are the desktop values',()=>{
 for(const [tab,source] of [['sheet',stats.sheetRows],['aliases',stats.aliases],['raw',stats.properties]])source.forEach((row,i)=>{
  for(const e of [result[tab][i].editor,result[tab][i].extra].filter(Boolean)){
   assert(e.modes.includes(e.preferred));
   if(e.key.sp===46)assert.deepEqual(e.modes,['added']);
   if(tab==='aliases'&&e.key.sp!==46)assert.deepEqual(e.modes,[row.mode]);
   for(const mode of e.modes){const u=ST.unit(stats,e.key,mode,tab==='aliases'?row:null);
    assert.deepEqual(e.units[mode],{...u,...ST.bounds(stats,mode,u,e.key)});
    assert(Number.isFinite(e.units[mode].minimum)&&Number.isFinite(e.units[mode].maximum));
   }
   for(const mode of ['added','increased','more'])assert.deepEqual(e.summaryUnits[mode],ST.unit(stats,e.key,mode));
  }
 });
});

test('summon shortcuts include only the desktop minion and companion aliases',()=>{
 const expected=stats.aliases.map((r,i)=>({r,i})).filter(({r})=>/^minion|^maxcompanions/.test(r.name));
 assert(expected.length>0);
 assert.deepEqual(result.aliases.filter(r=>r.summon).map(r=>r.index),expected.map(({i})=>i));
 for(const row of result.aliases.filter(r=>r.summon))assert.deepEqual(row.editor.key,stats.aliases[row.index].key);
});
