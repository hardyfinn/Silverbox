// Behaviour tests for SBV (spec §5.8, §5.2a).
#include "check.hpp"
#include "../src/dsp/Voice303.hpp"
#include <algorithm>

using namespace sb;

static float ladderGain(float f, float fc, float k, float fs = 192000.f, float amp = 0.02f) {
	DiodeLadder303 l;
	l.setRate(fs);
	int n = (int)(fs * 0.6f), m = (int)(fs * 0.15f);
	float pk = 0.f;
	for (int i = 0; i < n; i++) {
		float y = l.process(amp * std::sin(2.f * PI_F * f * i / fs), fc, k);
		if (i > n - m) pk = std::max(pk, std::fabs(y));
	}
	return pk / amp;
}
static float dB(float r) { return 20.f * std::log10(std::max(r, 1e-12f)); }

// Run the voice with a held gate and return a lambda-friendly struct.
struct Rig {
	Voice303 v;
	VoiceParams p;
	VoiceExt x;
	VoiceIn in;
	float fs;
	explicit Rig(float sr = 48000.f) : fs(sr) { v.setRate(sr); }
	VoiceOut step() { return v.process(in, p, x); }
	void run(float sec) { int n = (int)(sec * fs); for (int i = 0; i < n; i++) step(); }
};

TEST("SBV pitch: 0 V = C2, +1 V doubles, TUNING shifts") {
	for (float sr : {44100.f, 48000.f, 96000.f}) {
		Rig r(sr);
		r.in.gate = true;
		auto count = [&](float v, float tune) {
			r.in.voct = v; r.p.tuning = tune; r.run(0.05f);
			int wraps = 0; float prev = r.v.osc.phase;
			int n = (int)(2.f * sr);
			for (int i = 0; i < n; i++) { r.step(); for (int j = 0; j < 1; j++) {} if (r.v.osc.phase < prev) wraps++; prev = r.v.osc.phase; }
			return wraps / 2.f;
		};
		float f0 = count(0.f, 0.f);
		CHECK_NEAR(f0, 65.41f, 0.6f);
		CHECK_NEAR(count(1.f, 0.f) / f0, 2.f, 0.02f);
		CHECK_NEAR(count(0.f, 12.f) / f0, 2.f, 0.02f);
		CHECK_NEAR(count(0.f, -12.f) / f0, 0.5f, 0.01f);
	}
}

TEST("SBV oscillator: falling saw, off-centre darker square") {
	CHECK(Osc303::sawShape(0.1f) > Osc303::sawShape(0.9f));
	CHECK_NEAR(Osc303::sawShape(0.f), 1.f, 1e-5);
	CHECK_NEAR(Osc303::sawShape(1.f), -1.f, 1e-5);
	Osc303 o; o.setRate(192000.f);
	int hi = 0, n = 192000;
	double sawSq = 0, sqSq = 0;
	Osc303 o2; o2.setRate(192000.f);
	for (int i = 0; i < n; i++) {
		float s = o.process(110.f, true);
		if (o.lastShaped > 0.f) hi++;
		sqSq += s * s;
		float w = o2.process(110.f, false);
		sawSq += w * w;
	}
	float duty = (float)hi / n;
	INFO("  square duty %.3f, rms sq %.3f saw %.3f\n", duty, std::sqrt(sqSq / n), std::sqrt(sawSq / n));
	CHECK(duty > 0.44f && duty < 0.49f);
	CHECK(sqSq < sawSq);
}

