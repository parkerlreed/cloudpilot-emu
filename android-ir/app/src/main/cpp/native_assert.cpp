// SPDX-License-Identifier: GPL-3.0-or-later
#include <android/log.h>
#include <cstdlib>
extern "C" void MyAssertFailed(const char* assertion,const char* file,unsigned int line){
 __android_log_print(ANDROID_LOG_FATAL,"PalmNative","%s:%u: %s",file,line,assertion);std::abort();
}
