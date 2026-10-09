/* Presentation only. Native stat keys, modes and catalog bounds stay unchanged. */
(function(root, factory) {
 const api=factory();
 if(typeof module==="object"&&module.exports)module.exports=api;
 else root.EpochStats=api;
})(typeof window!=="undefined"?window:this,function(){
 "use strict";
 const keyId=key=>[key.sp,key.tags,key.special,key.extra].join(":");
 const words=s=>String(s).replace(/([a-z\d])([A-Z])/g,"$1 $2").replace(/([A-Z])([A-Z][a-z])/g,"$1 $2").trim();
 function name(row,tab){
  if(tab==="sheet"){
   const n=row.objectName.replace(/Stat(?: \(\d+\))?$/," ").replace(/Value$/," ");
   return ({Int:"Intelligence",Vit:"Vitality",str:"Strength",MeleeAtackSpeed:"Melee attack speed"})[n.trim()]||words(n);
  }
  if(tab==="raw")return row.name==="Movespeed"?"Movement speed":words(row.name);
  let n=words(row.key.property).replace(/^Increased /,"").replace(/\bMovespeed\b/,"Movement speed");
  if(row.name==="dotdamage")return "Damage over time";
  const prefix={melee:"Melee",bow:"Bow",throwing:"Throwing",spell:"Spell",dot:"Damage over time",minion:"Minion"};
  const elements={fire:"Fire",cold:"Cold",lightning:"Lightning",physical:"Physical",void:"Void",necrotic:"Necrotic",poison:"Poison"};
  for(const [key,label] of Object.entries(prefix))if(row.name.startsWith(key))return label+" "+n.toLowerCase();
  for(const [key,label] of Object.entries(elements))if(row.name.startsWith(key)&&row.name!==key)return label+" "+n.toLowerCase();
  return n;
 }
 function unit(stats,key,mode,alias){
  const fraction=alias?alias.unit==="fraction":mode!=="added"||stats.aliases.some(a=>a.sharedKey===keyId(key)&&a.mode==="added"&&a.unit==="fraction");
  return {unit:fraction?"percent":"raw",scale:fraction?100:1,suffix:fraction?(mode==="added"?" pp":"%"):"",points:fraction&&mode==="added"};
 }
 function preferredMode(stats,row,key,tab){
  if(tab==="aliases")return row.mode;
  const aliases=stats.aliases.filter(a=>a.sharedKey===keyId(key));
  if(aliases.some(a=>a.mode==="added"&&a.unit==="fraction"))return "added";
  if(row.displayMetadata?.ignoreAdded||aliases.some(a=>a.mode==="increased")||/^(CriticalChance|CriticalMultiplier|AttackSpeed|CastSpeed|Movespeed)$/.test(key.property||""))return "increased";
  return "added";
 }
 function bounds(stats,mode,info,key){
  const b=stats.modeLimits[mode];
  const max=info.scale===100?100:[7,8,10,11,17,18,76,92].includes(key.sp)?2000:[19,20,21,22,23,46].includes(key.sp)?200:1000;
  return {minimum:b.minimum*info.scale,maximum:b.maximum*info.scale,sliderMin:Math.max(b.minimum*info.scale,info.scale===100?-100:0),sliderMax:Math.min(max,b.maximum*info.scale),step:1};
 }
 function category(row,tab){
  const key=tab==="raw"?{sp:row.id,tags:0,property:row.name}:row.key;
  const n=(key.property||row.name||"").toLowerCase();
  if((key.tags&8192)||key.sp===61)return "Minions";
  if([19,20,21,22,23,46].includes(key.sp))return "Attributes";
  if(/resistance|penetration/.test(n))return "Resistances";
  if(/movespeed|cooldown|haste|experience|drop|area/.test(n))return "Movement & utility";
  if(/damagetaken|armour|dodge|ward|block|endurance|avoidance|critavoid|glancing|parry|thorns|potion/.test(n))return "Defence";
  if(/health|mana|regen|leech|healing/.test(n))return "Health & mana";
  if(/damage|attack|cast|critical|ailment|freeze|stunchance/.test(n))return "Offence";
  return "Other";
 }
 function favoriteId(row,tab){
  const key=tab==="raw"?{sp:row.id,tags:0,special:0,extra:0}:row.key;
  return tab+":"+keyId(key)+":"+(row.objectName||row.name);
 }
 function contributions(text){
  const m=String(text||"").match(/EpochPact added=([\d.eE+-]+) increased=([\d.eE+-]+) more=([\d.eE+-]+) attached=([01])/);
  if(m){const modes={added:Number(m[1]),increased:Number(m[2]),more:Number(m[3])};return Object.values(modes).every(Number.isFinite)?{modes,attached:m[4]==="1"}:null;}
  if(/EpochPact added=/.test(String(text||"")))return null;
  // Native read replies omit the owned-entry suffix when no mod entry exists.
  if(/(?:\bSP=\d+ \([^\n]*\) tags=-?\d+ special=\d+ extra=-?\d+: exact-key sum|\b(?:statraw|stat)\b)[^\n]*applicableMore=/.test(String(text||"")))return {modes:{added:0,increased:0,more:0},attached:true};
  return null;
 }
 function signed(value,suffix=""){return (value>0?"+":"")+Number(value).toLocaleString("en-US",{maximumFractionDigits:6})+suffix;}
 function appliedMode(current,modes,fallback){
  if(current?.modes[fallback])return fallback;
  return modes.find(mode=>current?.modes[mode])||fallback;
 }
 function bonusSummary(stats,key,current){
  return ["added","increased","more"].filter(mode=>current.modes[mode]).map(mode=>{
   const info=unit(stats,key,mode);
   return signed(current.modes[mode]*info.scale,info.suffix)+(mode==="increased"?" increased":mode==="more"?" more":"");
  }).join(" · ")||"No bonus";
 }
 return {keyId,name,unit,preferredMode,bounds,category,favoriteId,contributions,signed,appliedMode,bonusSummary};
});
