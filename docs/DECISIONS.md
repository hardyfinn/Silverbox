# Decisions

Where the build departs from, or fills a gap in, `docs/BUILD-INSTRUCTIONS.md`. Each one picks the reading closest to the original hardware or to what the spec's own tests require. Every constant mentioned lives in `src/dsp/Constants303.hpp`.

## Voice model

1. **Filter structure.** The ladder is the coupled four-stage diode ladder, with the bottom stage's capacitor ratio `STAGE1_RATIO = 1.8`. It is integrated with the trapezoidal rule as one tridiagonal system per sample, and the saturating input node is solved with two Newton steps. With this structure the resonant peak sits at 1.3775 × the matrix's base frequency, and self-oscillation needs a feedback of 17.03, the same figure Open303 uses. `scripts/ladder_calibration.py` reproduces both numbers.
2. **What "cutoff" means.** The knob and the cutoff law (spec §5.3) set the **resonant peak frequency**. With no resonance this ladder's −3 dB point is about a tenth of that, as on the original. So the spec's estimated range of 160 Hz to 2.4 kHz sounded far too dark, and round 1 used **300 Hz to 5 kHz**. In 2.1.0 this was corrected to the service-note calibration, **125 Hz to 2 kHz** (item 29).
3. **Slope.** Between 1 and 4 octaves above the peak the measured slope is 16 dB/oct, which is inside the spec's 15 to 21 range. The test allows up to 22 dB/oct, because this structure tends towards 21 to 23 dB/oct further up.
4. **Resonance scaling.** With the 150 Hz feedback high-pass in the loop, the feedback needed to self-oscillate rises at low cutoffs: `kcrit(fc) = 17.03 × (1 + 1.412/r + 0.576/r²)`, where `r = fc / 150 Hz`. Full RESONANCE is set to 99.5% of that. This compensation only applies down to 600 Hz (`KCOMP_MIN_FC`), so resonance still thins out at low cutoffs, as it does on the original. Measured: at full resonance an impulse is still about −19 dB after 200 ms and dies away within 3 s. The resonance gain is 0.9 dB at 80 Hz against 9.8 dB at 800 Hz.
5. **Peak test.** The feedback high-pass moves the actual peak up to about 1.10 × fc at a 1 kHz cutoff. So the test looks for the peak between 0.9 × and 1.2 × fc, and measures the passband at fc/4. The passband has to be above the feedback high-pass to mean anything, and fc/20 would not be. Measured: the peak is 28.9 dB above the passband.
6. **Square shaper.** `SQ_OFFSET = −0.08` gives a duty of 46.5%. The spec's +0.12 gave 37%. `SQ_LEVEL = 0.55` keeps the square's RMS below the saw's (0.49 against 0.58).
7. **Attack times.** The envelopes rise with a one-pole curve aimed at 1.2. Its time constant is scaled so that the level reaches 1 at the stated attack time (3 ms).
8. **Accent sweep.** `SWEEP_TC_MIN = 0.8 ms` (the spec estimated 2 ms) puts the low-resonance peak 4.8 ms after the note, inside the 6 ms target. The other sweep constants are as specified. Measured at full resonance: the first peak comes 59 ms in, and four accents climb 0.27 → 0.40 → 0.46 → 0.49, so the fourth is 1.78 × the first.
9. **BASS COMP.** Boosting the ladder's own output, as the spec first described, could not reach the ±2 dB target without lifting the resonant peak. The lost bass is instead added back from the ladder's **input**, through a two-pole low-pass at `min(0.1 × fc, 250 Hz)`, scaled by `RESONANCE^0.8 × 1.1`. Measured at full resonance with a 1 kHz cutoff: 50, 75 and 100 Hz tones come out at +0.9, −0.2 and −1.4 dB against resonance zero, where without BASS COMP they would be −16 to −19 dB. The peak changes by 0.2 dB. At 0, BASS COMP leaves the signal exactly as stock (tested sample for sample).
10. **Output level.** `OUT_LEVEL = 32` puts a default saw at about 4.6 V peak. Extreme settings (square wave, resonance near full, BASS COMP up, accents) drive the final soft limiter at ±10 V. That is louder than the spec's "about ±7 V accented", so it is deliberate: it behaves like a hot 303 into a mixer.
11. **Oversampling** follows the sample rate: 4× up to 50 kHz, 2× up to 100 kHz, 1× above. This keeps CPU flat, as in the Serge plugin. Measured aliasing of a 2 kHz saw or square at 48 kHz is below −130 dB in the 0 to 10 kHz band.
12. **Slide** uses a one-pole time constant of `slideTime / 1.715`, so a glide is 82% done at SLIDE TIME (60 ms stock).

## Sequencer

