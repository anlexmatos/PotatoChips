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
#include "dsp/namco_106.hpp"
#include "dsp/wavetable4bit.hpp"
#include "widget/wavetable_editor.hpp"

// ---------------------------------------------------------------------------
// MARK: Module
// ---------------------------------------------------------------------------

/// A Namco 106 chip emulator module.
struct Chip106 : Module {
 private:
    /// The BLIP buffer to render audio samples from
    BLIPBuffer buffers[POLYPHONY_CHANNELS][Namco106::OSC_COUNT];
    /// The 106 instance to synthesize sound with
    Namco106 apu[POLYPHONY_CHANNELS];
    /// the number of active oscillators on the chip
    unsigned num_oscillators[POLYPHONY_CHANNELS];

    /// a clock divider for running CV acquisition slower than audio rate
    dsp::ClockDivider cvDivider;
    /// a clock divider for running LED updates slower than audio rate
    dsp::ClockDivider lightsDivider;

 public:
    /// the indexes of parameters (knobs, switches, etc.) on the module
    enum ParamIds {
        ENUMS(PARAM_FREQ, Namco106::OSC_COUNT),
        ENUMS(PARAM_VOLUME, Namco106::OSC_COUNT),
        PARAM_NUM_OSCILLATORS,
        PARAM_NUM_OSCILLATORS_ATT,
        PARAM_WAVETABLE,
        PARAM_WAVETABLE_ATT,
        NUM_PARAMS
    };
    /// the indexes of input ports on the module
    enum InputIds {
        ENUMS(INPUT_VOCT, Namco106::OSC_COUNT),
        ENUMS(INPUT_FM, Namco106::OSC_COUNT),
        ENUMS(INPUT_VOLUME, Namco106::OSC_COUNT),
        INPUT_NUM_OSCILLATORS,
        INPUT_WAVETABLE,
        NUM_INPUTS
    };
    /// the indexes of output ports on the module
    enum OutputIds {
        ENUMS(OUTPUT_OSCILLATOR, Namco106::OSC_COUNT),
        NUM_OUTPUTS
    };
    /// the indexes of lights on the module
    enum LightIds {
        ENUMS(LIGHT_CHANNEL, 3 * Namco106::OSC_COUNT),
        NUM_LIGHTS
    };

    /// the bit-depth of the wave-table
    static constexpr auto BIT_DEPTH = 15;
    /// the number of samples in the wave-table
    static constexpr auto SAMPLES_PER_WAVETABLE = 32;
    /// the number of editors on the module
    static constexpr int NUM_WAVEFORMS = 5;

    /// the wave-tables to morph between
    uint8_t wavetable[NUM_WAVEFORMS][SAMPLES_PER_WAVETABLE];

    /// @brief Initialize a new 106 Chip module.
    Chip106() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(PARAM_NUM_OSCILLATORS,      1, Namco106::OSC_COUNT, 4, "Active Channels");
        configParam(PARAM_NUM_OSCILLATORS_ATT, -1, 1,                   0, "Active Channels Attenuverter");
        configParam(PARAM_WAVETABLE,            1, NUM_WAVEFORMS,       1, "Waveform Morph");
        configParam(PARAM_WAVETABLE_ATT,       -1, 1,                   0, "Waveform Morph Attenuverter");
        cvDivider.setDivision(16);
        lightsDivider.setDivision(128);
        // set the output buffer for each individual voice
        for (unsigned oscillator = 0; oscillator < Namco106::OSC_COUNT; oscillator++) {
            configParam(PARAM_FREQ + oscillator, -30.f, 30.f, 0.f, "Channel " + std::to_string(oscillator + 1) + " Frequency",  " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
            configParam(PARAM_VOLUME + oscillator, 0,   15,  15,   "Channel " + std::to_string(oscillator + 1) + " Volume",     "%",   0,                  100.f / 15.f);
        }
        // set the output buffer for each individual voice
        for (unsigned channel = 0; channel < POLYPHONY_CHANNELS; channel++) {
            for (unsigned oscillator = 0; oscillator < Namco106::OSC_COUNT; oscillator++)
                apu[channel].set_output(oscillator, &buffers[channel][oscillator]);
            // volume of 3 produces a roughly 5Vpp signal from all voices
            apu[channel].set_volume(3.f);
        }
        memset(num_oscillators, 1, sizeof num_oscillators);
        // update the sample rate on the engine
        onSampleRateChange();
        // reset the wave-tables to default values
        onReset();
    }

