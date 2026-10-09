const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const Session=require('../ui/session.js');

// Run the shipping handlers with a small DOM stand-in. IPC replies are fixtures;
// no game process, character file or mutation is touched by these tests.
function harness(){
 const elements=new Map(),buttons=[],calls=[],replies=[],windowListeners={};
 function element(selector){
  if(!elements.has(selector))elements.set(selector,{innerHTML:'',textContent:'',hidden:false,dataset:{},listeners:{},
   classList:{remove(){},toggle(){}},addEventListener(type,fn){(this.listeners[type]??=[]).push(fn);},close(){},showModal(){this.open=true;},
   querySelector(){return null;},closest(){return null;},insertAdjacentHTML(_,value){this.innerHTML+=value;}});
  return elements.get(selector);
 }
 const document={querySelector:element,querySelectorAll:s=>s==='[data-control-id]'?buttons:[],addEventListener(){},documentElement:{dataset:{}}};
 const context=vm.createContext({document,window:{EpochSession:Session,EpochStats:{},addEventListener(type,fn){(windowListeners[type]??=[]).push(fn);}},
  location:{hash:'#monolith'},setTimeout:()=>0,clearTimeout(){},console,fetch(){throw Error('unexpected HTTP call');}});
 context.FormData=class{constructor(form){this.fields=form.fields;}get(name){return this.fields[name]??null;}getAll(name){const value=this.get(name);return value==null?[]:[value];}};
 const source=fs.readFileSync(process.env.EPOCHPACT_UI_SOURCE||path.join(__dirname,'../ui/app.js'),'utf8');
 vm.runInContext(source.replace('void init();',''),context);
 context.testJob=async payload=>{calls.push(JSON.parse(JSON.stringify(payload)));assert.ok(replies.length,'unexpected request');const reply=replies.shift();if(reply instanceof Error)throw reply;return reply;};
 vm.runInContext(`job=testJob;const shippingRenderPage=renderPage;let pageRenderCount=0;renderPage=()=>{pageRenderCount++;updateDisabled();};catalog={controls:[
  {id:'monolith_read',group:'monolith',lifetime:'read_only',requirements:['offline'],widget:'read'},
  {id:'stability',group:'monolith',lifetime:'game_save',requirements:['offline'],widget:'number'}]};page='monolith';`,context);
 const hint=element('hint');
 const button={dataset:{controlId:'stability'},closest:()=>({querySelector:()=>hint})};buttons.push(button);
 const run=source=>vm.runInContext(source,context);
 const connection=scene=>({ok:true,connected:true,offline:true,state:'InGame',transitioning:false,player:{id:'0',scene},values:{}});
 const monolith=(scene,editable)=>({ok:true,player:{id:'0',scene},editable,stabilityMultiplier:1});
 context.connectionFixture=connection;context.monolithFixture=monolith;
 return {run,context,calls,replies,button,buttons,hint,element,connection,monolith,
  async focus(){windowListeners.focus.forEach(fn=>fn());await new Promise(setImmediate);}};
}

function automaticHarness(){
 const h=harness();h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
 h.run(`catalog=fullCatalog;page='general';connected=connectionFixture('M_Rest');connected.player.name='Test';values={xp:1,gold:1,speed:1};appliedValues={...values};`);
 return h;
}

test('General edits coalesce to their latest value without submitting unrelated crafting drafts',async()=>{
 const h=automaticHarness();h.run(`values.craft_glyphs=1;dirty.add('craft_glyphs');settingChanged('xp',2,300);settingChanged('xp',3,300);settingChanged('xp',4,300);`);
 await h.run('flushAutomatic()');assert.equal(h.calls.length,0,'typing debounce has not elapsed');
 h.replies.push({ok:true});await h.run('flushAutomatic(true)');
 assert.deepEqual(h.calls,[{type:'control',id:'xp',args:{value:4,expected_id:'0',expected_name:'Test'}}]);
 assert.equal(h.run('values.xp'),4);assert.equal(h.run('appliedValues.xp'),4);assert.equal(h.run('dirty.has("xp")'),false);
 assert.equal(h.run('dirty.has("craft_glyphs")'),true);assert.equal(h.run('pageRenderCount'),0,'typing never rebuilds the form');
});

test('a newer automatic edit survives an older acknowledgement and sends in order',async()=>{
 const h=automaticHarness();let resolve;h.replies.push(new Promise(r=>resolve=r));
 h.run(`settingChanged('xp',2);`);const first=h.run('flushAutomatic(true)');
 h.run(`settingChanged('xp',5);`);assert.equal(h.calls.length,1);
 resolve({ok:true});await first;
 assert.equal(h.run('values.xp'),5);assert.equal(h.run('appliedValues.xp'),2);assert.equal(h.run('dirty.has("xp")'),true);
 h.replies.push({ok:true});await h.run('flushAutomatic(true)');
 assert.deepEqual(h.calls.map(x=>x.args.value),[2,5]);assert.equal(h.run('dirty.size'),0);
});

