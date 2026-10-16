// The damage window's compact styles: the numbers they show and the HTML they draw.
const test=require('node:test'),assert=require('node:assert/strict');
const S=require('../ui/damage-styles.js');
const at=(i,v)=>{const a=[0,0,0,0,0,0,0];a[i]=v;return a;};
const close=(actual,expected,message)=>assert.ok(Math.abs(actual-expected)<1e-9,`${message||''} ${actual} != ${expected}`);
// The damage state from the DPS meter brief (docs/dps-meter-design-handoff-2026-10-15/examples/backend-state.json).
const you={name:'You',live:1,total:48210,hits:211,maxHit:1830,dps10:702.4,dps60:611,dps600:507.5,dps3600:507.5,average:507.5,combatAverage:679,
 elements:{total:[0,30110,0,18100,0,0,0],hits:[0,140,0,98,0,0,0],maxHit:[0,1830,0,960,0,0,0],dps10:[0,450.1,0,252.3,0,0,0],dps60:[0,380,0,231,0,0,0],dps600:[0,316.9,0,190.6,0,0,0],dps3600:[0,316.9,0,190.6,0,0,0]}};
const skeleton={name:'Skeleton',live:4,total:21340,hits:431,maxHit:210,dps10:260,dps60:241.5,dps600:224.6,dps3600:224.6,average:224.6,combatAverage:300.6,
 elements:{total:at(0,21340),hits:at(0,431),maxHit:at(0,210),dps10:at(0,260),dps60:at(0,241.5),dps600:at(0,224.6),dps3600:at(0,224.6)}};
const allies=(({name,live,...rest})=>rest)(skeleton);
const backend={ok:true,active:true,elapsedSeconds:95,combatSeconds:71,hits:642,unsplitHits:0,hooked:true,types:[you,skeleton],allies};

test('the brief example totals 962.4 DPS over 10 s: the allies are not added again and ×4 is a count',()=>{
 const m=S.model(backend);
 close(m.total,962.4,'total');close(m.you.dps,702.4,'you');close(m.allyDps,260,'allies');
 const s=m.sources.find(x=>x.name==='Skeleton');
 assert.equal(s.live,4);close(s.dps,260,'live is not a multiplier');
 assert.equal(m.allies.length,1);assert.equal(m.metric,'dps10');assert.ok(m.hooked&&m.active&&!m.unsplit);
});

test('every averaging window uses its own numbers; Session and Combat divide each type total by that time',()=>{
 for(const metric of S.METRICS){
  const m=S.model(backend,{metric});
  close(m.total,you[metric]+skeleton[metric],metric);
  close(m.allyDps,skeleton[metric],metric+' allies');
 }
 const session=S.model(backend,{metric:'average'}),combat=S.model(backend,{metric:'combatAverage'});
 close(session.you.values[1],30110/95,'session fire');close(session.you.values[3],18100/95,'session lightning');
 close(combat.you.values[1],30110/71,'combat fire');close(combat.allyValues[0],21340/71,'combat allies physical');
 // No time yet: known zeros, never NaN or Infinity.
 const fresh=S.model({...backend,elapsedSeconds:0,combatSeconds:0},{metric:'combatAverage'});
 assert.deepEqual(fresh.you.values,[0,0,0,0,0,0,0]);
 const missing=S.model({...backend,elapsedSeconds:undefined},{metric:'average'});
 assert.deepEqual(missing.you.values,[0,0,0,0,0,0,0]);
 assert.equal(S.metricOf('dps9'),'dps10');assert.equal(S.metricOf('combatAverage'),'combatAverage');
});

test('choosing or hiding a source never changes the total',()=>{
 const all=S.model(backend);
 for(const hidden of [['You'],['Skeleton'],['You','Skeleton'],['__total']])close(S.model(backend,{hidden}).total,all.total,hidden.join());
 assert.equal(S.selectedSource(all,'Skeleton').name,'Skeleton');
 assert.ok(S.selectedSource(all,'Gone').you,'a source that left falls back to You');
 assert.ok(S.selectedSource(all,null).you);
 const noYou=S.model(backend,{hidden:['You']});
 assert.equal(S.selectedSource(noYou,null).name,'Skeleton','with You hidden the first shown source is used');
 // Drawing a selection keeps the same total.
 const view={style:'bars',selected:'Skeleton'};
 assert.equal(S.structureKey(all,view),S.structureKey(all,{...view,selected:null}),'selecting is not a layout change');
});

