# Build instructions, round 2: SBO, SBF and SBT for Silverbox

**Audience:** an Opus model adding three modules to the existing plugin at `hardyfinn/silverbox` (branch `main`).
**Owner:** Hardy. Build everything in one go, test it yourself, hand over the result. No check-ins.

Read first: `docs/BUILD-INSTRUCTIONS.md` (round 1 spec; its ground rules §0, signal conventions §2 and panel rules §3 all still apply), `docs/DECISIONS.md`, and the existing code in `src/dsp/`, `src/modules/` and `scripts/make_panels.py`.

**Hardy's decisions (2026-11-15):**

| Question | Answer |
|---|---|
| Existing modules | SBV, SBS and SBX stay as they are, with **two changes Hardy asked for** (§1a): SBV's cutoff range is corrected to the service-note calibration, and SBS gains Scala tuning in its menu. Nothing else about them changes: behaviour, memory format, panel layout |
| New modules | **SBO** Silverbox Oscillator, **SBF** Silverbox Filter (filter with both envelopes, the accent circuit and the VCA), **SBT** Silverbox Tuner (Scala microtuning as its own module). Scala tuning **also** lives in SBS |
| Oscillator | PWM and a wavefolder before the filter, plus the extras in §2 |
| Filter | LP / BP / HP switch; board trimmers and fixed values exposed as knobs; GATE TRIM shortens the note |
| Panels | The "clean silver" style now in `scripts/make_panels.py` (flat, no texture). Text labels on everything |
| Build target | Apple silicon Mac only (CI as it is) |

---

## 0. Stability rules (these come first)

1. **Do not edit** `Osc303.hpp`, `DiodeLadder303.hpp`, `Envelopes303.hpp`, `Voice303.hpp`, `Pattern.hpp`, `SeqEngine.hpp` or `Voice.cpp`, and change nothing in `Constants303.hpp` or `SBS.cpp` beyond what §1a lists. New constants go in a new file, `src/dsp/ConstantsX.hpp`. New DSP goes in new files (`OscX.hpp`, `Folder.hpp`, `DiodeLadderX.hpp`, `VoiceBackX.hpp`, `src/tuning/Scala.hpp`, `src/tuning/Tuner.hpp`). Shared helpers (`Filters1.hpp`, `Oversample.hpp`, `Blep.hpp`) may be included but not changed. You may include the round 1 headers and reuse their classes (for example `DecayEnv`, `AccentSweep`, `SlideLag`).
2. **Golden test.** Make the §1a cutoff change first. Then, before writing anything else, add `tests/test_golden.cpp`: render 4 s of the SBS demo pattern through `Voice303` (the settings of the existing end-to-end test, saw then square) and 4 s of `SeqEngine` outputs, hash the samples (FNV-1a over the raw float bits), and hard-code the hashes as produced at that point. This test must still pass at the end. If floating-point differences between compilers make an exact hash impossible in CI, compare against a stored 16-bit rendering with a tolerance of 1 LSB instead, and say so in DECISIONS.md.
3. The panel script must regenerate `res/SBV.svg`, `res/SBS.svg` and `res/SBX.svg` byte-identically (check with `git diff --stat` after `make panels`).
4. Version becomes `2.1.0`. New module slugs: `SBO`, `SBF`, `SBT`. Names in `plugin.json`: `SBO Silverbox Oscillator`, `SBF Silverbox Filter`, `SBT Silverbox Tuner`. Descriptions should contain "303" and "acid" (SBO, SBF) and "Scala microtuning" (SBT). Keep trademarks out of names and descriptions as before.

---

## 1a. The two changes to existing modules

**SBV cutoff calibration.** The service notes put the resonant frequency at 500 Hz ± 100 Hz with CUT OFF FREQ half way (full resonance, ENV MOD at minimum, saw at C 65.4 Hz). Round 1's range of 300 Hz to 5 kHz sits at about 1.2 kHz there. In `Constants303.hpp` set `CUT_LO = 125` and `CUT_HI = 2000`, update their comments, and nothing else. Re-run the round 1 tests and adjust only those whose numbers depend on the cutoff range (for example the BASS COMP test computes its knob position from the constants and should still pass; the output-level and render checks may need `OUT_LEVEL` left alone and their bounds reviewed). Add a test for the calibration condition (peak between 400 and 600 Hz). Record the change in DECISIONS.md and in the manual ("patches saved before 2.1.0 will sound darker; turn CUT OFF FREQ up by about a third of its travel to get the old sound").