test('slider release matching an in-flight input never repeats the same command',async()=>{
 const h=automaticHarness();let resolve;h.replies.push(new Promise(r=>resolve=r));
 h.run(`settingChanged('xp',3,250);`);const first=h.run('flushAutomatic(true)');
 h.run(`settingChanged('xp',3,0);`);resolve({ok:true});await first;
 await h.run('flushAutomatic(true)');assert.equal(h.calls.length,1);assert.equal(h.run('dirty.size'),0);
});

test('returning a slider to its original value during an in-flight change still restores it',async()=>{
 const h=automaticHarness();let resolve;h.replies.push(new Promise(r=>resolve=r));
 h.run(`settingChanged('xp',4);`);const first=h.run('flushAutomatic(true)');
 h.run(`settingChanged('xp',1);`);resolve({ok:true});await first;
 h.replies.push({ok:true});await h.run('flushAutomatic(true)');
 assert.deepEqual(h.calls.map(x=>x.args.value),[4,1]);assert.equal(h.run('appliedValues.xp'),1);
});

test('an automatic edit waits for another operation without being dropped',async()=>{
 const h=automaticHarness();h.run(`busy=true;settingChanged('xp',2);`);
 await h.run('flushAutomatic(true)');assert.equal(h.calls.length,0);assert.equal(h.run('autoPending.size'),1);
 h.run('busy=false');h.replies.push({ok:true});await h.run('flushAutomatic(true)');assert.equal(h.calls.length,1);
});

test('automatic failure stops the queue without retrying and exposes an explicit retry',async()=>{
 const h=automaticHarness();h.run(`settingChanged('xp',2);settingChanged('gold',3);`);
 h.replies.push(new Error('The response was lost.'));await h.run('flushAutomatic(true)');
 assert.equal(h.calls.length,1);assert.equal(h.run('autoPending.size'),0);assert.match(h.run('settingFeedback("xp")'),/Retry/);
 assert.match(h.run('settingFeedback("gold")'),/Not applied/);assert.equal(h.run('dirty.size'),2);
 await h.run('flushAutomatic(true)');assert.equal(h.calls.length,1);
 h.run(`queueAutomatic('xp',values.xp,0)`);h.replies.push({ok:true});await h.run('flushAutomatic(true)');
 assert.equal(h.calls.length,2);assert.equal(h.run('autoErrors.has("xp")'),false);assert.equal(h.run('autoErrors.has("gold")'),true);
});

test('changing character cancels unsent automatic settings instead of applying them to the next actor',async()=>{
 const h=automaticHarness();h.run(`settingChanged('xp',2);connected.player.id='1';`);
 await h.run('flushAutomatic(true)');assert.equal(h.calls.length,0);assert.equal(h.run('autoPending.size'),0);assert.equal(h.run('values.xp'),1);
});

test('invalidating a session discards pending auto writes and ignores its late acknowledgement',async()=>{
 const h=automaticHarness();let resolve;h.replies.push(new Promise(r=>resolve=r));
 h.run(`settingChanged('xp',2)`);const first=h.run('flushAutomatic(true)');
 h.run(`settingChanged('gold',4);invalidateLive();`);resolve({ok:true});await first;
 assert.equal(h.calls.length,1);assert.equal(h.run('autoPending.size'),0);assert.equal(h.run('dirty.size'),0);assert.equal(h.run('appliedValues.xp'),1);
});

test('an old speed actor readback cannot overwrite the newer automatic speed edit',async()=>{
 const h=automaticHarness();let resolve;h.replies.push(new Promise(r=>resolve=r));
 h.run(`settingChanged('speed',2);`);const first=h.run('flushAutomatic(true)');
 h.run(`settingChanged('speed',3);`);resolve({ok:true,actorValues:{'9:0:0:0':{modes:{increased:1}}}});await first;
 assert.equal(h.run('values.speed'),3);assert.equal(h.run('dirty.has("speed")'),true);
 h.replies.push({ok:true,actorValues:{'9:0:0:0':{modes:{increased:2}}}});await h.run('flushAutomatic(true)');
 assert.equal(h.run('appliedValues.speed'),3);assert.equal(h.run('dirty.size'),0);
});

test('invalid and unfinished numeric input cancels the previous unsent value without sending zero',async()=>{
 const h=automaticHarness();h.run(`settingChanged('xp',2,300);`);
 const input={id:'',name:'',dataset:{settingNumber:'xp'},value:'',validity:{valid:false},closest:()=>null};
 h.element('#page-content').listeners.input[0]({target:input});
 await h.run('flushAutomatic(true)');assert.equal(h.calls.length,0);assert.equal(h.run('autoPending.size'),0);
 assert.match(h.run('settingFeedback("xp")'),/Enter a valid value/);assert.doesNotMatch(h.run('settingFeedback("xp")'),/Retry/);
 for(const value of [0,101,NaN,Infinity,null]){h.context.invalidValue=value;h.run(`settingChanged('xp',invalidValue)`);}
 assert.equal(h.run('autoPending.size'),0);
});

