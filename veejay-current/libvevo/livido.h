/* LiViDO is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   LiViDO is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this source code; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


   LiViDO is developed by:

   Niels Elburg - http://veejay.sf.net

   Gabriel "Salsaman" Finch - http://lives.sourceforge.net

   Denis "Jaromil" Rojo - http://freej.dyne.org

   Tom Schouten - http://zwizwa.fartit.com

   Andraz Tori - http://cvs.cinelerra.org

   reviewed with suggestions and contributions from:

   Silvano "Kysucix" Galliani - http://freej.dyne.org

   Kentaro Fukuchi - http://megaui.net/fukuchi

   Jun Iio - http://www.malib.net

   Carlo Prelz - http://www2.fluido.as:8080/

*/
#ifndef __LIVIDO_H__
#define __LIVIDO_H__

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */

/* for size_t */
#include <sys/types.h>

/* API version * 100 */
#define LIVIDO_API_VERSION 100

/* Palette types */
#
/* RGB palettes */
#define LIVIDO_PALETTE_RGB888 1
#define LIVIDO_PALETTE_RGB24 1
#define LIVIDO_PALETTE_BGR888 2
#define LIVIDO_PALETTE_BGR24 2
#define LIVIDO_PALETTE_RGBA8888 3
#define LIVIDO_PALETTE_RGBA32 3
#define LIVIDO_PALETTE_ARGB8888 4
#define LIVIDO_PALETTE_ARGB32 4
#define LIVIDO_PALETTE_RGBFLOAT 5
#define LIVIDO_PALETTE_ARGBFLOAT  6
#define LIVIDO_PALETTE_RGB565 7

/* YUV palettes */
#define LIVIDO_PALETTE_YUV422P 513
#define LIVIDO_PALETTE_YV16 513

/*
LIVIDO_PALETTE_YUV422P           LIVIDO_PALETTE_YV16
[Official name 'YV16', 8 bit Y plane followed by 8
   bit 2x1 subsampled V and U planes. Planar.]
 */
/*
LIVIDO_PALETTE_YUV420P           LIVIDO_PALETTE_YV12
[8 bit Y plane followed by 8 bit 2x2 subsampled V and U planes. Planar
   (Official name YV12)]
 */
#define LIVIDO_PALETTE_YUV420P 514
#define LIVIDO_PALETTE_YV12 514
/*
LIVIDO_PALETTE_YVU420P           LIVIDO_PALETTE_I420
[Same as YUV420P , but U and V are swapped. Planar.]
*/
#define LIVIDO_PALETTE_YVU420P 515
#define LIVIDO_PALETTE_I420 515

/*
LIVIDO_PALETTE_YUV444P
[Unofficial , 8 bit Y plane followed by 8 bit U and V planes, no
   subsampling. Planar.]
*/
#define LIVIDO_PALETTE_YUV444P 516
/*
LIVIDO_PALETTE_YUVA4444P         LIVIDO_PALETTE_YUV4444P
[Unofficial, like YUV444P but with Alpha. Planar.]
*/
#define LIVIDO_PALETTE_YUV4444P 517
/*
LIVIDO_PALETTE_YUYV8888
[Like YUV 4:2:2 but with different component ordering within the
   u_int32 macropixel. Packed.]
*/
#define LIVIDO_PALETTE_YUYV8888 518
/*
LIVIDO_PALETTE_YUYV8888
[Like YUV 4:2:2 but with different component ordering within the
   u_int32 macropixel. Packed.]
 */
#define LIVIDO_PALETTE_UYVY8888 519
/* 
LIVIDO_PALETTE_UYVY8888
[YUV 4:2:2 (Y sample at every pixel, U and V sampled at every second
   pixel horizontally on each line). A macropixel contains 2 pixels in 1
   u_int32. Packed.]
 */
