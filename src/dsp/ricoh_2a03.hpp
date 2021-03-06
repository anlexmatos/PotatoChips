// A Ricoh 2A03 sound chip emulator.
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

#ifndef DSP_2A03_HPP_
#define DSP_2A03_HPP_

#include "blip_buffer.hpp"
#include "exceptions.hpp"

/// @brief A Ricoh 2A03 sound chip emulator.
class Ricoh2A03 {
 public:
    /// the number of oscillators on the chip
    static constexpr unsigned OSC_COUNT = 4;
    /// the first address of the RAM space
    static constexpr uint16_t ADDR_START = 0x4000;
    /// the last address of the RAM space
    static constexpr uint16_t ADDR_END   = 0x4017;
    /// the number of registers on the chip
    static constexpr uint16_t NUM_REGISTERS = ADDR_END - ADDR_START;

    /// the indexes of the channels on the chip
    enum Channel {
        PULSE0,
        PULSE1,
        TRIANGLE,
        NOISE
    };

    /// the IO registers on the chip
    enum Register : uint16_t {
        /// the duty & 4-bit volume register for pulse waveform generator 0
        PULSE0_VOL =      0x4000,
        /// the sweep register for pulse waveform generator 0
        PULSE0_SWEEP =    0x4001,
        /// the frequency (low 8-bits) for pulse waveform generator 0
        PULSE0_LO =       0x4002,
        /// the frequency (high 3-bits) for pulse waveform generator 0
        PULSE0_HI =       0x4003,
        /// the duty & 4-bit volume register for pulse waveform generator 1
        PULSE1_VOL =      0x4004,
        /// the sweep register for pulse waveform generator 1
        PULSE1_SWEEP =    0x4005,
        /// the frequency (low 8-bits) for pulse waveform generator 1
        PULSE1_LO =       0x4006,
        /// the frequency (high 3-bits) for pulse waveform generator 1
        PULSE1_HI =       0x4007,
        /// the linear counter for the triangle waveform generator
        TRIANGLE_LINEAR = 0x4008,
        /// an unnecessary register that may be used for memory clearing loops
        /// by application code (NES ROMs)
        // APU_UNUSED1 =     0x4009,
        /// the frequency (low 8-bits) for triangle waveform generator
        TRIANGLE_LO =     0x400A,
        /// the frequency (high 3-bits) for triangle waveform generator
        TRIANGLE_HI =     0x400B,
        /// the volume register for the noise generator
        NOISE_VOL =       0x400C,
        /// an unnecessary register that may be used for memory clearing loops
        /// by application code (NES ROMs)
        // APU_UNUSED2 =     0x400D,
        /// period and waveform shape for the noise generator
        NOISE_LO =        0x400E,
        /// length counter value for the noise generator
        NOISE_HI =        0x400F,
        /// play mode and frequency for DMC samples
        // DMC_FREQ =        0x4010,
        /// 7-bit DAC
        // DMC_RAW =         0x4011,
        /// start of the DMC waveform
        // DMC_START =       0x4012,
        /// length of the DMC waveform
        // DMC_LEN =         0x4013,
        /// channel enables and status
        SND_CHN =         0x4015,
        // JOY1 =            0x4016,
        /// the status register
        STATUS =          0x4017,
    };

 private:
    /// An abstract base type for NES oscillators.
    struct Oscillator {
        /// the registers for the oscillator
        unsigned char regs[4];
        /// boolean flags determining if a given register was written to
        bool reg_written[4];
        /// the output buffer for the oscillator
        BLIPBuffer* output;
        /// length counter (0 if unused by oscillator)
        int length_counter;
        /// delay until next (potential) transition
        int delay;
        /// last amplitude oscillator was outputting
        int last_amp;

        /// TODO:
        inline void clock_length(int halt_mask) {
            if (length_counter && !(regs[0] & halt_mask)) length_counter--;
        }

        /// Return the current period the oscillator is set to.
        inline int period() const {
            return (regs[3] & 7) * 0x100 + (regs[2] & 0xff);
        }

        /// Reset the oscillator to it initial state.
        inline void reset() {
            delay = 0;
            last_amp = 0;
        }