test('one damage type: each source and the total show only that type, and the total says so',()=>{
 const fire=S.model(backend,{element:1});
 close(fire.you.dps,450.1);assert.equal(fire.sources.find(s=>!s.you).dps,0);close(fire.total,450.1);close(fire.allyDps,0);
 assert.match(S.totalHtml(fire),/TOTAL DPS<\/span> · <span>Fire/);
 assert.match(S.totalHtml(S.model(backend)),/<span>TOTAL DPS<\/span><\/span>/);
 const physicalCombat=S.model(backend,{element:0,metric:'combatAverage'});
 close(physicalCombat.total,21340/71);
 for(const bad of [-1,7,'1',null,1.5])close(S.model(backend,{element:bad}).total,962.4,'anything else is all damage: '+bad);
});

test('the allies are summed here when the core sends no aggregate, with arbitrary sources',()=>{
 const wolf={...skeleton,name:'Wolf',live:2,dps10:40,elements:{...skeleton.elements,dps10:at(4,40)}};
 const m=S.model({...backend,allies:undefined,types:[you,skeleton,wolf]});
 close(m.allyDps,300);close(m.total,1002.4);
 assert.deepEqual(m.allyValues.map(v=>Math.round(v*10)/10),[260,0,0,0,40,0,0]);
 assert.deepEqual(m.allies.map(a=>a.name),['Skeleton','Wolf'],'largest ally first');
 // Allies only: no You row, still a total.
 const only=S.model({...backend,types:[wolf]});
 assert.equal(only.you,null);close(only.total,40);
 assert.ok(!S.html(only,{style:'split'}).includes('data-v="you"'),'Split Meter leaves out the You column');
});

test('broken or missing data shows dashes, never NaN, Infinity or undefined',()=>{
 for(const state of [null,undefined,{},{ok:false,error:'Load an offline character first.'},{ok:true,types:'x'}]){
  const m=S.model(state);assert.ok(m.empty);
  for(const style of S.STYLES.slice(0,3))assert.ok(!/NaN|Infinity|undefined/.test(S.html(m,{style})+S.totalHtml(m)));
 }
 assert.match(S.html(S.model({ok:false,error:'<b>x</b>'})),/&lt;b&gt;x&lt;\/b&gt;/);
 const broken={ok:true,active:false,elapsedSeconds:'?',types:[null,{name:''},{name:'You',dps10:'abc',elements:{dps10:'no'}},{name:'Wolf',live:'many',dps10:Infinity,elements:{dps10:[1,'x',null]}},{name:'Imp',dps10:true}]};
 const m=S.model(broken);
 assert.deepEqual(m.sources.map(s=>[s.name,s.dps,s.live]),[['You',null,0],['Wolf',null,0],['Imp',null,0]]);
 assert.equal(m.total,null);assert.equal(S.formatter('en').full(m.total),'—');
 assert.deepEqual(m.sources[1].values,[1,null,null,null,null,null,null]);
 for(const style of S.STYLES.slice(0,3))assert.ok(!/NaN|Infinity|undefined/.test(S.html(m,{style,roster:true})),style);
});

test('game names are escaped and left untranslated; known zero is not missing data',()=>{
 const odd={...skeleton,name:'<img src=x onerror=alert(1)>',live:0};
 const m=S.model({...backend,types:[you,odd]});
 for(const style of ['quiet','bars','split']){
  const html=S.html(m,{style,roster:true});
  assert.ok(html.includes('&lt;img src=x onerror=alert(1)&gt;'),style);assert.ok(!html.includes('<img src=x'),style);
  assert.ok(html.includes('data-no-translate>&lt;img'),style);
 }
 close(m.sources[1].dps,260,'no live allies left: the recorded damage stays');
 assert.ok(!S.html(m,{style:'bars'}).includes('×0'));
 assert.ok(S.html(S.model(backend),{style:'bars'}).includes('×4'));
});

test('numbers are written the way the window language writes them',()=>{
 const en=S.formatter('en'),tr=S.formatter('tr'),ru=S.formatter('ru');
 assert.equal(en.full(54240),'54,240');assert.equal(tr.full(54240),'54.240');assert.equal(ru.full(54240).replace(/\s/g,' '),'54 240');
 assert.equal(en.short(18420),'18.4k');assert.equal(tr.short(18420),'18,4k');assert.equal(en.short(950),'950');assert.equal(en.short(2.5e6),'2.50M');assert.equal(en.short(472930),'473k');assert.equal(en.short(999999),'1.00M');assert.equal(en.short(1.25e9),'1.25B');assert.equal(en.short(999.4),'999');
 assert.equal(en.full(12345678),'12.3M');assert.equal(en.full(4.2e9),'4.20B');assert.equal(en.full(2898628),'2.90M');assert.equal(en.full(236e6),'236M');assert.equal(en.full(999999),'999,999');assert.equal(en.full(999.6e6),'1.00B');assert.equal(tr.full(2898628),'2,90M');assert.equal(en.full(-2.5e6),'-2.50M');assert.equal(en.full(0),'0');
 for(const bad of [null,undefined,NaN,Infinity])assert.equal(en.full(bad),'—');
});

