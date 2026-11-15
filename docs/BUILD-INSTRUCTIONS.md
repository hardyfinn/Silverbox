# Build instructions: SILVERBOX, a TB-303 voice and sequencer for VCV Rack

**Audience:** an Opus model implementing this plugin from scratch.
**Owner:** Hardy (also Haer). Fluent in VCV Rack, does not read C++. Build the whole plugin in one go, test it yourself, hand over the finished result. No milestone check-ins.

**What it is:** a VCV Rack 2 plugin, brand and slug `Silverbox`, with three modules that together reproduce the Roland TB-303 Bass Line (1981): the voice, the sequencer with its separate Pitch and Time programming, and an optional expander with Devil-Fish-style extensions.

**Status of decisions.** Hardy confirmed all three choices on 2026-11-15 (three modules; faithful Pitch/Time mode plus direct edit; extras as the SBX expander). Background research is in `docs/RESEARCH-AND-PROPOSAL.md`.

| Question | Answer used here |
|---|---|
| Shape | Three modules: SBV Voice, SBS Sequencer, SBX Extended (expander for SBV) |
| Sequencer programming | Faithful Pitch/Time programming is the default, plus a merged-result display and a direct-edit mode |
| Devil Fish extras | Only in SBX. SBV alone is a stock 303 when its two extra knobs are at 0 |
| Hardy's additions (2026-11-15) | On SBV itself: **BASS COMP** (boost the bass that drops away as resonance goes up) and **KEY TRACK** (filter cutoff follows pitch). Both default to 0 = stock |
| Names | "Roland", "TB-303", "Bass Line" and "Devil Fish" never appear in plugin, module, panel or tag names. The manual may name them as what is being modelled |
| Look | Text labels on every control and jack, no symbols |
| Repo | `hardyfinn/silverbox`, work on `main` |

---

## 0. Ground rules

1. **Faithful first.** SBV with default knobs and no SBX must sound and behave like a stock 303 fed by its own sequencer. Every modern addition (CV inputs, Audio In, direct edit, mutation tools, SBX) is additive and leaves the stock behaviour alone when unused.
2. **Model the circuit, not the idea.** Sections 5 to 7 give behavioural models with constants. Where a schematic or measurement from the §12 sources is reachable, check the model against it and prefer the source. Log every change in `docs/DECISIONS.md`.
3. **All tunable constants live in one place**: `src/dsp/Constants303.hpp`, one named `constexpr` per value in §12's estimate list, each with a comment saying where it came from. Hardy may want them tuned by ear later.
4. **DSP separate from Rack.** All models and the sequencer engine live in `src/dsp/` and `src/seq/` as plain C++17 with no Rack includes, so they can be unit tested on the host. `src/modules/*.cpp` are thin wrappers.
5. **No allocation, locks, file I/O or logging in `process()`.** UI-to-engine edits go through a lock-free single-producer queue or atomics; the engine owns the pattern memory during `process()`.
6. **Every param has a tooltip and unit, every port a name.** The tooltip name is the printed panel label, spelled out in full where the label is abbreviated.
7. **Stability.** Every state variable is clamped; every `process()` ends with an `isfinite` guard that resets state to 0. Outputs are within ±10 V (audio soft-clipped at ±10 V).
8. **Ambiguity:** pick the reading closest to the original hardware, write it in `docs/DECISIONS.md`, list it in the hand-over.
9. **CPU:** SBV (with 4× oversampling) under 1.5% of one core at 48 kHz, SBS under 0.3%, SBX negligible.
10. **Polyphony:** none. Every input reads channel 0; every output is 1 channel.
11. Licence GPL-3.0-or-later. Open303 (MIT) may be read and its ideas used; if any code is copied, credit it in `docs/CREDITS.md` with its licence.

---

## 1. Setup

