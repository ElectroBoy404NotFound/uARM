#ifndef MEMORY_H
#define MEMORY_H

void __mem_zero(void* ptr, UInt16 sz){
	
	UInt8* p = ptr;
	
	while(sz--) *p++ = 0;	
}

void __mem_copy(void* d_, const void* s_, UInt32 sz){
	
	UInt8* d = d_;
	const UInt8* s = s_;
	
	while(sz--) *d++ = *s++;
}

#define memset __memset_disabled__
#define memcpy __memcpy_disabled__

#endif