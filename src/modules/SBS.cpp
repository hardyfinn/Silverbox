// SBS Silverbox Sequencer (spec §6).
#include "../plugin.hpp"
#include "../seq/SeqEngine.hpp"
#include "../generated/Layout.hpp"
#include <atomic>
#include <osdialog.h>

// UI-thread edits travel to the engine through this single-producer queue.
struct Command {
	enum Type { NONE, SET_CURSOR_PITCH, SET_CURSOR_TIME, DIRECT_PITCH, DIRECT_FLAG, DIRECT_TIME,
	            COPY, PASTE, COPY_NEXT_EMPTY, CLEAR_ALL, LOAD_PATTERN };
	Type type = NONE;
	int a = 0, b = 0, c = 0;
	sb::Pattern pattern;
};

template <int N>
struct CommandQueue {
	Command buf[N];
	std::atomic<int> head{0}, tail{0};
	bool push(const Command& c) {
		int h = head.load(std::memory_order_relaxed);
		int n = (h + 1) % N;
		if (n == tail.load(std::memory_order_acquire)) return false;
		buf[h] = c;
		head.store(n, std::memory_order_release);
		return true;
	}
	bool pop(Command& c) {
		int t = tail.load(std::memory_order_relaxed);
		if (t == head.load(std::memory_order_acquire)) return false;
		c = buf[t];
		tail.store((t + 1) % N, std::memory_order_release);
		return true;
	}
};

// What the display draws, copied from the engine a few hundred times a second.
struct Snapshot {
	sb::Pattern edit;
	int editIdx = 0, playing = 0, queued = -1, stepIdx = -1;
	int mode = sb::PATTERN_PLAY;
	int cursorPitch = 0, cursorTime = 0, cursorTrack = 0;
	int track = 0, trackLen = 0, trackPos = 0;
	int slotPattern = 0, slotTranspose = 0, editTranspose = 0;
	int liveTranspose = 0;
	bool running = false, ext = false;
	float bpm = 120.f;
	int message = 0;   // 1 = copied to next empty, 2 = no empty pattern
	int messageArg = 0;
};

struct PatternQuantity : ParamQuantity {
	std::string getDisplayValueString() override { return sb::patternName((int) std::round(getValue())); }
};
struct TrackQuantity : ParamQuantity {
	std::string getDisplayValueString() override { return string::f("%d", (int) std::round(getValue()) + 1); }
};

struct SBS : Module {
	enum ParamId {
		MODE_PARAM,
		MODE0_PARAM, MODE1_PARAM, MODE2_PARAM, MODE3_PARAM, MODE4_PARAM,
		RUN_PARAM, CLEAR_PARAM, SCRAMBLE_PARAM, UNDO_PARAM,
		PATTERN_PARAM, TRACK_PARAM, TEMPO_PARAM, GATELEN_PARAM, LENGTH_PARAM, MUTATE_PARAM, TRIPLET_PARAM,
		SHIFTP_PARAM, SHIFTT_PARAM,
		KEY0_PARAM, KEY1_PARAM, KEY2_PARAM, KEY3_PARAM, KEY4_PARAM, KEY5_PARAM, KEY6_PARAM,
		KEY7_PARAM, KEY8_PARAM, KEY9_PARAM, KEY10_PARAM, KEY11_PARAM, KEY12_PARAM,
		DOWN_PARAM, UP_PARAM, ACC_PARAM, SLIDEB_PARAM, NOTE_PARAM, TIE_PARAM, REST_PARAM, BACK_PARAM, NEXT_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLOCK_INPUT, RESET_INPUT, TRANSPOSE_INPUT, RUN_INPUT, PATTERNCV_INPUT, MUTATECV_INPUT, INPUTS_LEN };
	enum OutputId { VOCT_OUTPUT, GATE_OUTPUT, ACCENT_OUTPUT, SLIDE_OUTPUT, CLOCK_OUTPUT, END_OUTPUT, OUTPUTS_LEN };
	enum LightId {
		MODE0_LIGHT, MODE1_LIGHT, MODE2_LIGHT, MODE3_LIGHT, MODE4_LIGHT,
		RUN_LIGHT, CLEAR_LIGHT, SCRAMBLE_LIGHT,
		DOWN_LIGHT, UP_LIGHT, ACC_LIGHT, SLIDEB_LIGHT,
		LIGHTS_LEN
	};

