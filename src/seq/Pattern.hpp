// SBS memory model and the Pitch/Time merge (spec §6.1, §6.2). No Rack includes.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>

namespace sb {

enum TimeSlot : uint8_t { NOTE = 0, TIE = 1, REST = 2 };

struct PitchSlot {
	uint8_t note = 0;        // 0..12, 0 = C, 12 = upper C
	bool up = false, down = false, accent = false, slide = false;
	bool operator==(const PitchSlot& o) const {
		return note == o.note && up == o.up && down == o.down && accent == o.accent && slide == o.slide;
	}
};

constexpr int STEPS = 16;
constexpr int PATTERNS = 64;
constexpr int TRACKS = 7;
constexpr int TRACK_SLOTS = 64;

struct Pattern {
	PitchSlot pitch[STEPS];
	TimeSlot time[STEPS];
	uint8_t length = 16;
	bool triplet = false;
	Pattern() { for (int i = 0; i < STEPS; i++) time[i] = NOTE; }
	bool operator==(const Pattern& o) const {
		if (length != o.length || triplet != o.triplet) return false;
		for (int i = 0; i < STEPS; i++)
			if (!(pitch[i] == o.pitch[i]) || time[i] != o.time[i]) return false;
		return true;
	}
	bool isEmpty() const { return *this == Pattern(); }
};

struct TrackSlot { uint8_t pattern = 0; int8_t transpose = 0; };
struct Track {
	TrackSlot slot[TRACK_SLOTS];
	uint8_t length = 0;
	bool operator==(const Track& o) const {
		if (length != o.length) return false;
		for (int i = 0; i < TRACK_SLOTS; i++)
			if (slot[i].pattern != o.slot[i].pattern || slot[i].transpose != o.slot[i].transpose) return false;
		return true;
	}
};

// One step of the merged result (what actually plays).
struct PlayStep {
	TimeSlot kind = REST;
	int pitchSlot = -1;     // pitch slot used, -1 for REST
	float volts = 0.f;      // relative to middle C = 0 V
	int semis = 0;          // same, in semitones
	bool accent = false, slide = false;
};

inline int slotSemis(const PitchSlot& p) { return (int)p.note + 12 * ((p.up ? 1 : 0) - (p.down ? 1 : 0)); }

// The 303 merge: NOTE steps consume the next pitch slot; TIE extends the previous note;
// REST is silence. The pitch pointer restarts with each pattern loop. A TIE with no
// note to extend (step 1, or after a REST) plays as a REST.
inline void merge(const Pattern& p, PlayStep out[STEPS]) {
	int pp = 0;
	int len = p.length < 1 ? 1 : (p.length > STEPS ? STEPS : p.length);
	PlayStep prev;
	for (int i = 0; i < STEPS; i++) {
		PlayStep s;
		if (i < len) {
			TimeSlot t = p.time[i];
			if (t == NOTE) {
				const PitchSlot& ps = p.pitch[pp];
				s.kind = NOTE;
				s.pitchSlot = pp;
				s.semis = slotSemis(ps);
				s.volts = s.semis / 12.f;
				s.accent = ps.accent;
				s.slide = ps.slide;
				pp = (pp + 1) % STEPS;
			} else if (t == TIE && prev.kind != REST) {
				s = prev;
				s.kind = TIE;
			}
		}
		out[i] = s;
		prev = s;
	}
}

// Pattern index 0..63 as group-number-section, e.g. "I-1A", "IV-8B".
inline std::string patternName(int i) {
	static const char* g[4] = {"I", "II", "III", "IV"};
	if (i < 0) i = 0;
	if (i > 63) i = 63;
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%s-%d%c", g[i / 16], (i % 16) / 2 + 1, (i % 2) ? 'B' : 'A');
	return buf;
}

static const char* const NOTE_NAMES[13] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C"};

// Compact text form for saving: 32 hex chars of pitch, 16 chars of time (N/T/R), length, triplet.
inline std::string patternToString(const Pattern& p) {
	std::string s;
	char buf[8];
	for (int i = 0; i < STEPS; i++) {
		const PitchSlot& q = p.pitch[i];
		int b = (q.note & 15) | (q.up ? 16 : 0) | (q.down ? 32 : 0) | (q.accent ? 64 : 0) | (q.slide ? 128 : 0);
		std::snprintf(buf, sizeof(buf), "%02x", b);
		s += buf;
	}
	s += ':';
	for (int i = 0; i < STEPS; i++) s += "NTR"[p.time[i] > 2 ? 2 : p.time[i]];
	std::snprintf(buf, sizeof(buf), ":%d:%d", (int)p.length, p.triplet ? 1 : 0);
	s += buf;
	return s;
}

inline bool patternFromString(const std::string& s, Pattern& p) {
	if (s.size() < 32 + 1 + 16 + 4) return false;
	Pattern q;
	for (int i = 0; i < STEPS; i++) {
		unsigned b = 0;
		if (std::sscanf(s.c_str() + 2 * i, "%2x", &b) != 1) return false;
		q.pitch[i].note = (uint8_t)((b & 15) > 12 ? 12 : (b & 15));
		q.pitch[i].up = b & 16; q.pitch[i].down = b & 32;
		q.pitch[i].accent = b & 64; q.pitch[i].slide = b & 128;
		if (q.pitch[i].up && q.pitch[i].down) q.pitch[i].down = false;
	}
	if (s[32] != ':') return false;
	for (int i = 0; i < STEPS; i++) {
		char c = s[33 + i];
		q.time[i] = c == 'T' ? TIE : (c == 'R' ? REST : NOTE);
	}
	int len = 16, trip = 0;
	if (std::sscanf(s.c_str() + 49, ":%d:%d", &len, &trip) != 2) return false;
	q.length = (uint8_t)(len < 1 ? 1 : (len > 16 ? 16 : len));
	q.triplet = trip != 0;
	p = q;
	return true;
}

// The demo line in pattern I-1A (shows slide, accent and the time list).
inline Pattern demoPattern() {
	Pattern p;
	struct D { int n; bool up, down, acc, sl; };
	const D d[16] = {
		{0, 0, 0, 1, 0}, {0, 1, 0, 0, 0}, {3, 0, 0, 0, 1}, {0, 0, 0, 0, 0},
		{7, 0, 0, 1, 0}, {10, 0, 0, 0, 1}, {0, 1, 0, 1, 0}, {5, 0, 0, 0, 0},
		{0, 0, 0, 0, 0}, {3, 0, 0, 1, 1}, {7, 0, 0, 0, 0}, {0, 0, 1, 0, 0},
		{10, 0, 0, 0, 0}, {0, 1, 0, 1, 0}, {7, 0, 0, 0, 1}, {5, 0, 0, 1, 0}};
	for (int i = 0; i < 16; i++) {
		p.pitch[i].note = (uint8_t)d[i].n; p.pitch[i].up = d[i].up; p.pitch[i].down = d[i].down;
		p.pitch[i].accent = d[i].acc; p.pitch[i].slide = d[i].sl;
	}
	const char* t = "NNRNNTNNRNNNTNNN";
	for (int i = 0; i < 16; i++) p.time[i] = t[i] == 'T' ? TIE : (t[i] == 'R' ? REST : NOTE);
	return p;
}

} // namespace sb