TEST("SBV filter: slope under 24 dB/oct, resonant peak, ringing, feedback high-pass") {
	float fc = 1000.f;
	float g2 = ladderGain(2 * fc, fc, 0.f), g16 = ladderGain(16 * fc, fc, 0.f);
	float slope = (dB(g2) - dB(g16)) / 3.f;
	INFO("  slope 1-4 oct above fc: %.1f dB/oct\n", slope);
	CHECK(slope > 15.f && slope < 22.f);
	// peak at RESONANCE 1 vs the passband (fc/4, above the feedback high-pass) at RESONANCE 1
	float kx = resonanceK(1.f, fc);
	float pk = 0.f, pkF = 0.f;
	for (float m = 0.9f; m <= 1.2f; m += 0.025f) { float g = ladderGain(fc * m, fc, kx); if (g > pk) { pk = g; pkF = m; } }
	float pass = ladderGain(fc / 4.f, fc, kx);
	INFO("  peak at %.3f x fc\n", pkF);
	INFO("  peak %.1f dB over passband\n", dB(pk / pass));
	CHECK(dB(pk / pass) >= 15.f);
	// the peak is at fc (within the shift the feedback high-pass causes)
	CHECK(pkF > 0.95f && pkF < 1.15f);
	// impulse rings at least 200 ms at CUT OFF FREQ 0.5 but does not sustain
	{
		DiodeLadder303 l; float fs = 192000.f; l.setRate(fs);
		float f = CUT_LO * std::sqrt(CUT_HI / CUT_LO);
		float early = 0, at200 = 0, late = 0;
		for (int i = 0; i < (int)(fs * 3); i++) {
			float y = std::fabs(l.process(i == 0 ? 1.f : 0.f, f, resonanceK(1.f, f)));
			float t = i / fs;
			if (t > 0.01f && t < 0.03f) early = std::max(early, y);
			if (t > 0.2f && t < 0.22f) at200 = std::max(at200, y);
			if (t > 2.8f) late = std::max(late, y);
		}
		INFO("  ring: early %.3g, 200ms %.3g, 2.8s %.3g\n", early, at200, late);
		CHECK(at200 > early * 0.01f);
		CHECK(late < at200 * 0.1f);
	}
	// feedback high-pass: resonance is weaker at 80 Hz than at 800 Hz
	float r80 = ladderGain(80.f, 80.f, resonanceK(1.f, 80.f)) / ladderGain(80.f, 80.f, 0.f);
	float r800 = ladderGain(800.f, 800.f, resonanceK(1.f, 800.f)) / ladderGain(800.f, 800.f, 0.f);
	INFO("  resonance gain at 80 Hz %.1f dB, at 800 Hz %.1f dB\n", dB(r80), dB(r800));
	CHECK(dB(r800) - dB(r80) >= 6.f);
}

TEST("SBV ENV MOD lowers the resting cutoff") {
	Rig a, b;
	a.p.envMod = 0.f; b.p.envMod = 1.f;
	a.run(0.01f); b.run(0.01f);
	CHECK(b.v.lastFcHz < a.v.lastFcHz * 0.5f);
}

static float timeTo(Rig& r, std::function<float()> val, float level, float maxT) {
	int n = (int)(maxT * r.fs);
	for (int i = 0; i < n; i++) { r.step(); if (val() <= level) return i / r.fs; }
	return maxT;
}

TEST("SBV MEG decay range and accent override") {
	for (float d : {0.f, 1.f}) {
		Rig r; r.p.decay = d; r.in.gate = true; r.run(0.01f);
		float t = timeTo(r, [&] { return r.v.meg.v; }, 0.01f, 5.f);
		CHECK_NEAR(t, d == 0.f ? 0.2f : 2.0f, d == 0.f ? 0.03f : 0.25f);
	}
	Rig r; r.p.decay = 1.f; r.in.gate = true; r.in.accent = true; r.run(0.01f);
	float t = timeTo(r, [&] { return r.v.meg.v; }, 0.01f, 5.f);
	CHECK_NEAR(t, 0.2f, 0.03f);
}

TEST("SBV VEG: slow fade with gate held, fast close on gate off") {
	Rig r; r.in.gate = true; r.run(1.f);
	CHECK(r.v.veg.v > 0.2f && r.v.veg.v < 0.6f);
	r.in.gate = false;
	float t = timeTo(r, [&] { return r.v.veg.v; }, 0.01f, 1.f);
	CHECK(t < 0.015f);
}

