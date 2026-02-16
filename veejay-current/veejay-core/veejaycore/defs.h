/*
 * Copyright (C) 2002-2015 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef VJEDEF_H
#define VJEDEF_H

#define V_STATUS 1
#define V_CMD 0

#define FMT_420 0
#define FMT_420F 2
#define FMT_422 1
#define FMT_422F 3
#define FMT_444 4

#define FMT_RGB24	2
#define FMT_RGB32	1

enum
{
	VJ_CMD_PORT=0,
	VJ_STA_PORT=1,
	VJ_CMD_MCAST=3,
	VJ_CMD_MCAST_IN=4,
	VJ_MSG_PORT=5,
	VJ_CMD_OSC=2,
};

typedef struct VJFrame_t 
{
    int stride[4];
    uint8_t *data[4];
    int uv_len;
    int len;
    int uv_width;
    int uv_height;
    int shift_v;
    int shift_h;
    int format;
    int width;
    int height;
    int ssm;
    int stand; //ccir/jpeg
    float   fps;
    double  timecode;
    int yuv_fmt;
    int range;
    int offset;
    int jobnum;
    int totaljobs;
    uint8_t **local;
    int out_width;
    int out_height;
    long frame_num;
    int queue_index;
} VJFrame __attribute__((aligned(16)));

typedef struct VJFrameInfo_t
{
    int width;
    int height;
    float fps;
    int64_t timecode;
    uint8_t inverse;
} VJFrameInfo __attribute__((aligned(16)));

typedef struct VJRectangle_t
{
        int top;
        int bottom;
        int left;
        int right;
} VJRectangle;
#endif
