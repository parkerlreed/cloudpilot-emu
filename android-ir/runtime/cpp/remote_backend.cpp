// SPDX-License-Identifier: GPL-3.0-or-later
#include "remote_backend.h"
#ifndef OMNI_REMOTE
#include "cputil.h"
#else
#include "EmHAL.h"
#include "EmSession.h"
#include "EmCPU68K.h"
#endif
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <fstream>
namespace {
 bool write(const std::string& path,const void* bytes,size_t size){
  std::string temp=path+".tmp";std::ofstream f(temp,std::ios::binary|std::ios::trunc);
  f.write(static_cast<const char*>(bytes),size);f.close();return f&&std::rename(temp.c_str(),path.c_str())==0;
 }
 void* copy(const void* data,size_t n){void* p=malloc(n);if(p)memcpy(p,data,n);return p;}
}
#ifdef OMNI_REMOTE
bool RemoteBackend::Restore(const std::vector<uint8_t>& data){
 if(!source.Deserialize(const_cast<uint8_t*>(data.data()),data.size())||std::string(source.GetDeviceId())!="PalmM130")return false;
 if(!cp.InitializeSession(copy(source.GetRomImage(),source.GetRomImageSize()),source.GetRomImageSize(),source.GetDeviceId())||!cp.ImportMemoryImage(source.GetMemoryImage(),source.GetMemoryImageSize())||!cp.LoadState(source.GetSavestate(),source.GetSavestateSize()))return false;
 audio.Reset(gSession->GetSystemCycles());
 EmHAL::onPwmChange.AddHandler([this](double frequency,double duty){
  const uint64_t cycle=gSession->GetSystemCycles()+gCPU68K->GetCurrentCycles();
  if(audioEnabled)audio.Update(cycle,cp.GetCyclesPerSecond(),frequency,duty);
  else {audio.Reset(cycle);audio.Update(cycle,cp.GetCyclesPerSecond(),frequency,duty);}
 });
 cp.SetIrBridgeEnabled(true);cp.SetClipboardIntegration(false);cp.SetNetworkRedirection(false);cp.LaunchAppByName("Remote");return true;
}
bool RemoteBackend::Save(const std::string& path){
 if(!cp.SaveState())return false;SessionImage s;s.SetDeviceId("PalmM130").SetRomImage(cp.GetRomPtr(),cp.GetRomSize()).SetMemoryImage(cp.GetMemoryPtr(),cp.GetMemorySize()).SetSavestate(cp.GetSavestatePtr(),cp.GetSavestateSize());
 return s.Serialize()&&write(path,s.GetSerializedImage(),s.GetSerializedImageSize());
}
void RemoteBackend::Cycle(uint64_t now){
 if(lastCycle&&!cp.IsSuspended())cp.RunEmulation(uint64_t(cp.GetCyclesPerSecond())*std::min<uint64_t>(now-lastCycle,50000)/1000000);
 lastCycle=now;
 if(audioEnabled)audio.Render(gSession->GetSystemCycles(),cp.GetCyclesPerSecond());
}
void RemoteBackend::SetAudioEnabled(bool enabled){audioEnabled=enabled;audio.Reset(gSession->GetSystemCycles());}
void RemoteBackend::GetAudio(std::vector<uint32_t>& samples){audio.Take(samples);}
void RemoteBackend::PenDown(int x,int y){cp.QueuePenMove(x/2,y/2);}
void RemoteBackend::PenUp(){cp.QueuePenUp();}
void RemoteBackend::KeyDown(unsigned k){if(k<=6)cp.QueueButtonDown(k-1);}
void RemoteBackend::KeyUp(unsigned k){if(k<=6)cp.QueueButtonUp(k-1);}
bool RemoteBackend::IsIrLearnPending(){return cp.IsIrLearnPending();}
bool RemoteBackend::CompleteIrLearn(const uint32_t* data,unsigned n){return cp.CompleteIrLearn(const_cast<uint32_t*>(data),n);}
void RemoteBackend::CancelIrLearn(){if(cp.IsIrLearnPending())cp.GetSuspendContext().Cancel();}
void* RemoteBackend::GetFrame(){
 if(!cp.IsScreenDirty())return nullptr;Frame& f=cp.CopyFrame();if(f.lineWidth!=160||f.lines!=160)return nullptr;
 frame.resize(160*160*4);const uint8_t* raw=f.GetBuffer();
 for(unsigned y=0;y<160;y++)for(unsigned x=0;x<160;x++){
  unsigned i=(y*160+x)*4,j=y*f.bytesPerLine;
  if(f.bpp==24){unsigned p=j+4*(x+f.margin);frame[i]=raw[p];frame[i+1]=raw[p+1];frame[i+2]=raw[p+2];}
  else if(f.bpp==16){unsigned p=j+2*(x+f.margin),v=raw[p]|raw[p+1]<<8;frame[i]=(v>>11)*255/31;frame[i+1]=((v>>5)&63)*255/63;frame[i+2]=(v&31)*255/31;}
  else {unsigned bit=(x+f.margin)*f.bpp,mask=(1<<f.bpp)-1,v=(raw[j+(bit>>3)]>>(8-f.bpp-(bit&7)))&mask;frame[i]=frame[i+1]=frame[i+2]=255-v*255/mask;}
  frame[i+3]=255;
 }
 return frame.data();
}
void RemoteBackend::ResetFrame(){cp.MarkScreenClean();}
#else
bool RemoteBackend::Restore(const std::vector<uint8_t>& data){
#ifdef NOVII_PV
 constexpr int expectedDevice=2;
 constexpr unsigned displayMode=6;
#else
 constexpr int expectedDevice=0;
 constexpr unsigned displayMode=5;
#endif
 if(!source.Deserialize(data.size(),data.data())||source.GetDeviceType()!=expectedDevice||source.GetDisplayMode()!=displayMode)return false;
 cp.SetRamSize(source.GetRamSize()).SetDisplayMode(displayMode).SetNand(source.GetNandSize(),const_cast<void*>(source.GetNand()));
 cp.SetMemory(source.GetMemorySize(),copy(source.GetMemory(),source.GetMemorySize()));cp.SetSavestate(source.GetSavestateSize(),copy(source.GetSavestate(),source.GetSavestateSize()));
 if(!cp.Launch(source.GetNorSize(),const_cast<void*>(source.GetNor())))return false;
 cp.SetIrBridgeEnabled(true);cp.SetPcmOutputEnabled(false);return true;
}
bool RemoteBackend::Save(const std::string& path){
 cp.Save();SessionFile5 s;s.SetDeviceType(cp.GetDeviceType()).SetDisplayMode(cp.GetDeviceType()==2?6:5).SetRamSize(cp.GetRamSize());
 s.SetNor(cp.GetRomDataSize(),cp.GetRomData());s.SetNand(cp.GetNandDataSize(),cp.GetNandData());s.SetMemory(cp.GetMemoryDataSize(),cp.GetMemoryData());s.SetSavestate(cp.GetSavestateSize(),cp.GetSavestateData());
 return s.Serialize()&&write(path,s.GetSerializedSession(),s.GetSerializedSessionSize());
}
// The ARM loop uses the system monotonic clock, while SDL ticks are relative
// to SDL initialization. Preserve the clock domain used by its constructor.
void RemoteBackend::Cycle(uint64_t){cp.Cycle(timestampUsec());}
void RemoteBackend::SetAudioEnabled(bool enabled){
 cp.SetPcmOutputEnabled(enabled);
 if(cp.PendingSamples())cp.PopQueuedSamples();
}
void RemoteBackend::GetAudio(std::vector<uint32_t>& samples){
 const unsigned count=cp.PendingSamples();samples.clear();
 if(count){const uint32_t* data=cp.PopQueuedSamples();samples.assign(data,data+count);}
}
void RemoteBackend::PenDown(int x,int y){cp.PenDown(x,y);}
void RemoteBackend::PenUp(){cp.PenUp();}
void RemoteBackend::KeyDown(unsigned k){cp.KeyDown(k);}
void RemoteBackend::KeyUp(unsigned k){cp.KeyUp(k);}
bool RemoteBackend::IsIrLearnPending(){return cp.IsIrLearnPending();}
bool RemoteBackend::CompleteIrLearn(const uint32_t* data,unsigned n){return cp.CompleteIrLearn(const_cast<uint32_t*>(data),n);}
void RemoteBackend::CancelIrLearn(){cp.CancelIrLearn();}
void* RemoteBackend::GetFrame(){return cp.GetFrame();}
void RemoteBackend::ResetFrame(){cp.ResetFrame();}
#endif
unsigned RemoteBackend::GetIrTransmitSize(){return cp.GetIrTransmitSize();}
unsigned RemoteBackend::GetIrTransmitFrequency(){return cp.GetIrTransmitFrequency();}
void* RemoteBackend::GetIrTransmitData(){return cp.GetIrTransmitData();}
void RemoteBackend::PopIrTransmit(){cp.PopIrTransmit();}