13. **TIE with nothing to tie** (step 1, or straight after a REST) plays as a REST, as the spec suggests. No source said otherwise.
14. **Edit target.** Every edit (write modes, scramble, shifts, direct edit, LENGTH, TRIPLET) goes to the pattern selected by PATTERN plus PATTERN CV. That pattern only starts playing at the end of the current loop. The display names both when they differ.
15. **LENGTH and TRIPLET** follow the selected pattern's memory, so they jump to its stored values when you change pattern, and write to memory when you move them.
16. **Track write** auditions the pattern under the PATTERN knob, because it is queued for playback like in Pattern play.
17. **Triplet with "1 per step" clocks** has no effect on timing, since each clock pulse is one step.
18. **Clock input "4 PPQN"** behaves exactly like "1 per step" (a 16th note per pulse). It is listed only so the name matches what other clocks call it.
19. **Mutate** works on the pattern that has just finished playing, so the changes you hear always belong to the pattern on the display.

## Panels

20. **Widths.** To meet the spacing rules with every label in place, some modules are wider than the spec's guide widths: SBV is 16 HP (spec 12/14), SBS 32 HP (spec 24) and SBX 12 HP (spec 8).
21. **MODE** is five lit buttons, one per mode, rather than a five-position rotary switch. The five position names would not fit around one knob with readable labels. The mode is still a single (hidden) parameter, so it is saved, and it can be MIDI-mapped through the buttons.
22. **SBX CV jacks** are labelled `CV` and joined to their knob by a line. The tooltip gives the full name.
23. **Lettering** is DejaVu Sans Bold condensed to 86% and converted to paths, as in the Serge plugin.
24. **Keyboard** labels: the upper C is `HI C`.

25a. **Panel colour.** Hardy found the brushed-silver panels dirty-looking and chose flat "clean silver" (2026-11-15): light silver `#e3e5e7`, charcoal bands, no texture. `scripts/mock_themes.py` renders the alternatives that were offered.

## Build