**Scala tuning in SBS.** SBS uses the same `Scala.hpp` parser and the same mapping code as SBT (§4.3), applied to the engine's pitch output inside `SBS.cpp`; `SeqEngine.hpp` and the pattern memory are not touched.
- Context menu additions: Tuning ▸ Load Scala scale (.scl)… ; Load keyboard mapping (.kbm)… ; Built-in scales ▸ (the §4.4 list); Key mapping ▸ "12 keys per period" / "One key per degree"; Root ▸ C … B; Reset to 12-tone equal.
- The engine's semitone number for the playing note (pattern note + UP/DOWN + key transpose + track transpose) is mapped through the scale, so keys are scale degrees, UP and DOWN move by one period, and key transposes move by degrees. The **TRANSPOSE jack is added after the mapping as an exact pitch shift** (its semitone quantising stays).
- The scale is saved in the patch JSON under a new key (`tuning`), absent when 12-tone equal, so old patches load unchanged and a patch with no scale produces identical output (the golden test covers this).
- The display header shows the scale name after the tempo when a scale is loaded (shorten to fit).
- Hand-off from the UI thread as for memory import (atomic pointer swap).
- Tests: with no scale the output voltages equal round 1's; with Just intonation the demo pattern's notes land on the just pitches; UP adds exactly one period; TRANSPOSE adds exact volts.

## 1. What the three modules are for

SBO → SBF is the 303 voice with the lid off: SBO OUT into SBF IN, with the sequencer's 1V/OCT and SLIDE to SBO, and GATE and ACCENT to SBF. With every new control at its stock position that chain must sound like SBV (test in §5). SBT sits between any sequencer's 1V/OCT output and any oscillator.

Notation: **K** knob, **k** small knob, **S** switch, **I** input, **O** output, **L** light.

---

## 2. SBO, Silverbox Oscillator

### 2.1 Controls

| Type | Name | Range / default | Notes |
|---|---|---|---|
| K | TUNING | −12 to +12 semitones, 0 | |
| K | PULSE WIDTH | 5% to 95%, default 46.5% (stock) | Detent at stock, §2.3 |
| k | PWM | −1 to +1, 0 | Depth of the PWM input: ±5 V × depth = ±45% |
| K | WAVE MIX | 0 (saw) to 1 (square), 0 | Equal-gain crossfade; CV adds |
| K | SUB | 0 to 1, 0 | Level of the sub-octave into OUT |
| K | FOLD | 0 to 1, 0 | §2.5 |
| k | FOLD CV | −1 to +1, 0 | Depth of the FOLD input (±5 V = full range) |
| K | SYMMETRY | −1 to +1, 0 | Bias into the folder |
| k | FM | 0 to 1, 0 | Depth of the FM input |
| k | SLIDE TIME | 2 ms to 500 ms, 60 ms | As SBX |
| k | DRIFT | 0 to 1, 0 | §2.6 |
| k (TRIM row) | TUNE | −100 to +100 cents, 0 | Board trimmer TM4 |
| k (TRIM row) | WIDTH | 0.90 to 1.10 V/oct scale, 1.00 | Board trimmer TM5; scaling pivots at 0 V |
| k (TRIM row) | SAW CURVE | 0 to 3, 0.6 (stock) | The saw's curvature `K` |
| k (TRIM row) | SQUARE SHAPE | 1.5 to 20, 6 (stock) | Gain of the square's transistor shaper (`SQ_GAIN`): low is rounded and soft, high is hard-edged |
| I | 1V/OCT, SLIDE, PWM, MIX CV, FOLD, FM, SYNC, FOLD IN | | MIX CV: ±5 V = ±1. SYNC: rising edge through 0 V. FOLD IN is normalled to the mixed oscillator signal |
| O | SAW, SQUARE, SUB, OUT | | SAW and SQUARE about ±5 V; OUT is mix + sub, through the folder |

The panel has a titled `TRIM` group for the four trimmers. All labels as named above.

### 2.2 Core
- Pitch: `V' = WIDTH · V_in`; slide exactly as `SlideLag` in SBV (mode chosen at each pitch change; there is no gate here, so pass `gateRise = false`), with SLIDE TIME as in SBX. `f = 65.406 · 2^(V' + (TUNING + TUNE/100)/12 + drift)`.
- **FM** is linear: `f += FM · (FM_in / 5 V) · f` (so ±100% at full depth and ±5 V), clamped to ≥ 0 (no through-zero).
- **SYNC**: hard sync; reset the phase on a rising zero crossing with sub-sample position and a BLEP for the jump.
- Saw: `sawShape` with curvature `K = SAW CURVE` (K → 0 must be handled as a straight ramp without dividing by zero).
- Runs at the same oversampling as SBV (4× up to 50 kHz, 2× up to 100 kHz, else 1×). Outputs are decimated separately (SAW, SQUARE, SUB, OUT), so four decimators; keep CPU under 2%.

