// Tests for Scala parsing, SBT mapping and SBS's tuning step (round 2 spec §1a, §4.5).
#include "check.hpp"
#include "../src/tuning/Scala.hpp"
#include "../src/tuning/Tuner.hpp"
#include "../src/seq/SeqEngine.hpp"
#include <algorithm>

using namespace sb;

static float centsOf(float v) { return v * 1200.f; }

TEST("Scala parser: cents, ratios, mixed, comments, blank description, trailing text, N = 0") {
	Scale s; std::string err;
	CHECK(parseScl("! comment\n! more\nCents scale\n 3\n!\n100.0\n250.5 trailing words\n1200.\n", s, err));
	CHECK(s.name == "Cents scale" && s.n() == 3);
	CHECK_NEAR(s.cents[1], 250.5, 1e-12);
	CHECK(parseScl("Ratios\n3\n9/8\n5/4 major third\n2\n", s, err));
	CHECK_NEAR(s.cents[0], 203.91000173, 1e-6);
	CHECK_NEAR(s.cents[2], 1200.0, 1e-9);
	CHECK(parseScl("!\n\n2\n701.955\n2/1\n", s, err, "file.scl"));
	CHECK(s.name == "file.scl" && s.n() == 2);
	CHECK(parseScl("Unison only\n0\n", s, err));
	CHECK(s.n() == 0);
	CHECK_NEAR(s.degreeCents(3), 3600.0, 1e-9);
	// malformed files are reported and leave the scale unchanged
	Scale keep = equal12();
	Scale t = keep;
	CHECK(!parseScl("Bad\n2\n100.0\n", t, err));               // missing a line
	CHECK(!parseScl("Bad\n1\n-3/2\n", t, err));                // negative ratio
	CHECK(!parseScl("Bad\n2000\n", t, err));                   // too many notes
	CHECK(!parseScl("Bad\nx\n", t, err));
	CHECK(!parseScl("Bad\n1\n3/0\n", t, err));
	CHECK(t.n() == 12 && t.name == keep.name);
	// round trip through the patch text is exact
	Scale u; builtinScale(1, u);
	u.kbm.loaded = true; u.kbm.size = 3; u.kbm.map = {0, -1, 4}; u.kbm.refFreq = 432.123456789;
	Scale back;
	CHECK(deserializeScale(serializeScale(u), back));
	bool same = back.name == u.name && back.cents == u.cents && back.oneKeyPerDegree == u.oneKeyPerDegree &&
	            back.kbm.map == u.kbm.map && back.kbm.refFreq == u.kbm.refFreq && back.kbm.loaded;
	CHECK(same);
}

TEST("SBT mapping: 12-equal identity, just pitches, octaves, ROOT") {
	TunerCore t; Scale eq = equal12();
	float in[16], out[16];
	float worst = 0.f;
	for (int i = 0; i < 2000; i++) {
		in[0] = -5.f + i * 0.00513f;
		t.process(eq, 0, 7.f, in, out, 1);
		worst = std::max(worst, std::fabs(out[0] - (in[0] + 7.f / 1200.f)));
	}
	CHECK(worst < 1e-6f);
	Scale ji; builtinScale(1, ji);
	CHECK_NEAR(centsOf(TuningMap::map(ji, 0, 0.f, 4.f / 12.f, 0.f)), 386.31f, 0.01f);
	CHECK_NEAR(centsOf(TuningMap::map(ji, 0, 0.f, 7.f / 12.f, 0.f)), 701.96f, 0.01f);
	CHECK_NEAR(centsOf(TuningMap::map(ji, 0, 0.f, 1.f + 7.f / 12.f, 0.f)) - centsOf(TuningMap::map(ji, 0, 0.f, 7.f / 12.f, 0.f)), 1200.f, 1e-3f);
	// ROOT = D: D itself is unchanged, E is a just 9/8 above D
	CHECK_NEAR(centsOf(TuningMap::map(ji, 2, 0.f, 2.f / 12.f, 0.f)), 200.f, 1e-3f);
	CHECK_NEAR(centsOf(TuningMap::map(ji, 2, 0.f, 4.f / 12.f, 0.f)), 200.f + 203.91f, 0.01f);
}

TEST("SBT mapping modes: Bohlen-Pierce one key per degree, 19-equal and 7-equal with 12 keys") {
	Scale bp; builtinScale(9, bp);
	CHECK(bp.name == "Bohlen-Pierce" && bp.oneKeyPerDegree);
	CHECK_NEAR(centsOf(TuningMap::map(bp, 0, 0.f, 13.f / 12.f, 0.f)), 1901.96f, 0.01f);
	Scale e19; builtinScale(7, e19);
	CHECK_NEAR(centsOf(TuningMap::map(e19, 0, 0.f, 11.f / 12.f, 0.f)), 11.f * 1200.f / 19.f, 0.01f);
	CHECK_NEAR(centsOf(TuningMap::map(e19, 0, 0.f, 1.f, 0.f)), 1200.f, 0.01f);
	Scale e7; builtinScale(6, e7);
	float prev = -1.f; bool mono = true; int distinct = 0;
	for (int k = 0; k < 12; k++) {
		float c = centsOf(TuningMap::map(e7, 0, 0.f, k / 12.f, 0.f));
		if (c < prev - 1e-3f) mono = false;
		if (c > prev + 1e-3f) distinct++;
		prev = c;
	}
	CHECK(mono);
	CHECK(distinct == 7);
}

