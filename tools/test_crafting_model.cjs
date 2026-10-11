const test=require('node:test'),assert=require('node:assert/strict');
const C=require('../ui/crafting-model.js');
const data={items:[{id:10,name:'Calamity',set:false},{id:11,name:'Isadora',set:true}],affixes:Array.from({length:8},(_,id)=>({id,name:`Affix ${id}`})),modifiers:Array.from({length:65},(_,id)=>({id:`u0:${id}`,name:`Effect ${id}`}))};
test('crafting is split into three task categories',()=>assert.deepEqual(C.sections.map(x=>x.id),['forge','materials','custom']));
test('search matches every word and supports Unique/Set filtering',()=>{
 assert.deepEqual(C.filtered(data.items,'CAL am'),[data.items[0]]);
 assert.deepEqual(C.filtered(data.items,'','set'),[data.items[1]]);
 assert.deepEqual(C.filtered(data.items,'isadora','unique'),[]);
});
test('a normal recipe uses seven affixes, a mixed recipe leaves its seventh slot free',()=>{
 const d={...C.defaults(),affixes:['0','1','2','3','4','5','6']};
 assert.equal(C.problem(d,data), '');d.modifiers=['u0:0'];assert.match(C.problem(d,data),/at most 6/);
 d.affixes.pop();assert.equal(C.problem(d,data),'');
 assert.deepEqual(C.args(d),{template_id:-1,affixes:'0,1,2,3,4,5',perfect:1,modifiers:'u0:0'});
});
test('keeping current affixes includes the real normal-affix count and excludes an existing mixed recipe',()=>{
 const d={...C.defaults(),modifiers:['u0:0']};
 assert.match(C.problem(d,data,{affixes:data.affixes.slice(0,7)}),/seven affixes/);
 assert.equal(C.problem(d,data,{affixes:[...data.affixes.slice(0,6),{id:4000}]}),'');
 assert.equal(C.args(d).affixes,'keep');
});
test('unknown choices, duplicates, invalid roll modes and more than 64 effects are rejected',()=>{
 for(const draft of [{...C.defaults(),template_id:'999'}, {...C.defaults(),perfect:'2'}, {...C.defaults(),affixes:['999']}, {...C.defaults(),affixes:['0','0']}, {...C.defaults(),modifiers:['x']}, {...C.defaults(),modifiers:['u0:0','u0:0']}, {...C.defaults(),modifiers:data.modifiers.map(x=>x.id)}])assert.notEqual(C.problem(draft,data),'');
 const max={...C.defaults(),modifiers:data.modifiers.slice(0,64).map(x=>x.id)};assert.equal(C.problem(max,data),'');
});
test('an untouched recipe keeps affixes and omits extra effects; summary distinguishes material preservation from stocking',()=>{
 assert.deepEqual(C.args(C.defaults()),{template_id:-1,affixes:'keep',perfect:1,modifiers:'none'});
 assert.equal(C.summary({...C.defaults(),template_id:'11',perfect:'0'},data).template,'Set · Isadora');
 assert.equal(C.summary({...C.defaults(),perfect:'0'},data).rolls,'Midpoint for new effects · current affix rolls kept');
 assert.equal(C.summary({...C.defaults(),affixes:['0'],perfect:'0'},data).rolls,'Midpoint rolls');
 const mixed=C.summary({...C.defaults(),modifiers:['u0:0'],perfect:'0'},data);
 assert.equal(mixed.affixes,'Current affixes rebuilt at T7');assert.equal(mixed.rolls,'Midpoint rolls');assert.equal(mixed.effects,'1 extra effect');
});
