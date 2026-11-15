// Behaviour tests for SBS (spec §6.9).
#include "check.hpp"
#include "../src/seq/SeqEngine.hpp"
#include <vector>
#include <algorithm>

using namespace sb;

static Pattern blank() { return Pattern(); }
static void setTimes(Pattern& p, const char* t) {
	for (int i = 0; t[i] && i < 16; i++) p.time[i] = t[i] == 'T' ? TIE : (t[i] == 'R' ? REST : NOTE);
}

TEST("SBS merge: notes consume pitch slots, REST to NOTE shifts later pitches") {
	Pattern p = blank();
	for (int i = 0; i < 16; i++) p.pitch[i].note = (uint8_t)(i % 13);
	p.pitch[2].accent = true; p.pitch[3].slide = true;
	setTimes(p, "NRNNNNNNNNNNNNNN");
	PlayStep m[16];
	merge(p, m);
	CHECK(m[0].pitchSlot == 0 && m[1].kind == REST && m[2].pitchSlot == 1 && m[3].pitchSlot == 2);
	CHECK(m[3].accent && !m[2].accent);
	CHECK(m[4].slide);
	p.time[1] = NOTE;
	merge(p, m);
	CHECK(m[1].pitchSlot == 1 && m[2].pitchSlot == 2 && m[3].pitchSlot == 3);
	CHECK(m[2].accent && m[3].slide);
}

TEST("SBS merge: wrap past 16 notes, short LENGTH, TIE handling, octaves") {
	Pattern p = blank();
	p.pitch[0].note = 5; p.pitch[0].up = true; p.pitch[1].note = 7; p.pitch[1].down = true;
	PlayStep m[16];
	merge(p, m);
	CHECK(m[0].semis == 17 && m[1].semis == -5);
	CHECK_NEAR(m[0].volts, 17.f / 12.f, 1e-6);
	p.length = 4;
	merge(p, m);
	CHECK(m[3].kind == NOTE && m[4].kind == REST);
	p.length = 16;
	setTimes(p, "TNTRTN");
	merge(p, m);
	CHECK(m[0].kind == REST);            // TIE at step 1 plays as a REST
	CHECK(m[1].kind == NOTE && m[1].pitchSlot == 0);
	CHECK(m[2].kind == TIE && m[2].pitchSlot == 0 && m[2].semis == m[1].semis);
	CHECK(m[4].kind == REST);            // TIE after a REST
	CHECK(m[5].pitchSlot == 1);
}

TEST("SBS pattern text round trip and names") {
	Pattern p = demoPattern(); p.length = 11; p.triplet = true;
	Pattern q;
	CHECK(patternFromString(patternToString(p), q));
	CHECK(p == q);
	CHECK(patternName(0) == "I-1A" && patternName(1) == "I-1B" && patternName(63) == "IV-8B" && patternName(18) == "II-2A");
}

struct Log { std::vector<SeqOutputs> o; float dt; };
static Log runEngine(SeqEngine& e, float sec, float fs = 48000.f) {
	Log l; l.dt = 1.f / fs;
	int n = (int)(sec * fs);
	for (int i = 0; i < n; i++) l.o.push_back(e.process(l.dt));
	return l;
}
// times (s) of rising edges of a field
template <class F>
static std::vector<float> edges(const Log& l, F f, bool rising = true) {
	std::vector<float> t;
	for (size_t i = 0; i < l.o.size(); i++) {
		bool a = i ? f(l.o[i - 1]) : false, b = f(l.o[i]);
		if (rising ? (b && !a) : (a && !b)) t.push_back(i * l.dt);
	}
	return t;
}

TEST("SBS timing at 120 BPM: step 125 ms, gate 50%, triplet 83.3 ms") {
	SeqEngine e;
	e.pattern[0] = blank();
	e.setRunning(true);
	Log l = runEngine(e, 1.0f);
	auto clk = edges(l, [](const SeqOutputs& o) { return o.clock; });
	CHECK(clk.size() >= 7);
	CHECK_NEAR(clk[2] - clk[1], 0.125f, 0.0005f);
	auto up = edges(l, [](const SeqOutputs& o) { return o.gate; });
	auto dn = edges(l, [](const SeqOutputs& o) { return o.gate; }, false);
	CHECK(up.size() >= 7 && dn.size() >= 7);
	CHECK_NEAR(dn[1] - up[1], 0.0625f, 0.001f);
	SeqEngine t;
	t.pattern[0] = blank(); t.pattern[0].triplet = true;
	t.setRunning(true);
	Log lt = runEngine(t, 1.0f);
	auto ct = edges(lt, [](const SeqOutputs& o) { return o.clock; });
	CHECK_NEAR(ct[2] - ct[1], 0.08333f, 0.0005f);
}

