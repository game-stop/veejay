/*
Copyright (c) 2004-2005 N.Elburg <nelburg@looze.net>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

# ifndef VEVO_H_INCLUDED
# define VEVO_H_INCLUDED

#include <stdio.h>
#include <stdint.h>

/** 
	Error types
*/

#define 	VEVO_ERR_SUCCESS				0
#define 	VEVO_ERR_NO_SUCH_PROPERTY 		1		///< property does not exist
#define		VEVO_ERR_OOB 					2		///< array out of bounds
#define		VEVO_ERR_READONLY				3		///< property is readonly
#define		VEVO_ERR_UNSUPPORTED_FORMAT		4		///< unsupported format
#define		VEVO_ERR_INVALID_STORAGE_TYPE	5		///< illegal storage type for conversion
#define		VEVO_ERR_INVALID_CAST			6		///< illegal cast
#define		VEVO_ERR_INVALID_PROPERTY		7
#define		VEVO_ERR_INVALID_PROPERTY_VALUE 8 
#define		VEVO_ERR_INVALID_CONVERSION		9
#define		VEVO_ERR_INVALID_FORMAT			10

/*!
	The list of supported color space models
*/

#define		VEVO_INVALID					0		///< End of palette list
#define		VEVO_RGBAFLOAT					1		///< float 
#define		VEVO_RGBA8888					2		///< each component is 8 bits
#define		VEVO_RGB888						3		///< each component is 8 bits
#define		VEVO_RGB161616					4		///< each component is 16 bits
#define		VEVO_RGBA16161616				5		///< each component is 16 bits
#define		VEVO_RGB565						6		///< 
#define		VEVO_BGR888						7		///< each component is 8 bits
#define		VEVO_YUV888						8		///<
#define		VEVO_YUVA8888					9		///<
#define		VEVO_YUV161616					10		///<
#define		VEVO_YUVA16161616				11		///<
#define		VEVO_YUV422P					12		///<
#define		VEVO_YUV420P					13		///<
#define		VEVO_YUV444P					14		///<
#define		VEVO_YUYV8888					15		///<
#define		VEVO_UYVY8888					16		///<
#define		VEVO_ALPHAFLOAT					17		///< alpha in float
#define		VEVO_ALPHA8						18		///< alpha 8 bits
#define		VEVO_ALPHA16					19		///< alpha 16 bits

typedef int vevo_format_t;							///< dataformat identifier

/**
	The list of port identifiers. 
*/

#define		VEVO_CONTROL					0		///< Port is Parameter
#define		VEVO_DATA						1		///< Port is Data (audio/video)
#define		VEVO_INSTANCE					2		///< Port is Plugin (global properties)

typedef int vevo_port_type_id_t;					///< port type specifier

/**
	Boolean type definition
*/

#define FALSE 0
#define TRUE 1
typedef int vevo_boolean_t;

/**
	Atom type definitions
*/
#define	VEVO_INT 1
#define VEVO_STRING 2
#define VEVO_DOUBLE 3
#define VEVO_BOOLEAN 4
#define VEVO_PTR_VOID 5
#define VEVO_PTR_DBL 6
#define VEVO_PTR_U8 7
#define VEVO_PTR_U16 8
#define VEVO_PTR_U32 9
#define VEVO_PTR_S8 10
#define VEVO_PTR_S16 11
#define VEVO_PTR_S32 12
typedef int vevo_atom_type_t;

#define VEVO_FRAME_U8 1
#define VEVO_FRAME_U16 2
#define VEVO_FRAME_U32 3
#define VEVO_FRAME_S8 4
#define VEVO_FRAME_S16 5
#define VEVO_FRAME_S32 6
#define VEVO_FRAME_FLOAT 7
typedef int vevo_pixel_info_t;


/**
	Atom storage type definitions
*/
#define VEVOP_ATOM 0
#define VEVOP_ARRAY 1
typedef int vevo_storage_t;

/**
	Property types
*/
#define VEVO_PROPERTY_PARAMETER			0
#define VEVO_PROPERTY_CHANNEL  			100
#define VEVO_PROPERTY_INSTANCE 			200


