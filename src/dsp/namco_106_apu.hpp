// An oscillator based on the Namco 106 synthesis chip.
// Copyright 2020 Christian Kauten
// Copyright 2006 Shay Green
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
// derived from: Nes_Snd_Emu 0.1.7
//

#ifndef NES_NAMCO106_APU_HPP_
#define NES_NAMCO106_APU_HPP_

#include "blip_buffer/blip_synth.hpp"

/// An oscillator based on the Namco 106 synthesis chip.
class Namco106 {
 public:
    /// CPU clock cycle count
    typedef int32_t cpu_time_t;
    /// 16-bit memory address
    typedef int16_t cpu_addr_t;
    /// the number of oscillators on the Namco 106 chip
    static constexpr int OSC_COUNT = 8;
    /// the number of registers on the chip
    static constexpr int REG_COUNT = 0x80;

    /// Initialize a new Namco106 chip emulator.
    Namco106() { output(NULL); volume(1.0); reset(); }

    /// Reset internal frame counter, registers, and all oscillators.
    inline void reset() {
        last_time = 0;
        addr_reg = 0;
        memset(reg, 0, REG_COUNT);
        for (int i = 0; i < OSC_COUNT; i++) {
            Namco106_Oscillator& osc = oscs[i];
            osc.delay = 0;
            osc.last_amp = 0;
            osc.wave_pos = 0;
        }
    }

    /// Set the volume.
    ///
    /// @param value the global volume level of the chip
    ///
    inline void volume(double value = 1.f) {
        synth.volume(0.10 / OSC_COUNT * value);
    }

    /// Set treble equalization.
    ///
    /// @param eq the equalizer settings to use
    ///
    inline void treble_eq(const blip_eq_t& eq) { synth.treble_eq(eq); }

    /// Set buffer to generate all sound into, or disable sound if NULL.
    ///
    /// @param buf the buffer to write samples from the synthesizer to
    ///
    inline void output(BLIPBuffer* buf) {
        for (int i = 0; i < OSC_COUNT; i++) osc_output(i, buf);
    }

    /// Set the output buffer for an individual synthesizer voice.
    ///
    /// @param i the index of the oscillator to set the output buffer for
    /// @param buf the buffer to write samples from the synthesizer to
    /// @note If buffer is NULL, the specified oscillator is muted and
    ///       emulation accuracy is reduced.
    ///
    inline void osc_output(int i, BLIPBuffer* buf) {
        assert((unsigned) i < OSC_COUNT);
        oscs[i].output = buf;
    }

    /// Run all oscillators up to specified time, end current time frame, then
    /// start a new time frame at time 0. Time frames have no effect on
    /// emulation and each can be whatever length is convenient.
    ///
    /// @param time the number of elapsed cycles
    ///
    inline void end_frame(cpu_time_t time) {
        if (time > last_time) run_until(time);
        assert(last_time >= time);
        last_time -= time;
    }

    /// Write data to the register pointed to by the address register.
    /// Read/write data register is at 0x4800
    // enum { data_reg_addr = 0x4800 };
    inline void write_data(cpu_time_t time, int data) {
        run_until(time);
        access() = data;
    }

    /// Return the data pointed to by the value in the address register.
    inline int read_data() { return access(); }

    /// Set the address register to a new value.
    /// Write-only address register is at 0xF800
    // enum { addr_reg_addr = 0xF800 };
    inline void write_addr(int value) { addr_reg = value; }

 private:
    /// Disable the public copy constructor.
    Namco106(const Namco106&);

    /// Disable the public assignment operator.
    Namco106& operator=(const Namco106&);

    /// An oscillator on the Namco106 chip.
    struct Namco106_Oscillator {
        /// TODO: document
        int32_t delay;
        /// the output buffer to write samples to
        BLIPBuffer* output;
        /// TODO: document
        int16_t last_amp;
        /// the position in the waveform
        int16_t wave_pos;
    };

    /// the oscillators on the chip
    Namco106_Oscillator oscs[OSC_COUNT];

    /// the time after the last run_until call
    cpu_time_t last_time = 0;
    /// the register to read / write data from / to
    int addr_reg;

    /// the RAM on the chip
    uint8_t reg[REG_COUNT];
    /// the synthesizer for producing sound from the chip
    BLIPSynth<BLIPQuality::Good, 15> synth;

    /// Return a reference to the register pointed to by the address register.
    uint8_t& access() {
        int addr = addr_reg & 0x7f;
        if (addr_reg & 0x80) addr_reg = (addr + 1) | 0x80;
        return reg[addr];
    }

    /// Run Namco106 until specified time.
    ///
    /// @param time the number of elapsed cycles
    ///
    void run_until(cpu_time_t nes_end_time) {
        int active_oscs = ((reg[0x7f] >> 4) & 7) + 1;
        for (int i = OSC_COUNT - active_oscs; i < OSC_COUNT; i++) {
            Namco106_Oscillator& osc = oscs[i];
            BLIPBuffer* output = osc.output;
            if (!output) continue;
            // output->set_modified();

            auto time = output->resampled_time(last_time) + osc.delay;
            auto end_time = output->resampled_time(nes_end_time);
            osc.delay = 0;
            if (time < end_time) {
                const uint8_t* osc_reg = &reg[i * 8 + 0x40];
                if (!(osc_reg[4] & 0xe0))
                    continue;

                int volume = osc_reg[7] & 15;
                if (!volume)
                    continue;

                int32_t freq = (osc_reg[4] & 3) * 0x10000 + osc_reg[2] * 0x100L + osc_reg[0];
                // prevent low frequencies from excessively delaying freq changes
                if (freq < 64 * active_oscs) continue;
                auto period = output->resampled_duration(983040) / freq * active_oscs;

                int wave_size = 32 - (osc_reg [4] >> 2 & 7) * 4;
                if (!wave_size)
                    continue;

                int last_amp = osc.last_amp;
                int wave_pos = osc.wave_pos;

                do {
                    // read wave sample
                    int addr = wave_pos + osc_reg[6];
                    int sample = reg[addr >> 1] >> (addr << 2 & 4);
                    wave_pos++;
                    sample = (sample & 15) * volume;

                    // output impulse if amplitude changed
                    int delta = sample - last_amp;
                    if (delta) {
                        last_amp = sample;
                        synth.offset_resampled(time, delta, output);
                    }

                    // next sample
                    time += period;
                    if (wave_pos >= wave_size)
                        wave_pos = 0;
                } while (time < end_time);

                osc.wave_pos = wave_pos;
                osc.last_amp = last_amp;
            }
            osc.delay = time - end_time;
        }
        last_time = nes_end_time;
    }
};

#endif  // NES_NAMCO106_APU_HPP_
