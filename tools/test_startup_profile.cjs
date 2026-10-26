const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');

// Run the shipping startup-profile functions with a stored-preferences fixture.
function load(options={}){
 const store=new Map();
 const localStorage={getItem:k=>store.has(k)?store.get(k):null,setItem:(k,v)=>store.set(k,String(v)),removeItem:k=>store.delete(k)};
 const context=vm.createContext({localStorage,document:{querySelector:()=>({addEventListener(){},classList:{toggle(){},remove(){}}}),querySelectorAll:()=>[],addEventListener(){},documentElement:{dataset:{}}},
  window:{EpochSession:require('../ui/session.js'),EpochStats:{},EpochCrafting:require('../ui/crafting-model.js'),EpochAffixes:require('../ui/affix-picker.js'),addEventListener(){}},
  location:{hash:''},setTimeout:fn=>{fn();return 0;},setInterval:()=>0,clearTimeout(){},console});
 const source=fs.readFileSync(path.join(__dirname,'../ui/app.js'),'utf8');
 vm.runInContext(source.replace('void init();',''),context);
 vm.runInContext(`
  catalog={controls:[
   {id:'xp',group:'general',lifetime:'session',widget:'number',default:1,requirements:['offline'],parameters:[]},
   {id:'speed',group:'general',lifetime:'actor',widget:'number',default:1,requirements:['offline'],parameters:[]},
   {id:'campaign_complete',group:'campaign',lifetime:'game_save',widget:'button',default:null,requirements:['offline'],parameters:[]},
   {id:'loot_mode',group:'loot',lifetime:'session',widget:'selector',default:'all',requirements:['offline'],parameters:[{name:'mode'}]},
   {id:'loot_category',group:'loot',lifetime:'session',widget:'selector',default:null,requirements:['offline'],parameters:[{name:'category'},{name:'value'}]},
   {id:'font_family',group:'display',lifetime:'session',widget:'selector',default:'game',requirements:['offline'],parameters:[{name:'family'}]},
   {id:'loot_reset',group:'loot',lifetime:'session',widget:'button',default:null,requirements:['offline'],parameters:[]}]};
  profileKey='epochpact.profiles.live';preview=false;
  connected={ok:true,connected:true,offline:true,state:'InGame',transitioning:false,phase:'ready',sessionKey:'10:20',player:{id:'7',name:'Hero'}};
  appliedValues={xp:1,speed:1};values={xp:1,speed:1};
  var notes=[],applied=[];notify=(m,bad)=>notes.push(bad?'!'+m:m);`+(options.realDraft?'':'applyDraft=async()=>{applied.push([...dirty].sort());};'),context);
 return {context,store,run:s=>vm.runInContext(s,context)};
}

test('nothing is applied while the startup profile is turned off',()=>{
 const t=load();
 t.store.set('epochpact.startup-profile.live',JSON.stringify('__off__'));
 t.store.set('epochpact.last-settings.live',JSON.stringify({xp:5}));
 t.run('applyStartupProfile()');
 assert.equal(t.run('applied.length'),0);assert.equal(t.run('dirty.size'),0);
});

test('last used settings come back by default, also for the old empty choice',()=>{
 for(const stored of [undefined,'']){
  const t=load();
  if(stored!==undefined)t.store.set('epochpact.startup-profile.live',JSON.stringify(stored));
  t.store.set('epochpact.last-settings.live',JSON.stringify({xp:5}));
  t.run('applyStartupProfile()');
  assert.deepEqual(JSON.parse(t.run('JSON.stringify(applied)')),[['xp']]);
  assert.equal(t.run('startupChoice()'),'__last__');
 }
});

test('choice settings are recorded with their exact arguments, and cleared by a reset',()=>{
 const t=load();
 t.run("rememberReplay({type:'control',id:'loot_mode',args:{expected_session:'10:20',mode:'filter'}})");
 t.run("rememberReplay({type:'control',id:'loot_category',args:{category:'gold',value:0}})");
 t.run("rememberReplay({type:'control',id:'font_family',args:{family:'Segoe%20UI'}})");
 t.run("rememberReplay({type:'control',id:'xp',args:{value:3}})");
 t.run("rememberReplay({type:'control',id:'loot_mode',operation:'read'})");
 let replay=JSON.parse(t.store.get('epochpact.last-settings-replay.live'));
 assert.deepEqual(replay,{loot_mode:{id:'loot_mode',args:{mode:'filter'}},'loot_category:gold':{id:'loot_category',args:{category:'gold',value:0}},font_family:{id:'font_family',args:{family:'Segoe%20UI'}}});
 t.run("rememberReplay({type:'control',id:'font_family',operation:'reset'})");
 t.run("rememberReplay({type:'control',id:'loot_reset'})");
 replay=JSON.parse(t.store.get('epochpact.last-settings-replay.live'));
 assert.deepEqual(replay,{},'a reset forgets the choice and Loot reset forgets every loot choice');
});