        /// Update the waveform for the oscillator with the given amplitude.
        ///
        /// @param amp the amplitude for the current sample
        /// @returns the change in amplitude between amp and the last set amplitude
        ///
        inline int update_amp(int amp) {
            int delta = amp - last_amp;
            last_amp = amp;
            return delta;
        }
    };

    /// An envelope-based NES oscillator
    struct Envelope : Oscillator {
        /// the value of the envelope
        int envelope;
        /// TODO:
        int env_delay;

        /// Clock the envelope.
        void clock_envelope() {
            int period = regs[0] & 15;
            if (reg_written[3]) {
                reg_written[3] = false;
                env_delay = period;
                envelope = 15;
            } else if (--env_delay < 0) {
                env_delay = period;
                if (envelope | (regs[0] & 0x20))
                    envelope = (envelope - 1) & 15;
            }
        }

        /// Return the current volume output from the envelope.
        inline int volume() const {
            return length_counter == 0 ?
                0 : (regs[0] & 0x10) ? (regs[0] & 15) : envelope;
        }

        /// Reset the envelope to its default state
        inline void reset() {
            envelope = 0;
            env_delay = 0;
            Oscillator::reset();
        }
    };

    /// The square wave oscillator from the NES.
    struct Pulse : Envelope {
        /// TODO:
        enum { negate_flag = 0x08 };
        /// TODO:
        enum { shift_mask = 0x07 };
        /// TODO:
        enum { phase_range = 8 };
        /// the current phase of the oscillator
        int phase;
        /// TODO:
        int sweep_delay;

        /// the BLIP synthesizer for the oscillator (shared between pulse waves)
        typedef BLIPSynthesizer<BLIP_QUALITY_GOOD, 15> Synth;
        const Synth* synth;

        void clock_sweep(int negative_adjust) {
            int sweep = regs[1];

            if (--sweep_delay < 0) {
                reg_written[1] = true;

                int period = this->period();
                int shift = sweep & shift_mask;
                if (shift && (sweep & 0x80) && period >= 8) {
                    int offset = period >> shift;

                    if (sweep & negate_flag)
                        offset = negative_adjust - offset;

                    if (period + offset < 0x800) {
                        period += offset;
                        // rewrite period
                        regs[2] = period & 0xff;
                        regs[3] = (regs[3] & ~7) | ((period >> 8) & 7);
                    }
                }
            }

            if (reg_written[1]) {
                reg_written[1] = false;
                sweep_delay = (sweep >> 4) & 7;
            }
        }

        void run(blip_time_t time, blip_time_t end_time) {
            if (!output) return;
            const int volume = this->volume();
            const int period = this->period();
            int offset = period >> (regs[1] & shift_mask);
            if (regs[1] & negate_flag)
                offset = 0;

            const int timer_period = (period + 1) * 2;
            if (volume == 0 || period < 8 || (period + offset) >= 0x800) {
                if (last_amp) {
                    synth->offset(time, -last_amp, output);
                    last_amp = 0;
                }
                time += delay;
                if (time < end_time) {
                    // maintain proper phase
                    int count = (end_time - time + timer_period - 1) / timer_period;
                    phase = (phase + count) & (phase_range - 1);
                    time += static_cast<blip_time_t>(count * timer_period);
                }
            } else {
                // handle duty select
                int duty_select = (regs[0] >> 6) & 3;
                int duty = 1 << duty_select;  // 1, 2, 4, 2
                int amp = 0;
                if (duty_select == 3) {
                    duty = 2;  // negated 25%
                    amp = volume;
                }
                if (phase < duty)
                    amp ^= volume;

                int delta = update_amp(amp);
                if (delta)
                    synth->offset(time, delta, output);

                time += delay;
                if (time < end_time) {
                    BLIPBuffer* const output = this->output;
                    const auto synth = this->synth;
                    int delta = amp * 2 - volume;
                    int phase = this->phase;

                    do {
                        phase = (phase + 1) & (phase_range - 1);
                        if (phase == 0 || phase == duty) {
                            delta = -delta;
                            synth->offset(time, delta, output);
                        }
                        time += timer_period;
                    } while (time < end_time);

                    last_amp = (delta + volume) >> 1;
                    this->phase = phase;
                }
            }
            delay = time - end_time;
        }

        /// Reset the oscillator to its initial state.
        inline void reset() {
            sweep_delay = 0;
            Envelope::reset();
        }
    };