TEST("SBS gate: one rising edge for a note plus two ties, held through a slide") {
	SeqEngine e;
	Pattern p = blank();
	setTimes(p, "NTTNNRNNNNNNNNNN");
	p.pitch[1].slide = true;          // the second note (step 4) slides into step 5
	p.pitch[1].note = 3; p.pitch[2].note = 7;
	e.pattern[0] = p;
	e.setRunning(true);
	Log l = runEngine(e, 0.125f * 6);
	auto up = edges(l, [](const SeqOutputs& o) { return o.gate; });
	auto dn = edges(l, [](const SeqOutputs& o) { return o.gate; }, false);
	// rises: step1, step4 (none at 5: held). falls: end of tie at 3 (g), step 5 at g.
	CHECK(up.size() == 2);
	CHECK_NEAR(up[1], 0.375f, 0.001f);
	CHECK(dn.size() == 2);
	CHECK_NEAR(dn[0], 0.25f + 0.0625f, 0.001f);
	CHECK_NEAR(dn[1], 0.5f + 0.0625f, 0.001f);
	// SLIDE window: from g of step 4 to g of step 5
	auto su = edges(l, [](const SeqOutputs& o) { return o.slide; });
	auto sd = edges(l, [](const SeqOutputs& o) { return o.slide; }, false);
	CHECK(su.size() == 1 && sd.size() == 1);
	CHECK_NEAR(su[0], 0.375f + 0.0625f, 0.001f);
	CHECK_NEAR(sd[0], 0.5f + 0.0625f, 0.001f);
	// pitch changes at step 5 while SLIDE is high
	size_t i5 = (size_t)(0.5f / l.dt) + 2;
	CHECK(l.o[i5].slide && l.o[i5].gate);
	CHECK_NEAR(l.o[i5].voct, 7.f / 12.f, 1e-6);
}

TEST("SBS slide into REST holds to the end of the step; ACCENT only on accented notes") {
	SeqEngine e;
	Pattern p = blank();
	setTimes(p, "NRNNNNNNNNNNNNNN");
	p.pitch[0].slide = true; p.pitch[1].accent = true;
	e.pattern[0] = p;
	e.setRunning(true);
	Log l = runEngine(e, 0.125f * 3);
	auto dn = edges(l, [](const SeqOutputs& o) { return o.gate; }, false);
	CHECK(!dn.empty());
	CHECK_NEAR(dn[0], 0.125f, 0.001f);
	auto au = edges(l, [](const SeqOutputs& o) { return o.accent; });
	auto ad = edges(l, [](const SeqOutputs& o) { return o.accent; }, false);
	CHECK(au.size() == 1 && ad.size() == 1);
	CHECK_NEAR(au[0], 0.25f, 0.001f);
	CHECK_NEAR(ad[0], 0.25f + 0.0625f, 0.001f);
	CHECK(edges(l, [](const SeqOutputs& o) { return o.slide; }).empty());
}

TEST("SBS external clocks give the same steps as the internal clock") {
	for (int mode : {0, 1}) {
		SeqEngine e;
		e.pattern[0] = blank();
		e.ctl.clockPatched = true;
		e.ctl.clockMode = mode == 0 ? CLOCK_24PPQN : CLOCK_PER_STEP;
		e.setRunning(true);
		float fs = 48000.f, dt = 1.f / fs;
		float period = mode == 0 ? 0.5f / 24.f : 0.125f;
		std::vector<float> clk, up, dn;
		bool pc = false, pg = false;
		float next = 0.f;
		for (int i = 0; i < (int)(fs * 1.0f); i++) {
			float t = i * dt;
			if (t >= next) { e.clockEdge(); next += period; }
			SeqOutputs o = e.process(dt);
			if (o.clock && !pc) clk.push_back(t);
			if (o.gate && !pg) up.push_back(t);
			if (!o.gate && pg) dn.push_back(t);
			pc = o.clock; pg = o.gate;
		}
		CHECK(clk.size() >= 7);
		CHECK_NEAR(clk[3] - clk[2], 0.125f, 0.0005f);
		CHECK_NEAR(dn[3] - up[3], 0.0625f, 0.002f);
	}
}

TEST("SBS pattern change is queued to the loop end; track play with transpose") {
	SeqEngine e;
	e.pattern[0] = blank(); e.pattern[1] = blank();
	e.pattern[1].pitch[0].note = 7; e.pattern[1].length = 4;
	e.pattern[0].length = 4;
	e.setRunning(true);
	runEngine(e, 0.2f);
	e.ctl.pattern = 1;
	runEngine(e, 0.1f);
	CHECK(e.playing == 0 && e.queued == 1);
	runEngine(e, 0.3f);   // past 0.5 s = loop end
	CHECK(e.playing == 1);
	// track: slot 0 = pattern 0, slot 1 = pattern 1 transposed +5
	SeqEngine t;
	t.pattern[0] = blank(); t.pattern[0].length = 2;
	t.pattern[1] = blank(); t.pattern[1].length = 2; t.pattern[1].pitch[0].note = 2;
	t.track[0].length = 2;
	t.track[0].slot[0].pattern = 0; t.track[0].slot[1].pattern = 1; t.track[0].slot[1].transpose = 5;
	t.ctl.mode = TRACK_PLAY;
	t.setRunning(true);
	Log l = runEngine(t, 0.5f + 0.01f);
	CHECK_NEAR(l.o[(size_t)(0.01f / l.dt)].voct, 0.f, 1e-6);
	CHECK_NEAR(l.o[(size_t)(0.26f / l.dt)].voct, 7.f / 12.f, 1e-6);
	CHECK_NEAR(l.o[(size_t)(0.51f / l.dt)].voct, 0.f, 1e-6);
}