test('recorded choices are replayed after the last used numbers, once per session',async()=>{
 const t=load();
 t.store.set('epochpact.last-settings.live',JSON.stringify({xp:5}));
 t.store.set('epochpact.last-settings-replay.live',JSON.stringify({loot_mode:{id:'loot_mode',args:{mode:'quality'}},unknown:{id:'gone',args:{}}}));
 t.run('var tasks=[];runTask=async p=>{tasks.push([applied.length,p]);return {ok:true};}');
 t.run('applyStartupProfile()');
 await new Promise(r=>setImmediate(r));
 assert.deepEqual(JSON.parse(t.run('JSON.stringify(tasks)')),[[1,{type:'control',id:'loot_mode',args:{mode:'quality'}}]],'numbers first, unknown controls skipped');
 t.run('applyStartupProfile()');await new Promise(r=>setImmediate(r));
 assert.equal(t.run('tasks.length'),1);
});

test('a named profile does not replay the last used choices',async()=>{
 const t=load();
 t.store.set('epochpact.profiles.live',JSON.stringify({Farming:{xp:3}}));
 t.store.set('epochpact.startup-profile.live',JSON.stringify('Farming'));
 t.store.set('epochpact.last-settings-replay.live',JSON.stringify({loot_mode:{id:'loot_mode',args:{mode:'quality'}}}));
 t.run('var tasks=[];runTask=async p=>{tasks.push(p);return {ok:true};}');
 t.run('applyStartupProfile()');await new Promise(r=>setImmediate(r));
 assert.equal(t.run('tasks.length'),0);assert.equal(t.run('values.xp'),3);
});

test('last used settings are applied once per game session and character',()=>{
 const t=load();
 t.store.set('epochpact.startup-profile.live',JSON.stringify('__last__'));
 t.store.set('epochpact.last-settings.live',JSON.stringify({xp:5,speed:2,campaign_complete:1}));
 t.run('applyStartupProfile()');
 assert.deepEqual(JSON.parse(t.run('JSON.stringify(applied)')),[['speed','xp']]);
 assert.equal(t.run('values.xp'),5);assert.equal(t.run('values.campaign_complete'),undefined);
 t.run('applyStartupProfile()');
 assert.equal(t.run('applied.length'),1,'same session and character is not reapplied');
 t.run("connected={...connected,sessionKey:'11:30'};dirty.clear()");
 t.run('applyStartupProfile()');
 assert.equal(t.run('applied.length'),2,'a restarted game applies the profile again');
});

test('a named profile is applied, and a deleted one is ignored',()=>{
 const t=load();
 t.store.set('epochpact.profiles.live',JSON.stringify({Farming:{xp:3}}));
 t.store.set('epochpact.startup-profile.live',JSON.stringify('Farming'));
 t.run('applyStartupProfile()');
 assert.equal(t.run('values.xp'),3);
 const u=load();
 u.store.set('epochpact.profiles.live','{}');u.store.set('epochpact.startup-profile.live',JSON.stringify('Gone'));
 u.run('applyStartupProfile()');assert.equal(u.run('applied.length'),0);
});

test('waits for a ready offline character',()=>{
 const t=load();
 t.store.set('epochpact.startup-profile.live',JSON.stringify('__last__'));
 t.store.set('epochpact.last-settings.live',JSON.stringify({xp:5}));
 t.run("connected={...connected,transitioning:true}");t.run('applyStartupProfile()');
 assert.equal(t.run('applied.length'),0);
 t.run("connected={...connected,transitioning:false}");t.run('applyStartupProfile()');
 assert.equal(t.run('applied.length'),1);
});

test('last used settings record only changed session settings',()=>{
 const t=load();
 t.store.set('epochpact.startup-profile.live',JSON.stringify('__last__'));
 t.run('appliedValues={xp:4,speed:1,campaign_complete:1};rememberLastSettings()');
 assert.deepEqual(JSON.parse(t.store.get('epochpact.last-settings.live')),{xp:4});
 t.store.set('epochpact.startup-profile.live',JSON.stringify('__off__'));
 t.run('appliedValues={xp:9,speed:1};rememberLastSettings()');
 assert.deepEqual(JSON.parse(t.store.get('epochpact.last-settings.live')),{xp:4},'nothing is recorded while the option is off');
});

