const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const i18n=require('../ui/i18n.js');

test('exact texts keep their surrounding spaces',()=>{
 const t=i18n.translator({'Apply changes':'Değişiklikleri uygula'});
 assert.equal(t('Apply changes'),'Değişiklikleri uygula');
 assert.equal(t('  Apply   changes \n'),'  Değişiklikleri uygula \n');
 assert.equal(t('Unknown text'),null);
 assert.equal(t('1,234'),null,'numbers and symbols are never looked up');
});

test('templates fill their values, also in another word order',()=>{
 const t=i18n.translator({'{0} settings applied in order.':'{0} ayar sırayla uygulandı.','Move {0} to {1}':'{1} konumuna {0} taşı','Rule {0}':'Kural {0}'});
 assert.equal(t('3 settings applied in order.'),'3 ayar sırayla uygulandı.');
 assert.equal(t('Move Sword to Stash'),'Stash konumuna Sword taşı');
 assert.equal(t('Rule 45'),'Kural 45');
 assert.equal(t('Rules 45'),null);
});

test('a specific template wins over a general one',()=>{
 const t=i18n.translator({'{0} items':'{0} eşya','Show all {0} items':'Tüm {0} eşyaları göster'});
 assert.equal(t('Show all Unique items'),'Tüm Unique eşyaları göster');
});

test('regular-expression characters in English text are literal',()=>{
 const t=i18n.translator({'Multiplier (×{0}) + extra?':'Çarpan (×{0}) + ek?'});
 assert.equal(t('Multiplier (×5) + extra?'),'Çarpan (×5) + ek?');
});

test('a value that is itself interface text is translated too',()=>{
 const t=i18n.translator({'Applying {0} (1 setting).':'{0} uygulanıyor (1 ayar).','your last used settings':'Son kullandığınız ayarlar','Default: {0}':'Varsayılan: {0}','rank bonus':'rütbe bonusu'});
 assert.equal(t('Applying your last used settings (1 setting).'),'Son kullandığınız ayarlar uygulanıyor (1 ayar).');
 assert.equal(t('Applying Speed farm (1 setting).'),'Speed farm uygulanıyor (1 ayar).','a player-made name stays');
 assert.equal(t('Default: rank bonus'),'Varsayılan: rütbe bonusu');
});

test('Turkish and Russian write decimal values with a comma and translate the unit',()=>{
 const dictionary={'{0}–{1}{2} · Default: {3}':'{0}–{1}{2} · По умолчанию: {3}','s':'с','game units':'игровых ед.','Speed ×{0}':'Скорость ×{0}'};
 const ru=i18n.translator(dictionary,{decimalComma:true});
 assert.equal(ru('0.25–10 s · Default: 0.25'),'0,25–10 с · По умолчанию: 0,25');
 assert.equal(ru('0–60 game units · Default: 0'),'0–60 игровых ед. · По умолчанию: 0');
 assert.equal(ru('Speed ×1.5'),'Скорость ×1,5');
 assert.equal(ru('s'),'с','the unit after a number field');
 const en=i18n.translator(dictionary);
 assert.equal(en('0.25–10 s · Default: 0.25'),'0.25–10 с · По умолчанию: 0.25','without the option the number stays as written');
 assert.equal(ru('Speed ×1.2.3'),'Скорость ×1.2.3','only a plain decimal number changes');
 const source=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/i18n-source.json'),'utf8'));
 assert.ok(source.includes('s'),'the seconds unit is in the text list');
 for(const language of ['tr','ru']){
  const d=JSON.parse(fs.readFileSync(path.join(__dirname,`../ui/i18n-${language}.json`),'utf8'));
  assert.equal(d.s,language==='tr'?'sn':'с');
 }
});

test('lines and " · " parts are translated one by one when the whole is unknown',()=>{
 const t=i18n.translator({'Character: {0}':'Karakter: {0}','Loaded offline character':'Yüklü çevrimdışı karakter','{0} selected T7 affixes':'{0} seçili T7 affix','Midpoint rolls':'Orta değerler','No materials are consumed.':'Malzeme harcanmaz.','Tab {0}':'Sekme {0}'});
 assert.equal(t('Character: Loaded offline character\nWraith Dagger\n3 selected T7 affixes · 1 odd part · Midpoint rolls\n\nNo materials are consumed.'),
  'Karakter: Yüklü çevrimdışı karakter\nWraith Dagger\n3 seçili T7 affix · 1 odd part · Orta değerler\n\nMalzeme harcanmaz.');
 assert.equal(t('Stash · Tab 3 · (2, 5)'),'Stash · Sekme 3 · (2, 5)');
 assert.equal(t('Stash · (2, 5)'),null,'nothing known: left alone');
});