#define LIVIDO_PALETTE_YUV411 520
/*
LIVIDO_PALETTE_YUV411
[IEEE 1394 Digital Camera 1.04 spec. Is packed YUV format
with a 6 pixel macroblock structure containing 4 pixels.
Ordering is U2 Y0 Y1 V2 Y2 Y3. Uses same bandwith as YUV420P
Only used for SMPTE DV NTSC.]
 */


/* Pending: PALETTE YUV888, YUVA888 needed in cinerella */
#define	LIVIDO_PALETTE_YUV888 521
#define LIVIDO_PALETTE_YUVA8888 522

/* Alpha palettes */
/*
Alpha palettes have two uses: 1) for masks, 2) to split colour inputs into single colour channels, or to combine single colour channels into a combined channel. The order of colour channels is the same as the order in the combined channel. For example if an input in RGB24 palette is split into 3 non-mask alpha channels, then the alpha channels will be in the order: Red, Green, Blue. A single non-mask alpha channel would represent the luminance.

 */
#define LIVIDO_PALETTE_A1 1025
#define LIVIDO_PALETTE_A8 1026
#define LIVIDO_PALETTE_AFLOAT 1027

/* Filter flags */
#define LIVIDO_FILTER_NON_REALTIME    (1<<0)
#define LIVIDO_FILTER_CAN_DO_INPLACE  (1<<1)
#define LIVIDO_FILTER_STATELESS       (1<<2)
#define LIVIDO_FILTER_IS_CONVERTOR    (1<<3)

/* Channel flags */
#define LIVIDO_CHANNEL_CHANGE_UNADVISED (1<<0)
/*
 LIVIDO_CHANNEL_CHANGE_UNADVISED
plugin MAY use this flag to tell the host, that changing of channel size causes possibly unwanted behaviour of the filter. Unwanted behaviour can for example be reseting the accumulated values which causes the visual result of filter to change in unexpected way or maybe the next call to process function will take disproportional amount of time due to reinitialization. Host is safe to ignore the flag and plugin MUST still be useful, though functionality may suffer.
 */
#define LIVIDO_CHANNEL_PALETTE_UNADVISED (1<<1)
/*
LIVIDO_CHANNEL_PALETTE_UNADVISED
plugin MAY use this flag to tell the host, that changing of channel palette causes possibly unwanted behaviour of the filter. Unwanted behaviour can for example be reseting the accumulated values which causes the visual result of filter to change in unexpected way or maybe the next call to process function will take disproportional amount of time due to reinitialization. Host is safe to ignore the flag and plugin MUST still be useful, though functionality may suffer.
*/

/* Parameter flags */
#define LIVIDO_PARAMETER_CHANGE_UNADVISED (1<<0)
/*
plugin MAY use this flag to tell the host, that changing of this parameter causes possibly unwanted behaviour of the filter. Unwanted behaviour can for example be reseting the accumulated values which causes the visual result of filter to change in unexpected way or maybe the next call to process function will take disproportional amount of time due to reinitialization. Host is safe to ignore the flag and plugin MUST still be useful, though functionality may suffer. 

*/


/* Property flags */
#define LIVIDO_PROPERTY_READONLY (1<<0)

/* YUV sampling types */
#define LIVIDO_YUV_SAMPLING_NONE 0
#define LIVIDO_YUV_SAMPLING_SMPTE 1
#define LIVIDO_YUV_SAMPLING_JPEG 2
#define LIVIDO_YUV_SAMPLING_MPEG2 3
#define LIVIDO_YUV_SAMPLING_DVPAL 4
#define LIVIDO_YUV_SAMPLING_DVNTSC 5

/* Interlace types */
#define LIVIDO_INTERLACE_NONE            0
#define LIVIDO_INTERLACE_TOPFIRST        1
#define LIVIDO_INTERLACE_BOTTOMFIRST     2
#define LIVIDO_INTERLACE_PROGRESSIVE     3

