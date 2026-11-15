// General tests (spec §9): stability, sample-rate independence, aliasing, end-to-end render.
#include "check.hpp"
#include "../src/dsp/Voice303.hpp"
#include "../src/seq/SeqEngine.hpp"
#include <random>
#include <vector>
#include <algorithm>
#include <complex>
#include <sys/stat.h>

using namespace sb;

TEST("Stability: noise into every SBV input at four sample rates") {
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> U(-10.f, 10.f), K(0.f, 1.f);
	for (float sr : {44100.f, 48000.f, 96000.f, 192000.f}) {
		for (int trial = 0; trial < 3; trial++) {
			Voice303 v; v.setRate(sr);
			VoiceParams p; p.tuning = U(rng) * 1.2f; p.cutoff = K(rng); p.resonance = trial == 0 ? 1.f : K(rng);
			p.envMod = K(rng); p.decay = K(rng); p.accent = K(rng); p.square = trial % 2; p.bassComp = K(rng); p.keyTrack = K(rng);
			VoiceExt x; x.filterFm = trial == 2 ? 1.f : 0.f; x.overdriveDb = trial == 2 ? 24.f : 0.f; x.muffler = trial;
			VoiceIn in; in.audioPatched = trial == 1;
			bool ok = true;
			int n = (int)(sr * (sr > 100000.f ? 2.f : 10.f));
			for (int i = 0; i < n; i++) {
				in.voct = U(rng); in.gate = U(rng) > 0.f; in.accent = U(rng) > 3.f; in.slide = U(rng) > 0.f;
				in.cutoffCv = U(rng); in.audioIn = U(rng);
				VoiceOut o = v.process(in, p, x);
				if (!std::isfinite(o.out) || std::fabs(o.out) > 10.f || o.env < 0.f || o.env > 10.f) { ok = false; break; }
			}
			CHECK(ok);
		}
	}
}

TEST("Stability: SBS outputs are 0 or 10 V-shaped and finite under random clocks and edits") {
	SeqEngine e;
	std::mt19937 rng(3);
	e.setRunning(true);
	bool ok = true;
	for (int i = 0; i < 48000 * 10; i++) {
		if (rng() % 2000 == 0) e.scramble();
		if (rng() % 3000 == 0) e.ctl.pattern = rng() % 64;
		if (rng() % 5000 == 0) e.ctl.mode = (Mode)(rng() % 5);
		if (rng() % 4000 == 0) e.next();
		if (rng() % 7000 == 0) e.reset();
		e.ctl.mutate = (rng() % 100) / 100.f;
		SeqOutputs o = e.process(1.f / 48000.f);
		if (!std::isfinite(o.voct) || std::fabs(o.voct) > 4.f) { ok = false; break; }
	}
	CHECK(ok);
}

TEST("Sample-rate independence: pitch, cutoff, envelope, slide and step timing") {
	auto measure = [](float sr, float* pitch, float* fc, float* megT, float* slide, float* step) {
		Voice303 v; v.setRate(sr);
		VoiceParams p; VoiceExt x; VoiceIn in; in.gate = true;
		int wraps = 0; float prev = 0.f;
		for (int i = 0; i < (int)(sr * 1.f); i++) { v.process(in, p, x); if (v.osc.phase < prev) wraps++; prev = v.osc.phase; }
		*pitch = (float)wraps;
		Voice303 w; w.setRate(sr);
		VoiceIn g;
		w.process(g, p, x);
		*fc = w.lastFcHz;   // resting cutoff, gate low
		g.gate = true;
		float t = 0; int i = 0;
		for (; i < (int)(sr * 3); i++) { w.process(g, p, x); if (w.meg.v <= 0.01f && !w.meg.attacking) break; }
		*megT = i / sr; (void)t;
		Voice303 s; s.setRate(sr);
		VoiceIn si; si.gate = true;
		for (int k = 0; k < 100; k++) s.process(si, p, x);
		si.slide = true; si.voct = 1.f;
		for (int k = 0; k < (int)(0.06f * sr); k++) s.process(si, p, x);
		*slide = s.pitchV;
		SeqEngine e; e.setRunning(true);
		std::vector<float> clk; bool pc = false;
		for (int k = 0; k < (int)(sr * 0.6f); k++) { SeqOutputs o = e.process(1.f / sr); if (o.clock && !pc) clk.push_back(k / sr); pc = o.clock; }
		*step = clk.size() > 3 ? clk[3] - clk[2] : 0.f;
	};
	float a[5], b[5];
	measure(48000.f, &a[0], &a[1], &a[2], &a[3], &a[4]);
	for (float sr : {44100.f, 96000.f, 192000.f}) {
		measure(sr, &b[0], &b[1], &b[2], &b[3], &b[4]);
		for (int i = 0; i < 5; i++) {
			float tol = (i == 0) ? 1.5f / a[0] : (i == 2 ? 0.01f : 0.005f);
			INFO("  %g Hz: quantity %d = %g vs %g\n", sr, i, b[i], a[i]);
			CHECK(std::fabs(b[i] / a[i] - 1.f) <= tol);
		}
	}
}

