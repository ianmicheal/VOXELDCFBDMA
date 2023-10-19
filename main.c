#include <kos.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "display.h"
#include "load_map.h"

//TODO list
//Implement delta time
//Fix gif.h unaligned memory access
//Change all int to _t

//Constants
#define MAP_N 1024
#define MAP_SHIFT 10
#ifdef HI_RES
#define SCALE_FACTOR (70.0*2)
#else
#define SCALE_FACTOR (70.0)
#endif

// Buffers for height_map and texture_map
uint8_t* height_map = NULL;   // Buffer to hold height values in grayscale
uint16_t* texture_map = NULL;   // Buffer to hold pixel color values in RGB565


// Camera struct type declaration
typedef struct {
    float x;         // x position on the map
    float y;         // y position on the map
    float height;    // height of the camera
    float horizon;   // offset of the horizon position (looking up-down)
    float zfar;      // distance of the camera looking forward
    float angle;     // camera angle (radians, clockwise)
} camera_t;

// Camera definition
camera_t camera = {
    .x       = 512.0,
    .y       = 512.0,
    .height  = 70,
    .horizon = 60.0,
    .zfar    = 500,
    .angle   = 0.0 //1.5 * 3.141592 // (= 270 deg)
};

int quit = 0;

int show_low_detail = 0;
int dbl_pixels = 0;
// Handle controller input
int process_input() {
    maple_device_t *cont;
    cont_state_t *state;
    
    cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);

    // Check key status
    state = (cont_state_t *)maple_dev_status(cont);

    if(!state) {
        printf("Error reading controller\n");
        return -1;
    }
    if(state->buttons & CONT_START){
	quit = 1;
        return 0;
    }
    if(state->buttons & CONT_A){
        if(camera.height > 10){
            camera.height--;
        }
    }
    if(state->buttons & CONT_B){
        camera.horizon += 1.5;
    }
    if((state->buttons & CONT_X)) {
        camera.horizon -= 1.5;
    }
    if((state->buttons & CONT_Y)) {
        if(camera.height < 180){
            camera.height++;
        }
    }
    if(state->buttons & CONT_DPAD_UP){
        camera.x += cos(camera.angle);
        camera.y += sin(camera.angle);
    }
    if(state->buttons & CONT_DPAD_DOWN){
        camera.x -= cos(camera.angle);
        camera.y -= sin(camera.angle);
    }
    if(state->buttons & CONT_DPAD_LEFT){
        camera.angle -= 0.02;
        camera.angle = camera.angle >= 0.0 ? fmod(camera.angle, 6.28) : 6.28 - abs(fmod(camera.angle, 6.28));
    }
    if(state->buttons & CONT_DPAD_RIGHT){
        camera.angle = fmod((camera.angle + 0.02), (6.28));
    }
    static int last_l = 0;
    if(state->ltrig && !last_l){
	show_low_detail ^= 0x1f;
        //~ return 0;
    }
    last_l = state->ltrig;
    
    static int last_r = 0;
    if(state->rtrig && !last_r){
	dbl_pixels = !dbl_pixels;
    }
    last_r = state->rtrig;
    return 1;
}
    
// float perspecive_divide_table[512][600];
// void init_perspecive_divide_table(){
//     for(int i = -255; i < 255; i++){
//         for(int j = 0; j < 600; j++){
//             perspecive_divide_table[i + 255][j] = i / (float)j  * SCALE_FACTOR;
//         }
//     }
// }

// float perspecive_divide(int16_t height, uint16_t zfar){
//     return perspecive_divide_table[height + 255][zfar];
// }


static inline short * draw_col2(short *dst, int x, int bottom, int top, int color) {
    int comp;
    do {
	    bottom--;
	    comp = top != bottom;
	    //~ assert(dst >= backbuffer);
	    *dst = color;
	    dst -= SCREEN_WIDTH;
    } while(comp);
    return dst;
}

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

