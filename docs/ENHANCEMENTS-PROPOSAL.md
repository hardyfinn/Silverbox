# Silverbox round 2: enhanced oscillator, enhanced filter, Scala tuning

Draft 1, 2026-11-15. A proposal for discussion; nothing is built. The three existing modules (SBV, SBS, SBX) stay exactly as they are, apart from one optional menu item on the sequencer (section 3), which changes nothing unless you load a scale.

The idea: split the voice into its two halves and open each one up. **SBO + SBF patched together is a 303 voice with the lid off.** Each half also works alone with the rest of your rack.

---

## 1. SBO, Silverbox Oscillator (about 14 HP)

The 303 oscillator core on its own, with the things the original circuit almost does anyway.

**What you asked for**

- **PULSE WIDTH with PWM.** The 303 makes its square by comparing the saw against a fixed threshold. Moving that threshold is pulse width, so this is the original circuit with one fixed part made variable, not a bolted-on second oscillator. The square keeps its 303 character (slanted edges, droop) at every width. Knob, CV input and CV depth. The centre detent is the stock 46%.
- **FOLD, a wavefolder before the filter.** Knob, CV input and CV depth, plus **SYMMETRY** (a bias into the folder, which changes the even/odd harmonic balance and makes fold sweeps far more varied). At zero the signal passes untouched. It has its own **FOLD IN** jack, normalled to the oscillator, so the folder can also process anything else.

**What I suggest adding**

- **WAVE MIX** instead of a switch: a crossfade from saw to square, with CV. Separate SAW and SQUARE outputs as well as the mixed, folded OUT.
- **SUB**: a square one octave down, derived from the main oscillator by a divider (the way Roland did it in the SH-101). A 303 with a sub is a well-loved mod and fixes the same "thin bass" problem as BASS COMP from the other end.
- **SYNC** input (hard sync) and **FM** input with depth (linear, through-zero is not needed here). Both are cheap to add and suit the folder well.
- **Slide built in**: SLIDE input and SLIDE TIME knob, so SBO glides like the 303 from any sequencer.
- **DRIFT**: a small knob that adds slow, analog-style pitch wander (off at zero).

**Trimmers brought to the panel** (small knobs in a "TRIM" row)

- **TUNE** and **WIDTH**: the two oscillator calibration trimmers. WIDTH is the 1 V/oct scaling. Turning it away from centre stretches or squashes the octaves, which is useless on a real 303 and musically interesting here (deliberately "wrong" tracking, a different detuned scale in every octave).
- **SAW CURVE**: how bent the saw ramp is (a fixed capacitor value on the board). From a straight ramp to a strongly curved one.

---

## 2. SBF, Silverbox Filter (about 16 HP)

The second half of the voice: **the diode ladder, both envelopes, the accent circuit and the VCA**. I recommend including the envelopes and VCA rather than a bare filter, because the 303 sound is the filter *and* the way accent and envelope drive it. A bare filter would lose the accent sweep. A **FILTER OUT** jack before the VCA gives you the bare filter whenever you want it.

**What you asked for**

- **Band-pass.** Yes, and it suits this filter. A diode ladder gives band-pass and high-pass by subtracting stage outputs, with the same resonance and the same saturation. I suggest a three-way **MODE** switch: **LP / BP / HP**. In BP and HP the accent sweep and envelope work exactly as in LP. BP 303 lines sound nasal and vocal; HP gives thin, hissing acid that sits above another bass.
- **Trimmers as knobs:**
  - **CUTOFF TRIM**: the filter calibration trimmer. It shifts the whole range of the CUT OFF FREQ knob up or down by about two octaves.
  - **RES TRIM**: sets how much resonance the top of the RESONANCE knob gives. Stock at centre. Fully up, the filter **self-oscillates** and can be played as a sine oscillator (the real 303 cannot quite do this).
  - **GATE TRIM**: I need your steer on this one. On the board it sets the gate time, which SBS already has as GATE LENGTH. On SBF I would make it **gate delay/shorten**: it trims how long the VCA stays open relative to the incoming gate (50% to 150%), so you can tighten or loosen notes from any sequencer. Tell me if you meant something else.

  A caution: I am confident about the tuning, width and filter trimmers. I am less sure of the exact list of all six on the board, so I will check them against the service notes when writing the build spec and add any I have missed.