	sb::SeqEngine eng;
	CommandQueue<64> queue;
	Snapshot snaps[2];
	std::atomic<int> snapIdx{0};
	dsp::ClockDivider snapDiv, lightDiv;
	dsp::BooleanTrigger btn[PARAMS_LEN];
	dsp::SchmittTrigger clockTrig, resetTrig, runTrig;
	float clearHeld = 0.f, scrambleHeld = 0.f;
	bool clearFired = false, scrambleFired = false;
	int lastSel = -1, lastKnobLen = 16, lastMemLen = 16;
	int lastKnobTrip = 0, lastMemTrip = 0;
	int clockMode = sb::CLOCK_24PPQN;
	bool runIsGate = false;
	bool runGatePrev = false;
	int message = 0, messageArg = 0;
	float messageT = 0.f;
	float blink = 0.f;
	std::atomic<json_t*> pendingImport{nullptr};

	SBS() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(MODE_PARAM, 0.f, 4.f, (float) sb::PATTERN_PLAY, "Mode",
		             {"Pitch write", "Time write", "Pattern play", "Track write", "Track play"});
		const char* modeNames[5] = {"Pitch write", "Time write", "Pattern play", "Track write", "Track play"};
		for (int i = 0; i < 5; i++) configButton(MODE0_PARAM + i, modeNames[i]);
		configButton(RUN_PARAM, "Run / stop");
		configButton(CLEAR_PARAM, "Clear (hold 1 s): pattern, or the track from the cursor in Track write");
		configButton(SCRAMBLE_PARAM, "Scramble the pattern (hold 1 s)");
		configButton(UNDO_PARAM, "Undo");
		configParam<PatternQuantity>(PATTERN_PARAM, 0.f, 63.f, 0.f, "Pattern");
		paramQuantities[PATTERN_PARAM]->snapEnabled = true;
		configParam<TrackQuantity>(TRACK_PARAM, 0.f, 6.f, 0.f, "Track");
		paramQuantities[TRACK_PARAM]->snapEnabled = true;
		configParam(TEMPO_PARAM, 40.f, 300.f, 120.f, "Tempo", " BPM");
		configParam(GATELEN_PARAM, 0.10f, 0.95f, 0.5f, "Gate length", "% of a step", 0.f, 100.f);
		configParam(LENGTH_PARAM, 1.f, 16.f, 16.f, "Pattern length", " steps");
		paramQuantities[LENGTH_PARAM]->snapEnabled = true;
		configParam(MUTATE_PARAM, 0.f, 1.f, 0.f, "Mutate (changes per loop, written to memory)", "%", 0.f, 100.f);
		configSwitch(TRIPLET_PARAM, 0.f, 1.f, 0.f, "Triplet", {"Off", "On"});
		configButton(SHIFTP_PARAM, "Shift pitch list one slot");
		configButton(SHIFTT_PARAM, "Shift time list one step");
		for (int i = 0; i < 13; i++) configButton(KEY0_PARAM + i, std::string("Key ") + (i == 12 ? "high C" : sb::NOTE_NAMES[i]));
		configButton(DOWN_PARAM, "Down (octave; hold for negative transpose)");
		configButton(UP_PARAM, "Up (octave)");
		configButton(ACC_PARAM, "Accent");
		configButton(SLIDEB_PARAM, "Slide");
		configButton(NOTE_PARAM, "Note");
		configButton(TIE_PARAM, "Tie");
		configButton(REST_PARAM, "Rest");
		configButton(BACK_PARAM, "Back");
		configButton(NEXT_PARAM, "Next");
		configInput(CLOCK_INPUT, "Clock (24 PPQN by default; see the context menu)");
		configInput(RESET_INPUT, "Reset");
		configInput(TRANSPOSE_INPUT, "Transpose (1V/oct, semitones)");
		configInput(RUN_INPUT, "Run (trigger toggles)");
		configInput(PATTERNCV_INPUT, "Pattern CV (0 to 10 V adds 0 to 63)");
		configInput(MUTATECV_INPUT, "Mutate CV (±5 V)");
		configOutput(VOCT_OUTPUT, "1V/oct pitch");
		configOutput(GATE_OUTPUT, "Gate");
		configOutput(ACCENT_OUTPUT, "Accent");
		configOutput(SLIDE_OUTPUT, "Slide");
		configOutput(CLOCK_OUTPUT, "Clock (one trigger per step)");
		configOutput(END_OUTPUT, "End of pattern");
		snapDiv.setDivision(128);
		lightDiv.setDivision(64);
		eng.rng.seed(random::u32());
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		eng.setRunning(false);
		eng.clearAll();
		eng.pattern[0] = sb::demoPattern();
		lastSel = -1;
	}

	int selectedPattern() {
		int sel = (int) std::round(params[PATTERN_PARAM].getValue());
		if (inputs[PATTERNCV_INPUT].isConnected()) {
			float v = clamp(inputs[PATTERNCV_INPUT].getVoltage(), 0.f, 10.f);
			sel += (int) std::floor(v / 10.f * 63.f + 0.5f);
		}
		return ((sel % 64) + 64) % 64;
	}

	bool pressed(int id) { return btn[id].process(params[id].getValue() > 0.f); }

	void applyCommand(const Command& c) {
		switch (c.type) {
			case Command::SET_CURSOR_PITCH: eng.cursorPitch = clamp(c.a, 0, 15); break;
			case Command::SET_CURSOR_TIME: eng.cursorTime = clamp(c.a, 0, eng.pattern[eng.selected()].length - 1); break;
			case Command::DIRECT_PITCH: eng.directPitch(c.a, c.b, c.c); break;
			case Command::DIRECT_FLAG: eng.directFlag(c.a, c.b); break;
			case Command::DIRECT_TIME: eng.directTime(c.a, (sb::TimeSlot) c.b); break;
			case Command::COPY: eng.copy(); break;
			case Command::PASTE: eng.paste(); break;
			case Command::COPY_NEXT_EMPTY: {
				int i = eng.copyToNextEmpty();
				message = i >= 0 ? 1 : 2; messageArg = i; messageT = 2.f;
			} break;
			case Command::CLEAR_ALL: eng.clearAll(); lastSel = -1; break;
			default: break;
		}
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;
		Command c;
		while (queue.pop(c)) applyCommand(c);
		if (json_t* imp = pendingImport.exchange(nullptr)) { memoryFromJson(imp); json_decref(imp); }

		// mode buttons
		for (int i = 0; i < 5; i++)
			if (pressed(MODE0_PARAM + i)) params[MODE_PARAM].setValue((float) i);

		sb::SeqControls& k = eng.ctl;
		k.mode = (sb::Mode) clamp((int) std::round(params[MODE_PARAM].getValue()), 0, 4);
		k.pattern = selectedPattern();
		k.track = clamp((int) std::round(params[TRACK_PARAM].getValue()), 0, 6);
		k.bpm = params[TEMPO_PARAM].getValue();
		float g = params[GATELEN_PARAM].getValue();
		if (std::fabs(g - 0.5f) < 0.02f) g = 0.5f;   // detent at the original value
		k.gateLen = g;
		float mut = params[MUTATE_PARAM].getValue();
		if (inputs[MUTATECV_INPUT].isConnected()) mut += inputs[MUTATECV_INPUT].getVoltage() / 5.f;
		k.mutate = clamp(mut, 0.f, 1.f);
		k.transposeIn = inputs[TRANSPOSE_INPUT].isConnected() ? (int) std::round(inputs[TRANSPOSE_INPUT].getVoltage() * 12.f) : 0;
		k.clockPatched = inputs[CLOCK_INPUT].isConnected();
		k.clockMode = (sb::ClockMode) clockMode;
		k.downHeld = params[DOWN_PARAM].getValue() > 0.f;

		// LENGTH and TRIPLET follow the selected pattern's memory, and write to it when turned
		int sel = k.pattern;
		const sb::Pattern& sp = eng.pattern[sel];
		int knobLen = (int) std::round(params[LENGTH_PARAM].getValue());
		int knobTrip = params[TRIPLET_PARAM].getValue() > 0.5f;
		if (sel != lastSel || sp.length != lastMemLen) {
			params[LENGTH_PARAM].setValue(sp.length);
			lastKnobLen = lastMemLen = sp.length;
		} else if (knobLen != lastKnobLen) {
			eng.setLength(knobLen);
			lastKnobLen = lastMemLen = knobLen;
		}
		if (sel != lastSel || (int) sp.triplet != lastMemTrip) {
			params[TRIPLET_PARAM].setValue(sp.triplet ? 1.f : 0.f);
			lastKnobTrip = lastMemTrip = sp.triplet;
		} else if (knobTrip != lastKnobTrip) {
			eng.setTriplet(knobTrip);
			lastKnobTrip = lastMemTrip = knobTrip;
		}
		if (sel != lastSel) { eng.cursorTime = std::min(eng.cursorTime, (int) sp.length - 1); }
		lastSel = sel;

		// transport
		if (pressed(RUN_PARAM)) eng.toggleRun();
		if (inputs[RUN_INPUT].isConnected()) {
			float v = inputs[RUN_INPUT].getVoltage();
			if (runIsGate) {
				bool hi = runGatePrev ? v > 0.5f : v >= 1.f;
				if (hi != runGatePrev) eng.setRunning(hi);
				runGatePrev = hi;
			} else if (runTrig.process(v, 0.5f, 1.f)) {
				eng.toggleRun();
			}
		}
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.5f, 1.f)) eng.reset();
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.5f, 1.f)) eng.clockEdge();

		// editing buttons
		for (int i = 0; i < 13; i++)
			if (pressed(KEY0_PARAM + i)) eng.key(i);
		if (pressed(DOWN_PARAM)) eng.toggleFlag(0);
		if (pressed(UP_PARAM)) eng.toggleFlag(1);
		if (pressed(ACC_PARAM)) eng.toggleFlag(2);
		if (pressed(SLIDEB_PARAM)) eng.toggleFlag(3);
		if (pressed(NOTE_PARAM)) eng.writeTime(sb::NOTE);
		if (pressed(TIE_PARAM)) eng.writeTime(sb::TIE);
		if (pressed(REST_PARAM)) eng.writeTime(sb::REST);
		if (pressed(BACK_PARAM)) eng.back();
		if (pressed(NEXT_PARAM)) eng.next();
		if (pressed(SHIFTP_PARAM)) eng.shiftPitch();
		if (pressed(SHIFTT_PARAM)) eng.shiftTime();
		if (pressed(UNDO_PARAM)) eng.doUndo();
		// hold-to-act buttons
		if (params[CLEAR_PARAM].getValue() > 0.f) {
			clearHeld += dt;
			if (clearHeld >= 1.f && !clearFired) { eng.clear(); clearFired = true; }
		} else { clearHeld = 0.f; clearFired = false; }
		if (params[SCRAMBLE_PARAM].getValue() > 0.f) {
			scrambleHeld += dt;
			if (scrambleHeld >= 1.f && !scrambleFired) { eng.scramble(); scrambleFired = true; }
		} else { scrambleHeld = 0.f; scrambleFired = false; }

		sb::SeqOutputs o = eng.process(dt);
		outputs[VOCT_OUTPUT].setVoltage(o.voct);
		outputs[GATE_OUTPUT].setVoltage(o.gate ? 10.f : 0.f);
		outputs[ACCENT_OUTPUT].setVoltage(o.accent ? 10.f : 0.f);
		outputs[SLIDE_OUTPUT].setVoltage(o.slide ? 10.f : 0.f);
		outputs[CLOCK_OUTPUT].setVoltage(o.clock ? 10.f : 0.f);
		outputs[END_OUTPUT].setVoltage(o.end ? 10.f : 0.f);

		if (messageT > 0.f) { messageT -= dt; if (messageT <= 0.f) message = 0; }

		if (lightDiv.process()) {
			float t = dt * lightDiv.getDivision();
			blink += t;
			if (blink > 0.4f) blink -= 0.4f;
			bool on = blink < 0.2f;
			for (int i = 0; i < 5; i++) lights[MODE0_LIGHT + i].setBrightness(k.mode == i);
			lights[RUN_LIGHT].setBrightness(eng.running);
			lights[CLEAR_LIGHT].setBrightness(clearHeld > 0.f && !clearFired && on);
			lights[SCRAMBLE_LIGHT].setBrightness(scrambleHeld > 0.f && !scrambleFired && on);
			const sb::PitchSlot& ps = eng.pattern[sel].pitch[eng.cursorPitch];
			bool pw = k.mode == sb::PITCH_WRITE;
			lights[DOWN_LIGHT].setBrightness(pw ? ps.down : k.downHeld);
			lights[UP_LIGHT].setBrightness(pw && ps.up);
			lights[ACC_LIGHT].setBrightness(pw && ps.accent);
			lights[SLIDEB_LIGHT].setBrightness(pw && ps.slide);
		}

		if (snapDiv.process()) {
			int w = 1 - snapIdx.load(std::memory_order_relaxed);
			Snapshot& s = snaps[w];
			s.edit = eng.pattern[sel];
			s.editIdx = sel;
			s.playing = eng.playing;
			s.queued = eng.queued;
			s.stepIdx = eng.running && !eng.needStart ? eng.stepIdx : -1;
			s.mode = k.mode;
			s.cursorPitch = eng.cursorPitch;
			s.cursorTime = eng.cursorTime;
			s.cursorTrack = eng.cursorTrack;
			s.track = k.track;
			s.trackLen = eng.track[k.track].length;
			s.trackPos = eng.trackPos;
			s.slotPattern = eng.track[k.track].slot[std::min(eng.cursorTrack, sb::TRACK_SLOTS - 1)].pattern;
			s.slotTranspose = eng.track[k.track].slot[std::min(eng.cursorTrack, sb::TRACK_SLOTS - 1)].transpose;
			s.editTranspose = eng.editTranspose;
			s.liveTranspose = eng.liveTranspose;
			s.running = eng.running;
			s.ext = k.clockPatched;
			s.bpm = k.bpm;
			s.message = message;
			s.messageArg = messageArg;
			snapIdx.store(w, std::memory_order_release);
		}
	}

	// ------------------------------------------------------------ save / load --
	json_t* memoryToJson() {
		json_t* root = json_object();
		json_t* pats = json_array();
		for (int i = 0; i < sb::PATTERNS; i++) json_array_append_new(pats, json_string(sb::patternToString(eng.pattern[i]).c_str()));
		json_object_set_new(root, "patterns", pats);
		json_t* trks = json_array();
		for (int t = 0; t < sb::TRACKS; t++) {
			json_t* tr = json_array();
			for (int i = 0; i < eng.track[t].length; i++) {
				json_t* slot = json_array();
				json_array_append_new(slot, json_integer(eng.track[t].slot[i].pattern));
				json_array_append_new(slot, json_integer(eng.track[t].slot[i].transpose));
				json_array_append_new(tr, slot);
			}
			json_array_append_new(trks, tr);
		}
		json_object_set_new(root, "tracks", trks);
		return root;
	}
	void memoryFromJson(json_t* root) {
		json_t* pats = json_object_get(root, "patterns");
		if (pats) {
			for (int i = 0; i < sb::PATTERNS && i < (int) json_array_size(pats); i++) {
				json_t* s = json_array_get(pats, i);
				if (json_is_string(s)) sb::patternFromString(json_string_value(s), eng.pattern[i]);
			}
		}
		json_t* trks = json_object_get(root, "tracks");
		if (trks) {
			for (int t = 0; t < sb::TRACKS && t < (int) json_array_size(trks); t++) {
				json_t* tr = json_array_get(trks, t);
				int n = std::min((int) json_array_size(tr), sb::TRACK_SLOTS);
				eng.track[t] = sb::Track();
				for (int i = 0; i < n; i++) {
					json_t* slot = json_array_get(tr, i);
					eng.track[t].slot[i].pattern = (uint8_t) clamp((int) json_integer_value(json_array_get(slot, 0)), 0, 63);
					eng.track[t].slot[i].transpose = (int8_t) clamp((int) json_integer_value(json_array_get(slot, 1)), -12, 12);
				}
				eng.track[t].length = (uint8_t) n;
			}
		}
		eng.mergedDirty = true;
		lastSel = -1;
	}

	json_t* dataToJson() override {
		json_t* root = memoryToJson();
		json_object_set_new(root, "clockMode", json_integer(clockMode));
		json_object_set_new(root, "runIsGate", json_boolean(runIsGate));
		json_object_set_new(root, "running", json_boolean(eng.running));
		return root;
	}
	void dataFromJson(json_t* root) override {
		memoryFromJson(root);
		json_t* j;
		if ((j = json_object_get(root, "clockMode"))) clockMode = clamp((int) json_integer_value(j), 0, 2);
		if ((j = json_object_get(root, "runIsGate"))) runIsGate = json_boolean_value(j);
		if ((j = json_object_get(root, "running"))) eng.setRunning(json_boolean_value(j));
	}
};

