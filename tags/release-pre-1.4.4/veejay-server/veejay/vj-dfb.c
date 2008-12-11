/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nwelburg@gmail.com>
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
/* DirectFB support, this file is part of veejay 
   code inspired by mplayer and dfbtv 
   
   */

#include <stdio.h>
#include <stdint.h>
#include <libvjmem/vjmem.h>
#include <stdlib.h>
#include <config.h>
#ifdef HAVE_DIRECTFB
#include "vj-dfb.h"
void *vj_dfb_allocate(int width, int height, int norm)
{
    vj_dfb *dfb = (vj_dfb *) malloc(sizeof(vj_dfb));
    if (!dfb)
	return NULL;
    dfb->buffer = NULL;
    dfb->screen_width = 0;
    dfb->screen_height = 0;
    dfb->screen_framebuffer = 0;
    dfb->screen_pitch = 0;
    sprintf(dfb->dev_name, "%s", "/dev/fb0");
    fprintf(stderr, "DirectFB: using [%s]\n", dfb->dev_name);
    dfb->width = width;
    dfb->height = height;
    dfb->norm = norm;
    dfb->stretch = 0;
    return (void*)dfb;
}

int vj_dfb_init(void *ptr)
{
    vj_dfb *framebuffer = (vj_dfb*) ptr;
    DFBDisplayLayerConfig dlc;
    DFBDisplayLayerConfigFlags failed;
    DFBSurfaceDescription dsc;
    int n;

    DirectFBInit(0, 0);
    DirectFBSetOption("fbdev", framebuffer->dev_name);
    DirectFBSetOption("no-cursor", "");
    DirectFBSetOption("bg-color", "00000000");
    DirectFBSetOption("matrox-crtc2", "");
    DirectFBSetOption("matrox-tv-standard",
		      framebuffer->norm ? "pal" : "ntsc");

    fprintf(stderr, "DirectFB: PAL = %s\n",
	    framebuffer->norm ? "pal" : "ntsc");

    framebuffer->bufs[0] = framebuffer->bufs[1] = framebuffer->bufs[2] =
	NULL;

    DirectFBCreate(&framebuffer->d);

    framebuffer->d->GetDisplayLayer(framebuffer->d, 2,
				    &(framebuffer->crtc2));

    if (!framebuffer->crtc2) {
	fprintf(stderr, "Unable to initialize display layern");
	return -1;
    }

    framebuffer->crtc2->SetCooperativeLevel(framebuffer->crtc2,
					    DLSCL_EXCLUSIVE);

    framebuffer->d->GetInputDevice(framebuffer->d, DIDID_KEYBOARD,
				   &(framebuffer->keyboard));

    framebuffer->keyboard->CreateEventBuffer(framebuffer->keyboard,
					     &(framebuffer->buffer));

    framebuffer->buffer->Reset(framebuffer->buffer);

    dlc.flags = DLCONF_PIXELFORMAT | DLCONF_BUFFERMODE;
    dlc.buffermode = DLBM_BACKVIDEO;
    dlc.pixelformat = DSPF_I420;


    dsc.flags = DSDESC_WIDTH | DSDESC_HEIGHT | DSDESC_PIXELFORMAT;
    dsc.width = framebuffer->width;
    dsc.height = framebuffer->height;
    dsc.pixelformat = dlc.pixelformat;

    if (framebuffer->d->CreateSurface(framebuffer->d, &dsc,
				      &framebuffer->frame) != DFB_OK)
	return -1;

    if (framebuffer->crtc2->TestConfiguration(framebuffer->crtc2,
					      &dlc, &failed) != DFB_OK) {
	fprintf(stderr, "TestConfiguration failed\n");
	return -1;
    }

    framebuffer->crtc2->SetConfiguration(framebuffer->crtc2, &dlc);

    framebuffer->crtc2->GetSurface(framebuffer->crtc2,
				   &(framebuffer->c2frame)
	);

    framebuffer->c2frame->GetSize(framebuffer->c2frame,
				  &(framebuffer->screen_width),
				  &(framebuffer->screen_height)
	);

    fprintf(stderr, "DirectFB: Screen is %d x %d , Video is %d x %d\n",
	    framebuffer->screen_width, framebuffer->screen_height,
	    framebuffer->width, framebuffer->height);

    if (framebuffer->width != framebuffer->screen_width ||
	framebuffer->height != framebuffer->screen_height) {
	framebuffer->stretch = 1;
    }

    framebuffer->drect.x =
	(framebuffer->screen_width - framebuffer->width) / 2;
    framebuffer->drect.y =
	(framebuffer->screen_height - framebuffer->height) / 2;
    framebuffer->drect.w = framebuffer->width;
    framebuffer->drect.h = framebuffer->height;

    framebuffer->c2frame->Clear(framebuffer->c2frame, 0, 0, 0, 0xff);
    framebuffer->c2frame->Flip(framebuffer->c2frame, NULL, 0);
    framebuffer->c2frame->Clear(framebuffer->c2frame, 0, 0, 0, 0xff);


    framebuffer->c2frame->GetPixelFormat(framebuffer->c2frame,
					 &(framebuffer->frame_format)
	);

    framebuffer->frame->GetPixelFormat(framebuffer->frame,
				       &(framebuffer->frame_format)
	);

    fprintf(stderr, "DirectFB: Frame format = ");
    switch (framebuffer->frame_format) {
    case DSPF_I420:
	fprintf(stderr, "I420\n");
	break;
    default:
	fprintf(stderr, "Wrong pixel format!\n");
	break;
    }

    framebuffer->crtc2->SetOpacity(framebuffer->crtc2, 0xff);

    return 0;
}

