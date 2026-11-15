// Tests for the variable diode ladder used by SBF (round 2 spec §3.2, §3.4, §3.8).
#include "check.hpp"
#include "../src/dsp/DiodeLadder303.hpp"
#include "../src/dsp/DiodeLadderX.hpp"
#include <algorithm>
#include <random>

using namespace sb;

static float dBx(float r) { return 20.f * std::log10(std::max(r, 1e-12f)); }

// Steady-state gain of one tap at frequency f.
static float tapGain(float f, float fc, float res, int mode, float poles = 1.8f, float bass = 150.f,
                     float fs = 192000.f, float amp = 0.02f, float trim = 1.f) {
	DiodeLadderX l; l.setRate(fs); l.setBass(bass);
	LadderCoefs c = LadderCoefs::at(poles);
	float w[5]; tapWeights(mode, c, w);
	float k = std::pow(res, 0.8f) * RES_MAX * trim * c.kcrit(fc, bass);
	float per = std::max(0.15f, 6.f / f);
	int n = (int)(fs * (0.45f + per)), m = (int)(fs * per);
	float pk = 0.f;
	for (int i = 0; i < n; i++) {
		l.process(amp * std::sin(2.f * PI_F * f * i / fs), fc, k, c);
		if (i > n - m) pk = std::max(pk, std::fabs(l.tap(w)));
	}
	return pk / amp;
}

TEST("Ladder X: stock POLES and BASS are sample-identical to the round 1 ladder") {
	std::mt19937 rng(11);
	std::uniform_real_distribution<float> U(-1.f, 1.f);
	DiodeLadder303 a; DiodeLadderX b;
	a.setRate(192000.f); b.setRate(192000.f); b.setBass(150.f);
	LadderCoefs c = LadderCoefs::at(1.8f);
	bool same = true, kSame = true;
	for (int i = 0; i < 192000; i++) {
		float fc = 100.f * std::exp2(6.f * (0.5f + 0.5f * std::sin(i * 1e-4f)));
		float res = 0.5f + 0.5f * U(rng);
		float ka = resonanceK(res, fc);
		float kb = std::pow(clampf(res, 0.f, 1.f), 0.8f) * RES_MAX * c.kcrit(fc, 150.f);
		if (ka != kb) kSame = false;
		float x = 0.5f * U(rng);
		if (a.process(x, fc, ka) != b.process(x, fc, kb, c)) { same = false; break; }
	}
	CHECK(kSame);
	CHECK(same);
}

TEST("Ladder X: tables reproduce round 1 at POLES 1.8 and vary smoothly") {
	LadderCoefs c = LadderCoefs::at(1.8f);
	CHECK_NEAR(c.w180, 1.3775f, 0.01f * 1.3775f);
	CHECK_NEAR(c.kcrit0, 17.03f, 0.01f * 17.03f);
	for (float r : {4.f, 8.f, 16.f, 64.f}) {
		float fit = 1.f + 1.412f / r + 0.576f / (r * r);
		CHECK_NEAR(c.kcrit(r * 150.f, 150.f) / c.kcrit0, fit, 0.01f * fit);
	}
	// between grid points the coefficients are monotone in the w180 table
	float prev = 0.f;
	bool mono = true;
	for (float p = 1.f; p <= 3.f; p += 0.013f) { float w = LadderCoefs::at(p).w180; if (w < prev) mono = false; prev = w; }
	CHECK(mono);
}

TEST("Ladder X: RES TRIM above 1 self-oscillates to a stable sine; nothing blows up") {
	float fs = 192000.f, fc = 1000.f;
	DiodeLadderX l; l.setRate(fs); l.setBass(150.f);
	LadderCoefs c = LadderCoefs::at(1.8f);
	float k = RES_MAX * 1.1f * c.kcrit(fc, 150.f);
	float pkA = 0.f, pkB = 0.f;
	int zc = 0; float prev = 0.f;
	for (int i = 0; i < (int)(fs * 3); i++) {
		float y = l.process(i == 0 ? 1e-4f : 0.f, fc, k, c);
		float t = i / fs;
		if (t > 2.0f && t < 2.5f) pkA = std::max(pkA, std::fabs(y));
		if (t > 2.5f) { pkB = std::max(pkB, std::fabs(y)); if (prev < 0.f && y >= 0.f) zc++; }
		prev = y;
	}
	float freq = zc / 0.5f;
	// Normalised to the ladder's ceiling at the resonant frequency: the input node saturates at
	// SAT_LEVEL and stage 4 is 1/kcrit of it there. (About 1.4 V peak at SBF's OUT.)
	float ceiling = SAT_LEVEL / c.kcrit(fc, 150.f);
	INFO("  self-oscillation amplitude %.3f / %.3f (%.2f normalised), %.0f Hz\n", pkA, pkB, pkB / ceiling, freq);
	CHECK(pkB / ceiling > 0.3f && pkB / ceiling < 2.0f);
	CHECK(std::fabs(pkA / pkB - 1.f) < 0.02f);
	// the feedback high-pass raises the resonant frequency at low r, as in round 1 (peak 0.95 to 1.15 fc)
	CHECK(freq / fc > 0.95f && freq / fc < 1.15f);
	// RES TRIM 1.1, DRIVE 24 dB, noise in, every mode and pole setting
	std::mt19937 rng(5);
	std::uniform_real_distribution<float> U(-1.f, 1.f);
	bool ok = true;
	for (float poles : {1.f, 1.8f, 3.f}) for (float bass : {20.f, 400.f}) for (int mode = 0; mode < 3; mode++) {
		DiodeLadderX m; m.setRate(fs); m.setBass(bass);
		LadderCoefs cc = LadderCoefs::at(poles);
		float w[5]; tapWeights(mode, cc, w);
		for (int i = 0; i < (int)(fs * 0.5f); i++) {
			float f = 30.f * std::exp2(9.f * (0.5f + 0.5f * U(rng)));
			m.process(15.85f * 2.f * U(rng), f, RES_MAX * 1.1f * cc.kcrit(f, bass), cc);
			float y = m.tap(w);
			if (!std::isfinite(y) || std::fabs(y) > 200.f) { ok = false; break; }
		}
	}
	CHECK(ok);
}

