// SBS engine (spec §6.3 to §6.7): clock, step timing, gate/accent/slide outputs,
// pattern and track play, the write modes, and the material tools. No Rack includes.
// Everything here runs on the audio thread; the module turns button presses into calls.
#pragma once
#include "Pattern.hpp"
#include <cmath>

namespace sb {

enum Mode { PITCH_WRITE = 0, TIME_WRITE, PATTERN_PLAY, TRACK_WRITE, TRACK_PLAY, NUM_MODES };
enum ClockMode { CLOCK_24PPQN = 0, CLOCK_PER_STEP = 1, CLOCK_4PPQN = 2 };

struct Rng {
	uint32_t s = 0x9e3779b9u;
	void seed(uint32_t v) { s = v ? v : 0x9e3779b9u; }
	uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	float uniform() { return (next() >> 8) * (1.f / 16777216.f); }
	int below(int n) { return (int)(uniform() * n) % n; }
};

// What the module samples from knobs and inputs each sample.
struct SeqControls {
	Mode mode = PATTERN_PLAY;
	int pattern = 0;          // selected pattern 0..63 (knob + CV)
	int track = 0;            // 0..6
	float bpm = 120.f;
	float gateLen = 0.5f;     // 0.10 .. 0.95 of a step
	float mutate = 0.f;       // 0..1
	int transposeIn = 0;      // semitones, from TRANSPOSE IN
	bool clockPatched = false;
	ClockMode clockMode = CLOCK_24PPQN;
	bool downHeld = false;    // DOWN button held (modifier for transpose keys)
};

struct SeqOutputs {
	float voct = 0.f;
	bool gate = false, accent = false, slide = false;
	bool clock = false, end = false;   // 1 ms triggers
};

struct UndoEntry {
	uint8_t kind = 0;     // 0 pattern, 1 track
	uint8_t index = 0;
	Pattern p;
	Track t;
};

struct SeqEngine {
	// ---- memory ----
	Pattern pattern[PATTERNS];
	Track track[TRACKS];

	// ---- play state ----
	bool running = false;
	int playing = 0;          // pattern being played
	int queued = -1;          // pattern to switch to at loop end, -1 none
	int stepIdx = 0;
	bool needStart = true;    // the next clock pulse starts a step without advancing
	int pulseCount = 0;       // pulses since the current step started
	float pulseAcc = 0.f;     // internal clock: fraction of a pulse
	float sincePulse = 0.f, pulsePeriod = 0.02f;   // external clock timing
	float phase = 0.f;        // 0..1 within the current step
	bool gEvaluated = true;
	PlayStep cur;
	PlayStep merged[STEPS];
	int mergedFor = -1;
	bool mergedDirty = true;
	int liveTranspose = 0, trackTranspose = 0;
	int trackPos = 0, trackPlaying = 0;

	// ---- outputs ----
	float voct = 0.f;
	bool gate = false, accentOut = false, slideOut = false;
	bool gateHeld = false;    // gate continues into the next step
	bool pendingRise = false;
	float clockTrig = 0.f, endTrig = 0.f;
	float previewT = 0.f;     // preview gate time left (s)

	// ---- editing ----
	int cursorPitch = 0, cursorTime = 0, cursorTrack = 0;
	int editTranspose = 0;    // transpose for the TRACK WRITE slot being entered
	SeqControls ctl;

	// ---- undo ----
	static constexpr int UNDO_N = 32;
	UndoEntry undo[UNDO_N];
	int undoTop = 0, undoCount = 0;
	Rng rng;

	// clipboard
	Pattern clip;
	bool hasClip = false;

	SeqEngine() { pattern[0] = demoPattern(); }

	// ------------------------------------------------------------ helpers --
	int selected() const { return ctl.pattern < 0 ? 0 : (ctl.pattern > 63 ? 63 : ctl.pattern); }
	int pulsesPerStep() const {
		if (ctl.clockPatched && ctl.clockMode != CLOCK_24PPQN) return 1;
		return pattern[playing].triplet ? 4 : 6;
	}
	const PlayStep* mergedPlaying() {
		if (mergedDirty || mergedFor != playing) {
			merge(pattern[playing], merged);
			mergedFor = playing;
			mergedDirty = false;
		}
		return merged;
	}
	void touched(int pat) { if (pat == playing) mergedDirty = true; }
	bool trackMode() const { return ctl.mode == TRACK_PLAY && track[ctl.track].length > 0; }