test('short templates take numbers only, so game names stay',()=>{
 const t=i18n.translator({'Level {0}':'Seviye {0}','Sort only:':'Yalnızca sıralama:'});
 assert.equal(t('Level 85'),'Seviye 85');
 assert.equal(t('Level of Fireball'),null);
 assert.equal(t('Sort only: Rule A, Rule B'),'Yalnızca sıralama: Rule A, Rule B');
});

test('an icon in front of a known text keeps its place',()=>{
 const t=i18n.translator({'Character':'Karakter','Scan collection':'Koleksiyonu tara'});
 assert.equal(t('♜ Character'),'♜ Karakter');
 assert.equal(t('↻ Scan collection'),'↻ Koleksiyonu tara');
 assert.equal(t('♜ Unknown'),null);
});

test('known sentences are found inside a longer paragraph',()=>{
 const t=i18n.translator({'Use a Windows font. Missing letters use the game font.':'Bir Windows yazı tipi kullanın. Eksik harfler oyunun yazı tipini kullanır.','The choice is undone when you close EpochPact.':'Seçim, EpochPact kapatıldığında geri alınır.'});
 assert.equal(t('Use a Windows font. Missing letters use the game font. The choice is undone when you close EpochPact.'),
  'Bir Windows yazı tipi kullanın. Eksik harfler oyunun yazı tipini kullanır. Seçim, EpochPact kapatıldığında geri alınır.');
 assert.equal(t('Unknown first. The choice is undone when you close EpochPact.'),'Unknown first. Seçim, EpochPact kapatıldığında geri alınır.');
 assert.equal(t('Unknown first. Unknown second.'),null);
});

test('the stored language is read safely',()=>{
 const store=v=>({getItem:()=>v});
 assert.equal(i18n.readLanguage(store('"tr"')),'tr');
 assert.equal(i18n.readLanguage(store('"ru"')),'ru');
 assert.equal(i18n.readLanguage(store(null)),'en');
 assert.equal(i18n.readLanguage(store('"de"')),'en');
 assert.equal(i18n.readLanguage(store('{bad')),'en');
});

for(const language of ['tr','ru']){
 test(`${language}: every interface text is translated with the same placeholders`,()=>{
  const source=JSON.parse(fs.readFileSync(path.join(__dirname,'../ui/i18n-source.json'),'utf8'));
  const dictionary=JSON.parse(fs.readFileSync(path.join(__dirname,`../ui/i18n-${language}.json`),'utf8'));
  const marks=s=>(s.match(/\{\d+\}/g)||[]).sort().join();
  const missing=source.filter(s=>typeof dictionary[s]!=='string'||!dictionary[s].trim());
  assert.deepEqual(missing,[],'run py -3 tools/i18n_check.py');
  const wrong=source.filter(s=>marks(s)!==marks(dictionary[s]));
  assert.deepEqual(wrong,[]);
  assert.deepEqual(Object.keys(i18n.languages),['en','tr','ru']);
 });
}

test('an attribute whose translation is its own English text is not written again, so the page never loops',()=>{
 const queue=[];let callback=null;
 const node=(attrs,children=[])=>{const el={nodeType:1,tagName:'SPAN',attrs,childNodes:children,parentElement:null,
  hasAttribute:n=>n in attrs,getAttribute:n=>attrs[n],setAttribute(n,v){attrs[n]=v;queue.push({type:'attributes',target:el,attributeName:n});}};
  for(const c of children)c.parentElement=el;return el;};
 const voidTile=node({title:'Void'}),fireTile=node({title:'Fire'}),root=node({},[voidTile,fireTile]);
 const win={document:{documentElement:root,readyState:'complete',title:'',addEventListener(){}},
  localStorage:{getItem:k=>k==='epochpact.language'?'"tr"':null},addEventListener(){},
  MutationObserver:class{constructor(cb){callback=cb;}observe(){}},
  XMLHttpRequest:class{open(){}send(){this.status=200;this.responseText=JSON.stringify({Void:'Void',Fire:'Ateş'});}}};
 i18n.install(win);
 assert.equal(fireTile.attrs.title,'Ateş');assert.equal(voidTile.attrs.title,'Void');
 // A script sets the English title again: translated once, then quiet.
 voidTile.setAttribute('title','Void');fireTile.setAttribute('title','Fire');
 let rounds=0;while(queue.length&&rounds<10){callback(queue.splice(0));rounds++;}
 assert.equal(queue.length,0,'no endless attribute rewrites');assert.ok(rounds<=2);
 assert.equal(fireTile.attrs.title,'Ateş');assert.equal(voidTile.attrs.title,'Void');
});
