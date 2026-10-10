"use strict";
const collectionState={query:"",filter:"all",base:"all",page:0,selected:null};
const echoState={query:"",reward:"all",status:"all",selected:null};
const rewardNames=["Special Echo","Gold","Experience","Rare item","Rare items","Unique item","Unique items","Exalted item","Exalted items","Affix shards","Runes","Glyphs","Idols","Set items","Timeline key","Arena key","Woven Echoes","Dungeon keys","Memory Amber"];
function collectionPrefs(data){
 const key="epochpact.collection.v1."+(preview?"preview.":"")+(data?.scope||"none");
 let p;try{p=JSON.parse(localStorage.getItem(key)||"{}");}catch{}
 return {key,wishlist:new Set(Array.isArray(p?.wishlist)?p.wishlist:[]),protected:new Set(Array.isArray(p?.protected)?p.protected:[])};
}
function saveCollectionPrefs(p){
 try{localStorage.setItem(p.key,JSON.stringify({wishlist:[...p.wishlist],protected:[...p.protected]}));return true;}
 catch{notify("Could not save the local marker. Your game items are unchanged.",true);return false;}
}
function collectionSource(){return cache[page==="atlas"?"atlas_read":"stash_read"];}
function place(item){return item.source+(item.tab>=0?` · Tab ${item.tab+1}`:"")+` · (${item.x+1}, ${item.y+1})`;}
function lpLabel(item){return item.kind==="Legendary"?"Legendary · LP used":item.kind==="Set"?"Set item":item.weaversWill?`Weaver’s Will ${item.weaversWill}`:item.uniqueId!==null?`${item.lp} LP`:`${item.fp} FP`;}
function uniqueDetails(item){
 if(!item.uniqueMods?.length)return "";
 const number=v=>Number(v).toLocaleString("en-US",{maximumFractionDigits:2});
 return `<div class="unique-rolls"><small>Unique modifiers · actual game values</small>${item.uniqueMods.map(m=>`<li><div><span>${esc(clean(m.name)||"Unique effect")}${m.mode===1?" · Increased":m.mode===2?" · More":""}</span><strong>${number(m.value)}${m.percent?"%":""}</strong></div>${m.canRoll?`<small>Range ${number(m.min)}–${number(m.max)}${m.percent?"%":""}${m.lessIsBetter?" · Lower is better":""}</small><meter min="0" max="255" value="${m.roll}"></meter>`:"<small>Fixed modifier</small>"}</li>`).join("")}</div>`;
}
function itemDetails(item,prefs){
 return `<article class="copy-card ${prefs.protected.has(item.key)?"protected":""}"><div class="section-head"><h3>${esc(item.name)}</h3><button data-protect="${esc(item.key)}" aria-pressed="${prefs.protected.has(item.key)}" title="Local reminder; does not lock an item in game">${prefs.protected.has(item.key)?"◆ Protected":"◇ Mark protected"}</button></div><strong class="copy-value">${esc(lpLabel(item))}</strong><p class="muted">${esc(place(item))}${item.corrupted?" · Corrupted":""}</p><ul class="affix-list">${item.affixes.map(a=>`<li><div><span><b>T${a.tier}</b> ${esc(clean(a.name)||"Affix "+a.id)}${a.sealed?" · Sealed":""}</span><small>${Math.round(a.roll/255*100)}% roll</small></div><meter min="0" max="255" value="${a.roll}">${a.roll}</meter></li>`).join("")||'<li class="muted">No regular affixes.</li>'}</ul>${uniqueDetails(item)}<small class="muted">Roll % is position in the modifier’s range, not its stat value.</small></article>`;
}
function renderCollection(){
 const atlas=page==="atlas",id=atlas?"atlas_read":"stash_read",data=cache[id],p=collectionPrefs(data);
 content.innerHTML=`<section class="panel collection-browser"><div class="section-head"><div><span class="eyebrow">${atlas?"COLLECTION BOOK":"OWNED EQUIPMENT"}</span><h2>${atlas?"Unique Atlas":"Stash Assistant"}</h2></div><button class="primary" data-collection-refresh="${id}" data-control-id="${id}">↻ ${data?"Refresh collection":"Scan collection"}</button></div><p class="muted">${data?esc(data.player.name)+" · "+data.tabs+" stash tabs · ":""}Scans the loaded stash, bags and equipment. Includes the Forge and Eternity Cache. Other characters and stashes are outside this scan.</p>${data?`<div class="collection-summary"><div><strong>${atlas?data.owned:data.total}</strong><span>${atlas?"Owned types":"Equipment pieces"}</span></div><div><strong>${atlas?data.total-data.owned:data.duplicateGroups}</strong><span>${atlas?"Missing types":"Duplicate Unique groups"}</span></div><div><strong>${atlas?p.wishlist.size:p.protected.size}</strong><span>${atlas?"On your wishlist":"Local protection markers"}</span></div></div><div class="collection-toolbar"><label class="collection-search">Search ${atlas?"items":"groups"}<input id="collection-search" type="search" placeholder="Name, base type or affix…" value="${esc(collectionState.query)}"></label><label>Show<select id="collection-filter">${(atlas?[["all","All items"],["owned","Owned"],["missing","Missing"],["wishlist","Wishlist"],["set","Set items"]]:[["all","All equipment"],["duplicates","Duplicate Uniques"],["protected","Protected reminders"]]).map(([v,n])=>`<option value="${v}" ${collectionState.filter===v?"selected":""}>${n}</option>`).join("")}</select></label>${atlas?`<label>Base type<select id="collection-base"><option value="all">All types</option>${[...new Set(data.items.map(x=>x.baseType))].sort().map(n=>`<option ${collectionState.base===n?"selected":""}>${esc(n)}</option>`).join("")}</select></label>`:""}</div><div id="collection-results"></div>`:'<div class="empty collection-empty"><span>✦</span><h3>Your collection starts here</h3><p>Load an offline character, then scan. Nothing is sold, moved or changed.</p></div>'}</section>`;
 if(data)renderCollectionResults();updateDisabled();
}
function renderCollectionResults(){
 const atlas=page==="atlas",data=collectionSource();if(!data)return;
 const p=collectionPrefs(data),q=collectionState.query.toLowerCase().trim(),f=collectionState.filter;
 const source=atlas?data.items:data.groups;
 const rows=source.filter(x=>{
  const match=(x.name+" "+(x.baseType||"")+" "+(x.items||x.copies||[]).flatMap(i=>i.affixes.map(a=>a.name)).join(" ")).toLowerCase().includes(q);
  if(!match)return false;
  if(atlas)return (collectionState.base==="all"||x.baseType===collectionState.base)&&(f==="all"||f==="owned"&&x.count>0||f==="missing"&&!x.count||f==="wishlist"&&p.wishlist.has(x.id)||f==="set"&&x.set);
  return f==="all"||f==="duplicates"&&x.unique&&x.count>1||f==="protected"&&x.items.some(i=>p.protected.has(i.key));
 }).sort((a,b)=>a.name.localeCompare(b.name,"en"));
 const size=atlas?24:16,max=Math.max(0,Math.ceil(rows.length/size)-1);collectionState.page=Math.min(collectionState.page,max);
 const slice=rows.slice(collectionState.page*size,(collectionState.page+1)*size);
 const selection=source.find(x=>(atlas?String(x.id):x.key)===collectionState.selected);
 $("#collection-results").innerHTML=`<div class="stat-results-head"><span>${rows.length} ${atlas?"types":"groups"} · Snapshot${preview?" · Preview":""}</span><small>Refresh after moving or acquiring items.</small></div><div class="atlas-grid">${slice.map(x=>atlas?`<article class="atlas-card ${x.count?"owned":"missing"}"><div class="section-head"><span class="badge">${x.set?"Set":"Unique"} · ${esc(x.baseType)}</span><button class="wishlist-button" data-wishlist="${x.id}" aria-label="${p.wishlist.has(x.id)?"Remove from":"Add to"} wishlist: ${esc(x.name)}" aria-pressed="${p.wishlist.has(x.id)}">${p.wishlist.has(x.id)?"★":"☆"}</button></div><h3>${esc(x.name)}</h3><div class="atlas-status">${x.count?`<span class="owned-label">✓ ${x.count} owned</span><strong>${x.set?"Set item":x.bestLP===null?"Legendary / Weaver":x.bestLP+" LP best"}</strong>`:'<span class="muted">Not in this scan</span>'}</div><div class="atlas-bottom"><small>Level ${x.level} · ${x.randomDrop?"Random drop eligible":"Special acquisition"}</small>${x.count?`<button data-collection-select="${x.id}">View copies →</button>`:""}</div></article>`:`<button class="stash-group ${collectionState.selected===x.key?"selected":""}" data-collection-select="${esc(x.key)}"><span class="badge">${x.unique?"Unique / Set copies":"Same base comparison"}</span><h3>${esc(x.name)}</h3><span>${x.count} ${x.count===1?"copy":"copies"}${x.items.some(i=>p.protected.has(i.key))?" · ◆ Protected":""}</span></button>`).join("")||'<p class="empty">No matches. Try a different filter.</p>'}</div><div class="pagination"><button data-collection-page="-1" ${collectionState.page===0?"disabled":""}>← Previous</button><span>Page ${collectionState.page+1} of ${max+1}</span><button data-collection-page="1" ${collectionState.page===max?"disabled":""}>Next →</button></div>${selection?`<section id="copy-comparison" class="copy-comparison"><div class="section-head"><h2>${esc(selection.name)}</h2><button data-collection-close>Close comparison</button></div><p class="muted">No universal “best” score: compare LP and the modifiers your build needs. Protection is an EpochPact reminder and does not prevent selling in game.</p><div class="copy-grid">${[...(selection.items||selection.copies)].sort((a,b)=>b.lp-a.lp).map(i=>itemDetails(i,p)).join("")}</div></section>`:""}`;
}
async function refreshCollection(id){
 const r=await runTask({type:"control",id,operation:"read"},{silent:true});
 if(!r){delete cache[id];if(page==="atlas"||page==="stash")renderCollection();return;}
 const peer=id==="atlas_read"?"stash_read":"atlas_read";
 if(cache[peer]&&(cache[peer].scope!==r.scope||cache[peer].revision!==r.revision||cache[peer].player.id!==r.player.id))delete cache[peer];
 cache[id]=r;collectionState.selected=null;collectionState.page=0;
 if(page==="atlas"||page==="stash")renderCollection();
}
function navigatorPanel(){
 const data=cache.echo_read,m=cache.monolith_read;
 const current=data&&data.timeline===Number(monoTarget.timeline)&&data.difficulty===(monoTarget.difficulty==="empowered"?1:0)&&data.player.id===m?.player?.id;
 return `<section class="panel echo-navigator"><div class="section-head"><div><span class="eyebrow">REWARD FINDER</span><h2>Monolith Navigator</h2></div><button data-echo-refresh data-control-id="echo_read" ${!m?.player?.id?"data-unavailable=true":""}>↻ Read Echo web</button></div><p class="muted">Search the selected timeline’s existing web. “Show in game” focuses the normal map; it never starts an Echo.</p>${current?data.generated?`<div class="collection-toolbar"><label class="collection-search">Search rewards or Echo names<input id="echo-search" type="search" placeholder="Example: Unique, Exalted, Arena…" value="${esc(echoState.query)}"></label><label>Reward<select id="echo-reward"><option value="all">Any reward</option>${[...new Set(data.echoes.map(e=>e.rewardType))].sort((a,b)=>a-b).map(n=>`<option value="${n}" ${echoState.reward===String(n)?"selected":""}>${esc(rewardNames[n]||"Special reward")}</option>`).join("")}</select></label><label>State<select id="echo-status">${[["all","All existing Echoes"],["ready","Runnable"],["pending","Uncompleted"],["completed","Completed"]].map(([v,n])=>`<option value="${v}" ${echoState.status===v?"selected":""}>${n}</option>`).join("")}</select></label></div><div id="echo-results"></div>`:'<div class="empty">No web exists for this timeline yet. Open it normally in game, then read again.</div>':'<div class="empty">Read the Echo web to search rewards. Read requests do not generate new nodes.</div>'}</section>`;
}
function renderEchoResults(){
 const data=cache.echo_read,box=$("#echo-results");if(!data||!box)return;
 const q=echoState.query.trim().toLowerCase();
 const rows=data.echoes.filter(e=>(e.name+" "+e.reward+" "+(rewardNames[e.rewardType]||"")).toLowerCase().includes(q)&&(echoState.reward==="all"||String(e.rewardType)===echoState.reward)&&(echoState.status==="all"||echoState.status==="ready"&&e.runnable||echoState.status==="pending"&&!e.completed||echoState.status==="completed"&&e.completed));
 const hits=new Set(rows.map(e=>e.index)),coords=new Map(data.echoes.map(e=>[e.index,e]));
 const xs=data.echoes.map(e=>e.x),ys=data.echoes.map(e=>e.y),minX=Math.min(...xs)-35,minY=Math.min(...ys)-35;
 const width=Math.max(100,Math.max(...xs)-minX+35),height=Math.max(100,Math.max(...ys)-minY+35);
 const edges=new Set();let lines="";
 for(const e of data.echoes)for(const i of e.connections){const other=coords.get(i),key=[e.index,i].sort((a,b)=>a-b).join(":");if(other&&!edges.has(key)){edges.add(key);lines+=`<line x1="${e.x}" y1="${-e.y}" x2="${other.x}" y2="${-other.y}"/>`;}}
 box.innerHTML=`<div class="stat-results-head"><span>${rows.length} matches / ${data.echoes.length} existing Echoes</span><small>Bright nodes match your filters.</small></div>${data.echoes.length?`<svg class="echo-map" viewBox="${minX} ${-minY-height} ${width} ${height}" role="img" aria-label="Existing Echo web; matching rewards are highlighted"><g class="echo-edges">${lines}</g>${data.echoes.map(e=>`<g class="echo-node ${hits.has(e.index)?"match":""} ${echoState.selected===e.index?"selected":""}"><circle cx="${e.x}" cy="${-e.y}" r="${echoState.selected===e.index?11:7}"/><title>${esc(e.name)} · ${esc(e.reward||rewardNames[e.rewardType])}</title></g>`).join("")}</svg>`:""}<div class="echo-grid">${rows.slice(0,200).map(e=>`<article class="echo-card ${echoState.selected===e.index?"selected":""}"><span class="badge">${e.completed?"Completed":e.runnable?"Runnable":"Not currently reachable"}</span><h3>${esc(e.reward||rewardNames[e.rewardType]||"Special Echo")}</h3><p class="muted">${esc(e.name)} · ${e.stability} base stability</p><div class="actions"><button data-echo-highlight="${e.index}">Highlight</button><button data-echo-focus="${e.index}" data-control-id="echo_focus" data-unavailable="${cache.monolith_read?.editable!==true}">Show in game →</button></div></article>`).join("")||'<p class="empty">No reward matches these filters.</p>'}</div>${rows.length>200?'<p class="muted">First 200 matches shown. Narrow your search to see a smaller list.</p>':""}`;
 updateDisabled();
}
async function readEchoes(){
 const r=await runTask({type:"control",id:"echo_read",operation:"read",args:{timeline:Number(monoTarget.timeline),difficulty:monoTarget.difficulty,expected_id:cache.monolith_read?.player?.id}},{silent:true});
 if(!r)delete cache.echo_read;else cache.echo_read=r;
 if(page==="monolith")renderPage();
}
document.addEventListener("click",async e=>{
 const b=e.target.closest("button");if(!b||b.disabled)return;
 if(b.dataset.collectionRefresh){await refreshCollection(b.dataset.collectionRefresh);return;}
 const data=collectionSource(),prefs=collectionPrefs(data);
 if(b.dataset.wishlist!==undefined){const id=Number(b.dataset.wishlist);prefs.wishlist.has(id)?prefs.wishlist.delete(id):prefs.wishlist.add(id);if(saveCollectionPrefs(prefs))renderCollection();}
 else if(b.dataset.protect){const key=b.dataset.protect;prefs.protected.has(key)?prefs.protected.delete(key):prefs.protected.add(key);if(saveCollectionPrefs(prefs))renderCollection();}
 else if(b.dataset.collectionSelect!==undefined){collectionState.selected=b.dataset.collectionSelect;renderCollectionResults();$("#copy-comparison")?.scrollIntoView({block:"start",behavior:"smooth"});}
 else if(b.hasAttribute("data-collection-close")){collectionState.selected=null;renderCollectionResults();}
 else if(b.dataset.collectionPage){collectionState.page+=Number(b.dataset.collectionPage);renderCollectionResults();}
 else if(b.hasAttribute("data-echo-refresh"))await readEchoes();
 else if(b.dataset.echoHighlight!==undefined){echoState.selected=Number(b.dataset.echoHighlight);renderEchoResults();$(".echo-map")?.scrollIntoView({block:"start",behavior:"smooth"});}
 else if(b.dataset.echoFocus!==undefined){const d=cache.echo_read;if(!d)return;const r=await runTask({type:"control",id:"echo_focus",args:{timeline:d.timeline,difficulty:d.difficulty?"empowered":"normal",index:Number(b.dataset.echoFocus),expected_id:d.player.id}},{silent:true});if(r){echoState.selected=r.focused;renderEchoResults();notify("Echo focused in the game map. It has not been started.");}}
});
document.addEventListener("input",e=>{
 if(e.target.id==="collection-search"){collectionState.query=e.target.value;collectionState.page=0;renderCollectionResults();}
 if(e.target.id==="echo-search"){echoState.query=e.target.value;renderEchoResults();}
});
document.addEventListener("change",e=>{
 if(e.target.id==="collection-filter"){collectionState.filter=e.target.value;collectionState.page=0;renderCollectionResults();}
 if(e.target.id==="collection-base"){collectionState.base=e.target.value;collectionState.page=0;renderCollectionResults();}
 if(e.target.id==="echo-reward"){echoState.reward=e.target.value;renderEchoResults();}
 if(e.target.id==="echo-status"){echoState.status=e.target.value;renderEchoResults();}
});
