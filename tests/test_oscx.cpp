// Tests for SBO, Silverbox Oscillator (round 2 spec §2.7).
#include "check.hpp"
#include "../src/dsp/Osc303.hpp"
#include "../src/dsp/OscX.hpp"
#include <algorithm>
#include <complex>
#include <random>
#include <vector>

using namespace sb;

TEST("SBO: at defaults SAW and SQUARE equal the round 1 oscillator sample for sample") {
	for (float f : {41.2f, 110.f, 523.f, 2100.f}) {
		Osc303 a, b; OscCoreX c; SawCurve sc;
		a.setRate(192000.f); b.setRate(192000.f); c.setRate(192000.f);
		bool same = true;
		for (int i = 0; i < 192000; i++) {
			float s = a.process(f, false), q = b.process(f, true);
			c.process(f, sc, pulseThreshold(sc, SBO_PW_STOCK), SQ_GAIN);
			if (s != c.saw || q != c.square) { same = false; break; }
		}
		CHECK(same);
	}
}

static float dutyAt(float pw, float pwm = 0.f, float pwmIn = 0.f, float curveK = K_SAW) {
	OscX o; o.setRate(48000.f);
	OscXParams p; p.pulseWidth = pw; p.pwm = pwm; p.sawCurve = curveK;
	OscXIn in; in.voct = 1.f; in.pwm = pwmIn;
	int hi = 0, n = 0;
	for (int i = 0; i < 48000; i++) { o.process(in, p, 0); if (i > 4800) { n++; if (o.core.lastShaped > 0.f) hi++; } }
	return (float)hi / n;
}

TEST("SBO: pulse width and PWM") {
	for (float pw : {0.10f, 0.465f, 0.90f}) {
		float d = dutyAt(pw);
		INFO("  PULSE WIDTH %.3f -> duty %.4f\n", pw, d);
		CHECK_NEAR(d, pw, 0.01f);
	}
	CHECK_NEAR(dutyAt(0.3f, 0.f, 0.f, 2.5f), 0.3f, 0.01f);
	float up = dutyAt(0.5f, 0.5f, 2.f), dn = dutyAt(0.5f, 0.5f, -2.f);
	INFO("  PWM +2 V %.3f, -2 V %.3f\n", up, dn);
	CHECK_NEAR(up, 0.5f + 0.5f * 2.f / 5.f * 0.45f, 0.01f);
	CHECK_NEAR(dn, 0.5f - 0.5f * 2.f / 5.f * 0.45f, 0.01f);
	float hi = dutyAt(0.9f, 1.f, 10.f), lo = dutyAt(0.1f, 1.f, -10.f);
	INFO("  clamped %.3f %.3f\n", hi, lo);
	CHECK(hi <= 0.955f && lo >= 0.045f);
}

TEST("SBO: WIDTH, TUNE and SAW CURVE") {
	OscX o; OscXParams p; OscXIn in;
	auto oct = [&](float v) { in.voct = v; for (int i = 0; i < 10; i++) o.process(in, p, 0); return std::log2(o.lastFreq / BASE_FREQ); };
	p.width = 1.05f;
	CHECK_NEAR(oct(2.f) - oct(0.f), 2.1f, 1e-4f);
	p.width = 1.f; p.tune = 100.f;
	CHECK_NEAR(oct(0.f), 1.f / 12.f, 1e-5f);
	p.tune = -100.f;
	CHECK_NEAR(oct(0.f), -1.f / 12.f, 1e-5f);
	SawCurve sc; sc.set(0.f);
	float worst = 0.f;
	for (float ph = 0.f; ph < 1.f; ph += 0.001f) worst = std::max(worst, std::fabs(sc.shape(ph) - (1.f - 2.f * ph)));
	CHECK(worst < 1e-3f);
	// running: away from the wrap, samples follow the straight ramp
	OscCoreX c; c.setRate(192000.f);
	float w2 = 0.f;
	for (int i = 0; i < 19200; i++) {
		c.process(100.f, sc, 0.f, SQ_GAIN);
		float ph = c.phase - 100.f / 192000.f;   // one sample of BLEP latency
		if (ph > 0.01f && ph < 0.99f) w2 = std::max(w2, std::fabs(c.saw - (1.f - 2.f * ph)));
	}
	CHECK(w2 < 1e-3f);
}

TEST("SBO: SUB is exactly half the frequency and phase-locked over 10 s") {
	OscCoreX c; c.setRate(192000.f); SawCurve sc;
	int wraps = 0, subRises = 0, offLock = 0;
	bool prevSub = c.subHigh; float prevPh = 0.f;
	for (int i = 0; i < 192000 * 10; i++) {
		c.process(97.3f, sc, SQ_OFFSET, SQ_GAIN);
		bool wrapped = c.phase < prevPh;
		if (wrapped) wraps++;
		if (c.subHigh != prevSub) { if (!wrapped) offLock++; if (c.subHigh) subRises++; }
		prevSub = c.subHigh; prevPh = c.phase;
	}
	INFO("  wraps %d, sub cycles %d\n", wraps, subRises);
	CHECK(std::abs(wraps - 2 * subRises) <= 1);
	CHECK(offLock == 0);
}

