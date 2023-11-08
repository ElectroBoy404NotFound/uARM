#include "SoC.h"
#include "../CPU/CPU.h"
#include "../memory/MMU/MMU.h"
#include "../memory/mem.h"
#include "../memory/RAM/callout_RAM.h"
#include "../memory/RAM/RAM.h"
#include "../CPU/cp15.h"
#include "../math/math64.h"
#include "../pxa255/IC/pxa255_IC.h"
#include "../pxa255/TIMR/pxa255_TIMR.h"
#include "../pxa255/RTC/pxa255_RTC.h"
#include "../pxa255/UART/pxa255_UART.h"
#include "../pxa255/PwrClk/pxa255_PwrClk.h"
#include "../pxa255/GPIO/pxa255_GPIO.h"
#include "../pxa255/DMA/pxa255_DMA.h"
#include "../pxa255/DSP/pxa255_DSP.h"

#include "../helper/print.h"
#include "../helper/external.h"

#define ERR(s)	do{err_str(s " Halting\r\n"); while(1); }while(0)

static const UInt8 embedded_boot[] =	{
						0x01, 0x00, 0x8F, 0xE2, 0x10, 0xFF, 0x2F, 0xE1, 0x04, 0x27, 0x01, 0x20, 0x00, 0x21, 0x00, 0xF0,
						0x0D, 0xF8, 0x0A, 0x24, 0x24, 0x07, 0x65, 0x1C, 0x05, 0x27, 0x00, 0x22, 0x00, 0xF0, 0x06, 0xF8,
						0x20, 0x60, 0x24, 0x1D, 0x49, 0x1C, 0x80, 0x29, 0xF8, 0xD1, 0x28, 0x47, 0xBC, 0x46, 0xBB, 0xBB,
						0x70, 0x47
					};

#define ROM_BASE	0x00000000UL
#define ROM_SIZE	sizeof(embedded_boot)

#define RAM_BASE	0xA0000000UL
#define RAM_SIZE	0x01000000UL	//16M @ 0xA0000000


static Boolean vMemF(ArmCpu* cpu, void* buf, UInt32 vaddr, UInt8 size, Boolean write, Boolean priviledged, UInt8* fsrP){
	
	SoC* soc = cpu->userData;
	UInt32 pa;
	
	if(size & (size - 1)){	//size is not a power of two
		
		return false;	
	}
	if(vaddr & (size - 1)){	//bad alignment
		
		return false;	
	}

	return mmuTranslate(&soc->mmu, vaddr, priviledged, write, &pa, fsrP) && memAccess(&soc->mem, pa, size, write, buf);
}


static Boolean hyperF(ArmCpu* cpu){		//return true if handled

	SoC* soc = cpu->userData;
	
	switch(cpu->regs[12]){
	
		case 0:{
		
			err_str("Hypercall 0 caught\r\n");
			soc->go = false;
			break;
		}
		
		case 1:{
			
			err_dec(cpu->regs[0]);
			break;
		}
	
		case 2:{
			
			char x[2];
			x[1] = 0;
			x[0] = cpu->regs[0];
			err_str(x);
			break;
		}
		
		case 3:{
			
			cpu->regs[0] = RAM_SIZE;
			break;
		}
		
		case 4:{			//block device access perform [do a read or write]
		
			//IN:
			// R0 = op
			// R1 = sector
			
			return soc->blkF(soc->blkD, cpu->regs[1], soc->blkDevBuf, cpu->regs[0]);
		}
		
		case 5:{			//block device buffer access [read or fill emulator's buffer]
		
			//IN:
			// R0 = word value
			// R1 = word offset (0, 1, 2...)
			// R2 = op (1 = write, 0 = read)
			//OUT:
			// R0 = word value
			
			if(cpu->regs[1] >= BLK_DEV_BLK_SZ / sizeof(UInt32)) return false;	//invalid request
			
			if(cpu->regs[2] == 0){
				
				cpu->regs[0] = soc->blkDevBuf[cpu->regs[1]];
			}
			else if(cpu->regs[2] == 1){
				
				soc->blkDevBuf[cpu->regs[1]] = cpu->regs[0];
			}
			else return false;
		}
	}
	return true;
}

static void setFaultAdrF(ArmCpu* cpu, UInt32 adr, UInt8 faultStatus){
	
	SoC* soc = cpu->userData;
	
	cp15SetFaultStatus(&soc->cp15, adr, faultStatus);
}

static void emulErrF(_UNUSED_ ArmCpu* cpu, const char* str){
	err_str("Emulation error: <<");
	err_str(str);
	err_str(">> halting\r\n");
	while(1);
}

static Boolean pMemReadF(void* userData, UInt32* buf, UInt32 pa){	//for DMA engine and MMU pagetable walks

	ArmMem* mem = userData;

	return memAccess(mem, pa, 4, false, buf);
}

static void dumpCpuState(ArmCpu* cpu, char* label){

	UInt8 i;

	if(label){
		err_str("CPU ");
		err_str(label);
		err_str("\r\n");
	}
	
	for(i = 0; i < 16; i++){
		err_str("R");
		err_dec(i);
		err_str("\t= 0x");
		err_hex(cpuGetRegExternal(cpu, i));
		err_str("\r\n");	
	}
	err_str("CPSR\t= 0x");
	err_hex(cpuGetRegExternal(cpu, ARM_REG_NUM_CPSR));
	err_str("\r\nSPSR\t= 0x");
	err_hex(cpuGetRegExternal(cpu, ARM_REG_NUM_SPSR));
	err_str("\r\n");
}