static inline short * draw_col(short *dst, int x, int bottom, int top, int color) {
    //~ dst += top * SCREEN_WIDTH + x;
    int count = bottom - top;
    //~ for (int y = top; y < bottom; y++) {
    do {
	*dst = color;
	dst -= SCREEN_WIDTH;
        //~ DRAW_PIXEL(i, y, texture_map[map_offset]);
    } while(--count);
    return dst;
}
static inline short * draw_col_dbl(short *dst, int x, int bottom, int top, int color) {
    //~ dst += top * SCREEN_WIDTH + x;
    int count = bottom - top;
    int *longdst = (int*)dst;
    color &= 0xffff;
    color |= color << 16;
    //~ for (int y = top; y < bottom; y++) {
    do {
	*longdst = color;
	longdst -= SCREEN_WIDTH/2;
        //~ DRAW_PIXEL(i, y, texture_map[map_offset]);
    } while(--count);
    return longdst;
}


//Adjust step size based on distance and slope of heightmap
//TODO doesn't work
//low_detail_or is used to highlight low detail area
void update_game_state_slope(float min_detail_z, float detail_dx, int low_detail_or){
    //TODO remover esses calculos da main
    float sin_angle = sin(camera.angle);
    float cos_angle = cos(camera.angle);

    // Left-most point of the FOV
    float plx = cos_angle * camera.zfar + sin_angle * camera.zfar;
    float ply = sin_angle * camera.zfar - cos_angle * camera.zfar;

    // Right-most point of the FOV
    float prx = cos_angle * camera.zfar - sin_angle * camera.zfar;
    float pry = sin_angle * camera.zfar + cos_angle * camera.zfar;
    
    // Loop SCREEN_WIDTH rays from left to right
    for (int i = 0; i < SCREEN_WIDTH; i+=2) {
        float delta_x = (plx + (prx - plx) / SCREEN_WIDTH * i) / camera.zfar;
        float delta_y = (ply + (pry - ply) / SCREEN_WIDTH * i) / camera.zfar;
        
        short *dst = (short*)backbuffer + SCREEN_WIDTH * (SCREEN_HEIGHT-1)  + i;

        // Ray (x,y) coords
        float rx = camera.x;
        float ry = camera.y;

        // Store the tallest projected height per-ray
        int tallest_height = SCREEN_HEIGHT;

        // Loop all depth units until the zfar distance limit
        float z = 1;
        //~ unsigned int zstep = 1 << 0;
        float fzstep = 1, stepdelta = 0;
        
        for (; z < min_detail_z; z += fzstep) {
            rx += delta_x;
            ry += delta_y;

            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((MAP_N * ((int)(ry) & (MAP_N - 1))) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                draw_col(dst+1, i, tallest_height, proj_height, texture_map[map_offset]);
                dst = draw_col(dst, i, tallest_height, proj_height, texture_map[map_offset]);
                tallest_height = proj_height;
            }
        }
        int lastheight = 0;
        for (; z < camera.zfar*1;) {
            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((MAP_N * ((int)(ry) & (MAP_N - 1))) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                draw_col(dst+1, i+1, tallest_height, proj_height, texture_map[map_offset] | low_detail_or);
                dst = draw_col(dst, i, tallest_height, proj_height, texture_map[map_offset] | low_detail_or);
                tallest_height = proj_height;
                
		//~ if (lastheight - proj_height) {
			//~ fzstep += detail_dx;
			//~ z += 1;
			//~ rx += delta_x;
			//~ ry += delta_y;
			//~ lastheight = proj_height;
			//~ continue;
		//~ }
            }
	    lastheight = proj_height;
	    fzstep += detail_dx;
	    rx += delta_x * fzstep;
	    ry += delta_y * fzstep;
            //~ zstep += zstep >> 0;
            //~ fzstep *= 1.04;
            //~ fzstep += fzstep * (10 / 256.0f);
        }
        
        //~ draw_col(dst, i, tallest_height, 0, 0);
    }
}

