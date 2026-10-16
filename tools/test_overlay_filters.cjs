// The loot feed and damage windows' filters, run against a small fake page.
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=name=>fs.readFileSync(path.join(__dirname,'../ui',name),'utf8');

function element(extra={}){
 const listeners={};
 return {textContent:'',innerHTML:'',value:'',checked:false,disabled:false,dataset:{},listeners,...extra,
  addEventListener(type,fn){(listeners[type]||=[]).push(fn);},
  fire(type,target){for(const fn of listeners[type]||[])fn({target:target||this});}};
}
// One window script with its shared modules, a fake page and a fake EpochPact.
function page(script,{stored={},replies}){
 const elements={},all={},storage={...stored},calls=[],intervals=[];
 const context={console,JSON,Math,Number,String,Array,Object,Map,Set,Promise,Date,
  localStorage:{getItem:k=>k in storage?storage[k]:null,setItem:(k,v)=>{storage[k]=String(v);}},
  setInterval:fn=>{intervals.push(fn);return 0;},setTimeout:(fn)=>{fn();return 0;},
  fetch:async()=>({json:async()=>({session:'test'})}),
  EpochSession:{create:()=>({setSession(){},async run(p){calls.push(p);return replies(p);}})}};
 context.window=context;context.globalThis=context;
 context.document={hidden:false,
  querySelector:s=>elements[s]||(elements[s]=element()),
  querySelectorAll:s=>all[s]||[]};
 vm.createContext(context);
 for(const name of ['loot-log.js','damage-meter.js','damage-styles.js','overlay.js'])vm.runInContext(source(name),context);
 context.EpochOverlay.install=()=>{};
 return {context,elements,all,storage,calls,intervals,run:()=>vm.runInContext(source(script),context)};
}
const settle=()=>new Promise(r=>setImmediate(r));

test('loot feed: the old Gold and Materials switches become categories, saved with the window',async()=>{
 const boxes=['normal','magic','rare','exalted','unique','set','legendary','gold','amber','ember','xp','shard','rune','glyph','material']
  .map(id=>element({dataset:{feedShow:id}}));
 const entries=[{seq:1,kind:'gold',gold:5,name:'Gold'},{seq:2,kind:'material',name:'Strength',type:101,subtype:501},{seq:3,kind:'item',name:'Ring',rarity:'Magic'}];
 const p=page('feed.js',{stored:{'epochpact.loot-feed.v1':JSON.stringify({gold:false,materials:true,opacity:50})},
  replies:x=>x.id==='loot_log'?{ok:true,entries}:{ok:true,id:501,name:'Strength',type:'Prefix',level:0,rollsOn:['Helmet'],tiers:[{tier:1,min:'1',max:'1'}],valuesOn:'Helmet'}});
 p.all['[data-feed-show]']=boxes;
 p.run();await settle();await settle();
 const saved=()=>JSON.parse(p.storage['epochpact.loot-feed.v1']);
 assert.equal(boxes[7].checked,false,'Gold starts off as before');assert.ok(boxes.filter((b,i)=>i!==7).every(b=>b.checked));
 const list=p.elements['#feed-list'].innerHTML;
 assert.ok(!list.includes('Gold'));assert.ok(list.includes('Ring'));
 // The shard's affix is read once, by its ID, and becomes the line's hover text.
 assert.equal(JSON.stringify(p.calls.filter(c=>c.id==='affix_info').map(c=>[c.operation,c.args])),JSON.stringify([['read',{id:501}]]));
 assert.ok(list.includes('title="Strength\nStrength · Prefix'));
 // Switching a category re-renders and is saved without the old keys.
 boxes[1].checked=false;boxes[1].fire('change');
 assert.ok(!p.elements['#feed-list'].innerHTML.includes('Ring'));
 assert.equal(saved().show.magic,false);assert.equal(saved().show.gold,false);assert.ok(!('gold' in saved())&&!('materials' in saved()));
 p.elements['[data-feed-none]'].fire('click');
 assert.ok(boxes.every(b=>!b.checked));assert.match(p.elements['#feed-list'].innerHTML,/No pickups in the categories you show/);
 p.elements['[data-feed-all]'].fire('click');
 assert.ok(boxes.every(b=>b.checked));assert.ok(Object.values(saved().show).every(Boolean));
 assert.ok(p.calls.every(c=>c.operation==='read'),'the feed only reads');
});

