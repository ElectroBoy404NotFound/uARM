#include "SoC/SoC.h"
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <signal.h>
#include <termios.h>

#define off64_t __off64_t
unsigned char* readFile(const char* name, UInt32* lenP){

	long len = 0;
	unsigned char *r = NULL;
	int i;
	FILE* f;
	
	f = fopen(name, "r");
	if(!f){
		perror("cannot open file");
		return NULL;
	}
	
	i = fseek(f, 0, SEEK_END);
	if(i){
		return NULL;
		perror("cannot seek to end");
	}
	
	len = ftell(f);
	if(len < 0){
		perror("cannot get position");
		return NULL;
	}
	
	i = fseek(f, 0, SEEK_SET);
	if(i){
		return NULL;
		perror("cannot seek to start");
	}
	
	
	r = malloc(len);
	if(!r){
		perror("cannot alloc memory");
		return NULL;
	}
	
	if(len != (long)fread(r, 1, len, f)){
		perror("canot read file");
		free(r);
		return NULL;
	}
	
	*lenP = len;
	return r;
}



static int ctlCSeen = 0;

static int readchar(void){
	
	struct timeval tv;
	fd_set set;
	char c;
	int i, ret = CHAR_NONE;
	
	if(ctlCSeen){
		ctlCSeen = 0;
		return 0x03;
	}
	
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	
	FD_ZERO(&set);
	FD_SET(0, &set);
	
	i = select(1, &set, NULL, NULL, &tv);
	if(i == 1 && 1 == read(0, &c, 1)){
		
		ret = c;
	}

	return ret;
}

static void writechar(int chr){

	if(!(chr & 0xFF00)){
		
		printf("%c", chr);
	}
	else{
		printf("<<~~ EC_0x%x ~~>>", chr);
	}
	fflush(stdout);	
}

void ctl_cHandler(_UNUSED_ int v){	//handle SIGTERM      
	
//	exit(-1);
	ctlCSeen = 1;
}

int rootOps(void* userData, UInt32 sector, void* buf, UInt8 op){
	
	FILE* root = userData;
	int i;
	
	switch(op){
		case BLK_OP_SIZE:
			
			if(sector == 0){	//num blocks
				
				if(root){
					
					i = fseeko64(root, 0, SEEK_END);
					if(i) return false;
					
					 *(unsigned long*)buf = (off64_t)ftello64(root) / (off64_t)BLK_DEV_BLK_SZ;
				}
				else{
					
					*(unsigned long*)buf = 0;
				}
			}
			else if(sector == 1){	//block size
				
				*(unsigned long*)buf = BLK_DEV_BLK_SZ;
			}
			else return 0;
			return 1;
		
		case BLK_OP_READ:
			
			i = fseeko64(root, (off64_t)sector * (off64_t)BLK_DEV_BLK_SZ, SEEK_SET);
			if(i) return false;
			return fread(buf, 1, BLK_DEV_BLK_SZ, root) == BLK_DEV_BLK_SZ;
		
		case BLK_OP_WRITE:
			
			i = fseeko64(root, (off64_t)sector * (off64_t)BLK_DEV_BLK_SZ, SEEK_SET);
			if(i) return false;
			return fwrite(buf, 1, BLK_DEV_BLK_SZ, root) == BLK_DEV_BLK_SZ;
	}
	return 0;	
}

SoC soc;

int main(int argc, char** argv){
	
	struct termios cfg, old;
	FILE* root = NULL;
	
	if(argc != 2){
		fprintf(stderr,"usage: %s path_to_disk\n", argv[0]);
		return -1;	
	}
	
	//setup the terminal
	{
		int ret;
		
		ret = tcgetattr(0, &old);
		cfg = old;
		if(ret) perror("cannot get term attrs");
		
		#ifndef _DARWIN_
		
			cfg.c_iflag &=~ (INLCR | INPCK | ISTRIP | IUCLC | IXANY | IXOFF | IXON);
			cfg.c_oflag &=~ (OPOST | OLCUC | ONLCR | OCRNL | ONOCR | ONLRET);
			cfg.c_lflag &=~ (ECHO | ECHOE | ECHONL | ICANON | IEXTEN | XCASE);
		#else
			cfmakeraw(&cfg);
		#endif
		
		ret = tcsetattr(0, TCSANOW, &cfg);
		if(ret) perror("cannot set term attrs");
	}
	
	root = fopen64(argv[1], "r+b");
	if(!root){
		fprintf(stderr,"Failed to open root device\n");
		exit(-1);
	}
	
	socInit(&soc, socRamModeAlloc, NULL, readchar, writechar, rootOps, root);
	signal(SIGINT, &ctl_cHandler);
	socRun(&soc);
	
	fclose(root);
	tcsetattr(0, TCSANOW, &old);
	
	return 0;
}


//////// runtime things

void* emu_alloc(UInt32 size){
	
	return calloc(size,1);	
}

void emu_free(void* ptr){
	
	free(ptr);
}

UInt32 rtcCurTime(void){
	
	struct timeval tv;
	
	gettimeofday(&tv, NULL);
	
	return tv.tv_sec;	
}

void err_str(const char* str){
	
	fprintf(stderr, "%s", str);	
}

