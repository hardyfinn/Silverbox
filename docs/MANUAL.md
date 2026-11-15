# Silverbox for VCV Rack

Silverbox is a model of the 1981 TB-303 bass line synthesizer, split into three modules. The modules can be used together or with anything else in your rack.

| Module | What it is |
|---|---|
| **SBV Silverbox Voice** (16 HP) | The 303 voice: oscillator, diode ladder filter, envelopes, accent, slide. It has two additions, BASS COMP and KEY TRACK |
| **SBS Silverbox Sequencer** (32 HP) | The 303 sequencer, with separate pitch and time programming, 64 patterns, 7 tracks and tools for generating material |
| **SBX Silverbox Extended** (12 HP) | An expander that sits directly to the right of SBV and adds extended controls, each with a CV input |

With its knobs at default and no SBX attached, SBV behaves like a stock 303. Every addition leaves the stock sound alone until you use it.

![Panels](images/panels.png)

---

## SBV Silverbox Voice

### Controls

| Control | What it does |
|---|---|
| TUNING | ±12 semitones, continuous |
| CUT OFF FREQ | Where the filter's resonant peak sits, 300 Hz to 5 kHz |
| RESONANCE | Filter feedback. Also sets how the accent sweep behaves (see below) |
| ENV MOD | How far the filter envelope opens the filter. Turning it up also lowers the resting cutoff, as on the original |
| DECAY | Filter envelope decay, 0.2 s to 2 s. Accented notes always use the shortest decay |
| ACCENT | How much an accent opens the filter and raises the volume |
| WAVEFORM | SAW or SQUARE. The square is shaped from the saw, so it is darker than a textbook square and slightly off-centre |
| BASS COMP | Puts back the low end that drops away as RESONANCE goes up, without changing the resonant peak. 0 = stock |
| KEY TRACK | Makes the cutoff follow pitch, 0 to 100%. It pivots around C3 (1 V) and follows slides. 0 = stock |

| Jack | |
|---|---|
| 1V/OCT | Pitch. 0 V = C2 |
| GATE | A rising edge starts both envelopes. While GATE stays high nothing retriggers |
| ACCENT | High = accented note |
| SLIDE | If SLIDE is high when the pitch changes, the pitch glides into the new note (about 60 ms) instead of jumping |
| CUTOFF CV | ±5 V moves the cutoff ±3 octaves |
| AUDIO IN | Replaces the oscillator, so anything can go through the 303 filter, envelopes and accent |
| FILTER ENV | The filter envelope, 0 to 10 V |
| ACCENT SWEEP | The accent sweep, 0 to 10 V |
| OUT | Audio, about ±5 V on normal notes |

The GATE, ACCENT and SLIDE lights show those inputs.

### How the voice behaves

- **Notes are as long as the gate.** The amplitude envelope decays slowly (about 3.5 s) while the gate is held, then closes in a few milliseconds when the gate drops.
- **Accent does three things at once.** It forces the shortest filter decay, raises the volume, and charges the *accent sweep*, which adds to the cutoff.
- **The accent sweep depends on RESONANCE.** At low resonance each accent is a sharp spike. At high resonance it becomes a rounded swell that peaks around 60 ms in. It also discharges slowly, so a run of accents climbs higher each time. Most clones leave this out. In practice:
  - resonance low: accents bark;
  - resonance high: four accents in a row scream upwards.
- **Slide** glides in a fixed time, whatever the interval. An octave and a semitone take the same 60 ms.

### Driving SBV from another sequencer

Any gate sequencer works:

1. Patch pitch to 1V/OCT and gate to GATE.
2. For an accent, send a gate to ACCENT that is high when the note's gate rises.
3. For a slide, keep GATE high across both notes, and have SLIDE high at the moment the pitch changes. A simple way is to send the same gate to SLIDE on the step that slides into the next one, and lengthen GATE on that step to 100%.

---

## SBS Silverbox Sequencer

### How the 303 is programmed

The original 303 stores **pitch** and **time** as two separate lists:

- The **pitch list** holds up to 16 notes. Each note has its own UP or DOWN (octave), ACCENT and SLIDE.
- The **time list** says, for each step, **NOTE**, **TIE** or **REST**.
- When the pattern plays, each NOTE step takes the **next** note from the pitch list. A TIE holds the previous note, and a REST is silence.

So the lists are not lined up step for step. Turn one REST into a NOTE and every later note, accent and slide moves one step along. People found lines this way that they would never have written on purpose. SBS keeps this behaviour, and the display shows what is happening.

### The display

- **PITCH**: the 16 pitch slots: note name, ▲ or ▼ for octave, and A and S for accent and slide.
- **TIME**: a dot for NOTE, a dash for TIE, blank for REST. Steps past LENGTH are dimmed.
- **RESULT**: what actually plays. A thin line runs from each RESULT note up to the pitch slot it uses.
- **Header line**: the mode, the pattern being edited, the pattern playing (if it is a different one), the next pattern if one is queued, the track position, the tempo or EXT, TRIP, and the live transpose.