test('readback, rendering and profile-like drafts cannot auto-apply anything on startup',async()=>{
 const h=automaticHarness();h.run(`values={xp:5,craft_glyphs:1};dirty=new Set(['xp','craft_glyphs']);shippingRenderPage();syncLootCraft('map_read',{enabled:true});`);
 await h.run('flushAutomatic(true)');assert.equal(h.calls.length,0);assert.equal(h.run('autoPending.size'),0);
 assert.equal(h.element('#apply-draft').hidden,true);assert.equal(h.element('#reset-draft').hidden,true);
 assert.match(h.element('#page-content').innerHTML,/Changes apply automatically/);
 h.run(`page='crafting';shippingRenderPage()`);assert.equal(h.element('#apply-draft').hidden,false);
});

test('manual Apply on another page cannot resend a failed auto mutation or flush unsent automatic edits',async()=>{
 const h=automaticHarness();h.run(`settingChanged('xp',2);values.craft_glyphs=1;dirty.add('craft_glyphs');page='crafting';`);
 h.replies.push({ok:true,preserveGlyphs:true},{ok:true});await h.run('applyDraft()');
 assert.deepEqual(h.calls.map(x=>x.id),['craft_glyphs','craft_read']);assert.equal(h.run('autoPending.has("xp")'),true);
});

test('discarding drafts cancels unsent automatic edits on another page',async()=>{
 const h=automaticHarness();h.run(`settingChanged('xp',2);page='crafting';`);
 h.element('#reset-draft').onclick();await h.run('flushAutomatic(true)');
 assert.equal(h.calls.length,0);assert.equal(h.run('values.xp'),1);assert.equal(h.run('autoPending.size'),0);
});

test('loading a real profile cancels older queued edits and keeps the profile unapplied',async()=>{
 const h=automaticHarness();h.context.localStorage={getItem:()=>JSON.stringify({Saved:{xp:7,craft_glyphs:1}})};
 h.element('#profile-select').value='Saved';h.run(`profileKey='test';settingChanged('xp',2);page='settings';`);
 const button={id:'profile-load',dataset:{},disabled:false,hasAttribute:()=>false};
 await h.element('#page-content').listeners.click[0]({target:{closest:()=>button}});
 await h.run('flushAutomatic(true)');
 assert.equal(h.calls.length,0);assert.equal(h.run('autoPending.size'),0);assert.equal(h.run('values.xp'),7);
 assert.equal(h.run('dirty.has("xp")'),true);assert.equal(h.run('dirty.has("craft_glyphs")'),true);
});

test('save actions, crafting, loot and Monolith controls never enter the automatic queue',()=>{
 const h=automaticHarness();
 for(const id of ['craft_forge','quests_complete','craft_glyphs','autopickup','stability'])assert.equal(h.run(`automaticSetting(controlById('${id}'))`),false,id);
 h.run(`page='overview';settingChanged('xp',2);`);assert.equal(h.run('autoPending.size'),1,'overview quick controls share General behavior');
 h.run(`cancelAutomatic();connected.offline=false;settingChanged('xp',2);`);assert.equal(h.run('autoPending.size'),0);
 assert.equal(h.calls.length,0);
});

test('forge readiness uses its selected item owner and native eligibility, not CoF data',()=>{
 const h=harness();
 h.run(`catalog.controls.push({id:'craft_forge',group:'crafting',lifetime:'game_save',requirements:['offline'],widget:'button'});connected=connectionFixture('M_Rest');cache={craft_read:{forgePreview:{player:{id:'0'},canForge:true}}};`);
 const button={dataset:{controlId:'craft_forge'},closest:()=>({querySelector:()=>h.hint})};h.buttons.push(button);
 h.run('updateDisabled()');assert.equal(button.disabled,false);
 h.run('cache.craft_read.forgePreview.canForge=false;updateDisabled()');assert.equal(button.disabled,true);
 h.run(`cache.craft_read.forgePreview={player:{id:'1'},canForge:true};updateDisabled()`);assert.equal(button.disabled,true);
 assert.equal(h.calls.length,0);
});

test('material/map readbacks synchronize switches without overwriting pending drafts',()=>{
 const h=harness();
 h.run(`values={craft_runes:1};dirty.add('craft_runes');syncLootCraft('craft_read',{preserveRunes:false,preserveGlyphs:true,bypassLevel:false});syncLootCraft('map_read',{enabled:true});`);
 assert.equal(h.run('values.craft_runes'),1);assert.equal(h.run('appliedValues.craft_runes'),0);
 assert.equal(h.run('values.craft_glyphs'),1);assert.equal(h.run('values.craft_level'),0);assert.equal(h.run('values.map_reveal'),1);
 assert.equal(h.calls.length,0);
});