### 2.3 Pulse width
The square is still `tanh(SQ_GAIN · (saw − threshold))` followed by the same low-pass, high-pass and level as SBV. The threshold is what PULSE WIDTH moves: for a wanted duty `d` (the fraction of the cycle the square is high), `threshold = sawShape(d)` using the current SAW CURVE. `d = clamp(PULSE WIDTH + PWM · PWM_in/5 · 0.45, 0.05, 0.95)`. At the default positions this is identical to SBV's square (test).

### 2.4 Sub
A flip-flop toggled each time the main phase wraps gives a square one octave down. Band-limit it with a BLEP at the wrap, pass it through the same `SQ_LP` low-pass, level `0.8`. SUB output is always at full level; the SUB knob only sets how much goes into OUT.

### 2.5 Folder (`Folder.hpp`)
- Input `x = FOLD IN / 5` (or the internal mix when unpatched: `mix = (1−m)·saw + m·square + SUB·sub`).
- `g = 1 + 7 · fold²` where `fold = clamp(FOLD + FOLD CV · FOLD_in/5, 0, 1)`. `b = 0.8 · SYMMETRY · fold` (so symmetry does nothing when FOLD is 0).
- `y = tri(g · x + b)`, where `tri` is the reflecting triangle fold: identity for |u| ≤ 1, reflected at ±1 thereafter (`tri(u) = 4·|frac((u+1)/4) − 0.5| − 1` rearranged so that `tri(u) = u` on [−1, 1]). Round the corners over ±0.05 with a parabola so the aliasing target is met.
- Remove the DC that the bias adds with a one-pole high-pass at 10 Hz **only while `b ≠ 0`** (crossfade it in with |b|), so that at FOLD = 0 the path is exactly `y = x` (test: bit-identical to the unfolded mix when FOLD = 0 and the FOLD input is unpatched).
- Runs at the oversampled rate. Aliasing target: a 500 Hz saw at FOLD = 1 has aliases below −50 dB in 0 to 10 kHz at 48 kHz.

### 2.6 Drift
`drift` (in octaves) = `DRIFT² · 0.02 ·` a slow random walk: white noise through two one-pole low-passes at 0.3 Hz, normalised to about ±1. Seeded per module instance. Zero when DRIFT = 0.

### 2.7 Tests (SBO)
- At defaults, SAW and SQUARE equal `Osc303` saw and square sample for sample (same rate, same pitch).
- Duty measured at PULSE WIDTH 10%, 46.5%, 90% within ±1%; PWM input moves it; duty stays within 5% to 95%.
- WIDTH: at 1.05, a 2 V input gives 2.1 octaves. TUNE ±100 cents. SAW CURVE 0 gives a straight ramp (max deviation from linear < 1e-3).
- SUB is exactly half the main frequency and stays phase-locked over 10 s.
- SYNC to a 110 Hz master forces a 110 Hz period on a 300 Hz slave.
- FM at full depth with a ±5 V sine at 5 Hz swings the frequency between 0 and 2f.
- Folder: identity at FOLD 0; output stays within ±1; number of extrema per cycle rises with FOLD; SYMMETRY ≠ 0 adds even harmonics (2nd harmonic of a folded sine rises by > 20 dB); aliasing target of §2.5.
- Slide as SBV. DRIFT 0 gives a constant frequency; DRIFT 1 wanders by no more than ±25 cents.
- Stability and sample-rate independence as round 1 §9.

---

## 3. SBF, Silverbox Filter

The back half of the voice: input → diode ladder → VCA, with the MEG, the VEG and the accent circuit.

### 3.1 Controls