TEST("SBO: SYNC to a 110 Hz master forces its period on a 300 Hz slave") {
	float sr = 44000.f;   // 400 samples per master cycle
	OscX o; o.setRate(sr);
	OscXParams p; OscXIn in;
	in.voct = std::log2(300.f / BASE_FREQ); in.syncPatched = true;
	std::vector<float> y;
	for (int i = 0; i < (int)sr; i++) {
		in.sync = 5.f * std::sin(2.f * PI_F * 110.f * i / sr + 0.3f);
		y.push_back(o.process(in, p).saw);
	}
	float worst = 0.f;
	for (int i = (int)sr / 2; i + 400 < (int)sr; i++) worst = std::max(worst, std::fabs(y[i + 400] - y[i]));
	INFO("  worst difference one master period apart: %.4f V\n", worst);
	CHECK(worst < 0.05f);
	// without sync it does not repeat at 400 samples
	OscX q; q.setRate(sr); in.syncPatched = false;
	std::vector<float> z;
	for (int i = 0; i < (int)sr; i++) z.push_back(q.process(in, p).saw);
	float w2 = 0.f;
	for (int i = (int)sr / 2; i + 400 < (int)sr; i++) w2 = std::max(w2, std::fabs(z[i + 400] - z[i]));
	CHECK(w2 > 1.f);
}

TEST("SBO: linear FM at full depth swings between 0 and 2f") {
	OscX o; OscXParams p; p.fm = 1.f; OscXIn in; in.voct = 1.f;
	float lo = 1e9f, hi = 0.f;
	for (int i = 0; i < 48000; i++) {
		in.fm = 5.f * std::sin(2.f * PI_F * 5.f * i / 48000.f);
		o.process(in, p, OX_OUT);
		lo = std::min(lo, o.lastF); hi = std::max(hi, o.lastF);
	}
	float f = BASE_FREQ * 2.f;
	INFO("  FM range %.2f .. %.2f Hz (f = %.2f)\n", lo, hi, f);
	CHECK(lo < 0.01f * f);
	CHECK_NEAR(hi, 2.f * f, 0.01f * f);
}

static int extremaPerCycle(float fold, float sym) {
	Folder fl; fl.setRate(192000.f);
	int n = 0; float a = 0, b = 0;
	for (int i = 0; i < 1000; i++) {
		float x = std::sin(2.f * PI_F * i / 1000.f);
		float y = fl.process(x, fold, sym);
		if (i >= 2 && ((b > a && b > y) || (b < a && b < y))) n++;
		a = b; b = y;
	}
	return n;
}

TEST("SBO folder: identity at FOLD 0, bounded, more folds with FOLD, SYMMETRY adds even harmonics") {
	Folder f; f.setRate(192000.f);
	bool ident = true, bounded = true;
	for (float x = -1.5f; x <= 1.5f; x += 0.0007f) if (f.process(x, 0.f, 0.7f) != x) ident = false;
	for (float x = -1.5f; x <= 1.5f; x += 0.0007f) { float y = f.process(x, 1.f, 0.f); if (std::fabs(y) > 1.f) bounded = false; }
	CHECK(ident);
	CHECK(bounded);
	// at the module level OUT equals SAW at FOLD 0 with WAVE MIX at saw
	OscX o; OscXParams p; OscXIn in; in.voct = 0.5f;
	bool same = true;
	for (int i = 0; i < 48000; i++) { OscXOut r = o.process(in, p); if (r.out != r.saw) same = false; }
	CHECK(same);
	int e0 = extremaPerCycle(0.001f, 0.f), e3 = extremaPerCycle(0.4f, 0.f), e1 = extremaPerCycle(1.f, 0.f);
	INFO("  extrema per cycle: %d, %d, %d\n", e0, e3, e1);
	CHECK(e0 < e3 && e3 < e1);
	auto h2 = [](float sym) {
		Folder fl; fl.setRate(48000.f);
		std::complex<double> a1 = 0, a2 = 0;
		int n = 48000;
		for (int i = 0; i < n; i++) {
			double ph = 2 * M_PI * 100.0 * i / 48000.0;
			float y = fl.process(0.9f * (float)std::sin(ph), 0.5f, sym);
			if (i >= n / 2) { a1 += (double)y * std::polar(1.0, -ph); a2 += (double)y * std::polar(1.0, -2 * ph); }
		}
		return 20.0 * std::log10(std::abs(a2) / std::abs(a1) + 1e-12);
	};
	double s0 = h2(0.f), s5 = h2(0.5f);
	INFO("  2nd harmonic: symmetric %.1f dB, SYMMETRY 0.5 %.1f dB\n", s0, s5);
	CHECK(s5 - s0 > 20.0);
}