TEST("Ladder X: band-pass and high-pass taps") {
	float fc = 1000.f;
	float bpPk = 0.f;
	for (float f = 300.f; f < 3000.f; f *= 1.05f) bpPk = std::max(bpPk, tapGain(f, fc, 0.f, MODE_BP));
	float bp60 = tapGain(60.f, fc, 0.f, MODE_BP), bp16k = tapGain(16000.f, fc, 0.f, MODE_BP);
	INFO("  BP peak %.2f dB, 60 Hz %.1f dB, 16 kHz %.1f dB\n", dBx(bpPk), dBx(bp60 / bpPk), dBx(bp16k / bpPk));
	CHECK(std::fabs(dBx(bpPk)) < 1.f);
	CHECK(dBx(bp60 / bpPk) <= -20.f);
	CHECK(dBx(bp16k / bpPk) <= -20.f);
	float hp60 = tapGain(60.f, fc, 0.f, MODE_HP), hp10k = tapGain(10000.f, fc, 0.f, MODE_HP);
	INFO("  HP 60 Hz %.1f dB, 10 kHz %.2f dB\n", dBx(hp60), dBx(hp10k));
	CHECK(dBx(hp60) <= -40.f);
	CHECK(std::fabs(dBx(hp10k)) <= 1.5f);
	// resonant peak within 10% of fc in every mode
	for (int mode = 0; mode < 3; mode++) {
		float pk = 0.f, pkF = 0.f;
		for (float m = 0.7f; m <= 1.4f; m += 0.01f) { float g = tapGain(fc * m, fc, 1.f, mode); if (g > pk) { pk = g; pkF = m; } }
		INFO("  mode %d resonant peak at %.2f x fc\n", mode, pkF);
		CHECK(pkF > 0.9f && pkF < 1.1f);
	}
}

TEST("Ladder X: BASS 20 Hz keeps resonance at low cutoffs; POLES changes the passband slightly") {
	// resonance gain = height of the resonant peak (searched 0.9 to 1.3 fc, the high-pass shifts it up
	// at low r) over the response at fc without resonance
	auto resGain = [](float fc, float bass) {
		float pk = 0.f;
		for (float m = 0.9f; m <= 1.3f; m += 0.01f) pk = std::max(pk, tapGain(fc * m, fc, 1.f, MODE_LP, 1.8f, bass));
		return pk / tapGain(fc, fc, 0.f, MODE_LP, 1.8f, bass);
	};
	float r80 = resGain(80.f, 20.f), r800 = resGain(800.f, 20.f);
	float s80 = resGain(80.f, 150.f), s800 = resGain(800.f, 150.f);
	INFO("  BASS 20 Hz: 80 Hz %.1f dB, 800 Hz %.1f dB; stock: %.1f, %.1f dB\n", dBx(r80), dBx(r800), dBx(s80), dBx(s800));
	CHECK(std::fabs(dBx(r800) - dBx(r80)) <= 3.f);
	// The spec expected POLES 1.0 to be steeper above fc. In this model the slope does not depend on
	// c1 (23 dB/oct between 2 and 5 octaves for every c1, scripts/ladder_calibration.py), because the
	// ladder is rescaled so the resonance stays on fc. What POLES does change is the passband below
	// fc and the feedback needed to resonate (DECISIONS.md). Test that: fc/4 is lower at POLES 1.0.
	float fc = 500.f;
	float p1 = dBx(tapGain(fc / 4.f, fc, 0.f, MODE_LP, 1.f)), p18 = dBx(tapGain(fc / 4.f, fc, 0.f, MODE_LP, 1.8f));
	INFO("  response at fc/4: POLES 1.0 %.2f dB, 1.8 %.2f dB\n", p1, p18);
	CHECK(p18 - p1 > 0.3f);
}
