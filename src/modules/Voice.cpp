// SBV Silverbox Voice and SBX Silverbox Extended (spec §5, §7).
#include "../plugin.hpp"
#include "../dsp/Voice303.hpp"
#include "../generated/Layout.hpp"

// Message from SBX to the SBV on its left.
struct ExtMessage {
	sb::VoiceExt ext;
	bool valid = false;
};

struct SBV : Module {
	enum ParamId { TUNING_PARAM, CUTOFF_PARAM, RESONANCE_PARAM, ENVMOD_PARAM, DECAY_PARAM, ACCENT_PARAM,
	               WAVE_PARAM, BASSCOMP_PARAM, KEYTRACK_PARAM, PARAMS_LEN };
	enum InputId { VOCT_INPUT, GATE_INPUT, ACCENT_INPUT, SLIDE_INPUT, CUTOFF_INPUT, AUDIO_INPUT, INPUTS_LEN };
	enum OutputId { FENV_OUTPUT, SWEEP_OUTPUT, OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { GATE_LIGHT, ACCENT_LIGHT, SLIDE_LIGHT, LIGHTS_LEN };

	sb::Voice303 voice;
	GateIn gateIn, accentIn, slideIn;
	ExtMessage extMsg[2];
	dsp::ClockDivider lightDiv;

	SBV() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(TUNING_PARAM, -12.f, 12.f, 0.f, "Tuning", " semitones");
		configParam(CUTOFF_PARAM, 0.f, 1.f, 0.5f, "Cut off freq", "%", 0.f, 100.f);
		configParam(RESONANCE_PARAM, 0.f, 1.f, 0.5f, "Resonance", "%", 0.f, 100.f);
		configParam(ENVMOD_PARAM, 0.f, 1.f, 0.5f, "Env mod", "%", 0.f, 100.f);
		configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay", "%", 0.f, 100.f);
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent", "%", 0.f, 100.f);
		configSwitch(WAVE_PARAM, 0.f, 1.f, 0.f, "Waveform", {"Saw", "Square"});
		configParam(BASSCOMP_PARAM, 0.f, 1.f, 0.f, "Bass comp (restores the low end lost as resonance rises)", "%", 0.f, 100.f);
		configParam(KEYTRACK_PARAM, 0.f, 1.f, 0.f, "Key track (cut off follows pitch)", "%", 0.f, 100.f);
		configInput(VOCT_INPUT, "1V/oct pitch");
		configInput(GATE_INPUT, "Gate");
		configInput(ACCENT_INPUT, "Accent");
		configInput(SLIDE_INPUT, "Slide");
		configInput(CUTOFF_INPUT, "Cutoff CV (±5 V = ±3 octaves)");
		configInput(AUDIO_INPUT, "Audio in (replaces the oscillator)");
		configOutput(FENV_OUTPUT, "Filter envelope");
		configOutput(SWEEP_OUTPUT, "Accent sweep");
		configOutput(OUT_OUTPUT, "Audio");
		configLight(GATE_LIGHT, "Gate");
		configLight(ACCENT_LIGHT, "Accent");
		configLight(SLIDE_LIGHT, "Slide");
		rightExpander.producerMessage = &extMsg[0];
		rightExpander.consumerMessage = &extMsg[1];
		lightDiv.setDivision(64);
		voice.setRate(48000.f);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override { voice.setRate(e.sampleRate); }
	void onReset(const ResetEvent& e) override { Module::onReset(e); voice.reset(); }

	void process(const ProcessArgs& args) override {
		sb::VoiceParams p;
		p.tuning = params[TUNING_PARAM].getValue();
		p.cutoff = params[CUTOFF_PARAM].getValue();
		p.resonance = params[RESONANCE_PARAM].getValue();
		p.envMod = params[ENVMOD_PARAM].getValue();
		p.decay = params[DECAY_PARAM].getValue();
		p.accent = params[ACCENT_PARAM].getValue();
		p.square = params[WAVE_PARAM].getValue() > 0.5f;
		p.bassComp = params[BASSCOMP_PARAM].getValue();
		p.keyTrack = params[KEYTRACK_PARAM].getValue();

		sb::VoiceIn in;
		in.voct = inputs[VOCT_INPUT].getVoltage();
		in.gate = gateIn.process(inputs[GATE_INPUT].getVoltage());
		in.accent = accentIn.process(inputs[ACCENT_INPUT].getVoltage());
		in.slide = slideIn.process(inputs[SLIDE_INPUT].getVoltage());
		in.cutoffCv = inputs[CUTOFF_INPUT].getVoltage();
		in.audioPatched = inputs[AUDIO_INPUT].isConnected();
		in.audioIn = inputs[AUDIO_INPUT].getVoltage();

		sb::VoiceExt ext;
		bool linked = rightExpander.module && rightExpander.module->model == modelSBX;
		if (linked) {
			ExtMessage* m = (ExtMessage*) rightExpander.consumerMessage;
			if (m->valid) ext = m->ext;
		}
		sb::VoiceOut o = voice.process(in, p, ext);
		outputs[OUT_OUTPUT].setVoltage(o.out);
		outputs[FENV_OUTPUT].setVoltage(o.env);
		outputs[SWEEP_OUTPUT].setVoltage(o.sweep);
		if (lightDiv.process()) {
			float t = args.sampleTime * lightDiv.getDivision();
			lights[GATE_LIGHT].setBrightnessSmooth(in.gate, t);
			lights[ACCENT_LIGHT].setBrightnessSmooth(in.accent, t);
			lights[SLIDE_LIGHT].setBrightnessSmooth(in.slide, t);
		}
	}
};

struct SBVWidget : ModuleWidget {
	SBVWidget(SBV* module) {
		sbScaffold(this, module, "SBV");
		placeSBV(this, module);
	}
};

Model* modelSBV = createModel<SBV, SBVWidget>("SBV");

// ---------------------------------------------------------------------------
// SBX: knob value in 0..1 maps to display = mult * base^v (seconds, or x for SWEEP SPEED)

struct SBX : Module {
	enum ParamId { VEGDECAY_PARAM, ACCDECAY_PARAM, SLIDETIME_PARAM, SOFTATTACK_PARAM, SWEEPSPEED_PARAM,
	               FILTERFM_PARAM, OVERDRIVE_PARAM, DECAYRANGE_PARAM, MUFFLER_PARAM, PARAMS_LEN };
	enum InputId { VEGDECAY_INPUT, ACCDECAY_INPUT, SLIDETIME_INPUT, SOFTATTACK_INPUT, SWEEPSPEED_INPUT,
	               FILTERFM_INPUT, OVERDRIVE_INPUT, INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId { LINKED_LIGHT, LIGHTS_LEN };

