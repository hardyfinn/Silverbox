// SBF Silverbox Filter (round 2 spec §3).
#include "../plugin.hpp"
#include "../dsp/VoiceBackX.hpp"
#include "../generated/Layout.hpp"
#include "../widgets/ExpParam.hpp"

// GATE TRIM: exponential 20 ms to 2 s over the first 95% of the travel, OFF above.
constexpr float GATE_TRIM_OFF = 0.95f;
inline float gateTrimSeconds(float v) { return v > GATE_TRIM_OFF ? 0.f : 0.02f * std::pow(100.f, clamp(v / GATE_TRIM_OFF, 0.f, 1.f)); }

struct GateTrimQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float t = gateTrimSeconds(getValue());
		if (t <= 0.f) return "Off";
		return t < 1.f ? string::f("%.0f ms", t * 1000.f) : string::f("%.2f s", t);
	}
	std::string getUnit() override { return ""; }
};

struct SBF : Module {
	enum ParamId { CUTOFF_PARAM, RESONANCE_PARAM, ENVMOD_PARAM, DECAY_PARAM, ACCENT_PARAM, MODE_PARAM,
	               DRIVE_PARAM, BASSCOMP_PARAM, KEYTRACK_PARAM, FM_PARAM, VCADECAY_PARAM, ACCDECAY_PARAM,
	               CUTTRIM_PARAM, RESTRIM_PARAM, GATETRIM_PARAM, BASS_PARAM, POLES_PARAM, ENVBIAS_PARAM,
	               ENVATTACK_PARAM, RELEASE_PARAM, ACCVCA_PARAM, SWEEP_PARAM, SWEEPSHAPE_PARAM, CLICK_PARAM, PARAMS_LEN };
	enum InputId { CUTOFF_INPUT, RES_INPUT, ENVMOD_INPUT, DECAY_INPUT, ACCENTCV_INPUT, IN_INPUT, GATE_INPUT, TRIG_INPUT,
	               ACCENT_INPUT, VOCT_INPUT, FM_INPUT, ENVIN_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, FILTER_OUTPUT, FENV_OUTPUT, SWEEP_OUTPUT, VCAENV_OUTPUT, OUTPUTS_LEN };
	enum LightId { GATE_LIGHT, ACCENT_LIGHT, LIGHTS_LEN };

	const ExpCurve vcaDecay{0.016f, 6.f, sb::VEG_DECAY_T};
	const ExpCurve accDecay{0.030f, 3.f, sb::MEG_ACCENT_T};
	const ExpCurve envAttack{sb::MEG_ATTACK, 0.3f, sb::MEG_ATTACK};
	const ExpCurve release{sb::VEG_RELEASE, 1.f, sb::VEG_RELEASE};
	const ExpCurve bass{sb::BASS_MIN, sb::BASS_MAX, sb::FB_HP};

	sb::VoiceBackX voice;
	GateIn gateIn;
	dsp::ClockDivider lightDiv;

