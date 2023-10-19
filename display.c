#include "display.h"

int current_buffer;
uint16_t* framebuffer_1;
uint16_t* framebuffer_2;
uint32_t framebuffer_size;
uint16_t* backbuffer;

/* Initialize double buffer
 * parameter: size of framebuffer */
void display_initialize_doublebuffer(){
    framebuffer_size = SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t);
    framebuffer_1 = (uint16_t*) memalign(32, framebuffer_size);
    framebuffer_2 = (uint16_t*) memalign(32, framebuffer_size);
    memset(framebuffer_1,'\0', framebuffer_size);
    memset(framebuffer_2,'\0', framebuffer_size);

    backbuffer = framebuffer_1;
    current_buffer = 1;
}

void display_initialize(){
    //set our video mode
   #ifndef HI_RES
    vid_set_mode(DM_320x240, PM_RGB565);
   #endif

    //initialize software double buffer
    display_initialize_doublebuffer();
}


#define MEM_MODE_VRAM64	0
#define MEM_MODE_VRAM32	1
void sq_cpy_pvr_fast(void *dst, void *src, size_t len, unsigned mem_mode) {
   //Set PVR DMA register
   *(volatile int *)0xA05F6884 = mem_mode;
   
   //Convert read/write area pointer to DMA write only area pointer
   uint32_t dmaareaptr = ((uintptr_t)dst & 0xffffff) | 0x11000000;
   
   sq_cpy((void *)dmaareaptr, src, len);
}

//flip double buffer
void display_flip_framebuffer(){
    if (1) {
	//Use SQ
        unsigned int pixelsize = vid_mode->pm;
	pixelsize += pixelsize == 0;
	pixelsize++;
	
	//Size of framebuffer in bytes
	size_t fbsize = vid_mode->width * vid_mode->height * pixelsize;
        sq_cpy_pvr_fast(vram_l, backbuffer, fbsize, MEM_MODE_VRAM32);
    } else {
	//Use DMA
        fbdma_flip(backbuffer);
    }
    
    // Store Queue Trasnfer
    if(current_buffer == 1){
        current_buffer = 2;
        backbuffer = framebuffer_2;
        //vid_waitvbl();
        //~ sq_cpy(vram_s, framebuffer_1, framebuffer_size);

    } else {
        current_buffer = 1;
        backbuffer = framebuffer_1;
        //vid_waitvbl();
        //~ sq_cpy(vram_s, framebuffer_2, framebuffer_size);
    }
    // DMA Trasnfer
    // dcache_flush_range((uint32_t) backbuffer,framebuffer_size);
    // while (!pvr_dma_ready());
    // pvr_dma_transfer(backbuffer,(uint32_t) vram_s, framebuffer_size,
    //                  PVR_DMA_VRAM32, -1, NULL, (ptr_t) NULL);
}

/* Initialize double buffer
 * parameter: Red, Green, Blue */
void display_clear_backbuffer(int r, int g, int b){
    memset(backbuffer, PACK_PIXEL(r, g, b), framebuffer_size);
}
