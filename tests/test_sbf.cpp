// Tests for SBF, Silverbox Filter (round 2 spec §3.5, §3.6, §3.8).
#include "check.hpp"
#include "../src/dsp/VoiceBackX.hpp"
#include "../src/dsp/Voice303.hpp"
#include "../src/seq/SeqEngine.hpp"
#include <algorithm>
#include <random>
#include <vector>

using namespace sb;

// An Osc303 run exactly as inside Voice303 (same slide, same pitch, oversampled), feeding SBF.
struct OscFeed {
	Osc303 osc; SlideLag slide; bool prevGate = false; float fs, dt; int factor;
	explicit OscFeed(float sr) : fs(sr), dt(1.f / sr), factor(oversampleFactor(sr)) { osc.setRate(sr * factor); }
	void run(float voct, bool gate, bool sl, bool square, float* out) {
		bool rise = gate && !prevGate; prevGate = gate;
		float pv = slide.process(dt, voct, sl, rise, 0.060f / 1.715f);
		float freq = BASE_FREQ * std::exp2(clampf(pv, -6.f, 7.f));
		for (int i = 0; i < factor; i++) out[i] = osc.process(freq, square);
	}
};

TEST("SBF equivalence: Osc303 into SBF at stock equals SBV on the demo pattern within -60 dB") {
	for (float sr : {48000.f, 96000.f}) {
		SeqEngine e; e.setRunning(true);
		Voice303 v; v.setRate(sr);
		VoiceBackX b; b.setRate(sr);
		OscFeed feed(sr);
		VoiceParams p; p.resonance = 0.75f; p.envMod = 0.6f; p.decay = 0.3f; p.accent = 0.8f; p.cutoff = 0.35f;
		VoiceExt x;
		VbParams q; q.resonance = p.resonance; q.envMod = p.envMod; q.decay = p.decay; q.accent = p.accent; q.cutoff = p.cutoff;
		double err = 0, sig = 0;
		for (int i = 0; i < (int)(sr * 6); i++) {
			if (i == (int)(sr * 3)) { p.square = true; p.resonance = q.resonance = 0.95f; p.bassComp = q.bassComp = 1.f; }
			SeqOutputs o = e.process(1.f / sr);
			VoiceIn in; in.voct = o.voct; in.gate = o.gate; in.accent = o.accent; in.slide = o.slide;
			float a = v.process(in, p, x).out;
			float os[OS_MAX];
			feed.run(o.voct, o.gate, o.slide, p.square, os);
			VbIn bi; bi.gate = o.gate; bi.accent = o.accent ? 10.f : 0.f; bi.voct = o.voct;
			float c = b.process(bi, q, false, os).out;
			err += (double)(a - c) * (a - c); sig += (double)a * a;
		}
		double db = 10 * std::log10(err / sig + 1e-30);
		INFO("  %g Hz: RMS error %.1f dB\n", sr, db);
		CHECK(db < -60.0);
	}
}

// Rig for SBF fed by a stock saw at the oversampled rate.
struct FRig {
	VoiceBackX b; VbParams p; VbIn in; OscFeed feed; float fs; bool square = false;
	explicit FRig(float sr = 48000.f) : feed(sr), fs(sr) { b.setRate(sr); }
	VbOut step() { float os[OS_MAX]; feed.run(in.voct, in.gate, false, square, os); return b.process(in, p, true, os); }
	void run(float sec) { for (int i = 0; i < (int)(sec * fs); i++) step(); }
};

