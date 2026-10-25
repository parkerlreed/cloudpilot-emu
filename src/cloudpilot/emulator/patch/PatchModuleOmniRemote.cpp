#include "PatchModuleOmniRemote.h"

#include <cmath>
#include <vector>
#include <deque>
#include "EmHAL.h"
#include "EmCommon.h"
#include "EmMemory.h"
#include "Marshal.h"
#include "SuspendContext.h"
#include "SuspendManager.h"

bool PatchModuleOmniRemote::enabled = false;

namespace {
    struct Request {
        emuptr buffer = 0, length = 0;
        uint16 capacity = 0;
        uint32 rate = 0;
        bool pending = false;
        emuptr noviiObject = 0;
    } request;

    struct Transmission { unsigned frequency; std::vector<uint32> timings; };
    std::deque<Transmission> transmitQueue;
    uint16 uartControl = 0, uartBaud = 0, uartMisc = 0;
    uint8 portSelect = 0xff;
    struct Run { bool mark; unsigned samples; };
    std::vector<Run> runs;
    double sampleUs = 0, carrierHz = 0;
    unsigned sampleCount = 0;

    void FinishTransmission() {
        if (runs.empty()) return;
        Transmission tx{static_cast<unsigned>(std::lround(carrierHz)), {}};
        for (unsigned i = runs[0].mark ? 0 : 1; i < runs.size(); ++i)
            tx.timings.push_back(std::lround(runs[i].samples * sampleUs));
        if (!tx.timings.empty() && transmitQueue.size() < 8) transmitQueue.push_back(std::move(tx));
        runs.clear(); sampleCount = 0;
    }

    void ReturnStatus(uint8 status, uint16 length) {
        CALLED_SETUP("UInt8", "void");
        if (request.noviiObject) {
            PUT_RESULT_VAL(UInt8, status == 1 ? 0 : 100);
        } else {
            EmMemPut16(request.length, length);
            PUT_RESULT_VAL(UInt8, status);
        }
        request.pending = false;
    }

    class IrLearnContext : public SuspendContext {
     public:
        Kind GetKind() const override { return Kind::irLearn; }
        void Cancel() override { ReturnStatus(2, 0); ResumeExecution(); }
        void Complete(uint16 length) { ReturnStatus(1, length); ResumeExecution(); }
    };

    // These private traps are used only by the version-checked Novii adapter.
    CallROMType NoviiLearn() {
        CALLED_SETUP("UInt8", "UInt32 magic, UInt32 object");
        CALLED_GET_PARAM_VAL(UInt32, magic);
        CALLED_GET_PARAM_VAL(UInt32, object);
        if (!PatchModuleOmniRemote::enabled || magic != 0x4e524952) return kExecuteROM;
        if (!object) { PUT_RESULT_VAL(UInt8, 11); return kSkipROM; }
        request = {};
        request.noviiObject = object;
        request.rate = 76000;
        request.pending = true;
        SuspendManager::Suspend<IrLearnContext>();
        return kSkipROM;
    }

    CallROMType NoviiTransmit() {
        CALLED_SETUP("UInt8", "UInt32 magic, UInt32 object, UInt16 repeats");
        CALLED_GET_PARAM_VAL(UInt32, magic);
        CALLED_GET_PARAM_VAL(UInt32, object);
        CALLED_GET_PARAM_VAL(UInt16, repeats);
        (void)repeats;
        if (!PatchModuleOmniRemote::enabled || magic != 0x4e524952) return kExecuteROM;
        if (!object) { PUT_RESULT_VAL(UInt8, 11); return kSkipROM; }
        const unsigned count = EmMemGet16(object + 6), tickNs = EmMemGet16(object + 0x12),
                       halfPeriod = EmMemGet16(object + 0x14);
        if (!count || count > 300 || !tickNs || !halfPeriod) {
            PUT_RESULT_VAL(UInt8, 11); return kSkipROM;
        }
        Transmission tx{static_cast<unsigned>(std::lround(1e9 / (2.0 * tickNs * halfPeriod))), {}};
        bool lastMark = false;
        for (unsigned i = 0; i < count; ++i) {
            const int32 duration = static_cast<int32>(EmMemGet32(object + 0x4c6 + 4 * i));
            if (!duration || duration == INT32_MIN) continue;
            const bool mark = duration > 0;
            const uint64 us = std::llround(std::abs(static_cast<double>(duration)) * tickNs / 1000.0);
            if (!us || us > 2000000) { PUT_RESULT_VAL(UInt8, 11); return kSkipROM; }
            if (tx.timings.empty() && !mark) continue;
            if (!tx.timings.empty() && mark == lastMark) tx.timings.back() += us;
            else tx.timings.push_back(us);
            lastMark = mark;
        }
        if (tx.frequency < 10000 || tx.frequency > 100000 || tx.timings.empty() || transmitQueue.size() >= 8) {
            PUT_RESULT_VAL(UInt8, 11); return kSkipROM;
        }
        // A button action emits one complete code; held-button repeats can be
        // added separately without blocking the Palm event loop.
        transmitQueue.push_back(std::move(tx));
        PUT_RESULT_VAL(UInt8, 0);
        return kSkipROM;
    }

    CallROMType RecordSample() {
        if (!PatchModuleOmniRemote::enabled) return kExecuteROM;
        // Confirmed against ORLib's dispatch table and the supplied PRC's
        // callers: A807(refNum, buffer, capacity, &length, baud, offLimit, flags).
        CALLED_SETUP("UInt8", "UInt16 refNum, UInt32 buffer, UInt16 capacity, "
                     "UInt32 length, UInt32 rate, UInt16 offLimit, UInt16 flags");
        CALLED_GET_PARAM_VAL(UInt32, buffer);
        CALLED_GET_PARAM_VAL(UInt16, capacity);
        CALLED_GET_PARAM_VAL(UInt32, length);
        CALLED_GET_PARAM_VAL(UInt32, rate);
        request = {buffer, length, capacity, rate, true};
        if (!buffer || !length || capacity < 2 || rate < 1000 || rate > 1000000) {
            request.pending = false;
            PUT_RESULT_VAL(UInt8, 0);
            return kSkipROM;
        }
        SuspendManager::Suspend<IrLearnContext>();
        return kSkipROM;
    }
}