test('Minimum Weaver\'s Will and the Weaver\'s Will multiplier come back with the last used settings',()=>{
 const t=load();
 const real=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8')).controls.filter(c=>['loot_lp','loot_ww','weaverswill'].includes(c.id));
 assert.equal(real.length,3);
 t.context.real=real;t.run('catalog.controls.push(...real)');
 // A session where both were changed: they are remembered with the other settings.
 t.run('appliedValues={xp:1,speed:1,loot_lp:2,loot_ww:18,weaverswill:2};rememberLastSettings()');
 assert.deepEqual(JSON.parse(t.store.get('epochpact.last-settings.live')),{loot_ww:18,weaverswill:2},'defaults (LP 2) are not stored');
 // After a restart the next ready character gets them back.
 t.run('appliedValues={xp:1,speed:1,loot_lp:2,loot_ww:0,weaverswill:1};values={...appliedValues};startupApplied.clear();applyStartupProfile()');
 assert.deepEqual(JSON.parse(t.run('JSON.stringify(applied)')),[['loot_ww','weaverswill']]);
 assert.equal(t.run('values.loot_ww'),18);assert.equal(t.run('values.weaverswill'),2);
});

test('a character loaded again gets the last used settings again, even when the interface thinks they are set',()=>{
 const t=load();
 t.store.set('epochpact.last-settings.live',JSON.stringify({xp:5}));
 t.run("connected={...connected,player:{id:'7',name:'Hero',load:1}};appliedValues={xp:1,speed:1};applyStartupProfile()");
 assert.equal(t.run('applied.length'),1);
 t.run("appliedValues={xp:5,speed:1};dirty.clear();applyStartupProfile()");
 assert.equal(t.run('applied.length'),1,'the same load is not applied twice');
 // Back from the character list: the game may have reset the setting; it is sent again although the last read said 5.
 t.run("connected={...connected,player:{id:'7',name:'Hero',load:2}};dirty.clear();applyStartupProfile()");
 assert.deepEqual(JSON.parse(t.run('JSON.stringify(applied)')),[['xp'],['xp']]);
});

test('the profile waits while another task runs, goes on past a refused setting and says so when it starts and ends',async()=>{
 const t=load({realDraft:true});
 t.run('catalog.controls.push({id:"gold",group:"general",lifetime:"session",widget:"number",default:1,requirements:["offline"],parameters:[]})');
 t.store.set('epochpact.last-settings.live',JSON.stringify({xp:5,speed:2,gold:3}));
 t.run('var sent=[];runTask=async p=>{sent.push(p.id);return p.id==="speed"?null:{ok:true};};var timers=[];setTimeout=(fn,ms)=>{timers.push(fn);return 0;};renderPage=()=>{};syncLootCraft=()=>false');
 t.run('applyStartupProfile()');
 // Another task starts before the profile's turn: it waits instead of being skipped.
 t.run('busy=true');await t.run('timers.shift()')();
 assert.equal(t.run('sent.length'),0);assert.equal(t.run('timers.length'),1,'a retry is queued');
 t.run('busy=false');t.run('timers.shift()')();
 for(let i=0;i<20;i++)await new Promise(r=>setImmediate(r));
 assert.deepEqual(JSON.parse(t.run('JSON.stringify(sent)')).sort(),['gold','speed','xp']);
 assert.deepEqual(JSON.parse(t.run('JSON.stringify(notes)')),['Applying your last used settings (3 settings).',
  '!2 of 3 settings of your last used settings applied; the others were refused and stay in the draft.']);
 assert.equal(t.run('dirty.has("speed")'),true);assert.equal(t.run('appliedValues.xp'),5);
});

test('Apply changes goes on past a refused setting and keeps it as a draft',async()=>{
 const t=load({realDraft:true});
 t.run('catalog.controls.push({id:"gold",group:"general",lifetime:"session",widget:"number",default:1,requirements:["offline"],parameters:[]})');
 t.run('var sent=[];runTask=async p=>{sent.push(p.id);return p.id==="gold"?null:{ok:true};};renderPage=()=>{};syncLootCraft=()=>false');
 t.run('values.xp=4;values.gold=3;values.speed=2;dirty.add("xp");dirty.add("gold");dirty.add("speed")');
 await t.run('applyDraft()');
 assert.deepEqual(JSON.parse(t.run('JSON.stringify(sent)')),['xp','gold','speed'],'the setting after the refused one is still sent');
 assert.deepEqual(JSON.parse(t.run('JSON.stringify(notes)')),['!2 of 3 settings applied; the others were refused and stay in the draft.']);
 assert.equal(t.run('dirty.has("gold")'),true);assert.equal(t.run('dirty.has("xp")||dirty.has("speed")'),false);
 assert.equal(t.run('appliedValues.speed'),2);
});

