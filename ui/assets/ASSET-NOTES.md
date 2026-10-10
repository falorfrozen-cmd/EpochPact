# UI assets

Chronoforge and Void Atlas use the user's approved concepts from
`Hero Siege/source/Last-Epoch-UI-Concepts/2026-10-05/`.
Reference images are retained as theme-picker thumbnails and isolated decorative
crest/icon crops. UI text, buttons, sliders and panels are real HTML controls.

`chronoforge-art.png` and `void-atlas-art.png` are generated scene-only derivatives
of those references, produced with `image_gen.imagegen` on 2026-10-08.
They remove all UI/text from the environment. Exact prompts: `art-prompts.json`.
No animation or per-frame image work is performed by the interface.

`inter.ttf` is copied from the installed Inter font package. Its license is
included unchanged as `inter-OFL.txt`.