**Click** a PITCH or TIME cell to move the edit cursor there.

**Click a RESULT cell** to edit that step directly: its note, octave, accent, slide, and time. Note, octave, accent and slide change the pitch slot that step uses. Time changes the time list, so it may shift later notes, and the menu says so.

### Modes (the five lit buttons)

| Mode | Keys | Other buttons |
|---|---|---|
| PITCH WRITE | Set the note of the current pitch slot. If stopped, you hear it | DOWN / UP / ACCENT / SLIDE toggle that slot's flags. NEXT / BACK move along |
| TIME WRITE | — | NOTE / TIE / REST write a step and move on. NEXT / BACK move without writing |
| PATTERN PLAY | Live transpose: C = 0 up to HI C = +12. Hold DOWN for −12 to 0 | |
| TRACK WRITE | Set the transpose of the slot being entered | PATTERN picks the slot's pattern. NEXT stores the slot and moves on. Holding CLEAR cuts the track at the cursor |
| TRACK PLAY | Live transpose, as in Pattern play | Plays the selected track's slots in order, then loops |

Changing mode never stops playback, so you can edit while it runs.

### Other controls

| Control | What it does |
|---|---|
| RUN | Start and stop |
| CLEAR (hold 1 s) | Clears the pattern. In Track write it cuts the track at the cursor |
| SCRAMBLE (hold 1 s) | Randomises the pattern, like a 303 whose batteries were pulled |
| UNDO | Undoes the last memory change, 32 levels |
| PATTERN | Selects one of 64 patterns (I-1A to IV-8B). While running, the change waits for the end of the loop |
| TRACK | Selects one of 7 tracks |
| TEMPO | 40 to 300 BPM. It is ignored when CLOCK is patched |
| GATE LENGTH | 10% to 95% of a step, with a detent at the original 50% |
| LENGTH | 1 to 16 steps. It is stored with the pattern |
| MUTATE | At the end of every loop, makes up to 4 random changes to the pattern, written to memory |
| TRIPLET | Triplet steps (12 steps to a bar of 4/4 at 16ths). It is stored with the pattern |
| SHIFT PITCH / SHIFT TIME | Rotate one list by one slot against the other: the classic way to get a variation |

