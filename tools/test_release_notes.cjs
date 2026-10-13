const test=require('node:test'),assert=require('node:assert/strict');
const notes=require('../ui/release-notes.js');

test('release notes keep the structure of the patch notes',()=>{
 const html=notes.render('# EpochPact 0.1.2-dev.22\n\nPlayer feedback.\n\n## Fixes\n\n- **Tier colours** now reach every\n  affix line (`T6`).\n- Second item\n\nUpdating text\nthat wraps.');
 assert.equal(html,'<h3>EpochPact 0.1.2-dev.22</h3><p>Player feedback.</p><h4>Fixes</h4><ul><li><b>Tier colours</b> now reach every affix line (<code>T6</code>).</li><li>Second item</li></ul><p>Updating text that wraps.</p>');
});
test('release notes cannot inject markup or link anywhere but EpochPact',()=>{
 const html=notes.render('<img src=x onerror=alert(1)> <script>x</script>\n\n[ok](https://github.com/falorfrozen-cmd/EpochPact/blob/v1/docs/a.md) [chat](https://discord.gg/q6aexZZSAf) [bad](https://evil.example/x) [js](javascript:alert(1)) [look-alike](https://github.com/falorfrozen-cmd/EpochPactEvil) [quote](https://github.com/falorfrozen-cmd/EpochPact/"onmouseover="x)');
 assert.ok(!/<img|<script/.test(html));
 assert.match(html,/&lt;img src=x onerror=alert\(1\)&gt;/);
 assert.equal((html.match(/<a /g)||[]).length,3);
 assert.match(html,/href="https:\/\/github.com\/falorfrozen-cmd\/EpochPact\/blob\/v1\/docs\/a.md"/);
 assert.match(html,/href="https:\/\/discord.gg\/q6aexZZSAf"/);
 assert.ok(!html.includes('evil.example"')&&!html.includes('href="javascript'));
 assert.ok(!html.includes('EpochPactEvil"'));
 assert.ok(!/href="[^"]*"onmouseover/.test(html));  // the quote stays escaped inside the address
});
test('progress wording for each state',()=>{
 assert.equal(notes.progress({state:'downloading',received:5*1048576,total:20*1048576}),'Downloading… 5.0 MB of 20.0 MB (25%)');
 assert.match(notes.progress({state:'verifying'}),/Checking every file/);
 assert.match(notes.progress({state:'ready'}),/SHA-256/);
 assert.equal(notes.progress({state:'error',error:'Network down.'}),'Network down.');
 assert.equal(notes.progress({state:'idle'}),'');
 assert.equal(notes.megabytes(27130630),'25.9 MB');
});