| Type | Name | Range / default | Notes |
|---|---|---|---|
| K | CUT OFF FREQ | 0 to 1, 0.5 | §3.3 |
| K | RESONANCE | 0 to 1, 0.5 | Also the accent sweep gang, as SBV |
| K | ENV MOD | 0 to 1, 0.5 | |
| K | DECAY | 0 to 1, 0.5 | MEG decay 0.2 to 2 s |
| K | ACCENT | 0 to 1, 0.5 | |
| S | MODE | LP / BP / HP, default LP | §3.4 |
| k | DRIVE | 0 to 24 dB, 0 | Gain into the ladder |
| k | BASS COMP | 0 to 1, 0 | As SBV |
| k | KEY TRACK | 0 to 1, 0 | Uses the 1V/OCT input; pivots at 1 V |
| k | FM | 0 to 1, 0 | Depth of the FM input: ±5 V × depth = ±2 octaves, applied at the oversampled rate |
| k | VCA DECAY | 16 ms to 6 s, 3.5 s (stock) | Curve as SBX |
| k | ACCENT DECAY | 30 ms to 3 s, 0.2 s (stock) | Curve as SBX |
| k (TRIM row) | CUTOFF TRIM | −2 to +2 octaves, 0 | Board trimmer TM3 |
| k (TRIM row) | RES TRIM | 0.5 to 1.1, default 1.0 | Scales the feedback at full RESONANCE as a fraction of the stock maximum; above about 1.005 the filter self-oscillates |
| k (TRIM row) | GATE TRIM | 20 ms to 2 s, then OFF at fully clockwise (default OFF) | §3.6 |
| k (TRIM row) | BASS | 20 Hz to 400 Hz, 150 Hz (stock) | Corner of the high-pass in the resonance feedback path |
| k (TRIM row) | POLES | 1.0 to 3.0, 1.8 (stock) | Ratio of the bottom ladder capacitor; 1.0 is four equal stages |
| k (TRIM row) | ENV BIAS | 0 to 2 octaves, 1.2 (stock) | How far full ENV MOD lowers the resting cutoff |
| k (TRIM row) | ENV ATTACK | 3 ms (stock) to 300 ms | Filter envelope attack. Longer values give swelling "wow" sweeps |
| k (TRIM row) | RELEASE | 4 ms (stock) to 1 s | How fast the VCA closes when the gate ends. Longer lets notes ring and overlap |
| k (TRIM row) | ACCENT VCA | 0 to 2, 1 (stock) | Scales how much an accent raises the volume, separately from how much it opens the filter (one pot does both on the board) |
| S + k (TRIM row) | SWEEP: GANGED / FREE, and SWEEP SHAPE 0 to 1 | GANGED (stock) | GANGED: the accent sweep's charge and discharge follow RESONANCE, as on the board's dual pot. FREE: they follow SWEEP SHAPE instead, so resonance and accent shape can be set independently |
| k (TRIM row) | CLICK | 0 to 1, 0 (stock model) | Adds the note-on thump of an untrimmed VCA: a fraction of the VEG's attack edge leaks into the output (band-limited, up to about 0.5 V), giving notes a percussive front |
| I | IN | | Audio, ±5 V nominal |
| I | GATE, TRIG, ACCENT | | §3.5 |
| I | 1V/OCT | | For KEY TRACK only |
| I | CUTOFF CV, RES CV, ENV MOD CV, DECAY CV, ACCENT CV | | CUTOFF: ±5 V = ±3 octaves. The others: ±5 V adds ±1 to the knob, clamped 0..1 |
| I | FM | | Audio-rate cutoff modulation |
| I | ENV IN | | 0 to 10 V; replaces the internal MEG while patched |
| O | OUT, FILTER OUT, FILTER ENV, ACCENT SWEEP, VCA ENV | | FILTER OUT is before the VCA, at a level that matches OUT for a held note. Envelopes 0 to 10 V |
| L | GATE, ACCENT | | |

Expect about 20 HP; widen as needed to meet the layout rules. Group the trimmers under a `TRIM` title (two rows if needed), and the CV jacks in one row directly under the knobs they belong to where that is possible.

### 3.2 Ladder (`DiodeLadderX.hpp`)
Same structure as `DiodeLadder303` (coupled four-stage ladder, trapezoidal integration, tridiagonal solve, saturating input node, feedback through a high-pass) with two things made variable:

