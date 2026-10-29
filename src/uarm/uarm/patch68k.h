#ifndef _PATCH_68K_H_
#define _PATCH_68K_H_

#include <cstdint>

#include "device_type5.h"

#define PATCH_68K_NVFS 1
#define PATCH_68K_KEY_RATES 2

void patch68kInit(uint32_t patches);

bool patch68kHandle(uint16_t trapWord);

#endif  // _PATCH_68K_H_
