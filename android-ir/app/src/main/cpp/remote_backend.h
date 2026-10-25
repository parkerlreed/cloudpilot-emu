// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#ifdef OMNI_REMOTE
#include "Cloudpilot.h"
#include "SessionImage.h"
#include "pwm_audio.h"
#else
#include "uarm.h"
#include "session/session_file5.h"
#endif
class RemoteBackend {
public:
 bool Restore(const std::vector<uint8_t>& data);
 bool Save(const std::string& path);
 void Cycle(uint64_t now);
 void PenDown(int x,int y);
 void PenUp();
 void KeyDown(unsigned key);
 void KeyUp(unsigned key);
 bool IsIrLearnPending();
 bool CompleteIrLearn(const uint32_t* data,unsigned size);
 void CancelIrLearn();
 unsigned GetIrTransmitSize();
 unsigned GetIrTransmitFrequency();
 void* GetIrTransmitData();
 void PopIrTransmit();
 void* GetFrame();
 void ResetFrame();
 void SetAudioEnabled(bool enabled);
 void GetAudio(std::vector<uint32_t>& samples);
#ifdef OMNI_REMOTE
 static constexpr unsigned Width=160;
private:
 SessionImage source;
 Cloudpilot cp;
 uint64_t lastCycle=0;
 bool audioEnabled=false;
 PwmAudio audio;
 std::vector<uint8_t> frame;
#else
 static constexpr unsigned Width=320;
private:
 SessionFile5 source;
 Uarm cp;
#endif
};