- `c1` = POLES (was `STAGE1_RATIO`), and the feedback high-pass corner = BASS (was `FB_HP`).
- The resonant frequency factor (`LADDER_W180`) and the critical feedback both depend on `c1`, and the critical feedback also on `r = fc / BASS`. **Do not reuse the fitted constants.** Extend `scripts/ladder_calibration.py` to print, for `c1` from 1.0 to 3.0 in steps of 0.1: `w180(c1)` and `kcrit0(c1)` (no high-pass), and a fit of the high-pass correction `kcrit(c1, r) / kcrit0(c1)` and of the peak shift. Put the results in `ConstantsX.hpp` as tables with linear interpolation, evaluated at control rate (once per host sample at most). The existing rule stays: compensation only applies for `fc ≥ 4 · BASS` (which is 600 Hz at the stock 150 Hz), so resonance still thins at low cutoffs.
- At POLES = 1.8 and BASS = 150 Hz the tables must reproduce the round 1 values (1.3775, 17.03 and the fit `1 + 1.412/r + 0.576/r²` within 1%).
- Feedback: `k = RESONANCE_eff^0.8 · 0.995 · RES TRIM · kcrit(c1, fc)`. With RES TRIM > 1 the loop gain exceeds 1; the tanh at the input node limits the oscillation. Test that it settles to a stable sine between 0.3 and 2.0 (normalised) at 1 kHz with no input, and that nothing blows up at RES TRIM 1.1 with DRIVE at 24 dB and noise input.

### 3.3 Cutoff law and calibration
As SBV §5.3 with these changes:
```
base = log2(CUT_LO_X) + CUTOFF · log2(CUT_HI_X / CUT_LO_X) + CUTOFF TRIM
offset = −ENV_MOD · ENV BIAS
```
- **Calibration from the service notes:** with CUT OFF FREQ at 50%, RESONANCE at full, ENV MOD at 0 and a saw at C (65.4 Hz), the resonant frequency should be **500 Hz ± 100 Hz** (the TM3 adjustment). So `CUT_LO_X = 125 Hz`, `CUT_HI_X = 2000 Hz` (centre 500 Hz) with CUTOFF TRIM at 0. SBV is corrected to the same range in §1a. CUTOFF TRIM at +1.3 octaves reproduces the old, brighter round 1 range.
- Everything else (ENV_OCT 4, ACC_OCT 2.5, CUTOFF CV, KEY TRACK, FM, clamps) as SBV, using the SBF knob values with their CVs added.

### 3.4 Modes
The feedback always comes from stage 4, so resonance, the accent behaviour and the saturation are the same in every mode. Only the output tap changes.

