// A Namco 106 Chip module.
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
#include "widget/wavetable_editor.hpp"
#include "dsp/namco_106.hpp"
#include "wavetable4bit.hpp"

// ---------------------------------------------------------------------------
// MARK: Module
// ---------------------------------------------------------------------------

/// A Namco 106 Chip module.
struct Chip106 : Module {
    enum ParamIds {
        ENUMS(PARAM_FREQ, Namco106::OSC_COUNT),
        ENUMS(PARAM_VOLUME, Namco106::OSC_COUNT),
        PARAM_NUM_CHANNELS, PARAM_NUM_CHANNELS_ATT,
        PARAM_WAVETABLE, PARAM_WAVETABLE_ATT,
        PARAM_COUNT
    };
    enum InputIds {
        ENUMS(INPUT_VOCT, Namco106::OSC_COUNT),
        ENUMS(INPUT_FM, Namco106::OSC_COUNT),
        ENUMS(INPUT_VOLUME, Namco106::OSC_COUNT),
        INPUT_NUM_CHANNELS,
        INPUT_WAVETABLE,
        INPUT_COUNT
    };
    enum OutputIds {
        ENUMS(OUTPUT_CHANNEL, Namco106::OSC_COUNT),
        OUTPUT_COUNT
    };
    enum LightIds {
        ENUMS(LIGHT_CHANNEL, Namco106::OSC_COUNT),
        LIGHT_COUNT
    };

    /// The BLIP buffer to render audio samples from
    BLIPBuffer buf[Namco106::OSC_COUNT];
    /// The 106 instance to synthesize sound with
    Namco106 apu;
    /// the number of active channels
    unsigned num_channels = 1;

    // a clock divider for running CV acquisition slower than audio rate
    dsp::ClockDivider cvDivider;
    // a clock divider for running LED updates slower than audio rate
    dsp::ClockDivider lightsDivider;

    /// the bit-depth of the wave-table
    static constexpr auto BIT_DEPTH = 15;
    /// the number of samples in the wave-table
    static constexpr auto SAMPLES_PER_WAVETABLE = 32;

    // the number of editors on the module
    static constexpr int NUM_WAVETABLES = 5;

    /// the samples in the wave-table (1)
    uint8_t values0[SAMPLES_PER_WAVETABLE];
    /// the samples in the wave-table (2)
    uint8_t values1[SAMPLES_PER_WAVETABLE];
    /// the samples in the wave-table (3)
    uint8_t values2[SAMPLES_PER_WAVETABLE];
    /// the samples in the wave-table (4)
    uint8_t values3[SAMPLES_PER_WAVETABLE];
    /// the samples in the wave-table (5)
    uint8_t values4[SAMPLES_PER_WAVETABLE];

    /// the wave-tables to morph between
    uint8_t* values[NUM_WAVETABLES] = {
        values0,
        values1,
        values2,
        values3,
        values4
    };