TEST("SBO folder aliasing: 500 Hz saw at FOLD 1 below -50 dB in 0 to 10 kHz") {
	float sr = 48000.f, f = 500.f;
	OscX o; o.setRate(sr);
	OscXParams p; p.fold = 1.f; OscXIn in; in.voct = std::log2(f / BASE_FREQ);
	int n = 48000, m = 24000;
	std::vector<float> y(n);
	for (int i = 0; i < n; i++) y[i] = o.process(in, p, OX_OUT).out;
	auto mag = [&](float hz) {
		std::complex<double> acc = 0;
		for (int i = 0; i < m; i++) {
			double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (m - 1));
			acc += (double)y[n - m + i] * w * std::polar(1.0, -2 * M_PI * hz * i / sr);
		}
		return std::abs(acc);
	};
	double ref = 0;
	for (int k = 1; k <= 20; k++) ref = std::max(ref, mag(k * f));
	double worst = 0;
	for (float hz = 20.f; hz < 10000.f; hz += 5.f) {
		float r = std::fmod(hz, f);
		if (r < 20.f || r > f - 20.f) continue;
		worst = std::max(worst, mag(hz));
	}
	float db = 20.f * std::log10((float)(worst / ref));
	INFO("  worst alias %.1f dB below the strongest harmonic\n", db);
	CHECK(db < -50.f);
}

TEST("SBO: slide as SBV, drift bounded") {
	OscX o; OscXParams p; OscXIn in;
	for (int i = 0; i < 100; i++) o.process(in, p, 0);
	in.slide = true; in.voct = 1.f;
	for (int i = 0; i < (int)(0.06f * 48000); i++) o.process(in, p, 0);
	INFO("  glide after 60 ms: %.3f\n", o.pitchV);
	CHECK(o.pitchV > 0.80f && o.pitchV < 0.85f);
	in.slide = false; in.voct = 2.f;
	o.process(in, p, 0);
	CHECK(o.pitchV == 2.f);
	// DRIFT 0: constant; DRIFT 1: within +-25 cents
	OscX d; d.seed(1234); OscXParams dp; OscXIn di;
	float f0 = 0.f; bool constant = true;
	for (int i = 0; i < 48000; i++) { d.process(di, dp, 0); if (i == 0) f0 = d.lastFreq; else if (d.lastFreq != f0) constant = false; }
	CHECK(constant);
	dp.drift = 1.f;
	float lo = 0.f, hi = 0.f;
	for (int i = 0; i < 48000 * 30; i++) {
		d.process(di, dp, 0);
		float c = 1200.f * std::log2(d.lastFreq / f0);
		lo = std::min(lo, c); hi = std::max(hi, c);
	}
	INFO("  drift range %.1f .. %.1f cents\n", lo, hi);
	CHECK(lo >= -25.f && hi <= 25.f);
	CHECK(hi - lo > 5.f);
}

TEST("SBO: stability under noise at four sample rates; pitch independent of rate") {
	std::mt19937 rng(9);
	std::uniform_real_distribution<float> U(-10.f, 10.f), K(0.f, 1.f);
	for (float sr : {44100.f, 48000.f, 96000.f, 192000.f}) {
		OscX o; o.setRate(sr);
		OscXParams p; p.pwm = 1.f; p.fold = K(rng); p.foldCv = 1.f; p.symmetry = 2.f * K(rng) - 1.f; p.fm = 1.f;
		p.drift = 1.f; p.sub = 1.f; p.sawCurve = 3.f * K(rng); p.squareShape = 20.f; p.waveMix = 0.5f;
		OscXIn in; in.syncPatched = true;
		bool ok = true;
		for (int i = 0; i < (int)sr * 2; i++) {
			in.voct = U(rng); in.pwm = U(rng); in.mixCv = U(rng); in.fold = U(rng); in.fm = U(rng); in.sync = U(rng);
			in.slide = U(rng) > 0.f;
			OscXOut r = o.process(in, p);
			for (float v : {r.saw, r.square, r.sub, r.out}) if (!std::isfinite(v) || std::fabs(v) > 12.f) ok = false;   // OUT is clamped to +-12 V
			if (!ok) { INFO("  %g Hz: saw %g square %g sub %g out %g\n", sr, r.saw, r.square, r.sub, r.out); break; }
		}
		CHECK(ok);
		// pitch: wraps in one second at 0 V
		OscX q; q.setRate(sr); OscXParams qp; OscXIn qi;
		int wraps = 0; float prev = 0.f;
		for (int i = 0; i < (int)sr; i++) { q.process(qi, qp, 0); if (q.core.phase < prev) wraps++; prev = q.core.phase; }
		CHECK(std::abs(wraps - 65) <= 1);
	}
}
