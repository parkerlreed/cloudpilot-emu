#ifndef PATCH_MODULE_OMNIREMOTE_H
#define PATCH_MODULE_OMNIREMOTE_H
#include "EmPatchModule.h"

// Opt-in bridge for ORLibRecordSample in OmniRemote Pro 2.x.
class PatchModuleOmniRemote : public EmPatchModule {
 public:
    PatchModuleOmniRemote();
    static bool enabled;
    static HeadpatchProc NoviiHeadpatch(uint16 trap);
    static bool IsPending();
    static unsigned GetRate();
    static bool Complete(const uint32* timings, unsigned count);
    static void RegisterWrite(emuptr address, unsigned size, uint32 value);
    static unsigned TransmitSize();
    static unsigned TransmitFrequency();
    static void* TransmitData();
    static void PopTransmit();
};
#endif