TEST("SBF calibration: resonant peak 400 to 600 Hz at CUT OFF FREQ half way") {
	FRig r;
	r.p.cutoff = 0.5f; r.p.resonance = 1.f; r.p.envMod = 0.f;
	r.in.gate = true;
	r.run(0.2f);
	int n = 48000;
	std::vector<float> y(n);
	for (int i = 0; i < n; i++) y[i] = r.step().out;
	auto mag = [&](double hz) {
		double re = 0, im = 0;
		for (int i = 0; i < n; i++) {
			double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1));
			re += y[i] * w * std::cos(2 * M_PI * hz * i / 48000.0);
			im -= y[i] * w * std::sin(2 * M_PI * hz * i / 48000.0);
		}
		return std::sqrt(re * re + im * im);
	};
	std::vector<double> h(31);
	int best = 1;
	for (int k = 1; k <= 30; k++) { h[k] = mag(k * BASE_FREQ) * k; if (h[k] > h[best]) best = k; }
	double a = std::log(h[best - 1]), bb = std::log(h[best]), c = std::log(h[best + 1]);
	double peak = (best + 0.5 * (a - c) / (a - 2 * bb + c)) * BASE_FREQ;
	INFO("  resonant peak %.0f Hz\n", peak);
	CHECK(peak >= 400.0 && peak <= 600.0);
}

TEST("SBF GATE TRIM: 50 ms closes the VCA with the gate still high; OFF follows the gate") {
	FRig r; r.p.gateTrim = 0.05f; r.in.gate = true;
	r.run(0.051f);
	CHECK(!r.b.veg.gateOn);
	r.run(0.009f);
	INFO("  VCA at 60 ms: %.3f\n", r.b.veg.v);
	CHECK(r.b.veg.v < 0.1f);
	CHECK(r.b.meg.v > 0.f);   // MEG unaffected
	FRig s; s.in.gate = true; s.run(0.5f);
	CHECK(s.b.veg.gateOn && s.b.veg.v > 0.3f);
	s.in.gate = false; s.run(0.02f);
	CHECK(s.b.veg.v < 0.01f);
}

TEST("SBF ACCENT is a level: 2.5 V gives half of 5 V; 10 V equals 5 V") {
	auto measure = [](float volts, float* boost, float* sweepOct) {
		FRig r; r.p.envMod = 0.f; r.p.accent = 1.f;
		float base = std::log2(CUT_LO_X) + 0.5f * std::log2(CUT_HI_X / CUT_LO_X);
		r.in.gate = true; r.in.accent = volts;
		*boost = 0.f; *sweepOct = 0.f;
		for (int i = 0; i < 48000 / 2; i++) {
			r.step();
			*boost = std::max(*boost, r.b.accGain - 1.f);
			*sweepOct = std::max(*sweepOct, std::log2(r.b.lastFcHz) - base);
		}
	};
	float b5, s5, b25, s25, b10, s10;
	measure(5.f, &b5, &s5); measure(2.5f, &b25, &s25); measure(10.f, &b10, &s10);
	INFO("  boost %.3f %.3f %.3f, sweep %.3f %.3f %.3f oct\n", b25, b5, b10, s25, s5, s10);
	CHECK_NEAR(b25 / b5, 0.5f, 0.02f);
	CHECK_NEAR(s25 / s5, 0.5f, 0.02f);
	CHECK(b10 == b5 && s10 == s5);
}

TEST("SBF TRIG restarts the envelopes with GATE high; TRIG alone plays notes; ENV IN replaces the MEG") {
	FRig r; r.in.gate = true; r.in.trigPatched = true;
	r.run(1.0f);
	float before = r.b.meg.v;
	r.in.trig = 10.f; r.run(0.004f); r.in.trig = 0.f; r.run(0.004f);
	INFO("  MEG before %.3f after trigger %.3f\n", before, r.b.meg.v);
	CHECK(r.b.meg.v > 0.9f && before < 0.2f);
	FRig t; t.in.gatePatched = false; t.in.trigPatched = true;
	t.run(0.01f);
	CHECK(t.b.veg.v == 0.f);
	t.in.trig = 10.f; t.run(0.002f); t.in.trig = 0.f; t.run(0.05f);
	CHECK(t.b.veg.gateOn && t.b.veg.v > 0.5f);
	t.run(0.1f);
	CHECK(!t.b.veg.gateOn);
	FRig e; e.in.gate = true; e.in.envInPatched = true; e.in.envIn = 5.f; e.p.envMod = 1.f;
	VbOut o;
	for (int i = 0; i < 4800; i++) o = e.step();
	float expect = std::log2(CUT_LO_X) + 0.5f * std::log2(CUT_HI_X / CUT_LO_X) - ENV_SHIFT + ENV_OCT * 0.5f;
	CHECK_NEAR(o.filterEnv, 5.f, 1e-5f);
	CHECK_NEAR(std::log2(e.b.lastFcHz), expect, 1e-3f);
}