TEST("SBT continuity, polyphony, kbm with white keys only, slot choice, built-ins valid") {
	Scale ji; builtinScale(1, ji);
	TunerCore t;
	float in[16], out[16], prev = -100.f, step = 1e-4f, worstJump = 0.f;
	bool mono = true;
	for (int i = 0; i < 30000; i++) {
		in[0] = -1.f + i * step;
		t.process(ji, 0, 0.f, in, out, 1);
		if (out[0] < prev - 1e-6f) mono = false;
		if (i) worstJump = std::max(worstJump, out[0] - prev);
		prev = out[0];
	}
	CHECK(mono);
	double slope = 0, last = 0;   // largest step between neighbouring keys, in semitones
	for (double c : ji.cents) { slope = std::max(slope, (c - last) / 100.0); last = c; }
	INFO("  worst jump %.3g V for a %.3g V step, largest slope %.3f\n", worstJump, step, slope);
	CHECK(worstJump < step * slope * 1.01);
	for (int c = 0; c < 16; c++) in[c] = c / 12.f;
	t.process(ji, 0, 0.f, in, out, 16);
	bool poly = true;
	for (int c = 0; c < 16; c++) if (std::fabs(out[c] - TuningMap::map(ji, 0, 0.f, in[c], 0.f)) > 1e-7f) poly = false;
	CHECK(poly);
	// kbm mapping only the white keys onto a 7-note scale; black keys hold
	Kbm k; std::string err;
	CHECK(parseKbm("! white keys\n12\n0\n127\n60\n69\n440.0\n7\n0\nx\n1\nx\n2\n3\nx\n4\nx\n5\nx\n6\n", k, err));
	Scale maj; CHECK(parseScl("Major\n7\n9/8\n5/4\n4/3\n3/2\n5/3\n15/8\n2/1\n", maj, err));
	maj.kbm = k;
	float c4 = TuningMap::map(maj, 0, 0.f, 0.f, 0.f);
	float a4 = TuningMap::map(maj, 0, 0.f, 9.f / 12.f, 0.f);
	CHECK_NEAR(261.6256f * std::exp2(a4), 440.f, 0.01f);
	CHECK_NEAR(centsOf(a4 - c4), 1200.f * std::log2(5.f / 3.f), 0.01f);
	CHECK(TuningMap::map(maj, 0, 0.f, 1.f / 12.f, 0.123f) == 0.123f);   // C# unmapped: holds
	CHECK_NEAR(centsOf(TuningMap::map(maj, 0, 0.f, 1.f, 0.f) - c4), 1200.f, 0.01f);
	// slots
	CHECK(tunerSlot(0.f, 0.f) == 0 && tunerSlot(3.f, 0.f) == 3 && tunerSlot(0.f, 10.f) == 7 && tunerSlot(5.f, 10.f) == 4);
	for (int i = 0; i < (int)builtinScales().size(); i++) { Scale s; CHECK(builtinScale(i, s)); CHECK(s.n() > 0); }
	Scale mt; builtinScale(3, mt);
	CHECK_NEAR(mt.cents[6], 696.578, 0.001);   // fifth of quarter-comma meantone
}

// SBS's tuning step against the engine. Mirrors SBS.cpp: the jack is latched at a NOTE step start.
static std::vector<float> runSbs(SeqEngine& e, const Scale* s, int jack, float secs, std::vector<float>* raw = nullptr) {
	SbsTuning t;
	std::vector<float> out;
	bool prevClock = false;
	e.ctl.transposeIn = jack;
	e.setRunning(true);
	for (int i = 0; i < (int)(secs * 48000); i++) {
		SeqOutputs o = e.process(1.f / 48000.f);
		bool start = o.clock && !prevClock && e.cur.kind == NOTE;
		prevClock = o.clock;
		out.push_back(t.process(s, 0, o.voct, e.ctl.transposeIn, start));
		if (raw) raw->push_back(o.voct);
	}
	return out;
}

TEST("SBS tuning: none equals round 1; just pitches; UP adds a period; TRANSPOSE is exact") {
	SeqEngine a;
	std::vector<float> raw;
	std::vector<float> t = runSbs(a, nullptr, 0, 4.f, &raw);
	CHECK(t == raw);
	Scale eq = equal12();
	SeqEngine b;
	CHECK(runSbs(b, &eq, 0, 4.f) == raw);
	// Just intonation: every output is a just pitch of the semitone the engine plays
	Scale ji; builtinScale(1, ji);
	SeqEngine c;
	std::vector<float> rj;
	std::vector<float> j = runSbs(c, &ji, 0, 4.f, &rj);
	bool ok = true;
	for (size_t i = 0; i < j.size(); i++) {
		long key = std::lround(rj[i] * 12.f);
		long o = key >= 0 ? key / 12 : -((-key + 11) / 12), d = key - o * 12;
		float want = (float)(o + (d == 0 ? 0.0 : ji.cents[d - 1] / 1200.0));
		if (std::fabs(j[i] - want) > 1e-5f) ok = false;
	}
	CHECK(ok);
	// UP (one octave above in the engine) adds exactly one period
	CHECK_NEAR(TuningMap::map(ji, 0, 0.f, 1.f + 4.f / 12.f, 0.f) - TuningMap::map(ji, 0, 0.f, 4.f / 12.f, 0.f), 1.f, 1e-6f);
	// TRANSPOSE jack: +3 semitones gives exactly +0.25 V on top of the mapped notes
	SeqEngine d;
	std::vector<float> tj = runSbs(d, &ji, 3, 4.f);
	bool exact = true;
	for (size_t i = 0; i < tj.size(); i++) if (std::fabs(tj[i] - j[i] - 0.25f) > 1e-5f) exact = false;
	CHECK(exact);
}