test('applying crafting settings refreshes real forge eligibility once and never crafts automatically',async()=>{
 const h=harness();h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
 h.run(`catalog=fullCatalog;connected=connectionFixture('M_Rest');values={craft_level:1,craft_glyphs:1};dirty=new Set(['craft_level','craft_glyphs']);cache.craft_read={forgePreview:{player:{id:'0'},canForge:false}};`);
 h.replies.push({ok:true,bypassLevel:true},{ok:true,preserveGlyphs:true},{ok:true,forgePreview:{player:{id:'0'},canForge:true}});
 await h.run('applyDraft()');
 assert.deepEqual(h.calls.map(p=>p.id),['craft_level','craft_glyphs','craft_read']);
 assert.equal(h.calls.at(-1).operation,'read');assert.equal(h.run('cache.craft_read.forgePreview.canForge'),true);
 assert.equal(h.run('dirty.size'),0);assert.ok(h.calls.every(p=>p.id!=='craft_forge'));
});

test('player pages retain all usable controls and developer pages retain diagnostic readers',()=>{
 const h=harness();h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
 h.run(`catalog=fullCatalog;connected=connectionFixture('M_Rest');values={};cache={craft_read:{forgePreview:{player:{id:'0'},canForge:false,message:'Add an item to craft.'}}};`);
 for(const mode of [false,true])for(const group of ['general','loot','crafting']){
  h.context.developerMode=mode;h.run('developer=developerMode');
  h.context.targetPage=group;h.run('page=targetPage;shippingRenderPage()');
  const html=h.element('#page-content').innerHTML;
  const displayed=[...html.matchAll(/(?:data-setting|data-id)="([^"]+)"/g)].map(x=>x[1]);
  const refreshes=[...html.matchAll(/data-refresh="([^"]+)"/g)].map(x=>x[1]);
  const expected=h.context.fullCatalog.controls.filter(c=>c.group===group&&(mode||!['density_read','map_read'].includes(c.id))).map(c=>c.id);
  assert.deepEqual([...new Set([...displayed,...refreshes])].sort(),expected.slice().sort(),group+' developer='+mode);
  assert.equal(new Set(displayed).size,displayed.length,'every control appears once');
 }
 assert.ok(h.element('#page-content').innerHTML.includes('Add an item to craft.'));
 assert.ok(h.element('#nav').innerHTML.includes('href="#crafting"'));
 assert.ok(h.element('#nav').innerHTML.includes('href="#loot"'));
 assert.equal(h.calls.length,0,'rendering cannot craft or apply settings');
});

test('entering each reorganized page reads only its own live section and preserves cross-page drafts',async()=>{
 for(const [target,id] of [['general','map_read'],['loot','loot_read'],['crafting','craft_read']]){
  const h=harness();h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
  h.context.targetHash='#'+target;
  h.run(`catalog=fullCatalog;connected=connectionFixture('M_Rest');location.hash=targetHash;values={craft_glyphs:1};dirty=new Set(['craft_glyphs']);`);
  h.replies.push(h.connection('M_Rest'),{ok:true});
  await h.run('openPage()');
  assert.deepEqual(h.calls.map(c=>c.type==='connection'?'connection':c.id),['connection',id]);
  assert.equal(h.calls.at(-1).operation,'read');
  assert.equal(h.run('values.craft_glyphs'),1);assert.equal(h.run('dirty.has("craft_glyphs")'),true);
 }
});

test('returning to the Crafting page refreshes a changed Forge selection without dispatching a craft',async()=>{
 const h=harness();h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
 h.run(`catalog=fullCatalog;page='crafting';connected=connectionFixture('M_Rest');cache.craft_read={forgePreview:{player:{id:'0'},canForge:false}};`);
 h.replies.push(h.connection('M_Rest'),{ok:true,forgePreview:{player:{id:'0'},canForge:true}});
 await h.run('refreshConnection(true,true,{preservePage:true})');
 assert.deepEqual(h.calls.map(c=>c.type==='connection'?'connection':c.id),['connection','craft_read']);
 assert.equal(h.run('cache.craft_read.forgePreview.canForge'),true);
});

test('a search result on the current Crafting page clears search and reopens the page without a mutation',async()=>{
 const h=harness();h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
 h.run(`catalog=fullCatalog;page='crafting';location.hash='#crafting';connected=connectionFixture('M_Rest');`);
 h.element('#global-search').value='Preserve crafting runes';
 h.replies.push(h.connection('M_Rest'),{ok:true});
 h.run(`go('crafting')`);await new Promise(setImmediate);
 assert.equal(h.element('#global-search').value,'');
 assert.deepEqual(h.calls.map(c=>c.type==='connection'?'connection':c.id),['connection','craft_read']);
});

function enterMonolithValue(h,id,value,event='input'){
 const input={id:'',dataset:{},name:'value',value:String(value),closest:()=>({dataset:{id}})};
 h.element('#page-content').listeners[event][0]({target:input});
}

