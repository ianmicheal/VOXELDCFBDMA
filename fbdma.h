#pragma once

#include <kos.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FBDMA_NONBLOCK 0
#define FBDMA_BLOCK 1

//Copies src to target frame buffer (vram_s/vram_l) using DMA and does a page flip
//Size of transfer is calculated automatically from video mode.
//Do not modify src until DMA has finished, either double buffer src or call fbdma_wait before modifiying src.
//Automatically flushes relevant parts of cache
//Call fbdma_init first
//Returns 0 on success. On error, returns nonzero and errno is set as per fbdma_transfer
int fbdma_flip(const void * src);

//Waits until DMA is finished
//Call fbdma_init first
void fbdma_wait(void);

//Returns 1 if DMA in progress, or 0 if no DMA happening
//Call fbdma_init first
int fbdma_busy(void);

//Custom transfer. Copy count bytes from src to dest. Does not flush cache.
//Returns 0 on success. On error, returns nonzero and errno is set:
// EPERM : FBDMA not initialized
// EINPROGRESS : DMA is already happening
// EFAULT : src, dest, or count is not a multiple of 32, or src is not in main RAM
// EIO : weird hardware error
//Call fbdma_init first
int fbdma_transfer(const void * src, uint32 dest, uint32 count,  int block, pvr_dma_callback_t callback, ptr_t cbdata);

//Set up FBDMA. Do not use 3D rendering at the same time
//(3D must be disabled before calling this, and don't enable 3D after calling this without first shutting down FBDMA)
void fbdma_init(void);

//Turns off FBDMA. It's safe to use 3D after this.
void fbdma_shutdown(void);


#ifdef __cplusplus
}
#endif
