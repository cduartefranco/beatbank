# Beat Bank

A library of **standard drum patterns** for [Schwung](https://github.com/charlesvestal/schwung) on Ableton Move.

No generation, no randomness, no fills. Beat Bank is a chainable `midi_fx`
that plays one of ~100 hand-authored, canonical drum grids straight into
whatever drum kit follows it in the chain — so you can drop in a recognizable
groove from almost any genre instead of programming one by hand.

```
[Beat Bank MIDI FX] → [Drum Kit Sound Generator] → [Audio FX] → out
```

Beat Bank deliberately has **no tempo or swing controls** — it follows Move's
transport and tempo and plays the grid straight, so it never competes with
Move's own groove. (It stays silent unless Move's MIDI Clock Out is on and the
transport is running.)

## Patterns are editable files — add your own

Patterns are **not** baked into the binary. They load at startup from plain
`.beat` text files, scanned from two folders and merged:

1. **Shipped defaults** — `…/modules/midi_fx/beatbank/patterns/*.beat`
   (refreshed on every module upgrade)
2. **Your patterns** — `/data/UserData/schwung/beatbank/patterns/*.beat`
   (persist across upgrades; the module seeds a `_HOWTO.txt` here on first run)

A `.beat` file is a list of pattern blocks separated by blank lines:

```
name: Boom Bap
genre: LOFI
steps: 16
kick:  x.......x.......
snare: ....A.......A...
ch:    x.x.x.x.x.x.x.x.
```

- `steps` is `16` (one bar of 16th notes) or `32` (two bars)
- Row characters: `.` rest · `x` hit · `A` accent · `g` ghost (soft)
- Omit voices that don't play; rows must be exactly `steps` long
- Drop a file in the user folder (or copy a shipped one and edit it), reload
  the module, and your patterns appear after the built-ins

Edit them over SSH, the Schwung Manager file browser (`move.local:7700/files`),
or anywhere you can drop a text file.

## Genres included

~100 patterns across: hip-hop / boom-bap, lofi, trap, trip-hop · house, techno,
electro, disco, UK garage · drum & bass, jungle, breakbeat, dubstep · funk
(Funky Drummer, Cold Sweat, Purdie shuffle…), soul/Motown, rock/pop, gospel,
country · bossa nova, samba, Latin, Afrobeat, reggae/dub, calypso.

## Voices

Twelve voices, each sending a General MIDI drum note by default. Remap any
voice in the Drum Notes menu to match your kit's pads.

| Voice | Default | Voice | Default |
|-------|---------|-------|---------|
| Kick | 36 | Ride | 51 |
| Snare | 38 | Crash | 49 |
| Closed Hat | 42 | Cowbell | 56 |
| Open Hat | 46 | Conga | 63 |
| Clap | 39 | Perc (shaker/tamb) | 70 |
| Rim / Clave | 37 | Tom | 45 |

## Controls

Beat Bank uses Schwung's standard chain menu (like any synth/FX), so it's
fully swappable. Open the slot's **MIDI FX** to get:

- **Pattern** — scroll to change it; the beat updates live, with the name +
  genre shown.
- **Drum Notes** — a submenu of the 12 voice notes. **Scrolling a voice
  auditions it** — Beat Bank fires that note into the kit so you can map each
  voice to the right drum *by ear* (Move kits have no fixed pad→note layout, so
  this is how you line them up).
- **Swap Module** — change Beat Bank for another MIDI FX, or pick **None** to
  clear it.

## Two ways to use it

**1. Live, into a Schwung slot synth.** Load Beat Bank as a slot's MIDI FX in
front of a drum sound-generator (an SF2 drum kit, etc.). Pick a pattern; it
plays in sync with Move's transport.

**2. Print into Move's native sequencer.** Capture a pattern as a real,
editable Move clip — so Move owns it and you switch it with Move's own pattern
buttons. This uses the chain's **Schw+Move** injection mode:

1. On **Move**: pick the drum track to fill, and note its MIDI channel
   (track *N* defaults to channel *N*). Its drum kit owns the 16 pads.
2. In **Schwung**, on a shadow slot: set **MIDI FX = Beat Bank**, switch the
   slot's **MIDI FX mode to Schw+Move**, and set the slot's **Receive Channel**
   to that track's channel. (A slot needs a sound generator to be valid — load
   any and **mute the slot** so only Move's kit sounds; injection is on the
   MIDI path and keeps working.)
3. Pick a Beat Bank pattern (jog).
4. On **Move**: **arm Record** on that track and start the transport. Beat Bank
   plays in sync and injects its notes into Move, which **records them into the
   current pattern**.
5. Stop after a bar. That pattern is now a native, editable Move clip.
6. Switch Move's pattern slot, pick another Beat Bank pattern, repeat — build
   up all of the track's patterns.

> Note: injected notes use Beat Bank's voice notes (kick 36, snare 38, …). A
> standard Move drum kit maps pads to notes 36–51, so Kick/Snare/Hats/Clap/
> Rim/Tom/Ride/Crash land on pads out of the box; Cowbell/Conga/Perc default
> above 51 — remap them in the Drum Notes menu if your kit needs them.
>
> The record-capture path was confirmed on hardware (Move records external
> USB/injected MIDI into its step sequencer). The exact slot wiring (Schw+Move
> + Receive Channel) should be sanity-checked on your device the first time.

## Build & install

```bash
./scripts/build.sh            # cross-compile dsp.so (Docker or native)
./scripts/install.sh          # deploy to move.local (incl. the patterns/ folder)
./scripts/package.sh          # or build a release tarball
```

Tests (no hardware required):

```bash
make test                     # validate the .beat library + parser + sequencer timing
make validate                 # just lint the .beat files
```

## Releasing

Tag-driven GitHub Actions (`.github/workflows/release.yml`) builds in Docker,
attaches `beatbank-module.tar.gz`, and updates `release.json`. To cut a release:

```bash
git tag v0.1.0 && git push --tags
```

## Notes

- The only real interchange "standard" for drum patterns is the MIDI file, but
  those bake in tempo/timing and are awkward to hand-edit — the whole point
  here is a human-editable grid, so Beat Bank uses the `.beat` text format.
  (MIDI-file import could be added later as a converter.)
- The Amen/Think/clave patterns are quantized templates; the original
  recordings have microtiming a 16th grid can't capture — they'll sound a touch
  stiffer than a sampled break, which is correct for a step voicing.

## Pattern sources

Canonical placements were cross-checked against MusicRadar, Attack Magazine,
Native Instruments, Drumeo, Modern Drummer, Studio Brootle, BVKER, MIDI Mighty,
Wikipedia and others (per-genre references are in the research notes).

## Acknowledgements

Built on the Schwung MIDI FX plugin API; the clock-handling skeleton follows
the same shape as the `branchage` module.
