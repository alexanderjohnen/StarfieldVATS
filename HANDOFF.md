# StarfieldVATS — Handoff (2026-08-29)

Read this first in a new chat. Point-in-time snapshot — verify against the
actual code and log before trusting anything here; offsets and "confirmed"
claims go stale. **If something here looks inconsistent with the code:**
check the public repo (https://github.com/alexanderjohnen/StarfieldVATS —
`git log --oneline` plus commit bodies, which carry the "why"), and search
this machine's other Claude Code session transcripts. Many prior sessions
are not summarised here, and searching past chats for a topic (health bar,
hit marker, a specific offset, a past crash) often surfaces detail this
file leaves out to stay readable.

## What this is

SFSE mod for Starfield: real-time FO76-style VATS. A hotkey locks a target
while the game keeps running; shots are redirected toward it in flight (no
camera or weapon snap, no dice roll). `C:\Dev\StarfieldVATS`, game
v1.16.244.

**Alexander tests in-game; Claude cannot.** Always read the SFSE log
yourself — `Documents\My Games\Starfield\SFSE\Logs\StarfieldVATS.log`,
truncated fresh every launch, not appended — rather than guessing from a
description.

Build/deploy (fails while Starfield is running — check first, it has
sometimes shown **two** processes and both must be gone):
```
tasklist //FI "IMAGENAME eq Starfield.exe"
cd C:\Dev\StarfieldVATS && powershell -ExecutionPolicy Bypass -File deploy.ps1
```