	void pushUndoPattern(int i) {
		UndoEntry& e = undo[undoTop];
		e.kind = 0; e.index = (uint8_t)i; e.p = pattern[i];
		undoTop = (undoTop + 1) % UNDO_N;
		if (undoCount < UNDO_N) undoCount++;
	}
	void pushUndoTrack(int i) {
		UndoEntry& e = undo[undoTop];
		e.kind = 1; e.index = (uint8_t)i; e.t = track[i];
		undoTop = (undoTop + 1) % UNDO_N;
		if (undoCount < UNDO_N) undoCount++;
	}
	void doUndo() {
		if (undoCount == 0) return;
		undoTop = (undoTop + UNDO_N - 1) % UNDO_N;
		undoCount--;
		UndoEntry& e = undo[undoTop];
		if (e.kind == 0) { pattern[e.index] = e.p; touched(e.index); }
		else track[e.index] = e.t;
	}

	// ------------------------------------------------------------ transport --
	void setRunning(bool r) {
		if (r == running) return;
		running = r;
		if (!running) {
			gate = accentOut = slideOut = false;
			gateHeld = pendingRise = false;
		}
		needStart = true;
		stepIdx = 0;
		pulseCount = 0;
		pulseAcc = 0.f;
		trackPos = 0;
		if (running && !ctl.clockPatched) onPulse();   // internal clock starts at once
	}
	void toggleRun() { setRunning(!running); }
	void reset() {
		needStart = true;
		stepIdx = 0;
		pulseCount = 0;
		trackPos = 0;
		if (running && !ctl.clockPatched) { pulseAcc = 0.f; onPulse(); }
	}
	// external clock rising edge
	void clockEdge() {
		if (!ctl.clockPatched) return;
		if (sincePulse > 1e-4f) pulsePeriod = sincePulse;
		sincePulse = 0.f;
		if (running) onPulse();
	}

	// first pattern of a track loop / the pattern after this loop
	int nextPatternAfterLoop(int* trackPosOut, int* transOut) const {
		if (trackMode()) {
			const Track& t = track[ctl.track];
			int tp = (trackPos + 1) % t.length;
			if (trackPlaying != ctl.track) tp = 0;
			if (trackPosOut) *trackPosOut = tp;
			if (transOut) *transOut = t.slot[tp].transpose;
			return t.slot[tp].pattern;
		}
		if (trackPosOut) *trackPosOut = 0;
		if (transOut) *transOut = 0;
		return queued >= 0 ? queued : playing;
	}

	void startFirst() {
		// when starting: take the selected pattern (or slot 1 of the track) at once
		if (trackMode()) {
			trackPlaying = ctl.track;
			trackPos = 0;
			playing = track[ctl.track].slot[0].pattern;
			trackTranspose = track[ctl.track].slot[0].transpose;
		} else {
			playing = selected();
			trackTranspose = 0;
		}
		queued = -1;
		stepIdx = 0;
	}

	void loopEnd() {
		// mutation pass on the pattern that just played
		int n = (int)std::lround(std::fmin(std::fmax(ctl.mutate, 0.f), 1.f) * 4.f);
		if (n > 0) mutatePattern(playing, n);
		int tp = 0, tr = 0;
		int next = nextPatternAfterLoop(&tp, &tr);
		if (trackMode()) {
			trackPlaying = ctl.track;
			trackPos = tp;
			trackTranspose = tr;
		} else {
			trackTranspose = 0;
		}
		playing = next;
		queued = -1;
		stepIdx = 0;
	}

