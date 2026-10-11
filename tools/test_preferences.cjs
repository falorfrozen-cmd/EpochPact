const test=require('node:test'),assert=require('node:assert/strict');
const P=require('../ui/preferences.js');
test('hidden sections are reversible and navigation recovery stays visible',()=>{
 let p=P.toggle(P.defaults(),'hidden','loot',true);p=P.toggle(p,'hidden','settings',true);
 assert.deepEqual(P.visiblePages(['overview','loot','settings'],p,'overview'),['overview','settings']);
 assert.deepEqual(P.visiblePages(['overview','loot','settings'],p,'loot'),['overview','loot','settings']);
 p=P.toggle(p,'hidden','loot',false);assert.equal(P.visiblePages(['overview','loot','settings'],p,'overview').length,3);
});
test('favorites reorder stably without duplicating sections',()=>{
 let p=P.toggle(P.defaults(),'favorites','loot',true);p=P.toggle(p,'favorites','loot',true);
 assert.deepEqual(P.visiblePages(['overview','loot','settings'],p,'overview'),['loot','overview','settings']);assert.equal(p.favorites.length,1);
});
test('column and stat choices survive restart without sending game actions',()=>{
 const values=new Map(),storage={getItem:k=>values.get(k),setItem:(k,v)=>values.set(k,v)};
 let p=P.toggle(P.defaults(),'summonColumns','Cold',true);p=P.toggle(p,'hiddenStats','sheet:9:0:0:0',true);
 assert.ok(P.save(storage,p));assert.deepEqual(P.load(storage),p);
 assert.deepEqual(P.load({getItem(){throw Error('denied');}}),P.defaults());
 assert.equal(P.save({setItem(){throw Error('denied');}},p),false);
 assert.deepEqual(P.load({getItem:()=>'{invalid'}),P.defaults());
});
test('invalid and oversized local preferences are bounded',()=>{
 const p=P.sanitize({hidden:['settings','overview','loot',4,'loot'],favorites:Array(2000).fill('a'),hiddenStats:'all'});
 assert.deepEqual(p.hidden,['loot']);assert.deepEqual(p.favorites,['a']);assert.deepEqual(p.hiddenStats,[]);
});
