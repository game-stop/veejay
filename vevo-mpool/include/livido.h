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
#define	LIVIDO_BEGIN_DECLS extern "C" {
#define LIVIDO_END_DECLS }
#else
#define LIVIDO_BEGIN_DECLS
#define LIVIDO_END_DECLS
#endif

#ifndef FALSE
#define FALSE   (0)
#endif

#ifndef TRUE
#define TRUE    (!FALSE)
#endif

LIVIDO_BEGIN_DECLS
#include <sys/types.h>
#define LIVIDO_API_VERSION 100
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
#define LIVIDO_PALETTE_YUV422P 513
#define LIVIDO_PALETTE_YV16 513
#define LIVIDO_PALETTE_YUV420P 514
#define LIVIDO_PALETTE_YV12 514
#define LIVIDO_PALETTE_YVU420P 515
#define LIVIDO_PALETTE_I420 515
#define LIVIDO_PALETTE_YUV444P 516
#define LIVIDO_PALETTE_YUV4444P 517
#define LIVIDO_PALETTE_YUYV8888 518
#define LIVIDO_PALETTE_UYVY8888 519
#define LIVIDO_PALETTE_YUV411 520
#define	LIVIDO_PALETTE_YUV888 521
#define LIVIDO_PALETTE_YUVA8888 522
#define LIVIDO_PALETTE_A1 1025
#define LIVIDO_PALETTE_A8 1026
#define LIVIDO_PALETTE_AFLOAT 1027
#define LIVIDO_FILTER_NON_REALTIME    (1<<0)
#define LIVIDO_FILTER_CAN_DO_INPLACE  (1<<1)
#define LIVIDO_FILTER_STATELESS       (1<<2)
#define LIVIDO_FILTER_IS_CONVERTOR    (1<<3)
#define LIVIDO_CHANNEL_CHANGE_UNADVISED (1<<0)
#define LIVIDO_CHANNEL_PALETTE_UNADVISED (1<<1)
#define LIVIDO_PARAMETER_CHANGE_UNADVISED (1<<0)
#define LIVIDO_PROPERTY_READONLY (1<<0)
#define LIVIDO_YUV_SAMPLING_NONE 0
#define LIVIDO_YUV_SAMPLING_SMPTE 1
#define LIVIDO_YUV_SAMPLING_JPEG 2
#define LIVIDO_YUV_SAMPLING_MPEG2 3
#define LIVIDO_YUV_SAMPLING_DVPAL 4
#define LIVIDO_YUV_SAMPLING_DVNTSC 5
#define LIVIDO_INTERLACE_NONE            0
#define LIVIDO_INTERLACE_TOPFIRST        1
#define LIVIDO_INTERLACE_BOTTOMFIRST     2
#define LIVIDO_INTERLACE_PROGRESSIVE     3
#define LIVIDO_NO_ERROR 0
#define LIVIDO_ERROR_MEMORY_ALLOCATION 1
#define LIVIDO_ERROR_PROPERTY_READONLY 2
#define LIVIDO_ERROR_NOSUCH_ELEMENT 3
#define LIVIDO_ERROR_NOSUCH_PROPERTY 4
#define LIVIDO_ERROR_WRONG_ATOM_TYPE 5
#define LIVIDO_ERROR_TOO_MANY_INSTANCES 6
#define LIVIDO_ERROR_HARDWARE 7
#define LIVIDO_ATOM_TYPE_INT 1
#define LIVIDO_ATOM_TYPE_DOUBLE 2
#define LIVIDO_ATOM_TYPE_BOOLEAN 3
#define LIVIDO_ATOM_TYPE_STRING 4
#define LIVIDO_ATOM_TYPE_VOIDPTR 65
#define LIVIDO_ATOM_TYPE_PORTPTR 66
#define LIVIDO_PORT_TYPE_PLUGIN_INFO 1
#define LIVIDO_PORT_TYPE_FILTER_CLASS 2
#define LIVIDO_PORT_TYPE_FILTER_INSTANCE 3
#define LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE 4
#define LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE 5
#define LIVIDO_PORT_TYPE_CHANNEL 6
#define LIVIDO_PORT_TYPE_PARAMETER 7
#define LIVIDO_PORT_TYPE_GUI 8
#ifndef HAVE_LIVIDO_PORT_T
#define HAVE_LIVIDO_PORT_T
typedef void livido_port_t;
#endif

//#define FUNCSTRUCT
/*
	Uncomment the #define above and recompile all
 */


typedef int (*livido_init_f) (livido_port_t * filter_instance);
typedef int (*livido_process_f) (livido_port_t * filter_instance,
				 double timestamp);
typedef int (*livido_deinit_f) (livido_port_t * filter_instance);