    /// @brief Respond to the change of sample rate in the engine.
    inline void onSampleRateChange() override {
        // update the buffer for each oscillator and polyphony channel
        for (unsigned channel = 0; channel < POLYPHONY_CHANNELS; channel++) {
            for (unsigned oscillator = 0; oscillator < Namco106::OSC_COUNT; oscillator++) {
                buffers[channel][oscillator].set_sample_rate(APP->engine->getSampleRate(), CLOCK_RATE);
            }
        }
    }

    /// @brief Respond to the user resetting the module with the "Initialize" action.
    void onReset() override {
        /// the default wave-table for each page of the wave-table editor
        static constexpr uint8_t* WAVETABLE[NUM_WAVEFORMS] = {
            SINE,
            PW5,
            RAMP_UP,
            TRIANGLE_DIST,
            RAMP_DOWN
        };
        for (unsigned i = 0; i < NUM_WAVEFORMS; i++)
            memcpy(wavetable[i], WAVETABLE[i], SAMPLES_PER_WAVETABLE);
    }

    /// @brief Respond to the user randomizing the module with the "Randomize" action.
    void onRandomize() override {
        for (unsigned table = 0; table < NUM_WAVEFORMS; table++) {
            for (unsigned sample = 0; sample < SAMPLES_PER_WAVETABLE; sample++) {
                wavetable[table][sample] = random::u32() % BIT_DEPTH;
                // interpolate between random samples to smooth slightly
                if (sample > 0) {
                    auto last = wavetable[table][sample - 1];
                    auto next = wavetable[table][sample];
                    wavetable[table][sample] = (last + next) / 2;
                }
            }
        }
    }

    /// @brief Convert the module's state to a JSON object.
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        for (int table = 0; table < NUM_WAVEFORMS; table++) {
            json_t* array = json_array();
            for (int sample = 0; sample < SAMPLES_PER_WAVETABLE; sample++)
                json_array_append_new(array, json_integer(wavetable[table][sample]));
            auto key = "wavetable" + std::to_string(table);
            json_object_set_new(rootJ, key.c_str(), array);
        }