//Draw with double width pixels (half horizontal resolution)
//Detail drops off gradually past min_detail_z
//low_detail_or is used to highlight low detail area
__attribute__((noinline)) void update_game_state_dbl(float min_detail_z, float detail_dx, int low_detail_or){
    //TODO remover esses calculos da main
    float sin_angle = sin(camera.angle);
    float cos_angle = cos(camera.angle);
    const float zfar = camera.zfar;
    const float inv_zfar = 1/zfar;

    // Left-most point of the FOV
    float plx = cos_angle * camera.zfar + sin_angle * camera.zfar;
    float ply = sin_angle * camera.zfar - cos_angle * camera.zfar;

    // Right-most point of the FOV
    float prx = cos_angle * camera.zfar - sin_angle * camera.zfar;
    float pry = sin_angle * camera.zfar + cos_angle * camera.zfar;
    
    // Loop SCREEN_WIDTH rays from left to right
    for (int i = 0; i < SCREEN_WIDTH; i+=2) {
        float delta_x = (plx + (prx - plx) / SCREEN_WIDTH * i) / camera.zfar;
        float delta_y = (ply + (pry - ply) / SCREEN_WIDTH * i) / camera.zfar;
        
        short *dst = (short*)backbuffer + SCREEN_WIDTH * (SCREEN_HEIGHT-1)  + i;

        // Ray (x,y) coords
        float rx = camera.x;
        float ry = camera.y;

        // Store the tallest projected height per-ray
        int tallest_height = SCREEN_HEIGHT;

        // Loop all depth units until the zfar distance limit
        float z = 1;
        //~ unsigned int zstep = 1 << 0;
        float fzstep = 1, stepdelta = 0;
        
        //Draw with constant step for stable, full detail
        for (; z < min_detail_z; z += fzstep) {
            rx += delta_x;
            ry += delta_y;

            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((((int)(ry) & (MAP_N - 1)) << MAP_SHIFT) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                dst = draw_col_dbl(dst, i, tallest_height, proj_height, texture_map[map_offset]);
                tallest_height = proj_height;
            }
        }
        
        //Make larger steps as we get farther
        for (; z < camera.zfar*1; z += fzstep) {
            rx += delta_x * fzstep;
            ry += delta_y * fzstep;
            fzstep += detail_dx;

            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((((int)(ry) & (MAP_N - 1)) << MAP_SHIFT) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                dst = draw_col_dbl(dst, i, tallest_height, proj_height, texture_map[map_offset] | low_detail_or);
                tallest_height = proj_height;
            }
        }
        
    }
}

//Detail drops off gradually past min_detail_z
//low_detail_or is used to highlight low detail area
__attribute__((noinline)) void update_game_state(float min_detail_z, float detail_dx, int low_detail_or){
    //TODO remover esses calculos da main
    float sin_angle = sin(camera.angle);
    float cos_angle = cos(camera.angle);
    const float zfar = camera.zfar;
    const float inv_zfar = 1/zfar;

    // Left-most point of the FOV
    float plx = cos_angle * camera.zfar + sin_angle * camera.zfar;
    float ply = sin_angle * camera.zfar - cos_angle * camera.zfar;

    // Right-most point of the FOV
    float prx = cos_angle * camera.zfar - sin_angle * camera.zfar;
    float pry = sin_angle * camera.zfar + cos_angle * camera.zfar;
    
    // Loop SCREEN_WIDTH rays from left to right
    for (int i = 0; i < SCREEN_WIDTH; i+=1) {
        float delta_x = (plx + (prx - plx) / SCREEN_WIDTH * i) / camera.zfar;
        float delta_y = (ply + (pry - ply) / SCREEN_WIDTH * i) / camera.zfar;
        
        short *dst = (short*)backbuffer + SCREEN_WIDTH * (SCREEN_HEIGHT-1)  + i;

        // Ray (x,y) coords
        float rx = camera.x;
        float ry = camera.y;

        // Store the tallest projected height per-ray
        int tallest_height = SCREEN_HEIGHT;

        // Loop all depth units until the zfar distance limit
        float z = 1;
        //~ unsigned int zstep = 1 << 0;
        float fzstep = 1, stepdelta = 0;
        
        //Draw with constant step for stable, full detail
        for (; z < min_detail_z; z += fzstep) {
            rx += delta_x;
            ry += delta_y;

            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((((int)(ry) & (MAP_N - 1)) << MAP_SHIFT) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                dst = draw_col(dst, i, tallest_height, proj_height, texture_map[map_offset]);
                tallest_height = proj_height;
            }
        }
        
        //Make larger steps as we get farther
        for (; z < camera.zfar*1; z += fzstep) {
            rx += delta_x * fzstep;
            ry += delta_y * fzstep;
            fzstep += detail_dx;

            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((((int)(ry) & (MAP_N - 1)) << MAP_SHIFT) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                dst = draw_col(dst, i, tallest_height, proj_height, texture_map[map_offset] | low_detail_or);
                tallest_height = proj_height;
            }
        }
        
    }
}