25. Only the **Apple silicon Mac** plugin is built in CI (Hardy's request, 2026-11-15). The model tests run on Linux.
26. Tests use the small built-in harness from the Serge plugin (`tests/check.hpp`), so `make test` needs nothing downloaded.

## Not verified

27. The plugin compiles and links for macOS arm64 in CI, and all the models are tested on the host. It has **not yet been opened in Rack**, so the panel widgets and the SBS display have not been seen running.
28. No 303 schematic or measurement was reachable as text, so the estimated constants in spec §12 stand, apart from the changes above. The sound has not been tuned by ear.

## Round 2 (2.1.0): SBO, SBF, SBT

Where the build departs from, or fills a gap in, `docs/BUILD-INSTRUCTIONS-ROUND-2.md`. New constants live in `src/dsp/ConstantsX.hpp`.

29. **SBV cutoff calibration.** `CUT_LO = 125 Hz`, `CUT_HI = 2000 Hz`, as Hardy asked: with CUT OFF FREQ half way, full resonance, ENV MOD 0 and a saw at C 65.4 Hz, the measured resonant peak is 580 Hz (spec 500 ± 100 Hz; the feedback high-pass raises the peak about 10% above the control value of 500 Hz). Patches saved before 2.1.0 sound darker. The round 1 ringing test measured at CUT OFF FREQ 0.5; it now uses the old half-way frequency, 1.22 kHz, since 500 Hz is below the 600 Hz compensation corner where resonance thins by design.
30. **Golden test.** `tests/test_golden.cpp` hashes (FNV-1a over float bits) 4 s of the demo pattern through `Voice303` and 4 s of `SeqEngine` output, as produced right after item 29. The hashes match in CI (same compiler family). In case a compiler changes the last bits, it falls back to stored renderings in `tests/golden/` (16-bit voice samples within 1 LSB; sequencer output changes within 1e-4 V) and says so. `--write-golden` regenerates them. A round 1 sequencer test read one sample past the end of its log (it passed by luck); it now runs 10 ms longer.
31. **Ladder tables.** `scripts/ladder_calibration.py` now prints `w180`, `kcrit0` and the high-pass correction for POLES (c1) 1.0 to 3.0 and emits the C++ tables. At c1 = 1.8 it reproduces round 1's numbers within 0.05%, and the 1.8 row holds round 1's exact constants, so SBF at stock runs the ladder sample-for-sample like SBV (tested). The peak-shift fit (about `1 + 0.75/r − 0.30/r²`) is printed for the record but not applied, because applying it would make SBF differ from SBV.
32. **BP and HP taps** have closed forms: BP = g(c1) · (y2 − 4 y3 + 5 y4) and HP = u − (1 + 6/c1) y1 + (1 + 26/c1) y2 − (48/c1) y3 + (42/c1) y4, so only the band-pass gain g(c1) needs a table. Normalised as the spec asks (BP 0 dB peak, HP 0 dB above cutoff). Measured at fc = 1 kHz: BP −25.5 dB at 60 Hz and −30.3 dB at 16 kHz; HP −82 dB at 60 Hz and −0.5 dB at 10 kHz; the resonant peak is at 1.09 fc in all modes.
33. **BP and HP output levels.** Because the low-pass tap sits about 20 dB down at typical resonance (OUT_LEVEL makes that up), the normalised BP and HP taps would clip SBF's output. They are scaled by `BP_LEVEL = 0.12` and `HP_LEVEL = 0.15` so a default saw note peaks at a similar level in all three modes.
34. **POLES does very little.** The spec expected POLES 1.0 to be at least 1 dB/oct steeper above cutoff. In this model the slope 2 to 5 octaves above fc is 23 dB/oct for every c1 from 0.3 to 10, because the ladder is rescaled so the resonance stays on fc. What changes is the passband just below fc (0.5 dB at fc/4 between 1.0 and 1.8) and the feedback needed to resonate. The test checks that instead. **Hardy may want POLES replaced** with something more audible; it is kept because the spec asked for it.
35. **BASS test.** Resonance gain is measured as the height of the resonant peak (searched 0.9 to 1.3 fc), not the gain at fc, because the feedback high-pass moves the peak above fc at low r. Measured with BASS 20 Hz: 27.8 dB at an 80 Hz cutoff against 30.4 dB at 800 Hz (within 3 dB). At stock BASS the 80 Hz figure is 1.3 dB.
36. **Self-oscillation level.** At RES TRIM 1.1 and 1 kHz the filter settles to a steady sine at 0.57 of the ladder's ceiling (the input node saturates; stage 4 is 1/kcrit of it at resonance), about 1.4 V at OUT, and at 1.10 × fc (the feedback high-pass shift). The spec's "0.3 to 2.0 normalised" is read against that ceiling. A noise floor of 1e-5 is added at the ladder input only while RES TRIM is above 1, so an empty filter starts oscillating.
37. **SBF equivalence** is not just within −60 dB but sample-identical, because SBF reuses the same ladder arithmetic, envelopes and accent code at stock (`tests/test_sbf.cpp` feeds it the oscillator at the oversampled rate, as inside SBV). Through cables at the host rate, SBO → SBF adds the oversampling filters' delay (under 1 ms) and is not bit-identical.
38. **Accent level law.** `strength = clamp(V / 5, 0.2, 1)` while ACCENT is on (Schmitt 0.5 / 1.0 V); it is held after the accent ends so the sweep's tail keeps its strength. Strength scales the accent's cutoff contribution and VCA boost; ACCENT DECAY applies to any accent.
39. **CLICK** is the VEG's rate of rise (normalised to the 3 ms attack), low-passed at 2.5 kHz and added to OUT: a one-sided thump of about 0.45 V at CLICK 1.
40. **SBO pulse width.** The square's threshold is `sawShape(d)` at the current SAW CURVE plus a tiny constant (−0.0008) that makes the stock 46.5% give exactly SBV's −0.08, so the stock square is sample-identical (tested) while every other width lands within 1%. The SQUARE output jack is scaled to about ±5 V; OUT keeps SBV's saw/square balance.
41. **SBO sub** toggles on each phase wrap and also on a sync reset that comes late in the cycle (after half), so sync does not leave the sub stuck.
42. **SBO OUT** is clamped at ±12 V: with a biased fold the DC removal can push it past ±10 V.
43. **Folder at FOLD 0** is bypassed entirely (bit-identical to the mix); the corner rounding only applies once FOLD is above 0. Measured aliasing at FOLD 1 on a 500 Hz saw: −76 dB.
44. **Drift** is white noise through two 0.3 Hz one-poles, normalised to a standard deviation of 0.4 and soft-limited to ±1, times 0.02 octaves: never more than ±24 cents. Seeded per module instance.
45. **SBF CV ranges.** RES, ENV MOD, DECAY and ACCENT CV add ±1 per ±5 V, as specified. GATE TRIM's tooltip shows ms or "Off".
46. **Scala.** `.scl` N = 0 means only 1/1; its period is taken as an octave. A `.kbm` "formal octave degree" of 0 means the scale's own period. With a `.kbm`, ROOT is ignored (the file's middle note decides). Inputs within 1e-4 semitone of a whole key snap to it, so sequencer voltages land exactly on scale pitches.
47. **SBS tuning and the TRANSPOSE jack.** The engine adds the jack's semitones when a NOTE step starts, so SBS latches the jack value at that moment, maps `voct − jack/12` through the scale and adds the jack back. While stopped (key previews) no jack is applied. With 12-tone equal (or no tuning saved) the output is the engine's output unchanged.
48. **Built-in scales.** Bohlen-Pierce and Wendy Carlos alpha default to "One key per degree" since neither repeats at the octave. Wendy Carlos alpha uses a 15-step, 1170-cent period as the spec says. Pelog is Bill Alves's just-intonation pelog from the Scala archive (`alves_pelog.scl`): 8/7 6/5 21/16 3/2 8/5 7/4 2/1. SBT's slot 6 is 19-equal and slot 8 Harmonic 8 to 16, as specified.
49. **SBT's LOAD button** opens the file dialog from the UI thread (the module widget watches the button), and loaded scales reach the audio thread by pointer swap. Replaced scales are freed when the module is removed, never while the audio thread might read them.
50. **Widths.** SBO 20 HP, SBF 28 HP (spec "about 20", widened for 5 knobs with CV jacks, 6 small knobs, 12 trimmers and 12 jacks within the spacing rules) and SBT 8 HP.
51. **Not verified.** SBO, SBF and SBT compile and link for macOS arm64 in CI and their models are tested on the host, but they have **not been opened in Rack**. The new knob ranges, the folder, drift, click and accent law are estimates, not measurements.