// ---------------------------------------------------------------------------
// Display

struct SeqDisplay : widget::Widget {
	SBS* module = nullptr;
	std::string fontPath;
	// layout in px
	float labelW = 0.f, cellW = 0.f, top = 0.f, rowH = 0.f;
	float rowY[3] = {0.f, 0.f, 0.f};

	SeqDisplay() { fontPath = asset::system("res/fonts/ShareTechMono-Regular.ttf"); }

	void layout() {
		labelW = box.size.x * 0.085f;
		cellW = (box.size.x - labelW - 6.f) / 16.f;
		top = box.size.y * 0.24f;
		rowH = (box.size.y - top - 4.f) / 3.f;
		for (int i = 0; i < 3; i++) rowY[i] = top + i * rowH;
	}

	Snapshot demo() {
		Snapshot s;
		s.edit = sb::demoPattern();
		s.stepIdx = 4;
		s.running = true;
		return s;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		layout();
		Snapshot s = module ? module->snaps[module->snapIdx.load(std::memory_order_acquire)] : demo();
		std::shared_ptr<window::Font> font = APP->window->loadFont(fontPath);
		if (!font) return;
		NVGcontext* vg = args.vg;
		NVGcolor fg = nvgRGB(0xff, 0x9a, 0x2e), dim = nvgRGBA(0xff, 0x9a, 0x2e, 0x50), hl = nvgRGB(0x6a, 0xd0, 0xff);
		NVGcolor grid = nvgRGBA(0xff, 0x9a, 0x2e, 0x22);
		nvgFontFaceId(vg, font->handle);

		// header
		static const char* modeNames[5] = {"PITCH WRITE", "TIME WRITE", "PATTERN PLAY", "TRACK WRITE", "TRACK PLAY"};
		std::string h = modeNames[clamp(s.mode, 0, 4)];
		h += "   PTN " + sb::patternName(s.editIdx);
		if (s.running && s.playing != s.editIdx) h += "  PLAYING " + sb::patternName(s.playing);
		if (s.queued >= 0) h += "  NEXT " + sb::patternName(s.queued);
		if (s.mode == sb::TRACK_WRITE || s.mode == sb::TRACK_PLAY) {
			h += string::f("   TRK %d %s %d/%d", s.track + 1, s.mode == sb::TRACK_WRITE ? "SLOT" : "AT",
			               (s.mode == sb::TRACK_WRITE ? s.cursorTrack : s.trackPos) + 1, s.trackLen);
			if (s.mode == sb::TRACK_WRITE) h += string::f("  ENTER %s %+d", sb::patternName(s.editIdx).c_str(), s.editTranspose);
		}
		std::string r = s.ext ? "EXT" : string::f("%.0f BPM", s.bpm);
		if (s.edit.triplet) r = "TRIP  " + r;
		if (s.liveTranspose) r = string::f("TR %+d  ", s.liveTranspose) + r;
		if (s.message == 1) h = "COPIED TO " + sb::patternName(s.messageArg);
		if (s.message == 2) h = "NO EMPTY PATTERN";
		nvgFontSize(vg, box.size.y * 0.14f);
		nvgFillColor(vg, fg);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgText(vg, 6.f, top * 0.45f, h.c_str(), NULL);
		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		nvgText(vg, box.size.x - 6.f, top * 0.45f, r.c_str(), NULL);

		// row labels
		static const char* rows[3] = {"PITCH", "TIME", "RESULT"};
		nvgFontSize(vg, rowH * 0.42f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < 3; i++) { nvgFillColor(vg, dim); nvgText(vg, 6.f, rowY[i] + rowH * 0.5f, rows[i], NULL); }

		sb::PlayStep m[16];
		sb::merge(s.edit, m);
		bool playingThis = s.stepIdx >= 0 && s.playing == s.editIdx;

		// cell frames and highlights
		for (int i = 0; i < 16; i++) {
			float x = labelW + i * cellW;
			for (int rr = 0; rr < 3; rr++) {
				nvgBeginPath(vg);
				nvgRect(vg, x + 1.f, rowY[rr] + 1.f, cellW - 2.f, rowH * 0.62f);
				bool cursor = (rr == 0 && s.mode == sb::PITCH_WRITE && s.cursorPitch == i) ||
				              (rr == 1 && s.mode == sb::TIME_WRITE && s.cursorTime == i);
				bool play = playingThis && i == s.stepIdx && rr > 0;
				if (cursor) { nvgFillColor(vg, nvgRGBA(0x6a, 0xd0, 0xff, 0x40)); nvgFill(vg); }
				else if (play) { nvgFillColor(vg, nvgRGBA(0xff, 0x9a, 0x2e, 0x40)); nvgFill(vg); }
				nvgStrokeColor(vg, grid);
				nvgStrokeWidth(vg, 0.6f);
				nvgStroke(vg);
			}
		}
		// links from RESULT notes to the pitch slot they use
		for (int i = 0; i < 16; i++) {
			if (m[i].kind != sb::NOTE) continue;
			float x0 = labelW + i * cellW + cellW * 0.5f;
			float x1 = labelW + m[i].pitchSlot * cellW + cellW * 0.5f;
			nvgBeginPath(vg);
			nvgMoveTo(vg, x1, rowY[0] + rowH * 0.62f + 1.f);
			nvgLineTo(vg, x0, rowY[2] + 1.f);
			nvgStrokeColor(vg, nvgRGBA(0x6a, 0xd0, 0xff, 0x55));
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);
		}
		float fs = std::min(rowH * 0.36f, cellW * 0.42f);
		nvgFontSize(vg, fs);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		auto noteCell = [&](float x, float y, int note, bool up, bool down, bool acc, bool sl, NVGcolor col) {
			nvgFillColor(vg, col);
			nvgText(vg, x, y, note == 12 ? "C'" : sb::NOTE_NAMES[note], NULL);
			float sz = fs * 0.28f;
			if (up || down) {
				float tx = x + cellW * 0.36f, ty = y - fs * 0.2f;
				nvgBeginPath(vg);
				if (up) { nvgMoveTo(vg, tx, ty - sz); nvgLineTo(vg, tx + sz, ty + sz * 0.6f); nvgLineTo(vg, tx - sz, ty + sz * 0.6f); }
				else { nvgMoveTo(vg, tx, ty + sz); nvgLineTo(vg, tx + sz, ty - sz * 0.6f); nvgLineTo(vg, tx - sz, ty - sz * 0.6f); }
				nvgClosePath(vg);
				nvgFill(vg);
			}
			nvgFontSize(vg, fs * 0.62f);
			if (acc) nvgText(vg, x - cellW * 0.25f, y + fs * 0.75f, "A", NULL);
			if (sl) nvgText(vg, x + cellW * 0.25f, y + fs * 0.75f, "S", NULL);
			nvgFontSize(vg, fs);
		};
		for (int i = 0; i < 16; i++) {
			float x = labelW + i * cellW + cellW * 0.5f;
			const sb::PitchSlot& p = s.edit.pitch[i];
			noteCell(x, rowY[0] + rowH * 0.3f, p.note, p.up, p.down, p.accent, p.slide, fg);
			bool inLen = i < s.edit.length;
			NVGcolor tc = inLen ? fg : dim;
			float ty = rowY[1] + rowH * 0.31f;
			nvgFillColor(vg, tc);
			nvgStrokeColor(vg, tc);
			if (s.edit.time[i] == sb::NOTE) { nvgBeginPath(vg); nvgCircle(vg, x, ty, fs * 0.28f); nvgFill(vg); }
			else if (s.edit.time[i] == sb::TIE) { nvgBeginPath(vg); nvgMoveTo(vg, x - cellW * 0.35f, ty); nvgLineTo(vg, x + cellW * 0.35f, ty); nvgStrokeWidth(vg, 1.6f); nvgStroke(vg); }
			if (inLen && m[i].kind == sb::NOTE) {
				const sb::PitchSlot& q = s.edit.pitch[m[i].pitchSlot];
				noteCell(x, rowY[2] + rowH * 0.3f, q.note, q.up, q.down, q.accent, q.slide, fg);
			} else if (inLen && m[i].kind == sb::TIE) {
				float yy = rowY[2] + rowH * 0.31f;
				nvgBeginPath(vg); nvgMoveTo(vg, x - cellW * 0.5f, yy); nvgLineTo(vg, x + cellW * 0.35f, yy);
				nvgStrokeColor(vg, fg); nvgStrokeWidth(vg, 1.6f); nvgStroke(vg);
			}
		}
		(void) hl;
		Widget::drawLayer(args, layer);
	}

	int cellAt(Vec p, int* row) {
		layout();
		if (p.x < labelW || p.y < top) return -1;
		int c = (int) ((p.x - labelW) / cellW);
		int r = (int) ((p.y - top) / rowH);
		if (c < 0 || c > 15 || r < 0 || r > 2) return -1;
		*row = r;
		return c;
	}

	void send(Command::Type t, int a, int b = 0, int c = 0) {
		if (!module) return;
		Command cmd;
		cmd.type = t; cmd.a = a; cmd.b = b; cmd.c = c;
		module->queue.push(cmd);
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) { Widget::onButton(e); return; }
		int row = 0;
		int c = cellAt(e.pos, &row);
		if (c < 0) { Widget::onButton(e); return; }
		e.consume(this);
		if (row == 0) { send(Command::SET_CURSOR_PITCH, c); return; }
		if (row == 1) { send(Command::SET_CURSOR_TIME, c); return; }
		// RESULT cell: direct edit menu
		Snapshot s = module->snaps[module->snapIdx.load(std::memory_order_acquire)];
		sb::PlayStep m[16];
		sb::merge(s.edit, m);
		SeqDisplay* self = this;
		int step = c;
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Step %d of %s", step + 1, sb::patternName(s.editIdx).c_str())));
		if (m[step].kind == sb::NOTE || m[step].kind == sb::TIE) {
			const sb::PitchSlot q = s.edit.pitch[m[step].pitchSlot];
			menu->addChild(createMenuLabel(string::f("Uses pitch slot %d", m[step].pitchSlot + 1)));
			menu->addChild(createSubmenuItem("Note", sb::NOTE_NAMES[q.note], [=](ui::Menu* sub) {
				for (int n = 0; n < 13; n++)
					sub->addChild(createCheckMenuItem(n == 12 ? "C (high)" : sb::NOTE_NAMES[n], "",
						[=]() { return q.note == n; }, [=]() { self->send(Command::DIRECT_PITCH, step, n, 9); }));
			}));
			menu->addChild(createSubmenuItem("Octave", q.up ? "Up" : (q.down ? "Down" : "Middle"), [=](ui::Menu* sub) {
				const char* o[3] = {"Down", "Middle", "Up"};
				for (int i = 0; i < 3; i++)
					sub->addChild(createCheckMenuItem(o[i], "", [=]() { return (i == 0 && q.down) || (i == 2 && q.up) || (i == 1 && !q.up && !q.down); },
						[=]() { self->send(Command::DIRECT_PITCH, step, -1, i - 1); }));
			}));
			menu->addChild(createCheckMenuItem("Accent", "", [=]() { return q.accent; }, [=]() { self->send(Command::DIRECT_FLAG, step, 2); }));
			menu->addChild(createCheckMenuItem("Slide", "", [=]() { return q.slide; }, [=]() { self->send(Command::DIRECT_FLAG, step, 3); }));
		}
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Time (changing it shifts later notes)"));
		const char* tn[3] = {"Note", "Tie", "Rest"};
		for (int i = 0; i < 3; i++)
			menu->addChild(createCheckMenuItem(tn[i], "", [=]() { return (int) s.edit.time[step] == i; },
				[=]() { self->send(Command::DIRECT_TIME, step, i); }));
	}
};