        return rootJ;
    }

    /// @brief Load the module's state from a JSON object.
    void dataFromJson(json_t* rootJ) override {
        for (int table = 0; table < NUM_WAVEFORMS; table++) {
            auto key = "wavetable" + std::to_string(table);
            json_t* data = json_object_get(rootJ, key.c_str());
            if (data) {
                for (int sample = 0; sample < SAMPLES_PER_WAVETABLE; sample++)
                    wavetable[table][sample] = json_integer_value(json_array_get(data, sample));
            }
        }
    }

    /// @brief Return the active oscillators parameter.
    ///
    /// @param channel the polyphonic channel to return the active oscillators for
    /// @returns the active oscillator count \f$\in [1, 8]\f$
    ///
    inline uint8_t getActiveOscillators(unsigned channel) {
        auto param = params[PARAM_NUM_OSCILLATORS].getValue();
        auto att = params[PARAM_NUM_OSCILLATORS_ATT].getValue();
        // get the CV as 1V per oscillator
        auto cv = 8.f * inputs[INPUT_NUM_OSCILLATORS].getPolyVoltage(channel) / 10.f;
        // oscillators are indexed maths style on the chip, not CS style
        return rack::math::clamp(param + att * cv, 1.f, 8.f);
    }

    /// @brief Return the wave-table position parameter.
    ///
    /// @param channel the polyphonic channel to return the wavetable position for
    /// @returns the floating index of the wave-table position \f$\in [0, 4]\f$
    ///
    inline float getWavetablePosition(unsigned channel) {
        auto param = params[PARAM_WAVETABLE].getValue();
        auto att = params[PARAM_WAVETABLE_ATT].getValue();
        // get the CV as 1V per wave-table
        auto cv = inputs[INPUT_WAVETABLE].getPolyVoltage(channel) / 2.f;
        // wave-tables are indexed maths style on panel, subtract 1 for CS style
        return rack::math::clamp(param + att * cv, 1.f, 5.f) - 1;
    }

    /// @brief Return the frequency parameter for the given oscillator.
    ///
    /// @param oscillator the oscillator to get the frequency parameter for
    /// @param channel the polyphonic channel to return the frequency for
    /// @returns the frequency parameter for the given oscillator. This includes
    /// the value of the knob and any CV modulation.
    ///
    inline uint32_t getFrequency(unsigned oscillator, unsigned channel) {
        // get the frequency of the oscillator from the parameter and CVs
        float pitch = params[PARAM_FREQ + oscillator].getValue() / 12.f;
        pitch += inputs[INPUT_VOCT + oscillator].getPolyVoltage(channel);
        pitch += inputs[INPUT_FM + oscillator].getPolyVoltage(channel) / 5.f;
        float freq = rack::dsp::FREQ_C4 * powf(2.0, pitch);
        freq = rack::clamp(freq, 0.0f, 20000.0f);
        // convert the frequency to the 8-bit value for the oscillator
        static constexpr uint32_t wave_length = 64 - (SAMPLES_PER_WAVETABLE / 4);
        // ignoring num_oscillators in the calculation allows the standard 103
        // function where additional oscillators reduce the frequency of all
        freq *= (wave_length * 15.f * 65536.f) / buffers[channel][oscillator].get_clock_rate();
        // clamp within the legal bounds for the frequency value
        freq = rack::clamp(freq, 512.f, 262143.f);
        // OR the waveform length into the high 6 bits of "frequency Hi"
        // register, which is the third bite, i.e. shift left 2 + 16
        return static_cast<uint32_t>(freq) | (wave_length << 18);
    }

    /// @brief Return the volume parameter for the given oscillator.
    ///
    /// @param oscillator the oscillator to get the volume parameter for
    /// @param channel the polyphonic channel to return the volume for
    /// @returns the volume parameter for the given oscillator. This includes
    /// the value of the knob and any CV modulation.
    ///
    inline uint8_t getVolume(unsigned oscillator, unsigned channel) {
        // the minimal value for the volume width register
        static constexpr float VOLUME_MIN = 0;
        // the maximal value for the volume width register
        static constexpr float VOLUME_MAX = 15;
        // get the volume from the parameter knob
        auto param = params[PARAM_VOLUME + oscillator].getValue();
        // apply the control voltage to the attenuation
        if (inputs[INPUT_VOLUME + oscillator].isConnected()) {
            auto cv = inputs[INPUT_VOLUME + oscillator].getPolyVoltage(channel) / 10.f;
            cv = rack::clamp(cv, 0.f, 1.f);
            cv = roundf(100.f * cv) / 100.f;
            param *= 2 * cv;
        }
        // get the 8-bit volume clamped within legal limits
        return rack::clamp(param, VOLUME_MIN, VOLUME_MAX);
    }

    void processCV(unsigned channel) {
        // write waveform data to the chip's RAM based on the position in
        // the wave-table
        auto position = getWavetablePosition(channel);
        // calculate the address of the base waveform in the table
        int wavetable0 = floor(position);
        // calculate the address of the next waveform in the table
        int wavetable1 = ceil(position);
        // calculate floating point offset between the base and next table
        float interpolate = position - wavetable0;
        for (int i = 0; i < SAMPLES_PER_WAVETABLE / 2; i++) {  // iterate over nibbles
            // get the first waveform data
            auto nibbleHi0 = wavetable[wavetable0][2 * i];
            auto nibbleLo0 = wavetable[wavetable0][2 * i + 1];
            // get the second waveform data
            auto nibbleHi1 = wavetable[wavetable1][2 * i];
            auto nibbleLo1 = wavetable[wavetable1][2 * i + 1];
            // floating point interpolation
            uint8_t nibbleHi = ((1.f - interpolate) * nibbleHi0 + interpolate * nibbleHi1);
            uint8_t nibbleLo = ((1.f - interpolate) * nibbleLo0 + interpolate * nibbleLo1);
            // combine the two nibbles into a byte for the RAM
            apu[channel].write(i, (nibbleHi << 4) | nibbleLo);
        }
        // get the number of active oscillators from the panel
        num_oscillators[channel] = getActiveOscillators(channel);
        // set the frequency for all oscillators on the chip
        for (unsigned oscillator = 0; oscillator < Namco106::OSC_COUNT; oscillator++) {
            // extract the low, medium, and high frequency register values
            auto freq = getFrequency(oscillator, channel);
            // FREQUENCY LOW
            uint8_t low = (freq & 0b000000000000000011111111) >> 0;
            apu[channel].write(Namco106::FREQ_LOW + Namco106::REGS_PER_VOICE * oscillator, low);
            // FREQUENCY MEDIUM
            uint8_t med = (freq & 0b000000001111111100000000) >> 8;
            apu[channel].write(Namco106::FREQ_MEDIUM + Namco106::REGS_PER_VOICE * oscillator, med);
            // WAVEFORM LENGTH + FREQUENCY HIGH
            uint8_t hig = (freq & 0b111111110000000000000000) >> 16;
            apu[channel].write(Namco106::FREQ_HIGH + Namco106::REGS_PER_VOICE * oscillator, hig);
            // WAVE ADDRESS
            apu[channel].write(Namco106::WAVE_ADDRESS + Namco106::REGS_PER_VOICE * oscillator, 0);
            // VOLUME (and oscillator selection on oscillator 8, this has
            // no effect on other oscillators, so check logic is skipped)
            apu[channel].write(Namco106::VOLUME + Namco106::REGS_PER_VOICE * oscillator, ((num_oscillators[channel] - 1) << 4) | getVolume(oscillator, channel));
        }
    }

    /// @brief Process a sample.
    ///
    /// @param args the sample arguments (sample rate, sample time, etc.)
    ///
    void process(const ProcessArgs &args) override {
        // determine the number of channels based on the inputs
        unsigned channels = 1;
        for (unsigned input = 0; input < NUM_INPUTS; input++)
            channels = std::max(inputs[input].getChannels(), static_cast<int>(channels));
        // process the CV inputs to the chip
        if (cvDivider.process()) {
            for (unsigned channel = 0; channel < channels; channel++) {
                processCV(channel);
            }
        }
        // set output polyphony channels
        for (unsigned oscillator = 0; oscillator < Namco106::OSC_COUNT; oscillator++)
            outputs[OUTPUT_OSCILLATOR + oscillator].setChannels(channels);
        // process audio samples on the chip engine.
        for (unsigned channel = 0; channel < channels; channel++) {
            // end the frame on the engine
            apu[channel].end_frame(CLOCK_RATE / args.sampleRate);
            // get the output from each oscillator and accumulate into the sum
            for (unsigned oscillator = 0; oscillator < Namco106::OSC_COUNT; oscillator++) {
                const auto sample = buffers[channel][oscillator].read_sample_10V();
                outputs[OUTPUT_OSCILLATOR + oscillator].setVoltage(sample, channel);
            }
        }
        // set the VU meter lights if the light divider is high
        if (lightsDivider.process()) {
            float brightness[Namco106::OSC_COUNT] = {};
            // accumulate brightness for all the channels
            for (unsigned channel = 0; channel < channels; channel++) {
                for (unsigned oscillator = 0; oscillator < Namco106::OSC_COUNT; oscillator++) {
                    brightness[oscillator] = brightness[oscillator] + (oscillator < num_oscillators[channel]);
                }
            }
            // set the lights based on the accumulated brightness
            for (unsigned oscillator = 0; oscillator < Namco106::OSC_COUNT; oscillator++) {
                const auto light = LIGHT_CHANNEL + 3 * (Namco106::OSC_COUNT - oscillator - 1);
                // get the brightness level for the oscillator.  Because the
                // signal is boolean, the root mean square will have no effect.
                // Instead, the average over the channels is used as brightness
                auto level = brightness[oscillator] / channels;
                // set the light colors in BGR order.
                lights[light + 2].setSmoothBrightness(level, args.sampleTime * lightsDivider.getDivision());
                // if there is more than one channel running (polyphonic), set
                // red and green to 0 to produce a blue LED color. This is the
                // standard for LEDs that indicate polyphonic signals in VCV
                // Rack.
                if (channels > 1) level *= 0;
                lights[light + 1].setSmoothBrightness(level, args.sampleTime * lightsDivider.getDivision());
                lights[light + 0].setSmoothBrightness(level, args.sampleTime * lightsDivider.getDivision());
            }
        }
    }
};