test('styles and their layout: the layout key changes only when the layout does',()=>{
 const m=S.model(backend);
 const quiet=S.html(m,{style:'quiet'});
 assert.ok(quiet.includes('dm-quiet')&&quiet.includes('data-roster')&&quiet.includes(' hidden>'),'Quiet HUD: allies collapsed');
 assert.ok(!quiet.includes('data-src="You"'),'the roster lists the allies');
 assert.ok(S.html(m,{style:'quiet',roster:true}).includes('aria-expanded="true"'));
 assert.equal((S.html(m,{style:'bars'}).match(/data-src=/g)||[]).length,2,'Pact Bars: every source');
 assert.equal((S.html(m,{style:'bars'}).match(/data-tv=/g)||[]).length,7,'the seven damage types');
 assert.ok(S.html(m,{style:'split'}).includes('data-v="inspectlines"'));
 assert.equal(S.styleOf('nope'),'quiet');
 const key=S.structureKey(m,{style:'quiet'});
 const later=S.model({...backend,types:[{...you,dps10:900},{...skeleton,dps10:10}]});
 assert.equal(S.structureKey(later,{style:'quiet'}),key,'new numbers keep the layout');
 assert.notEqual(S.structureKey(m,{style:'quiet',roster:true}),key);
 assert.notEqual(S.structureKey(m,{style:'bars'}),key);
 assert.notEqual(S.structureKey(S.model({...backend,types:[you]}),{style:'quiet'}),key,'a source left');
 assert.notEqual(S.structureKey(S.model({...backend,types:[you,{...skeleton,live:5}]}),{style:'quiet'}),key,'a different count');
 const withIcon=S.model({...backend,types:[{...you,icon:'c:1:0'},skeleton]});
 const icons=new Map();const before=S.structureKey(withIcon,{style:'quiet',icons});
 icons.set('c:1:0','data:image/png;base64,AAAA');
 assert.notEqual(S.structureKey(withIcon,{style:'quiet',icons}),before,'an icon arrived');
 assert.ok(S.html(withIcon,{style:'bars',icons}).includes('<img class="dm-src-ico" src="data:image/png;base64,AAAA"'));
 assert.ok(S.html(m,{style:'bars'}).includes('dm-dot'),'no icon: a plain marker');
 assert.ok(S.html(S.model({...backend,unsplitHits:3}),{style:'quiet'}).includes('data-unsplit'));
 assert.equal(S.model({...backend,unsplitHits:3}).unsplit,true);
 assert.equal(S.model({...backend,hooked:false}).hooked,false);
 assert.match(S.html(S.model(backend,{hidden:['You','Skeleton']}),{style:'quiet'}),/Every row is hidden/);
});

test('sources show the game\'s own name, keyed by the object name for selection and hiding',()=>{
 const m=S.model({...backend,types:[you,{...skeleton,name:'SummonedSkeletonArcher',label:'Skeleton <Archer>'}]},{hidden:[]});
 const ally=m.sources[1];
 assert.equal(ally.key,'SummonedSkeletonArcher');assert.equal(ally.label,'Skeleton <Archer>');
 for(const style of ['quiet','bars','split']){
  const html=S.html(m,{style,roster:true});
  assert.ok(html.includes('data-src="SummonedSkeletonArcher"'),style);assert.ok(html.includes('Skeleton &lt;Archer&gt;'),style);assert.ok(!html.includes('Skeleton <Archer>'),style);
 }
 assert.equal(S.model({...backend,types:[you,{...skeleton,name:'PrimalWolf'}]}).sources[1].label,'Primal Wolf','no game name: the object name made readable');
 assert.ok(S.model({...backend,types:[you,{...skeleton,label:'Skeleton'}]},{hidden:['Skeleton']}).sources[1].hidden,'hiding uses the object name');
 const named=S.structureKey(S.model({...backend,types:[you,{...skeleton,label:'Skeleton Warrior'}]}),{style:'bars'});
 assert.notEqual(named,S.structureKey(S.model(backend),{style:'bars'}),'a name that arrives later redraws the rows');
});
