# A TB-303 for VCV Rack: research and proposed modules

Draft 1, 2026-11-15. Nothing here is fixed until Hardy picks. See "Choices for Hardy" at the end.

Working plugin name: **Silverbox** (the 303's nickname). "Roland", "TB-303" and "Bass Line" are live trademarks, so a public release must not use them in plugin, module or panel names. For private use the manual can say "TB-303" when describing what is being modelled. "Devil Fish" is Robin Whittle's name for his mod, so the same applies to it.

---

## Part 1. How the original works

The TB-303 Bass Line (1981 to 1984) is one monophonic voice and one step sequencer in a box, with no CV inputs except a sync/clock socket and a CV/gate out. Almost everything that makes acid sound like acid comes from four places: the filter, the way accent drives the filter, slide, and the sequencer's odd programming. Values below come from the service notes, the Open303 and Devil Fish write-ups and published circuit analyses. Values marked (est.) are best estimates that the build must expose as constants so they can be tuned by ear.

### 1.1 Oscillator

- One VCO, **sawtooth or square**, chosen by a switch. No pulse width control, no sub, no noise.
- The **saw is not a perfect ramp**. It is an exponential capacitor charge, so the ramp bends slightly, and the reset is fast but not instant.
- The **square is made from the saw** by a one-transistor waveshaper. It is not a clean 50% square: the duty is a little off centre, the edges are slanted and the top droops. It is quieter and darker than a textbook square, and it is the reason 303 square lines sound "woody".
- Pitch: 1 V/oct internally. The sequencer covers **one octave of keys (C to C) plus octave Up and Down**, so three octaves in all. A **Tuning** knob sets the base pitch (about ±1 octave on the original).

### 1.2 Filter: a diode ladder that is not quite 24 dB

- A **4-pole diode ladder**, but the capacitors are not equal: the bottom stage uses a different value from the other three. The result behaves more like **18 dB/oct with a rounder resonant peak** than a Moog-style 24 dB ladder. This is a big part of the sound.
- **Resonance** never gives a clean, loud self-oscillation. At full resonance the peak screams but the filter only just rings.
- There are **coupling capacitors** around the ladder and a **high-pass in the feedback path**, so resonance thins out and fades at very low cutoffs, and the bass loses weight as resonance goes up. Faithful models keep this; it stops a 303 sounding like a Minimoog.
- **Cutoff** knob plus **Env Mod** (envelope depth). The filter does **not track the keyboard** on the original.
- The diodes saturate softly, so driving the input harder (or turning resonance up) adds grit rather than hard clipping.

### 1.3 Envelopes

There are two, both triggered by the gate, neither with sustain.

- **Filter envelope ("MEG")**: near-instant attack (~3 ms), exponential decay set by the **Decay** knob, about **200 ms to 2 s**. Its depth into the filter is **Env Mod**.
- **Amplitude envelope ("VEG")**: same fast attack, a **fixed, long decay** (~3 to 4 s est.) that the user cannot change. When the gate goes low the VCA closes quickly (a few ms), so in practice note length is set by the sequencer's gate, not by the envelope. This is why 303 notes feel "full" until they stop.

### 1.4 Accent: the part everyone gets wrong

An accented step does three things at once.

1. **The filter envelope decay is forced to its minimum** (~200 ms), whatever the Decay knob says.
2. **The accent sweep circuit**: the filter envelope is also sent through a resistor and a capacitor (the "accent sweep" cap) and added to the cutoff, scaled by the **Accent** knob. The capacitor charges on each accented note and discharges slowly, so **several accents in a row stack up** and the filter climbs higher each time ("wow" accents).
3. **The VCA gets louder**, by the same Accent amount.

The **Resonance pot is dual-ganged**: its second half changes how fast the accent sweep cap charges. At low resonance accents are sharp peaks, at high resonance they become rounded, rising sweeps that stack. This interaction between Resonance and Accent is essential and almost all clones skip it.

### 1.5 Slide

- A step marked **Slide** holds its gate into the next step. The envelopes **do not retrigger**, and the pitch **glides** into the next note.
- The glide is an RC lag with a **fixed time** (~60 ms est.), the same for any interval. It is not a "rate" portamento.
- Slide and accent together give the classic yelp: no retrigger, but the stacked accent sweep keeps rising.

### 1.6 The sequencer and why its programming matters

- **Patterns**: up to 16 steps. 4 groups (I to IV) × 8 patterns × sections A and B, so 64 patterns. **Tracks** (7 on the original) chain patterns into a song.
- **Pitch and Time are entered separately.**
  - In **Pitch mode** you enter a list of notes. Each note carries its own **Up / Down** (octave), **Accent** and **Slide**.
  - In **Time mode** you enter, for each step, **Note**, **Tie** or **Rest**.
  - At playback each **Note** step takes the **next pitch from the list**. Tie extends the previous note (no new gate), Rest is silence.
  - So the two lists are **decoupled**. Changing one time step from Rest to Note shifts every later pitch, accent and slide one step along. This is how people stumbled into lines they could not have written on purpose, and it is the most "303" thing about the sequencer.
- **Timing**: internal clock at 24 ppqn; a step is a 16th note (6 pulses). The gate is high for roughly the **first half** of the step (est.); a slide or tie holds it through.
- **Triplet mode** changes the step length so 12 steps fill a bar, and the pattern length can be anything from 1 to 16.
- **Memory scramble**: removing the batteries corrupted the patterns into random data, which some early acid producers used. A "scramble" action is a fair, fun way to honour this.
- **Live controls**: Transpose (by key, in Track Play), pattern select while running (switches at the end of the pattern), and Run/Stop.

### 1.7 After the original

- **Devil Fish** (Robin Whittle, 1993 on): adds controls for VEG decay, accent decay, normal decay range, slide time, filter tracking, **filter FM from the VCA output**, soft attack, sweep speed, overdrive into the filter, and a "Muffler" soft clipper on the output, plus CV/gate/filter inputs. It widens the 303 a lot without losing it.
- **Common external chain**: overdrive or distortion (the Rat and the Boss DS-1 are the classic pedals), then delay. The Rack library already has good distortion and delay, so this set does not rebuild them (see Part 3).

---

## Part 2. What already exists in the VCV library

Checked on library.vcvrack.com on 2026-11-15 (searches for "303", "acid" and "diode").

| Plugin | Module | What it is |
|---|---|---|
| Voxglitch | Centralics ($20) | 303-style voice with a pattern generator and synced delay |
| TriggerFish | 303 Voice Core, 303 Oscillator | Circuit-modelled ladder, accent path and VCA; separate oscillator with slide |
| Animatek | ATEK303, ATEK303 SEQ | Voice with accent and slide; 64-step acid pattern generator |
| Bidoo | aCiDe | Mono acid voice: saw/square, diode ladder, accent, slide |
| Twisted Cable | MonoSlide, TwinSlide | Mono and dual acid bass synths |
| Autinn | Bass | Bass and acid voice |
| NYSTHI | Dica33 | "Distorted acid filter, 303 wannabe" |
| Vult | Nitrous, Lateralus | Acid filter; diode ladder filter |
| LindenbergResearch, kocmoc | Laika, DIOD, uDIOD | Diode ladder filters, not 303-specific |

**What none of them do, as far as the listings show:**

- The **separate Pitch and Time programming**, with its shifting behaviour, on a panel you can actually see. Existing acid sequencers are step grids or random pattern generators.
- The **Resonance/Accent gang** and a stacking accent sweep with a documented model.
- Devil-Fish-style extensions as their own optional expander, with CV on every one.
- Text-labelled panels, a PDF manual with patch recipes, and a shared house style with your Serge and Tudor sets.

So this set adds a faithful sequencer and a faithful accent path. It does not try to beat the existing voices on filter modelling alone; the filter will be a careful model, but the reason to build this is the whole instrument.

---

## Part 3. Proposed set (recommended: three modules)

Every control and jack gets an accurate text label. Every module name includes its short name so Rack's browser finds it.

### 3.1 SBV, Silverbox Voice (14 HP)

The 303 voice and nothing else, playable from any sequencer.

- **Knobs (as on the original)**: Tuning, Cutoff, Resonance, Env Mod, Decay, Accent. **Switch**: Saw / Square.
- **Added at Hardy's request (2026-11-15)**: **Bass Comp** (boosts the bass that thins out as resonance goes up) and **Key Track** (cutoff follows pitch). Both default to 0, which is the stock 303.
- **Inputs**: 1V/Oct, Gate, Accent (gate, >1 V = accented), Slide (gate, high = glide into the next note and do not retrigger), Cutoff CV, Audio In (replaces the oscillator, so you can put anything through the 303 filter and accent path).
- **Outputs**: Audio, Filter Env (the MEG, for patching elsewhere), Accent Sweep (the sweep cap voltage).
- **Inside**: band-limited saw and saw-derived square, unequal-capacitor diode ladder with feedback high-pass, MEG with accent-forced decay, accent sweep with the resonance gang, VCA with accent boost, fixed-time slide lag.
- Polyphony: mono by design; a polyphonic cable plays its first channel only (keeps CPU low and the behaviour faithful).

### 3.2 SBS, Silverbox Sequencer (20 HP)

The 303 sequencer with its Pitch/Time programming, plus what a Rack user needs.

- **Patterns**: 4 groups × 8 patterns × A/B = 64, each 1 to 16 steps, saved in the patch. **Tracks**: 7 chains of up to 64 pattern slots.
- **Editing, faithful mode**: Pitch mode and Time mode as on the original, with the 13 note keys, Up, Down, Accent, Slide, Note/Tie/Rest, and Back/Forward. A **small display** shows both lists and the merged result, so the shifting is visible rather than mysterious.
- **Editing, direct mode**: click a step in the merged view and set its note, octave, accent, slide and time directly. Faithful mode stays the default.
- **Clock**: internal tempo, or Clock In (24 ppqn, as the original's sync, or 1 pulse per step, switchable). Reset In, Run In, Run/Stop button.
- **Timing**: Triplet switch, pattern length, and a **Gate Length** knob with a detent at the original value.
- **Live**: Transpose In (1V/Oct, quantised to semitones), Pattern Select CV, pattern switches at pattern end.
- **Outputs**: 1V/Oct, Gate, Accent, Slide (so it drives SBV directly with four cables, or anything else), plus End-of-Pattern trigger.
- **Mutation tools** (these are the "generate material, save it" side): **Scramble** (randomise the current pattern's memory, as a battery pull), **Shift Pitch** and **Shift Time** (rotate one list against the other), **Mutate** knob (each pattern loop, change a few attributes at random, with an Undo).

### 3.3 SBX, Silverbox Extended (6 HP expander, sits to the right of SBV)

Devil-Fish-style extras, each with a knob and a CV input. With SBX absent, SBV is a stock 303.

- VEG Decay, Accent Decay, Normal Decay range, Slide Time, Filter FM (VCA output into cutoff), Soft Attack, Sweep Speed, Overdrive (into the filter), Muffler (output soft clip).

### 3.4 Leaning on existing modules

- **Distortion and delay**: not rebuilt. The manual's patches use library modules that are free and well known (named in the build spec once checked), because the classic acid chain is voice → distortion → delay and those already exist in good form.
- **Clock**: SBS has its own clock, but patches will also show it running from any Rack clock at 1 pulse per step.

---

## Choices for Hardy

1. **Shape of the set.** Recommended: three modules (Voice, Sequencer, Extended expander), so the voice works with other sequencers and the sequencer drives other voices. Alternative: one all-in-one 303 module, closer to the box but less useful in Rack.
2. **Sequencer programming.** Recommended: faithful Pitch/Time mode as the default, with a display of the merged result and a direct-edit mode for quick fixes. Alternatives: faithful mode only, or a modern step grid only.
3. **Devil Fish extras.** Recommended: as the optional SBX expander. Alternatives: built into the voice, or left out.

Also needed before the build: an **empty GitHub repository**. Suggested name: **hardyfinn/Silverbox**.

Defaults carried over from the Serge set: text labels on every control and jack, short names in module names, a PDF manual with patch recipes, CI builds for Linux, Mac and Windows, and work on main.