struct SBSWidget : ModuleWidget {
	SBSWidget(SBS* module) {
		sbScaffold(this, module, "SBS");
		SeqDisplay* d = createWidget<SeqDisplay>(mm2px(Vec(SBS_DISPLAY_X, SBS_DISPLAY_Y)));
		d->box.size = mm2px(Vec(SBS_DISPLAY_W, SBS_DISPLAY_H));
		d->module = module;
		addChild(d);
		placeSBS(this, module);
	}

	void appendContextMenu(Menu* menu) override {
		SBS* m = dynamic_cast<SBS*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Clock input", {"24 PPQN", "1 per step", "4 PPQN (1 per 16th)"},
			[=]() { return m->clockMode; }, [=](int i) { m->clockMode = i; }));
		menu->addChild(createBoolPtrMenuItem("Run input is a gate", "", &m->runIsGate));
		menu->addChild(new MenuSeparator);
		auto cmd = [=](Command::Type t) { Command c; c.type = t; m->queue.push(c); };
		menu->addChild(createMenuItem("Copy pattern", "", [=]() { cmd(Command::COPY); }));
		menu->addChild(createMenuItem("Paste pattern", "", [=]() { cmd(Command::PASTE); }));
		menu->addChild(createMenuItem("Copy to next empty pattern", "", [=]() { cmd(Command::COPY_NEXT_EMPTY); }));
		menu->addChild(createSubmenuItem("Clear all memory", "", [=](Menu* sub) {
			sub->addChild(createMenuItem("Yes, clear all 64 patterns and 7 tracks", "", [=]() { cmd(Command::CLEAR_ALL); }));
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Export memory…", "", [=]() {
			char* path = osdialog_file(OSDIALOG_SAVE, NULL, "silverbox-memory.json", NULL);
			if (!path) return;
			json_t* root = m->memoryToJson();
			json_dump_file(root, path, JSON_INDENT(2));
			json_decref(root);
			std::free(path);
		}));
		menu->addChild(createMenuItem("Import memory…", "", [=]() {
			char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (!path) return;
			json_error_t err;
			json_t* root = json_load_file(path, 0, &err);
			std::free(path);
			if (!root) return;
			// handed to the audio thread, which applies it between samples
			json_t* old = m->pendingImport.exchange(root);
			if (old) json_decref(old);
		}));
	}
};

Model* modelSBS = createModel<SBS, SBSWidget>("SBS");
