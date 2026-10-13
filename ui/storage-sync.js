/* Keeps the interface's own choices (epochpact.* localStorage keys) in the
   app's preferences file, so a different interface port, an app update or a
   new WebView profile keeps profiles, last used settings, theme and window
   options. Loaded first and synchronously on every page: it fills
   localStorage from the file before any other script reads it, then mirrors
   later writes. Preview keys stay local. Nothing here is sent to the game. */
(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.EpochStorageSync=api;})(typeof window==='object'?window:globalThis,function(){
 'use strict';
 const prefix='epochpact.';
 const mirrored=key=>typeof key==='string'&&key.startsWith(prefix)&&!key.endsWith('.preview');
 // Server values win for their keys; local keys the file does not have yet
 // (first start after an update, or written while the app was unreachable)
 // are uploaded. Returns the changes to upload.
 function merge(storage,server){
  const upload={};
  for(const [key,value] of Object.entries(server||{}))if(mirrored(key)&&typeof value==='string'&&storage.getItem(key)!==value)storage.setItem(key,value);
  for(let i=0;i<storage.length;i++){const key=storage.key(i);if(mirrored(key)&&!Object.prototype.hasOwnProperty.call(server||{},key))upload[key]=storage.getItem(key);}
  return upload;
 }
 function install(win){
  const storage=win.localStorage,proto=win.Storage.prototype;
  const set=proto.setItem,remove=proto.removeItem;
  let token=null,pending={},timer=null;
  function flush(){
   clearTimeout(timer);timer=null;
   if(!token||!Object.keys(pending).length)return;
   const body=JSON.stringify({changes:pending});pending={};
   try{win.fetch('/api/preferences',{method:'POST',headers:{'Content-Type':'application/json','X-Epoch-Token':token},body,keepalive:true}).catch(()=>{});}catch{}
  }
  const queue=(key,value)=>{pending[key]=value;clearTimeout(timer);timer=setTimeout(flush,300);};
  let server=null;
  try{
   // Synchronous on purpose: other scripts read localStorage while they load.
   const request=new win.XMLHttpRequest();request.open('GET','/api/preferences',false);request.send();
   if(request.status===200)server=JSON.parse(request.responseText);
  }catch{}
  if(server?.ok){
   token=server.token;
   // Writes made by merge() itself must not be echoed back to the file.
   const upload=merge({getItem:k=>storage.getItem(k),setItem:(k,v)=>set.call(storage,k,v),get length(){return storage.length;},key:i=>storage.key(i)},server.values||{});
   for(const [key,value] of Object.entries(upload))queue(key,value);
  }
  proto.setItem=function(key,value){set.call(this,key,value);if(this===storage&&mirrored(key))queue(key,String(value));};
  proto.removeItem=function(key){remove.call(this,key);if(this===storage&&mirrored(key))queue(key,null);};
  win.addEventListener('pagehide',flush);
  return {connected:Boolean(server?.ok),flush};
 }
 const api={merge,mirrored,install};
 if(typeof window==='object'&&window.localStorage&&window.XMLHttpRequest)Object.assign(api,install(window));
 return api;
});