#ifdef FUNCSTRUCT
typedef struct {
    void *(*livido_malloc_f) (size_t size);
    void (*livido_free_f) (void *ptr);
    void *(*livido_memset_f) (void *s, int c, size_t n);
    void *(*livido_memcpy_f) (void *dest, const void *src, size_t n);

    livido_port_t *(*livido_port_new_f) (int);
    void (*livido_port_free_f) (livido_port_t * port);

    int (*livido_property_set_f) (livido_port_t *, const char *, int, int,
				  void *);
    int (*livido_property_get_f) (livido_port_t *, const char *, int,
				  void *);

    int (*livido_property_num_elements_f) (livido_port_t *, const char *);
     size_t(*livido_property_element_size_f) (livido_port_t *,
					      const char *, const int);
    int (*livido_property_atom_type_f) (livido_port_t *, const char *);

    char **(*livido_list_properties_f) (livido_port_t *);

    livido_port_t *extensions;	// Extensions, port that holds voidptr to extension functions

} livido_setup_t;

typedef livido_port_t *(*livido_setup_f) (const livido_setup_t * list,
					  int version);
#else

typedef void *(*livido_malloc_f) (size_t size);
typedef void (*livido_free_f) (void *ptr);
typedef void *(*livido_memset_f) (void *s, int c, size_t n);
typedef void *(*livido_memcpy_f) (void *dest, const void *src, size_t n);
typedef livido_port_t *(*livido_port_new_f) (int);
typedef void (*livido_port_free_f) (livido_port_t * port);
typedef int (*livido_property_set_f) (livido_port_t *, const char *, int, int,  void *);
typedef int (*livido_property_get_f) (livido_port_t *, const char *, int,  void *);
typedef int (*livido_property_num_elements_f) (livido_port_t *, const char *);
typedef size_t(*livido_property_element_size_f) (livido_port_t *, const char *, const int);
typedef int (*livido_property_atom_type_f) (livido_port_t *, const char *);
typedef char **(*livido_list_properties_f) (livido_port_t *);

typedef struct
{
	void *f;
} livido_setup_t;
typedef livido_port_t *(*livido_setup_f) (const livido_setup_t list[], int );

#endif

#define	LIVIDO_PLUGIN \
static livido_port_t *(*livido_port_new) (int) = 0;\
static void (*livido_port_free) (livido_port_t * port) = 0;\
static int (*livido_property_set) (livido_port_t * port,const char *key, int atom_type, int num_elems, void *value) = 0;\
static int (*livido_property_get) (livido_port_t * port,const char *key, int idx, void *value) = 0;\
static int (*livido_property_num_elements) (livido_port_t * port,const char *key) = 0;\
static int (*livido_property_atom_type) (livido_port_t * port,const char *key) = 0;\
static size_t(*livido_property_element_size) (livido_port_t * port,const char *key, const int idx) = 0;\
static char **(*livido_list_properties) (livido_port_t * port) = 0;\
static void *(*livido_malloc) (size_t size) = 0;\
static void (*livido_free) (void *ptr) = 0;\
static void *(*livido_memset) (void *s, int c, size_t n) = 0;\
static void *(*livido_memcpy) (void *dest, const void *src, size_t n) = 0;\


#ifdef FUNCTSTRUCT
#define	LIVIDO_IMPORT(list) \
{\
	livido_malloc		= list->livido_malloc_f;\
	livido_free		= list->livido_free_f;\
	livido_memset		= list->livido_memset_f;\
	livido_memcpy		= list->livido_memcpy_f;\
	livido_port_free	= list->livido_port_free_f;\
	livido_port_new		= list->livido_port_new_f;\
	livido_property_set	= list->livido_property_set_f;\
	livido_property_get	= list->livido_property_get_f;\
	livido_property_num_elements = list->livido_property_num_elements_f;\
	livido_property_atom_type = list->livido_property_atom_type_f;\
	livido_property_element_size = list->livido_property_element_size_f;\
	livido_list_properties	=	list->livido_list_properties_f;\
}
#else
/* Using void* to pass base address of function, needs explicit typecast and host
   must match ordering */
#define	LIVIDO_IMPORT(list) \
{\
	livido_malloc		= (livido_malloc_f) list[0].f;\
	livido_free		= (livido_free_f) list[1].f;\
	livido_memset		= (livido_memset_f) list[2].f;\
	livido_memcpy		= (livido_memcpy_f) list[3].f;\
	livido_port_free	= (livido_port_free_f) list[5].f;\
	livido_port_new		= (livido_port_new_f) list[4].f;\
	livido_property_set 	= (livido_property_set_f) list[6].f;\
	livido_property_get	= (livido_property_get_f) list[7].f;\
	livido_property_num_elements = (livido_property_num_elements_f) list[8].f;\
	livido_property_atom_type = (livido_property_atom_type_f) list[9].f;\
	livido_property_element_size = (livido_property_element_size_f) list[10].f;\
	livido_list_properties = (livido_list_properties_f) list[11].f;\
}

#endif

LIVIDO_END_DECLS
#endif				// #ifndef __LIVIDO_H__