- In the linear model, with `u` the (saturated) input-node signal and `y1..y4` the stage states, every stage has the same denominator `D(s)`, and the numerators of `u, y1, y2, y3, y4` have degrees 4, 3, 2, 1, 0 (in the open-loop sense). So any response with a numerator of degree ≤ 4 is a weighted sum `a0·u + a1·y1 + a2·y2 + a3·y3 + a4·y4`.
- **LP** = `y4` (stock).
- **BP**: weights that give a numerator proportional to `s²` (12 dB/oct up, then the ladder's roll-off).
- **HP**: weights that give a numerator proportional to `s⁴` (the low end rolls off at about 24 dB/oct below the cutoff, flat above).
- Derive the weights as functions of `c1` in `scripts/ladder_calibration.py` (symbolically with sympy, or numerically for the same `c1` grid and tabulated). Normalise BP so its peak gain at RESONANCE 0 is 0 dB, and HP so its gain well above cutoff is 0 dB.
- Tests: at RESONANCE 0 and fc = 1 kHz: BP is at least 20 dB down at 60 Hz and at 16 kHz relative to its peak; HP is at least 40 dB down at 60 Hz and within ±1.5 dB at 10 kHz; in all modes the resonant peak at RESONANCE 1 is within 10% of fc.
- BASS COMP only applies in LP (it would defeat the other two); in BP and HP it is ignored.
- Switching mode while running must not click more than the difference in the signals themselves: crossfade the output tap over 5 ms.

### 3.5 Gate, trigger, accent, envelopes
- **GATE** rising edge starts the MEG and VEG, exactly as SBV. No retrigger while it stays high.
- **TRIG** (when patched): a rising edge restarts both envelopes even while GATE is high. If GATE is unpatched, TRIG alone plays notes: each trigger opens an internal gate lasting until GATE TRIM's time (or 125 ms when GATE TRIM is OFF).
- **ACCENT is a level.** `on = V ≥ 1 V` (Schmitt 0.5/1.0). `strength = clamp(V / 5 V, 0.2, 1)` while on. A normal 5 V or 10 V gate gives strength 1, so SBS and any gate sequencer behave as with SBV. ACCENT CV adds to the ACCENT knob as usual; strength multiplies the accent sweep's contribution to cutoff and the VCA boost. The forced MEG decay (ACCENT DECAY) applies whenever `on` at the gate's rising edge, whatever the strength.
- **ENV IN** patched: `m(t) = clamp(ENV IN / 10, 0, 1)` replaces the MEG for the cutoff and for the accent sweep. FILTER ENV then outputs that value.
- MEG and VEG follow the round 1 classes with VCA DECAY, ACCENT DECAY, ENV ATTACK and RELEASE in place of the fixed values (write variants in the new files where the round 1 classes hard-code a constant). VEG attack as stock.
- Tests for the added controls: ENV ATTACK 100 ms puts the MEG peak 90 to 110 ms after the gate; RELEASE 500 ms leaves the VCA above 10% at 200 ms after gate off; ACCENT VCA 0 removes the level boost while the cutoff sweep is unchanged; SWEEP FREE makes the sweep peak time independent of RESONANCE; CLICK 0 is sample-identical to no click and CLICK 1 adds a transient of 0.3 to 0.6 V with a silent input.

### 3.6 GATE TRIM
Shortens notes from any sequencer. It is an absolute hold time: the VCA closes (stock 4 ms release) when GATE falls **or** when the time since the last envelope start exceeds GATE TRIM, whichever comes first. Fully clockwise is OFF: the note lasts as long as the gate (stock). Taper: exponential, 20 ms to 2 s over the first 95% of the travel, OFF above that; the tooltip shows milliseconds or "Off". The MEG is not affected. Note for the manual: a slide holds the gate across notes, so a short GATE TRIM will cut the second note of a slide; that is expected.

### 3.7 Output
`FILTER OUT = tap · FILTER_OUT_LEVEL`; `OUT = outHP(tap · VCA) · OUT_LEVEL`, soft limit ±10 V, as SBV. Oversampling as SBV. Input: `x = IN / 5`.

### 3.8 Tests (SBF)
- **Equivalence with SBV.** `Osc303` saw → SBF model with everything stock (after §1a both use 125 Hz to 2 kHz), driven by the demo pattern's gate and accent: output equals `Voice303` driven by the same pattern within −60 dB RMS error. (It will not be bit-exact, because SBV's slide and key tracking live in one object.)
- Calibration: the §3.3 condition gives a resonant peak between 400 and 600 Hz.
- Tables reproduce round 1 values (§3.2). Self-oscillation tests (§3.2). Mode tests (§3.4).
- BASS at 20 Hz: the resonance gain at an 80 Hz cutoff is within 3 dB of that at 800 Hz (compare round 1, where it is about 9 dB lower).
- POLES 1.0: slope between 2 and 5 octaves above fc is steeper than at POLES 1.8 by at least 1 dB/oct.
- GATE TRIM at 50 ms closes the VCA within 60 ms of the gate rise with the gate still high; OFF follows the gate.
- ACCENT at 2.5 V gives half the sweep contribution and half the VCA boost of 5 V; 10 V equals 5 V.
- TRIG restarts the MEG with GATE high. ENV IN replaces the MEG.
- All CV inputs move their targets; stability with noise on every input at four sample rates, including RES TRIM 1.1.

---

## 4. SBT, Silverbox Tuner

A 1V/OCT retuner that loads Scala files. It works with SBS and with any other sequencer or keyboard. About 8 HP.

### 4.1 Controls

| Type | Name | Range / default | Notes |
|---|---|---|---|
| Display | | | Slot number, scale name (from the .scl description, else the file name), number of notes, mapping mode |
| K | SCALE | slots 1 to 8, snap, default 1 | SCALE CV adds (0 to 10 V = 0 to 7 slots, wraps) |
| K | ROOT | C, C#, … B, snap, default C | Which key is the scale's 1/1 |
| k | FINE | −100 to +100 cents, 0 | Shifts everything |
| Button | LOAD | | Opens a file dialog for a .scl file into the current slot |
| I | 1V/OCT IN, SCALE CV | | IN is polyphonic (up to 16 channels) |
| O | 1V/OCT OUT | | Same channel count as IN |
| L | ACTIVE | | Lit when the current slot is not 12-tone equal |

Context menu: Load Scala scale (.scl)… ; Load keyboard mapping (.kbm)… ; Clear mapping; Built-in scales ▸ (list below); Key mapping ▸ "12 keys per period" / "One key per degree"; Reset slot to 12-tone equal; Reset all slots.

