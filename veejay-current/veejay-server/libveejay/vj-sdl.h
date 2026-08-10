/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef VJ_SDL_H
#define VJ_SDL_H

#include <config.h>
#include <stddef.h>
#include <stdint.h>
#ifdef HAVE_SDL
typedef struct vj_sdl_present_timing_t {
    uint64_t convert_ns;
    uint64_t upload_ns;
    uint64_t present_block_ns;
    uint64_t completed_ns;
    double refresh_interval_s;
    unsigned int timing_generation;
    int vsync_enabled;
} vj_sdl_present_timing_t;

void *vj_sdl_allocate(VJFrame *frame, int k, int m, int s, int borderless);
void vj_sdl_resize( void *ptr ,int x, int y, int scaled_width, int scaled_height, int fs );
int vj_sdl_init(void *ptr, int x, int y,int w, int h, int scaled_width, int scaled_height, char *caption, int show, int fs,int vjfmt, float fps, double *vsync);
void vj_sdl_grab(void *ptr, int status);
void vj_sdl_get_position(void *ptr, int *dst_x, int *dst_y );
void vj_sdl_convert_and_update_screen(void *ptr, uint8_t ** yuv420);
void vj_sdl_put_to_screen(void *ptr, uint8_t *pixels_to_render);
void vj_sdl_process_pending(void *ptr);
void vj_sdl_set_perf(void *ptr, void *perf);
void vj_sdl_convert_to_screen(void *ptr, VJFrame *frame_to_dsplay, uint8_t *pixels);
int vj_sdl_present_frame(void *ptr, VJFrame *frame);
int vj_sdl_prepare_frame(void *ptr, VJFrame *frame, vj_sdl_present_timing_t *timing);
int vj_sdl_present_prepared(void *ptr, vj_sdl_present_timing_t *timing);
void vj_sdl_discard_prepared(void *ptr);
int vj_sdl_get_present_mode(void *ptr, int *vsync_enabled,
                            double *refresh_interval_s,
                            unsigned int *timing_generation);
int vj_sdl_get_backend(void *ptr, char *name, size_t name_size,
                       int *direct_lock_disabled);
void vj_sdl_shutdown(void *ptr);
void vj_sdl_quit();
void vj_sdl_free(void *ptr);
uint8_t* vj_sdl_get_buffer( void *ptr, int index );
void vj_sdl_enable_screensaver();
void vj_sdl_preroll(void *ptr, int frame_count);
int vj_sdl_set_fullscreen(void *ptr, int enabled);
int vj_sdl_set_window_size(void *ptr, int w, int h, int x, int y);
int vj_sdl_set_identify(void *ptr, int display_number);
int vj_sdl_set_display_target(void *ptr, int x, int y);
int vj_sdl_get_display_index(void *ptr);
#endif
#endif
