// SPDX-License-Identifier: GPL-3.0-or-later
#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>
#include <android/log.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include "remote_backend.h"
namespace {
std::atomic<bool> saveRequested{false},exitRequested{false};
std::mutex captureMutex;
std::vector<uint32_t> capture;
bool captureReady=false,captureFailed=false;
JNIEnv* jni; jobject activity;
// Upstream Uarm owns process-lifetime device allocations. Keep one instance
// across SDL activity recreation, along with the ROM/NAND backing storage.
RemoteBackend* emulator=nullptr;
std::vector<uint8_t> lastFrame;
jmethodID messageMethod,savedMethod,learnMethod,transmitMethod;
void message(const std::string& text){
 __android_log_print(ANDROID_LOG_INFO,"NoviiNative","%s",text.c_str());
 jstring s=jni->NewStringUTF(text.c_str());jni->CallVoidMethod(activity,messageMethod,s);jni->DeleteLocalRef(s);
}
std::vector<uint8_t> readFile(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
std::vector<uint8_t> readAsset(const char* name){
 SDL_RWops* f=SDL_RWFromFile(name,"rb");if(!f)return {};const Sint64 n=SDL_RWsize(f);
 if(n<=0){SDL_RWclose(f);return {};}std::vector<uint8_t> data(n);
 if(SDL_RWread(f,data.data(),1,data.size())!=data.size())data.clear();SDL_RWclose(f);return data;
}
SDL_Texture* skin(SDL_Renderer* r,const char* name){SDL_RWops* f=SDL_RWFromFile(name,"rb");if(!f)return nullptr;SDL_Surface* s=SDL_LoadBMP_RW(f,1);if(!s)return nullptr;SDL_Texture* t=SDL_CreateTextureFromSurface(r,s);SDL_FreeSurface(s);return t;}
unsigned hardware(float x,float y){if(x<60)return 1;if(x<120)return 2;if(x>=260)return 4;if(x>=200)return 3;if(x<144)return 7;if(x>176)return 8;if(y<18)return 5;if(y>42)return 6;return 9;}
unsigned keyboard(SDL_Keycode k){switch(k){case SDLK_F1:return 1;case SDLK_F2:return 2;case SDLK_F3:return 3;case SDLK_F4:return 4;case SDLK_UP:return 5;case SDLK_DOWN:return 6;case SDLK_LEFT:return 7;case SDLK_RIGHT:return 8;case SDLK_RETURN:return 9;default:return 0;}}
}
extern "C" JNIEXPORT void JNICALL Java_local_omniremote_NativeActivity_captureComplete(JNIEnv* env,jobject,jintArray values,jstring error){
 std::lock_guard<std::mutex> lock(captureMutex);capture.clear();captureFailed=error!=nullptr||values==nullptr;
 if(values){jsize n=env->GetArrayLength(values);capture.resize(n);env->GetIntArrayRegion(values,0,n,reinterpret_cast<jint*>(capture.data()));}captureReady=true;
}
extern "C" JNIEXPORT void JNICALL Java_local_omniremote_NativeActivity_requestSave(JNIEnv*,jobject,jboolean exit){if(exit)exitRequested=true;saveRequested=true;}
extern "C" int SDL_main(int argc,char** argv){
 saveRequested=false;exitRequested=false;{std::lock_guard<std::mutex> lock(captureMutex);captureReady=false;capture.clear();}
 jni=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());activity=static_cast<jobject>(SDL_AndroidGetActivity());jclass type=jni->GetObjectClass(activity);
 messageMethod=jni->GetMethodID(type,"showMessage","(Ljava/lang/String;)V");savedMethod=jni->GetMethodID(type,"saved","(ZZ)V");learnMethod=jni->GetMethodID(type,"learn","()V");transmitMethod=jni->GetMethodID(type,"transmit","(I[I)V");jni->DeleteLocalRef(type);
 const std::string sessionPath=argc>1?argv[1]:"session-e2.img";
 SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE,"0");SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
 if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS)!=0){message(SDL_GetError());return 1;}
 SDL_Window* window=SDL_CreateWindow("Palm IR Remote",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,320,502,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_FULLSCREEN_DESKTOP);
 SDL_Renderer* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
 if(!window||!renderer){message(SDL_GetError());SDL_Quit();return 1;}SDL_RenderSetLogicalSize(renderer,320,502);
 SDL_Texture* lcd=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STREAMING,RemoteBackend::Width,RemoteBackend::Width),*graffiti=skin(renderer,"graffiti.bmp"),*buttons=skin(renderer,"buttons.bmp");
 if(!emulator){
  auto data=readFile(sessionPath);if(data.empty())data=readAsset("session.img");
  emulator=new RemoteBackend();
  if(!emulator->Restore(data)){message("Could not restore the Palm session");SDL_Quit();return 1;}
 }
 RemoteBackend& cp=*emulator;
 SDL_AudioSpec requested{},actual{};
 requested.freq=44100;requested.format=AUDIO_S16SYS;requested.channels=2;requested.samples=512;
 SDL_AudioDeviceID audioDevice=SDL_OpenAudioDevice(nullptr,0,&requested,&actual,SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
 cp.SetAudioEnabled(audioDevice!=0);
 if(audioDevice){SDL_PauseAudioDevice(audioDevice,0);__android_log_print(ANDROID_LOG_INFO,"NoviiNative","Palm audio opened: %d Hz, %u channels, %u frames",actual.freq,actual.channels,actual.samples);}
 else message(std::string("Could not open Palm audio: ")+SDL_GetError());
 std::vector<uint32_t> audioSamples;bool loggedAudio=false;
 __android_log_print(ANDROID_LOG_INFO,"NoviiNative","Native Palm ready; SDL2 video=%s",SDL_GetCurrentVideoDriver());
 bool running=true,background=false,learningRequested=false,pen=false,haveFrame=false,fingerActive=false;unsigned pressed=0;SDL_FingerID finger=0;
 if(!lastFrame.empty()){SDL_UpdateTexture(lcd,nullptr,lastFrame.data(),RemoteBackend::Width*4);haveFrame=true;}
 auto release=[&](){if(pen){cp.PenUp();pen=false;}if(pressed){cp.KeyUp(pressed);pressed=0;}};
 auto move=[&](float x,float y){cp.PenDown(SDL_clamp(int(x),0,319),SDL_clamp(int(y>=320?SDL_max(320,y-2):y),0,439));};
 auto down=[&](float x,float y){if(cp.IsIrLearnPending()||x<0||x>=320||y<0||y>=502)return;if(y>=442){pressed=hardware(x,y-442);cp.KeyDown(pressed);}else{pen=true;move(x,y);}};
 while(running){
  SDL_Event e;while(SDL_PollEvent(&e))switch(e.type){
   case SDL_QUIT:exitRequested=true;saveRequested=true;break;
   case SDL_APP_WILLENTERBACKGROUND:background=true;saveRequested=true;release();cp.SetAudioEnabled(false);if(audioDevice){SDL_ClearQueuedAudio(audioDevice);SDL_PauseAudioDevice(audioDevice,1);}break;
   case SDL_APP_DIDENTERFOREGROUND:background=false;cp.SetAudioEnabled(audioDevice!=0);if(audioDevice)SDL_PauseAudioDevice(audioDevice,0);break;
   case SDL_FINGERDOWN:if(!fingerActive){fingerActive=true;finger=e.tfinger.fingerId;down(e.tfinger.x*320,e.tfinger.y*502);}break;
   case SDL_FINGERMOTION:if(fingerActive&&e.tfinger.fingerId==finger&&pen&&!cp.IsIrLearnPending())move(e.tfinger.x*320,e.tfinger.y*502);break;
   case SDL_FINGERUP:if(fingerActive&&e.tfinger.fingerId==finger){release();fingerActive=false;}break;
   case SDL_MOUSEBUTTONDOWN:if(e.button.which!=SDL_TOUCH_MOUSEID&&e.button.button==SDL_BUTTON_LEFT)down(e.button.x,e.button.y);break;
   case SDL_MOUSEMOTION:if(pen&&e.motion.which!=SDL_TOUCH_MOUSEID&&!cp.IsIrLearnPending())move(e.motion.x,e.motion.y);break;
   case SDL_MOUSEBUTTONUP:if(e.button.which!=SDL_TOUCH_MOUSEID)release();break;
   case SDL_KEYDOWN:if(e.key.keysym.sym==SDLK_AC_BACK){exitRequested=true;saveRequested=true;}else if(!e.key.repeat){unsigned k=keyboard(e.key.keysym.sym);if(k)cp.KeyDown(k);}break;
   case SDL_KEYUP:{unsigned k=keyboard(e.key.keysym.sym);if(k)cp.KeyUp(k);break;}
  }
  {std::lock_guard<std::mutex> lock(captureMutex);if(captureReady){if(cp.IsIrLearnPending()){if(captureFailed)cp.CancelIrLearn();else if(!cp.CompleteIrLearn(capture.data(),capture.size()))message("Capture did not fit Novii's buffer");}captureReady=false;learningRequested=false;}}
  if(exitRequested&&cp.IsIrLearnPending()){cp.CancelIrLearn();learningRequested=false;}
  if(saveRequested&&!cp.IsIrLearnPending()){release();bool exit=exitRequested.load(),ok=cp.Save(sessionPath);saveRequested=false;jni->CallVoidMethod(activity,savedMethod,exit,ok);if(exit&&ok)running=false;else if(!ok)exitRequested=false;}
  if(!running)break;
  if(!background&&!cp.IsIrLearnPending())cp.Cycle(SDL_GetTicks64()*1000);
  if(audioDevice&&!background){
   cp.GetAudio(audioSamples);
   if(!audioSamples.empty()){
    // Bound latency after an Android scheduling stall; never replay old audio.
    if(SDL_GetQueuedAudioSize(audioDevice)>44100*4/5)SDL_ClearQueuedAudio(audioDevice);
    if(SDL_QueueAudio(audioDevice,audioSamples.data(),audioSamples.size()*4)!=0)message(SDL_GetError());
    if(!loggedAudio){for(uint32_t sample:audioSamples)if(sample){__android_log_print(ANDROID_LOG_INFO,"NoviiNative","Palm sound reached SDL audio (%zu frames)",audioSamples.size());loggedAudio=true;break;}}
   }
  }
  if(cp.IsIrLearnPending()&&!learningRequested){learningRequested=true;jni->CallVoidMethod(activity,learnMethod);}
  while(cp.GetIrTransmitSize()){unsigned n=cp.GetIrTransmitSize();jintArray p=jni->NewIntArray(n);jni->SetIntArrayRegion(p,0,n,static_cast<const jint*>(cp.GetIrTransmitData()));jni->CallVoidMethod(activity,transmitMethod,cp.GetIrTransmitFrequency(),p);jni->DeleteLocalRef(p);cp.PopIrTransmit();}
  if(background){SDL_Delay(20);continue;}
  if(void* frame=cp.GetFrame()){lastFrame.assign(static_cast<uint8_t*>(frame),static_cast<uint8_t*>(frame)+RemoteBackend::Width*RemoteBackend::Width*4);SDL_UpdateTexture(lcd,nullptr,frame,RemoteBackend::Width*4);cp.ResetFrame();haveFrame=true;}
  SDL_SetRenderDrawColor(renderer,24,32,42,255);SDL_RenderClear(renderer);SDL_Rect lr{0,0,320,320},gr{0,322,320,120},br{0,442,320,60};
  if(haveFrame)SDL_RenderCopy(renderer,lcd,nullptr,&lr);if(graffiti)SDL_RenderCopy(renderer,graffiti,nullptr,&gr);if(buttons)SDL_RenderCopy(renderer,buttons,nullptr,&br);SDL_RenderPresent(renderer);
 }
 cp.SetAudioEnabled(false);if(audioDevice)SDL_CloseAudioDevice(audioDevice);
 SDL_DestroyTexture(buttons);SDL_DestroyTexture(graffiti);SDL_DestroyTexture(lcd);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();jni->DeleteLocalRef(activity);activity=nullptr;return 0;
}
