/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
 * See COPYING for software license and distribution details
 */

#ifndef LVD_COMMON_H
#define LVD_COMMON_H

#define GIMP_rgb2yuv(r,g,b,y,u,v)\
 {\
        float Ey = (0.299 * (float)r) + (0.587 * (float)g) + (0.114 * (float) b);\
        float Eu = (-0.168736 * (float)r) - (0.331264 * (float)g) + (0.500 * (float)b) + 128.0;\
        float Ev = (0.500 * (float)r) - (0.418688 * (float)g) - (0.081312 * (float)b)+ 128.0;\
    y = myround(Ey);\
        u = myround(Eu);\
        v = myround(Ev);\
 }

#define CCIR601_rgb2yuv(r,g,b,y,u,v) \
 {\
 float Ey = (0.299f * (float)r) + (0.587f * (float) g) + (0.114f * (float)b );\
 float Eu = (0.713f * ( ((float)r) - Ey ) );\
 float Ev = (0.564f * ( ((float)b) - Ey ) );\
 y = (int) ( 255.0 * Ey );\
 u = (int) (( 255.0 * Eu ) + 128);\
 v = (int) (( 255.0 * Ev ) + 128);\
}

#endif