    /// The quantized triangle wave oscillator from the NES.
    struct Triangle : Oscillator {
        enum { phase_range = 16 };
        int phase;
        int linear_counter;
        /// the BLIP synthesizer for the oscillator
        BLIPSynthesizer<BLIP_QUALITY_GOOD, 15> synth;

        inline int calc_amp() const {
            int amp = phase_range - phase;
            if (amp < 0)
                amp = phase - (phase_range + 1);
            return amp;
        }

        void run(blip_time_t time, blip_time_t end_time) {
            if (!output) return;
            // TODO: track phase when period < 3
            // TODO: Output 7.5 on dac when period < 2? More accurate,
            //       but results in more clicks.

            int delta = update_amp(calc_amp());
            if (delta)
                synth.offset(time, delta, output);

            time += delay;
            const int timer_period = period() + 1;
            if (length_counter == 0 || linear_counter == 0 || timer_period < 3) {
                time = end_time;
            } else if (time < end_time) {
                BLIPBuffer* const output = this->output;

                int phase = this->phase;
                int volume = 1;
                if (phase > phase_range) {
                    phase -= phase_range;
                    volume = -volume;
                }

                do {
                    if (--phase == 0) {
                        phase = phase_range;
                        volume = -volume;
                    } else {
                        synth.offset(time, volume, output);
                    }

                    time += timer_period;
                } while (time < end_time);

                if (volume < 0) phase += phase_range;
                this->phase = phase;
                last_amp = calc_amp();
            }
            delay = time - end_time;
        }

        void clock_linear_counter() {
            if (reg_written[3])
                linear_counter = regs[0] & 0x7f;
            else if (linear_counter)
                linear_counter--;

            if (!(regs[0] & 0x80))
                reg_written[3] = false;
        }

        inline void reset() {
            linear_counter = 0;
            phase = phase_range;
            Oscillator::reset();
        }
    };

    /// The noise oscillator from the NES.
    struct Noise : Envelope {
        /// the output value from the noise oscillator
        int noise;
        /// the BLIP synthesizer for the oscillator
        BLIPSynthesizer<BLIP_QUALITY_MEDIUM, 15> synth;

        void run(blip_time_t time, blip_time_t end_time) {
            static const int16_t noise_period_table[16] = {
                0x004, 0x008, 0x010, 0x020, 0x040, 0x060, 0x080, 0x0A0,
                0x0CA, 0x0FE, 0x17C, 0x1FC, 0x2FA, 0x3F8, 0x7F2, 0xFE4
            };
            if (!output) return;
            const int volume = this->volume();
            int amp = (noise & 1) ? volume : 0;
            int delta = update_amp(amp);
            if (delta)
                synth.offset(time, delta, output);

            time += delay;
            if (time < end_time) {
                constexpr int mode_flag = 0x80;

                int period = noise_period_table[regs[2] & 15];
                if (!volume) {
                    // round to next multiple of period
                    time += (end_time - time + period - 1) / period * period;

                    // approximate noise cycling while muted, by shuffling up noise
                    // register
                    // TODO: precise muted noise cycling?
                    if (!(regs[2] & mode_flag)) {
                        int feedback = (noise << 13) ^ (noise << 14);
                        noise = (feedback & 0x4000) | (noise >> 1);
                    }
                } else {
                    BLIPBuffer* const output = this->output;

                    // using re-sampled time avoids conversion in synth.offset()
                    auto rperiod = output->resampled_time(period);
                    auto rtime = output->resampled_time(time);

                    int noise = this->noise;
                    int delta = amp * 2 - volume;
                    const int tap = (regs[2] & mode_flag ? 8 : 13);

                    do {
                        int feedback = (noise << tap) ^ (noise << 14);
                        time += period;

                        if ((noise + 1) & 2) {
                            // bits 0 and 1 of noise differ
                            delta = -delta;
                            synth.offset_resampled(rtime, delta, output);
                        }

                        rtime += rperiod;
                        noise = (feedback & 0x4000) | (noise >> 1);
                    } while (time < end_time);

                    last_amp = (delta + volume) >> 1;
                    this->noise = noise;
                }
            }
            delay = time - end_time;
        }

        /// Reset the oscillator to its initial state.
        inline void reset() {
            noise = 1 << 14;
            Envelope::reset();
        }
    };