	void onPulse() {
		int pps = pulsesPerStep();
		if (needStart) {
			needStart = false;
			startFirst();
			startStep();
			pulseCount = 1;
			return;
		}
		if (pulseCount >= pps) {
			// finish the current step (a missed gate-off evaluation happens now)
			if (!gEvaluated) evaluateG();
			stepIdx++;
			if (stepIdx >= pattern[playing].length) loopEnd();
			startStep();
			pulseCount = 1;
		} else {
			pulseCount++;
		}
	}

	TimeSlot kindAfter(int i) {
		const PlayStep* m = mergedPlaying();
		if (i + 1 < pattern[playing].length) return m[i + 1].kind;
		int nxt = nextPatternAfterLoop(nullptr, nullptr);
		PlayStep tmp[STEPS];
		merge(pattern[nxt], tmp);
		return tmp[0].kind;
	}

	void startStep() {
		const PlayStep* m = mergedPlaying();
		if (stepIdx >= pattern[playing].length) stepIdx = 0;
		cur = m[stepIdx];
		phase = 0.f;
		gEvaluated = false;
		clockTrig = 0.001f;
		if (stepIdx == 0) endTrig = 0.001f;
		int trans = liveTranspose + trackTranspose + ctl.transposeIn;
		if (cur.kind == NOTE) {
			voct = (cur.semis + trans) / 12.f;
			accentOut = cur.accent;
			if (!gateHeld) { gate = false; pendingRise = true; }
		} else if (cur.kind == TIE) {
			if (!gateHeld) { gate = false; pendingRise = true; }
		} else {
			gate = false;
			accentOut = false;
		}
		gateHeld = false;
	}

	// at the gate-length point of a step
	void evaluateG() {
		gEvaluated = true;
		if (cur.kind == REST) { slideOut = false; return; }
		TimeSlot nxt = kindAfter(stepIdx);
		if (nxt == TIE) {
			gateHeld = true;
			slideOut = false;
		} else if (cur.slide && nxt == NOTE) {
			gateHeld = true;
			slideOut = true;
		} else if (cur.slide && nxt == REST) {
			// held to the end of the slid step; the REST turns it off
			gateHeld = false;
			slideOut = false;
		} else {
			gate = false;
			pendingRise = false;
			accentOut = false;
			slideOut = false;
			gateHeld = false;
		}
	}

	// ------------------------------------------------------------ per sample --
	SeqOutputs process(float dt) {
		// selection
		int sel = selected();
		if (!running) {
			if (!trackMode()) playing = sel;
			queued = -1;
		} else if (!trackMode()) {
			queued = (sel != playing) ? sel : -1;
		}
		bool rise = false;
		if (pendingRise) { gate = true; pendingRise = false; rise = true; }
		(void)rise;

		// clock
		if (running) {
			if (!ctl.clockPatched) {
				pulseAcc += dt * ctl.bpm / 60.f * 24.f;
				while (pulseAcc >= 1.f) { pulseAcc -= 1.f; onPulse(); }
				int pps = pulsesPerStep();
				phase = ((pulseCount - 1) + pulseAcc) / pps;
			} else {
				sincePulse += dt;
				int pps = pulsesPerStep();
				float frac = std::fmin(sincePulse / pulsePeriod, 0.999f);
				phase = std::fmin(((pulseCount - 1) + frac) / pps, 0.999f);
			}
			if (!needStart && !gEvaluated && phase >= ctl.gateLen) evaluateG();
		} else {
			sincePulse += dt;
		}

		// preview (PITCH WRITE while stopped)
		if (previewT > 0.f) {
			previewT -= dt;
			if (previewT <= 0.f && !running) gate = false;
		}

		SeqOutputs o;
		o.voct = voct;
		o.gate = gate;
		o.accent = accentOut && gate;
		o.slide = slideOut;
		o.clock = clockTrig > 0.f;
		o.end = endTrig > 0.f;
		if (clockTrig > 0.f) clockTrig -= dt;
		if (endTrig > 0.f) endTrig -= dt;
		return o;
	}