    /// Initialize a new 106 Chip module.
    Chip106() {
        config(PARAM_COUNT, INPUT_COUNT, OUTPUT_COUNT, LIGHT_COUNT);
        configParam(PARAM_NUM_CHANNELS, 1, 8, 4, "Active Channels");
        configParam(PARAM_NUM_CHANNELS_ATT, -1, 1, 0, "Active Channels Attenuverter");
        configParam(PARAM_WAVETABLE, 1, 5, 1, "Wavetable Morph");
        configParam(PARAM_WAVETABLE_ATT, -1, 1, 0, "Wavetable Morph Attenuverter");
        cvDivider.setDivision(16);
        lightsDivider.setDivision(128);
        // set the output buffer for each individual voice
        for (unsigned i = 0; i < Namco106::OSC_COUNT; i++) {
            auto descFreq = "Channel " + std::to_string(i + 1) + " Frequency";
            configParam(PARAM_FREQ + i, -30.f, 30.f, 0.f, descFreq,  " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
            auto descVol = "Channel " + std::to_string(i + 1) + " Volume";
            configParam(PARAM_VOLUME + i, 0, 15, 15, descVol,  "%", 0, 100.f / 15.f);
            apu.set_output(i, &buf[i]);
        }
        // volume of 3 produces a roughly 5Vpp signal from all voices
        apu.set_volume(3.f);
        // update the sample rate on the engine
        onSampleRateChange();
        // reset the wave-tables to default values
        onReset();
    }

    /// Return the active channels parameter.
    ///
    /// @returns the active channel count in [1, 8]
    ///
    inline uint8_t getActiveChannels() {
        auto param = params[PARAM_NUM_CHANNELS].getValue();
        auto att = params[PARAM_NUM_CHANNELS_ATT].getValue();
        // get the CV as 1V per channel
        auto cv = 8.f * inputs[INPUT_NUM_CHANNELS].getVoltage() / 10.f;
        // channels are indexed maths style on the chip, not CS style
        return rack::math::clamp(param + att * cv, 1.f, 8.f);
    }

    /// Return the wave-table parameter.
    ///
    /// @returns the floating index of the wave-table table in [0, 4]
    ///
    inline float getWavetable() {
        auto param = params[PARAM_WAVETABLE].getValue();
        auto att = params[PARAM_WAVETABLE_ATT].getValue();
        // get the CV as 1V per wave-table
        auto cv = inputs[INPUT_WAVETABLE].getVoltage() / 2.f;
        // wave-tables are indexed maths style on panel, subtract 1 for CS style
        return rack::math::clamp(param + att * cv, 1.f, 5.f) - 1;
    }

    /// Return the frequency parameter for the given channel.
    ///
    /// @param channel the channel to get the frequency parameter for
    /// @returns the frequency parameter for the given channel. This includes
    /// the value of the knob and any CV modulation.
    ///
    inline uint32_t getFrequency(uint8_t channel) {
        // get the frequency of the oscillator from the parameter and CVs
        float pitch = params[PARAM_FREQ + channel].getValue() / 12.f;
        pitch += inputs[INPUT_VOCT + channel].getVoltage();
        float freq = rack::dsp::FREQ_C4 * powf(2.0, pitch);
        static constexpr float FM_SCALE = 5.f;
        freq += FM_SCALE * inputs[INPUT_FM + channel].getVoltage();
        freq = rack::clamp(freq, 0.0f, 20000.0f);
        // convert the frequency to the 8-bit value for the oscillator
        static constexpr uint32_t wave_length = 64 - (SAMPLES_PER_WAVETABLE / 4);
        // ignoring num_channels in the calculation allows the standard 103
        // function where additional channels reduce the frequency of all
        freq *= (wave_length * 15.f * 65536.f) / buf[0].get_clock_rate();
        // clamp within the legal bounds for the frequency value
        freq = rack::clamp(freq, 4.f, 262143.f);
        // OR the waveform length into the high 6 bits of "frequency Hi"
        // register, which is the third bite, i.e. shift left 2 + 16
        return static_cast<uint32_t>(freq) | (wave_length << 18);
    }

    /// Return the volume parameter for the given channel.
    ///
    /// @param channel the channel to get the volume parameter for
    /// @returns the volume parameter for the given channel. This includes
    /// the value of the knob and any CV modulation.
    ///
    inline uint8_t getVolume(uint8_t channel) {
        // the minimal value for the volume width register
        static constexpr float VOLUME_MIN = 0;
        // the maximal value for the volume width register
        static constexpr float VOLUME_MAX = 15;
        // get the volume from the parameter knob
        auto levelParam = params[PARAM_VOLUME + channel].getValue();
        // apply the control voltage to the volume
        static constexpr float FM_SCALE = 0.5f;
        if (inputs[INPUT_VOLUME + channel].isConnected())
            levelParam *= FM_SCALE * inputs[INPUT_VOLUME + channel].getVoltage();
        // get the 8-bit volume clamped within legal limits
        return rack::clamp(levelParam, VOLUME_MIN, VOLUME_MAX);
    }

    /// Return a 10V signed sample from the chip.
    ///
    /// @param channel the channel to get the audio sample for
    ///
    inline float getAudioOut(uint8_t channel) {
        // the peak to peak output of the voltage
        static constexpr float Vpp = 10.f;
        // the amount of voltage per increment of 16-bit fidelity volume
        static constexpr float divisor = std::numeric_limits<int16_t>::max();
        // convert the 16-bit sample to 10Vpp floating point
        return Vpp * buf[channel].read_sample() / divisor;
    }

    /// Process a sample.
    void process(const ProcessArgs &args) override {
        if (cvDivider.process()) {
            // write the waveform data to the chip's RAM
            auto wavetable = getWavetable();
            // calculate the address of the base waveform in the table
            int wavetable0 = floor(wavetable);
            // calculate the address of the next waveform in the table
            int wavetable1 = ceil(wavetable);
            // calculate floating point offset between the base and next table
            float interpolate = wavetable - wavetable0;
            for (int i = 0; i < SAMPLES_PER_WAVETABLE / 2; i++) {  // iterate over nibbles
                // get the first waveform data
                auto nibbleHi0 = values[wavetable0][2 * i];
                auto nibbleLo0 = values[wavetable0][2 * i + 1];
                // get the second waveform data
                auto nibbleHi1 = values[wavetable1][2 * i];
                auto nibbleLo1 = values[wavetable1][2 * i + 1];
                // floating point interpolation
                uint8_t nibbleHi = ((1.f - interpolate) * nibbleHi0 + interpolate * nibbleHi1);
                uint8_t nibbleLo = ((1.f - interpolate) * nibbleLo0 + interpolate * nibbleLo1);
                // combine the two nibbles into a byte for the RAM
                apu.write(i, (nibbleHi << 4) | nibbleLo);
            }
            // get the number of active channels from the panel
            num_channels = getActiveChannels();
            // set the frequency for all channels on the chip
            for (unsigned channel = 0; channel < Namco106::OSC_COUNT; channel++) {
                // extract the low, medium, and high frequency register values
                auto freq = getFrequency(channel);
                // FREQUENCY LOW
                uint8_t low = (freq & 0b000000000000000011111111) >> 0;
                apu.write(Namco106::FREQ_LOW + Namco106::REGS_PER_VOICE * channel, low);
                // FREQUENCY MEDIUM
                uint8_t med = (freq & 0b000000001111111100000000) >> 8;
                apu.write(Namco106::FREQ_MEDIUM + Namco106::REGS_PER_VOICE * channel, med);
                // WAVEFORM LENGTH + FREQUENCY HIGH
                uint8_t hig = (freq & 0b111111110000000000000000) >> 16;
                apu.write(Namco106::FREQ_HIGH + Namco106::REGS_PER_VOICE * channel, hig);
                // WAVE ADDRESS
                apu.write(Namco106::WAVE_ADDRESS + Namco106::REGS_PER_VOICE * channel, 0);
                // VOLUME (and channel selection on channel 8, this has no effect on
                // other channels, so the check logic is skipped)
                apu.write(Namco106::VOLUME + Namco106::REGS_PER_VOICE * channel, ((num_channels - 1) << 4) | getVolume(channel));
            }
        }
        // process audio samples on the chip engine
        apu.end_frame(CLOCK_RATE / args.sampleRate);
        for (unsigned i = 0; i < Namco106::OSC_COUNT; i++)
            outputs[i].setVoltage(getAudioOut(i));
        // set the channel lights if the light divider is high
        if (lightsDivider.process()) {
            for (unsigned i = 0; i < Namco106::OSC_COUNT; i++) {
                auto light = LIGHT_CHANNEL + Namco106::OSC_COUNT - i - 1;
                lights[light].setSmoothBrightness(i < num_channels, args.sampleTime * lightsDivider.getDivision());
            }
        }
    }

    /// Respond to the change of sample rate in the engine.
    inline void onSampleRateChange() override {
        for (unsigned i = 0; i < Namco106::OSC_COUNT; i++)
            buf[i].set_sample_rate(APP->engine->getSampleRate(), CLOCK_RATE);
    }

    /// Respond to the user resetting the module with the "Initialize" action.
    void onReset() override {
        /// the default wave-table for each page of the wave-table editor
        static constexpr uint8_t* wavetables[NUM_WAVETABLES] = {
            SINE,
            PW5,
            RAMP_UP,
            TRIANGLE_DIST,
            RAMP_DOWN
        };
        for (unsigned i = 0; i < NUM_WAVETABLES; i++)
            memcpy(values[i], wavetables[i], SAMPLES_PER_WAVETABLE);
    }

    /// Respond to the user randomizing the module with the "Randomize" action.
    void onRandomize() override {
        for (unsigned table = 0; table < NUM_WAVETABLES; table++) {
            for (unsigned sample = 0; sample < SAMPLES_PER_WAVETABLE; sample++) {
                values[table][sample] = random::u32() % BIT_DEPTH;
                // interpolate between random samples to smooth slightly
                if (sample > 0) {
                    auto last = values[table][sample - 1];
                    auto next = values[table][sample];
                    values[table][sample] = (last + next) / 2;
                }
            }
        }
    }

    /// Convert the module's state to a JSON object.
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        for (int table = 0; table < NUM_WAVETABLES; table++) {
            json_t* array = json_array();
            for (int sample = 0; sample < SAMPLES_PER_WAVETABLE; sample++)
                json_array_append_new(array, json_integer(values[table][sample]));
            auto key = "values" + std::to_string(table);
            json_object_set_new(rootJ, key.c_str(), array);
        }

        return rootJ;
    }