test('returning from an Echo refreshes the old denied permissions for the same character',async()=>{
 const h=harness();h.run(`connected=connectionFixture('Echo');cache.monolith_read=monolithFixture('Echo',false);cache.echo_read={old:true};`);
 h.run('updateDisabled()');assert.equal(h.button.disabled,true);assert.match(h.hint.textContent,/End of Time/);
 h.replies.push(h.connection('M_Rest'),h.monolith('M_Rest',true));
 await h.run('refreshConnection()');
 assert.deepEqual(h.calls,[{type:'connection'},{type:'control',id:'monolith_read',operation:'read'}]);
 assert.equal(h.button.disabled,false);assert.equal(h.hint.hidden,true);assert.equal(h.run('cache.echo_read'),undefined);
});

test('leaving the hub removes old permission and presents the actual area restriction',async()=>{
 const h=harness();h.run(`connected=connectionFixture('M_Rest');cache.monolith_read=monolithFixture('M_Rest',true);`);
 h.replies.push(h.connection('Echo'),h.monolith('Echo',false));
 await h.run('refreshConnection()');
 assert.equal(h.button.disabled,true);assert.equal(h.hint.hidden,false);assert.match(h.button.title,/End of Time/);
});

test('one Refresh live click updates stale identity before reading the section, without recursion',async()=>{
 const h=harness();h.run(`connected=connectionFixture('Echo');cache.monolith_read=monolithFixture('Echo',false);`);
 h.replies.push(h.connection('M_Rest'),h.monolith('M_Rest',true));
 await h.run(`refreshSection('monolith_read')`);
 assert.equal(h.calls.length,2);assert.equal(h.calls[0].type,'connection');assert.equal(h.calls[1].id,'monolith_read');
 assert.equal(h.button.disabled,false);
 assert.ok(h.calls.every(x=>x.type==='connection'||x.operation==='read'));
});

test('typed Stability survives a refresh but is discarded when the character changes',async()=>{
 const h=harness();h.run(`connected=connectionFixture('M_Rest');cache.monolith_read=monolithFixture('M_Rest',true);monoTarget={timeline:1,difficulty:'normal'};`);
 const input={id:'',dataset:{},name:'value',value:'300',closest:()=>({dataset:{id:'stability'}})};
 h.element('#page-content').listeners.input[0]({target:input});
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'300');
 h.replies.push(h.connection('M_Rest'));await h.run('refreshConnection()');
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'300');
 const other=h.connection('M_Rest');other.player.id='1';h.replies.push(other);
 await h.run('refreshConnection(true,false)');assert.equal(h.run('Object.keys(monoDrafts).length'),0);
});

test('rendering fresh values preserves the entered amount for its own timeline only',()=>{
 const h=harness();
 h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
 h.run(`catalog=fullCatalog;connected=connectionFixture('M_Rest');monoTarget={timeline:1,difficulty:'normal'};
  monoDrafts[monoDraftKey('stability')]='300';
  cache.monolith_read={...monolithFixture('M_Rest',true),timelines:[1,2].map(id=>({id,name:'Timeline '+id,difficulties:[{index:0,level:62,unlocked:true,minCorruption:0,maxCorruption:50,maxStability:500,run:{corruption:0,stability:0}}]}))};
  renderMonolith();`);
 assert.match(h.element('#page-content').innerHTML,/data-id="stability"[\s\S]*?value="300"/);
 h.run(`monoTarget.timeline=2;renderMonolith();`);
 assert.doesNotMatch(h.element('#page-content').innerHTML,/value="300"/);
 assert.match(h.element('#page-content').innerHTML,/data-id="stability"[\s\S]*?value="0"/);
});

test('a transition or disconnected game never causes a save request or enables Apply',async()=>{
 for(const reply of [{ok:true,connected:false,offline:false},{...harness().connection('M_Rest'),transitioning:true}]){
  const h=harness();h.run(`connected=connectionFixture('M_Rest');cache.monolith_read=monolithFixture('M_Rest',true);`);
  h.replies.push(reply);await h.run(`refreshSection('monolith_read')`);
  assert.equal(h.calls.length,1);assert.equal(h.button.disabled,true);
 }
});

test('Refresh live stays available when stale connection state has blocked Apply',()=>{
 const h=harness(),refresh={dataset:{controlId:'monolith_read',refresh:'monolith_read'},closest:()=>null};
 h.buttons.push(refresh);
 h.run(`connected={connected:false,offline:false};updateDisabled();`);
 assert.equal(h.button.disabled,true);assert.equal(refresh.disabled,false);assert.match(refresh.title,/Refresh game connection/);
 h.run('busy=true;updateDisabled()');assert.equal(refresh.disabled,true);
});