// Four (or two) accented notes 125 ms apart, gate 62.5 ms; returns sweep peak and time per note.
static void accentRun(float res, int notes, std::vector<float>& peaks, std::vector<float>& when) {
	Rig r; r.p.resonance = res; r.p.accent = 1.f;
	int stepN = (int)(0.125f * r.fs);
	for (int k = 0; k < notes; k++) {
		float pk = 0, pt = 0;
		for (int i = 0; i < stepN; i++) {
			r.in.gate = i < stepN / 2;
			r.in.accent = r.in.gate;
			r.step();
			if (r.v.sweep.s > pk) { pk = r.v.sweep.s; pt = i / r.fs; }
		}
		peaks.push_back(pk); when.push_back(pt);
	}
}

TEST("SBV accent sweep: sharp at low resonance, stacking and rounded at high") {
	std::vector<float> p, w;
	accentRun(0.f, 2, p, w);
	INFO("  res 0: peaks %.3f %.3f at %.1f %.1f ms\n", p[0], p[1], w[0] * 1e3, w[1] * 1e3);
	CHECK(w[0] <= 0.006f);
	CHECK(p[1] <= 1.1f * p[0]);
	p.clear(); w.clear();
	accentRun(1.f, 4, p, w);
	INFO("  res 1: peaks %.3f %.3f %.3f %.3f, first at %.1f ms\n", p[0], p[1], p[2], p[3], w[0] * 1e3);
	CHECK(w[0] >= 0.030f && w[0] <= 0.090f);
	CHECK(p[1] > p[0] && p[2] > p[1] && p[3] > p[2]);
	CHECK(p[3] >= 1.5f * p[0]);
}

TEST("SBV VCA accent boost") {
	Rig a, b; a.in.gate = b.in.gate = true; b.in.accent = true; a.p.accent = b.p.accent = 1.f;
	a.run(0.02f); b.run(0.02f);
	CHECK_NEAR(b.v.accGain, 1.f + VCA_ACC_GAIN, 0.01f);
	CHECK_NEAR(a.v.accGain, 1.f, 0.001f);
}

TEST("SBV slide: glides 80-85% in 60 ms when SLIDE is high, jumps otherwise, no retrigger") {
	Rig r; r.in.gate = true; r.run(0.05f);
	r.in.slide = true; r.in.voct = 1.f;
	int n = (int)(0.060f * r.fs);
	for (int i = 0; i < n; i++) r.step();
	INFO("  glide after 60 ms: %.3f\n", r.v.pitchV);
	CHECK(r.v.pitchV > 0.80f && r.v.pitchV < 0.85f);
	CHECK(!r.v.meg.attacking);
	r.in.slide = false; r.run(0.05f);
	r.in.voct = 0.f; r.step();
	CHECK_NEAR(r.v.pitchV, 0.f, 1e-6);
	// a glide continues to completion even if SLIDE drops mid-way
	r.in.slide = true; r.in.voct = 1.f; r.step();
	r.in.slide = false; r.run(0.02f);
	CHECK(r.v.pitchV > 0.2f && r.v.pitchV < 0.8f);
}

TEST("SBV AUDIO IN replaces the oscillator") {
	Rig r; r.in.gate = true; r.in.audioPatched = true; r.p.cutoff = 1.f;
	float pk = 0;
	for (int i = 0; i < 4800; i++) { r.in.audioIn = 0.f; pk = std::max(pk, std::fabs(r.step().out)); }
	CHECK(pk < 1e-3f);
	for (int i = 0; i < 4800; i++) { r.in.audioIn = 5.f * std::sin(2 * PI_F * 200.f * i / 48000.f); pk = std::max(pk, std::fabs(r.step().out)); }
	CHECK(pk > 0.5f);
}