TEST("SBF ENV ATTACK, RELEASE, ACCENT VCA, SWEEP FREE and CLICK") {
	{   // ENV ATTACK 100 ms
		FRig r; r.p.envAttack = 0.1f; r.in.gate = true;
		int peak = 0; float pk = 0.f;
		for (int i = 0; i < 48000 / 2; i++) { r.step(); if (r.b.meg.v > pk) { pk = r.b.meg.v; peak = i; } }
		float t = peak / 48000.f;
		INFO("  MEG peak at %.1f ms\n", t * 1000.f);
		CHECK(t >= 0.09f && t <= 0.11f);
	}
	{   // RELEASE 500 ms
		FRig r; r.p.release = 0.5f; r.in.gate = true; r.run(0.1f);
		r.in.gate = false; r.run(0.2f);
		CHECK(r.b.veg.v > 0.1f);
	}
	{   // ACCENT VCA 0: no level boost, same sweep
		FRig a, b; b.p.accentVca = 0.f;
		a.in.gate = b.in.gate = true; a.in.accent = b.in.accent = 10.f;
		float ma = 0.f, mb = 0.f; bool sameSweep = true;
		for (int i = 0; i < 24000; i++) { a.step(); b.step(); ma = std::max(ma, a.b.accGain); mb = std::max(mb, b.b.accGain); if (a.b.sweep.s != b.b.sweep.s) sameSweep = false; }
		CHECK(ma > 1.2f);
		CHECK(mb == 1.f);
		CHECK(sameSweep);
	}
	{   // SWEEP FREE: peak time independent of RESONANCE
		auto peakT = [](float res, bool free) {
			FRig r; r.p.resonance = res; r.p.sweepFree = free; r.p.sweepShape = 0.5f;
			r.in.gate = true; r.in.accent = 10.f;
			int pk = 0; float m = 0.f;
			for (int i = 0; i < 24000; i++) { r.step(); if (r.b.sweep.s > m) { m = r.b.sweep.s; pk = i; } }
			return pk / 48000.f;
		};
		float g0 = peakT(0.f, false), g1 = peakT(1.f, false), f0 = peakT(0.f, true), f1 = peakT(1.f, true);
		INFO("  sweep peak: ganged %.1f / %.1f ms, free %.1f / %.1f ms\n", g0 * 1e3f, g1 * 1e3f, f0 * 1e3f, f1 * 1e3f);
		CHECK(std::fabs(g1 - g0) > 0.005f);
		CHECK(f0 == f1);
	}
	{   // CLICK: 0 adds nothing, 1 adds a 0.3 to 0.6 V transient on a silent input
		VoiceBackX a, b; VbParams pa, pb; pb.click = 1.f; VbIn in; in.gate = true;
		float pk = 0.f; bool zero = true;
		for (int i = 0; i < 4800; i++) {
			float ya = a.process(in, pa).out, yb = b.process(in, pb).out;
			if (ya != 0.f) zero = false;
			pk = std::max(pk, std::fabs(yb));
		}
		INFO("  click peak %.3f V\n", pk);
		CHECK(zero);
		CHECK(pk >= 0.3f && pk <= 0.6f);
	}
}

