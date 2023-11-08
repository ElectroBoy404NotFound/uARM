APP	= uARM
CC	= gcc
LD	= gcc

.PHONY: $(APP)

CC_FLAGS	= -m64 -O3 -fomit-frame-pointer -march=core2 -momit-leaf-frame-pointer -D_FILE_OFFSET_BITS=64 -D__USE_LARGEFILE64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
LDFLAGS = $(LD_FLAGS) -Wall -Wextra
CCFLAGS = $(CC_FLAGS) -Wall -Wextra

SOURCES := $(shell find emulator/*/ -name '*.c')

$(APP):
	$(CC) $(CCFLAGS) $(LDFLAGS) emulator/main_pc.c $(SOURCES) -o $(APP)

clean:
	rm -f $(APP)
	rm -rf linux/linux*

linux/linux-2.6.34.1:
	@cd linux; wget https://cdn.kernel.org/pub/linux/kernel/v2.6/linux-2.6.34.1.tar.xz; tar -xf linux-2.6.34.1.tar.xz; cp kernel.config linux-2.6.34.1/.config

linux_build: linux/linux-2.6.34.1
	@cd linux/linux-2.6.34.1; make $(shell nproc)
