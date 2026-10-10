/* Local interface sessions. Accepted actions are never submitted again. */
(function(root,factory){
 const api=factory();
 if(typeof module==='object'&&module.exports)module.exports=api;
 else root.EpochSession=api;
})(typeof window==='object'?window:globalThis,function(){
 'use strict';
 function current(snapshot,connection,preview=false){
  return !!snapshot&&(preview||snapshot.player?.id!=null&&connection?.player?.id!=null&&!!connection?.connected&&!!connection?.offline&&
   connection.state==='InGame'&&!connection.transitioning&&
   String(snapshot.player?.id)===String(connection.player?.id));
 }
 function currentArea(snapshot,connection,preview=false){
  return current(snapshot,connection,preview)&&(preview||
   !!snapshot.player?.scene&&snapshot.player.scene===connection.player?.scene);
 }
 function saveActionReason(snapshot,connection,{preview=false,monolith=false}={}){
  if(!current(snapshot,connection,preview))return 'Refresh live data to enable this action for the loaded character.';
  if(monolith&&!currentArea(snapshot,connection,preview))return 'The area changed. Refresh live data before editing Monoliths.';
  if(monolith&&snapshot.editable!==true)return "Return to End of Time, Monolith Hub or Traveler's Rest to edit Monoliths, then refresh live data.";
  return '';
 }
 function create({fetch,sleep,now=Date.now,warn=()=>{},onSessionChanged=()=>{},isReadOnly}){
  let token;
  function setSession(boot){token=boot.token;}
  async function submit(payload){
   const response=await fetch('/api/jobs',{method:'POST',headers:{'Content-Type':'application/json','X-Epoch-Token':token},body:JSON.stringify(payload)});
   return {response,body:await response.json()};
  }
  async function run(payload){
   let submitted=await submit(payload);
   if(submitted.response.status===403&&submitted.body.error==='Invalid interface session.'){
    // The server's guard rejects this before queueing it. Refresh read requests
    // only; mutations require a new explicit click against fresh character data.
    const response=await fetch('/api/bootstrap');
    if(!response.ok)throw new Error('Could not reconnect the interface. Reload this window.');
    const boot=await response.json();setSession(boot);onSessionChanged(boot);
    if(!isReadOnly(payload))throw new Error('Interface reconnected. Refresh live data before trying this action again.');
    submitted=await submit(payload);
   }
   if(!submitted.response.ok||!submitted.body.ok)throw new Error(submitted.body.error||'Could not submit the request.');
   const started=now();let warned=false;
   for(;;){
    const response=await fetch('/api/jobs/'+submitted.body.job),status=await response.json();
    if(!response.ok)throw new Error(status.error||'Could not read operation status. The action was not submitted again.');
    if(status.done)return status.result;
    await sleep(now()-started<1000?50:150);
    if(now()-started>15000&&!warned){warned=true;warn(payload.type==='launcher'?
     'Waiting for game setup. Check for a Windows administrator prompt. The action will not be submitted again.':
     'Waiting for the game. The action is queued and will not be submitted again.');}
   }
  }
  return {run,setSession};
 }
 return {create,current,currentArea,saveActionReason};
});