test('Monolith density uses the shared catalog bounds and stays in sync with General settings',()=>{
 const h=harness();h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
 h.run(`catalog=fullCatalog;connected=connectionFixture('M_Rest');values.density=appliedValues.density=1;`);
 const initial=h.run('monolithDensityPanel()');
 assert.match(initial,/Monolith monster density/);assert.match(initial,/data-setting-range="density" min="1" max="5"/);
 assert.match(initial,/Default: 1/);assert.match(initial,/shared with General settings/);assert.match(initial,/Empowered Echoes/);
 assert.equal(h.calls.length,0);
 h.run(`settingChanged('density',2.5)`);
 for(const card of [h.run('monolithDensityPanel()'),h.run(`settingCard(controlById('density'))`)]){
  assert.match(card,/data-setting-number="density"[^>]*value="2.5"/);assert.match(card,/Draft/);
 }
 h.run(`settingChanged('density',6)`);assert.equal(h.run('values.density'),2.5);
 h.run(`settingChanged('density',0)`);assert.equal(h.run('values.density'),2.5);
});

test('Monolith density only submits one existing density command after explicit Apply changes',async()=>{
 const h=harness();h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
 h.run(`catalog=fullCatalog;connected=connectionFixture('M_Echo');cache.monolith_read=monolithFixture('M_Echo',false);values.density=appliedValues.density=1;`);
 h.run(`settingChanged('density',3)`);assert.equal(h.calls.length,0);
 h.replies.push({ok:true,text:'density -> x3'});await h.run('applyDraft()');
 assert.deepEqual(h.calls,[{type:'control',id:'density',args:{value:3}}]);
 assert.equal(h.run('dirty.size'),0);assert.equal(h.run('appliedValues.density'),3);
 assert.match(h.run('monolithDensityPanel()'),/data-setting-number="density"[^>]*value="3"/);
});

test('actual focus handler keeps both Monolith drafts through missing identity and reconnect',async()=>{
 const h=harness();h.run(`connected=connectionFixture('M_Rest');cache.monolith_read=monolithFixture('M_Rest',true);monoTarget={timeline:1,difficulty:'empowered'};`);
 enterMonolithValue(h,'stability','300');enterMonolithValue(h,'corruption','150');
 h.replies.push({...h.connection('M_Rest'),transitioning:true,player:null});await h.focus();
 assert.equal(h.button.disabled,true);assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'300');
 assert.equal(h.run(`monoDrafts[monoDraftKey('corruption')]`),'150');
 assert.equal(h.run('cache.monolith_read'),undefined);
 h.replies.push(h.connection('M_Rest'),h.monolith('M_Rest',true));await h.focus();
 assert.equal(h.button.disabled,false);assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'300');
 assert.equal(h.run(`monoDrafts[monoDraftKey('corruption')]`),'150');
 assert.equal(h.run('monoTarget.timeline'),1);assert.equal(h.run('monoTarget.difficulty'),'empowered');
 assert.ok(h.calls.every(p=>p.type==='connection'||p.operation==='read'));
});

test('timeout, server-session invalidation and repeated focus reads keep an unapplied amount',async()=>{
 const h=harness();h.run(`connected=connectionFixture('M_Rest');cache.monolith_read=monolithFixture('M_Rest',true);monoTarget={timeline:1,difficulty:'normal'};`);
 enterMonolithValue(h,'stability','321');h.replies.push(new Error('IPC timed out'));await h.focus();
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'321');assert.equal(h.button.disabled,true);
 h.run('invalidateLive()');assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'321');
 h.replies.push(h.connection('M_Rest'),h.monolith('M_Rest',true));await h.focus();
 for(let n=0;n<3;n++){h.replies.push(h.connection('M_Rest'));await h.focus();}
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'321');
 assert.ok(h.calls.every(p=>p.type==='connection'||p.operation==='read'));
});

test('slot zero is stable across number/string identities; a verified new owner clears draft and target',async()=>{
 const h=harness();h.run(`connected=connectionFixture('M_Rest');connected.player.id=0;connected.player.name='Falor';monoTarget={timeline:8,difficulty:'empowered'};`);
 enterMonolithValue(h,'stability','400');
 const same=h.connection('M_Rest');same.player.name='Falor';h.replies.push(same);await h.run('refreshConnection(true,false)');
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'400');
 const unnamed=h.connection('M_Rest');unnamed.player.name='';h.replies.push(unnamed);await h.run('refreshConnection(true,false)');
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'400');
 h.replies.push({...h.connection('M_Rest'),state:'CharacterSelect',player:null});await h.run('refreshConnection(true,false)');
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'400');
 const other=h.connection('M_Rest');other.player.id='1';other.player.name='Another';h.replies.push(other);await h.run('refreshConnection(true,false)');
 assert.equal(h.run('Object.keys(monoDrafts).length'),0);assert.equal(h.run('monoTarget.timeline'),null);
 assert.equal(h.run('monoTarget.difficulty'),'normal');
 enterMonolithValue(h,'stability','200');
 const reusedSlot=h.connection('M_Rest');reusedSlot.player.id='1';reusedSlot.player.name='Recreated';h.replies.push(reusedSlot);await h.run('refreshConnection(true,false)');
 assert.equal(h.run('Object.keys(monoDrafts).length'),0);
});