TEST("SBV KEY TRACK follows pitch around 1 V, including during a slide") {
	Rig r; r.p.keyTrack = 1.f; r.in.gate = true;
	r.in.voct = 1.f; r.run(0.02f); float f1 = r.v.lastFcHz;
	r.in.voct = 2.f; r.run(0.02f); float f2 = r.v.lastFcHz;
	r.in.voct = 0.f; r.run(0.02f);
	// MEG has moved meanwhile; compare with tracking off at the same instant instead
	Rig a, b; a.p.keyTrack = 1.f; b.p.keyTrack = 0.f;
	a.in.voct = b.in.voct = 2.f; a.run(0.01f); b.run(0.01f);
	CHECK_NEAR(a.v.lastFcHz / b.v.lastFcHz, 2.f, 0.06f);
	a.in.voct = b.in.voct = 1.f; a.run(0.01f); b.run(0.01f);
	CHECK_NEAR(a.v.lastFcHz / b.v.lastFcHz, 1.f, 0.03f);
	(void)f1; (void)f2;
	Rig c, d; c.p.keyTrack = 0.f; d.p.keyTrack = 0.f; c.in.voct = 2.f; c.run(0.01f);
	CHECK_NEAR(c.v.lastFcHz / a.v.lastFcHz * 1.f, 1.f, 1.f);
	// glide: cutoff moves with the gliding pitch
	Rig g, h; g.p.keyTrack = 1.f; g.in.gate = h.in.gate = true; g.run(0.05f); h.run(0.05f);
	g.in.slide = h.in.slide = true; g.in.voct = h.in.voct = 1.f;
	for (int i = 0; i < 480; i++) { g.step(); h.step(); }   // 10 ms into the glide
	float ratio = std::log2(g.v.lastFcHz / h.v.lastFcHz) + KEY_TRACK_REF;
	CHECK_NEAR(ratio, g.v.pitchV, 0.02f);
}

static float toneLevel(float res, float bass, float f, float cutoff) {
	Rig r; r.p.resonance = res; r.p.bassComp = bass; r.p.envMod = 0.f; r.p.cutoff = cutoff;
	r.in.gate = true; r.in.audioPatched = true;
	float pk = 0;
	int n = (int)(0.5f * r.fs);
	for (int i = 0; i < n; i++) {
		r.in.audioIn = 0.5f * std::sin(2 * PI_F * f * i / r.fs);
		float y = r.step().out;
		if (i > n / 2) pk = std::max(pk, std::fabs(y));
	}
	return pk;
}

TEST("SBV BASS COMP restores the low end without touching the peak") {
	float cut = std::log(1000.f / CUT_LO) / std::log(CUT_HI / CUT_LO);   // resonant peak at 1 kHz
	for (float f : {50.f, 75.f, 100.f}) {
		float ref = toneLevel(0.f, 0.f, f, cut);
		float off = toneLevel(1.f, 0.f, f, cut);
		float on = toneLevel(1.f, 1.f, f, cut);
		INFO("  %g Hz: res0 %.3f, res1 %.3f (%.1f dB), res1+comp %.3f (%.1f dB)\n", f, ref, off, dB(off / ref), on, dB(on / ref));
		CHECK(std::fabs(dB(on / ref)) <= 2.f);
	}
	float pOff = toneLevel(1.f, 0.f, 1000.f, cut), pOn = toneLevel(1.f, 1.f, 1000.f, cut);
	INFO("  peak change %.2f dB\n", dB(pOn / pOff));
	CHECK(std::fabs(dB(pOn / pOff)) < 1.5f);
	// BASS COMP = 0 is the stock path exactly
	Rig a, b; b.p.bassComp = 0.f; a.in.gate = b.in.gate = true;
	float diff = 0;
	for (int i = 0; i < 4800; i++) diff = std::max(diff, std::fabs(a.step().out - b.step().out));
	CHECK(diff == 0.f);
}

TEST("SBV output level near 5 V on a default saw") {
	Rig r; r.in.gate = true;
	float pk = 0;
	for (int i = 0; i < 9600; i++) pk = std::max(pk, std::fabs(r.step().out));
	INFO("  default saw peak %.2f V\n", pk);
	CHECK(pk > 3.5f && pk < 6.5f);
}

TEST("SBV silence with no gate") {
	Rig r; float pk = 0;
	for (int i = 0; i < 48000; i++) pk = std::max(pk, std::fabs(r.step().out));
	CHECK(pk == 0.f);
}

TEST("SBV no retrigger while GATE stays high") {
	Rig r; r.in.gate = true; r.run(0.3f);
	float m = r.v.meg.v;
	r.in.voct = 0.5f; r.step();
	CHECK(r.v.meg.v <= m);
}