// Aliasing check: DFT magnitude at non-harmonic bins below 10 kHz, relative to the fundamental.
static float worstAlias(bool square) {
	float sr = 48000.f, f = 2000.f;
	Osc303 o; o.setRate(sr * 4);
	OsKernel k; k.design(4);
	Decimator d;
	int n = 48000;
	std::vector<float> y(n);
	for (int i = 0; i < n; i++) {
		float buf[4];
		for (int j = 0; j < 4; j++) buf[j] = o.process(f, square);
		y[i] = d.process(k, buf);
	}
	// 1 Hz bins over the last 24000 samples with a Hann window, scan 0..10 kHz
	int m = 24000;
	auto mag = [&](float hz) {
		std::complex<double> acc = 0;
		for (int i = 0; i < m; i++) {
			double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (m - 1));
			acc += (double)y[n - m + i] * w * std::polar(1.0, -2 * M_PI * hz * i / sr);
		}
		return std::abs(acc);
	};
	double fund = mag(f);
	double worst = 0;
	for (float hz = 20.f; hz < 10000.f; hz += 10.f) {
		float r = std::fmod(hz, f);
		if (r < 40.f || r > f - 40.f) continue;   // skip harmonics
		worst = std::max(worst, mag(hz));
	}
	return 20.f * std::log10((float)(worst / fund));
}

TEST("Aliasing: 2 kHz saw and square below -60 dB in 0 to 10 kHz") {
	float s = worstAlias(false), q = worstAlias(true);
	INFO("  saw %.1f dB, square %.1f dB\n", s, q);
	CHECK(s < -60.f);
	CHECK(q < -60.f);
}

static void writeWav(const char* path, const std::vector<float>& x, int sr) {
	FILE* f = std::fopen(path, "wb");
	if (!f) return;
	auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	uint32_t bytes = (uint32_t)x.size() * 2;
	std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	w32(16); w16(1); w16(1); w32(sr); w32(sr * 2); w16(2); w16(16);
	std::fwrite("data", 1, 4, f); w32(bytes);
	for (float v : x) { int s = (int)std::lround(std::fmax(-1.f, std::fmin(1.f, v / 10.f)) * 32767.f); w16((uint16_t)(int16_t)s); }
	std::fclose(f);
}

TEST("End to end: SBS demo pattern drives SBV (renders tests/out/demo.wav with --render)") {
	float sr = 48000.f;
	SeqEngine e; e.setRunning(true);
	Voice303 v; v.setRate(sr);
	VoiceParams p; p.resonance = 0.75f; p.envMod = 0.6f; p.decay = 0.3f; p.accent = 0.8f; p.cutoff = 0.35f;
	VoiceExt x;
	std::vector<float> out;
	float pk = 0;
	for (int i = 0; i < (int)(sr * 10); i++) {
		if (i == (int)(sr * 5)) { p.square = true; p.resonance = 0.95f; p.bassComp = 1.f; }
		SeqOutputs o = e.process(1.f / sr);
		VoiceIn in; in.voct = o.voct; in.gate = o.gate; in.accent = o.accent; in.slide = o.slide;
		float y = v.process(in, p, x).out;
		out.push_back(y);
		pk = std::max(pk, std::fabs(y));
		if (i == (int)(sr * 5) - 1) INFO("  first half peak %.2f V\n", pk);
	}
	INFO("  render peak %.2f V\n", pk);
	CHECK(pk > 1.f && pk <= 10.f);
	if (g_render) {
		mkdir("tests/out", 0755);
		writeWav("tests/out/demo.wav", out, (int)sr);
		std::printf("  wrote tests/out/demo.wav\n");
	}
}