/* Livido errors */
/* Core errors */
#define LIVIDO_NO_ERROR 0
#define LIVIDO_ERROR_MEMORY_ALLOCATION 1
#define LIVIDO_ERROR_PROPERTY_READONLY 2
#define LIVIDO_ERROR_NOSUCH_ELEMENT 3
#define LIVIDO_ERROR_NOSUCH_PROPERTY 4
#define LIVIDO_ERROR_WRONG_ATOM_TYPE 5

/* Plugin errors */
#define LIVIDO_ERROR_TOO_MANY_INSTANCES 6
#define LIVIDO_ERROR_HARDWARE 7

/* Atom types */
/* Fundamental atoms */
#define LIVIDO_ATOM_TYPE_INT 1
#define LIVIDO_ATOM_TYPE_DOUBLE 2
#define LIVIDO_ATOM_TYPE_BOOLEAN 3
#define LIVIDO_ATOM_TYPE_STRING 4

/* Pointer atoms */
#define LIVIDO_ATOM_TYPE_VOIDPTR 65
#define LIVIDO_ATOM_TYPE_PORTPTR 66

/* Port types */
#define LIVIDO_PORT_TYPE_PLUGIN_INFO 1
#define LIVIDO_PORT_TYPE_FILTER_CLASS 2
#define LIVIDO_PORT_TYPE_FILTER_INSTANCE 3
#define LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE 4
#define LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE 5
#define LIVIDO_PORT_TYPE_CHANNEL 6
#define LIVIDO_PORT_TYPE_PARAMETER 7
#define LIVIDO_PORT_TYPE_GUI 8

/*

A port is a set of one or more properties

Each port has a mandatory property called "type" (see below), depending upon "type" property port has other mandatory and optional properties.

"type" can be one of:

    * LIVIDO_PORT_TYPE_PLUGIN_INFO : Information about plugin and list of filter classes it includes
    * LIVIDO_PORT_TYPE_FILTER_CLASS : Descriptive information about single filter class
    * LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE : Information about what kinds of channels filter accepts
    * LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE : Information about what kinds of parameters filter has
    * LIVIDO_PORT_TYPE_FILTER_INSTANCE : All data about an instance
    * LIVIDO_PORT_TYPE_CHANNEL : Instantination of a channel
    * LIVIDO_PORT_TYPE_PARAMETER : Instantination of a parameter 

    * LIVIDO_PORT_TYPE_GUI : described in the separate livido GUI extension (TODO) 

"type" is a single valued property with atom_type LIVIDO_ATOM_TYPE_INT. 

The host should provide its own mediation layer for providing a datacontainer for its port.
This host uses the mediation layer, where a new port is defined.

*/
#ifndef HAVE_LIVIDO_PORT_T
#define HAVE_LIVIDO_PORT_T
    typedef void livido_port_t;
#endif

    extern void livido_port_free(livido_port_t * port);
    extern livido_port_t *livido_port_new(int port_type);

    extern int livido_property_set(livido_port_t * port, const char *key,
				   int atom_type, int num_elems,
				   void *value);
    extern int livido_property_get(livido_port_t * port, const char *key,
				   int idx, void *value);

    extern int livido_property_num_elements(livido_port_t * port,
					    const char *key);
    extern int livido_property_atom_type(livido_port_t * port,
					 const char *key);
    extern size_t livido_property_element_size(livido_port_t * port,
					       const char *key,
					       const int idx);
    extern char **livido_list_properties(livido_port_t * port);

    extern void *livido_malloc_f(size_t size);
    extern void livido_free_f(void *ptr);
    extern void *livido_memset_f(void *s, int c, size_t n);
    extern void *livido_memcpy_f(void *dest, const void *src, size_t n);

    typedef livido_port_t *(*livido_setup_f) (void);
    typedef int (*livido_init_f) (livido_port_t * filter_instance);
    typedef int (*livido_process_f) (livido_port_t * filter_instance,
				     double timestamp);
    typedef int (*livido_deinit_f) (livido_port_t * filter_instance);

#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				// #ifndef __LIVIDO_H__