/*!
	Video interlacing predefined values
*/
#define VEVO_INTERLACE_TOP_BOTTOM		2
#define VEVO_INTERLACE_BOTTOM_TOP		1
#define VEVO_INTERLACE_NONE				0			///< default choice (whole frames)

/*!
	Parameter properties */
#define	VEVOP_MIN		(VEVO_PROPERTY_PARAMETER + 1)	///< (depends) parameter min value
#define VEVOP_MAX		(VEVO_PROPERTY_PARAMETER + 2)	///< (depends) parameter max value
#define VEVOP_DEFAULT	(VEVO_PROPERTY_PARAMETER + 3)	///< parameter default value
#define VEVOP_STEP_SIZE (VEVO_PROPERTY_PARAMETER + 4)	///< (optional) parameter step size
#define VEVOP_PAGE_SIZE (VEVO_PROPERTY_PARAMETER + 5)	///< (optional) parameter page size
#define VEVOP_VALUE		(VEVO_PROPERTY_PARAMETER + 6)	///< parameter value
#define VEVOP_GROUP_ID	(VEVO_PROPERTY_PARAMETER + 7)	///< (optional) parameter belongs to a group
#define VEVOP_ENABLED	(VEVO_PROPERTY_PARAMETER + 8)	///< (optional) toggleable parameters

/*!
	Channel properties
*/
#define	VEVOC_FLAGS			(VEVO_PROPERTY_CHANNEL + 1)		///< channel optional flags
#define VEVOC_WIDTH			(VEVO_PROPERTY_CHANNEL + 2)		///< image data width
#define VEVOC_HEIGHT		(VEVO_PROPERTY_CHANNEL + 3)		///< image data height
#define VEVOC_FPS			(VEVO_PROPERTY_CHANNEL + 4)		///< video fps
#define VEVOC_INTERLACING	(VEVO_PROPERTY_CHANNEL + 5)		///< video interlacing order (!!)
#define VEVOC_SAME_AS		(VEVO_PROPERTY_CHANNEL + 6)		///< channel must have same properties as ...
#define VEVOC_ENABLED		(VEVO_PROPERTY_CHANNEL + 7)		///< channel is enabled 
#define VEVOC_FORMAT		(VEVO_PROPERTY_CHANNEL + 8)		///< pixel format
#define	VEVOC_SHIFT_V		(VEVO_PROPERTY_CHANNEL + 9)		///< vertical shift value (for YUV planar data)
#define VEVOC_SHIFT_H		(VEVO_PROPERTY_CHANNEL + 10)	///< horizontal shift value (for YUV planar data)
#define VEVOC_PIXELDATA		(VEVO_PROPERTY_CHANNEL + 11)	///< pointer to image data
#define VEVOC_PIXELINFO		(VEVO_PROPERTY_CHANNEL + 12)	///< description of imagedata (byte size)

/*!
	Instance properties
*/
#define VEVOI_SCALE_X		(VEVO_PROPERTY_INSTANCE + 1)	///< horizontal scale multiplier
#define VEVOI_SCALE_Y		(VEVO_PROPERTY_INSTANCE + 2)	///< vertical scale multiplier
#define VEVOI_FPS			(VEVO_PROPERTY_INSTANCE + 3)	///< (optional) frames per second desired by host
#define VEVOI_VIEWPORT		(VEVO_PROPERTY_INSTANCE + 4)	///< (optional) viewport (x1,y1,w,h)
#define VEVOI_PRIVATE		(VEVO_PROPERTY_INSTANCE + 5)	///< (optional) plugin's private data
#define VEVOI_SMP			(VEVO_PROPERTY_INSTANCE + 6)	///< (optional) plugin's multi threaeding use
#define VEVOI_FLAGS			(VEVO_PROPERTY_INSTANCE + 7)	///< plugin flags

/*! 	
	Instance properties that are set by vevo_allocate_instance(...) :
*/
#define VEVOI_N_IN_PARAMETERS 	(VEVO_PROPERTY_INSTANCE + 8)	///< number of input parameters
#define	VEVOI_N_OUT_PARAMETERS 	(VEVO_PROPERTY_INSTANCE + 9)	///< number of output parameters
#define VEVOI_N_IN_CHANNELS		(VEVO_PROPERTY_INSTANCE + 10)	///< number of input channels
#define VEVOI_N_OUT_CHANNELS	(VEVO_PROPERTY_INSTANCE + 11)	///< number of output channels