test('damage window: one damage type and the chosen rows, remembered with the window',async()=>{
 const el=total=>({total,hits:total.map(v=>v?1:0),maxHit:total,dps10:total,dps60:total,dps600:total,dps3600:total});
 const state={ok:true,active:false,elapsedSeconds:10,combatSeconds:5,hits:3,
  types:[{name:'You',live:1,total:30,hits:1,elements:el([30,0,0,0,0,0,0])},{name:'RogueBallista',live:1,total:70,hits:2,elements:el([20,50,0,0,0,0,0])},{name:'Wolf',live:2,total:10,hits:1,elements:el([10,0,0,0,0,0,0])}],
  allies:{total:80,hits:3,elements:el([30,50,0,0,0,0,0])}};
 const p=page('damage.js',{stored:{'epochpact.damage-window.v1':JSON.stringify({element:9,hidden:'x',style:'table'})},replies:()=>state});
 p.run();await settle();await settle();
 const saved=()=>JSON.parse(p.storage['epochpact.damage-window.v1']||'{}');
 assert.equal(p.elements['#damage-element'].value,'-1','an invalid stored type falls back to all damage');
 const rows=p.elements['#damage-rows'].innerHTML;
 for(const key of ['RogueBallista','Wolf','__total','You'])assert.ok(rows.includes(`data-damage-row="${key}"`));
 p.elements['#damage-element'].value='1';p.elements['#damage-element'].fire('change');
 let table=p.elements['#damage-table'].innerHTML;
 assert.ok(table.includes('Source · Fire'));assert.ok(table.includes('<td>50</td>'));assert.equal(saved().element,1);
 p.elements['#damage-rows'].fire('change',{dataset:{damageRow:'Wolf'},checked:false});
 table=p.elements['#damage-table'].innerHTML;
 assert.ok(!table.includes('Wolf'));assert.equal(JSON.stringify(saved().hidden),'["Wolf"]');
 p.elements['#damage-rows'].fire('change',{dataset:{damageRow:'Wolf'},checked:true});
 assert.ok(p.elements['#damage-table'].innerHTML.includes('Wolf ×2'));assert.equal(JSON.stringify(saved().hidden),'[]');
 assert.ok(p.calls.every(c=>c.id==='damage_session_read'),'filters never send a session action');
});
test('loot feed: an affix that cannot be read is tried at most three times',async()=>{
 const p=page('feed.js',{replies:x=>x.id==='loot_log'?{ok:true,entries:[{seq:1,kind:'material',name:'Odd shard',type:101,subtype:9}]}:{ok:false,error:'affix is not in this game\'s catalog'}});
 p.run();await settle();await settle();
 for(let i=0;i<5;i++){p.intervals[0]();await settle();await settle();}
 assert.equal(p.calls.filter(c=>c.id==='affix_info').length,3);
 assert.ok(!p.elements['#feed-list'].innerHTML.includes('title="Odd shard\n'),'no description without a read');
});

test('damage window: a session started in the main window shows up without reopening the window',async()=>{
 let clock=1000,active=false;
 const stopped={ok:true,active:false,elapsedSeconds:0,combatSeconds:0,hits:0,types:[],allies:{total:0,hits:0}};
 const running=()=>({ok:true,active:true,elapsedSeconds:12,combatSeconds:8,hits:5,types:[{name:'You',live:1,total:1432,hits:5,dps10:120,dps60:23.5,combatAverage:179,elements:{total:[0,625,0,807,0,0,0]}}],allies:{total:0,hits:0}});
 const p=page('damage.js',{stored:{'epochpact.damage-window.v1':JSON.stringify({style:'table'})},replies:x=>x.id==='damage_session_read'?(active?running():stopped):{ok:false}});
 p.context.Date={now:()=>clock};
 p.run();await settle();await settle();
 assert.match(p.elements['#damage-table'].innerHTML,/No damage recorded/);
 const tick=async()=>{for(const fn of p.intervals)fn();await settle();await settle();};
 const reads=()=>p.calls.filter(c=>c.id==='damage_session_read').length;
 // Started elsewhere: within a few seconds the window reads it, without any click here.
 active=true;
 const before=reads();clock+=1000;await tick();
 assert.equal(reads(),before,'a stopped session is not read every second');
 clock+=2500;await tick();
 assert.ok(reads()>before,'but it is read again after a few seconds');
 assert.ok(p.elements['#damage-table'].innerHTML.includes('You (character)'));
 assert.match(p.elements['#damage-status'].textContent,/Measuring/);
 // Now running: read every second.
 const running1=reads();clock+=1000;await tick();
 assert.ok(reads()>running1,'a running session is read every second');
 assert.ok(p.calls.every(c=>c.id==='damage_session_read'),'reading never starts or stops the session');
});

