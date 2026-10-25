#ifndef NOVII_IR_H
#define NOVII_IR_H
#include <cstdint>
bool noviiIrHandle(uint16_t trap);
void noviiIrEnable(bool enabled);
bool noviiIrPending();
bool noviiIrComplete(const uint32_t* timings, unsigned count);
void noviiIrCancel();
unsigned noviiIrTransmitSize();
unsigned noviiIrTransmitFrequency();
void* noviiIrTransmitData();
void noviiIrPopTransmit();
#endif
