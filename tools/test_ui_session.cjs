const test=require('node:test'),assert=require('node:assert/strict');
const {create,current,currentArea,saveActionReason}=require('../ui/session.js');
const reply=(status,body)=>({status,ok:status>=200&&status<300,json:async()=>body});
const expired=()=>reply(403,{ok:false,error:'Invalid interface session.'});
function harness(responses,readOnly){
 const calls=[],sessions=[];
 const transport=create({fetch:async(url,args)=>{calls.push({url,args});assert.ok(responses.length,'unexpected extra request');const value=responses.shift();if(value instanceof Error)throw value;return value;},sleep:async()=>{},isReadOnly:()=>readOnly,onSessionChanged:boot=>sessions.push(boot)});
 transport.setSession({token:'old'});return {transport,calls,sessions};
}
test('expired interface reconnects a read once, using the fresh token',async()=>{
 const h=harness([expired(),reply(200,{token:'new',preview:false}),reply(202,{ok:true,job:'new-job'}),reply(200,{done:true,result:{ok:true,player:{id:'6'}}})],true);
 assert.deepEqual(await h.transport.run({type:'connection'}),{ok:true,player:{id:'6'}});
 assert.equal(h.calls[0].args.headers['X-Epoch-Token'],'old');
 assert.equal(h.calls[2].args.headers['X-Epoch-Token'],'new');assert.equal(h.sessions.length,1);
});
test('expired mutation reconnects the session but requires a fresh explicit action',async()=>{
 const h=harness([expired(),reply(200,{token:'new'})],false);
 await assert.rejects(h.transport.run({type:'control',id:'campaign_complete'}),/Refresh live data/);
 assert.equal(h.calls.filter(c=>c.url==='/api/jobs').length,1);assert.equal(h.sessions.length,1);
});
test('lost response cannot establish rejection and never repeats a mutation',async()=>{
 const h=harness([new Error('network lost')],false);
 await assert.rejects(h.transport.run({type:'control',id:'cof_favor'}),/network lost/);
 assert.equal(h.calls.length,1);assert.equal(h.sessions.length,0);
});
test('accepted mutation is never resubmitted if a restarted server loses its job',async()=>{
 const h=harness([reply(202,{ok:true,job:'accepted'}),reply(404,{error:'Operation not found.'})],false);
 await assert.rejects(h.transport.run({type:'control',id:'campaign_complete'}),/Operation not found/);
 assert.equal(h.calls.filter(c=>c.url==='/api/jobs').length,1);assert.equal(h.sessions.length,0);
});
test('recovery cannot loop on a second invalid session or unrelated 403',async()=>{
 const h=harness([expired(),reply(200,{token:'new'}),expired()],true);
 await assert.rejects(h.transport.run({type:'connection'}),/Invalid interface session/);assert.equal(h.calls.length,3);
 const x=harness([reply(403,{error:'Cross-origin requests are not allowed.'})],true);
 await assert.rejects(x.transport.run({type:'connection'}),/Cross-origin/);assert.equal(x.calls.length,1);
});
test('a former character reading is current only for the connected loaded identity',()=>{
 const d={player:{id:'0'}},c={connected:true,offline:true,state:'InGame',transitioning:false,player:{id:'0'}};
 assert.equal(current(d,c),true);
 for(const changed of [{connected:false},{offline:false},{state:'Login'},{transitioning:true},{player:{id:'6'}},{player:null}])assert.equal(current(d,{...c,...changed}),false);
 assert.equal(current(null,c),false);assert.equal(current({},{}),false);
 assert.equal(current(d,{},true),true);
});

test('Monolith permissions from another area cannot enable or permanently disable a hub action',()=>{
 const c={connected:true,offline:true,state:'InGame',transitioning:false,player:{id:'0',scene:'M_Rest'}};
 const previous={player:{id:'0',scene:'Echo'},editable:false};
 assert.equal(current(previous,c),true);
 assert.equal(currentArea(previous,c),false);
 assert.match(saveActionReason(previous,c,{monolith:true}),/area changed/i);
 const fresh={player:{id:'0',scene:'M_Rest'},editable:true};
 assert.equal(currentArea(fresh,c),true);
 assert.equal(saveActionReason(fresh,c,{monolith:true}),'');
 const inEcho={...c,player:{id:'0',scene:'Echo'}};
 assert.match(saveActionReason(previous,inEcho,{monolith:true}),/End of Time/);
 assert.equal(currentArea(fresh,inEcho),false);
});

test('save readiness supports slot zero but rejects absent identity and transition snapshots',()=>{
 const c={connected:true,offline:true,state:'InGame',transitioning:false,player:{id:0,scene:'M_Rest'}};
 const d={player:{id:0,scene:'M_Rest'},editable:true};
 assert.equal(saveActionReason(d,c,{monolith:true}),'');
 for(const changed of [{player:{id:'1',scene:'M_Rest'}},{transitioning:true},{connected:false},{offline:false}]){
  assert.match(saveActionReason(d,{...c,...changed},{monolith:true}),/Refresh live data/);
 }
 assert.equal(currentArea({player:{id:0}},c),false);
 assert.equal(saveActionReason(null,c,{monolith:true}), 'Refresh live data to enable this action for the loaded character.');
 assert.equal(saveActionReason(d,{}, {preview:true,monolith:true}),'');
});
