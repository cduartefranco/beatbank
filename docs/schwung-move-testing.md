# Schwung + Move recording — test matrix

Tracking recording behaviour for Beat Bank driving a Move native track via
Pre-mode injection. Lives on the `schwung-move-support` branch.

**Depends on:** chain host fix — [charlesvestal/schwung#150](https://github.com/charlesvestal/schwung/pull/150).
Without it, Pre-mode injects nothing for a clock-driven FX.

## Standing setup (unless a case says otherwise)

- Beat Bank in a chain **slot on Move Track 1**, MIDI FX = **Schw+Move** (Pre).
- **Note Map = drumrack** (36–51) so notes hit the target track's drum pads.
- Target track chosen via the slot's **Recv Ch** (Move default: Track N ↔ ch N).
- Pattern: a recognizable one (e.g. `Classic House`) unless noted.
- Record: arm the target Move track and record **1 bar**.

**Invariant under test:** the recorded pattern on the target track should be
**identical (same steps, same timing, no stuck/dropped/doubled notes)** across
all four configurations. Config differences must not change the captured MIDI.

## Core matrix (2 × 2: synth × target track)

| # | Chain slot | Target track (Recv Ch) | Pass? | Notes |
|---|------------|------------------------|-------|-------|
| 1 | Track 1, **with** synth | **Track 2** (different) | ☐ | |
| 2 | Track 1, **with** synth | **Track 1** (same)      | ☐ | |
| 3 | Track 1, **no** synth   | **Track 2** (different) | ☐ | |
| 4 | Track 1, **no** synth   | **Track 1** (same)      | ☐ | |

### What to check in every case
- ☐ Recorded pattern matches the on-screen grid (right steps on right pads)
- ☐ Timing is tight — downbeat lands with Move's transport (no 1-step lag)
- ☐ No **stuck** notes after Stop
- ☐ No **doubled** / flammed hits
- ☐ No **dropped** hits on busy bars
- ☐ Playback of the recording matches what you heard live

### Case-specific watch-items
- **Cases 2 & 4 (target = chain's own track 1):** highest risk of **self-echo /
  feedback** (chain re-hearing its injected notes) and, in case 2, **doubled
  audio** (slot synth *and* Track 1's native instrument). Confirm the echo
  guard holds and the recording isn't doubled.
- **Case 1 vs 3 (synth vs no synth):** the recorded MIDI should be the same;
  only whether you *also* hear the slot synth live should differ.

## Additional combinations worth running

Not full cross-product — the axes most likely to expose inconsistency:

| Axis | Why it matters | Suggested check |
|------|----------------|-----------------|
| **Pattern density** | A dense/repeated-note pattern (kick every step) stresses the echo refcount hardest — most likely to strand or drop notes | Re-run cases 2 & 4 with a kick-heavy pattern |
| **Pattern length** | 32-step (2-bar) patterns record over a loop boundary; different from 16-step | Run one case with a 2-bar pattern, record 2 bars |
| **Re-record / overwrite** | The build-a-kit workflow: record A → change pattern → record B onto the **same** track/pad | Record twice to one target, confirm clean overwrite |
| **Record target** | Move **track clip-record** vs **pad resample** are different capture paths | Test whichever you actually use (both if building pad kits) |
| **Recv Ch indexing** | Confirm which Recv Ch value hits which Move track (0- vs 1-index) | Pin the mapping once, note it here |
| **Note Map = gm** | GM puts cowbell/conga/perc at 56/63/70 — off a drum track's pads | Confirm gm is wrong for drum tracks (expected), drumrack right |
| **Baseline: Pre off (Post)** | Control — slot-synth-only path must be unaffected | Record nothing to Move; slot synth still plays normally |

## Results log

**Case 1** — Boom Bap 101, slot on T1 w/ Mr.Drums, Recv→T2 (built-in kit, T2 MIDI-in from T1). Mr.Drums not heard (expected, MIDI went to T2).
- **Downbeat correct; every later step recorded +1 sixteenth late.** Constant 1-step shift — does not accumulate; persists into bar 2.
- Recorded `ch` = `xx.x.x.x.x.x.x.x` vs original `x.x.x.x.x.x.x.x.` → parses as **{0} ∪ (all 8 hats +1 → {1,3,5,7,9,11,13,15})**, i.e. the **downbeat is doubled** (step 0 at *both* 0 and 1) and every hat is +1.
- Kick: hit 1 (step 0) looks right; hit 2 (step 8) recorded at step 9 (+1).
- **Read:** every generated note snaps forward one 16th in Move's *step recorder*; the downbeat is *additionally* anchored at step 0 by record punch-in (hence 0 **and** 1). Local slot-synth audio was tight in the earlier timing test, so generation is on time — the shift is introduced by the inject→Move-record path.
- **TODO confirm:** does T2 sound tight *live* (only the recording is shifted)? Record-quantize on/off? Shift stay exactly 1 step at 2× tempo?
