const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');

// Run the shipping startup-profile functions with a stored-preferences fixture.
function load(){
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
  var notes=[],applied=[];notify=m=>notes.push(m);applyDraft=async()=>{applied.push([...dirty].sort());};`,context);
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
