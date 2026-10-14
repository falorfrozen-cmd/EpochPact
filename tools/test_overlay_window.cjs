const test=require('node:test'),assert=require('node:assert/strict');
const overlay=require('../ui/overlay.js');

test('defaults: dark background at 85%, normal spacing, bar on hover',()=>{
 assert.deepEqual(overlay.normalise({}),overlay.defaults);
 assert.deepEqual(overlay.normalise(null),overlay.defaults);
});

test('the old whole-window opacity becomes the background opacity',()=>{
 assert.equal(overlay.normalise({opacity:40}).backgroundOpacity,40);
 assert.equal(overlay.normalise({opacity:40,backgroundOpacity:10}).backgroundOpacity,10,'a saved background opacity wins');
});

test('stored values are kept within their limits',()=>{
 const o=overlay.normalise({background:'red',text:'#ABCDEF',backgroundOpacity:140,fontSize:3,spacing:'huge',bar:'never'});
 assert.equal(o.background,overlay.defaults.background,'only #rrggbb colours');
 assert.equal(o.text,'#abcdef');
 assert.equal(o.backgroundOpacity,100);
 assert.equal(o.fontSize,10);
 assert.equal(o.spacing,'normal');
 assert.equal(o.bar,'hover');
 assert.equal(overlay.normalise({backgroundOpacity:0}).backgroundOpacity,0,'fully clear is allowed');
 assert.equal(overlay.normalise({backgroundOpacity:''}).backgroundOpacity,overlay.defaults.backgroundOpacity);
});

test('only the background is see-through; text gets an outline over a light background',()=>{
 assert.equal(overlay.rgba('#102030',50),'rgba(16,32,48,0.50)');
 const clear=overlay.variables(overlay.normalise({backgroundOpacity:20,fontSize:15,spacing:'compact'}));
 assert.equal(clear['--overlay-bg'],'rgba(9,19,16,0.20)');
 assert.equal(clear['--overlay-font'],'15px');
 assert.equal(clear['--overlay-gap'],'1px');
 assert.notEqual(clear['--overlay-shadow'],'none');
 assert.equal(overlay.variables(overlay.normalise({backgroundOpacity:90}))['--overlay-shadow'],'none');
});

test('the corner grip follows the pointer and never goes below the minimum size',()=>{
 assert.deepEqual(overlay.dragSize([380,520],[100,100],[160,90],[220,120]),[440,510]);
 assert.deepEqual(overlay.dragSize([380,520],[100,100],[-400,-900],[220,120]),[220,120]);
});