function actionHarness(){
 const h=harness();h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
 h.run(`catalog=fullCatalog;connected=connectionFixture('M_Rest');cache.monolith_read=monolithFixture('M_Rest',true);monoTarget={timeline:1,difficulty:'normal'};refreshSection=async()=>{};`);
 const form={dataset:{id:'stability'},fields:{value:'300',timeline:'1',difficulty:'normal'},reportValidity:()=>true};
 h.context.testForm=form;enterMonolithValue(h,'stability','300');return h;
}

test('player Stability Apply keeps backup history and shows a precise message without raw JSON',async()=>{
 const h=actionHarness();h.run(`confirmAction=async()=>true;cache.monolith_read.timelines=[{id:1,name:'Fall of the Outcasts'}];`);
 const history=[{id:0,control:'stability',backup:'C:/private/backup',ok:true,time:1}];
 h.replies.push({ok:true,backup:'C:/private/backup',history,actorValues:{},text:'raw native output'});
 await h.run('formAction(testForm)');
 assert.equal(h.calls.length,1);assert.deepEqual(JSON.parse(h.run('JSON.stringify(history)')),history);
 assert.equal(h.element('#notification').textContent,'Stability set to 300. Fall of the Outcasts (Normal). A recovery backup is available in Settings.');
 assert.equal(h.element('#result-dialog').open,undefined);assert.equal(h.element('#result-text').innerHTML,'');
 assert.doesNotMatch(h.element('#notification').textContent,/actorValues|native output|private/);
});

test('a failed save action retains its draft and backup while hiding technical details',async()=>{
 const h=actionHarness();h.run('confirmAction=async()=>true');
 h.replies.push({ok:false,error:'Traceback: actorValues { bad } C:/private/backup',backup:'C:/private/backup',history:[{id:0,control:'stability',backup:'C:/private/backup',ok:false,time:1}]});
 await h.run('formAction(testForm)');
 assert.match(h.element('#notification').textContent,/could not complete|recovery backup/);
 assert.doesNotMatch(h.element('#notification').textContent,/Traceback|actorValues|private/);
 assert.equal(h.element('#result-dialog').open,undefined);
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'300');assert.equal(h.run('history.length'),1);
});

test('developer opt-in preserves technical result inspection without changing the operation',async()=>{
 const h=actionHarness();h.run('developer=true;confirmAction=async()=>true');
 h.replies.push({ok:true,actorValues:{},backup:'C:/private/backup'});await h.run('formAction(testForm)');
 assert.equal(h.calls.length,1);assert.equal(h.element('#result-dialog').open,true);
 assert.match(h.element('#result-text').innerHTML,/actorValues|private/);
});

test('player Settings keeps themes profiles and recovery but hides connection diagnostics',()=>{
 const h=automaticHarness();h.context.localStorage={getItem:()=>null};
 h.run(`page='settings';history=[{id:0,control:'stability',backup:'C:/private/backup',ok:true,time:1}];renderSettings();updateDisabled();`);
 const markup=h.element('#page-content').innerHTML;
 assert.match(markup,/Chronoforge/);assert.match(markup,/Void Atlas/);assert.match(markup,/profile-save/);assert.match(markup,/Recovery backups/);assert.match(markup,/data-id="undo"/);
 assert.doesNotMatch(markup,/Game connection|Mod settings \/ diagnostics|data-id="session_read"|data-id="play_offline"|data-id="load_character"/);
 assert.equal(h.element('#reconcile').hidden,true);assert.equal(h.calls.length,0);
 h.run('developer=true;renderSettings();updateDisabled();');
 assert.match(h.element('#page-content').innerHTML,/Game connection|data-id="session_read"/);assert.equal(h.element('#reconcile').hidden,false);
});

test('global search cannot bring hidden diagnostic cards into the player interface',()=>{
 const h=automaticHarness();h.run(`controlById('status').label='Connection diagnostics';`);const search=h.element('#global-search').listeners.input[0];
 search({target:{value:'diagnostics'}});assert.doesNotMatch(h.element('#page-content').innerHTML,/Connection diagnostics/);
 h.run('developer=true');search({target:{value:'diagnostics'}});assert.match(h.element('#page-content').innerHTML,/Connection diagnostics/);
});

test('Prophecy preview uses reward and lens names plus charges instead of raw metadata',()=>{
 const h=automaticHarness();h.run(`cache.cof_read={rewards:[{id:42,uiLabel:'Unique weapons'}],lenses:[{id:7,name:'Greater Lens'}]};resultDialog({ok:true,currentCharges:4,chargesAfter:9,actorValues:{},backup:'C:/private'},'raw',{id:'cofpreview',args:{slot:0,reward:42,lens:7}});`);
 assert.equal(h.element('#result-dialog').open,true);
 assert.match(h.element('#result-text').innerHTML,/Unique weapons|Greater Lens|4 → 9|No changes/);
 assert.doesNotMatch(h.element('#result-text').innerHTML,/actorValues|private|<pre/);
});