    /// Load the module's state from a JSON object.
    void dataFromJson(json_t* rootJ) override {
        for (int table = 0; table < NUM_WAVETABLES; table++) {
            auto key = "values" + std::to_string(table);
            json_t* data = json_object_get(rootJ, key.c_str());
            if (data) {
                for (int sample = 0; sample < SAMPLES_PER_WAVETABLE; sample++)
                    values[table][sample] = json_integer_value(json_array_get(data, sample));
            }
        }
    }
};

// ---------------------------------------------------------------------------
// MARK: Widget
// ---------------------------------------------------------------------------

/// The widget structure that lays out the panel of the module and the UI menus.
struct Chip106Widget : ModuleWidget {
    Chip106Widget(Chip106 *module) {
        setModule(module);
        static constexpr auto panel = "res/106.svg";
        setPanel(APP->window->loadSvg(asset::plugin(plugin_instance, panel)));
        // panel screws
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        // if the module is displaying in/being rendered for the library, the
        // module will be null and a dummy waveform is displayed
        auto module_ = reinterpret_cast<Chip106*>(this->module);
        // the fill colors for the wave-table editor lines
        static constexpr NVGcolor colors[Chip106::NUM_WAVETABLES] = {
            {{{1.f, 0.f, 0.f, 1.f}}},  // red
            {{{0.f, 1.f, 0.f, 1.f}}},  // green
            {{{0.f, 0.f, 1.f, 1.f}}},  // blue
            {{{1.f, 1.f, 0.f, 1.f}}},  // yellow
            {{{1.f, 1.f, 1.f, 1.f}}}   // white
        };
        /// the default wave-table for each page of the wave-table editor
        static constexpr uint8_t* wavetables[Chip106::NUM_WAVETABLES] = {
            SINE,
            PW5,
            RAMP_UP,
            TRIANGLE_DIST,
            RAMP_DOWN
        };
        // add wave-table editors
        for (int i = 0; i < Chip106::NUM_WAVETABLES; i++) {
            // get the wave-table buffer for this editor
            uint8_t* wavetable =
                module ? &module_->values[i][0] : &wavetables[i][0];
            // setup a table editor for the buffer
            auto table_editor = new WaveTableEditor<uint8_t>(
                wavetable,                       // wave-table buffer
                Chip106::SAMPLES_PER_WAVETABLE,  // wave-table length
                Chip106::BIT_DEPTH,              // waveform bit depth
                Vec(10, 26 + 67 * i),            // position
                Vec(135, 60),                    // size
                colors[i]                        // line fill color
            );
            // add the table editor to the module
            addChild(table_editor);
        }
        // channel select
        addParam(createParam<Rogan3PSNES>(Vec(155, 38), module, Chip106::PARAM_NUM_CHANNELS));
        addParam(createParam<Rogan1PSNES>(Vec(161, 88), module, Chip106::PARAM_NUM_CHANNELS_ATT));
        addInput(createInput<PJ301MPort>(Vec(164, 126), module, Chip106::INPUT_NUM_CHANNELS));
        // wave-table morph
        addParam(createParam<Rogan3PSNES>(Vec(155, 183), module, Chip106::PARAM_WAVETABLE));
        addParam(createParam<Rogan1PSNES>(Vec(161, 233), module, Chip106::PARAM_WAVETABLE_ATT));
        addInput(createInput<PJ301MPort>(Vec(164, 271), module, Chip106::INPUT_WAVETABLE));
        // individual channel controls
        for (unsigned i = 0; i < Namco106::OSC_COUNT; i++) {
            addInput(createInput<PJ301MPort>(  Vec(212, 40 + i * 41), module, Chip106::INPUT_VOCT + i    ));
            addInput(createInput<PJ301MPort>(  Vec(242, 40 + i * 41), module, Chip106::INPUT_FM + i      ));
            addParam(createParam<Rogan2PSNES>( Vec(275, 35 + i * 41), module, Chip106::PARAM_FREQ + i    ));
            addInput(createInput<PJ301MPort>(  Vec(317, 40 + i * 41), module, Chip106::INPUT_VOLUME + i  ));
            addParam(createParam<Rogan2PSNES>( Vec(350, 35 + i * 41), module, Chip106::PARAM_VOLUME + i  ));
            addOutput(createOutput<PJ301MPort>(Vec(392, 40 + i * 41), module, Chip106::OUTPUT_CHANNEL + i));
            addChild(createLight<SmallLight<WhiteLight>>(Vec(415, 60 + i * 41), module, Chip106::LIGHT_CHANNEL + i));
        }
    }
};

/// the global instance of the model
Model *modelChip106 = createModel<Chip106, Chip106Widget>("106");