	SBF() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CUTOFF_PARAM, 0.f, 1.f, 0.5f, "Cut off freq", "%", 0.f, 100.f);
		configParam(RESONANCE_PARAM, 0.f, 1.f, 0.5f, "Resonance", "%", 0.f, 100.f);
		configParam(ENVMOD_PARAM, 0.f, 1.f, 0.5f, "Env mod", "%", 0.f, 100.f);
		configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay", "%", 0.f, 100.f);
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent", "%", 0.f, 100.f);
		configSwitch(MODE_PARAM, 0.f, 2.f, 0.f, "Mode", {"Low-pass", "Band-pass", "High-pass"});
		configParam(DRIVE_PARAM, 0.f, 24.f, 0.f, "Drive into the ladder", " dB");
		configParam(BASSCOMP_PARAM, 0.f, 1.f, 0.f, "Bass comp (low-pass only)", "%", 0.f, 100.f);
		configParam(KEYTRACK_PARAM, 0.f, 1.f, 0.f, "Key track (uses 1V/OCT, pivots at 1 V)", "%", 0.f, 100.f);
		configParam(FM_PARAM, 0.f, 1.f, 0.f, "FM depth (±5 V × depth = ±2 octaves)", "%", 0.f, 100.f);
		configExp(this, VCADECAY_PARAM, vcaDecay, "VCA decay", " ms", 1000.f);
		configExp(this, ACCDECAY_PARAM, accDecay, "Accent decay", " ms", 1000.f);
		configParam(CUTTRIM_PARAM, -2.f, 2.f, 0.f, "Cutoff trim (TM3)", " octaves");
		configParam(RESTRIM_PARAM, 0.5f, 1.1f, 1.f, "Resonance trim (above about 1.005 the filter self-oscillates)", "×");
		configParam<GateTrimQuantity>(GATETRIM_PARAM, 0.f, 1.f, 1.f, "Gate trim (shortens notes)");
		configExp(this, BASS_PARAM, bass, "Bass (feedback high-pass corner)", " Hz");
		configParam(POLES_PARAM, sb::POLES_MIN, sb::POLES_MAX, sb::STAGE1_RATIO, "Poles (bottom capacitor ratio)", "×");
		configParam(ENVBIAS_PARAM, 0.f, 2.f, sb::ENV_SHIFT, "Env bias (how far full ENV MOD lowers the cutoff)", " octaves");
		configExp(this, ENVATTACK_PARAM, envAttack, "Filter envelope attack", " ms", 1000.f);
		configExp(this, RELEASE_PARAM, release, "VCA release", " ms", 1000.f);
		configParam(ACCVCA_PARAM, 0.f, 2.f, 1.f, "Accent VCA (level boost scale)", "×");
		configSwitch(SWEEP_PARAM, 0.f, 1.f, 0.f, "Sweep", {"Ganged with resonance", "Free (SWEEP SHAPE)"});
		configParam(SWEEPSHAPE_PARAM, 0.f, 1.f, 0.5f, "Sweep shape (when FREE)", "%", 0.f, 100.f);
		configParam(CLICK_PARAM, 0.f, 1.f, 0.f, "Click (VCA attack leak)", "%", 0.f, 100.f);
		configInput(CUTOFF_INPUT, "Cutoff CV (±5 V = ±3 octaves)");
		configInput(RES_INPUT, "Resonance CV (±5 V = ±1)");
		configInput(ENVMOD_INPUT, "Env mod CV (±5 V = ±1)");
		configInput(DECAY_INPUT, "Decay CV (±5 V = ±1)");
		configInput(ACCENTCV_INPUT, "Accent CV (±5 V = ±1)");
		configInput(IN_INPUT, "Audio");
		configInput(GATE_INPUT, "Gate");
		configInput(TRIG_INPUT, "Trigger (restarts the envelopes; plays notes alone when GATE is unpatched)");
		configInput(ACCENT_INPUT, "Accent (a level: 5 V and above is full)");
		configInput(VOCT_INPUT, "1V/oct (for KEY TRACK)");
		configInput(FM_INPUT, "Cutoff FM");
		configInput(ENVIN_INPUT, "Envelope in (0 to 10 V, replaces the filter envelope)");
		configOutput(OUT_OUTPUT, "Audio");
		configOutput(FILTER_OUTPUT, "Filter (before the VCA)");
		configOutput(FENV_OUTPUT, "Filter envelope");
		configOutput(SWEEP_OUTPUT, "Accent sweep");
		configOutput(VCAENV_OUTPUT, "VCA envelope");
		configLight(GATE_LIGHT, "Gate");
		configLight(ACCENT_LIGHT, "Accent");
		lightDiv.setDivision(64);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override { voice.setRate(e.sampleRate); }
	void onReset(const ResetEvent& e) override { Module::onReset(e); voice.reset(); }

	float expv(const ExpCurve& c, int id) { return c.value(params[id].getValue(), atDefault(this, id)); }

	void process(const ProcessArgs& args) override {
		sb::VbParams p;
		p.cutoff = params[CUTOFF_PARAM].getValue();
		p.resonance = params[RESONANCE_PARAM].getValue();
		p.envMod = params[ENVMOD_PARAM].getValue();
		p.decay = params[DECAY_PARAM].getValue();
		p.accent = params[ACCENT_PARAM].getValue();
		p.mode = (int) std::round(params[MODE_PARAM].getValue());
		p.driveDb = params[DRIVE_PARAM].getValue();
		p.bassComp = params[BASSCOMP_PARAM].getValue();
		p.keyTrack = params[KEYTRACK_PARAM].getValue();
		p.fm = params[FM_PARAM].getValue();
		p.vcaDecayT = expv(vcaDecay, VCADECAY_PARAM);
		p.accentDecayT = expv(accDecay, ACCDECAY_PARAM);
		p.cutoffTrim = params[CUTTRIM_PARAM].getValue();
		p.resTrim = params[RESTRIM_PARAM].getValue();
		p.gateTrim = gateTrimSeconds(params[GATETRIM_PARAM].getValue());
		p.bass = expv(bass, BASS_PARAM);
		p.poles = params[POLES_PARAM].getValue();
		p.envBias = params[ENVBIAS_PARAM].getValue();
		p.envAttack = expv(envAttack, ENVATTACK_PARAM);
		p.release = expv(release, RELEASE_PARAM);
		p.accentVca = params[ACCVCA_PARAM].getValue();
		p.sweepFree = params[SWEEP_PARAM].getValue() > 0.5f;
		p.sweepShape = params[SWEEPSHAPE_PARAM].getValue();
		p.click = params[CLICK_PARAM].getValue();

		sb::VbIn in;
		in.in = inputs[IN_INPUT].getVoltage();
		in.gatePatched = inputs[GATE_INPUT].isConnected();
		in.gate = gateIn.process(inputs[GATE_INPUT].getVoltage());
		in.trigPatched = inputs[TRIG_INPUT].isConnected();
		in.trig = inputs[TRIG_INPUT].getVoltage();
		in.accent = inputs[ACCENT_INPUT].getVoltage();
		in.voct = inputs[VOCT_INPUT].getVoltage();
		in.cutoffCv = inputs[CUTOFF_INPUT].getVoltage();
		in.resCv = inputs[RES_INPUT].getVoltage();
		in.envModCv = inputs[ENVMOD_INPUT].getVoltage();
		in.decayCv = inputs[DECAY_INPUT].getVoltage();
		in.accentCv = inputs[ACCENTCV_INPUT].getVoltage();
		in.fm = inputs[FM_INPUT].getVoltage();
		in.envInPatched = inputs[ENVIN_INPUT].isConnected();
		in.envIn = inputs[ENVIN_INPUT].getVoltage();

		sb::VbOut o = voice.process(in, p, outputs[FILTER_OUTPUT].isConnected());
		outputs[OUT_OUTPUT].setVoltage(o.out);
		outputs[FILTER_OUTPUT].setVoltage(o.filterOut);
		outputs[FENV_OUTPUT].setVoltage(o.filterEnv);
		outputs[SWEEP_OUTPUT].setVoltage(o.sweep);
		outputs[VCAENV_OUTPUT].setVoltage(o.vcaEnv);
		if (lightDiv.process()) {
			float t = args.sampleTime * lightDiv.getDivision();
			lights[GATE_LIGHT].setBrightnessSmooth(o.gate, t);
			lights[ACCENT_LIGHT].setBrightnessSmooth(o.accent, t);
		}
	}
};

struct SBFWidget : ModuleWidget {
	SBFWidget(SBF* module) {
		sbScaffold(this, module, "SBF");
		placeSBF(this, module);
	}
};

Model* modelSBF = createModel<SBF, SBFWidget>("SBF");