PatchModuleOmniRemote::PatchModuleOmniRemote() {
    static ProtoPatchTableEntry table[] = {{0xa807, RecordSample, nullptr}, {0, nullptr, nullptr}};
    LoadProtoPatchTable(table);
}

HeadpatchProc PatchModuleOmniRemote::NoviiHeadpatch(uint16 trap) {
    if (!enabled) return nullptr;
    return trap == 0xa3ec ? NoviiLearn : trap == 0xa3ed ? NoviiTransmit : nullptr;
}

bool PatchModuleOmniRemote::IsPending() {
    return request.pending && SuspendManager::IsSuspended() &&
           SuspendManager::GetContext().GetKind() == SuspendContext::irLearn;
}

unsigned PatchModuleOmniRemote::GetRate() { return IsPending() ? request.rate : 0; }

bool PatchModuleOmniRemote::Complete(const uint32* timings, unsigned count) {
    if (!IsPending()) return false;
    auto& context = static_cast<IrLearnContext&>(SuspendManager::GetContext());
    if (!timings || count < 6 || count > 100000) { context.Cancel(); return false; }
    if (request.noviiObject) {
        if (count > 300) { context.Cancel(); return false; }
        const emuptr object = request.noviiObject;
        uint64 total = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (!timings[i] || timings[i] > 2000000 || (total += timings[i]) > 10000000) {
                context.Cancel(); return false;
            }
        }
        // Novii stores signed durations in timer ticks: positive marks,
        // negative spaces. Use 100 ns per tick to limit carrier rounding when Novii saves and
        // reloads its integer timer period. The dongle does not measure carrier.
        for (unsigned i = 0; i < 300; ++i)
            EmMemPut32(object + 0x4c6 + 4*i, i < count ? (i & 1 ? -static_cast<int32>(timings[i] * 10) : timings[i] * 10) : 0);
        EmMemPut16(object + 4, 1);
        EmMemPut16(object + 6, count);
        EmMemPut16(object + 8, 0);
        EmMemPut16(object + 0xa, 0);
        EmMemPut16(object + 0x12, 100);
        // Novii rounds the stored period again on reload; this produces
        // approximately 38 kHz after saving and reopening the learned code.
        EmMemPut16(object + 0x14, 128);
        context.Complete(count);
        return true;
    }
    // ORLib stores an initial 0x01 byte, followed by MSB-first envelope bits.
    // One bit is a ten-bit UART frame, hence five carrier cycles at baud/2.
    std::vector<uint8> result(request.capacity, 0);
    result[0] = 1;
    uint64 totalUs = 0;
    unsigned previousBit = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!timings[i] || timings[i] > 2000000 || totalUs + timings[i] > 10000000) {
            context.Cancel(); return false;
        }
        totalUs += timings[i];
        const auto end = static_cast<unsigned>(std::llround(totalUs * request.rate / 10000000.0));
        if (1 + (end + 7) / 8 > result.size()) { context.Cancel(); return false; }
        if (!(i & 1)) {
            for (unsigned bit = previousBit; bit < end; ++bit)
                result[1 + bit / 8] |= 0x80 >> (bit % 8);
        }
        previousBit = end;
    }
    const unsigned length = 1 + (previousBit + 7) / 8;
    EmMem_memcpy(request.buffer, static_cast<const void*>(result.data()), length);
    context.Complete(length);
    return true;
}

void PatchModuleOmniRemote::RegisterWrite(emuptr address, unsigned size, uint32 value) {
    if (!enabled) return;
    if (address == 0xfffff423 && size == 1) portSelect = value;
    if (address == 0xfffff900 || address == 0xfffff902 || address == 0xfffff908) {
        FinishTransmission();
        if (address == 0xfffff900) uartControl = value;
        if (address == 0xfffff902) uartBaud = value;
        if (address == 0xfffff908) uartMisc = value;
    }
    if (address != 0xfffff906 || size != 2 || value != 0x0855 ||
        (uartControl & 0xa100) != 0xa100 || (uartControl & 0x0a00) || (uartMisc & 0x20)) return;
    const double baud = static_cast<double>(EmHAL::GetSystemClockFrequency()) /
                        (65 - (uartBaud & 63)) / (1 << ((uartBaud >> 8) & 7)) / 16;
    if (runs.empty()) { sampleUs = 10000000.0 / baud; carrierHz = baud / 2; }
    const bool mark = !(portSelect & 0x20);
    if (sampleCount++ >= 100000) { runs.clear(); sampleCount = 0; return; }
    if (!runs.empty() && runs.back().mark == mark) ++runs.back().samples;
    else runs.push_back({mark, 1});
}

unsigned PatchModuleOmniRemote::TransmitSize() { return transmitQueue.empty() ? 0 : transmitQueue.front().timings.size(); }
unsigned PatchModuleOmniRemote::TransmitFrequency() { return transmitQueue.empty() ? 0 : transmitQueue.front().frequency; }
void* PatchModuleOmniRemote::TransmitData() { return transmitQueue.empty() ? nullptr : transmitQueue.front().timings.data(); }
void PatchModuleOmniRemote::PopTransmit() { if (!transmitQueue.empty()) transmitQueue.pop_front(); }
