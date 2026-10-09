const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');

function harness(initial={selected:false,installed:false,running:false,executable:''}){
 const listeners={},calls=[],changes=[];let shown=0,closed=0,busy=false;
 const input={value:'',defaultValue:''},panel={
  querySelector(){return input;},
  set innerHTML(value){this.html=value;const match=value.match(/value="([^"]*)"/);input.value=input.defaultValue=match?.[1]||'';}
 };
 const dialog={showModal(){shown++;},close(){closed++;}},feedback={classList:{toggle(){}}};
 const document={addEventListener(type,fn){listeners[type]=fn;},
  querySelector:s=>s==='#setup-dialog'?dialog:{insertAdjacentHTML(){}},
  querySelectorAll:s=>s==='[data-launcher-panel]'?[panel]:s==='[data-game-path]'?[input]:s==='[data-setup-feedback]'?[feedback]:[]};
 const window={},ctx=vm.createContext({window,document});
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../ui/launcher.js'),'utf8'),ctx);
 const api=window.EpochLauncher;
 api.init(initial,{isBusy:()=>busy,run:async payload=>{
  calls.push(JSON.parse(JSON.stringify(payload)));
  return {ok:true,launcher:{...initial,selected:true,executable:payload.args.executable??initial.executable},message:'Saved'};
 },changed:async reset=>changes.push(reset)});
 async function click(attribute){const button={disabled:false,hasAttribute:x=>x===attribute,closest:()=>panel};await listeners.click({target:{closest:()=>button}});}
 return {window,api,calls,changes,panel,input,feedback,click,
  type:value=>{input.value=value;listeners.input({target:{value,matches:()=>true}});},
  setBusy:v=>busy=v,get shown(){return shown;},get closed(){return closed;}};
}

test('first-run UI opens setup without installing, launching or submitting a setting',()=>{
 const h=harness();assert.equal(h.shown,1);assert.deepEqual(h.calls,[]);
 assert.match(h.panel.html,/Browse/);assert.match(h.panel.html,/Last Epoch.exe/);
});
test('dedicated Game setup navigation exposes Browse without submitting an action',()=>{
 const h=harness();h.api.mount('game_setup');
 assert.match(h.panel.html,/Game setup/);assert.match(h.panel.html,/Browse/);
 assert.equal(h.calls.length,0);
});
test('remembered installation skips first-run modal and never auto-launches',()=>{
 const h=harness({selected:true,installed:true,running:false,executable:'D:/Custom/Last Epoch.exe'});
 assert.equal(h.shown,0);assert.equal(h.calls.length,0);assert.match(h.panel.html,/D:\/Custom/);
});
test('cancelled native picker submits no job and cannot install anything',async()=>{
 const h=harness();h.window.pywebview={api:{choose_game_executable:async()=>null}};
 await h.click('data-game-choose');assert.equal(h.calls.length,0);assert.equal(h.api.browsing(),false);
});
test('native selection sends exactly one selection job then refreshes the interface',async()=>{
 const h=harness();h.window.pywebview={api:{choose_game_executable:async()=> 'D:/Other/Last Epoch.exe'}};
 await h.click('data-game-choose');
 assert.deepEqual(h.calls,[{type:'launcher',action:'select',args:{executable:'D:/Other/Last Epoch.exe'}}]);
 assert.equal(h.changes.length,1);assert.equal(h.api.browsing(),false);
 assert.equal(h.input.value,'D:/Other/Last Epoch.exe');
});
test('browser fallback explains manual path entry and installs only on its own button',async()=>{
 const h=harness();await h.click('data-game-choose');assert.match(h.feedback.textContent,/desktop app/);
 h.input.value='D:/Custom/Last Epoch.exe';await h.click('data-game-save');
 assert.equal(h.calls[0].action,'select');
 await h.click('data-game-install');assert.equal(h.calls[1].action,'install');
 assert.equal(h.calls.some(x=>x.action==='launch'),false);
});
test('setup controls do not overlap a running native operation',async()=>{
 const h=harness();h.setBusy(true);await h.click('data-game-install');await h.click('data-game-launch');
 assert.equal(h.calls.length,0);
});
test('a typed executable path survives background refresh and page remount',()=>{
 const h=harness();h.type('D:/Custom/Last Epoch.exe');
 h.api.update({selected:false,installed:false,running:false,executable:''});
 assert.equal(h.input.value,'D:/Custom/Last Epoch.exe');
 h.input.value=h.input.defaultValue='';h.api.mount('settings');
 assert.equal(h.input.value,'D:/Custom/Last Epoch.exe');
 assert.equal(h.calls.length,0);
});
test('successful selection replaces the draft with the validated location',async()=>{
 const h=harness();h.type('D:/Old draft/Last Epoch.exe');
 h.window.pywebview={api:{choose_game_executable:async()=> 'D:/New/Last Epoch.exe'}};
 await h.click('data-game-choose');
 h.api.update({selected:true,installed:false,running:false,executable:'D:/New/Last Epoch.exe'});
 assert.equal(h.input.value,'D:/New/Last Epoch.exe');
 assert.equal(h.calls.length,1);
});
