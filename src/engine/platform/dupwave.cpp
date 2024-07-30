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

#define CLOCK_DIVIDER 4
#define WAVE_DIVIDER 256

 // wav:
 // bits 0-1: filter mode
 // bit 2: double
 // bit 3: mix
int filter_get_base(int pos, int wav)
{
#define bit(x,n) (((x)>>(n))&1)
	// Exception: Wave 0 + mix (0100, would be silent otherwise) is 12.5% pulse
	if (wav == 0b1000)
		return (pos >> 5) > 0;
	unsigned char pos1 = pos, pos2 = pos ^ 0xFF; // Reversed for option with bit 3
	// Double
	if (wav & 4) {
		pos1 = bit(pos1, 7) ? 0xFF : (pos1 << 1);
		pos2 = bit(pos2, 7) ? 0xFF : (pos2 << 1);
	}
	// Filter modes
	for (int i = 0; i < (wav & 3); i++) {
		pos1 = bit(pos1, 7) ? (pos1 << 2) : (pos1 << 1);
		pos2 = bit(pos2, 7) ? (pos2 << 2) : (pos2 << 1);
	}
	// Convert phase to square
	pos1 = bit(pos1, 7);
	pos2 = bit(pos2, 7);
	// Mix
	if (wav & 8)
		return (wav & 4) ? (pos1 + 1 - pos2) : (pos1 + pos2);
	else
		return pos1 << 1;
#undef bit
}

// Un-skissued version of DivEngine::calcBaseFreq that doesn't do linear pitch
// Tuning in Hz, value of A-4, nominally 440Hz
// clock = sample rate in Hz
// note = 0 for C-0, +1 for 1 semitone
double expBaseFreq(double tuning, double clock, int note)
{
	double base = tuning * pow(2.0, (float)(note - 180) / 48.0);
	double step = base * WAVE_DIVIDER / clock;
	return step;
}

void DivPlatformDupwave::Channel::init(unsigned int rate)
{
	this->rate = rate;
	this->detune = 0;
	this->_freq = 0.0;
	this->_phase = 0.0;
	this->step = 0;
}

short DivPlatformDupwave::Channel::runStep()
{
	_phase += _freq;
	if (_phase >= 1.0f) {
		_phase -= 1.0f;
		step = (step + 1) % 256;
	}
	return (float)filter_get_base(step, wave) * outVol * 32.0f;
}

void DivPlatformDupwave::Channel::finishBlock()
{
}

// ==================== Furnace stuff ====================

void DivPlatformDupwave::acquire(short** buf, size_t len)
{
	for (size_t h = 0; h < len; h++) { // h = sample index
		short samp = 0;
		for (int i = 0; i < 4; i++) { // i = channel index
			short voice = chan[i].runStep();
			oscBuf[i]->data[oscBuf[i]->needle++] = voice * 8;
			samp += voice;
		}
		buf[0][h] = samp;
	}
	for (int i = 0; i < 4; i++)
		chan[i].finishBlock();
}

void DivPlatformDupwave::tick(bool sysTick)
{
#define b32repl(x,y,off,len) do { \
	int m = (1 << len) - 1; \
	x &= ~(m << off); \
	x |= (y & m) << off; \
} while(0)

	for (int i = 0; i < 4; i++) {
		auto& ch = chan[i];
		ch.std.next();

		if (ch.std.wave.had)
			b32repl(ch.wave, ch.std.wave.val, 0, 2);

		if (ch.std.alg.had)
			b32repl(ch.wave, ch.std.alg.val, 2, 2);

		if (sysTick)
			ch.outVol = ch.active ? ch.vol : 0;
	}
}

int DivPlatformDupwave::dispatch(DivCommand c)
{
	auto& ch = chan[c.chan];
	switch (c.cmd)
	{
	case DIV_CMD_NOTE_ON: {
		DivInstrument* ins = parent->getIns(ch.ins, DIV_INS_DUPWAVE);
		if (c.value != DIV_NOTE_NULL) {
			ch.freqChanged = true;
			ch.note = c.value * 4 + 0; // TODO feed in detune
			ch._freq = expBaseFreq(parent->song.tuning, rate, ch.note);
		}
		ch.active = true;
		ch.macroInit(ins);
		break;
	}
	case DIV_CMD_NOTE_OFF:
		ch.active = false;
		ch.macroInit(NULL);
		break;
	case DIV_CMD_NOTE_OFF_ENV:
	case DIV_CMD_ENV_RELEASE:
		ch.std.release();
		break;
	case DIV_CMD_INSTRUMENT:
		if (ch.ins != c.value || c.value2 == 1) {
			ch.ins = c.value;
		}
		break;
	case DIV_CMD_VOLUME:
		ch.vol = MIN(c.value, 15);
		break;
	case DIV_CMD_GET_VOLUME:
		return ch.vol;
	case DIV_CMD_PITCH:
		ch.pitch = c.value;
		ch.freqChanged = true;
		break;
	case DIV_CMD_WAVE:
		ch.wave = c.value & 0x0F;
		break;
		//DIV_CMD_NOTE_PORTA
		//DIV_CMD_LEGATO
		//DIV_CMD_PRE_PORTA
	case DIV_CMD_GET_VOLMAX:
		return 15;
	case DIV_CMD_MACRO_OFF:
		ch.std.mask(c.value, true);
		break;
	case DIV_CMD_MACRO_ON:
		ch.std.mask(c.value, false);
		break;
	case DIV_CMD_MACRO_RESTART:
		ch.std.restart(c.value);
		break;
	default:
		break;
	}
	return 1;
}

// forceIns?

void DivPlatformDupwave::reset()
{
	memset(regPool, 0, 8);
	for (int i = 0; i < 4; i++) {
		chan[i] = Channel();
		chan[i].std.setEngine(parent);
		chan[i].init(rate);
	}
}

void DivPlatformDupwave::setFlags(const DivConfig & flags)
{
	// I don't know what value this should be
	chipClock = 2119040;
	CHECK_CUSTOM_CLOCK;
	rate = chipClock / CLOCK_DIVIDER;
	for (int i = 0; i < 4; i++)
		oscBuf[i]->rate = rate;
}

int DivPlatformDupwave::init(DivEngine * p, int channels, int sugRate, const DivConfig & flags)
{
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

void DivPlatformDupwave::notifyInsDeletion(void* ins)
{
	for (int i = 0; i < 4; i++)
		chan[i].std.notifyInsDeletion((DivInstrument*)ins);
}

void DivPlatformDupwave::quit()
{
	for (int i = 0; i < 4; i++)
		delete oscBuf[i];
}

// Getters and setters

const char** DivPlatformDupwave::getRegisterSheet()
{
	return regCheatSheetDupwave;
}

bool DivPlatformDupwave::isVolGlobal()
{
	return true;
}

int DivPlatformDupwave::getRegisterPoolSize()
{
	return 8;
}

int DivPlatformDupwave::getOutputCount()
{
	return 1;
}

void* DivPlatformDupwave::getChanState(int ch)
{
	return &chan[ch];
}

DivMacroInt* DivPlatformDupwave::getChanMacroInt(int ch)
{
	return &chan[ch].std;
}

DivDispatchOscBuffer* DivPlatformDupwave::getOscBuffer(int ch)
{
	return oscBuf[ch];
}

unsigned char* DivPlatformDupwave::getRegisterPool()
{
	return regPool;
}

void DivPlatformDupwave::muteChannel(int ch, bool mute)
{
	isMuted[ch] = mute;
}