### 4.2 Scala parsing (`src/tuning/Scala.hpp`, no Rack includes)
- `.scl`: lines beginning with `!` are comments. First non-comment line is the description (may be empty). Next is the number of notes N. Then N pitch lines: a value containing a `.` is cents; otherwise a ratio `a/b` or an integer `a` (= a/1). Ignore anything after the first whitespace-delimited token on a pitch line. Negative ratios and N > 1024 are errors. N = 0 is valid (only 1/1). The last pitch is the period. Follow the format description at https://www.huygens-fokker.org/scala/scl_format.html.
- `.kbm`: per https://www.huygens-fokker.org/scala/help.htm#mappings: map size, first and last MIDI note, middle note, reference note, reference frequency, formal octave degree, then one entry per key (`x` = unmapped). Map size 0 means linear mapping.
- On a parse error leave the slot unchanged and show `FILE ERROR` on the display for 2 s. File reading happens in the UI thread; the parsed scale is handed to the audio thread through an atomic pointer swap (as SBS does for memory import), never read while half-written.
- Scales are stored **in the patch** (`dataToJson`): for each slot the name, the cents array, the mapping mode and the kbm data, so a patch does not depend on the file still being there.

### 4.3 Mapping
Work in semitone keys. `n = V_in · 12` (0 V = C). Let `key = floor(n)` and `frac = n − key`. Define `pitch(key)` in octaves below, and output `V_out = pitch(key) + frac · (pitch(key+1) − pitch(key)) + FINE/1200`. So whole-semitone inputs land exactly on scale pitches, and anything in between (a glide, a bend) is interpolated between its neighbours, so slides pass through SBT smoothly and the map is continuous.

With scale cents `c[0] = 0, c[1..N]`, period `P = c[N]`, and `k = key − ROOT`:
- **12 keys per period** (default): `oct = floor(k / 12)`, `d = k mod 12`. If N ≥ 12 use degree `d` (the first 12 degrees of each period). If N < 12 use degree `floor(d · N / 12)` (several keys share a degree). `pitch = (oct · P + c[d']) / 1200 + ROOT/12`.
- **One key per degree**: `oct = floor(k / N)`, `d = k mod N`, `pitch = (oct · P + c[d]) / 1200 + ROOT/12`. (Every key is the next scale step; the period falls every N keys.)
- With a `.kbm` loaded for the slot, its mapping replaces both modes: keys map per the file relative to its middle note (treat MIDI note 60 as 0 V), unmapped keys hold the previous output, and the reference note and frequency set the absolute pitch (convert with 0 V = C4 = 261.6256 Hz for the reference only).
- 12-tone equal in a slot gives `V_out = V_in + FINE/1200` exactly (test: error < 1e-6 V).

Note for the manual: SBS outputs exact semitone voltages, so SBS → SBT → SBV (or SBO) retunes the sequencer, UP and DOWN move by one period, and SBS's key transpose becomes a transpose by scale degrees. Through SBT the TRANSPOSE jack on SBS also moves by degrees; SBS's own Tuning menu (§1a) keeps that jack an exact pitch shift. Do not use both at once.

### 4.4 Built-in scales
So SBT is useful with no files: 12-tone equal; Just intonation (5-limit: 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8 2/1); Pythagorean; Quarter-comma meantone; Werckmeister III; Harmonic series 8 to 16 (9/8 10/8 … 16/8); 7-tone equal; 19-tone equal; 24-tone equal; Bohlen-Pierce (13 equal steps of 3/1); Wendy Carlos alpha (78.0 cent steps, period 15 steps); Slendro (5 equal, as an approximation); Pelog (a published approximation; cite the source in CREDITS.md). Check each table against a reference before committing, and log the sources. Default slots: slot 1 = 12-tone equal; slots 2 to 8 = Just, Pythagorean, Meantone, 7-equal, 19-equal, Bohlen-Pierce, Harmonic 8-16.

### 4.5 Tests (SBT)
- Parser: a cents file, a ratio file, a mixed file, comments and blank description, trailing text on pitch lines, N = 0, malformed files (reported, slot unchanged). Round trip through the patch JSON is exact.
- 12-equal identity. Just scale: key E (4 semitones above root C) gives 386.31 cents; G gives 701.96; one octave up adds exactly 1200.
- ROOT = D moves the 1/1 to D, and D itself is unchanged in pitch relative to 12-equal.
- Bohlen-Pierce in "One key per degree": 13 keys span 1901.96 cents. 19-equal in "12 keys per period": 12 keys reach degree 11, key 12 is +1200.
- N < 12 in "12 keys per period": 7-equal maps the 12 keys onto 7 degrees monotonically.
- Continuity: a slow ramp input gives a monotonic output with no jumps larger than the ramp's own step × the largest local slope.
- Polyphony: 16 channels in, 16 out, each mapped independently.
- kbm: a file that maps only white keys; unmapped keys hold.

