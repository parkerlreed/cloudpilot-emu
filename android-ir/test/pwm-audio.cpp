#include "../runtime/cpp/pwm_audio.h"
#include <cassert>
#include <algorithm>
int main(){
 PwmAudio audio;std::vector<uint32_t> samples;
 audio.Reset(0);
 // A 10 ms beep starts and stops inside a single 20 ms emulator frame.
 audio.Update(10000,1000000,1000,0.5);
 audio.Update(20000,1000000,0,0);
 audio.Render(30000,1000000);audio.Take(samples);
 assert(samples.size()==1323);
 assert(std::all_of(samples.begin(),samples.begin()+441,[](auto s){return s==0;}));
 assert(std::any_of(samples.begin()+441,samples.begin()+882,[](auto s){return s!=0;}));
 assert(std::all_of(samples.begin()+882,samples.end(),[](auto s){return s==0;}));
 for(auto sample:samples)assert(uint16_t(sample)==uint16_t(sample>>16));
 audio.Take(samples);assert(samples.empty());
 audio.Reset(900000);audio.Render(910000,1000000);audio.Take(samples);
 assert(samples.size()==441);
 assert(std::all_of(samples.begin(),samples.end(),[](auto s){return s==0;}));
}
