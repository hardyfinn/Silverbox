// SBT Silverbox Tuner (round 2 spec §4): a polyphonic 1V/OCT retuner with eight Scala slots.
#include "../plugin.hpp"
#include "../tuning/Tuner.hpp"
#include "../widgets/TuningMenu.hpp"
#include "../generated/Layout.hpp"

static const std::vector<std::string> ROOT_NAMES = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct SBT : Module {
	enum ParamId { SCALE_PARAM, ROOT_PARAM, FINE_PARAM, LOAD_PARAM, PARAMS_LEN };
	enum InputId { VOCT_INPUT, SCALE_INPUT, INPUTS_LEN };
	enum OutputId { VOCT_OUTPUT, OUTPUTS_LEN };
	enum LightId { ACTIVE_LIGHT, LIGHTS_LEN };

	sb::ScaleSlot slots[sb::TUNER_SLOTS];
	sb::TunerCore core;
	std::atomic<int> current{0};
	std::atomic<bool> fileError{false};
	dsp::ClockDivider lightDiv;

	SBT() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(SCALE_PARAM, 0.f, 7.f, 0.f, "Scale slot", "", 0.f, 1.f, 1.f);
		paramQuantities[SCALE_PARAM]->snapEnabled = true;
		configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root (the key that plays the scale's 1/1)", ROOT_NAMES);
		configParam(FINE_PARAM, -100.f, 100.f, 0.f, "Fine", " cents");
		configButton(LOAD_PARAM, "Load a Scala scale (.scl) into the current slot");
		configInput(VOCT_INPUT, "1V/oct (polyphonic)");
		configInput(SCALE_INPUT, "Scale slot CV (0 to 10 V = 0 to 7 slots, wraps)");
		configOutput(VOCT_OUTPUT, "Retuned 1V/oct");
		configLight(ACTIVE_LIGHT, "Scale active (not 12-tone equal)");
		configBypass(VOCT_INPUT, VOCT_OUTPUT);
		resetSlots();
		lightDiv.setDivision(64);
	}

	// Default slots (spec §4.4): 12-equal, Just, Pythagorean, Meantone, 7-equal, 19-equal, Bohlen-Pierce, Harmonic 8-16.
	void resetSlots() {
		static const int def[sb::TUNER_SLOTS] = {0, 1, 2, 3, 6, 7, 9, 5};
		for (int i = 0; i < sb::TUNER_SLOTS; i++) { sb::Scale s; sb::builtinScale(def[i], s); slots[i].set(s); }
	}

	void onReset(const ResetEvent& e) override { Module::onReset(e); resetSlots(); }

	void process(const ProcessArgs& args) override {
		int slot = sb::tunerSlot(params[SCALE_PARAM].getValue(), inputs[SCALE_INPUT].getVoltage());
		current.store(slot, std::memory_order_relaxed);
		const sb::Scale& s = slots[slot].get();
		int root = clamp((int) std::round(params[ROOT_PARAM].getValue()), 0, 11);
		float fine = params[FINE_PARAM].getValue();
		int n = std::max(1, inputs[VOCT_INPUT].getChannels());
		float in[sb::TUNER_CHANNELS], out[sb::TUNER_CHANNELS];
		for (int c = 0; c < n; c++) in[c] = inputs[VOCT_INPUT].getVoltage(c);
		core.process(s, root, fine, in, out, n);
		outputs[VOCT_OUTPUT].setChannels(n);
		for (int c = 0; c < n; c++) outputs[VOCT_OUTPUT].setVoltage(out[c], c);
		if (lightDiv.process()) lights[ACTIVE_LIGHT].setBrightnessSmooth(!s.isEqual12(), args.sampleTime * lightDiv.getDivision());
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* arr = json_array();
		for (int i = 0; i < sb::TUNER_SLOTS; i++) json_array_append_new(arr, json_string(sb::serializeScale(slots[i].get()).c_str()));
		json_object_set_new(root, "slots", arr);
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* arr = json_object_get(root, "slots");
		if (!arr || !json_is_array(arr)) return;
		for (int i = 0; i < sb::TUNER_SLOTS && i < (int) json_array_size(arr); i++) {
			json_t* j = json_array_get(arr, i);
			sb::Scale s;
			if (j && json_is_string(j) && sb::deserializeScale(json_string_value(j), s)) slots[i].set(s);
		}
	}
};

