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

test('rule labels show the game number, what the rule does and its full name',()=>{
 assert.equal(L.ruleLabel({kind:'item',ruleNumber:0}),'No matching rule');
 assert.equal(L.ruleLabel({kind:'item',ruleNumber:0,outcome:-1}),'');
 assert.equal(L.ruleLabel({kind:'item',ruleNumber:4,rule:'  Uniques   and  Sets '}),'Rule 4 · Uniques and Sets');
 assert.equal(L.ruleLabel({kind:'item',ruleNumber:4,outcome:2,rule:'Two T6 and a T7'}),'Rule 4 · Highlight · Two T6 and a T7');
 assert.equal(L.ruleLabel({kind:'item',ruleNumber:1,rule:'x'.repeat(80)}).length,'Rule 1 · '.length+80);
 assert.equal(L.ruleLabel({kind:'gold',ruleNumber:3}),'');
 assert.equal(L.tierLabel({tiers:'7 6 6'}),'T7 T6 T6');
});
test('identical resources picked up one after another become one line',()=>{
 const e=(seq,kind,name,gold=0,extra={})=>({seq,kind,name,gold,at:seq,...extra});
 const log=[e(1,'gold','',149),e(2,'gold','',558),e(3,'item','Noble Gloves',0,{rarity:'Magic',ruleNumber:59,outcome:1,rule:'Hide magic',tiers:'3 2'}),
  e(4,'gold','',72),e(5,'gold','',19),e(6,'material','Shard of Health'),e(7,'material','Shard of Health'),e(8,'material','Shard of Armour'),e(9,'material','Shard of Armour'),e(10,'material','Shard of Health')];
 const rows=L.rows(log);
 assert.deepEqual(rows.map(r=>L.itemLabel(r)),['Shard of Health','Shard of Armour ×2','Shard of Health ×2','+91 Gold','Noble Gloves','+707 Gold']);
 assert.deepEqual(rows.map(r=>r.count),[1,2,2,2,1,2]);
 const html=L.compact(log);
 assert.ok(html.includes('+707 Gold <small class="muted">(2 pickups)</small>'));
 assert.ok(html.includes('Rule 59 · Hide · Hide magic'));assert.ok(html.includes('T3 T2'));
 assert.equal(L.rows(log,{group:false}).length,10);
 assert.equal(L.totalGold(log),798);
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

// Entries as the game's loot log reports them (live test, 0.1.2-dev.28 work).
const pickups=[
 {seq:1,kind:'item',name:'Refuge Helmet',rarity:'Normal',type:0,subtype:0},
 {seq:2,kind:'item',name:'Refuge Helmet',rarity:'Magic',type:0,subtype:0},
 {seq:3,kind:'item',name:'Fractured Crown',rarity:'Unique',type:0,subtype:22},
 {seq:4,kind:'material',name:'Strength',rarity:'Material',type:101,subtype:501},
 {seq:5,kind:'material',name:'Rune of Shattering',rarity:'Material',type:102,subtype:0},
 {seq:6,kind:'material',name:'Glyph of Hope',rarity:'Material',type:103,subtype:0},
 {seq:7,kind:'material',name:'Arena Key',rarity:'Material',type:104,subtype:0},
 {seq:8,kind:'gold',name:'Gold',rarity:'Gold',gold:25,type:-1,subtype:-1},
 {seq:9,kind:'amber',name:'Memory Amber',rarity:'Amber',gold:10},
 {seq:10,kind:'amber',name:'Soul Ember',rarity:'Amber',gold:3},
 {seq:11,kind:'amber',name:'XP mote',rarity:'Amber',gold:1},
 {seq:12,kind:'item',name:'Old entry',rarity:'Experimental'}];
test('every pickup has one feed category: rarity, resource or material type',()=>{
 assert.deepEqual(pickups.map(L.category),['normal','magic','unique','shard','rune','glyph','material','gold','amber','ember','xp','normal']);
 assert.deepEqual(L.categories.map(c=>c[0]),['normal','magic','rare','exalted','unique','set','legendary','gold','amber','ember','xp','shard','rune','glyph','material']);
 for(const r of ['Rare','Exalted','Set','Legendary'])assert.equal(L.category({kind:'item',rarity:r}),r.toLowerCase());
});
test('categories can be hidden and the old Gold and Materials switches carry over',()=>{
 const shownSeqs=options=>L.rows(pickups,{...options,group:false}).map(e=>e.seq).reverse();
 assert.deepEqual(shownSeqs({}),pickups.map(e=>e.seq));
 assert.deepEqual(shownSeqs({show:{magic:false,unique:false,ember:false}}),[1,4,5,6,7,8,9,11,12]);
 assert.deepEqual(shownSeqs({materials:false}),[1,2,3,8,9,10,11,12]);
 assert.deepEqual(shownSeqs({gold:false,show:{gold:true}}),pickups.map(e=>e.seq),'a new choice wins over the old switch');
 const migrated=L.shown({gold:false,materials:false});
 assert.equal(migrated.gold,false);assert.equal(migrated.shard,false);assert.equal(migrated.material,false);assert.equal(migrated.amber,true);
 const none=Object.fromEntries(L.categories.map(([id])=>[id,false]));
 assert.match(L.compact(pickups,{show:none}),/No pickups in the categories you show/);
 assert.match(L.compact([],{show:none}),/Waiting for pickups/);
});
test('an affix shard shows what its affix does when pointed at',()=>{
 assert.deepEqual(L.shardIds([...pickups,{seq:13,kind:'material',type:101,subtype:501},{seq:14,kind:'material',type:101,subtype:-1}]),[501]);
 const info={ok:true,id:501,name:'Strength',type:'Prefix',level:0,rollsOn:['Helmet','Body Armor','Ring'],
  tiers:[{tier:1,min:'1',max:'1',min2:'',max2:''},{tier:7,min:'14',max:'16',min2:'',max2:''},{tier:8,min:'24',max:'28',min2:'',max2:''}],valuesOn:'Helmet'};
 assert.equal(L.affixText(info),'Strength · Prefix\nRolls on: Helmet, Body Armor, Ring\nT1  1\nT7  14 – 16\nT8  24 – 28\n(Values for Helmet)');
 assert.equal(L.affixText({ok:true,name:'Mana and Regen',type:'Prefix',level:20,rollsOn:['Helmet'],tiers:[{tier:1,min:'3%',max:'3%',min2:'10%',max2:'13%'}],valuesOn:'Helmet'}),
  'Mana and Regen · Prefix · Level 20\nRolls on: Helmet\nT1  3% / 10% – 13%');
 assert.equal(L.affixText({ok:false,error:'x'}),'');assert.equal(L.affixText(undefined),'');
 const affixes=new Map([[501,info]]);
 const feed=L.compact(pickups,{affixes});
 assert.ok(feed.includes('title="Strength\nStrength · Prefix\nRolls on: Helmet, Body Armor, Ring'),'the feed line title carries the description');
 const table=L.table(pickups,{affixes,now:0});
 assert.ok(table.includes('class="loot-material" title="Strength · Prefix'),'the table pickup cell carries it too');
 assert.equal((table.match(/title=/g)||[]).length,1,'only shards get a description');
 const hostile={...info,name:'<img src=x>',rollsOn:['"><b>']};
 const html=L.compact(pickups,{affixes:{501:hostile}});
 assert.ok(!html.includes('<img'));assert.ok(!html.includes('"><b>'));
});
