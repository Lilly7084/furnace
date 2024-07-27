/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2024 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "dupwave.h"
#include "../engine.h"

#define rWrite(a,v) do { \
	regPool[(a)] = (v) & 0xff; \
} while(0)

// ??? needed for NOTE_PERIODIC
#define CHIP_DIVIDER 32

// .wave:
// bits 0-1 = waveform
// bit 2 = double
// bit 3 = mix

const uint64_t WAVETABLES[4][4] = {
	{
		0b0000000000000000000000000000000000000000000000000000000000000000,
		0b0000000000000000000000000000000000000000000000000000000000000000,
		0b1111111111111111111111111111111111111111111111111111111111111111,
		0b1111111111111111111111111111111111111111111111111111111111111111
	},
	{
		0b0000000000000000000000000000000000000000000000000000000000000000,
		0b1111111111111111111111111111111111111111111111111111111111111111,
		0b0000000000000000000000000000000011111111111111111111111111111111,
		0b0000000000000000000000000000000011111111111111111111111111111111
	},
	{
		0b0000000000000000000000000000000011111111111111111111111111111111,
		0b0000000000000000111111111111111100000000000000001111111111111111,
		0b0000000000000000111111111111111100000000111111110000000011111111,
		0b0000000000000000111111111111111100000000111111110000000011111111
	},
	{
		0b0000000000000000111111111111111100000000111111110000000011111111,
		0b0000000011111111000011110000111100000000111111110000111100001111,
		0b0000000011111111000011110000111100001111001100110000111100110011,
		0b0000000011111111000011110000111100001111001100110000111100110011
	}
};

int wt_get_raw(int w, unsigned int idx) {
	if (idx >= 256)
		return 1;
	return (WAVETABLES[w][idx >> 6] >> (0x3F - (idx & 0x3F))) & 1;
}

int wt_get(int w, unsigned int idx) {
	if (w == 0b0111)
		return (idx >= 32 && idx < 64) ? 2 : 0;
	unsigned int idx2 = 0xFF - idx;
	if (w & 0x4) {
		idx <<= 1;
		idx2 <<= 1;
	}
	short normal = wt_get_raw(w & 3, idx);
	short flip = wt_get_raw(w & 3, idx2);
	switch ((w >> 2) & 0x3)
	{
	case 0b00:
	case 0b01:
	default:
		return normal * 2;
	case 0b10:
		return normal + flip;
	case 0b11:
		return normal + 1 - flip;
	}
}

// ==================== Chip interface ====================

void DivPlatformDupwave::acquire(short** buf, size_t len) {
	for (size_t h = 0; h < len; h++) { // h = sample index
		short samp = 0;
		for (int i = 0; i < 4; i++) { // i = channel index

			clockDiv[i] += 16;
			if (clockDiv[i] >= chan[i].freq) {
				if (chan[i].freq > 0)
					clockDiv[i] %= chan[i].freq;
				else
					clockDiv[i] = 0;
				phaseCounter[i] = (phaseCounter[i] + 1) % 256;
			}

			if (isMuted[i] || !chan[i].active) {
				oscBuf[i]->data[oscBuf[i]->needle++] = 0;
				continue;
			}

			short voice = wt_get(chan[i].wave & 0xF, phaseCounter[i])
				* chan[i].outVol * 768;
			oscBuf[i]->data[oscBuf[i]->needle++] = voice;
			samp += voice;
		}
		buf[0][h] = samp;
	}
}

void DivPlatformDupwave::tick(bool sysTick) {
	for (int i = 0; i < 4; i++) {
		chan[i].std.next();

		if (chan[i].std.vol.had) {
			chan[i].outVol = MIN(15, chan[i].std.vol.val);
		}

		if (NEW_ARP_STRAT) {
			chan[i].handleArp();
		}
		else if (chan[i].std.arp.had) {
			if (!chan[i].inPorta) {
				chan[i].baseFreq = NOTE_PERIODIC(parent->calcArp(chan[i].note, chan[i].std.arp.val));
			}
			chan[i].freqChanged = true;
		}
		
		if (chan[i].std.wave.had) {
			chan[i].wave = (chan[i].wave & 0xC) | (chan[i].std.wave.val & 0x3);
		}

		if (chan[i].std.alg.had) {
			chan[i].wave = (chan[i].wave & 0x3) | ((chan[i].std.alg.val << 2) & 0xC);
		}

		if (chan[i].freqChanged) {
			// This thing was cargo-culted from the VIC20 core. I have no idea why it was there.
			chan[i].freq = parent->calcFreq(
				chan[i].baseFreq,
				chan[i].pitch,
				chan[i].fixedArp ? chan[i].baseNoteOverride : chan[i].arpOff,
				chan[i].fixedArp,
				true,
				0,
				chan[i].pitch2,
				chipClock,
				CHIP_DIVIDER);
			// if (isMuted[i])
			// 	chan[i].keyOn = false;
		}
	}
}

