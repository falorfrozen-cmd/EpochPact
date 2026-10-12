const test=require('node:test'),assert=require('node:assert/strict'),P=require('../ui/affix-picker.js');
const source=[{id:0,name:'Armor'},{id:1,name:'Fire Damage'},{id:2,name:'Cold Damage'},{id:3,name:'Armor'}];
test('search and both sort orders keep all chosen affixes in the right box',()=>{
 const s=P.create();P.receive(s,[2],'session A');P.move(s,[0],true);s.query='fire';
 assert.deepEqual(P.lists(s,source).available.map(x=>x.id),['1']);assert.deepEqual(P.lists(s,source).selected.map(x=>x.id),['0','2']);
 s.order='za';assert.deepEqual(P.lists(s,source).selected.map(x=>x.id),['2','0']);assert.deepEqual(s.selected,['2','0']);
});
test('refresh and temporary loss of character keep drafts; an actual new game session resets them',()=>{
 const s=P.create();P.receive(s,[0],'PID:created/char');P.move(s,[1],true);P.receive(s,[0],'PID:created/char');P.receive(s,[],null);
 assert.deepEqual(s.selected,['0','1']);assert.equal(s.dirty,true);
 P.receive(s,[2],'PID:newcreated/char');assert.deepEqual(s.selected,['2']);assert.equal(s.dirty,false);
});
test('successful acknowledgement preserves edits made during the request, while reset discards them',()=>{
 const s=P.create();P.receive(s,[0],'A');P.move(s,[1],true);P.move(s,[2],true);P.receive(s,[0,1],'A');
 assert.deepEqual(s.selected,['0','1','2']);assert.equal(s.dirty,true);P.receive(s,[],'A',{force:true});assert.deepEqual(s.selected,[]);assert.equal(s.dirty,false);
});
test('64-choice boundary refuses the entire addition and IDs remain unique',()=>{
 const s=P.create();P.receive(s,[],'A');assert.equal(P.move(s,Array.from({length:64},(_,i)=>i),true),true);
 assert.equal(P.move(s,[64,65],true),false);assert.equal(s.selected.length,64);
 P.move(s,[0],false);assert.equal(P.move(s,[64],true),true);P.move(s,[64],true);assert.equal(s.selected.length,64);
});
test('unknown names stay removable, empty means any T7, and equal labels have deterministic ID order',()=>{
 const s=P.create();P.receive(s,[50],'A');assert.match(P.lists(s,source).selected[0].name,/name unavailable/);
 P.move(s,[50],false);assert.equal(s.selected.length,0);assert.deepEqual(P.lists(s,source).available.slice(0,2).map(x=>x.id),['0','3']);
});