test('damage window: Quiet HUD by default, the style and averaging window are saved, the total sits outside the list',async()=>{
 const state={ok:true,active:true,elapsedSeconds:95,combatSeconds:71,hits:3,unsplitHits:0,hooked:true,
  types:[{name:'You',live:1,total:900,dps10:702.4,dps60:611,elements:{total:[0,900,0,0,0,0,0],dps10:[0,702.4,0,0,0,0,0],dps60:[0,611,0,0,0,0,0]}},
   {name:'Skeleton',live:4,total:500,dps10:260,dps60:241.5,elements:{total:[500,0,0,0,0,0,0],dps10:[260,0,0,0,0,0,0],dps60:[241.5,0,0,0,0,0,0]}},
   {name:'Wolf',live:1,total:50,dps10:20,dps60:10,elements:{total:[50,0,0,0,0,0,0],dps10:[20,0,0,0,0,0,0],dps60:[10,0,0,0,0,0,0]}}],
  allies:{total:550,dps10:280,dps60:251.5,elements:{total:[550,0,0,0,0,0,0],dps10:[280,0,0,0,0,0,0],dps60:[251.5,0,0,0,0,0,0]}}};
 // An older window's preferences: no style yet.
 const p=page('damage.js',{stored:{'epochpact.damage-window.v1':JSON.stringify({compact:false,element:-1,hidden:['__total'],fontSize:16})},replies:()=>state});
 p.run();await settle();await settle();
 const saved=()=>JSON.parse(p.storage['epochpact.damage-window.v1']||'{}');
 assert.match(p.elements['#damage-table'].innerHTML,/dm-quiet/);
 assert.match(p.elements['#damage-total'].innerHTML,/TOTAL DPS/);assert.equal(p.elements['#damage-total'].hidden,false);
 assert.equal(p.elements['#damage-style'].value,'quiet');assert.equal(p.elements['#damage-metric'].value,'dps10');
 assert.equal(p.elements['#damage-metric'].hidden,false);assert.equal(p.elements['#damage-compact-row'].hidden,true);
 assert.equal(p.elements['#damage-status'].hidden,true,'the status line is only for problems in the compact styles');
 assert.ok(!p.elements['#damage-rows'].innerHTML.includes('__total'),'the allies total row choice is a table option');
 p.elements['#damage-metric'].value='dps60';p.elements['#damage-metric'].fire('change');
 p.elements['#damage-style'].value='bars';p.elements['#damage-style'].fire('change');
 assert.match(p.elements['#damage-table'].innerHTML,/dm-bars/);
 assert.equal(saved().style,'bars');assert.equal(saved().metric,'dps60');assert.equal(saved().compact,false);assert.equal(saved().fontSize,16);
 p.elements['#damage-style'].value='table';p.elements['#damage-style'].fire('change');
 assert.match(p.elements['#damage-table'].innerHTML,/damage-table/);assert.equal(p.elements['#damage-total'].hidden,true);
 assert.equal(p.elements['#damage-metric'].hidden,true);assert.equal(p.elements['#damage-status'].hidden,false);
 assert.ok(p.elements['#damage-rows'].innerHTML.includes('__total'));
 assert.ok(p.calls.every(c=>c.id==='damage_session_read'),'changing the view never sends a session action');
});

test('damage window: each source icon is read once from the game, a missing one at most three times',async()=>{
 const state={ok:true,active:true,elapsedSeconds:10,combatSeconds:10,hits:1,
  types:[{name:'You',icon:'c:1:2',live:1,dps10:5,elements:{dps10:[5,0,0,0,0,0,0]}},{name:'Wolf',icon:'a:wo3',live:1,dps10:2,elements:{dps10:[2,0,0,0,0,0,0]}},{name:'Imp',icon:'',live:1,dps10:1,elements:{dps10:[1,0,0,0,0,0,0]}}],allies:{dps10:3}};
 const p=page('damage.js',{stored:{'epochpact.damage-window.v1':JSON.stringify({style:'bars'})},replies:x=>x.id==='damage_icon'?(x.args.key==='c:1:2'?{ok:true,key:'c:1:2',png:'iVBORw0KGgo='}:{ok:false,error:'not yet'}):state});
 p.run();await settle();await settle();
 const tick=async()=>{for(const fn of p.intervals)fn();await settle();await settle();await settle();};
 for(let i=0;i<8;i++)await tick();
 const icons=p.calls.filter(c=>c.id==='damage_icon');
 assert.deepEqual(icons.map(c=>[c.operation,c.args.key]),[['read','c:1:2'],['read','a:wo3'],['read','a:wo3'],['read','a:wo3']]);
 assert.ok(p.elements['#damage-table'].innerHTML.includes('src="data:image/png;base64,iVBORw0KGgo="'));
 assert.ok(p.calls.every(c=>c.operation==='read'),'icons and the session are only read');
});
