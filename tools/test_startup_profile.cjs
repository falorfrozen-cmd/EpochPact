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
   {id:'campaign_complete',group:'campaign',lifetime:'game_save',widget:'button',default:null,requirements:['offline'],parameters:[]}]};
  profileKey='epochpact.profiles.live';preview=false;
  connected={ok:true,connected:true,offline:true,state:'InGame',transitioning:false,phase:'ready',sessionKey:'10:20',player:{id:'7',name:'Hero'}};
  appliedValues={xp:1,speed:1};values={xp:1,speed:1};
  var notes=[],applied=[];notify=m=>notes.push(m);applyDraft=async()=>{applied.push([...dirty].sort());};`,context);
 return {context,store,run:s=>vm.runInContext(s,context)};
}

test('nothing is applied while the startup profile is off',()=>{
 const t=load();
 t.store.set('epochpact.last-settings.live',JSON.stringify({xp:5}));
 t.run('applyStartupProfile()');
 assert.equal(t.run('applied.length'),0);assert.equal(t.run('dirty.size'),0);
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
 t.store.set('epochpact.startup-profile.live',JSON.stringify(''));
 t.run('appliedValues={xp:9,speed:1};rememberLastSettings()');
 assert.deepEqual(JSON.parse(t.store.get('epochpact.last-settings.live')),{xp:4},'nothing is recorded while the option is off');
});