typedef int vevo_property_type_t;

/*!
	Parameter layout hints
*/
#define	VEVOP_HINT_NORMAL		0					///< parameter is a single value (numeric or text)
#define VEVOP_HINT_RGBA			1					///< parameter is RGBA color key (numeric or text)
#define VEVOP_HINT_COORD2D		2					///< parameter is 2d coordinate (numeric only)
#define VEVOP_HINT_TRANSITION	4					///< parameter is a transition (numeric only)
#define VEVOP_HINT_GROUP		5					///< parameter is a group of values (numeric of text)

typedef int vevo_param_hint_t;

#define	VEVO_PROPERTY_WRITEABLE 	0				///< property is writeable
#define VEVO_PROPERTY_READONLY		1				///< property is readonly

typedef int vevo_cntl_flags_t;


/*!
	Channel flags.
	These flags can be set per channel.   
*/	

#define	VEVOC_FLAG_OPTIONAL		(1<<0)				///< tell host this channel can be disabled
#define VEVOC_FLAG_MASK			(1<<1)				///< tell host this channel is a mask
#define VEVOC_FLAG_SIZE			(1<<2)				///< tell host this channel size must match size of another channel
#define VEVOC_FLAG_FORMAT		(1<<3)				///< tell host this channel format must match format of another channel
#define VEVOC_FLAG_REINIT		(1<<4)				///< tell host this channels requires calling init on property changes

/*!
	Parameter flags
	These flags can be set per parameter
*/

#define	VEVOP_FLAG_REINIT		(1<<0x1)			///< tell host to call init after changing this parameter
#define VEVOP_FLAG_KEYFRAME		(1<<0x2)			///< tell host this parameter is keyframeable
#define VEVOP_FLAG_VISIBLE		(1<<0x3)			///< tell host this parameter should not be visible in GUI


/*!
	Plugin flags

	Plugin tells host what it requires and/or what special features it has. 
	A 'Can do' is an optional feature; the plugin can run if implied properties are missing.
	A 'Require' is a required feature; the plugin cannot run without. Host must provide
	implied functionality to support this plugin.
	 
	

	Host must check implied port configurations.
*/

#define	VEVOI_FLAG_CAN_DO_REALTIME		(1<<0x01)			///< plugin is realtime
#define VEVOI_FLAG_REQUIRE_INPLACE		(1<<0x02)			///< inplace operation
#define VEVOI_FLAG_CAN_DO_SCALED 		(1<<0x03)			///< plugin supports parameter scaling 
#define VEVOI_FLAG_CAN_DO_WINDOW 		(1<<0x04)			///< plugin supports viewport 
#define VEVOI_FLAG_CAN_DO_KEYFRAME		(1<<0x05)			///< plugin supports keyframing
#define VEVOI_FLAG_REQUIRE_FPS			(1<<0x07)			///< plugin requires fps information
#define VEVOI_FLAG_REQUIRE_STATIC_SIZES  (1<<0x08)			///< channel sizes dont change in runtime
#define VEVOI_FLAG_REQUIRE_STATIC_FORMAT (1<<0x09)			///< no format changes during runtime
#define VEVOI_FLAG_REQUIRE_INTERLACING	(1<<0x0a)			///< plugin requires interlacing information



/**
	Internal structures 
*/

typedef struct
{
	 void	 *value;								///< valueless pointer  
	size_t	 size;									///< memory size of atom
} atom_t;


typedef struct
{
	vevo_storage_t		type;						///< type of container, array or atom
	vevo_atom_type_t		ident;					///< C type identifier
	union				
	{
		atom_t 			*atom;						///< generic value
		atom_t			**array;					///< array of atoms (of type ident) 
	};
	size_t				length;						///< length of array 
	vevo_cntl_flags_t	cmd;
} vevo_datatype;



/**
    A property 
*/

struct vevo_property
{
	vevo_property_type_t type;		  				///< property: value, min,max,default, step_size , etc etc
	vevo_datatype	*data;			  				///< data
	struct	vevo_property *next; 					///< next element in linked list
};

