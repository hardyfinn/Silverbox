// Round 2 demo render (spec §7.7): the demo pattern through SBO + SBF with a fold sweep, then
// band-pass, then retuned through SBT in Just intonation. Writes tests/out/demo2.wav with --render.
#include "check.hpp"
#include "../src/dsp/OscX.hpp"
#include "../src/dsp/VoiceBackX.hpp"
#include "../src/seq/SeqEngine.hpp"
#include "../src/tuning/Tuner.hpp"
#include <sys/stat.h>
#include <vector>

using namespace sb;

static void writeWav2(const char* path, const std::vector<float>& x, int sr) {
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

TEST("Demo 2: SBS -> SBO + SBF (fold sweep, band-pass, Just intonation through SBT)") {
	float sr = 48000.f;
	SeqEngine e; e.setRunning(true);
	OscX osc; osc.setRate(sr); osc.seed(7);
	VoiceBackX f; f.setRate(sr);
	TunerCore tuner; Scale ji; builtinScale(1, ji);
	OscXParams op; op.symmetry = 0.3f;
	VbParams fp; fp.resonance = 0.8f; fp.envMod = 0.6f; fp.decay = 0.3f; fp.accent = 0.8f; fp.cutoff = 0.55f;
	std::vector<float> out;
	float pk = 0.f, fenv = 0.f;
	int n = (int)(sr * 12);
	for (int i = 0; i < n; i++) {
		float t = i / sr;
		SeqOutputs o = e.process(1.f / sr);
		float pitch = o.voct;
		if (t >= 8.f) { float in = o.voct, outv; tuner.process(ji, 0, 0.f, &in, &outv, 1); pitch = outv; }
		op.fold = t < 4.f ? 0.15f + 0.2f * (0.5f - 0.5f * std::cos(2.f * PI_F * t / 4.f)) : 0.1f;
		op.foldCv = 0.6f;
		fp.mode = (t >= 4.f && t < 8.f) ? MODE_BP : MODE_LP;
		OscXIn oi; oi.voct = pitch; oi.slide = o.slide; oi.fold = fenv / 2.f;   // FILTER ENV into FOLD
		float a = osc.process(oi, op, OX_OUT).out;
		VbIn fi; fi.in = a; fi.gate = o.gate; fi.accent = o.accent ? 10.f : 0.f; fi.voct = pitch;
		VbOut r = f.process(fi, fp, false);
		fenv = r.filterEnv;
		out.push_back(r.out);
		pk = std::max(pk, std::fabs(r.out));
	}
	INFO("  demo 2 peak %.2f V\n", pk);
	CHECK(pk > 1.f && pk <= 10.f);
	if (g_render) {
		mkdir("tests/out", 0755);
		writeWav2("tests/out/demo2.wav", out, (int)sr);
		std::printf("  wrote tests/out/demo2.wav\n");
	}
}
