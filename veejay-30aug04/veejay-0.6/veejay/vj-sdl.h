#ifndef VJ_SDL_H
#define VJ_SDL_H

#include <SDL/SDL.h>
#include "vj-dv.h"


typedef struct vj_sdl_t {
    SDL_Surface *screen;
    SDL_Overlay *yuv_overlay;
    SDL_Rect rectangle;
    SDL_Event event;
    int use_yuv_direct;
    int use_yuv_hwaccel;
    int show_cursor;
    int mouse_motion;
    int use_keyboard;
    int pix_format;
    int width;
    int height;
    int sw_scale_width;
    int sw_scale_height;
    int frame_size;
    char last_error[255];
    int  custom_geo[2];
} vj_sdl;

vj_sdl *vj_sdl_allocate(int width, int height);
void vj_sdl_set_geometry(vj_sdl *sdl, int w, int h);
void vj_sdl_show(vj_sdl *vjsdl);
int vj_sdl_init(vj_sdl * vjsdl, int scaled_width, int scaled_height,char *caption, int show);
int vj_sdl_lock(vj_sdl * vjsdl);
int vj_sdl_unlock(vj_sdl * vjsdl);
int vj_sdl_update_yuv_overlay(vj_sdl * vjsdl, uint8_t ** yuv420);
int vj_sdl_direct_yuv_overlay(vj_sdl * vjsdl, uint8_t * buffer, int buflen,
			      int dataformat);
void vj_sdl_free(vj_sdl * vjsdl);

#endif
