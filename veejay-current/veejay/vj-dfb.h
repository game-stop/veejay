/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */
#ifndef VJ_DFB_H
#define VJ_DFB_H
#include <config.h>

#ifdef HAVE_DIRECTFB
#include <directfb.h>
#include <linux/fb.h>
#include <stdint.h>

typedef struct vj_dfb_t {
    IDirectFB *d;
    IDirectFBDisplayLayer *crtc2;
    IDirectFBSurface *bufs[3];
    IDirectFBSurface *c2frame;
    IDirectFBSurface *frame;
    DFBRectangle drect;
    DFBSurfacePixelFormat frame_format;
    IDirectFBInputDevice *keyboard;
    IDirectFBEventBuffer *buffer;
    unsigned int screen_width;
    unsigned int screen_height;
    void *screen_framebuffer;
    int screen_pitch;
    int stretch;
    int width;
    int height;
    char dev_name[100];
    int norm;
} vj_dfb;

vj_dfb *vj_dfb_allocate(int width, int height, int norm);

int vj_dfb_init(vj_dfb * dfb);

int vj_dfb_lock(vj_dfb * dfb);

int vj_dfb_unlock(vj_dfb * dfb);

int vj_dfb_update_yuv_overlay(vj_dfb * dfb, uint8_t ** yuv420);

int vj_dfb_free(vj_dfb * dfb);

void vj_dfb_wait_for_sync();

int vj_dfb_get_pitch(vj_dfb * framebuffer);

int vj_dfb_update_yuv_overlay(vj_dfb * framebuffer, uint8_t ** frame);

uint8_t *vj_dfb_get_address(vj_dfb * framebuffer);

int vj_dfb_get_output_field(vj_dfb * framebuffer);
#endif
#endif
