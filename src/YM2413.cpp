// A Yamaha YM2413 chip emulator module.
// Copyright 2020 Christian Kauten
//
// Author: Christian Kauten (kautenja@auburn.edu)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "plugin.hpp"
#include "components.hpp"
#include "dsp/yamaha_ym2413.hpp"

// ---------------------------------------------------------------------------
// MARK: Module
// ---------------------------------------------------------------------------

/// A Yamaha YM2413 chip emulator module.
struct Chip2413 : Module {
    /// the indexes of parameters (knobs, switches, etc.) on the module
    enum ParamIds {
        ENUMS(PARAM_FREQ, YamahaYM2413::channel_count),
        ENUMS(PARAM_LEVEL, YamahaYM2413::channel_count),
        NUM_PARAMS
    };
    /// the indexes of input ports on the module
    enum InputIds {
        ENUMS(INPUT_VOCT, YamahaYM2413::channel_count),
        ENUMS(INPUT_FM, YamahaYM2413::channel_count),
        ENUMS(INPUT_GATE, YamahaYM2413::channel_count),
        NUM_INPUTS
    };
    /// the indexes of output ports on the module
    enum OutputIds {
        ENUMS(OUTPUT_CHANNEL, YamahaYM2413::channel_count),
        NUM_OUTPUTS
    };
    /// the indexes of lights on the module
    enum LightIds { NUM_LIGHTS };

    /// a clock divider for running CV acquisition slower than audio rate
    dsp::ClockDivider cvDivider;

    /// The YM2413 instance to synthesize sound with
    YamahaYM2413 apu;

    /// Initialize a new YM2413 Chip module.
    Chip2413() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        // configParam(PARAM_FREQ + 0, -56.f, 56.f, 0.f,  "Pulse A Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
        // configParam(PARAM_FREQ + 1, -56.f, 56.f, 0.f,  "Pulse B Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
        // configParam(PARAM_FREQ + 2, -56.f, 56.f, 0.f,  "Pulse C Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
        // configParam(PARAM_LEVEL + 0,  0.f,  1.f, 0.5f, "Pulse A Level", "%", 0.f, 100.f);
        // configParam(PARAM_LEVEL + 1,  0.f,  1.f, 0.5f, "Pulse B Level", "%", 0.f, 100.f);
        // configParam(PARAM_LEVEL + 2,  0.f,  1.f, 0.5f, "Pulse C Level", "%", 0.f, 100.f);
        cvDivider.setDivision(16);
        onSampleRateChange();
    }

    // /// Return the frequency for the given channel.
    // ///
    // /// @param channel the index of the channel to get the frequency of
    // /// @returns the 12-bit frequency in a 16-bit container
    // ///
    // inline uint16_t getFrequency(int channel) {
    //     // the minimal value for the frequency register to produce sound
    //     static constexpr float FREQ12BIT_MIN = 4;
    //     // the maximal value for the frequency register
    //     static constexpr float FREQ12BIT_MAX = 4067;
    //     // the clock division of the oscillator relative to the CPU
    //     static constexpr auto CLOCK_DIVISION = 32;
    //     // the constant modulation factor
    //     static constexpr auto MOD_FACTOR = 10.f;
    //     // get the pitch from the parameter and control voltage
    //     float pitch = params[PARAM_FREQ + channel].getValue() / 12.f;
    //     pitch += inputs[INPUT_VOCT + channel].getVoltage();
    //     // convert the pitch to frequency based on standard exponential scale
    //     // and clamp within [0, 20000] Hz
    //     float freq = rack::dsp::FREQ_C4 * powf(2.0, pitch);
    //     freq += MOD_FACTOR * inputs[INPUT_FM + channel].getVoltage();
    //     freq = rack::clamp(freq, 0.0f, 20000.0f);
    //     // convert the frequency to 12-bit
    //     freq = buf[channel].get_clock_rate() / (CLOCK_DIVISION * freq);
    //     return rack::clamp(freq, FREQ12BIT_MIN, FREQ12BIT_MAX);
    // }

    // /// Return the volume parameter for the given channel.
    // ///
    // /// @param channel the channel to get the volume parameter for
    // /// @returns the volume parameter for the given channel. This includes
    // /// the value of the knob and any CV modulation.
    // ///
    // inline uint8_t getVolume(uint8_t channel) {
    //     // the minimal value for the volume width register
    //     static constexpr float LEVEL_MIN = 0;
    //     // the maximal value for the volume width register
    //     static constexpr float LEVEL_MAX = 15;
    //     // get the level from the parameter knob
    //     auto levelParam = params[PARAM_LEVEL + channel].getValue();
    //     // apply the control voltage to the level
    //     if (inputs[INPUT_GATE + channel].isConnected())
    //         levelParam *= inputs[INPUT_GATE + channel].getVoltage() / 2.f;
    //     // return the 8-bit level clamped within legal limits
    //     return rack::clamp(LEVEL_MAX * levelParam, LEVEL_MIN, LEVEL_MAX);
    // }

    // /// Return a 10V signed sample from the YM2413.
    // ///
    // /// @param channel the channel to get the audio sample for
    // ///
    // inline float getAudioOut(int channel) {
    //     // the peak to peak output of the voltage
    //     static constexpr float Vpp = 10.f;
    //     // the amount of voltage per increment of 16-bit fidelity volume
    //     static constexpr float divisor = std::numeric_limits<int16_t>::max();
    //     // convert the 16-bit sample to 10Vpp floating point
    //     return Vpp * buf[channel].read_sample() / divisor;
    // }

    /// @brief Process a sample.
    ///
    /// @param args the sample arguments (sample rate, sample time, etc.)
    ///
    void process(const ProcessArgs &args) override {
        // if (cvDivider.process()) {  // process the CV inputs to the chip
        //     for (int i = 0; i < YamahaYM2413::channel_count; i++) {
        //         // frequency
        //         auto freq = getFrequency(i);
        //         uint8_t lo =  freq & 0b11111111;
        //         apu.write(YamahaYM2413::PULSE_A_LO + 2 * i, lo);
        //         uint8_t hi = (freq & 0b0000111100000000) >> 8;
        //         apu.write(YamahaYM2413::PULSE_A_HI + 2 * i, hi);
        //         // level
        //         apu.write(YamahaYM2413::PULSE_A_ENV + i, getVolume(i));
        //     }
        // }
        // // process audio samples on the chip engine
        // apu.end_frame(CLOCK_RATE / args.sampleRate);
        // for (int i = 0; i < YamahaYM2413::channel_count; i++)
        //     outputs[OUTPUT_CHANNEL + i].setVoltage(getAudioOut(i));

        // apu.write(0x00, 0b00100010);
        // apu.write(0x01, 0b00100010);

        // apu.write(0x02, 0b00100010);

        // apu.write(0x04, 0xff);
        // apu.write(0x05, 0xff);

        // apu.write(0x06, 0xff);
        // apu.write(0x07, 0xff);

        // apu.write(0x0e, 0b00101000);

        // for (int i = 0; i < 8; i++)
        //     apu.write(0x10 + i, 0b00101000);

        // auto key_on = 0b00100000 * ((inputs[INPUT_GATE].getVoltage() / 10.f) > 2.f);
        // auto sustain_on = 0b00000000;
        // for (int i = 0; i < 8; i++)
        //     apu.write(0x20 + i, key_on | sustain_on);

        // for (int i = 0; i < 8; i++)
        //     apu.write(0x30 + i, 0b01001000);


        apu.write(14, random::u32());
        // apu.write(14, random::u32() % 6);

        apu.write(0x36, random::u32());
        apu.write(0x37, random::u32());
        apu.write(0x38, random::u32());


        int16_t samples[2];
        apu.run(2, samples);
        outputs[OUTPUT_CHANNEL + 0].setVoltage(10.f * samples[0] / std::numeric_limits<int16_t>::max());
        outputs[OUTPUT_CHANNEL + 1].setVoltage(10.f * samples[1] / std::numeric_limits<int16_t>::max());
    }

    /// Respond to the change of sample rate in the engine.
    inline void onSampleRateChange() override {
        apu.set_rate(APP->engine->getSampleRate(), CLOCK_RATE);
    }

 };

