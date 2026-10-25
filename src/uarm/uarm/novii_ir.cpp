#include "novii_ir.h"
#include "pace.h"
#include <cmath>
#include <deque>
#include <vector>
namespace {
    bool enabled = false, pending = false;
    uint32_t captureBuffer = 0, capturePeriod = 0, captureCapacity = 0;
    struct Tx { unsigned frequency; std::vector<uint32_t> timings; };
    std::deque<Tx> queue;
}
void noviiIrEnable(bool value) { enabled = value; }
bool noviiIrPending() { return pending; }
bool noviiIrHandle(uint16_t trap) {
    if (!enabled || trap != 0xa3ee) return false;
    const uint32_t stack = paceGetAreg(7);
    const uint32_t code = paceGet32(stack), params = paceGet32(stack + 4);
    if (!code || !params || paceGet32(code) != 0x4e524952) return false;
    const unsigned op = paceGet32(params), count = paceGet32(params + 4);
    const uint32_t buffer = paceGet32(params + 8), context = paceGet32(params + 20);
    paceSetDreg(0, 0);
    switch (op) {
        case 6: paceSetDreg(0, 256); break; // private native context size
        case 7: if (context) { paceSet32(context, 1); paceSet32(context + 4, 26); } break;
        case 1: { // Load code: signed microsecond durations and full carrier period.
            const unsigned period = paceGet32(params + 12);
            if (period < 10 || period > 100) paceSetDreg(0, 12);
            else if (context) { paceSet32(context, 1); paceSet32(context + 4, period); }
            break;
        }
        case 2: {
            if (!count || count > 300 || !buffer || queue.size() >= 8) { paceSetDreg(0, 11); break; }
            const unsigned period = context ? paceGet32(context + 4) : 26;
            if (period < 10 || period > 100) { paceSetDreg(0, 12); break; }
            Tx tx{static_cast<unsigned>(std::lround(1000000.0 / period)), {}};
            uint64_t total = 0;
            bool lastMark = false;
            for (unsigned i = 0; i < count; ++i) {
                const int32_t signedUs = static_cast<int32_t>(paceGet32(buffer + 4*i));
                const bool mark = signedUs > 0;
                const uint64_t us = signedUs < 0 ? -static_cast<int64_t>(signedUs) : signedUs;
                if (!us) continue;
                if (us > 2000000 || (total += us) > 10000000) { tx.timings.clear(); break; }
                if (tx.timings.empty() && !mark) continue;
                if (!tx.timings.empty() && lastMark == mark) tx.timings.back() += us;
                else tx.timings.push_back(us);
                lastMark = mark;
            }
            if (tx.timings.empty()) paceSetDreg(0, 11);
            else queue.push_back(std::move(tx));
            break;
        }
        case 3: case 8:
            if (!buffer || !count || count > 300 || !paceGet32(params + 12)) { paceSetDreg(0, 11); break; }
            captureBuffer = buffer; captureCapacity = count;
            capturePeriod = paceGet32(params + 12); pending = true;
            break;
        case 4: break; // Hardware cleanup; buffers already contain microseconds.
        default: paceSetDreg(0, 100); break;
    }
    return true;
}
bool noviiIrComplete(const uint32_t* timings, unsigned count) {
    if (!pending) return false;
    uint64_t total = 0;
    if (!timings || count < 6 || count > captureCapacity) { noviiIrCancel(); return false; }
    for (unsigned i = 0; i < count; ++i)
        if (!timings[i] || timings[i] > 2000000 || (total += timings[i]) > 10000000) { noviiIrCancel(); return false; }
    for (unsigned i = 0; i < captureCapacity; ++i)
        paceSet32(captureBuffer + 4*i, i < count ? (i & 1 ? -static_cast<int32_t>(timings[i]) : timings[i]) : 0);
    paceSet32(capturePeriod, 26); // Envelope receiver cannot measure carrier: assume ~38 kHz.
    paceSetDreg(0, 0); pending = false;
    return true;
}
void noviiIrCancel() { if (pending) { paceSetDreg(0, 100); pending = false; } }
unsigned noviiIrTransmitSize() { return queue.empty() ? 0 : queue.front().timings.size(); }
unsigned noviiIrTransmitFrequency() { return queue.empty() ? 0 : queue.front().frequency; }
void* noviiIrTransmitData() { return queue.empty() ? nullptr : queue.front().timings.data(); }
void noviiIrPopTransmit() { if (!queue.empty()) queue.pop_front(); }