test('a shared profile file becomes a new profile with only the settings a profile may hold',()=>{
 const t=load();
 t.run('catalog.controls.push({id:"gold",group:"general",lifetime:"session",widget:"number",default:1,requirements:["offline"],parameters:[{name:"value",type:"number",minimum:1,maximum:100}]})');
 t.store.set('epochpact.profiles.live',JSON.stringify({Mine:{xp:2}}));
 const added=JSON.parse(t.run(`JSON.stringify(addSharedProfile("Mine",JSON.parse('{"xp":5,"speed":2,"gold":500,"campaign_complete":1,"loot_mode":"filter","unknown":3,"__proto__":{"xp":9}}')))`));
 assert.deepEqual(added,{name:'Mine (2)',count:2},'the existing profile is kept; a save action, a choice, an unknown ID and an out-of-range value are dropped');
 assert.deepEqual(JSON.parse(t.store.get('epochpact.profiles.live')),{Mine:{xp:2},'Mine (2)':{xp:5,speed:2}});
 assert.equal(t.run('addSharedProfile("",{campaign_complete:1})'),null);
 assert.deepEqual(Object.keys(JSON.parse(t.store.get('epochpact.profiles.live'))),['Mine','Mine (2)'],'nothing is stored without usable settings');
 assert.equal(t.run('addSharedProfile("  ",{xp:3}).name'),'Imported profile');
});

test('changing one setting never drops the others from the last used settings',async()=>{
 const t=load();
 t.store.set('epochpact.last-settings.live',JSON.stringify({xp:5,speed:2}));
 // The game was restarted: the interface reads the defaults until the profile is applied again.
 t.run("appliedValues={xp:1,speed:1};job=async p=>({ok:true});busy=false;renderPage=()=>{};updateDisabled=()=>{}");
 await t.run("runTask({type:'control',id:'xp',args:{value:7}},{silent:true})");
 assert.deepEqual(JSON.parse(t.store.get('epochpact.last-settings.live')),{xp:7,speed:2},'speed stays although it is not applied yet');
 await t.run("runTask({type:'control',id:'speed',operation:'reset',args:{}},{silent:true})");
 assert.deepEqual(JSON.parse(t.store.get('epochpact.last-settings.live')),{xp:7},'a reset removes it');
 await t.run("runTask({type:'control',id:'xp',args:{value:1}},{silent:true})");
 assert.deepEqual(JSON.parse(t.store.get('epochpact.last-settings.live')),{},'the default removes it');
});

test('the character bonuses come back after the settings, quietly when there are none, never with the startup profile off',async()=>{
 const restore="catalog.controls.push({id:'stat_restore',group:'stats',lifetime:'actor',widget:'button',default:null,requirements:['offline','loaded_actor'],parameters:[]})";
 const reply=n=>`var tasks=[];runTask=async p=>{tasks.push([applied.length,p.id]);return {ok:true,text:'statrestore: restored ${n} of 3 bonuses (0 already applied)'};}`;
 const t=load();t.run(restore);t.run(reply(2));
 t.store.set('epochpact.last-settings.live',JSON.stringify({xp:5}));
 t.run('applyStartupProfile()');await new Promise(r=>setImmediate(r));
 assert.deepEqual(JSON.parse(t.run('JSON.stringify(tasks)')),[[1,'stat_restore']],'after the settings');
 assert.equal(t.run('notes[notes.length-1]'),'2 character bonuses restored.');
 const none=load();none.run(restore);none.run(reply(0));
 none.run('applyStartupProfile()');await new Promise(r=>setImmediate(r));
 assert.deepEqual(JSON.parse(none.run('JSON.stringify(tasks)')),[[0,'stat_restore']],'also with no settings stored');
 assert.equal(none.run('notes.length'),0,'nothing to say');
 const off=load();off.run(restore);off.run(reply(2));
 off.store.set('epochpact.startup-profile.live',JSON.stringify('__off__'));
 off.run('applyStartupProfile()');await new Promise(r=>setImmediate(r));
 assert.equal(off.run('tasks.length'),0);
});

test('Reset all settings sends every profile setting not read at its default, and only those',async()=>{
 const t=load();
 t.run("appliedValues={xp:5,speed:1};values={xp:5,speed:1};eligible=()=>true;renderPage=()=>{};");
 await t.run('resetAllSettings()');
 assert.deepEqual(JSON.parse(t.run('JSON.stringify(applied)')),[['xp']],'speed is read at its default; save actions and choices are not settings to reset');
 assert.equal(t.run('values.xp'),1);
 const t2=load();
 t2.run("eligible=()=>true;renderPage=()=>{};");
 await t2.run('resetAllSettings()');
 assert.equal(t2.run('applied.length'),0);
 assert.match(t2.run('notes.at(-1)'),/already at the game's normal values/);
});
