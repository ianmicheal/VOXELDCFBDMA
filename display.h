#ifndef display_h
#define display_h

#include <stdlib.h>
#include <kos.h>
#include "fbdma.h"

//MACROS
//~ #define HI_RES
#ifdef HI_RES
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#else
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#endif
#define PACK_PIXEL(r, g, b) ( ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3) )
#if 0
#define DRAW_PIXEL(x, y, color) \
	if((x >= 0) && (x < SCREEN_HEIGHT) && (y >= 0) && (y < SCREEN_WIDTH)) \
		backbuffer[(y * SCREEN_WIDTH) + x] = color;
#else
#define DRAW_PIXEL(x, y, color) \
	backbuffer[(y * SCREEN_WIDTH) + x] = color;
#endif

//Variables
extern uint16_t* backbuffer;

//Functions
void display_initialize(void);
void display_initialize_doublebuffer(void);
void display_flip_framebuffer(void);
void display_clear_backbuffer(int r, int g, int b);

#endif