// Display: slot, scale name, number of notes, mapping mode. Same font and colours as SBS.
struct TunerDisplay : widget::Widget {
	SBT* module = nullptr;
	std::string fontPath = asset::system("res/fonts/ShareTechMono-Regular.ttf");
	float errT = 0.f;
	double lastTime = 0.0;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		std::shared_ptr<window::Font> font = APP->window->loadFont(fontPath);
		if (!font) return;
		double now = system::getTime();
		if (module && module->fileError.exchange(false)) errT = 2.f;
		errT -= (float) (now - lastTime);
		lastTime = now;
		int slot = 0;
		sb::Scale s = sb::equal12();
		if (module) { slot = module->current.load(); s = module->slots[slot].get(); }
		NVGcontext* vg = args.vg;
		NVGcolor fg = nvgRGB(0xff, 0x9a, 0x2e), dim = nvgRGBA(0xff, 0x9a, 0x2e, 0x80);
		nvgFontFaceId(vg, font->handle);
		float h = box.size.y, w = box.size.x;
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFontSize(vg, h * 0.2f);
		nvgFillColor(vg, dim);
		nvgText(vg, 4.f, h * 0.17f, string::f("SLOT %d", slot + 1).c_str(), NULL);
		nvgFillColor(vg, fg);
		nvgFontSize(vg, h * 0.22f);
		std::string name = errT > 0.f ? "FILE ERROR" : s.name;
		// two lines of about 13 characters
		std::string l1 = name.substr(0, 13), l2 = name.size() > 13 ? name.substr(13, 13) : "";
		nvgText(vg, 4.f, h * 0.42f, l1.c_str(), NULL);
		if (!l2.empty()) nvgText(vg, 4.f, h * 0.62f, l2.c_str(), NULL);
		nvgFillColor(vg, dim);
		nvgFontSize(vg, h * 0.18f);
		std::string info = string::f("%d NOTES  %s", s.n(), s.kbm.loaded ? "KBM" : (s.oneKeyPerDegree ? "1 KEY/DEG" : "12 KEYS"));
		nvgText(vg, 4.f, h * 0.85f, info.c_str(), NULL);
		(void) w;
	}
};

struct SBTWidget : ModuleWidget {
	bool loadPrev = false;

	SBTWidget(SBT* module) {
		sbScaffold(this, module, "SBT");
		TunerDisplay* d = createWidget<TunerDisplay>(mm2px(Vec(SBT_DISPLAY_X, SBT_DISPLAY_Y)));
		d->box.size = mm2px(Vec(SBT_DISPLAY_W, SBT_DISPLAY_H));
		d->module = module;
		addChild(d);
		placeSBT(this, module);
	}

	// LOAD opens the file dialog from the UI thread.
	void step() override {
		ModuleWidget::step();
		SBT* m = dynamic_cast<SBT*>(module);
		if (!m) return;
		bool p = m->params[SBT::LOAD_PARAM].getValue() > 0.f;
		if (p && !loadPrev) {
			m->params[SBT::LOAD_PARAM].setValue(0.f);
			sbLoadScl(&m->slots[m->current.load()], &m->fileError);
			p = false;
		}
		loadPrev = p;
	}

	void appendContextMenu(Menu* menu) override {
		SBT* m = dynamic_cast<SBT*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Slot %d", m->current.load() + 1)));
		appendTuningMenu(menu, &m->slots[m->current.load()], nullptr, &m->fileError, true, [=]() { m->resetSlots(); });
	}
};

Model* modelSBT = createModel<SBT, SBTWidget>("SBT");