TEST("SBF CV inputs move their targets; FILTER OUT matches OUT for a held note; mode switch is smooth") {
	auto settle = [](VbIn in, VbParams p) { FRig r; r.in = in; r.p = p; r.in.gate = true; r.run(0.3f); return r; };
	VbParams p; VbIn in;
	FRig base = settle(in, p);
	VbIn c = in; c.cutoffCv = 5.f;
	CHECK(settle(c, p).b.lastFcHz > base.b.lastFcHz * 7.f);
	c = in; c.resCv = 2.f;
	CHECK(settle(c, p).b.lastK > base.b.lastK * 1.2f);
	c = in; c.envModCv = 5.f;
	{ FRig r = settle(c, p); r.run(2.f); FRig q = settle(in, p); q.run(2.f); CHECK(r.b.lastFcHz < q.b.lastFcHz * 0.75f); }
	c = in; c.decayCv = 5.f;
	CHECK(settle(c, p).b.meg.v > base.b.meg.v * 1.5f);
	c = in; c.accent = 10.f; c.accentCv = 5.f;
	{ VbIn d = in; d.accent = 10.f; CHECK(settle(c, p).b.accGain > settle(d, p).b.accGain + 0.1f); }
	{   // FM input changes the sound
		FRig a, b; a.in.gate = b.in.gate = true; b.p.fm = 1.f;
		double diff = 0;
		for (int i = 0; i < 9600; i++) { b.in.fm = 5.f * std::sin(i * 0.05f); float x = a.step().out, y = b.step().out; diff += std::fabs(x - y); }
		CHECK(diff / 9600 > 0.05);
	}
	{   // FILTER OUT level
		FRig r; r.in.gate = true; r.run(0.01f);
		double so = 0, sf = 0;
		for (int i = 0; i < 2400; i++) { VbOut o = r.step(); so += o.out * o.out; sf += o.filterOut * o.filterOut; }
		float db = 10.f * std::log10((float)(sf / so));
		INFO("  FILTER OUT vs OUT on a held note: %.2f dB\n", db);
		CHECK(std::fabs(db) < 1.5f);
	}
	{   // switching LP -> HP: no step bigger than the signals' own movement
		FRig r; r.p.resonance = 0.f; r.in.gate = true; r.p.vcaDecayT = 20.f;
		r.run(0.3f);
		float steady = 0.f, atSwitch = 0.f, prev = r.step().out;
		for (int i = 0; i < 4800; i++) { float y = r.step().out; steady = std::max(steady, std::fabs(y - prev)); prev = y; }
		r.p.mode = MODE_HP;
		for (int i = 0; i < 480; i++) { float y = r.step().out; atSwitch = std::max(atSwitch, std::fabs(y - prev)); prev = y; }
		for (int i = 0; i < 4800; i++) { float y = r.step().out; steady = std::max(steady, std::fabs(y - prev)); prev = y; }
		INFO("  largest step during switch %.3f V, steady LP/HP %.3f V\n", atSwitch, steady);
		CHECK(atSwitch <= steady * 1.2f);
		// a hard switch (no crossfade) would jump by about the difference of the two signals
		CHECK(r.b.xfade == 1.f);
	}
}

TEST("SBF stability: noise on every input at four sample rates, RES TRIM 1.1, every mode") {
	std::mt19937 rng(21);
	std::uniform_real_distribution<float> U(-10.f, 10.f), K(0.f, 1.f);
	for (float sr : {44100.f, 48000.f, 96000.f, 192000.f}) {
		for (int mode = 0; mode < 3; mode++) {
			VoiceBackX b; b.setRate(sr);
			VbParams p; p.mode = mode; p.resTrim = 1.1f; p.driveDb = 24.f; p.fm = 1.f; p.keyTrack = 1.f; p.bassComp = 1.f;
			p.poles = 1.f + 2.f * K(rng); p.bass = 20.f + 380.f * K(rng); p.click = 1.f; p.gateTrim = 0.05f; p.cutoffTrim = 2.f;
			VbIn in; in.trigPatched = true; in.envInPatched = mode == 1;
			bool ok = true;
			for (int i = 0; i < (int)sr; i++) {
				in.in = U(rng); in.gate = U(rng) > 0.f; in.trig = U(rng); in.accent = U(rng); in.voct = U(rng);
				in.cutoffCv = U(rng); in.resCv = U(rng); in.envModCv = U(rng); in.decayCv = U(rng); in.accentCv = U(rng);
				in.fm = U(rng); in.envIn = U(rng);
				VbOut o = b.process(in, p);
				for (float v : {o.out, o.filterOut}) if (!std::isfinite(v) || std::fabs(v) > 10.f) ok = false;
				for (float v : {o.filterEnv, o.sweep, o.vcaEnv}) if (!(v >= 0.f && v <= 10.f)) ok = false;
				if (!ok) break;
			}
			CHECK(ok);
		}
	}
}