- **VCV Rack 2.x**, newest 2.x Rack SDK. Read https://vcvrack.com/manual/PluginDevelopmentTutorial, https://vcvrack.com/manual/Building, https://vcvrack.com/manual/Panel, https://vcvrack.com/manual/DSP and https://vcvrack.com/manual/Expanders (SBX) first.
- `export RACK_DIR=/path/to/Rack-SDK`, then `make`, `make dist`, `make install`.
- Unit tests with **doctest** in `tests/`; `make test` builds `tests/*.cpp` with `src/dsp/*.cpp` and `src/seq/*.cpp` as a host executable and runs it.
- `.github/workflows/build.yml`: build Windows x64, macOS x64 + arm64 and Linux x64 with the official `rack-plugin-toolchain` Docker image (or the community `build-plugin` action based on it), run `make test` on Linux, upload `.vcvplugin` artifacts on every push and release assets on tags `v*`. Reuse the working workflow from `hardyfinn/Serge` if it is reachable. Install steps (drop the `.vcvplugin` into Rack's `plugins-<os>-<cpu>` user folder) go in `README.md`.

```
plugin.json   Makefile   README.md   LICENSE
docs/   BUILD-INSTRUCTIONS.md (this file)  DECISIONS.md  CREDITS.md  MANUAL.md  Silverbox-Manual.pdf
res/    SBV.svg  SBS.svg  SBX.svg  components/  fonts/ (OFL font for the display)
scripts/ make_panels.py  make_manual_pdf.(py|sh)
src/
  plugin.hpp  plugin.cpp
  dsp/   Constants303.hpp  Oversample.hpp  Blep.hpp  Osc303.hpp  DiodeLadder303.hpp
         Envelopes303.hpp  AccentSweep.hpp  SlideLag.hpp  Voice303.hpp (+ .cpp as needed)
  seq/   Pattern.hpp  Merge.hpp  SeqEngine.hpp  Mutate.hpp  Clock.hpp (+ .cpp)
  modules/  SBV.cpp  SBS.cpp  SBX.cpp
  widgets/  SilverboxComponents.hpp  SeqDisplay.hpp  Keyboard.hpp
tests/
```

`plugin.json`: slug `Silverbox`, name `Silverbox`, brand `Silverbox`, version `2.0.0`, licence `GPL-3.0-or-later`.

| Slug | Module name (as shown in the browser) | HP | Tags |
|---|---|---|---|
| `SBV` | `SBV Silverbox Voice` | 14 | Synth voice, Filter, Monophonic |
| `SBS` | `SBS Silverbox Sequencer` | 24 | Sequencer, Clock generator |
| `SBX` | `SBX Silverbox Extended` | 8 | Expander |

Module descriptions in `plugin.json` should contain the words "303" and "acid" so the browser search finds them (descriptions are not names; keep "TB-303" and "Roland" out of them too).

---

## 2. Signal conventions

| Signal | Level |
|---|---|
| Pitch | 1 V/oct. **0 V = C2 = 65.406 Hz** at Tuning centre. SBS outputs −1 V to +2 V (three octaves plus transpose) |
| Gate | 0 or +10 V out. Inputs high above 1 V, Schmitt trigger 0.5 V / 1.0 V |
| Accent, Slide | 0 or +10 V out, read as gates |
| Audio | SBV output about ±5 V on normal notes, up to about ±7 V accented, soft limit ±10 V |
| Envelope outputs | 0 to +10 V |
| CV inputs | ±5 V covers the full range of the knob they add to, unless stated |
| Triggers out (Clock, End) | +10 V, 1 ms |

---

## 3. Panels

### 3.1 Look
- Brushed-aluminium ground: light silver `#cfd2d4` with a faint horizontal brushed texture (a few hundred 2% opacity hairlines; keep SVG small). A black band across the top 16 mm holds the module title in silver. A thin black band at the bottom carries the word `SILVERBOX`.
- Ink: near-black `#1b1b1b`. Section dividers 0.5 mm.
- Knobs: black, round, skirted, with a white line indicator (the 303's knob look), 11 mm; a 8 mm version for attenuators and SBX.
- Jacks: Rack-style 3.5 mm jacks with a dark nut. Outputs sit on a black rounded rectangle (inverse labels in silver) so inputs and outputs are told apart at a glance.
- Buttons (SBS): small rectangular keys. White keys and black keys for the keyboard; grey function keys; red LED above each key that has a state.
- Switches: chrome toggles.

### 3.2 Labels
- **Every knob, switch position, jack, button and LED has a printed text label.** No pictograms. No waveform icons (the waveform switch says `SAW` and `SQUARE`).
- The label text is the name in the module tables below, verbatim, in capitals. The six SBV knobs use the original 303's printed wording: `TUNING`, `CUT OFF FREQ`, `RESONANCE`, `ENV MOD`, `DECAY`, `ACCENT`, and the switch is `WAVEFORM` with positions `SAW` / `SQUARE`.
- Title at the top: `SBV · VOICE`, `SBS · SEQUENCER`, `SBX · EXTENDED`.
- Placement: knob label centred above the knob, jack label centred below the jack, button label below the button. Labels never overlap and are never rotated.
- Type: an OFL sans (for example "Jost" or "Barlow Condensed"), capitals, 2.2 mm cap height, 1.8 mm minimum, 3 mm title. **Convert all text to paths.**
- **The check:** `scripts/make_panels.py` builds the SVGs and a C++ header of positions from one shared table, and fails if any control lacks a label or the label set differs from the tables in §5 to §7.

### 3.3 Layout rules
- Minimum centre spacing: jack to jack 10 mm, knob to knob 14 mm, knob to jack 12 mm.
- Render every panel to PNG (CI or locally with Playwright or Inkscape) and look at each one before hand-over: no overlaps, no clipped text.

---

## 4. Shared DSP

- **Oversampling** (`Oversample.hpp`): the whole SBV audio path (oscillator, filter, VCA) runs at 4× with a polyphase half-band up/down pair (or Rack's `dsp::Decimator`). Control signals (envelopes, sweep, slide) run at 1× and are linearly interpolated across the 4 sub-samples.
- **Band-limiting**: PolyBLEP (or minBLEP) on the saw reset, applied at the oversampled rate.
- **Fast exp2/tanh**: use Rack's `dsp::approxExp2_taylor5` and a rational tanh; check accuracy in tests (pitch error < 1 cent).
- **Denormals**: flush to zero (Rack does this on its audio thread; tests must do it too).

---

## 5. SBV, Silverbox Voice (14 HP)

Notation: **K** knob, **S** switch, **I** input, **O** output, **L** light.

| Type | Name | Range / default | Notes |
|---|---|---|---|
| K | TUNING | −12 to +12 semitones, default 0 | Continuous, not quantised |
| K | CUT OFF FREQ | 0 to 1, default 0.5 | §5.3 |
| K | RESONANCE | 0 to 1, default 0.5 | Also the accent sweep gang, §5.5 |
| K | ENV MOD | 0 to 1, default 0.5 | §5.3 |
| K | DECAY | 0 to 1, default 0.5 | MEG decay, §5.4 |
| K | ACCENT | 0 to 1, default 0.5 | §5.5 |
| S | WAVEFORM | SAW / SQUARE, default SAW | |
| K (small) | BASS COMP | 0 to 1, default 0 | Hardy's addition. Restores the low end lost as resonance rises, §5.2a |
| K (small) | KEY TRACK | 0 to 100%, default 0 | Hardy's addition. Cutoff follows pitch, §5.3 |
| I | 1V/OCT | | |
| I | GATE | | Rising edge triggers both envelopes |
| I | ACCENT | | High = accented note |
| I | SLIDE | | §5.6 |
| I | CUTOFF CV | ±5 V = ±3 octaves | Added to cutoff in octaves |
| I | AUDIO IN | ±5 V | When patched, replaces the oscillator |
| O | OUT | | Audio |
| O | FILTER ENV | 0 to 10 V | MEG output m(t) × 10 V |
| O | ACCENT SWEEP | 0 to 10 V | Sweep state s(t) × 10 V |
| L | GATE, ACCENT, SLIDE | | One LED above each of those inputs |

### 5.1 Oscillator (`Osc303`)
- Frequency `f = 65.406 · 2^(V + tuning/12)` Hz, V from the slide lag (§5.6). Clamp 8 Hz to 8 kHz.
- **Saw**: phase `p` from 0 to 1. Shape is an exponential capacitor charge, `saw(p) = (1 − e^(−K_SAW·p)) / (1 − e^(−K_SAW))`, mapped to ±1 and inverted so it is a falling ramp as on the 303. `K_SAW = 0.6` (est.). Reset band-limited with PolyBLEP.
- **Square**: derived from the saw by the one-transistor shaper, not a separate oscillator:
  `sq = tanh(SQ_GAIN · (saw − SQ_OFFSET))`, `SQ_GAIN = 6`, `SQ_OFFSET = 0.12` (gives a duty a little off 50%, est.), then a one-pole low-pass at `SQ_LP = 7 kHz` (slanted edges) and a one-pole high-pass at `SQ_HP = 20 Hz` (top droop), level `SQ_LEVEL = 0.7` relative to the saw (est.). The tanh runs at 4×, which keeps aliasing low enough; check it in a spectrum test.
- **AUDIO IN** patched: input / 5 replaces the oscillator signal (no change to the rest of the path).

### 5.2 Diode ladder filter (`DiodeLadder303`)
- A **4-pole diode ladder** solved as a zero-delay-feedback (ZDF / TPT) network at the 4× rate. Use the coupled diode-ladder topology (stages are not buffered from each other), not a Moog ladder. Will Pirkle's "Virtual Analog Diode Ladder Filter" app note and Zavalishin's *The Art of VA Filter Design* §5 give the structure; a per-stage tanh nonlinearity solved with one Newton step (or the "cheap" method of evaluating tanh on the previous-sample estimate) is enough.
- **Unequal capacitors**: the bottom (input) stage's capacitor is smaller than the other three, so its pole is higher: `STAGE1_RATIO = 1.8` × the other stages' cutoff (est.; check Stinchcombe, §12). This gives the roughly 18 dB/oct slope and the softer, rounder peak.
- **Resonance**: feedback gain `k = RESONANCE^0.8 · K_MAX`, with `K_MAX` set so that at RESONANCE = 1 the filter rings for a long time after an impulse but does **not** sustain a stable self-oscillation at default cutoff (test in §9).
- **Feedback high-pass**: a one-pole high-pass at `FB_HP = 150 Hz` (est.) inside the feedback path, so resonance thins out and fades below about 150 Hz.
- **Input coupling** one-pole high-pass `IN_HP = 30 Hz`; **output coupling** one-pole high-pass `OUT_HP = 40 Hz` after the VCA (est.).
- **Drive**: the signal enters the ladder at `DRIVE_IN = 1.0` (normalised so a full saw lightly saturates at high resonance). SBX OVERDRIVE scales this.
- **Gain compensation**: none by default. Like the original, the passband drops as resonance rises. BASS COMP (§5.2a) adds it back on request.

### 5.2a BASS COMP (Hardy's addition)
On the original, turning RESONANCE up thins the bass: the ladder's passband gain falls roughly as 1/(1+k), and the feedback high-pass makes it worse at low cutoffs. BASS COMP puts the low end back without flattening the resonant peak.
- Let `r = k / K_MAX` (0..1). Compute a one-pole low-pass of the ladder output at `fc_bass = min(0.5·fc, BASS_CORNER_MAX)`, `BASS_CORNER_MAX = 250 Hz` (est.), and add it back: `y = ladder + BASS_COMP · r · BASS_GAIN · LP1(ladder)`.
- Tune `BASS_GAIN` so that at BASS COMP = 1 and RESONANCE = 1 the level of a 50 Hz to 100 Hz test tone through the filter (cutoff 1 kHz) is within ±2 dB of its level at RESONANCE = 0. The added part is low-passed, so the resonant peak above it is left alone.
- Applied before the VCA, at the oversampled rate, followed by the same soft limit as the output so heavy settings saturate rather than clip.
- Tests: BASS COMP = 0 is sample-identical to the stock path; at 1 the target above holds; the peak height at RESONANCE = 1 changes by less than 1.5 dB.

### 5.3 Cutoff law
Work in octaves. With knobs in 0..1:

```
base   = log2(CUT_LO) + CUTOFF·log2(CUT_HI/CUT_LO)          CUT_LO = 160 Hz, CUT_HI = 2.4 kHz (est.)
envDepth = ENV_MOD · ENV_OCT                                  ENV_OCT = 4.0 octaves (est.)
offset = −ENV_MOD · ENV_SHIFT                                 ENV_SHIFT = 1.2 octaves (est.)
acc    = ACCENT · ACC_OCT · s(t)                              ACC_OCT = 2.5 octaves (est.)
fc     = 2^(base + offset + envDepth·m(t) + acc + CUTOFF_CV/5·3 + keyTrack + sbx terms)
```
- The `offset` term models the original's Env Mod pot lowering the resting cutoff as depth goes up.
- Clamp `fc` to 20 Hz … 0.45 × the oversampled Nyquist.
- **KEY TRACK** (Hardy's addition): add `KEY_TRACK · (V_pitch − KEY_TRACK_REF)` octaves, where `V_pitch` is the **gliding** oscillator pitch (so tracking follows slides) and `KEY_TRACK_REF = 1 V` (C3, the middle of the sequencer range), so tracking pivots around the middle of a typical line rather than darkening everything. 100% = the cutoff moves one octave per octave. At 0 (default) there is no tracking, as on the original.

### 5.4 Envelopes (`Envelopes303`)
Both are triggered by the GATE rising edge. Neither has sustain.

- **MEG (filter envelope)** `m(t)` 0..1: attack is a one-pole rise with `MEG_ATTACK = 3 ms` to 1, then exponential decay. Decay time (to −40 dB) is `T = 0.2 s · 10^(DECAY)` (so 0.2 s to 2 s). **If the note is accented** (ACCENT input high at the gate's rising edge), `T = MEG_ACCENT_T = 0.2 s` regardless of DECAY (SBX can change this). The MEG ignores the gate's falling edge; it keeps decaying.
- **VEG (amplitude envelope)** `v(t)` 0..1: attack `VEG_ATTACK = 3 ms`, then decay with a fixed time `VEG_DECAY_T = 3.5 s` to −40 dB (est.). **When GATE falls**, the VCA closes with a one-pole release `VEG_RELEASE = 4 ms` (est.). So with a held gate a note fades slowly, and normally the gate length decides the note length.
- A new rising edge restarts both envelopes from their **current** value (no reset to zero, so there are no clicks).
- No retrigger happens while GATE stays high (this is how slide and tie work).

### 5.5 Accent (`AccentSweep`)
- `accentOn` = ACCENT input high (Schmitt). It is read continuously, but latched into the MEG decay choice only at the gate's rising edge.
- **Sweep**: input `a = accentOn ? m(t) : 0`. The sweep capacitor state `s` follows
  ```
  if a > s:  ds/dt = (a − s) / τc       (charging through the Resonance gang)
  else:      ds/dt = −s / τd             (discharging)
  τc = SWEEP_TC_MIN + RESONANCE² · (SWEEP_TC_MAX − SWEEP_TC_MIN)     2 ms … 80 ms (est.)
  τd = SWEEP_TD_MIN + RESONANCE · (SWEEP_TD_MAX − SWEEP_TD_MIN)      30 ms … 450 ms (est.)
  ```
  `s` enters the cutoff law as `acc` (§5.3).
- **What this must do** (targets that tests check, §9). These targets matter more than the constants; tune the constants until they hold, and log the final values.
  1. RESONANCE = 0: an accent gives a sharp cutoff peak within 6 ms of the note start, and the second of two accents 125 ms apart peaks at most 1.1 × the first (little stacking).
  2. RESONANCE = 1: the accent peak is rounded and arrives 30 ms to 90 ms after the note start, and in a run of four accents 125 ms apart each peak is higher than the last, the fourth at least 1.5 × the first.
- **VCA accent**: while `accentOn`, the VCA gain is `1 + ACCENT · VCA_ACC_GAIN`, `VCA_ACC_GAIN = 0.6` (about +4 dB at full ACCENT, est.), smoothed with a 2 ms one-pole to avoid clicks.

### 5.6 Slide (`SlideLag`)
- The target pitch is the 1V/OCT input (plus TUNING). The oscillator pitch follows it through a one-pole lag whose mode is chosen at each **pitch change**:
  - A pitch change is when the target moves by more than 0.5 mV between samples.
  - At that moment, if SLIDE is high, the lag time is `SLIDE_TAU = 35 ms` (so about 82% of the way after 60 ms, est.); otherwise the pitch jumps (the lag state is set to the target).
  - The chosen mode holds until the next pitch change, so a glide always completes even if SLIDE goes low during it.
- A GATE rising edge with SLIDE low also snaps the pitch (covers sequencers that change pitch and gate on the same sample).
- SBS sets SLIDE so that this works (§6.5). Any other sequencer works if it keeps GATE high across the two notes and SLIDE high around the pitch change.

### 5.7 Output
`out = OUT_HP( VCA( ladder(osc) ) ) · OUT_LEVEL`, `VCA = v(t) · accent gain`, `OUT_LEVEL` set so a saw at default knobs peaks near ±5 V. Final soft limit `10·tanh(x/10)`. Downsample from 4× before the output high-pass.

### 5.8 Behaviour tests (SBV)
- Pitch: 0 V gives 65.41 Hz ±1 cent; +1 V doubles; TUNING ±12 shifts an octave.
- Saw is a falling ramp; square duty within 44% to 49%; square RMS below saw RMS.
- Filter: with RESONANCE = 0, slope between 1 and 4 octaves above cutoff measures between 15 and 21 dB/oct (not 24). Peak at RESONANCE = 1 at least 15 dB above passband; impulse rings at least 200 ms but decays (no stable oscillation) at CUT OFF FREQ 0.5.
- Feedback high-pass: at RESONANCE = 1 the peak height at a 80 Hz cutoff is at least 6 dB lower than at 800 Hz.
- ENV MOD up lowers the resting cutoff (offset term).
- MEG decay 0.2 s at DECAY 0 and 2 s at DECAY 1 (±10%); accented notes always 0.2 s.
- VEG: gate held 3 s gives a slow fade; gate dropped closes within 15 ms.
- Accent targets 1 and 2 of §5.5.
- Slide: with SLIDE high, a 1 V step reaches 80% to 85% in 60 ms; with SLIDE low it jumps. No envelope retrigger while GATE stays high.
- AUDIO IN replaces the oscillator.
- KEY TRACK at 100%: cutoff moves 1 octave per 1 V of pitch (±3%), pivoting at 1 V; at 0 it does not move. During a slide the cutoff glides with the pitch.
- BASS COMP tests as in §5.2a.

---

## 6. SBS, Silverbox Sequencer (24 HP)

### 6.1 Memory model (`seq/Pattern.hpp`)
```
struct PitchSlot { uint8 note;   // 0..12, 0 = C, 12 = upper C
                   bool up, down, accent, slide; };
enum TimeSlot : uint8 { NOTE, TIE, REST };
struct Pattern  { PitchSlot pitch[16]; TimeSlot time[16];
                  uint8 length;   // 1..16, default 16
                  bool triplet; };
struct TrackSlot { uint8 pattern; int8 transpose; };   // pattern 0..63, transpose −12..+12
struct Track    { TrackSlot slot[64]; uint8 length; }; // length 0..64
Memory: Pattern pattern[64]; Track track[7];
```
- Pattern index 0..63 is shown as **group-number-section**: `I-1A` … `IV-8B` (group = i/16, number = (i%16)/2 + 1, section = A for even, B for odd).
- Default (new module): every pattern is 16 × (C, no flags) with time all NOTE except pattern `I-1A`, which holds a short demo line (choose one that shows slide and accent). Tracks empty.
- The whole memory is saved in the patch (`dataToJson`/`dataFromJson`) as compact strings, one per pattern. Also an "Export memory…" / "Import memory…" JSON file option in the context menu (I/O happens in the UI thread, never in `process()`).

### 6.2 The merge (`seq/Merge.hpp`), the heart of the 303 behaviour
A pure function `merge(const Pattern&) → PlayStep[16]` used by both the engine and the display.
```
struct PlayStep { TimeSlot kind; int pitchSlot;  // −1 for REST, or the slot index used
                  float volts; bool accent, slide; };
```
- Walk `i = 0 .. length−1`, with a pitch pointer `pp = 0`.
  - **NOTE**: take `pitch[pp]`, then `pp = (pp + 1) % 16`. `volts = (note + 12·(up − down)) / 12`. accent, slide from that slot.
  - **TIE**: continue the previous step's note: same volts, same pitchSlot, accent and slide copied from it. A TIE at step 0 (or after a REST) behaves as a REST (log in DECISIONS.md; check against the original if a source says otherwise).
  - **REST**: silence.
- The pitch pointer resets to 0 at the start of every pattern loop. So changing a REST to a NOTE shifts every later pitch, accent and slide one step along, exactly as on the 303. **This must never be "fixed".**

### 6.3 Clock (`seq/Clock.hpp`)
- **Internal**: TEMPO knob 40 to 300 BPM, default 120, generating 24 ppqn internally.
- **CLOCK IN** patched: internal clock is off. Context menu "Clock input": `24 PPQN` (default, like the original's sync), `1 per step`, `4 PPQN` (1 per 16th; same as 1 per step but named for clarity). Step length is measured from the last two input pulses for gate timing.
- **Step length**: a normal step is a 16th note = **6 pulses** at 24 ppqn. In **triplet** mode a step is a 16th-note triplet = **4 pulses**, so 6 steps fill a beat and 12 steps fill half a bar. In `1 per step` clock mode triplet has no effect on timing (say so in the manual).
- RUN button toggles running; **RUN IN** trigger toggles it too (context menu option: "Run input is a gate"). **RESET IN** trigger: next step is step 1 of the current pattern (and slot 1 of the track in track play). Stopping sets GATE low immediately.

### 6.4 Gate, accent and slide timing (`seq/SeqEngine.hpp`)
Let `g` = GATE LENGTH knob (10% to 95% of a step, default **50%**, with a detent there; original value est.).
- **NOTE** step without slide: GATE goes high at step start (after the pitch is set on the same sample) and low at `g` of the step.
- **Slide**: if the step's note has slide and the next step is a NOTE or TIE, GATE stays high through the step boundary (no new rising edge on the next step). If the next step is a REST, GATE is held to the end of the slid step, then goes low.
- **TIE**: GATE stays high from the tied-from note through the TIE step and goes low at `g` of the last TIE step (or holds on if that note slides into a following NOTE).
- **ACCENT out**: high from the start of an accented note to the end of its gate (including ties).
- **SLIDE out**: high from `g` of the slid step to `g` of the next step. This makes SBV (§5.6) glide on that boundary and on no other.
- **1V/OCT out**: set at step start to `volts + transpose` and held through ties and rests (the original's CV out holds the last note).
- **CLOCK out**: 1 ms trigger at each step start. **END out**: 1 ms trigger at the start of step 1 of each pattern loop.
- At the end of a pattern (after step `length`), go to the queued pattern if one is queued (§6.6).

### 6.5 Controls

| Type | Name | Range / default | Notes |
|---|---|---|---|
| K | MODE | 5-position: PITCH WRITE, TIME WRITE, PATTERN PLAY, TRACK WRITE, TRACK PLAY. Default PATTERN PLAY | Printed position labels around the knob |
| K | PATTERN | 1 to 64 (shown `I-1A`…), default 1 | Snap knob |
| K | TRACK | 1 to 7, default 1 | Snap knob |
| K | TEMPO | 40 to 300 BPM, default 120 | Ignored when CLOCK IN is patched |
| K | GATE LENGTH | 10% to 95%, default 50% | Detent at 50% |
| K | LENGTH | 1 to 16, default 16 | Length of the current pattern; writes to memory |
| K | MUTATE | 0 to 1, default 0 | §6.7 |
| S | TRIPLET | OFF / ON | Per pattern; writes to memory |
| Buttons | Keyboard: C, C#, D, D#, E, F, F#, G, G#, A, A#, B, C (upper) | | Piano layout, 13 keys, labels are the note names |
| Buttons | DOWN, UP, ACCENT, SLIDE | | Toggle on the current pitch slot; LED shows state |
| Buttons | NOTE, TIE, REST | | Time write |
| Buttons | BACK, NEXT | | Move the edit cursor |
| Buttons | RUN | | LED lit while running |
| Buttons | CLEAR | hold 1 s | Clears the current pattern (or the track from the cursor in TRACK WRITE) |
| Buttons | SCRAMBLE | hold 1 s | §6.7 |
| Buttons | SHIFT PITCH, SHIFT TIME | | §6.7 |
| Buttons | UNDO | | §6.7 |
| I | CLOCK, RESET, RUN | | §6.3 |
| I | TRANSPOSE | 1V/Oct, quantised to semitones | Added to the output |
| I | PATTERN CV | 0 to 10 V adds 0 to 63 patterns to the PATTERN knob (wraps) | Takes effect at pattern end |
| I | MUTATE CV | ±5 V adds ±1 to MUTATE | |
| O | 1V/OCT, GATE, ACCENT, SLIDE | | §6.4 |
| O | CLOCK, END | | Triggers |
| Display | | | §6.8 |

### 6.6 Modes
- **PITCH WRITE**: the cursor is a pitch slot (1..16). A **key** sets that slot's note and previews it (if stopped, SBS outputs the pitch and a 150 ms gate so the patched voice sounds). **DOWN/UP/ACCENT/SLIDE** toggle that slot's flags (UP and DOWN are exclusive). **NEXT/BACK** move the cursor (wrap at 16). The sequencer keeps playing if running, so edits are heard live.
- **TIME WRITE**: the cursor is a time step (1..length). **NOTE/TIE/REST** write the step and advance. NEXT/BACK move without writing.
- **PATTERN PLAY**: plays the selected pattern. Turning PATTERN (or PATTERN CV) **queues** a change that happens at the end of the current loop; the display shows the queued pattern. A **key** sets live transpose (C = 0 … upper C = +12; with DOWN held, −12 … 0), applied from the next step.
- **TRACK WRITE**: the cursor is a track slot. Turning PATTERN sets that slot's pattern; a key sets its transpose (as above). **NEXT** stores and advances, extending the track length if at the end. **CLEAR** (hold) cuts the track at the cursor.
- **TRACK PLAY**: plays the selected track's slots in order, each pattern once with its transpose, then loops. Live transpose from the keys and TRANSPOSE IN adds on top.
- Switching mode never stops playback.

### 6.7 Making material (the "generate first, save it, perform later" tools)
- **SCRAMBLE** (hold 1 s; LED blinks while held): randomises the current pattern, as a 303's memory did when the batteries were pulled. Each pitch slot: note uniform 0..12, UP 15%, DOWN 15%, ACCENT 25%, SLIDE 20%. Each time step: NOTE 70%, TIE 10%, REST 20%. Length and triplet unchanged. Use Rack's random generator; context menu "Scramble seed: random / fixed" not needed, keep it simple.
- **SHIFT PITCH / SHIFT TIME**: rotate the pitch list (or the time list) of the current pattern by one slot forward. Rotating one list against the other is the standard 303 trick for new variations.
- **MUTATE** (knob plus CV): at each pattern end, make `round(MUTATE · 4)` random changes to the current pattern in memory. Each change picks one at random: a pitch slot's note to a random note; flip a slot's ACCENT; flip a slot's SLIDE; flip UP/DOWN; cycle one time step NOTE→REST→TIE→NOTE. At 0 nothing changes. Mutations are written to memory, so a line you like is kept.
- **UNDO**: 32-level undo for every memory change from buttons, scramble, shift, mutate and direct edit (a mutation pass counts as one level). Kept in RAM only, not saved in the patch.
- **Context menu**: Copy pattern, Paste pattern, **Copy to next empty pattern** (empty = still the default C pattern), Clear all memory (with confirmation via Rack's menu submenu, no modal dialog).

### 6.8 Display (`widgets/SeqDisplay.hpp`)
An LCD-style panel area about 90 × 34 mm, drawn with NanoVG in the UI thread from a snapshot of the engine state (copied through an atomic double buffer; never read engine memory directly).
- Top line: mode, pattern name (`II-5B`), queued pattern if any, track and slot in track modes, tempo or `EXT`, `TRIP` when triplet.
- **Row PITCH**: 16 cells, each showing note name, `▲`/`▼` for up/down, `A` and `S` for accent and slide (the arrows and letters are the only glyphs, and the row is labelled in words on the panel: `PITCH`).
- **Row TIME**: 16 cells, `●` NOTE, `—` TIE, blank REST, cells past LENGTH greyed. Labelled `TIME`.
- **Row RESULT**: the `merge()` output, each cell showing the note actually played with its accent/slide, and a thin link line from each NOTE cell up to the pitch slot it used, so the shifting is visible. Labelled `RESULT`.
- The playing step and the edit cursor are highlighted in different colours.
- **Direct edit**: clicking a RESULT cell opens a Rack context menu with: note (C … upper C), octave (DOWN / middle / UP), ACCENT, SLIDE, and time (NOTE / TIE / REST). Note, octave, accent and slide edit the **pitch slot that cell uses**; time edits the time step (which may shift later notes; the menu says so in a one-line hint). Clicking a PITCH or TIME cell moves the edit cursor there.

### 6.9 Behaviour tests (SBS)
- `merge()` on hand-written cases, including: the shift when a REST becomes a NOTE; pitch pointer wrap past 16 notes; LENGTH shorter than 16; TIE at step 0; slide into TIE, into REST, into NOTE.
- Engine at 120 BPM internal: step 125 ms; triplet step 83.3 ms; GATE high for `g` of a plain step; held through a slide; one rising edge for a note plus two ties; SLIDE out windows exactly as §6.4; ACCENT high only on accented notes.
- External 24 ppqn and 1-per-step clocks give the same step boundaries as the internal clock at the same tempo.
- Queued pattern switches only at loop end. TRACK PLAY steps through slots with transposes.
- SCRAMBLE distribution within ±5% of the stated probabilities over 10,000 runs. MUTATE 0 never changes memory. UNDO restores memory bit-exactly after any sequence of 32 edits.
- Save/load round trip of the full memory is bit-exact.

---

## 7. SBX, Silverbox Extended (8 HP, expander)

Sits **directly to the right of SBV**. SBV reads it through `rightExpander` each sample (use Rack's expander message double-buffer). A light on SBX shows `LINKED` when attached. **At default knob positions SBX changes nothing**: each default equals the stock behaviour. Every knob has a CV input below it (±5 V adds ± the full range).

| Type | Name | Range / default (stock) | What it does in SBV |
|---|---|---|---|
| K + I | VEG DECAY | 16 ms to 6 s, default 3.5 s (stock) | Replaces `VEG_DECAY_T` |
| K + I | ACCENT DECAY | 30 ms to 3 s, default 0.2 s | Replaces `MEG_ACCENT_T` |
| S | DECAY RANGE | STOCK / WIDE | WIDE makes DECAY run 30 ms to 3 s |
| K + I | SLIDE TIME | 2 ms to 500 ms, default 60 ms | Sets `SLIDE_TAU` so the glide is 82% done at this time |
| K + I | SOFT ATTACK | 0.3 ms to 30 ms, default 3 ms | VEG attack on **non-accented** notes |
| K + I | SWEEP SPEED | ×0.25 to ×4, default ×1 | Multiplies τc and τd of the accent sweep |
| K + I | FILTER FM | 0 to 1, default 0 | Adds the VCA output (audio rate, at 4×) to cutoff, up to ±2 octaves per volt-normalised unit |
| K + I | OVERDRIVE | 0 to +24 dB, default 0 | Gain into the ladder (`DRIVE_IN`) |
| S | MUFFLER | OFF / SOFT / HARD | Output soft clipper before OUT_HP: SOFT `tanh(1.5x)/1.5`-style, HARD a diode-pair clip at about ±0.6 of full level (est.) |
| L | LINKED | | |

Tests: SBX at defaults is sample-identical to SBV without SBX (same seed, same input) apart from floating-point noise below −120 dB. Each control changes what its row says.

---

## 8. Leaning on existing modules

- **Distortion and delay are not rebuilt.** The classic acid chain is voice → overdrive → delay. The manual's patches use VCV's free Fundamental **Delay** and one free, currently maintained distortion or saturation module from the library: check the library at build time, pick one, and name it in the manual and in DECISIONS.md with the reason.
- **Clocks**: SBS has its own, but at least one manual patch runs it from VCV's free **Clocked**-style clock or any 1-per-step clock, to show external sync.

---

## 9. General tests

For each module: the behaviour tests in its section, plus:
- **Stability**: 10 s of white noise at ±10 V into every input with random knob settings at 44.1, 48, 96 and 192 kHz. All outputs finite and within ±10 V; gate outputs exactly 0 or 10.
- **Silence**: SBV with no gate outputs 0 V (no DC, no denormals).
- **Sample-rate independence**: pitch, cutoff, envelope times, slide time and step timing within 0.5% across the four rates.
- **Aliasing**: a 2 kHz saw and square at 48 kHz have aliased components below −60 dB relative to the fundamental within 0 to 10 kHz (after 4× oversampling).
- **End to end**: SBS (demo pattern I-1A) driving SBV for 10 s renders to a WAV in `tests/out/` (not committed) so the build can be listened to.

In Rack itself before hand-over: load every module, patch every manual recipe, change sample rate while running, save and reload, check CPU meters, check SBX link and unlink.

---

## 10. Manual (`docs/MANUAL.md` and `docs/Silverbox-Manual.pdf`)

Same structure as the Serge manual: what each module is, a control table, then patch recipes as cable lists. Build the PDF from the Markdown in CI or with a script (pandoc + a LaTeX or HTML-to-PDF engine), with panel images.

Must include:
1. **First acid line**: SBS 1V/OCT, GATE, ACCENT, SLIDE → SBV; SBV OUT → distortion → Delay → Audio. Knob positions for a classic squelch.
2. **Programming the 303 way**: a worked example entering a pitch list and a time list, then changing one REST to a NOTE and watching the RESULT row shift.
3. **Generate, save, perform**: SCRAMBLE, audition, MUTATE at low value, Copy to next empty pattern, build a track from the kept patterns, perform with transpose and pattern CV.
4. **The accent stack**: RESONANCE low vs high with four accents in a row, what to listen for.
5. **SBV from another sequencer**: how to drive SLIDE and ACCENT from any gate sequencer, with the timing rule of §5.6.
6. **Drums through the 303 filter**: a drum loop into AUDIO IN with accents from a trigger sequencer.
7. **SBX squelch**: FILTER FM and OVERDRIVE with MUFFLER.
7a. **Hardy's two knobs**: BASS COMP to keep the weight under a screaming resonance, and KEY TRACK for melodic lines that should stay equally bright across octaves.
8. **Unstable time**: SBS clocked by a jittery or random clock, a nod to the Autechre-style set in this project.

Also a short "What is modelled" page summarising §5, and the trademark note (this is an independent model; names belong to their owners).

---

## 11. Order of work

1. Repo skeleton, Makefile, CI, `plugin.json`, shared components, panel script, three empty modules that load.
2. `Constants303`, oversampling, BLEP, oscillator with tests.
3. Diode ladder with tests (slope, peak, ringing, feedback high-pass).
4. Envelopes, accent sweep, slide lag, the full voice; tune until the §5.5 targets pass.
5. `Pattern`, `merge()`, clock, engine with tests.
6. SBS module: controls, modes, display, direct edit, making-material tools, save/load.
7. SBX and the expander link.
8. Panels drawn, rendered and inspected; label check passes.
9. Manual and PDF; end-to-end WAV; full test pass; CI green on all platforms.
10. Hand-over note: what was built, the contents of DECISIONS.md, the final values of every estimated constant, what in §12 could or could not be verified, install steps.

---

## 12. Accuracy ledger

**From sources (service notes, Devil Fish documentation, Open303, circuit analyses):**
- One VCO, saw or square, square derived from the saw by a transistor shaper; falling-ramp saw with a slightly curved ramp.
- 4-pole diode ladder with an unequal bottom capacitor, slope less steep than 24 dB/oct; resonance thins at low cutoff; no keyboard tracking.
- MEG decay range about 200 ms to 2 s; MEG decay forced to minimum on accented notes; VEG decay fixed and long; fast VCA close on gate off.
- Accent sweep circuit fed by the MEG, scaled by ACCENT; resonance pot dual-ganged into the sweep; stacking accents; accent raises VCA level.
- Slide: gate held, no retrigger, fixed-time RC glide.
- Sequencer: separate pitch and time entry, notes consumed only by NOTE steps; ties; rests; up/down/accent/slide stored with pitches; 16 steps maximum, variable length; triplet mode; 24 ppqn sync; 4 groups × 8 patterns × A/B; 7 tracks.
- Devil Fish additions (control list in §7, ranges approximate).

**Estimates for this spec (verify where possible; all live in `Constants303.hpp`):**
- `K_SAW`, `SQ_GAIN`, `SQ_OFFSET`, `SQ_LP`, `SQ_HP`, `SQ_LEVEL`.
- `STAGE1_RATIO` (1.8), `K_MAX`, `FB_HP` (150 Hz), `IN_HP`, `OUT_HP`, `DRIVE_IN`.
- `CUT_LO`/`CUT_HI` (160 Hz / 2.4 kHz), `ENV_OCT` (4), `ENV_SHIFT` (1.2), `ACC_OCT` (2.5).
- `MEG_ATTACK`, `VEG_ATTACK` (3 ms), `VEG_DECAY_T` (3.5 s), `VEG_RELEASE` (4 ms), `MEG_ACCENT_T` (0.2 s).
- Sweep `SWEEP_TC_*`, `SWEEP_TD_*`, `VCA_ACC_GAIN`. The behaviour targets in §5.5 take precedence.
- `SLIDE_TAU` (35 ms, i.e. ~60 ms to mostly arrive).
- Gate length 50% of a step; TIE at step 0 treated as REST; 0 V = C2.
- SBX ranges and the MUFFLER curves.
- BASS COMP `BASS_CORNER_MAX` and `BASS_GAIN` (target in §5.2a takes precedence); KEY TRACK pivot `KEY_TRACK_REF` = 1 V.

**Sources**
- Open303 (Robin Schmidt), open-source 303 emulation, MIT licence: https://github.com/RobinSchmidt/Open303 and the KVR thread https://www.kvraudio.com/forum/viewtopic.php?p=3761614
- Robin Whittle, Devil Fish documentation and 303 circuit notes: https://www.firstpr.com.au/rwi/dfish/
- Tim Stinchcombe, analysis of the TB-303 diode ladder filter: https://www.timstinchcombe.co.uk/ (synth / TB-303 pages)
- Zoe Blade, TB-303 notebook: https://notebook.zoeblade.com/TB-303.html
- Will Pirkle, "Virtual Analog Diode Ladder Filter" app note; Vadim Zavalishin, *The Art of VA Filter Design*.
- Roland TB-303 owner's manual and service notes (1981–82) for the sequencer's programming and timing.