	struct Curve { float base, mult, stock; };
	// stock values match SBV without SBX
	static Curve curve(int i) {
		switch (i) {
			case VEGDECAY_PARAM: return {375.f, 0.016f, sb::VEG_DECAY_T};
			case ACCDECAY_PARAM: return {100.f, 0.030f, sb::MEG_ACCENT_T};
			case SLIDETIME_PARAM: return {250.f, 0.002f, 0.060f};
			case SOFTATTACK_PARAM: return {100.f, 0.0003f, sb::VEG_ATTACK};
			default: return {16.f, 0.25f, 1.f};   // SWEEP SPEED
		}
	}
	static float defaultFor(int i) {
		Curve c = curve(i);
		return std::log(c.stock / c.mult) / std::log(c.base);
	}

	dsp::ClockDivider div;

	SBX() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const char* names[5] = {"VEG decay", "Accent decay", "Slide time", "Soft attack", "Sweep speed"};
		for (int i = 0; i < 5; i++) {
			Curve c = curve(i);
			bool speed = i == SWEEPSPEED_PARAM;
			configParam(i, 0.f, 1.f, defaultFor(i), names[i], speed ? "×" : " ms", c.base, speed ? c.mult : c.mult * 1000.f);
		}
		configParam(FILTERFM_PARAM, 0.f, 1.f, 0.f, "Filter FM (VCA output into cut off)", "%", 0.f, 100.f);
		configParam(OVERDRIVE_PARAM, 0.f, 24.f, 0.f, "Overdrive into the filter", " dB");
		configSwitch(DECAYRANGE_PARAM, 0.f, 1.f, 0.f, "Decay range", {"Stock (0.2 to 2 s)", "Wide (30 ms to 3 s)"});
		configSwitch(MUFFLER_PARAM, 0.f, 2.f, 0.f, "Muffler", {"Off", "Soft", "Hard"});
		const char* in[7] = {"VEG decay CV", "Accent decay CV", "Slide time CV", "Soft attack CV", "Sweep speed CV", "Filter FM CV", "Overdrive CV"};
		for (int i = 0; i < 7; i++) configInput(i, in[i]);
		configLight(LINKED_LIGHT, "Linked to SBV");
		div.setDivision(16);
	}

	float knob(int i) {
		float v = params[i].getValue();
		if (inputs[i].isConnected()) v += inputs[i].getVoltage() / 5.f;
		return clamp(v, 0.f, 1.f);
	}
	float timeParam(int i) {
		Curve c = curve(i);
		float v = knob(i);
		float t = c.mult * std::pow(c.base, v);
		// exact stock value at the default position, so SBX at defaults changes nothing
		if (!inputs[i].isConnected() && std::fabs(params[i].getValue() - defaultFor(i)) < 1e-5f) t = c.stock;
		return t;
	}

	void process(const ProcessArgs&) override {
		bool linked = leftExpander.module && leftExpander.module->model == modelSBV;
		lights[LINKED_LIGHT].setBrightness(linked);
		if (!linked || !div.process()) return;
		ExtMessage* m = (ExtMessage*) leftExpander.module->rightExpander.producerMessage;
		sb::VoiceExt& x = m->ext;
		x.vegDecayT = timeParam(VEGDECAY_PARAM);
		x.accentDecayT = timeParam(ACCDECAY_PARAM);
		x.slideTime = timeParam(SLIDETIME_PARAM);
		x.softAttack = timeParam(SOFTATTACK_PARAM);
		x.sweepSpeed = timeParam(SWEEPSPEED_PARAM);
		x.filterFm = knob(FILTERFM_PARAM);
		float od = params[OVERDRIVE_PARAM].getValue();
		if (inputs[OVERDRIVE_INPUT].isConnected()) od += inputs[OVERDRIVE_INPUT].getVoltage() / 5.f * 24.f;
		x.overdriveDb = clamp(od, 0.f, 24.f);
		x.decayWide = params[DECAYRANGE_PARAM].getValue() > 0.5f;
		x.muffler = (int) std::round(params[MUFFLER_PARAM].getValue());
		m->valid = true;
		leftExpander.module->rightExpander.requestMessageFlip();
	}
};

struct SBXWidget : ModuleWidget {
	SBXWidget(SBX* module) {
		sbScaffold(this, module, "SBX");
		placeSBX(this, module);
	}
};

Model* modelSBX = createModel<SBX, SBXWidget>("SBX");
