/* Release notes from GitHub (Markdown) as safe HTML. Text is escaped first;
   only headings, lists, bold, inline code and links to EpochPact's GitHub and
   Discord are formatted. Also the update dialog's progress wording. */
(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.EpochReleaseNotes=api;})(typeof window==='object'?window:globalThis,function(){
 'use strict';
 const escape=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
 const linkAllowed=url=>/^https:\/\/(github\.com\/falorfrozen-cmd\/EpochPact(\/|$)|discord\.gg\/)/.test(url);
 function inline(text){
  let s=escape(text);
  s=s.replace(/`([^`]+)`/g,'<code>$1</code>');
  s=s.replace(/\*\*(.+?)\*\*/g,'<b>$1</b>');
  // Escaped text cannot leave the attribute; the address is checked unescaped.
  s=s.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g,(m,label,url)=>linkAllowed(url.replace(/&amp;/g,'&'))?`<a href="${url}" target="_blank" rel="noopener">${label}</a>`:label);
  return s;
 }
 function render(markdown){
  const lines=String(markdown||'').replace(/\r\n?/g,'\n').split('\n');
  let html='',items=null,item=null,para=[];
  const endItem=()=>{if(item!==null){items.push(`<li>${inline(item)}</li>`);item=null;}};
  const endList=()=>{endItem();if(items){html+=`<ul>${items.join('')}</ul>`;items=null;}};
  const endPara=()=>{if(para.length){html+=`<p>${inline(para.join(' '))}</p>`;para=[];}};
  for(const raw of lines){
   const line=raw.trim();let m;
   if(!line){endPara();endList();continue;}
   if((m=/^(#{1,4})\s+(.*)$/.exec(line))){endPara();endList();const level=m[1].length+2;html+=`<h${level}>${inline(m[2])}</h${level}>`;continue;}
   if((m=/^[-*]\s+(.*)$/.exec(line))){endPara();endItem();items=items||[];item=m[1];continue;}
   if(item!==null&&/^\s{2,}/.test(raw)){item+=' '+line;continue;}  // a wrapped list item
   endList();para.push(line);
  }
  endPara();endList();
  return html;
 }
 function megabytes(bytes){return `${(Math.max(0,Number(bytes)||0)/1048576).toFixed(1)} MB`;}
 function progress(state){
  if(!state)return '';
  if(state.state==='downloading')return state.total?`Downloading… ${megabytes(state.received)} of ${megabytes(state.total)} (${Math.floor(100*state.received/state.total)}%)`:'Downloading…';
  if(state.state==='verifying')return 'Checking every file of the download…';
  if(state.state==='ready')return "Downloaded and checked: the download matches the SHA-256 GitHub records for this release, and every file matches the package's checksum list.";
  if(state.state==='installing')return 'Closing EpochPact to update…';
  if(state.state==='error')return state.error||'The update could not be prepared. Nothing was changed.';
  return '';
 }
 return {render,inline,progress,megabytes,linkAllowed};
});
