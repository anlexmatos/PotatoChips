// The NES Oscillators VCVRack plugin.
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

#include "rack.hpp"

#ifndef PLUGIN_HPP
#define PLUGIN_HPP

using namespace rack;

/// the base clock rate of the VCV Rack environment
static constexpr uint32_t CLOCK_RATE = 768000;

/// the number of potential polyphonic channels
static constexpr unsigned POLYPHONY_CHANNELS = 16;

/// the global instance of the plug-in
extern rack::Plugin* plugin_instance;
// the global instance of each the 2A03 module
extern rack::Model *modelChip2A03;
// the global instance of each the VRC6 module
extern rack::Model *modelChipVRC6;
// the global instance of each the FME7 module
extern rack::Model *modelChipFME7;
// the global instance of each the 106 module
extern rack::Model *modelChip106;
// the global instance of each the SN76489 module
extern rack::Model *modelChipSN76489;
// the global instance of each the GBS module
extern rack::Model *modelChipGBS;
// the global instance of each the TurboGrafx16 module
extern rack::Model *modelChipTurboGrafx16;
// the global instance of each the SCC module
extern rack::Model *modelChipSCC;
// the global instance of each the AY-3-8910 module
extern rack::Model *modelChipAY_3_8910;
// the global instance of each the POKEY module
extern rack::Model *modelChipPOKEY;
// the global instance of each the 2413 module
extern rack::Model *modelChip2413;

#endif  // PLUGIN_HPP
