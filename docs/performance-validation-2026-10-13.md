# Nearby interactions and graphics switches — performance (2026-10-13)

Player report: World → Nearby interactions (chests, shrines, portals) "freezes"
and Display → Graphics effects "stutter every 1–2 seconds"; switching off
shadows alone was fine. A second player found chest/shrine opening "a bit
problematic".

Steam Last Epoch 1.5.2.1, offline, on fresh isolated copies of the first
character (EpFeedbackTest), in Z52, Z32 and Z72. Every session restored and
byte-verified the 54 save files and the installed runtime; the real character
was never loaded. This PC runs the game at a steady 13.3 ms per frame (75 fps).

## Measured before the change

- **Steady play** (standing and moving between spawners, each switch on its own,
  all switches together, with the desktop app's polling simulated: status,
  session, identity, stats every second, loot feed every 2 s, damage window
  every second, World page every 5 s): no extra slow frames. The app's reads cost
  under 0.4 ms of a game frame; the once-a-second graphics pass 2–7 µs.
- **Every scene search** (`FindObjectsOfType` for a game script type) cost
  23–30 ms of one frame. After each zone change the mod ran 12 of them in the
  first ~12 s (six for chests/shrines/portals/caves, six for post-processing
  volumes and fog cameras): five or six frames of 34–58 ms after loading.
- **Condition handlers:** a zone load queues 200–450 of them; one was sorted
  per 50 ms tick and nearby objects were not checked until the queue was empty —
  about 13 s after every zone change in which chests and shrines were not
  opened.
- **A shrine's own activation** costs the game about 72 ms. An object the game
  kept offering after a trigger was triggered again every 2 s indefinitely.

Nothing in the measured steady state stutters on this PC; the costs above are
the ones that scale with a slower CPU or a busier zone.

## Changes

- Graphics switches read the game's own `PostProcessManager.m_Volumes` list and
  `HxVolumetricCamera.Active` once a second (about 0.1 ms; maximum seen 0.4 ms)
  instead of scene searches. Reflection probes are searched once, two seconds
  after a zone loads (0.05–0.09 ms — native components are cheap to find). The
  shadow quality is written only when it differs.
- Interaction objects of a new zone are registered by the existing Awake and
  initialisation hooks while it loads; a zone change only drops the previous
  zone's objects. The six searches run once, when a switch is turned on.
- Condition handlers are sorted 32 (or 1 ms) per tick.
- An object still offered after a trigger is retried after 2, 4 and 8 s, then
  left alone (`rules::TriggerRetryMs`, unit tested).

## Measured after the change

| Zone change (all switches on) | Scene searches | Extra slow frames after load | Handler queue empty | Objects registered |
|---|---|---|---|---|
| Z52 → Z32 | 0 + 1 probe search | none | < 0.6 s (398 handlers) | — (none in zone) |
| Z32 → Z52 | 0 + 1 | none | < 0.6 s (200) | 2 of 2 shrines |
| Z52 → Z72 | 0 + 1 | none | < 0.6 s (454) | 1 of 1 chest |

After every zone change bloom, ambient occlusion, reflections and fog stayed
off and shadows returned to the game's value when switched off. A shrine next
to the character was used 0.08 s after arriving. Faults: 0.

## Automated

Native 19/19 programs, including the new classify budget and retry rules.
Test-only additions: a frame timeline (`frames-start`/`frames-stop` with slow
frames, scene changes, searches and triggers), `interaction-audit` and
`handler-queue`.