    /// the channel 0 pulse wave generator
    Pulse pulse0;
    /// the channel 1 pulse wave generator
    Pulse pulse1;
    /// the channel 2 triangle wave generator
    Noise noise;
    /// the channel 3 noise generator
    Triangle triangle;
    /// pointers to the oscillators
    Oscillator* oscs[OSC_COUNT] = { &pulse0, &pulse1, &triangle, &noise };

    /// has been run until this time in current frame
    blip_time_t last_time;
    /// TODO: document
    int frame_period;
    /// cycles until frame counter runs next
    int frame_delay;
    /// current frame (0-3)
    int frame;
    /// the channel enabled register
    int osc_enables;
    /// TODO: document
    int frame_mode;
    /// a synthesizer shared by squares
    Pulse::Synth square_synth;

    /// @brief Run emulator until specified time, so that any DMC memory reads
    /// can be accounted for (i.e. inserting CPU wait states).
    ///
    /// @param time the number of elapsed cycles
    ///
    void run_until(blip_time_t end_time) {
        if (end_time < last_time)
            throw Exception("end_time must be >= last_time");
        else if (end_time == last_time)
            return;

        while (true) {
            // earlier of next frame time or end time
            blip_time_t time = last_time + frame_delay;
            if (time > end_time) time = end_time;
            frame_delay -= time - last_time;

            // run oscs to present
            pulse0.run(last_time, time);
            pulse1.run(last_time, time);
            triangle.run(last_time, time);
            noise.run(last_time, time);
            last_time = time;

            // no more frames to run
            if (time == end_time) break;

            // take frame-specific actions
            frame_delay = frame_period;
            switch (frame++) {
                case 0:  // fall through
                case 2:
                    // clock length and sweep on frames 0 and 2
                    pulse0.clock_length(0x20);
                    pulse1.clock_length(0x20);
                    noise.clock_length(0x20);
                    // different bit for halt flag on triangle
                    triangle.clock_length(0x80);

                    pulse0.clock_sweep(-1);
                    pulse1.clock_sweep(0);
                    break;
                case 1:
                    // frame 1 is slightly shorter
                    frame_delay -= 2;
                    break;
                case 3:
                    frame = 0;
                    // frame 3 is almost twice as long in mode 1
                    if (frame_mode & 0x80)
                        frame_delay += frame_period - 6;
                    break;
            }
            // clock envelopes and linear counter every frame
            triangle.clock_linear_counter();
            pulse0.clock_envelope();
            pulse1.clock_envelope();
            noise.clock_envelope();
        }
    }

    /// Disable the public copy constructor.
    Ricoh2A03(const Ricoh2A03&);

    /// Disable the public assignment operator.
    Ricoh2A03& operator=(const Ricoh2A03&);

 public:
    /// @brief Initialize a new Ricoh 2A03 emulator.
    Ricoh2A03() {
        // pulse 1 and 2 share the same synthesizer
        pulse0.synth = pulse1.synth = &square_synth;
        set_output(NULL);
        set_volume();
        reset();
    }

    /// @brief Assign single oscillator output to buffer. If buffer is NULL,
    /// silences the given oscillator.
    ///
    /// @param channel the index of the oscillator to set the output for
    /// @param buffer the BLIPBuffer to output the given voice to
    /// @returns 0 if the output was set successfully, 1 if the index is invalid
    /// @details
    /// If buffer is NULL, the specified oscillator is muted and emulation
    /// accuracy is reduced.
    ///
    inline void set_output(unsigned channel, BLIPBuffer* buffer) {
        if (channel >= OSC_COUNT)  // make sure the channel is within bounds
            throw ChannelOutOfBoundsException(channel, OSC_COUNT);
        oscs[channel]->output = buffer;
    }

    /// @brief Assign all oscillator outputs to specified buffer. If buffer
    /// is NULL, silences all oscillators.
    ///
    /// @param buffer the single buffer to output the all the voices to
    ///
    inline void set_output(BLIPBuffer* buffer) {
        for (unsigned channel = 0; channel < OSC_COUNT; channel++)
            set_output(channel, buffer);
    }