	// ------------------------------------------------------------ editing --
	void key(int n) {
		if (n < 0 || n > 12) return;
		switch (ctl.mode) {
			case PITCH_WRITE: {
				int p = selected();
				pushUndoPattern(p);
				pattern[p].pitch[cursorPitch].note = (uint8_t)n;
				touched(p);
				if (!running) {
					voct = slotSemis(pattern[p].pitch[cursorPitch]) / 12.f;
					gate = false; pendingRise = true; previewT = 0.15f;
				}
			} break;
			case PATTERN_PLAY:
			case TRACK_PLAY:
				liveTranspose = ctl.downHeld ? n - 12 : n;
				break;
			case TRACK_WRITE:
				editTranspose = ctl.downHeld ? n - 12 : n;
				break;
			default: break;
		}
	}
	void toggleFlag(int which) {   // 0 down, 1 up, 2 accent, 3 slide
		if (ctl.mode != PITCH_WRITE) return;
		int p = selected();
		pushUndoPattern(p);
		PitchSlot& s = pattern[p].pitch[cursorPitch];
		if (which == 0) { s.down = !s.down; if (s.down) s.up = false; }
		if (which == 1) { s.up = !s.up; if (s.up) s.down = false; }
		if (which == 2) s.accent = !s.accent;
		if (which == 3) s.slide = !s.slide;
		touched(p);
	}
	void writeTime(TimeSlot t) {
		if (ctl.mode != TIME_WRITE) return;
		int p = selected();
		pushUndoPattern(p);
		if (cursorTime >= pattern[p].length) cursorTime = 0;
		pattern[p].time[cursorTime] = t;
		touched(p);
		cursorTime = (cursorTime + 1) % pattern[p].length;
	}
	void next() {
		int p = selected();
		if (ctl.mode == PITCH_WRITE) cursorPitch = (cursorPitch + 1) % STEPS;
		else if (ctl.mode == TIME_WRITE) cursorTime = (cursorTime + 1) % pattern[p].length;
		else if (ctl.mode == TRACK_WRITE) {
			Track& t = track[ctl.track];
			pushUndoTrack(ctl.track);
			if (cursorTrack >= TRACK_SLOTS) cursorTrack = TRACK_SLOTS - 1;
			t.slot[cursorTrack].pattern = (uint8_t)p;
			t.slot[cursorTrack].transpose = (int8_t)editTranspose;
			if (cursorTrack + 1 > t.length) t.length = (uint8_t)(cursorTrack + 1);
			if (cursorTrack < TRACK_SLOTS - 1) cursorTrack++;
		}
	}
	void back() {
		int p = selected();
		if (ctl.mode == PITCH_WRITE) cursorPitch = (cursorPitch + STEPS - 1) % STEPS;
		else if (ctl.mode == TIME_WRITE) cursorTime = (cursorTime + pattern[p].length - 1) % pattern[p].length;
		else if (ctl.mode == TRACK_WRITE && cursorTrack > 0) cursorTrack--;
	}
	void clear() {
		if (ctl.mode == TRACK_WRITE) {
			pushUndoTrack(ctl.track);
			track[ctl.track].length = (uint8_t)cursorTrack;
			return;
		}
		int p = selected();
		pushUndoPattern(p);
		pattern[p] = Pattern();
		touched(p);
	}
	void setLength(int n) {
		int p = selected();
		n = n < 1 ? 1 : (n > 16 ? 16 : n);
		if (pattern[p].length == n) return;
		pushUndoPattern(p);
		pattern[p].length = (uint8_t)n;
		touched(p);
	}
	void setTriplet(bool t) {
		int p = selected();
		if (pattern[p].triplet == t) return;
		pushUndoPattern(p);
		pattern[p].triplet = t;
		touched(p);
	}