typedef struct vevo_property vevo_property_t; 		///< property type specifier

typedef struct
{
	vevo_port_type_id_t		id;						///< type of port (DATA or CONTROL)
	union
	{	
		vevo_param_hint_t	hint;					///< CONTROL has layout hints
		vevo_format_t		format;					///< DATA has data format
	};		
} vevo_port_type_t;


/**
	VeVo Port

	A port describes the data given to the plugin. Depending on its type, it can be 
	a parameter-, a channel- or a plugin's global property configuration.
	The 'properties' field is a linked list of properties ; 
	
*/

typedef struct
{
	vevo_port_type_t		*type;					///< port type description
	vevo_property_t		*properties;				///< list of properties
} vevo_port;


typedef double	vevo_timecode_t;					///< timecode (only used for keyframing)

/**
	VeVo Parameter Template
	
	This template describes a parameter at the plugin side.
	The plugin configures the parameter properties on calling vevo_init_f(...) ; 
	It can use vevo_set_property(...) to configure the port.               

*/

struct vevo_parameter_template
{
	const char 			*name;						///< parameter name
	const char 			*help;						///< parameter description
	const char 			*format;			 		///< string format identifier (currently unused but usable)
	vevo_param_hint_t	hint;						///< hint (must be set!)
	int					flags;						///< optional flags 
	int					arglen;						///< number of atoms inside this parameter
	struct vevo_parameter_template *next; 			///< next parameter
};

typedef struct vevo_parameter_template vevo_parameter_templ_t;

/*!
	VeVo Channel template

	This template describes a channel, over which DATA is sent.
*/

struct vevo_channel_template
{
	const char		*name;							///< channel name
	const char		*help;							///< channel help
	int				flags;							///< optional flags
	const char		*same_as;						///< must be same as channel <name>
	struct 	vevo_channel_template *next;  			///< next channel
	vevo_format_t	format[];						///< list of supported colorspaces (0 terminated)
};

typedef struct vevo_channel_template vevo_channel_templ_t;

/*!
	Vevo Frame

	This structure describes the actual image data at runtime
	(optional). The function collect_frame_data fills this structure
    so plugin has less code to call from library to initialize channels

*/

struct vevo_frame
{
	int			fmt;
	vevo_pixel_info_t	  type;
	union {
		uint8_t		*data_u8[4];
		uint16_t	*data_u16[4];
		uint32_t	*data_u32[4];
		int8_t		*data_s8[4];
		int16_t		*data_s16[4];
		int32_t		*data_s32[4];
		float		*data_float[4];
	};
	int			 row_strides[4];
	int			 width;
	int			 height;
	int			 shift_h;
	int			 shift_v;	
};

typedef struct vevo_frame vevo_frame_t;

/*!
	Runtime data of plugin
*/

typedef struct
{
	vevo_port	*self;				// instance
	vevo_port	**in_channels;		// input channels
	vevo_port	**out_channels;		// output channels
	vevo_port	**in_params;		// input parameters
	vevo_port 	**out_params;		// output parameters
} vevo_instance_t;


/*!
	Function definitions 

	The plugin must at least initialize pointer to vevo_init_f, vevo_deinit_f and vevo_process_f
*/

typedef	int		(vevo_init_f)		(vevo_instance_t*);
typedef int		(vevo_deinit_f)		(vevo_instance_t*);
typedef int		(vevo_process_f)	(vevo_instance_t*);

/*!
	Keyframe CALLBACK function defintion.
	The plugin can calculate new parameter values for Host
*/
typedef int		(vevo_cb_f)		(void *ptr, vevo_instance_t *, vevo_port *port, vevo_timecode_t pos, vevo_timecode_t keyframe);

/*!
	Memory managment / Memory copying and setting functions.

	Host must set these functions to its own (shared) memory allocator and 
	(optimized) memory copy functions 

*/
typedef void*	(vevo_malloc_f)	(size_t);
typedef void*	(vevo_free_f)	(void*);
typedef void*	(vevo_memset_f) (void *src, int c, size_t);
typedef void	(vevo_memcpy_f)	(void *to, const void *from, size_t); 

