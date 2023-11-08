#ifndef EXTERNAL_H_
#define EXTERNAL_H_

UInt32 rtcCurTime(void);
void* emu_alloc(UInt32 size);
void emu_free(void* ptr);

#endif