TEST("SBS write modes: pitch, flags, time, cursor") {
	SeqEngine e;
	e.pattern[0] = blank();
	e.ctl.mode = PITCH_WRITE;
	e.key(4); e.toggleFlag(1); e.toggleFlag(2); e.next(); e.key(9); e.toggleFlag(0); e.toggleFlag(3);
	CHECK(e.pattern[0].pitch[0].note == 4 && e.pattern[0].pitch[0].up && e.pattern[0].pitch[0].accent);
	CHECK(e.pattern[0].pitch[1].note == 9 && e.pattern[0].pitch[1].down && e.pattern[0].pitch[1].slide);
	e.toggleFlag(1);   // UP clears DOWN
	CHECK(e.pattern[0].pitch[1].up && !e.pattern[0].pitch[1].down);
	e.ctl.mode = TIME_WRITE;
	e.writeTime(REST); e.writeTime(TIE); e.writeTime(NOTE);
	CHECK(e.pattern[0].time[0] == REST && e.pattern[0].time[1] == TIE && e.pattern[0].time[2] == NOTE && e.cursorTime == 3);
	e.ctl.mode = TRACK_WRITE;
	e.ctl.pattern = 5; e.key(3); e.next();
	e.ctl.pattern = 6; e.ctl.downHeld = true; e.key(10); e.next();
	CHECK(e.track[0].length == 2 && e.track[0].slot[0].pattern == 5 && e.track[0].slot[0].transpose == 3);
	CHECK(e.track[0].slot[1].pattern == 6 && e.track[0].slot[1].transpose == -2);
}

TEST("SBS scramble probabilities, mutate 0 changes nothing, undo is exact") {
	SeqEngine e;
	e.rng.seed(12345);
	int up = 0, dn = 0, acc = 0, sl = 0, note = 0, tie = 0, rest = 0, n = 0;
	for (int k = 0; k < 10000; k++) {
		e.scramble();
		for (int i = 0; i < 16; i++) {
			const PitchSlot& s = e.pattern[0].pitch[i];
			up += s.up; dn += s.down; acc += s.accent; sl += s.slide;
			note += e.pattern[0].time[i] == NOTE; tie += e.pattern[0].time[i] == TIE; rest += e.pattern[0].time[i] == REST;
			n++;
		}
	}
	CHECK_NEAR((float)up / n, 0.15f, 0.0075f);
	CHECK_NEAR((float)dn / n, 0.15f, 0.0075f);
	CHECK_NEAR((float)acc / n, 0.25f, 0.0125f);
	CHECK_NEAR((float)sl / n, 0.20f, 0.01f);
	CHECK_NEAR((float)note / n, 0.70f, 0.035f);
	CHECK_NEAR((float)tie / n, 0.10f, 0.005f);
	CHECK_NEAR((float)rest / n, 0.20f, 0.01f);

	SeqEngine m;
	Pattern before = m.pattern[0];
	m.ctl.mutate = 0.f;
	m.setRunning(true);
	runEngine(m, 5.f);
	CHECK(m.pattern[0] == before);
	m.ctl.mutate = 1.f;
	runEngine(m, 2.1f);
	CHECK(!(m.pattern[0] == before));

	SeqEngine u;
	Pattern orig = u.pattern[0];
	u.ctl.mode = PITCH_WRITE;
	for (int k = 0; k < 32; k++) {
		switch (k % 4) {
			case 0: u.scramble(); break;
			case 1: u.shiftPitch(); break;
			case 2: u.shiftTime(); break;
			default: u.key(k % 13); break;
		}
	}
	CHECK(!(u.pattern[0] == orig));
	for (int k = 0; k < 32; k++) u.doUndo();
	CHECK(u.pattern[0] == orig);
}

TEST("SBS direct edit changes the slot a RESULT cell uses") {
	SeqEngine e;
	Pattern p = blank();
	setTimes(p, "RNNN");
	e.pattern[0] = p;
	e.directPitch(2, 9, 1);   // step 3 uses pitch slot 1
	CHECK(e.pattern[0].pitch[1].note == 9 && e.pattern[0].pitch[1].up);
	e.directFlag(3, 2);
	CHECK(e.pattern[0].pitch[2].accent);
	e.directTime(0, NOTE);    // now every later step shifts by one slot
	PlayStep m[16];
	merge(e.pattern[0], m);
	CHECK(m[3].pitchSlot == 3 && m[2].pitchSlot == 2);
	CHECK(e.copyToNextEmpty() == 1);
	CHECK(e.pattern[1] == e.pattern[0]);
}

TEST("SBS preview in PITCH WRITE while stopped") {
	SeqEngine e;
	e.ctl.mode = PITCH_WRITE;
	e.key(7);
	Log l = runEngine(e, 0.3f);
	CHECK(l.o[10].gate);
	CHECK_NEAR(l.o[10].voct, 7.f / 12.f, 1e-6);
	CHECK(!l.o.back().gate);
}
