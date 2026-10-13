const test=require('node:test'),assert=require('node:assert/strict');
const sync=require('../ui/storage-sync.js');

function storage(entries={}){
 const map=new Map(Object.entries(entries));
 return {map,getItem:k=>map.has(k)?map.get(k):null,setItem:(k,v)=>map.set(k,String(v)),get length(){return map.size;},key:i=>[...map.keys()][i]??null};
}

test('the preferences file wins over this browser profile for its keys',()=>{
 const local=storage({'epochpact.theme':'"void-atlas"','other.site':'x'});
 const upload=sync.merge(local,{'epochpact.theme':'"chronoforge"','epochpact.profiles.live':'{"Farm":{"xp":3}}'});
 assert.equal(local.getItem('epochpact.theme'),'"chronoforge"');
 assert.equal(local.getItem('epochpact.profiles.live'),'{"Farm":{"xp":3}}');
 assert.deepEqual(upload,{},'nothing to upload when the file has every key');
 assert.equal(local.getItem('other.site'),'x','foreign keys are untouched');
});

test('keys only this profile has are uploaded (first start after an update)',()=>{
 const local=storage({'epochpact.startup-profile.live':'"__last__"','epochpact.last-settings.live':'{"xp":5}','unrelated':'1'});
 const upload=sync.merge(local,{});
 assert.deepEqual(upload,{'epochpact.startup-profile.live':'"__last__"','epochpact.last-settings.live':'{"xp":5}'});
});

test('preview keys and other sites stay local',()=>{
 assert.equal(sync.mirrored('epochpact.profiles.preview'),false);
 assert.equal(sync.mirrored('epochpact.profiles.live'),true);
 assert.equal(sync.mirrored('elsewhere.key'),false);
 assert.equal(sync.mirrored(7),false);
 const local=storage({'epochpact.last-settings.preview':'{}'});
 assert.deepEqual(sync.merge(local,{'epochpact.x.preview':'1'}),{});
 assert.equal(local.getItem('epochpact.x.preview'),null);
});

test('non-string server values are ignored',()=>{
 const local=storage();
 sync.merge(local,{'epochpact.theme':42,'epochpact.ok':'"y"'});
 assert.equal(local.getItem('epochpact.theme'),null);assert.equal(local.getItem('epochpact.ok'),'"y"');
});
