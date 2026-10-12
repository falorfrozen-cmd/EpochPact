const test=require('node:test'),assert=require('node:assert/strict');
const L=require('../ui/loot-log.js');

test('entries merge once by sequence and keep the newest capacity',()=>{
 let list=L.merge([],[{seq:1,ageSeconds:0,kind:'gold',gold:5}],1000);
 list=L.merge(list,[{seq:1,ageSeconds:9,kind:'gold',gold:5},{seq:2,ageSeconds:0,kind:'item',name:'Ring'}],2000);
 assert.equal(list.length,2);assert.equal(list[0].at,1000,'a re-read entry keeps its first timestamp');
 const many=Array.from({length:L.capacity+20},(_,i)=>({seq:i+1,ageSeconds:0,kind:'gold',gold:1}));
 const capped=L.merge([],many,0);
 assert.equal(capped.length,L.capacity);assert.equal(capped[0].seq,21);assert.equal(L.newestSeq(capped),L.capacity+20);
});

test('rows are newest first and can hide gold or materials',()=>{
 const list=L.merge([],[{seq:1,kind:'gold',gold:3},{seq:2,kind:'material',name:'Shard'},{seq:3,kind:'item',name:'Helm'}],0);
 assert.deepEqual(L.rows(list).map(e=>e.seq),[3,2,1]);
 assert.deepEqual(L.rows(list,{gold:false}).map(e=>e.seq),[3,2]);
 assert.deepEqual(L.rows(list,{materials:false}).map(e=>e.seq),[3,1]);
});

test('rule labels use the game number and a shortened name',()=>{
 assert.equal(L.ruleLabel({kind:'item',ruleNumber:0}),'No matching rule');
 assert.equal(L.ruleLabel({kind:'item',ruleNumber:4,rule:'  Uniques   and  Sets '}),'Rule 4 · Uniques and Sets');
 assert.ok(L.ruleLabel({kind:'item',ruleNumber:1,rule:'x'.repeat(80)}).endsWith('…'));
 assert.equal(L.ruleLabel({kind:'gold',ruleNumber:3}),'');
});

test('item names and rule text are escaped in both views',()=>{
 const list=L.merge([],[{seq:1,kind:'item',name:'<img src=x onerror=1>',rarity:'Unique',ruleNumber:2,rule:'<b>'}],0);
 for(const html of [L.table(list,{now:0}),L.compact(list)]){assert.ok(!html.includes('<img'));assert.ok(!html.includes('<b>'));assert.ok(html.includes('&lt;img'));}
 assert.ok(L.table(list,{now:0}).includes('loot-unique'));
});

test('gold shows its amount and the empty state is explicit',()=>{
 assert.equal(L.itemLabel({kind:'gold',gold:12500}),'+12,500 Gold');
 assert.match(L.table([]),/No pickups yet/);assert.match(L.compact([]),/Waiting for pickups/);
});

test('amber, embers and motes show their quantity without a rule',()=>{
 assert.equal(L.itemLabel({kind:'amber',name:'Memory Amber',gold:10}),'Memory Amber ×10');
 assert.equal(L.ruleLabel({kind:'amber',ruleNumber:0}),'');
});

test('rich-text tags from rule descriptions are removed',()=>{
 assert.equal(L.ruleLabel({kind:'item',ruleNumber:1,rule:'<u><color=#FFAA00FF>SHOW</color></u> <color=#E3D157FF>Rare</color> items'}),'Rule 1 · SHOW Rare items');
});