	// ------------------------------------------------------------ material tools --
	void scramble() {
		int p = selected();
		pushUndoPattern(p);
		Pattern& q = pattern[p];
		for (int i = 0; i < STEPS; i++) {
			PitchSlot& s = q.pitch[i];
			s.note = (uint8_t)rng.below(13);
			float r = rng.uniform();
			s.up = r < 0.15f;
			s.down = r >= 0.15f && r < 0.30f;
			s.accent = rng.uniform() < 0.25f;
			s.slide = rng.uniform() < 0.20f;
			float t = rng.uniform();
			q.time[i] = t < 0.70f ? NOTE : (t < 0.80f ? TIE : REST);
		}
		touched(p);
	}
	void shiftPitch() {
		int p = selected();
		pushUndoPattern(p);
		PitchSlot last = pattern[p].pitch[STEPS - 1];
		for (int i = STEPS - 1; i > 0; i--) pattern[p].pitch[i] = pattern[p].pitch[i - 1];
		pattern[p].pitch[0] = last;
		touched(p);
	}
	void shiftTime() {
		int p = selected();
		pushUndoPattern(p);
		int len = pattern[p].length;
		TimeSlot last = pattern[p].time[len - 1];
		for (int i = len - 1; i > 0; i--) pattern[p].time[i] = pattern[p].time[i - 1];
		pattern[p].time[0] = last;
		touched(p);
	}
	void mutatePattern(int p, int n) {
		pushUndoPattern(p);
		Pattern& q = pattern[p];
		for (int k = 0; k < n; k++) {
			int slot = rng.below(STEPS);
			switch (rng.below(5)) {
				case 0: q.pitch[slot].note = (uint8_t)rng.below(13); break;
				case 1: q.pitch[slot].accent = !q.pitch[slot].accent; break;
				case 2: q.pitch[slot].slide = !q.pitch[slot].slide; break;
				case 3: {
					PitchSlot& s = q.pitch[slot];
					if (!s.up && !s.down) s.up = true;
					else if (s.up) { s.up = false; s.down = true; }
					else s.down = false;
				} break;
				default: {
					int t = rng.below(q.length);
					q.time[t] = q.time[t] == NOTE ? REST : (q.time[t] == REST ? TIE : NOTE);
				} break;
			}
		}
		touched(p);
	}

	// ------------------------------------------------------------ direct edit --
	// Edit a merged RESULT cell of the selected pattern.
	void directPitch(int step, int note, int octave /* -1, 0, 1 */) {
		int p = selected();
		PlayStep m[STEPS];
		merge(pattern[p], m);
		if (step < 0 || step >= STEPS || m[step].pitchSlot < 0) return;
		pushUndoPattern(p);
		PitchSlot& s = pattern[p].pitch[m[step].pitchSlot];
		if (note >= 0 && note <= 12) s.note = (uint8_t)note;
		if (octave >= -1 && octave <= 1) { s.up = octave > 0; s.down = octave < 0; }
		touched(p);
	}
	void directFlag(int step, int which /* 2 accent, 3 slide */) {
		int p = selected();
		PlayStep m[STEPS];
		merge(pattern[p], m);
		if (step < 0 || step >= STEPS || m[step].pitchSlot < 0) return;
		pushUndoPattern(p);
		PitchSlot& s = pattern[p].pitch[m[step].pitchSlot];
		if (which == 2) s.accent = !s.accent;
		if (which == 3) s.slide = !s.slide;
		touched(p);
	}
	void directTime(int step, TimeSlot t) {
		int p = selected();
		if (step < 0 || step >= STEPS) return;
		pushUndoPattern(p);
		pattern[p].time[step] = t;
		touched(p);
	}

	// ------------------------------------------------------------ clipboard --
	void copy() { clip = pattern[selected()]; hasClip = true; }
	void paste() { if (!hasClip) return; int p = selected(); pushUndoPattern(p); pattern[p] = clip; touched(p); }
	// returns the index written, or -1 when no pattern is empty
	int copyToNextEmpty() {
		int p = selected();
		for (int k = 1; k < PATTERNS; k++) {
			int i = (p + k) % PATTERNS;
			if (pattern[i].isEmpty()) {
				pushUndoPattern(i);
				pattern[i] = pattern[p];
				touched(i);
				return i;
			}
		}
		return -1;
	}
	void clearAll() {
		for (int i = 0; i < PATTERNS; i++) pattern[i] = Pattern();
		for (int i = 0; i < TRACKS; i++) track[i] = Track();
		undoCount = 0;
		mergedDirty = true;
	}
};

} // namespace sb