static UInt16 socUartPrvRead(void* userData){			//these are special funcs since they always get their own userData - the uart :)
	
	SoC* soc = userData;
	UInt16 v;
	int r;
	
	r = soc->rcF();
	if(r == CHAR_CTL_C) v = UART_CHAR_BREAK;
	else if(r == CHAR_NONE) v = UART_CHAR_NONE;
	else if(r >= 0x100) v = UART_CHAR_NONE;		//we canot send this char!!!
	else v = r;
	
	return v;
}

static void socUartPrvWrite(UInt16 chr, void* userData){	//these are special funcs since they always get their own userData - the uart :)
	
	SoC* soc = userData;
	
	if(chr == UART_CHAR_NONE) return;
	soc->wcF(chr);
}

void LinkError_SIZEOF_STRUCT_SOC_wrong();


void socRamModeAlloc(SoC* soc, _UNUSED_ void* ignored){
	
	UInt32* ramB = emu_alloc(RAM_SIZE);
	if(!ramB) ERR("Cannot allocate RAM buffer");
	if(!ramInit(&soc->ram.RAM, &soc->mem, RAM_BASE, RAM_SIZE, ramB)) ERR("Cannot init RAM");
	
	soc->calloutMem = false;	
}

void socRamModeCallout(SoC* soc, void* callout){
	
	if(!coRamInit(&soc->ram.coRAM, &soc->mem, RAM_BASE, RAM_SIZE, callout)) ERR("Cannot init coRAM");
	
	soc->calloutMem = true;	
}

#define ERR_(s)	ERR("error");

void socInit(SoC* soc, SocRamAddF raF, void*raD, readcharF rc, writecharF wc, blockOp blkF, void* blkD){

	Err e;
	
	soc->rcF = rc;
	soc->wcF = wc;
	
	soc->blkF = blkF;
	soc->blkD = blkD;

	soc->go = true;
	
	e = cpuInit(&soc->cpu, ROM_BASE, vMemF, emulErrF, hyperF, &setFaultAdrF);
	if(e){
		err_str("Failed to init CPU: ");
	//	err_dec(e);
	//	err_str(". Halting\r\n");
		while(1);
	}
	soc->cpu.userData = soc;
	
	memInit(&soc->mem);
	mmuInit(&soc->mmu, pMemReadF, &soc->mem);
	
	if(ROM_SIZE > sizeof(soc->romMem)) {
	//	err_str("Failed to init CPU: ");
		err_str("ROM_SIZE to small");
	//	err_str(". Halting\r\n");
		while(1);
	}
	
	raF(soc, raD);
	
	if(!ramInit(&soc->ROM, &soc->mem, ROM_BASE, ROM_SIZE, soc->romMem)) ERR_("Cannot init ROM");
	
	cp15Init(&soc->cp15, &soc->cpu, &soc->mmu);
	
	__mem_copy(soc->romMem, embedded_boot, sizeof(embedded_boot));
	
	if(!pxa255icInit(&soc->ic, &soc->cpu, &soc->mem)) ERR_("Cannot init PXA255's interrupt controller");
	if(!pxa255timrInit(&soc->timr, &soc->mem, &soc->ic)) ERR_("Cannot init PXA255's OS timers");
	if(!pxa255rtcInit(&soc->rtc, &soc->mem, &soc->ic)) ERR_("Cannot init PXA255's RTC");
	if(!pxa255uartInit(&soc->ffuart, &soc->mem, &soc->ic,PXA255_FFUART_BASE, PXA255_I_FFUART)) ERR_("Cannot init PXA255's FFUART");
	if(!pxa255uartInit(&soc->btuart, &soc->mem, &soc->ic,PXA255_BTUART_BASE, PXA255_I_BTUART)) ERR_("Cannot init PXA255's BTUART");
	if(!pxa255uartInit(&soc->stuart, &soc->mem, &soc->ic,PXA255_STUART_BASE, PXA255_I_STUART)) ERR_("Cannot init PXA255's STUART");
	if(!pxa255pwrClkInit(&soc->pwrClk, &soc->cpu, &soc->mem)) ERR_("Cannot init PXA255's Power and Clock manager");
	if(!pxa255gpioInit(&soc->gpio, &soc->mem, &soc->ic)) ERR_("Cannot init PXA255's GPIO controller");
	if(!pxa255dmaInit(&soc->dma, &soc->mem, &soc->ic)) ERR_("Cannot init PXA255's DMA controller");
	if(!pxa255dspInit(&soc->dsp, &soc->cpu)) ERR_("Cannot init PXA255's cp0 DSP");
	
	pxa255uartSetFuncs(&soc->ffuart, socUartPrvRead, socUartPrvWrite, soc);	
}

void socRun(SoC* soc){
	UInt32 cycles = 0;	//make 64 if you REALLY need it... later
	
	while(soc->go){
		cycles++;
		
		if(!(cycles & 0x000007UL)) pxa255timrTick(&soc->timr);
		if(!(cycles & 0x0000FFUL)) pxa255uartProcess(&soc->ffuart);
		if(!(cycles & 0x000FFFUL)) pxa255rtcUpdate(&soc->rtc);
		
		cpuCycle(&soc->cpu);
	}
}