// ---------------------------------------------------------------------------
// MARK: Widget
// ---------------------------------------------------------------------------

/// The panel widget for 2413.
struct Chip2413Widget : ModuleWidget {
    /// @brief Initialize a new widget.
    ///
    /// @param module the back-end module to interact with
    ///
    Chip2413Widget(Chip2413 *module) {
        setModule(module);
        static constexpr auto panel = "res/2413.svg";
        setPanel(APP->window->loadSvg(asset::plugin(plugin_instance, panel)));
        // panel screws
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        // V/OCT inputs
        // addInput(createInput<PJ301MPort>(Vec(23, 99),  module, Chip2413::INPUT_VOCT + 0));
        // addInput(createInput<PJ301MPort>(Vec(23, 211), module, Chip2413::INPUT_VOCT + 1));
        // addInput(createInput<PJ301MPort>(Vec(23, 320), module, Chip2413::INPUT_VOCT + 2));
        // FM inputs
        // addInput(createInput<PJ301MPort>(Vec(23, 56),  module, Chip2413::INPUT_FM + 0));
        // addInput(createInput<PJ301MPort>(Vec(23, 168), module, Chip2413::INPUT_FM + 1));
        // addInput(createInput<PJ301MPort>(Vec(23, 279), module, Chip2413::INPUT_FM + 2));
        // Frequency parameters
        // addParam(createParam<Rogan3PSNES>(Vec(54, 42),  module, Chip2413::PARAM_FREQ + 0));
        // addParam(createParam<Rogan3PSNES>(Vec(54, 151), module, Chip2413::PARAM_FREQ + 1));
        // addParam(createParam<Rogan3PSNES>(Vec(54, 266), module, Chip2413::PARAM_FREQ + 2));
        // levels
        addInput(createInput<PJ301MPort>(Vec(102, 36),   module, Chip2413::INPUT_GATE + 0));
        // addInput(createInput<PJ301MPort>(Vec(102, 146),  module, Chip2413::INPUT_GATE + 1));
        // addInput(createInput<PJ301MPort>(Vec(102, 255),  module, Chip2413::INPUT_GATE + 2));
        // addParam(createParam<Rogan0PSNES>(Vec(103, 64),  module, Chip2413::PARAM_LEVEL + 0));
        // addParam(createParam<Rogan0PSNES>(Vec(103, 174), module, Chip2413::PARAM_LEVEL + 1));
        // addParam(createParam<Rogan0PSNES>(Vec(103, 283), module, Chip2413::PARAM_LEVEL + 2));
        // channel outputs
        addOutput(createOutput<PJ301MPort>(Vec(107, 104), module, Chip2413::OUTPUT_CHANNEL + 0));
        addOutput(createOutput<PJ301MPort>(Vec(107, 214), module, Chip2413::OUTPUT_CHANNEL + 1));
        // addOutput(createOutput<PJ301MPort>(Vec(107, 324), module, Chip2413::OUTPUT_CHANNEL + 2));
    }
};

/// the global instance of the model
Model *modelChip2413 = createModel<Chip2413, Chip2413Widget>("2413");