**What I suggest adding**

- **Other fixed board values as knobs** (these are the ones modders most often change):
  - **BASS**: the corner of the high-pass in the resonance feedback path (fixed at about 150 Hz in the model). Turning it down keeps the bass under high resonance at the source. This is a more "circuit" version of BASS COMP, and I would include both.
  - **POLES**: the size of the odd capacitor in the ladder. From the stock 18 dB character to a tighter, more Moog-like 24 dB.
  - **DRIVE**: level into the ladder.
  - **ENV BIAS**: how far ENV MOD pulls the resting cutoff down.
  - **VCA DECAY** and **ACCENT DECAY** (as on SBX, so SBF does not need the expander).
- **CV inputs** for cutoff, resonance, env mod, decay and accent amount. Resonance CV in particular is something the real unit can never do.
- **FILTER FM** input (audio rate) with depth, for external signals. SBX only has the self-FM.
- **ACCENT as a level, not just on/off**: the ACCENT input reads 0 to 10 V as accent strength, so velocity or any CV can give half accents. A gate still works as before.
- **ENV IN**: an external envelope replaces the internal filter envelope when patched.
- **RETRIG switch**: OFF is 303 behaviour (no retrigger while gate is held). ON retriggers on a trigger input, for use with modern sequencers.
- **KEY TRACK** with a 1V/OCT input, and **BASS COMP**, as on SBV.
- Outputs: OUT, FILTER OUT, FILTER ENV, ACCENT SWEEP, **VCA ENV**.

---

## 3. Scala microtuning on the sequencer

**Recommended: build it into SBS as a menu item**, because SBS knows exactly which key each note is, so it can retune without guessing. With no scale loaded, SBS behaves exactly as now, and existing patches are unaffected.

- Context menu: **Load Scala scale (.scl)…**, optional **Load keyboard mapping (.kbm)…**, **Reset to 12-tone equal**. The scale is saved inside the patch (not as a file path), so the patch still works if the file moves.
- **Keys are scale degrees.** C is degree 0, C# is degree 1, and so on. UP and DOWN shift by one period of the scale (the octave, or whatever the scale repeats at, which makes non-octave scales such as Bohlen-Pierce work).
- Scales with exactly 12 notes map straight onto the 13 keys. Scales with more than 12 notes use the first 12 degrees per period by default; a .kbm mapping lets you choose which degrees go on which keys.
- **Root and reference**: menu choice of which key is the scale's 1/1, default C.
- **Transpose**: key transpose (Pattern play, tracks) moves by scale degrees, so a line stays in the scale. The TRANSPOSE jack stays an exact pitch shift in volts.
- The display header shows the scale name. Slides glide between the retuned pitches as normal.

**Alternative: a small separate module, SBT Silverbox Tuner** (4 HP), that sits between any sequencer's 1V/OCT and a voice and remaps semitones to a loaded scale. It would work with other sequencers too, and leaves SBS completely untouched. I can do both; if only one, the built-in one is more exact and needs no extra cable.

---

## 4. Things I considered and would leave out

- **More per-step data on the sequencer** (per-step gate length, ratchets, probability). They are useful, but they change SBS's memory format, and you asked to keep it stable. A separate "advanced sequencer" could come later.
- **Polyphony, stereo, built-in effects.** Not the 303's world; Rack already has them.
- **Changes to SBV.** None. SBO + SBF is the enhanced voice; SBV stays the stock one.

## 5. Choices for you

1. **Panel style** for the whole plugin (mock-ups A to D).
2. **SBF scope**: filter with envelopes and VCA (recommended), or a bare filter.
3. **GATE TRIM**: is "trim the note length relative to the incoming gate" what you meant?
4. **Scala**: built into SBS (recommended), a separate SBT module, or both.
5. **Suggestions**: I will include everything in sections 1 and 2 unless you strike items. The ones most worth a yes or no are SUB, DRIFT, POLES and accent-as-level.
