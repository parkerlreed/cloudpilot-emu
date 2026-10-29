#include "patch68k.h"

#include "pace.h"
#include "novii_ir.h"
#include "syscall_68k.h"

static bool patchNVFS = false;
static bool patchKeyRates = false;

void patch68kInit(uint32_t patches) {
    patchNVFS = (patches & PATCH_68K_NVFS) != 0;
    patchKeyRates = (patches & PATCH_68K_KEY_RATES) != 0;
}

// rePalm's key auto-repeat never leaves EvtGetEvent once a key is held past the initial delay
// with a zero repeat period, which NoviiRemote sets. Clamp the period to one tick and let the
// trap proceed.
static void sanitizeKeyRates() {
    const uint32_t sp = paceGetAreg(7);
    if (!paceGet8(sp)) return;

    const uint32_t periodP = paceGet32(sp + 6);
    if (periodP && paceGet16(periodP) == 0) paceSet16(periodP, 1);
}

bool patch68kHandle(uint16_t trapWord) {
    if (noviiIrHandle(trapWord)) return true;
    if (patchKeyRates && trapWord == SYSCALL_68K_KEY_RATES) sanitizeKeyRates();
    // ATM this is a hack, but it currently is more efficient than a bitfield or pointer array,
    // and chances are that that's all we'll ever need.
    if (!patchNVFS) return false;
    if (trapWord < SYSCALL_68K_DM_SYNC || trapWord > SYSCALL_68K_DM_SYNC_DATABASE) return false;

    paceSetDreg(0, 0);
    return true;
}
