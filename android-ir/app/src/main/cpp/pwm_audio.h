// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Collect PWM transitions on the emulator thread so a short click is retained
// even when both its start and stop occur during one emulation timeslice.
class PwmAudio {
public:
 static constexpr unsigned Rate=44100;
 void Reset(uint64_t cycle){lastCycle=cycle;fraction=0;phase=0;samples.clear();}
 void Update(uint64_t cycle,double clock,double hz,double duty){
  Render(cycle,clock);frequency=hz;dutyCycle=std::clamp(duty,0.0,1.0);
 }
 void Render(uint64_t cycle,double clock){
  if(cycle<lastCycle||clock<=0){Reset(cycle);return;}
  double count=std::min<double>(cycle-lastCycle,clock/10)*Rate/clock+fraction;
  lastCycle=cycle;const unsigned frames=static_cast<unsigned>(count);fraction=count-frames;
  const bool audible=frequency>0&&frequency<Rate/2&&dutyCycle>0&&dutyCycle<1;
  for(unsigned i=0;i<frames;i++){
   int16_t value=0;
   if(audible){value=static_cast<int16_t>(12000*((phase<dutyCycle?1.0:0.0)-dutyCycle));phase+=frequency/Rate;phase-=std::floor(phase);}
   const uint16_t bits=static_cast<uint16_t>(value);samples.push_back(uint32_t(bits)|(uint32_t(bits)<<16));
  }
 }
 void Take(std::vector<uint32_t>& destination){destination.clear();destination.swap(samples);}
private:
 uint64_t lastCycle=0;
 double frequency=0,dutyCycle=0,phase=0,fraction=0;
 std::vector<uint32_t> samples;
};
