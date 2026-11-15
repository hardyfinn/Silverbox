// Golden test (round 2 spec §0.2): locks the behaviour of SBV's voice model and SBS's engine
// as they were after the 2.1.0 cutoff correction, before any round 2 code was added.
//
// Primary check: FNV-1a over the raw float bits of the renders. If a different compiler or
// maths library changes the last bits, the test falls back to stored renderings in tests/golden/
// (voice as 16-bit samples with a 1 LSB tolerance, sequencer as a list of output changes with
// voltages to 1e-4 V) and says so. Run with --write-golden to regenerate the stored files.
#include "check.hpp"
#include "../src/dsp/Voice303.hpp"
#include "../src/seq/SeqEngine.hpp"
#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/stat.h>

using namespace sb;

extern bool g_writeGolden;

static const uint64_t GOLDEN_VOICE_HASH = 0x1f65473b8f12c81aull;
static const uint64_t GOLDEN_SEQ_HASH = 0xfe641791f5f94e9aull;

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
	const uint8_t* b = (const uint8_t*)p;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; }
	return h;
}
static const uint64_t FNV_INIT = 0xcbf29ce484222325ull;

static std::vector<float> renderVoice() {
	float sr = 48000.f;
	SeqEngine e; e.setRunning(true);
	Voice303 v; v.setRate(sr);
	VoiceParams p; p.resonance = 0.75f; p.envMod = 0.6f; p.decay = 0.3f; p.accent = 0.8f; p.cutoff = 0.35f;
	VoiceExt x;
	std::vector<float> out;
	for (int i = 0; i < (int)(sr * 4); i++) {
		if (i == (int)(sr * 2)) { p.square = true; p.resonance = 0.95f; p.bassComp = 1.f; }
		SeqOutputs o = e.process(1.f / sr);
		VoiceIn in; in.voct = o.voct; in.gate = o.gate; in.accent = o.accent; in.slide = o.slide;
		out.push_back(v.process(in, p, x).out);
	}
	return out;
}

struct SeqEvent { int32_t i; float voct; uint8_t flags; };

static std::vector<SeqEvent> renderSeq(uint64_t* hash) {
	float sr = 48000.f;
	SeqEngine e; e.setRunning(true);
	uint64_t h = FNV_INIT;
	std::vector<SeqEvent> ev;
	SeqEvent last{-1, 1e9f, 0xff};
	for (int i = 0; i < (int)(sr * 4); i++) {
		SeqOutputs o = e.process(1.f / sr);
		uint8_t f = (o.gate ? 1 : 0) | (o.accent ? 2 : 0) | (o.slide ? 4 : 0) | (o.clock ? 8 : 0) | (o.end ? 16 : 0);
		h = fnv(h, &o.voct, 4);
		h = fnv(h, &f, 1);
		if (o.voct != last.voct || f != last.flags) { last = {i, o.voct, f}; ev.push_back(last); }
	}
	*hash = h;
	return ev;
}

static bool readFile(const char* path, std::vector<uint8_t>& d) {
	FILE* f = std::fopen(path, "rb");
	if (!f) return false;
	std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
	d.resize(n);
	bool ok = std::fread(d.data(), 1, n, f) == (size_t)n;
	std::fclose(f);
	return ok;
}
static void writeFile(const char* path, const void* p, size_t n) {
	mkdir("tests/golden", 0755);
	FILE* f = std::fopen(path, "wb");
	if (!f) return;
	std::fwrite(p, 1, n, f);
	std::fclose(f);
}
static int16_t to16(float v) { return (int16_t)std::lround(std::fmax(-1.f, std::fmin(1.f, v / 10.f)) * 32767.f); }

TEST("Golden: SBV voice and SBS engine output unchanged since 2.1.0") {
	std::vector<float> y = renderVoice();
	uint64_t hv = fnv(FNV_INIT, y.data(), y.size() * 4);
	uint64_t hs = 0;
	std::vector<SeqEvent> ev = renderSeq(&hs);
	INFO("  voice hash 0x%016llx, seq hash 0x%016llx, %zu seq events\n", (unsigned long long)hv, (unsigned long long)hs, ev.size());
	if (g_writeGolden) {
		std::vector<int16_t> s(y.size());
		for (size_t i = 0; i < y.size(); i++) s[i] = to16(y[i]);
		writeFile("tests/golden/voice.s16", s.data(), s.size() * 2);
		std::vector<uint8_t> b;
		for (auto& e : ev) { uint8_t r[9]; std::memcpy(r, &e.i, 4); std::memcpy(r + 4, &e.voct, 4); r[8] = e.flags; b.insert(b.end(), r, r + 9); }
		writeFile("tests/golden/seq.bin", b.data(), b.size());
		std::printf("  wrote tests/golden/ (voice hash 0x%016llx, seq hash 0x%016llx)\n", (unsigned long long)hv, (unsigned long long)hs);
		return;
	}
	if (hv != GOLDEN_VOICE_HASH) {
		std::vector<uint8_t> d;
		bool have = readFile("tests/golden/voice.s16", d) && d.size() == y.size() * 2;
		CHECK(have);
		if (have) {
			const int16_t* s = (const int16_t*)d.data();
			int worst = 0;
			for (size_t i = 0; i < y.size(); i++) worst = std::max(worst, std::abs(to16(y[i]) - s[i]));
			std::printf("  note: voice hash differs (0x%016llx); 16-bit fallback, worst %d LSB\n", (unsigned long long)hv, worst);
			CHECK(worst <= 1);
		}
	}
	if (hs != GOLDEN_SEQ_HASH) {
		std::vector<uint8_t> d;
		bool have = readFile("tests/golden/seq.bin", d) && d.size() == ev.size() * 9;
		CHECK(have);
		if (have) {
			bool same = true;
			for (size_t k = 0; k < ev.size(); k++) {
				int32_t i; float v; std::memcpy(&i, &d[k * 9], 4); std::memcpy(&v, &d[k * 9 + 4], 4);
				if (i != ev[k].i || d[k * 9 + 8] != ev[k].flags || std::fabs(v - ev[k].voct) > 1e-4f) same = false;
			}
			std::printf("  note: seq hash differs (0x%016llx); event-list fallback %s\n", (unsigned long long)hs, same ? "matches" : "DIFFERS");
			CHECK(same);
		}
	}
}
