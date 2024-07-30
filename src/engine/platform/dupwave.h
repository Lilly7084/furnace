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

#ifndef _DUPWAVE_H
#define _DUPWAVE_H

#include "../dispatch.h"

class DivPlatformDupwave : public DivDispatch
{
public:
    // I'm not sure I understand
    struct Channel : public SharedChannel<int> {
        int wave, waveWriteCycle;
        bool onOff;
        Channel() :
            SharedChannel<int>(15),
            wave(0),
            waveWriteCycle(-1),
            onOff(true) {}

        void init(unsigned int rate);
        short runStep();

        int baseNote = 0;
        unsigned int rate = 44100;
        unsigned char detune = 0;
        float _freq = 0.0;
        float _phase = 0.0;
        unsigned int step = 0;
    };

    Channel chan[4];
    DivDispatchOscBuffer* oscBuf[4];
    bool isMuted[4];
    unsigned char regPool[8];

public:
    void acquire(short** buf, size_t len);
    int dispatch(DivCommand c);
    bool isVolGlobal();
    void* getChanState(int chan);
    DivMacroInt* getChanMacroInt(int ch);
    DivDispatchOscBuffer* getOscBuffer(int chan);
    unsigned char* getRegisterPool();
    int getRegisterPoolSize();
    void reset();
    void tick(bool sysTick = true);
    void muteChannel(int ch, bool mute);
    void setFlags(const DivConfig& flags);
    void notifyInsDeletion(void* ins);
    int getOutputCount();
    const char** getRegisterSheet();
    int init(DivEngine* parent, int channels, int sugRate, const DivConfig& flags);
    void quit();
    ~DivPlatformDupwave() {};
};

#endif