test('player All stats never requires hidden developer key inputs',()=>{
 const h=automaticHarness();h.run(`activeStat={tab:'raw',key:{sp:22,tags:0,special:0,extra:0}};syncRawKey();`);
 assert.deepEqual(JSON.parse(h.run('JSON.stringify(activeStat.key)')),{sp:22,tags:0,special:0,extra:0});
 assert.equal(h.calls.length,0);
});

test('a confirmed save action never borrows the next character identity after an intervening refresh',async()=>{
 const h=actionHarness();
 h.context.testConfirm=async()=>{
  const other=h.connection('M_Rest');other.player.id='1';h.replies.push(other);
  await h.run('refreshConnection(true,false)');return true;
 };
 h.run('confirmAction=testConfirm');await h.run('formAction(testForm)');
 assert.deepEqual(h.calls,[{type:'connection'}]);
 assert.match(h.element('#notification').textContent,/Refresh live data|character changed/);
});

test('successful Apply clears only its original timeline draft, leaving the next timeline draft intact',async()=>{
 const h=actionHarness();h.context.testConfirm=async()=>{h.run('monoTarget.timeline=2');enterMonolithValue(h,'stability','450');return true;};
 h.run('confirmAction=testConfirm');h.replies.push({ok:true,text:'stability applied'});await h.run('formAction(testForm)');
 assert.equal(h.calls.length,1);assert.equal(h.calls[0].args.timeline,1);assert.equal(h.calls[0].args.value,300);
 assert.equal(h.run(`monoDrafts['0:1:normal:stability']`),undefined);
 assert.equal(h.run(`monoDrafts['0:2:normal:stability']`),'450');
});

test('a failed Apply keeps the original entered value for an explicit retry',async()=>{
 const h=actionHarness();h.run('confirmAction=async()=>true');h.replies.push({ok:false,error:'Game paused'});
 await h.run('formAction(testForm)');assert.equal(h.calls.length,1);
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'300');
});

test('a successful in-flight Apply does not discard a newer edit to the same value',async()=>{
 const h=actionHarness();h.run('confirmAction=async()=>true');
 let complete;h.replies.push(new Promise(resolve=>complete=resolve));
 const action=h.run('formAction(testForm)');await new Promise(setImmediate);
 assert.equal(h.calls.length,1);assert.equal(h.calls[0].args.value,300);
 enterMonolithValue(h,'stability','350');complete({ok:true,text:'300 applied'});await action;
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'350');
});

test('an unchanged focus read leaves all current form DOM and input focus intact; manual refresh still redraws',async()=>{
 const h=harness();h.run(`connected=connectionFixture('M_Rest');cache.monolith_read=monolithFixture('M_Rest',true);`);
 for(let n=0;n<3;n++){h.replies.push(h.connection('M_Rest'));await h.focus();}
 assert.equal(h.run('pageRenderCount'),0);
 h.replies.push(h.connection('M_Rest'));await h.run('refreshConnection()');assert.equal(h.run('pageRenderCount'),1);
});

test('focus still refreshes changed settings and changed areas while preserving pending Monolith values',async()=>{
 const h=harness();h.run(`connected=connectionFixture('M_Rest');cache.monolith_read=monolithFixture('M_Rest',true);monoTarget={timeline:1,difficulty:'normal'};values.density=appliedValues.density=1;`);
 enterMonolithValue(h,'stability','300');
 const changed=h.connection('M_Rest');changed.values.density=2;h.replies.push(changed);await h.focus();
 assert.equal(h.run('pageRenderCount'),1);assert.equal(h.run('values.density'),2);
 h.replies.push(h.connection('Echo'),h.monolith('Echo',false));await h.focus();
 assert.equal(h.run('cache.monolith_read.player.scene'),'Echo');assert.equal(h.button.disabled,true);
 assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),'300');
});

test('blank, zero, out-of-range and change-only edits survive loss of live bounds without becoming zero',()=>{
 for(const value of ['', '0', '900']){
  const h=harness();h.context.fullCatalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/catalog.json'),'utf8'));
  h.run(`catalog=fullCatalog;connected=connectionFixture('M_Rest');monoTarget={timeline:1,difficulty:'normal'};`);
  enterMonolithValue(h,'stability',value,'change');h.run('invalidateLive();renderMonolith();');
  assert.equal(h.run(`monoDrafts[monoDraftKey('stability')]`),value);
  assert.match(h.element('#page-content').innerHTML,new RegExp(`data-id="stability"[\\s\\S]*?value="${value}"`));
  const field=h.element('#page-content').innerHTML.match(/<input name="value"[^>]+>/g).at(-1);
  assert.doesNotMatch(field,/max="0"/);assert.equal(h.calls.length,0);
 }
});