int DivPlatformDupwave::dispatch(DivCommand c) {
	switch (c.cmd)
	{
	case DIV_CMD_NOTE_ON: {
		DivInstrument* ins = parent->getIns(chan[c.chan].ins, DIV_INS_DUPWAVE);
		if (c.value != DIV_NOTE_NULL) {
			chan[c.chan].baseFreq = NOTE_PERIODIC(c.value);
			chan[c.chan].freqChanged = true;
			chan[c.chan].note = c.value;
		}
		chan[c.chan].active = true;
		chan[c.chan].macroInit(ins);
		break;
	}
	case DIV_CMD_NOTE_OFF:
		chan[c.chan].active = false;
		chan[c.chan].macroInit(NULL);
		break;
	case DIV_CMD_NOTE_OFF_ENV:
	case DIV_CMD_ENV_RELEASE:
		chan[c.chan].std.release();
		break;
	case DIV_CMD_INSTRUMENT:
		if (chan[c.chan].ins != c.value || c.value2 == 1) {
			chan[c.chan].ins = c.value;
		}
		break;
	case DIV_CMD_VOLUME:
		chan[c.chan].vol = c.value;
		if (chan[c.chan].vol > 15)
			chan[c.chan].vol = 15;
		break;
	case DIV_CMD_GET_VOLUME:
		return chan[c.chan].vol;
	case DIV_CMD_PITCH:
		chan[c.chan].pitch = c.value;
		chan[c.chan].freqChanged = true;
		break;
	case DIV_CMD_WAVE:
		chan[c.chan].wave = c.value & 0x0F;
		break;
		//DIV_CMD_NOTE_PORTA
		//DIV_CMD_LEGATO
		//DIV_CMD_PRE_PORTA
	case DIV_CMD_GET_VOLMAX:
		return 15;
	case DIV_CMD_MACRO_OFF:
		chan[c.chan].std.mask(c.value, true);
		break;
	case DIV_CMD_MACRO_ON:
		chan[c.chan].std.mask(c.value, false);
		break;
	case DIV_CMD_MACRO_RESTART:
		chan[c.chan].std.restart(c.value);
		break;
	default:
		break;
	}
	return 1;
}

void DivPlatformDupwave::muteChannel(int ch, bool mute) {
	isMuted[ch] = mute;
	if (chan[ch].onOff) {
		if (mute) {
			chan[ch].keyOff = true;
		}
		else if (chan[ch].active) {
			chan[ch].keyOn = true;
		}
	}
}

// forceIns?

void DivPlatformDupwave::reset() {
	memset(regPool, 0, 8);
	for (int i = 0; i < 4; i++) {
		chan[i] = Channel();
		chan[i].std.setEngine(parent);
		clockDiv[i] = 0;
		phaseCounter[i] = 0;
	}
}

void DivPlatformDupwave::setFlags(const DivConfig & flags) {
	// I don't know what value this should be
	chipClock = COLOR_NTSC * 2.0 / 7.0;
	CHECK_CUSTOM_CLOCK; // Is this right?
	rate = chipClock / 4;
	for (int i = 0; i < 4; i++)
		oscBuf[i]->rate = rate;
}

int DivPlatformDupwave::init(DivEngine * p, int channels, int sugRate, const DivConfig & flags) {
	// TODO: Additional setup
	parent = p;
	for (int i = 0; i < 4; i++) {
		isMuted[i] = false;
		oscBuf[i] = new DivDispatchOscBuffer;
	}
	setFlags(flags);
	reset();
	return 4;
}

// ==================== Furnace boilerplate ====================

//
// Constants, properties, metadata, whatever you call it idk
//

const char* regCheatSheetDupwave[] = {
	"CH1Freq", "00",
	"CH1Wave", "01",
	"CH2Freq", "02",
	"CH2Wave", "03",
	"CH3Freq", "04",
	"CH3Wave", "05",
	"CH4Freq", "06",
	"CH4Wave", "07",
	NULL
};

const char** DivPlatformDupwave::getRegisterSheet() {
	return regCheatSheetDupwave;
}

bool DivPlatformDupwave::isVolGlobal() {
	return true;
}

int DivPlatformDupwave::getRegisterPoolSize() {
	return 8;
}

int DivPlatformDupwave::getOutputCount() {
	return 1;
}

//
// Getters and setters
//

void* DivPlatformDupwave::getChanState(int ch) {
	return &chan[ch];
}

DivMacroInt* DivPlatformDupwave::getChanMacroInt(int ch) {
	return &chan[ch].std;
}

DivDispatchOscBuffer* DivPlatformDupwave::getOscBuffer(int ch) {
	return oscBuf[ch];
}

unsigned char* DivPlatformDupwave::getRegisterPool() {
	return regPool;
}

//
// Other stuff
//

void DivPlatformDupwave::poke(unsigned int addr, unsigned short val) {
	rWrite(addr, val);
}

void DivPlatformDupwave::poke(std::vector<DivRegWrite>& wlist) {
	for (DivRegWrite& i : wlist)
		rWrite(i.addr, i.val);
}

void DivPlatformDupwave::notifyInsDeletion(void* ins) {
	for (int i = 0; i < 4; i++)
		chan[i].std.notifyInsDeletion((DivInstrument*)ins);
}

void DivPlatformDupwave::quit() {
	for (int i = 0; i < 4; i++)
		delete oscBuf[i];
}