// instance template, host needs it to setup plugin
typedef struct 
{
	const char					*name;					///< Name of vevo plugin
	const char					*author;				///< Authors name / email
	const char					*description;			///< Help or general information
	const char					*license;				///< Licensing (GPL, LGPG, etc)
	const char					*version;				///< Version of plugin
	const char					*vevo_version;			///< version of API
	int							flags;					///< hints for host how to handle plugin

	vevo_channel_templ_t		*in_channels;			///< input data (array of channels)
	vevo_channel_templ_t		*out_channels;			///< output data (array of channels)
	vevo_parameter_templ_t 		*in_params;				///< input parameters (array of parameters)
	vevo_parameter_templ_t 		*out_params;			///< output parameters (array of parameters)

	vevo_init_f					*init;					///< pointer to plugin's initialization function
	vevo_deinit_f				*deinit;				///< pointer to plugin's deinitialization function
	vevo_process_f				*process;				///< pointer to plugin's process function
	vevo_cb_f					*next_keyframe;			///< pointer to plugin's next keyframe function 
	vevo_cb_f					*prev_keyframe;			///< pointer to plugin's previous keyframe function

} vevo_instance_templ_t;


typedef vevo_instance_templ_t * (vevo_setup_f) (void);








/*!
		Get the number of items inside a property.
		\param p		VeVo Port
		\param type		Property Type to search
		\param *size	Pointer to result 
		\return error code

*/
int		vevo_get_num_items( vevo_port *p, vevo_property_type_t type, int *size );

/*!
		Get the byte size of an item inside a property
		\param p		VeVo Port
		\param type		Property type to search
		\param index	Index (ignored if the property is a single atom)
		\param *size	Pointer to result
		\return error code
*/
int		vevo_get_item_size( vevo_port *p, vevo_property_type_t type,int index, int *size );

/*!
		Get the datatype specifier of a property
		\param p 		VeVo Port
		\param type		Property type to search
		\param *dst		Pointer to result (one of VEVO_INT, VEVO_DOUBLE, ... )
		\return error code
*/  
int		vevo_get_data_type( vevo_port *p, vevo_property_type_t type, int *dst );


/*!
		Find a property on a port
*/
int		vevo_find_property( vevo_port *p, vevo_property_type_t t );

/*!
		Put the property searched for as first in the list of properties
		
*/
int		vevo_sort_property( vevo_port *p, vevo_property_type_t t );

/*!
		Set a property READ_ONLY or WRITE_ABLE
*/
int		vevo_cntl_property( vevo_port *p, vevo_property_type_t t, vevo_cntl_flags_t ro_flag );

/*!
		Delete a property
*/
void	vevo_del_property(  vevo_port *p, vevo_property_type_t t );

/*!
		Set a value
		This value will be stored as a new property object.
*/
void	vevo_set_property(  vevo_port *p, vevo_property_type_t type, vevo_atom_type_t ident,size_t arglen, void *value );

/*!
		Set value from a different type
		Property must already exist.
*/
int		vevo_set_property_by( vevo_port *p, vevo_property_type_t type, vevo_atom_type_t src_ident, size_t arglen, void *value );

/*!
		Get a value
*/
int		vevo_get_property(  vevo_port *p, vevo_property_type_t type, void *dst );

/*!
		Get a value into a different type
*/
int		vevo_get_property_as( vevo_port *p, vevo_property_type_t type, vevo_atom_type_t ident, void *dst );

/*!
		Free memory in a port (deletes all properties)
*/
void	vevo_free_port	(	vevo_port *p );

/*!
		Free memory used by instance (frees all )
*/
void	vevo_free_instance( vevo_instance_t * );

/*!
		Allocate a port of type CONTROL based on a template
*/
vevo_port 		*vevo_allocate_parameter( vevo_parameter_templ_t *info );

/*!
		Allocate a port of type DATA based on a template
*/
vevo_port 		*vevo_allocate_channel( vevo_channel_templ_t *info );

/*!
		Allocate an instance of the plugin (including all configured ports)
		based on a template
*/
vevo_instance_t	*vevo_allocate_instance( vevo_instance_templ_t *info );



/*!
		Helper function to initialize a structure containing
		all frame related fields
*/
int		vevo_collect_frame_data( vevo_port *p, vevo_frame_t *dst );

# endif
