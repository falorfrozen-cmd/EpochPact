const test=require('node:test'),assert=require('node:assert/strict');
const C=require('../ui/crafting-model.js');
const data={items:[{id:10,name:'Calamity',set:false},{id:11,name:'Isadora',set:true}],affixes:Array.from({length:8},(_,id)=>({id,name:`Affix ${id}`})),modifiers:Array.from({length:65},(_,id)=>({id:`u0:${id}`,name:`Effect ${id}`}))};
test('crafting is split into four task categories',()=>assert.deepEqual(C.sections.map(x=>x.id),['forge','materials','custom','spawn']));
test('search matches every word and supports Unique/Set filtering',()=>{
 assert.deepEqual(C.filtered(data.items,'CAL am'),[data.items[0]]);
 assert.deepEqual(C.filtered(data.items,'','set'),[data.items[1]]);
 assert.deepEqual(C.filtered(data.items,'isadora','unique'),[]);
});
test('a normal recipe uses seven affixes, a mixed recipe leaves its seventh slot free',()=>{
 const d={...C.defaults(),affixes:['0','1','2','3','4','5','6']};
 assert.equal(C.problem(d,data), '');d.modifiers=['u0:0'];assert.match(C.problem(d,data),/at most 6/);
 d.affixes.pop();assert.equal(C.problem(d,data),'');
 assert.deepEqual(C.args(d),{template_id:-1,affixes:'0:7:100,1:7:100,2:7:100,3:7:100,4:7:100,5:7:100',perfect:1,modifiers:'u0:0'},'unchanged affixes: T7 at the item-wide roll');
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

test('each affix has its own tier 1-8 and roll 1-100% or random',()=>{
 const tiers={affixes:[{id:0,name:'A',tiers:8},{id:1,name:'B',tiers:8},{id:2,name:'C',tiers:7}],items:data.items,modifiers:[]};
 const d={...C.defaults(),affixes:['0','1','2'],settings:{0:{tier:8,roll:100},1:{tier:1,roll:'random'},2:{tier:5,roll:37}}};
 assert.equal(C.problem(d,tiers),'');
 assert.equal(C.args(d).affixes,'0:8:100,1:1:r,2:5:37');
 assert.equal(C.summary(d,tiers).affixes,'3 selected affixes · T8, T1, T5');
 assert.equal(C.summary({...C.defaults(),affixes:['0']},tiers).affixes,'1 selected affix · T7');
 assert.match(C.problem({...d,settings:{...d.settings,1:{tier:8,roll:50}}},tiers),/one T8/,'one primordial affix per item');
 assert.match(C.problem({...d,settings:{...d.settings,2:{tier:8,roll:50},0:{tier:7,roll:50}}},tiers),/no T8/,'T8 only where the game has it');
 for(const roll of [0,101,1.5,NaN,'x'])assert.match(C.problem({...d,settings:{...d.settings,2:{tier:5,roll}}},tiers),/roll from 1 to 100/);
 for(const tier of [0,9,2.5])assert.match(C.problem({...d,settings:{...d.settings,2:{tier,roll:50}}},tiers),/tier from T1 to T8/);
 assert.match(C.problem({...C.defaults(),affixes:['0'],settings:{0:{tier:8,roll:50}}},{...tiers,affixes:[{id:0,name:'A'}]}),/no T8/,'an older catalog without tier counts offers T1-T7');
});
test('a new affix starts at T7 with the item-wide maximum or midpoint roll',()=>{
 assert.deepEqual(C.setting(C.defaults(),'4'),{tier:7,roll:100});
 assert.deepEqual(C.setting({...C.defaults(),perfect:'0'},'4'),{tier:7,roll:50});
 assert.equal(C.tiersOf({affixes:[{id:4,tiers:12}]},'4'),8,'never above T8');
});
test('an affix the game converts on the forge item type is named before creating',()=>{
 const conv={items:data.items,modifiers:[],affixes:[{id:88,name:'<b>Throwing Damage</b>',convertTo:30,rollsOn:[2,4,20,22]},{id:30,name:'Physical Damage'},{id:503,name:'Dexterity'}]};
 const d={...C.defaults(),affixes:['503','88']};
 assert.equal(C.convertsTo(conv,'88',{itemType:1}).name,'Physical Damage');
 assert.equal(C.convertsTo(conv,'88',{itemType:2}),null,'a type in rollsOn keeps it');
 assert.equal(C.convertsTo(conv,'503',{itemType:1}),null,'an affix without convertTo');
 assert.equal(C.convertsTo(conv,'88',{}),null,'no forge item type known');
 assert.equal(C.problem(d,conv,{itemType:1,affixes:[]}),'Throwing Damage cannot be on this item type: the game turns it into Physical Damage. Choose another affix.');
 assert.equal(C.problem(d,conv,{itemType:4,affixes:[]}),'');
 assert.equal(C.convertsTo({...conv,affixes:[{id:88,name:'T',convertTo:31,rollsOn:[2]}]},'88',{itemType:0}).name,'affix 31','a target outside the list');
});