---

## 5. Panels
- Add the three panels to `scripts/make_panels.py` using the existing `Panel` class and the clean silver theme. Rules as round 1 §3: every control labelled in words; outputs on a dark output block; the label check must pass; render and look at every panel.
- New panel features you will need: a titled group outline for `TRIM` (thin 0.3 mm outline with the title breaking the top edge), and the SBT display rectangle (reuse the SBS display mechanism). SBT's display is a NanoVG widget in the style of SBS's (same font, same amber on near-black).
- Existing panels must regenerate unchanged (§0.3).

## 6. Manual
Extend `docs/MANUAL.md` and regenerate the PDF and the panel image: a section per new module with control tables, and these recipes:
1. **The open voice**: SBS → SBO + SBF, stock settings, and what each trimmer does to the sound.
2. **PWM acid**: an LFO into PWM, square, high resonance.
3. **Folded acid**: FOLD swept by the FILTER ENV output of SBF (envelope-following fold), SYMMETRY off-centre.
4. **Band-pass and high-pass lines**, including two voices layered (SBV low-pass bass plus SBO/SBF high-pass on the same sequence).
5. **Self-oscillating filter as a voice**: RES TRIM up, nothing in IN, KEY TRACK at 100%, pitch into 1V/OCT and CUTOFF CV.
6. **Half accents**: a velocity or random CV into ACCENT.
7. **Sub and BASS**: three ways to keep the low end under resonance (SUB, BASS, BASS COMP) and how they differ.
8. **Wrong tracking**: WIDTH off-centre for a stretched scale, then the principled version with SBT.
9. **Microtonal acid**: SBS → SBT → SBV with Just intonation, then Bohlen-Pierce with "One key per degree"; switching scales per pattern with SCALE CV from a sequencer.
10. **SBF as a processor** for drums or another oscillator, with TRIG from a clock.

Also document the two §1a changes: SBV's corrected cutoff range, and the Tuning menu on SBS (with a note on when to use SBS's own tuning and when SBT: SBT for other sequencers, for CV-switched scale slots and for polyphony).

## 7. Order of work
1. SBV cutoff correction (§1a), then the golden test (§0.2).
2. `ConstantsX.hpp`, calibration script extensions and tables, `DiodeLadderX` with tests.
3. `OscX`, `Folder` with tests. `VoiceBackX` (SBF model) with tests, including the equivalence test.
4. `Scala.hpp`, `Tuner.hpp` with tests; Scala in SBS (§1a).
5. Modules `SBO.cpp`, `SBF.cpp`, `SBT.cpp`; panels; `plugin.json`; CI symbol check extended to the new names.
6. Manual, PDF, DECISIONS.md (continue the numbering), CREDITS.md. Full test pass, golden test still passing, CI green.
7. Hand-over note: what was built, decisions, anything unverified, and a rendered demo (`tests/out/demo2.wav`: the demo pattern through SBO + SBF with a fold sweep, then band-pass, then through SBT in Just intonation).

## 8. Accuracy ledger
**From sources:** board trimmers TM3 (VCF cutoff; 500 Hz ± 100 Hz resonance at half cutoff, full resonance, C 65.4 Hz, per the Roland notes as quoted in the x0xb0x build pages), TM4 (VCO tune, A = 110 Hz) and TM5 (VCO width, 2:1 octaves). Scala file formats (Huygens-Fokker). SH-101-style divider sub-oscillator.
**Not confirmed:** the functions of the board's other trimmers (the sources reached list only TM3 to TM5 plus an optional VCA offset trim). GATE TRIM, RES TRIM, BASS, POLES, ENV BIAS, ENV ATTACK, RELEASE, ACCENT VCA, SWEEP and SQUARE SHAPE are fixed component values or ganged parts made variable for this plugin, not original trimmers; say so in the manual.
**Estimates:** folder law and corner rounding, drift depth and rate, sub level, accent strength law, all ranges of the new knobs, the BP/HP normalisation.
**Sources:** https://smakela.github.io/re-303-buildguide/docs/build_calibration ; https://www.ladyada.net/make/x0xb0x/fab/vcf/index.html ; https://www.manualslib.com/manual/3378574/Roland-Tb-303.html ; https://www.huygens-fokker.org/scala/scl_format.html
