/* Build the native browser from the desktop's actual presentation model. No game access. */
'use strict';
const fs=require('node:fs');
const ST=require('../ui/stat-model.js');
const catalog=JSON.parse(fs.readFileSync(require('node:path').join(__dirname,'../ui/catalog.json'),'utf8'));
const stats=catalog.characterStats;
function entry(row,tab,index){
 const key=tab==='raw'?{sp:row.id,tags:0,special:0,extra:0,property:row.name}:row.key;
 function editor(key,secondary){
  const modes=key.sp===46?['added']:tab==='aliases'?[row.mode]:row.modes||['added','increased','more'];
  const preferred=ST.preferredMode(stats,row,key,tab);
  return {key,modes,preferred:modes.includes(preferred)?preferred:modes[0],secondary,
   units:Object.fromEntries(modes.map(mode=>{const info=ST.unit(stats,key,mode,tab==='aliases'?row:null);return [mode,{...info,...ST.bounds(stats,mode,info,key)}];})),
   summaryUnits:Object.fromEntries(['added','increased','more'].map(mode=>[mode,ST.unit(stats,key,mode)]))};
 }
 const shared=stats.sheetRows.filter(r=>r.sharedKey===ST.keyId(key)).map(r=>ST.name(r,'sheet'));
 return {index,tab,alias:tab==='aliases'?row.name:null,summon:tab==='aliases'&&/^minion|^maxcompanions/.test(row.name),
  name:ST.name(row,tab),category:ST.category(row,tab),favorite:ST.favoriteId(row,tab),
  search:(ST.name(row,tab)+' '+ST.category(row,tab)+' '+JSON.stringify(row)).toLowerCase(),
  derived:!!row.derived,shared,editor:editor(key,false),extra:row.modifierKey?editor(row.modifierKey,true):null};
}
const result=Object.fromEntries([['sheet','sheetRows'],['aliases','aliases'],['raw','properties']].map(([tab,key])=>[tab,stats[key].map((r,i)=>entry(r,tab,i))]));
if(require.main===module)process.stdout.write(JSON.stringify(result));
module.exports={entry,result};