    /// @brief Set the volume level of all oscillators.
    ///
    /// @param level the value to set the volume level to, where \f$1.0\f$ is
    /// full volume. Can be overdriven past \f$1.0\f$.
    ///
    inline void set_volume(double level = 1.0) {
        square_synth.set_volume(0.1128 * level);
        triangle.synth.set_volume(0.12765 * level);
        noise.synth.set_volume(0.0741 * level);
    }

    /// @brief Set treble equalization for the synthesizers.
    ///
    /// @param equalizer the equalization parameter for the synthesizers
    ///
    inline void set_treble_eq(const BLIPEqualizer& equalizer) {
        square_synth.set_treble_eq(equalizer);
        triangle.synth.set_treble_eq(equalizer);
        noise.synth.set_treble_eq(equalizer);
    }

    /// @brief Reset internal frame counter, registers, and all oscillators.
    ///
    /// @param pal_timing Use PAL timing if pal_timing is true, otherwise NTSC
    ///
    inline void reset(bool pal_timing = false) {
        // TODO: time pal frame periods exactly
        frame_period = pal_timing ? 8314 : 7458;

        pulse0.reset();
        pulse1.reset();
        triangle.reset();
        noise.reset();

        last_time = 0;
        osc_enables = 0;
        frame_delay = 1;
        write(0x4017, 0x00);
        write(0x4015, 0x00);
        // initialize sq1, sq2, tri, and noise, not DMC
        for (uint16_t addr = ADDR_START; addr <= 0x4009; addr++)
            write(addr, (addr & 3) ? 0x00 : 0x10);
    }

    /// @brief Write to data to a register.
    ///
    /// @param address the address of the register to write in
    /// `[0x4000, 0x4017]`, except `0x4014` and `0x4016`. See enum Register
    /// for more details.
    /// @param data the data to write to the register
    ///
    void write(uint16_t address, uint8_t data) {
        /// the number of elapsed CPU cycles to this write operation
        static constexpr blip_time_t time = 0;
        /// The length table to lookup length values from registers
        static constexpr unsigned char length_table[0x20] = {
            0x0A, 0xFE, 0x14, 0x02, 0x28, 0x04, 0x50, 0x06,
            0xA0, 0x08, 0x3C, 0x0A, 0x0E, 0x0C, 0x1A, 0x0E,
            0x0C, 0x10, 0x18, 0x12, 0x30, 0x14, 0x60, 0x16,
            0xC0, 0x18, 0x48, 0x1A, 0x10, 0x1C, 0x20, 0x1E
        };
        // make sure the given address is legal
        if (address < ADDR_START or address > ADDR_END)
            throw AddressSpaceException<uint16_t>(address, ADDR_START, ADDR_END);
        // run the emulator up to the given time
        run_until(time);
        if (address < 0x4010) {  // synthesize registers
            // Write to channel
            int osc_index = (address - ADDR_START) >> 2;
            Oscillator* osc = oscs[osc_index];
            int reg = address & 3;
            osc->regs[reg] = data;
            osc->reg_written[reg] = true;
            /*if (osc_index == 4) {
                // handle DMC specially
            } else */if (reg == 3) {
                // load length counter
                if ((osc_enables >> osc_index) & 1)
                    osc->length_counter = length_table[(data >> 3) & 0x1f];
                // reset square phase
                // DISABLED TO HACK SQUARE OSCILLATOR for VCV Rack
                // if (osc_index < 2)
                //     ((Pulse*) osc)->phase = Pulse::phase_range - 1;
            }
        } else if (address == 0x4015) {
            // Channel enables
            for (int i = OSC_COUNT; i--;) {
                if (!((data >> i) & 1))
                    oscs[i]->length_counter = 0;
            }
            osc_enables = data;
        } else if (address == 0x4017) {
            // Frame mode
            frame_mode = data;
            // mode 1
            frame_delay = (frame_delay & 1);
            frame = 0;
            if (!(data & 0x80)) {
                // mode 0
                frame = 1;
                frame_delay += frame_period;
            }
        }
    }

    /// @brief Run all oscillators up to specified time, end current frame,
    /// then start a new frame at time 0.
    ///
    /// @param end_time the time to run the oscillators until
    ///
    inline void end_frame(blip_time_t end_time) {
        if (end_time > last_time) run_until(end_time);
        last_time -= end_time;
    }
};

#endif  // DSP_2A03_HPP_
