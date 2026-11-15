# Decisions

Where the build departs from, or fills a gap in, `docs/BUILD-INSTRUCTIONS.md`. Each one picks the reading closest to the original hardware or to what the spec's own tests require. Every constant mentioned lives in `src/dsp/Constants303.hpp`.

## Voice model

1. **Filter structure.** The ladder is the coupled four-stage diode ladder, with the bottom stage's capacitor ratio `STAGE1_RATIO = 1.8`. It is integrated with the trapezoidal rule as one tridiagonal system per sample, and the saturating input node is solved with two Newton steps. With this structure the resonant peak sits at 1.3775 × the matrix's base frequency, and self-oscillation needs a feedback of 17.03, the same figure Open303 uses. `scripts/ladder_calibration.py` reproduces both numbers.
2. **What "cutoff" means.** The knob and the cutoff law (spec §5.3) set the **resonant peak frequency**. With no resonance this ladder's −3 dB point is about a tenth of that, as on the original. So the spec's estimated range of 160 Hz to 2.4 kHz sounded far too dark, and the range is now **300 Hz to 5 kHz** (`CUT_LO`, `CUT_HI`).
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