| Jack | |
|---|---|
| CLOCK | External clock. The context menu sets 24 PPQN (the default, as the original's sync), 1 per step, or 4 PPQN |
| RESET | Back to step 1 |
| TRANSPOSE | 1V/oct, in semitones |
| RUN | A trigger toggles run. The context menu can make it a gate instead |
| PATTERN CV | 0 to 10 V adds 0 to 63 to PATTERN |
| MUTATE CV | ±5 V adds to MUTATE |
| 1V/OCT, GATE, ACCENT, SLIDE | Patch these to the same jacks on SBV |
| CLOCK | One trigger per step |
| END | A trigger at the start of each pattern loop |

The **context menu** also has Copy pattern, Paste pattern, Copy to next empty pattern, Clear all memory, and Export or Import memory as a JSON file. All 64 patterns and 7 tracks are saved with your patch.

---

## SBX Silverbox Extended

Place SBX directly to the right of SBV, and its LINKED light comes on. At its default positions it changes nothing. Each knob has a CV input below it: ±5 V sweeps the whole range.

| Control | Range (default) | What it does |
|---|---|---|
| VEG DECAY | 16 ms to 6 s (3.5 s) | How long a held note takes to fade |
| ACCENT DECAY | 30 ms to 3 s (0.2 s) | Filter decay on accented notes |
| SLIDE TIME | 2 ms to 500 ms (60 ms) | Glide time |
| SOFT ATTACK | 0.3 ms to 30 ms (3 ms) | Volume attack on non-accented notes |
| SWEEP SPEED | ×0.25 to ×4 (×1) | Speed of the accent sweep |
| FILTER FM | 0 to 100% (0) | Feeds the voice's own output into the cutoff at audio rate: grit and growl |
| OVERDRIVE | 0 to 24 dB (0) | Drives the filter input harder |
| DECAY RANGE | STOCK / WIDE | WIDE makes SBV's DECAY run from 30 ms to 3 s |
| MUFFLER | OFF / SOFT / HARD | A soft clipper on the output |

---

## Patch recipes

Each recipe is a list of cables. The distortion in these recipes is **NYSTHI RodentV2**, a free model of the ProCo RAT, the pedal most often used on a 303. The delay is VCV's free **Delay**. Any distortion and delay will do.

### 1. First acid line
- SBS 1V/OCT → SBV 1V/OCT, SBS GATE → SBV GATE, SBS ACCENT → SBV ACCENT, SBS SLIDE → SBV SLIDE.
- SBV OUT → RodentV2 in → Delay in → Audio.
- SBV: CUT OFF FREQ 30%, RESONANCE 75%, ENV MOD 60%, DECAY 30%, ACCENT 80%, SAW.
- SBS: press RUN. Pattern I-1A holds a demo line. Then sweep CUT OFF FREQ and RESONANCE slowly by hand: that is the whole genre.

### 2. Programming the 303 way
1. Choose an empty pattern (for example I-2A) and press **PITCH WRITE**.
2. Enter C, C, D#, C, G, A#, with NEXT between notes. Press UP on the second C and ACCENT on the G.
3. Press **TIME WRITE** and enter NOTE, REST, NOTE, NOTE, TIE, NOTE.
4. Press RUN and watch the RESULT row.
5. Click the second TIME cell and press NOTE. Every later note moves one step along and the accent lands somewhere new. Press UNDO to go back.

### 3. Generate, save, perform
1. Hold **SCRAMBLE** for one second. Listen. Repeat until something catches.
2. Set **MUTATE** to about 10% and let it run. Each loop drifts a little.
3. When it is good, set MUTATE to 0 and choose **Copy to next empty pattern** from the context menu. Carry on from there.
4. Press **TRACK WRITE**. For each slot, turn PATTERN to a kept pattern, press a key for transpose, and press NEXT.
5. Press **TRACK PLAY** to perform. Use the keys for live transpose, and PATTERN CV or MUTATE CV from an LFO for slow change.

### 4. The accent stack
Program four accented notes in a row (ACCENT on four pitch slots, four NOTE steps).
- With RESONANCE at 10%, each accent is the same short bark.
- With RESONANCE at 90%, each accent climbs above the last. Watch the ACCENT SWEEP output on a scope to see it charge.

### 5. SBV from another sequencer
Use VCV SEQ-3 (or any gate sequencer):
- Its pitch row → SBV 1V/OCT, its trigger or gate → SBV GATE.
- A second gate row → SBV ACCENT.
- For slides, send a third gate row to SLIDE, and set that sequencer's gate length to 100% on the slid steps.

### 6. Drums through the 303 filter
- A drum loop or a drum machine output → SBV AUDIO IN.
- The kick triggers → SBV GATE, the snare triggers → SBV ACCENT.
- RESONANCE 60%, ENV MOD 70%, DECAY 20%. The kick pumps the filter, and each snare gets an accent sweep.

### 7. SBX squelch
- SBX next to SBV. FILTER FM 25%, OVERDRIVE 9 dB, MUFFLER SOFT.
- Raise RESONANCE until it growls. FILTER FM makes the resonance bite on every note.

### 7a. BASS COMP and KEY TRACK
- Turn RESONANCE to 90% and notice how the bass thins. Then raise **BASS COMP** to bring the weight back under the scream.
- For lines that jump octaves, set **KEY TRACK** to 50 to 100% so high and low notes stay equally bright.

### 8. Unstable time
- Use a clock whose rate is wobbled by a slow random voltage. Patch it into SBS CLOCK, and set the context menu to **1 per step**.
- Set GATE LENGTH to 95% so the slides stay connected, and MUTATE to 5%.
- This works well alongside the Autechre-style set in this project.

---

## What is modelled

- **Oscillator:** a falling saw with the slight curve of a charging capacitor. The square is made from the saw by a transistor-style shaper (about 46% duty, slanted edges, slight droop). The audio path is oversampled.
- **Filter:** a four-stage coupled diode ladder with a smaller bottom capacitor. It rolls off at about 16 dB/oct in the first octaves rather than 24. A 150 Hz high-pass in the feedback path thins resonance at low cutoffs, and soft saturation at the input adds grit. Full resonance rings but does not quite self-oscillate.
- **Envelopes:** a filter envelope with a 3 ms attack and 0.2 to 2 s decay, forced short on accents. A volume envelope with a long fixed decay and a fast close on gate off.
- **Accent:** a sweep capacitor driven by the filter envelope on accented notes. RESONANCE sets how fast it charges and discharges, as the original's dual-gang resonance pot did. Accents also raise the volume.
- **Slide:** a fixed-time glide with no envelope retrigger.
- **Sequencer:** separate pitch and time lists, ties, rests, 24 PPQN timing, 50% gates, triplets, 64 patterns in four groups of 8 × A/B, and 7 tracks.

Every constant is an estimate made from published descriptions rather than a measurement of a real unit. They are listed in `src/dsp/Constants303.hpp`, and `docs/DECISIONS.md` explains each one that was changed.

*"Roland" and "TB-303" are trademarks of their owner. Silverbox is an independent model.*
