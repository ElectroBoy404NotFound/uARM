#ifndef _TYPES_H_
#define _TYPES_H_

#ifdef _FTL_TEST_
#include "test.h"
#endif

/* Types */

	typedef signed long Int32;

	typedef unsigned long long UInt64;
	typedef unsigned long UInt32;
	typedef unsigned short UInt16;
	typedef unsigned char UInt8;

/* Debug options */

	#define nTRACE(...)		printk(__VA_ARGS__)
	#define nDEBUG(...)		//printk(__VA_ARGS__)
	#define nERROR(...)		pr_err(__VA_ARGS__)

/* macros */

	#undef offsetof
	#define offsetof(mem, str)	((unsigned long)(&(((str*)0)->mem)))



#endif