//Update Game State
//Draw map with four detail levels based on distance
//Full X resolution
void update_game_state_zoned_detail(int show_detail){
    //TODO remover esses calculos da main
    float sin_angle = sin(camera.angle);
    float cos_angle = cos(camera.angle);

    // Left-most point of the FOV
    float plx = cos_angle * camera.zfar + sin_angle * camera.zfar;
    float ply = sin_angle * camera.zfar - cos_angle * camera.zfar;

    // Right-most point of the FOV
    float prx = cos_angle * camera.zfar - sin_angle * camera.zfar;
    float pry = sin_angle * camera.zfar + cos_angle * camera.zfar;
    
    // Loop SCREEN_WIDTH rays from left to right
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        float delta_x = (plx + (prx - plx) / SCREEN_WIDTH * i) / camera.zfar;
        float delta_y = (ply + (pry - ply) / SCREEN_WIDTH * i) / camera.zfar;
        
        short *dst = (short*)backbuffer + SCREEN_WIDTH * (SCREEN_HEIGHT-1)  + i;

        // Ray (x,y) coords
        float rx = camera.x;
        float ry = camera.y;

        // Store the tallest projected height per-ray
        int tallest_height = SCREEN_HEIGHT;

        // Loop all depth units until the zfar distance limit
        int z = 1;
        for (; z < camera.zfar/4; z++) {
            rx += delta_x;
            ry += delta_y;

            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((MAP_N * ((int)(ry) & (MAP_N - 1))) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                dst = draw_col(dst, i, tallest_height, proj_height, texture_map[map_offset]);
                tallest_height = proj_height;
            }
        }
        unsigned highlight = 0;
        if (show_detail)
	    highlight = 0x001f;
        for (; z < camera.zfar/2; z+=2) {
            rx += delta_x*2;
            ry += delta_y*2;

            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((MAP_N * ((int)(ry) & (MAP_N - 1))) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                dst = draw_col(dst, i, tallest_height, proj_height, texture_map[map_offset] | highlight);
                tallest_height = proj_height;
            }
        }
        if (show_detail)
	    highlight = 0x0780;
        for (; z < camera.zfar*3/4; z+=3) {
            rx += delta_x*3;
            ry += delta_y*3;

            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((MAP_N * ((int)(ry) & (MAP_N - 1))) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                dst = draw_col(dst, i, tallest_height, proj_height, texture_map[map_offset] | highlight);
                tallest_height = proj_height;
            }
        }
        if (show_detail)
	    highlight = 0xf800;
        for (; z < camera.zfar; z+=4) {
            rx += delta_x*4;
            ry += delta_y*4;

            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((MAP_N * ((int)(ry) & (MAP_N - 1))) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                dst = draw_col(dst, i, tallest_height, proj_height, texture_map[map_offset] | highlight);
                tallest_height = proj_height;
            }
        }
        
        //~ draw_col(dst, i, tallest_height, 0, 0);
    }
}

