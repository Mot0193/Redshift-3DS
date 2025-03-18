#ifndef SOCKET3DS_H
#define SOCKET3DS_H

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>


#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE (4 * 1024 * 1024)
static u32 *SOC_buffer = NULL;


void socShutdown(void) {
	printf("waiting for socExit...\n");
	socExit();
}

void initSocketService(){
    // initialize 3DS socket service
	// from 3ds socket example:
	// https://github.com/devkitPro/3ds-examples/blob/master/network/sockets/source/sockets.c#L70-L84
	
    int ret;

	// allocate buffer for SOC service
	SOC_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);

	if(SOC_buffer == NULL) {
        fprintf(stderr,"FAILED TO INIT 3DS SOCKET SERVICE!");
	}

	// Now intialise soc:u service
	if (R_FAILED(ret = socInit(SOC_buffer, SOC_BUFFERSIZE))) {
        fprintf(stderr,"FAILED TO INIT 3DS SOCKET SERVICE! socInit");
	}
}
#endif