int vj_dfb_lock(void *ptr)
{
    vj_dfb *framebuffer = (vj_dfb*) ptr;
    if (!framebuffer)
	return -1;
    framebuffer->c2frame->Lock(framebuffer->c2frame,
			       DSLF_WRITE,
			       &(framebuffer->screen_framebuffer),
			       &(framebuffer->screen_pitch)
	);
    return 0;
}

int vj_dfb_unlock(void *ptr)
{
	vj_dfb *framebuffer = (vj_dfb*) ptr;
    if (!framebuffer)
	return -1;

    framebuffer->c2frame->Unlock(framebuffer->c2frame);
    return 0;
}


int vj_dfb_free(void *ptr)
{
	vj_dfb *framebuffer = (vj_dfb*)ptr;
    if (!framebuffer)
	return -1;

    if (framebuffer->buffer) {
	framebuffer->buffer->Release(framebuffer->buffer);
    }
    if (framebuffer->keyboard) {
	framebuffer->keyboard->Release(framebuffer->keyboard);
    }
    if (framebuffer->c2frame) {
	framebuffer->c2frame->Release(framebuffer->c2frame);
    }
    if (framebuffer->crtc2) {
	framebuffer->crtc2->Release(framebuffer->crtc2);
    }
    if (framebuffer->d) {
	framebuffer->d->Release(framebuffer->d);
    }
    return 0;
}


void vj_dfb_wait_for_sync( void *ptr)
{
    vj_dfb *framebuffer = (vj_dfb*) ptr;
    framebuffer->d->WaitForSync(framebuffer->d);
}

int vj_dfb_get_pitch( void *ptr)
{
	vj_dfb *framebuffer = (vj_dfb*) ptr;
    return (int) framebuffer->screen_pitch;
}

uint8_t *vj_dfb_get_address(void *ptr)
{
	vj_dfb *framebuffer = (vj_dfb*) ptr;
    return (uint8_t *) framebuffer->screen_framebuffer;
}

int vj_dfb_get_output_field( void *ptr)
{
	vj_dfb *framebuffer = (vj_dfb*) ptr;
    int fieldid;
    framebuffer->crtc2->GetCurrentOutputField(framebuffer->crtc2,
					      &fieldid);
    return fieldid;
}

/*
int vj_dfb_update_yuv_overlay(void *ptr , uint8_t **frame) {
	uint8_t *output;
	vj_dfb *framebuffer = (vj_dfb*) ptr;
	int pitch,i,p;
	while(vj_dfb_get_output_field(framebuffer) != 0) {
		fprintf(stderr, "resyncing...\n");
		vj_dfb_wait_for_sync(framebuffer);
	}

	if(vj_dfb_lock(framebuffer)!=0) return -1;

	output = vj_dfb_get_address(framebuffer);

	pitch = vj_dfb_get_pitch(framebuffer);

	p = pitch;
	if(p > framebuffer->width) p = framebuffer->width;

	for(i=0; i < framebuffer->height; i++) {
		veejay_memcpy(output+i*pitch, frame[0]+i*framebuffer->width, p);
	}

	output += pitch * framebuffer->height;
	p = p / 2;
	for(i=0; i < framebuffer->height/2; i++) {
		veejay_memcpy(output+i*pitch/2, frame[1] + i * framebuffer->width/2, p);
	}	
			
	output += pitch * framebuffer->height/4;
	for(i=0; i < framebuffer->height/2; i++) {
		veejay_memcpy(output+i * pitch/2, frame[2] + i * framebuffer->width/2, p);
	}

	if(vj_dfb_unlock(framebuffer)!=0) return -1;

	vj_dfb_wait_for_sync(framebuffer);
	return 0;

}
*/

int vj_dfb_update_yuv_overlay(void *ptr, uint8_t ** frame)
{
	vj_dfb *framebuffer = (vj_dfb*) ptr;
    void *dst;
    int i, p, pitch;
    if (framebuffer->frame->Lock(framebuffer->frame,
				 DSLF_WRITE, &dst, &pitch) != DFB_OK)
	return -1;

    p = pitch;

    if (p > framebuffer->width)
	p = framebuffer->width;

    for (i = 0; i < framebuffer->height; i++) {
	veejay_memcpy(dst + i * pitch, frame[0] + i * framebuffer->width, p);
    }

    dst += pitch * framebuffer->height;
    p = p / 2;
    for (i = 0; i < framebuffer->height / 2; i++) {
	veejay_memcpy(dst + i * pitch / 2, frame[1] + i * framebuffer->width / 2,
	       p);
    }

    dst += pitch * framebuffer->height / 4;
    for (i = 0; i < framebuffer->height / 2; i++) {
	veejay_memcpy(dst + i * pitch / 2, frame[2] + i * framebuffer->width / 2,
	       p);
    }

    framebuffer->frame->Unlock(framebuffer->frame);

    framebuffer->c2frame->SetBlittingFlags(framebuffer->c2frame,
					   DSBLIT_NOFX);

    if (framebuffer->stretch) {
	framebuffer->c2frame->StretchBlit(framebuffer->c2frame,
					  framebuffer->frame, NULL,
					  //&(framebuffer->drect)
					  NULL);
    } else {
	framebuffer->c2frame->Blit(framebuffer->c2frame,
				   framebuffer->frame,
				   NULL,
				   framebuffer->drect.x,
				   framebuffer->drect.y);

    }

    framebuffer->c2frame->Flip(framebuffer->c2frame, NULL,
			       DSFLIP_WAITFORSYNC);

    return 0;
}
#endif