//Draw full detail, with constant steps
void update_game_state_full(){
    //TODO remover esses calculos da main
    float sin_angle = sin(camera.angle);
    float cos_angle = cos(camera.angle);

    // Left-most point of the FOV
    float plx = cos_angle * camera.zfar + sin_angle * camera.zfar;
    float ply = sin_angle * camera.zfar - cos_angle * camera.zfar;

    // Right-most point of the FOV
    float prx = cos_angle * camera.zfar - sin_angle * camera.zfar;
    float pry = sin_angle * camera.zfar + cos_angle * camera.zfar;
    
    // Loop SCREEN_WIDTH rays from left to right
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        float delta_x = (plx + (prx - plx) / SCREEN_WIDTH * i) / camera.zfar;
        float delta_y = (ply + (pry - ply) / SCREEN_WIDTH * i) / camera.zfar;
        
        short *dst = (short*)backbuffer + SCREEN_WIDTH * (SCREEN_HEIGHT-1)  + i;

        // Ray (x,y) coords
        float rx = camera.x;
        float ry = camera.y;

        // Store the tallest projected height per-ray
        int tallest_height = SCREEN_HEIGHT;

        // Loop all depth units until the zfar distance limit
        int z = 1;
        for (; z < camera.zfar; z++) {
            rx += delta_x;
            ry += delta_y;

            // Find the offset that we have to go and fetch values from the height_map
            int map_offset = ((MAP_N * ((int)(ry) & (MAP_N - 1))) + ((int)(rx) & (MAP_N - 1)));

            // Project height values and find the height on-screen
            int proj_height = (int)((camera.height - height_map[map_offset]) / z * SCALE_FACTOR + camera.horizon);
            if (proj_height < 0) proj_height = 0;
            // Only draw pixels if the new projected height is taller than the previous tallest height
            if (proj_height < tallest_height) {
                dst = draw_col(dst, i, tallest_height, proj_height, texture_map[map_offset]);
                tallest_height = proj_height;
            }
        }
        
        //~ draw_col(dst, i, tallest_height, 0, 0);
    }
}


char screen_text[20];
void render(){
    bfont_draw_str(backbuffer + SCREEN_WIDTH * 10, SCREEN_WIDTH, 0, screen_text);
    display_flip_framebuffer();
    display_clear_backbuffer(0, 0x82, 0xFF);
}

/* romdisk */
extern uint8 romdisk_boot[];
KOS_INIT_ROMDISK(romdisk_boot);

int main(void) {

    //init kos
    //~ pvr_init_defaults();
    fbdma_init();

    //initialize display
    display_initialize();

    //initialize level
    texture_map = loadmap_get_texture(0);
    height_map = loadmap_get_heights(0);

    //FPS Counter
    int number_of_frames = 0;
    uint64 start_time = timer_ms_gettime64();
    uint32 current_time = 0;

    //Main Loop
    while(!quit) 
    {
        //FPS Counter
        current_time = timer_ms_gettime64();
        if((current_time - start_time) > 1000){
            double fps = 1000.0 * (double)number_of_frames / (double)(current_time - start_time);
            sprintf(screen_text, "fps: %.2f", fps);
            number_of_frames = 0;
            start_time = current_time;
        }
        number_of_frames++;

        //Process controller input
        process_input();

        //Update Game State
	//~ update_game_state_full();
	//~ update_game_state_zoned_detail(show_low_detail);
	if (dbl_pixels)
		update_game_state_dbl(65, 0.022, show_low_detail);
	else
		update_game_state(65, 0.022, show_low_detail);
	//~ update_game_state_slope(65, 0.025, show_low_detail);

        //render
        render();

    }

    return 0;
}
