/* Look and controls shared by the small windows (loot feed, damage). The
   desktop window has no Windows frame: the bar at the top moves it, the grip
   in the corner resizes it, ⚙ opens its options and − hides it. Only the
   background is see-through; text keeps its full strength. The options are
   saved with the window's other preferences. */
(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.EpochOverlay=api;})(typeof window==='object'?window:globalThis,function(){
 'use strict';
 const defaults={background:'#091310',backgroundOpacity:85,text:'#eee6d5',fontSize:13,spacing:'normal',bar:'hover'};
 const spacings={compact:1,normal:4,roomy:8};
 const colour=(v,fallback)=>/^#[0-9a-f]{6}$/i.test(String(v))?String(v).toLowerCase():fallback;
 const whole=(v,low,high,fallback)=>{const n=Number(v);return v!==null&&v!==''&&Number.isFinite(n)?Math.min(high,Math.max(low,Math.round(n))):fallback;};
 // The previous windows faded as a whole (opacity 30–100); that value becomes
 // the background opacity.
 function normalise(o){
  o=o&&typeof o==='object'?o:{};
  return {
   background:colour(o.background,defaults.background),
   backgroundOpacity:whole(o.backgroundOpacity??o.opacity,0,100,defaults.backgroundOpacity),
   text:colour(o.text,defaults.text),
   fontSize:whole(o.fontSize,10,22,defaults.fontSize),
   spacing:Object.hasOwn(spacings,o.spacing)?o.spacing:defaults.spacing,
   bar:o.bar==='always'?'always':'hover'
  };
 }
 function rgba(hex,percent){const n=parseInt(hex.slice(1),16);return `rgba(${n>>16&255},${n>>8&255},${n&255},${(percent/100).toFixed(2)})`;}
 function variables(o){
  return {
   '--overlay-bg':rgba(o.background,o.backgroundOpacity),
   '--overlay-ink':o.text,
   '--overlay-font':o.fontSize+'px',
   '--overlay-gap':spacings[o.spacing]+'px',
   // Text over a see-through background gets a dark outline to stay readable.
   '--overlay-shadow':o.backgroundOpacity<60?'0 1px 2px #000,0 0 3px #000':'none'
  };
 }
 // New window size while the corner grip is dragged (logical pixels).
 function dragSize(start,from,to,minimum){return [Math.max(minimum[0],Math.round(start[0]+to[0]-from[0])),Math.max(minimum[1],Math.round(start[1]+to[1]-from[1]))];}
 function install(win,{options,save,minimum=[220,120],onChange=()=>{}}){
  const doc=win.document,$=s=>doc.querySelector(s);
  const api=()=>win.pywebview?.api;
  const apply=()=>{
   Object.assign(options,normalise(options));
   for(const [name,value] of Object.entries(variables(options)))doc.documentElement.style.setProperty(name,value);
   doc.body.classList.toggle('bar-always',options.bar==='always');
   for(const input of doc.querySelectorAll('[data-overlay]')){const v=options[input.dataset.overlay];if(input.value!==String(v))input.value=String(v);}
   const shown=$('[data-overlay-value]');if(shown)shown.textContent=options.backgroundOpacity+'%';
  };
  for(const input of doc.querySelectorAll('[data-overlay]')){
   input.addEventListener('input',()=>{const name=input.dataset.overlay;options[name]=input.type==='range'?Number(input.value):input.value;apply();save();onChange();});
  }
  const panel=$('.overlay-options');
  $('[data-overlay-options]')?.addEventListener('click',()=>{panel.hidden=!panel.hidden;doc.body.classList.toggle('options-open',!panel.hidden);});
  $('[data-overlay-hide]')?.addEventListener('click',()=>{const a=api();if(a?.hide_window)a.hide_window().catch(()=>{});else win.close();});
  const grip=$('.overlay-grip');
  if(grip)grip.addEventListener('pointerdown',e=>{
   const a=api();if(!a?.resize_window)return;
   e.preventDefault();grip.setPointerCapture(e.pointerId);
   const start=[win.innerWidth,win.innerHeight],from=[e.screenX,e.screenY];
   let wanted=null,sending=false;
   const send=async()=>{
    if(sending||!wanted)return;sending=true;const [w,h]=wanted;wanted=null;
    try{await a.resize_window(w,h);}catch{}
    sending=false;if(wanted)void send();
   };
   const move=ev=>{wanted=dragSize(start,from,[ev.screenX,ev.screenY],minimum);void send();};
   const stop=()=>{grip.removeEventListener('pointermove',move);grip.removeEventListener('pointerup',stop);grip.removeEventListener('pointercancel',stop);};
   grip.addEventListener('pointermove',move);grip.addEventListener('pointerup',stop);grip.addEventListener('pointercancel',stop);
  });
  // A browser tab has its own frame: no grip or hide button there.
  const desktop=()=>doc.body.classList.toggle('overlay-desktop',!!api()?.resize_window);
  win.addEventListener('pywebviewready',desktop);desktop();
  apply();
  return {apply};
 }
 return {defaults,spacings,normalise,rgba,variables,dragSize,install};
});