**Commit after every deployed build.** `docs/hudmenu-decompiled/`
(Bethesda's copyrighted decompiled UI assets) is deliberately gitignored
and was stripped from history once already — never add it to a commit.

## Working conventions that have repeatedly paid off

- **Measure before choosing a number.** Several things in this project were
  guessed at twice and wrong twice (the aim-point factor, the HUD restore
  delay) before a probe settled them in one round. If a constant has been
  adjusted by feel more than once, stop and log the inputs.
- **Pin down *when* before explaining *why*.** Two wrong theories about the
  crit marker both rested on an unexamined assumption about when it
  appeared. Alexander settled it by noticing he could *hear* the crit
  without seeing it.
- **A safety rule is not a universal.** See "risk categories" below.
- **Keep `res/StarfieldVATS.ini` in sync with `Settings.cpp`.** Twelve
  settings once became silently untunable. `deploy.ps1` now checks both
  directions (template vs code, deployed vs template) and warns —
  including, since 2026-08-26, that each key sits under the SECTION its
  `GetPrivateProfile*` call names. A key in the wrong section is read as
  absent and falls back to the code default while looking perfectly
  correct in the file; `bDebugAimMarkers` landed under `[Resource]`
  instead of `[HUD]` and the diagnostic it gates stayed off through two
  test sessions before the log gave it away.
- **Just edit the INI.** When a change needs a setting, write it into both
  the template and the deployed file rather than telling Alexander which
  line to add — he has asked for this explicitly.

## Risk categories, learned the hard way

1. **`REL::ID`-backed engine calls are the crash category.** An ID of `0`
   (unmapped) crashes instantly; a *mapped* ID is still no guarantee. Two
   confirmed hard crashes came from this. `IsHostileToActor` and
   `GetActorKnowledge` are both ID 0 — unusable.
2. **Virtual calls through an object's own vtable are NOT that category.**
   No Address Library lookup is involved. This distinction was missed for
   days and cost the whole live-health search (below). Caveat: a vtable
   *slot index* is itself reverse-engineered, so a deep slot on a large
   class is its own risk — `ActorValueOwner` was safe because it is a tiny
   interface and the sub-object was located by reading RTTI, not guessed.
3. **Struct offsets from CommonLibSF headers cannot be trusted blindly.**
   `TESObjectCELL::references` was off by 8. Verify empirically; wrap in
   `SafeRead` regardless.
4. **Enum *values* cannot be trusted either.** `BOOL_BITS::kDead` is inert
   in this game — it reads identically on a living actor and a corpse.
   `kPlayerTeammate` (1<<26) *was* confirmed, by measuring a companion
   against an enemy. Measure, don't assume.

## Current state — confirmed working in-game

- **Redirect**: hitscan weapons are flipped to real projectiles for the
  duration of a lock and slowed (`fLockedProjectileSpeed`, 80 m/s) so there
  is actually a frame in which to steer them. At stock 500 m/s a round
  crosses a typical engagement in one frame, which is why redirect only
  ever worked on rockets.
- **Live enemy health** via `ActorValueOwner::GetActorValue` — the same
  accessor behind the console's `getav` and Papyrus' `Actor.GetValue`.
  `ActorValueProbe.cpp` finds the sub-object at `Actor+0x070` by walking
  the MSVC RTTI class hierarchy (read from the base class descriptor's
  `mdisp`, not guessed) and re-verifies it on every call.
- **Health bar** on the target marker, as an arc.
- **Lock ends when the target dies** (`health <= 0`; overkill reads
  negative). Replaced a dead-bit check that had never once fired.
- **Corpses are not targetable**, same root cause.
- **Companions are not targetable** (teammate bit, measured).
- **VATS resource bar** — Alexander's design: full player health sets
  capacity, full player oxygen sets refill rate, budget is spent per point
  of damage *dealt* (measured as the target's health dropping, so armour
  counts). Maximums, not current values, to avoid a death spiral. Entirely
  self-contained; never writes to the player's stats.
- **Hit/kill/crit markers hidden** while Locked (`visible`, the AS3
  spelling — `_visible` is AS2 and was the entire earlier "unreachable
  path" saga). Restore is deferred until the indicator parks on its "End"
  frame, capped by `iHudRestoreDelayMs`.
- **HUD: a scanner-style ring, Alexander's design.** A thin circle instead
  of FO4 corner brackets, the distance set beside it on a tick the way the
  scanner sets its range, and no "TARGET" caption. Both gauges are arcs
  just OUTSIDE the ring, sharing a radius: target health below, the
  player's VATS budget above. Nothing is drawn inside the ring — the
  middle of the ring is where the target is, which is also why the
  distance sits beside it.
- **Marker size is FIXED** (`fTargetMarkerRadius`, 22px). See the note in
  Settings.h before changing it: a fixed size only looks wrong while the
  marker claims to ENCLOSE the target, so the size and the smallness are
  one decision, not two.
- **HUD text renders from a real TTF** (Bahnschrift → Segoe UI → ImGui's
  bitmap default), atlas built at 20px rather than upscaled from 13px.
  Text uses a 4-direction shadow and is dimmed by colour value, never by
  alpha — alpha over the dark shadow reads as grey mud.

## Open / unverified

- **Auto-advance to the next enemy is PARKED** (`bAutoAdvanceOnKill=0`,
  Alexander's call). The mechanism works; picking a sensible target does
  not, because there is no line-of-sight test. Three filters were tried and
  none is one: the crosshair activation target is occlusion-correct but
  only reaches interaction range, so it essentially never fired; "engaged
  with the player" still picks through walls, since an enemy shooting at
  you from the next room is still shooting at you; the on-screen projection
  check catches targets outside the view but not ones plainly visible
  through a doorway on another floor. Everything around it (resource cost,
  liveness, friendly filtering, 60° cone, on-screen check) is in place and
  correct — **re-enabling is one INI line once the depth-buffer occlusion
  below exists.** That is now this feature's blocking dependency.
- **The centre-height model amplifies the per-character spread, measured
  and confirmed in play 2026-08-26 — this is the cost that was accepted
  when it was chosen, now visible.** Two male pirates, same room, same
  pose, three seconds apart:

  | actor | radius | centreAboveFeet | aim |
  |---|---|---|---|
  | 0x0017E688 | 1.114 | 0.898 | 1.347 |
  | 0x0017E687 | 1.103 | 0.768 | 1.152 |

  Their radii differ by **1%** while their sphere-centre heights differ by
  **17%**, and the 1.5x factor turns a 13cm difference into **19.5cm** on
  the aim point. Alexander spotted it from screenshots before the numbers
  were looked at.

  What that isolates: the variance is not body size (radius says they are
  the same size) and not pose (both standing). The most likely remaining
  cause is EQUIPMENT inside the bounding sphere — a rifle carried low
  drags the centre down, a backpack or raised weapon pushes it up — which
  no factor on that centre can ever remove.

  This is the fourth time the same wall has been hit from a different
  side, and every time the answer has been the same: the bounding sphere
  does not know where a chest is. Do NOT build a fifth model. The options
  are (a) accept ~20cm on humans, (b) lower `fAimPointCentreFactor` to
  trade aim height for less spread (1.3 gives ~11cm and a slightly lower
  aim), or (c) the skeleton attempt above.

- **Aim point: settled on the sphere's CENTRE HEIGHT, plus smoothing
  (2026-08-26). Built, NOT yet deployed — Starfield was running.** The
  first non-humanoid tests decided it, after three models built on
  humanoids alone. Measured:

  | creature | radius | centreAboveFeet | centre/radius |
  |---|---|---|---|
  | human pirate | 1.03 | 0.84 | 0.81 |
  | mantid (spidery) | 2.96 | 1.40 | 0.47 |
  | hopper (ground-hugging) | 2.16 | 0.30 | 0.14 |
  | flyer | 3.09–4.50 | 0.14–2.18 | — |

  **The radius measures longest extent, not height.** A hopper reads a
  radius of 2.16 with its body 30cm off the ground, so the feet+radius
  model wanted to aim 2.22m up. All four non-humanoids hit the pose cap —
  i.e. the cap, not the model, was producing the aim point for the entire
  creature class. So the cap became the model:
  `aim = feet + fAimPointCentreFactor (1.5) x centreAboveFeet`, floor for
  collapsed bodies, nothing else. That lands at ~70% of body height on a
  human (the chest, up from the 64-68% measured before), 0.46m on a
  hopper, 2.10m on a mantid, and it tracks pose for free.

  This is the 2026-08-25 model returning, which was dropped for
  "amplifying the spread between characters". With creatures in the sample
  that objection is wrong: most of that spread is real, and 3cm between
  two pirates is a cheap price for not aiming two metres over an alien.

  **Smoothing (`fAimPointSmoothingSeconds`, 0.35) is new and was the
  missing piece under all three models.** The bounding sphere moves with
  the animation on everything — a guard's centre swings 0.09, the
  ground-huggers 0.32 — and on the flying alien it swings **2.04m in time
  with the wingbeat** (5938 samples), which the box followed. Only the
  offset from the actor's root is filtered, so a moving target still
  tracks with zero lag. State lives in a fixed 8-slot array under a mutex:
  `GetAimPoint` runs on both the render thread and AimAssist's steering
  thread, and an unguarded shared map there is the exact shape of the heap
  corruption this project already lost a day to.

  Also measured, and NOT yet acted on: **the horizontal offset theory is
  dead for humanoids but alive for sprawling creatures.**
  `|sphere.x - feet.x|` is 0.0013–0.0068 normalized on humans and
  **0.035** on the ground-huggers — their bounding sphere genuinely does
  not sit above their root. Which of the two is the better anchor for such
  a body is untested. Smoothing now filters x/y as well, so at least it
  no longer wanders.

  **Still open: the FOV axis.** Every screenshot so far had the target
  near x=0.5, where a wrong axis has zero error by construction. Both
  creature screenshots were third-person and the box looked left of the
  target — worth a first-person comparison shot, since a third-person
  shoulder offset applied outside `cameraRoot` would look exactly like
  that. One screenshot with the target at a screen edge settles the axis.

- **The skeleton route: the one remaining attempt is BUILT, not yet
  deployed (Starfield running). Run it, then decide.**
  The idea: read a real chest bone instead of deriving an aim point from
  the bounding sphere, which would be pose-correct and creature-correct by
  construction and would also serve per-body-part targeting later. Two
  findings, both 2026-08-26:

  - `bonedump` proved the tree walk never descended: **"1 nodes visited"**
    on every actor. So the years of only ever matching `HumanExportRoot`
    were never a bone-NAMING problem.
  - The children-offset probe (0xF0..0x1C0, validated by back-reference)
    then reported **not found**, on a human and a mantid alike.

  What keeps it from being fully closed: `worldBound` at 0x100 from that
  same pointer reads sane radii on every actor, so the header's
  NiAVObject layout is right up to at least 0x110 — which argues 0x130
  should have worked, and points at the *validator* rather than the
  window. It requires the first child's `parent` at 0x038 to point back,
  and 0x038 is itself a header claim.

  **Now built:** the probe validates by reading the first child's NAME
  (`ReadNodeName` is proven on these objects — it produced
  `HumanExportRoot` and `MantidA_mrRigRoot`) instead of following its
  parent pointer, logs every shape-plausible candidate with that name
  rather than auto-accepting one, and searches 0x0F0..0x220. Watch for
  `bone: children candidate at 0x... - N entries, first named '...'`. If
  nothing in that range yields a readable name, **stop** — the skeleton is
  not reachable this way, and centre-height plus smoothing is the answer.

  Note also that a named chest bone was never the right target anyway:
  the rig roots read `HumanExportRoot`, `MantidA_mrRigRoot`,
  `HopperA_mrRigRoot` — every creature family has its own naming. The
  naming-free version is the CENTROID of all bones, which is also far more
  animation-stable than a bounding sphere (a wingbeat moves a few joints
  of fifty and barely moves the mean, while it tears the sphere open by
  two metres).

- **Neutral civilians remain targetable.**
 Only companions are filtered. A
  real faction/relationship check is out of reach (`IsHostileToActor` is ID
  0, and reconstructing it means walking the actor's faction list, the
  player's, and the faction-reaction records).
- **Diagnostics need one INI switch before release.** Several log
  unconditionally (`worldBound`, `BoneProbe`, health). A shipping mod
  should be quiet, but "turn logging off" must mean flipping a setting,
  not deleting or commenting out code - these diagnostics decided nearly
  every question this week, and log volume has affected timing here
  before, so a release build can behave differently from a test run and
  you need to be able to switch them back on without a rebuild. Half an
  hour of work, not a feature. (Alexander's point, 2026-08-26: the log
  size itself is a non-issue - the file is truncated fresh every launch.)
- **Crit markers still occasionally appear seconds after a kill.**
 The game
  raises those events late; suppressing until they stop would mean
  suppressing indefinitely, which would break the base game's HUD. The
  2500ms ceiling is a deliberate compromise, not a fix.
- Heap-corruption fix in `ProjectileTracker` (re-validating formType and
  shooter before every write) has not been independently re-confirmed over
  a long session.

## Backlog: real occlusion via the depth buffer

**The problem it solves.** Nothing in this project can answer "is there a
wall between the player and that actor". Havok/`bhkWorld`/`NiPick` have no
bindings anywhere in CommonLibSF (grepped, zero hits), and the one engine
LOS function tried, `HasDetectionLOS`, is stubbed out as a **confirmed**
crash cause. This is what blocks auto-advance, and it would also serve
hit-chance and per-body-part targeting later.

**The idea.** A world position is already projected to screen coordinates
every frame for the target box (`CameraProject.cpp`). The game's depth
buffer says how far away the *visible* geometry is at any screen pixel. If
the actor is further away than the depth sampled at its projected
position, something is drawn in front of it. One sample answers "is this
target visible"; several across the body answer "which parts are exposed".

**Why this project should like it.** It lives entirely inside the D3D12
present hook we already own (`UI/D3DHook.cpp`) — no struct offsets, no
Address Library IDs, none of the category that has crashed this project
twice.

**Corroboration.** Alexander noticed that Starfield's own scanner outline
clips exactly at an actor's visible silhouette — occluded parts get no
outline. That is depth-testing, not a physics raycast: the game solves the
same problem the same way.

**What it takes.** Locating the depth-stencil resource in the D3D12 frame
and getting it readable on the CPU (a resolve/copy into a readback buffer;
one frame of latency is irrelevant here), then converting sampled depth
back to view distance using the projection constants `CameraProject`
already has. Estimated one to two sessions.

## Discussed, not implemented

- **Own crit indicator drawn on the VATS box**, instead of suppressing the
  vanilla one. Requires detecting a crit. The engine-side route is closed
  (`BSUIDataManager`, which `HitKillIndicator.as` subscribes to for
  `uHitType === CRITICAL`, does not appear anywhere in CommonLibSF). The
  Scaleform side looks feasible: a crit calls `gotoAndPlay` on the banner,
  and `currentLabel` was confirmed readable. Would need low-rate polling
  from the game thread — never per-frame from the render thread, which is
  the mechanism suspected in an earlier crash.
- **Per-body-part targeting — DECIDED AGAINST, 2026-08-26. Do not propose
  it again without new evidence.** Alexander's reasoning, and he has the
  strongest kind: a three-slot version was built and played, and it was
  not fun - nowhere near FO76's feel. On top of that, Starfield has none
  of the mechanics that make body parts mean anything: no crippling, no
  dismemberment, no limb reaction animations. A body part is a damage
  multiplier and nothing else, so it is head or not-head. And creature
  rigs differ completely from each other (`HumanExportRoot` vs
  `MantidA_mrRigRoot` vs `HopperA_mrRigRoot`), so it could never have been
  general.

  The one case worth keeping from it - shooting an enemy who is only
  partly behind cover - **is the occlusion feature, not this one**. A
  depth-buffer test answers "which point on this target is exposed", so
  the aim point can move onto the visible part silently: no slots, no UI,
  no decision for the player to make, and nothing new to balance.

  Claude proposed this feature twice from the FO76 framing without first
  checking whether Starfield has the mechanics underneath it. That is the
  same class of mistake as the three aim-point models: reasoning from a
  model instead of measuring the thing.
- **Ship-combat VATS** — see the `starfield-vats-mod-design` memory.


## Persistent memory

`starfield-vats-mod-design`, `starfield-vats-ui-hook`,
`commonlibsf-unmapped-ids`, `reference-starfield-vats-github`,
`feedback-commit-every-vats-build`, `feedback-model-tier-recommendations`.
`docs/CONTRIBUTIONS.md` has the detailed, honest attribution writeup —
including the misjudgments — if asked who contributed what.

## Speicherleck im Overlay — eingegrenzt auf eine Zeile (2026-08-27)

Alexander meldete, dass sein ganzer PC nach einigen Minuten zäh wird,
auch wenn Starfield nur im Hintergrund liegt, und sofort wieder normal,
sobald er das Spiel beendet. Gemessen mit `tools/Watch-StarfieldMemory.ps1`,
jedes Mal dieselbe Szene und Tätigkeit (in der Starmap sitzen, OBS
aufnehmen). Entscheidend ist die Spalte `PrivateMB` — der Task-Manager
zeigt den Working Set, den Windows eigenständig kürzt, und der verbirgt
das Leck.

| Bedingung | Dauer | Starfield (Private) |
|---|---|---|
| DLL wegbenannt | 6,2 min | −0,9 MB |
| `iOverlayStage=0` (Present reicht durch) | 2,8 min | −0,1 MB |
| `iOverlayStage=1` (+ ImGui NewFrame/Draw/Render) | 2,2 min | −9,7 MB |
| `iOverlayStage=2` (+ 11on12-Wrap, `OMSetRenderTargets`, `Flush`) | 6,5 min | −16,8 MB |
| `iOverlayStage=3` (+ `RenderDrawData`, normal) | 4 min | **+8.545 MB** |

Handles und Threads blieben in allen Läufen unverändert — reines
Speicherwachstum, kein Handle- oder Thread-Leck. Die Rate ist streng
linear: ~518 MB je 15 s, also rund 2 GB/min bzw. grob ein halbes
Megabyte pro dargestelltem Bild.

**Damit steckt das Leck in genau einem Aufruf**, dem einzigen
Unterschied zwischen Stufe 2 und 3:

```cpp
ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
```

Ausgeschlossen und nicht erneut zu prüfen:
- `Overlay::Draw` — alle Messungen liefen in der Starmap, wo es sofort
  zurückkehrt. Es ist nur ein Callback in `FakePresent`.
- `Initialize` — steht genau einmal im Log, baut sich nicht neu auf.
- Der fehlschlagende HUD-Font. `ImGui_ImplDX11_NewFrame` legt die Textur
  nur an, solange `pFontSampler` leer ist, und ruft beim Wiederholen
  vorher `InvalidateDeviceObjects()`. Kann nicht pro Bild allokieren.
  War zu diesem Zeitpunkt ein eigener, noch offener Fehler — inzwischen
  behoben, siehe den Nachtrag weiter unten.

### Nächster Schritt (überholt — die Vermutung stimmte, siehe BEHOBEN unten)

Erste Spur: `RenderDrawData` mappt Vertex- und Index-Puffer in jedem
Bild mit `D3D11_MAP_WRITE_DISCARD` (~100 KB + ~20 KB). DISCARD heißt
für den Treiber „gib mir einen frischen Puffer" — normalerweise
recycelt die Laufzeit die alten, sobald die GPU fertig ist. Auf einem
11on12-Gerät, das pro Bild `Flush()` bekommt und nie auf eine Fence
wartet, könnten sie sich stapeln. Passt zur Größenordnung, ist aber
unbestätigt.

Wichtig für den Test: In der Starmap ist `TotalVtxCount` null, es wird
also nichts gezeichnet — und es leckt trotzdem. Was auch immer es ist,
es hängt nicht an der Menge der Zeichenbefehle.

### Nachtrag: der HUD-Font (behoben 2026-08-27)

`AddFontFromFileTTF` schlug seit dem ersten Tag fehl, das Log meldete
durchgehend „no TTF found". Ursache war **nicht** der Pfad, sondern
`lib/imgui/imconfig.h`:

```c
#define IMGUI_DISABLE_FILE_FUNCTIONS
```

Wortgleich von BetterConsole übernommen. Das Define ersetzt `ImFileOpen`
durch eine Attrappe, die bedingungslos `NULL` liefert — und
`AddFontFromFileTTF` geht über `ImFileLoadToMemory`. Die Funktion konnte
also nie erfolgreich sein. Für BetterConsole ist das folgerichtig, es
benutzt ImGuis eingebaute Bitmap-Schrift und liest keine Dateien.

Es waren **zwei unabhängige Fehler in denselben drei Zeilen**: zusätzlich
waren die Pfade mit einfachen Backslashes geschrieben (`\W`, `\F`
unbekannte Escapes, `\b` ein echtes Backspace). Der Backslash-Fix allein
änderte nichts, weil das Define darunter lag — deshalb blieb die Meldung
nach dem vermeintlichen Fix bestehen. Beides musste weg.

Nebenwirkung, gleich mitbehandelt: mit aktivierten Dateifunktionen legt
ImGui von sich aus `imgui.ini` und `imgui_log.txt` an. In einem
Spielverzeichnis hat eine Mod das nicht zu tun, also setzt `D3DHook.cpp`
direkt nach `CreateContext` beide auf `nullptr`.

### BEHOBEN (2026-08-28)

Ursache war das ImGui-DX11-Backend, nicht der 11on12-Wrap. Die drei
Puffer (Vertex, Index, Constant) waren `D3D11_USAGE_DYNAMIC` und wurden
pro Bild mit `Map(D3D11_MAP_WRITE_DISCARD)` neu befuellt. Auf einem
normalen D3D11-Geraet ist das genau richtig: `DISCARD` liefert eine
frisch umbenannte Allokation, damit die CPU nie auf die GPU wartet — und
**dieser Vorrat wird bei `Present` eingesammelt.**

Dieses Overlay macht nie ein `Present`. Es zeichnet auf einem
D3D11On12-Geraet, dessen Arbeit per `Flush()` aus dem *D3D12*-Present-Hook
des Spiels abgeschickt wird. Die D3D11-Seite sieht also nie eine
Bildgrenze, und der Vorrat wuchs unbegrenzt.

Der entscheidende Hinweis war, dass es in der Starmap mit **voller Rate**
leckte, wo `Overlay::Draw` zurueckkehrt, bevor ein einziger Vertex
entsteht: alle drei `Map`-Aufrufe liefen trotzdem. Es hing also am
Umbenennen, nicht am Zeichnen.

Fix: die drei Puffer sind jetzt `D3D11_USAGE_DEFAULT` und werden mit
`UpdateSubresource` gefuellt — ein Aufruf pro Command-List, adressiert
ueber eine `D3D11_BOX`, damit jede Liste an ihren eigenen Offset kommt.
Keine CPU-Zwischenkopie, und nichts, was die Befehlsliste ueberlebt, in
die es geschrieben wurde. Die Aenderung steht in
`lib/imgui/imgui_impl_dx11.cpp` (eingebunden, daher aenderbar) und ist
dort ausfuehrlich kommentiert.

Nachgemessen, gleiche Szenen wie bei der Eingrenzung:

| Phase | vorher | nachher |
|---|---|---|
| Starmap, leerer Zeichenpfad, 3,25 min | +6.700 MB | **−9,9 MB** |
| Dungeon-Kampf mit voller HUD, 4,5 min | +9.300 MB | **−265 MB** |

Der Starmap-Lauf lief bewusst mit `bSkipEmptyFrames=0`, sonst haette die
Entschaerfung vom Vortag den Zeichenpfad gar nicht erst erreicht und ein
kaputter Fix haette genauso flach ausgesehen. Beide Schalter
(`iOverlayStage`, `bSkipEmptyFrames`) bleiben im Code — als Leiter, falls
im Renderpfad je wieder etwas zu suchen ist.

## Begleiter-Support (Aspekte 1 und 2 fertig, Stand 2026-08-29)

Bestätigt im Spiel: **Heilen funktioniert, und ein Begleiter im
Downed-State kommt dadurch wieder hoch.** Beides über
`ActorValueOwner::RestoreActorValue` (Vtable-Slot 09) auf demselben
RTTI-verifizierten Sub-Objekt, durch das `HealthReader` seit dem
25.08. liest — kein Address Library, kein selbstgebautes Struct.

Bedienung, einheitlich in beiden Modi:

| | |
|---|---|
| Tippen | handeln (sperren, Sitzung öffnen, heilen) |
| Halten (`iHoldToCancelMs`, 400 ms) | beenden, was offen ist |

Der Tipp-Druck im **Kampf**-Lock ist seit dem 28.08. **unbelegt**. Er hat
früher den Lock beendet, das macht jetzt das Halten. Zwei Kandidaten
stehen im Raum, beide von Alexander, beide auf etwas anderes blockiert:

- **Zum nächsten Gegner wechseln.** `TryAdvanceToNextTarget` existiert
  bereits (für auto-advance-on-kill), hängt aber an der fehlenden
  Sichtlinienprüfung — ohne sie werden Ziele durch Wände gewählt. Den
  Tipp-Druck jetzt daranzuhängen hiesse, denselben Fehler unter einem
  neuen Knopf auszuliefern.
- **Körperteil wechseln.** Braucht das Körperteil-System zurück, das am
  26.08. mangels brauchbarem Anker pro Teil aufgegeben wurde (der
  Knochen-Durchlauf hat nie einen gefunden, siehe `docs/FINDINGS.md`).

In beiden Fällen ist der Knopf die einfache Hälfte.

### Was NICHT geht, und warum es nicht nochmal probiert werden sollte

Die Aktion auf die **Aktionstaste des Spiels** (E) zu legen ist
gescheitert: unser WH_KEYBOARD_LL-Hook unterdrückt sie nicht. Alexander
hat es vom Spiel aus diagnostiziert — er konnte weiter mit dem Begleiter
reden, also kam die Taste durch. Derselbe Hook hat schon bei der
Zurück-Taste zeitweise nicht gegriffen (siehe Kommentar in
`BackKeyInterceptor.cpp`).

**Folge, die weiter reicht als dieses Feature:** die Rücktaste als
Ausstieg ist damit ebenfalls unzuverlässig. Halten-zum-Beenden ist der
Ausstieg, der nicht davon abhängt.

### Noch offen

- **Kein Item-Verbrauch.** Heilen ist gratis. Zwei mögliche Wege, beide
  mit offener Frage: `TESObjectREFR::RemoveItem` ist virtuell (Slot 08B),
  nimmt aber ein ungeprüftes 0x40-Byte-Struct; Papyrus wäre typsicher,
  hängt aber am `BSTThreadScrapFunction`-Platzhalter.
- **Neutrale NPCs.** Nur Begleiter werden erkannt (`kPlayerTeammate`,
  gemessen). Für „neutral statt feindlich" fehlt ein Signal —
  `IsHostileToActor` hat Address-Library-ID 0.
- **Buffs.** `Actor::DoCombatSpellApply` wäre genau der eine Aufruf, den
  es braucht, ist aber Papyrus-only. (Die **Starborn-Powers**, die früher
  ebenfalls hier standen, sind seit 2026-08-29 kein offener Punkt dieses
  Projekts mehr — siehe „Bewusst ausgelagert" unten.)

---

## Stand 2026-08-29 — Support ist fertig, alles bestätigt im Spiel

Der Begleiter-Support hat jetzt beide Hälften. Jeder Baustein wurde
einzeln bewiesen, bevor der nächste darauf gebaut wurde — die Reihenfolge
war hier wichtiger als das Tempo.

### Bedienung

Eine Taste, drei Bedeutungen, in beiden Modi gleich:

| | Kampf-Lock | Support-Sitzung |
|---|---|---|
| **Tippen** | *unbelegt* | heilen / wiederbeleben / schilden |
| **Halten** (`iHoldToCancelMs`, 400 ms) | beenden | beenden |

Scanner auf, Begleiter anvisieren → `SUPPORT (Taste)` erscheint statt
`TARGETING`. Tippen öffnet die Sitzung, der Prompt unter der Markierung
sagt dann `HEAL`, `REVIVE` (bei 0 Leben) oder `SHIELD` (bei vollem
Leben). Darunter steht `HOLD … TO EXIT`.

### Was bestätigt funktioniert

- **Heilen** über `ActorValueOwner::RestoreActorValue` (Vtable-Slot 09)
- **Wiederbeleben aus dem Downed-State** — funktioniert, anders als in
  Fallout 4, wo Alexander das erfolglos versucht hatte
- **Item-Verbrauch** über `TESObjectREFR::RemoveItem` (Slot 08B) mit
  selbst gebautem `RemoveItemRequest` — bewiesen an `item units 235 → 234`
- **Schild**: +500 auf alle drei DR-Typen über `ModActorValue` mit
  `kTemporary`, Zeit läuft runter, Bogen und Textanzeige stimmen.
  **Vtable-Slot 06 (Vier-Argument-Überladung) ist seit 2026-08-29
  bestätigt**: ein Lauf, in dem der Schild dreimal wirklich angewendet
  wurde (`0s → 30s → 57s → 86s`, Item 10 → 7, danach `expired`), ohne
  `ModActorValue faulted` und ohne eine einzige `[W]`/`[E]`-Zeile im
  ganzen Log. Nebenbei mitbewiesen: Nachlegen stapelt gegen die
  Restzeit, nicht von vorn, und der 300-s-Ablauf greift von selbst.

### Wo die Zahlen herkommen

`src/AidItems.h` — eine feste Tabelle aus gemessenen FormIDs. Es wird
**nie** ein ALCH-Record gelesen; das war der riskanteste Teil des
Features und ist damit gestrichen.

Heilung: Prozent vom **Maximalleben**, sofort (25/40/60 % für Med Pack /
Trauma Pack / Emergency Kit). Bewusst nicht die Spielwerte — 4 % über
fünf Sekunden ist keinen Tastendruck wert, und eine Heilung über Zeit
könnten wir gar nicht anwenden.

Schild: konstante 500 DR, das Item bestimmt nur die **Dauer**. Der
Umrechnungsschlüssel ist `DR × Sekunden ÷ 500`, eine Regel für alle neun
Items. Deckel 300 s, Nachlegen bis dorthin. Verbraucht wird immer das
schwächste passende Item.

### Offene Punkte, nach Wert sortiert

Neu geordnet 2026-08-29, nachdem die Starborn-Powers ausgelagert wurden
(siehe unten) — das war der Grund, warum `BSTThreadScrapFunction` ganz
oben stand.

1. **Sichtlinienprüfung** (Tiefenpuffer, eigener Abschnitt oben). Der
   wertvollste Punkt, weil er als einziger mehrere Dinge gleichzeitig
   freischaltet: den automatischen Zielwechsel, den freien Tipp-Druck im
   Kampf-Lock, und den Teil der neutralen NPCs, der sich über
   Sichtbarkeit statt über Fraktionen lösen lässt. Liegt komplett im
   D3D12-Hook, den wir besitzen — keine Struct-Offsets, keine
   Address-Library-IDs, also ausserhalb der Crash-Kategorie.
2. **Aufräumen, jetzt fällig.** `ProbeDamageResist` hat **gar keine
   Aufrufer mehr** (nur noch Definition und Deklaration) und kann sofort
   weg. Das SEH-Netz um `SafeModActorValue` ebenfalls — der Lauf, auf den
   es wartete, ist da (siehe Schild oben). `LogPlayerInventory` läuft
   dagegen noch bei jedem Support-Einstieg und sollte erst fallen, wenn
   ein Lauf zeigt, dass alle vorhandenen Aid-Items in `AidItems.h`
   stehen.
3. **Diagnose-Schalter vor Release** (eigener Punkt weiter oben).
4. **Neutrale NPCs**, soweit die Sichtlinie sie nicht abdeckt. Begleiter
   erkennen wir zuverlässig (`kPlayerTeammate`, gemessen); für „neutral
   statt feindlich" fehlt ein Signal, `IsHostileToActor` hat
   Address-Library-ID 0.
5. **`BSTThreadScrapFunction`** — **abgestuft, war vorher Platz 1.**
   CommonLibSF definiert den Typ in `RE/I/IVirtualMachine.h:16` als
   blossen Alias für `std::function`. Das ist kein *fehlendes* Binding,
   sondern ein **falsches**: Bethesdas Original ist ein eigenes Objekt
   mit eigener Vtable und eigenem Scrap-Allokator, `std::function` hat
   ein anderes Layout. Ein Aufruf damit compiliert sauber und übergibt
   der VM Müll — dieselbe Kategorie, die dieses Projekt schon zweimal in
   einen Crash geführt hat, nur ohne Compiler-Warnung.

   Wert gesunken, weil das, was daran hing, weg ist: Item-Verbrauch ist
   über `RemoveItem` gelöst, die Starborn-Powers sind ausgelagert. Übrig
   bleiben Buffs auf Begleiter — ein Extra, das keine Sitzung Reverse
   Engineering an diesem Typ rechtfertigt.

   Falls es doch jemand angeht: der Weg ist **nicht** „das Binding
   suchen", sondern das echte Layout ermitteln und minimal nachbauen —
   dieselbe Methode, die bei `RemoveItemRequest` funktioniert hat.
   Vorbild aus CommonLibSSE/CommonLibF4 (Skyrim und FO4 benutzen dieselbe
   Konstruktion), nachbauen, mit einem harmlosen Aufruf beweisen, und
   erst dann etwas Echtes darauf setzen.

### Bewusst ausgelagert: Starborn-Powers (Alexanders Entscheidung, 2026-08-29)

Kein offener Punkt dieses Projekts mehr, sondern die Idee für eine
**eigene Creation über das CK, ohne SFSE**.

Der Grund ist stärker als „passt hier nicht rein": im CK ist das der
**native** Weg. `AddSpell` und `DoCombatSpellApply` sind
Papyrus-Funktionen — in einem Papyrus-Skript ruft man sie einfach auf.
Das ganze `BSTThreadScrapFunction`-Problem existiert nur, weil wir aus
C++ von aussen in die VM hineinwollen. Von innen ist es eine Zeile.
Dazu läuft eine Creation ohne SFSE auf Xbox und überlebt Spiel-Updates,
statt bei jedem Patch auf eine neue SFSE-Version zu warten.

**Die Trennung ist nur so lange kostenlos, wie die beiden nichts
voneinander wissen müssen.** Sobald das VATS-HUD den Zustand einer Power
*anzeigen* soll, bräuchte es eine Brücke zwischen Creation und
SFSE-Mod — und die wäre wieder genau dieses Papyrus-Problem.

### Zwei Fehler dieser Sitzung, die sich lohnen zu kennen

**Eine Log-Grenze wurde zur Suchgrenze.** Der Inventar-Durchlauf hörte
nach 250 Einträgen auf — eine Zahl, die als Zeilenlimit fürs Logging
entstand und beim Herausziehen der Schleife mitwanderte. Alexanders
Inventar hat 417 Einträge, sämtliche Buff-Items lagen dahinter. Dass das
Heilen trotzdem lief, war Zufall: Med Pack liegt in den ersten 250.

**Überlagerung als Symptom behandelt.** Der Ressourcenbogen hatte keine
Modus-Prüfung und wurde auch um Begleiter gezeichnet. Statt zu fragen,
warum er dort erscheint, habe ich den Schildbogen auf einen eigenen
Radius geschoben. Alexanders Frage („die sollten doch nie gleichzeitig
auftauchen") hat den eigentlichen Fehler gefunden.