// ---------------------------------------------------------------------------
// MARK: Widget
// ---------------------------------------------------------------------------

/// The panel widget for 106.
struct Chip106Widget : ModuleWidget {
    /// @brief Initialize a new widget.
    ///
    /// @param module the back-end module to interact with
    ///
    Chip106Widget(Chip106 *module) {
        setModule(module);
        static constexpr auto panel = "res/106.svg";
        setPanel(APP->window->loadSvg(asset::plugin(plugin_instance, panel)));
        // panel screws
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        // the fill colors for the wave-table editor lines
        static constexpr NVGcolor colors[Chip106::NUM_WAVEFORMS] = {
            {{{1.f, 0.f, 0.f, 1.f}}},  // red
            {{{0.f, 1.f, 0.f, 1.f}}},  // green
            {{{0.f, 0.f, 1.f, 1.f}}},  // blue
            {{{1.f, 1.f, 0.f, 1.f}}},  // yellow
            {{{1.f, 1.f, 1.f, 1.f}}}   // white
        };
        /// the default wave-table for each page of the wave-table editor
        static constexpr uint8_t* wavetables[Chip106::NUM_WAVEFORMS] = {
            SINE,
            PW5,
            RAMP_UP,
            TRIANGLE_DIST,
            RAMP_DOWN
        };
        // Add wave-table editors. If the module is displaying in/being
        // rendered for the library, the module will be null and a dummy
        // waveform is displayed
        for (int waveform = 0; waveform < Chip106::NUM_WAVEFORMS; waveform++) {
            // get the wave-table buffer for this editor
            uint8_t* wavetable = module ?
                    &reinterpret_cast<Chip106*>(this->module)->wavetable[waveform][0] :
                    &wavetables[waveform][0];
            // setup a table editor for the buffer
            auto table_editor = new WaveTableEditor<uint8_t>(
                wavetable,                       // wave-table buffer
                Chip106::SAMPLES_PER_WAVETABLE,  // wave-table length
                Chip106::BIT_DEPTH,              // waveform bit depth
                Vec(10, 26 + 67 * waveform),     // position
                Vec(135, 60),                    // size
                colors[waveform]                 // line fill color
            );
            // add the table editor to the module
            addChild(table_editor);
        }
        // oscillator select
        addParam(createParam<Rogan3PSNES>(Vec(155, 38), module, Chip106::PARAM_NUM_OSCILLATORS));
        addParam(createParam<Rogan1PSNES>(Vec(161, 88), module, Chip106::PARAM_NUM_OSCILLATORS_ATT));
        addInput(createInput<PJ301MPort>(Vec(164, 126), module, Chip106::INPUT_NUM_OSCILLATORS));
        // wave-table morph
        addParam(createParam<Rogan3PSNES>(Vec(155, 183), module, Chip106::PARAM_WAVETABLE));
        addParam(createParam<Rogan1PSNES>(Vec(161, 233), module, Chip106::PARAM_WAVETABLE_ATT));
        addInput(createInput<PJ301MPort>(Vec(164, 271), module, Chip106::INPUT_WAVETABLE));
        // individual oscillator controls
        for (unsigned i = 0; i < Namco106::OSC_COUNT; i++) {
            addInput(createInput<PJ301MPort>(  Vec(212, 40 + i * 41), module, Chip106::INPUT_VOCT + i    ));
            addInput(createInput<PJ301MPort>(  Vec(242, 40 + i * 41), module, Chip106::INPUT_FM + i      ));
            addParam(createParam<Rogan2PSNES>( Vec(275, 35 + i * 41), module, Chip106::PARAM_FREQ + i    ));
            addInput(createInput<PJ301MPort>(  Vec(317, 40 + i * 41), module, Chip106::INPUT_VOLUME + i  ));
            addParam(createParam<Rogan2PSNES>( Vec(350, 35 + i * 41), module, Chip106::PARAM_VOLUME + i  ));
            addOutput(createOutput<PJ301MPort>(Vec(392, 40 + i * 41), module, Chip106::OUTPUT_OSCILLATOR + i));
            addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(415, 60 + i * 41), module, Chip106::LIGHT_CHANNEL + 3 * i));
        }
    }
};

/// the global instance of the model
Model *modelChip106 = createModel<Chip106, Chip106Widget>("106");
