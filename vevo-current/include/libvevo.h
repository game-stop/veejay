/**
@file	libvevo.h
@brief	VeVO - Veejay Video Objects header file
*/


/**
	@mainpage VeVo :: Veejay Video Objects
	@section intro Introduction

During the last couple of years we have been observing a blooming
development in the field of realtime video software for the Linux/GNU
platform. 

An increasing number of artists and other users are exploring the
possibilities of several unique software packages for video editing, mixing
and effect processing. 

The question arises here how we (the developers)
balance the growing number of features against complexity, (re)usability
and manageability of larger scale software design.

As users, we choose our applications because their features fit our
purposes , as developers we serve the needs of the users who want an
increasing number of features added to our applications while we should
focus on flexible core engines and program frameworks.

An elegant solution to this problem is to provide the Free Software
community with a flexible architecture that allows a shared pool of video
plugins. 

This brings a number of benefits:

 -  we can give designers of plugins the opportunity to develop their (video)
    processing algorithms without the distraction of resolving interface
    problems;
 -  we can share a set of unique plugins among a great number of programs;
    we can give the user the opportunity to control at least some aspects of
    the features they want.

The library attempts to give programmers the ability to write simple 'plugin'
video processors in C/C++ and link them dynamically with a range of
software packages (called hosts). It should be possible for any host and any
plugin to communicate through this interface.


@section License
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

#define		VEVO_VERSION				(VERSION)	

/**
	@defgroup	vevo_error Error Codes
	@{
*/	

#define 	VEVO_ERR_SUCCESS			0	///< success 
#define 	VEVO_ERR_NO_SUCH_PROPERTY 		1	///< property does not exist/
#define		VEVO_ERR_OOB 				2	///< array is out of bounds 
#define		VEVO_ERR_READONLY			3	///< cannot write value 
#define		VEVO_ERR_UNSUPPORTED_FORMAT		4	///< illegal format 
#define		VEVO_ERR_INVALID_STORAGE_TYPE		5	///< illegal storage type for conversion 
#define		VEVO_ERR_INVALID_CAST			6	///< illegal cast
#define		VEVO_ERR_INVALID_PROPERTY		7	///< illegal property 
#define		VEVO_ERR_INVALID_PROPERTY_VALUE 	8	///< invalid value for property 
#define		VEVO_ERR_INVALID_CONVERSION		9	///< invalid conversion requested 
#define		VEVO_ERR_INVALID_FORMAT			10	///< invalid format description

/// @}

/**
	@defgroup	atom	Vevo Objects

	Vevo stores data into generic objects and remembers 
	what type specifier belongs to the attribute 

	@{
*/
#define	VEVO_INT 	1		///< data is of type 'int'
#define VEVO_STRING 	2		///< data is of type 'char*'
#define VEVO_DOUBLE 	3		///< data is of type 'double'
#define VEVO_BOOLEAN 	4		///< data is of type 'vevo_boolean_t'
#define VEVO_PTR_VOID 	5		///< data is of type 'void*'
#define VEVO_PTR_DBL 	6		///< data is of type 'double *'
#define VEVO_PTR_U8 	7		///< data is of type 'uint8_t *'
#define VEVO_PTR_U16 	8		///< data is of type 'uint16_t *'
#define VEVO_PTR_U32 	9		///< data is of type 'uint32_t *'
#define VEVO_PTR_S8 	10		///< data is of type 'int8_t *'
#define VEVO_PTR_S16 	11		///< data is of type 'int16_t *'
#define VEVO_PTR_S32 	12		///< data is of type 'int32_t *'

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
typedef int vevo_boolean_t;		///< boolean type (FALSE or TRUE)

typedef int vevo_atom_type_t;		///< atom type specifier

#define VEVOP_ATOM 	0		///< single atom
#define VEVOP_ARRAY	1		///< array of atoms

typedef int vevo_storage_t;		///< atom storage specifier


typedef int vevo_cntl_flags_t;		///< datatype flags

//! Atom structure (for holding single attributes)
typedef struct
{
	 void	 *value;			///< valueless pointer  
	size_t	 size;				///< memory size of atom
} atom_t;

//! Generic Object storage structure (for holding one or more attributes of the same type)
typedef struct
{
	vevo_storage_t		type;		///< storage type (ATOM or ARRAY)
	vevo_atom_type_t	ident;		///< C type identifier
	union				
	{
		atom_t 		*atom;		///< generic pointer to value 
		atom_t		**array;	///< array of atoms (of type ident) 
	};
	size_t			length;		///< length of array 
	vevo_cntl_flags_t	cmd;		///< flags to set on this object           
} vevo_datatype;

/// @}




/**
	@defgroup vevoprop	Vevo Property
	@brief Properties

	The properties below are variables for both host and plugin,
	They are divided into Parameters, Channels and Instance.
	The plugin and host store and retrieve their runtime information
	from these properties. 

	@{
*/
typedef int vevo_property_type_t;				///< property type identifier
//! The vevo property is a linked list of generic objects
struct vevo_property
{
	vevo_property_type_t type;		 			///< type of property
	vevo_datatype	*data;			 			///< pointer to generic object
	struct	vevo_property *next; 					///< pointer to next property
};
typedef struct vevo_property vevo_property_t; 				///< property type specifier


#define VEVO_PROPERTY_PARAMETER			0
#define VEVO_PROPERTY_CHANNEL  			100
#define VEVO_PROPERTY_INSTANCE 			200

/*
	The control flags can be used to toggle a property
	readonly or writeable. 

*/
#define	VEVO_PROPERTY_WRITEABLE 	0			///< property is writeable
#define VEVO_PROPERTY_READONLY		1			///< property is readonly

/*	Parameter

	At plugins side, VEVOP_DEFAULT should be initialized at init()
	time so the host can initialize VEVOP_VALUE (and possibly use
	its own set of defaults).
	The function vevo_property_assign_value() can be used to
	clone properties and give them a new ID (like VEVOP_VALUE for example)

 */
 
#define	VEVOP_MIN		(VEVO_PROPERTY_PARAMETER + 1)	///< min value
#define VEVOP_MAX		(VEVO_PROPERTY_PARAMETER + 2)	///< max value
#define VEVOP_DEFAULT		(VEVO_PROPERTY_PARAMETER + 3)	///< default value
#define VEVOP_STEP_SIZE		(VEVO_PROPERTY_PARAMETER + 4)	///< step size
#define VEVOP_PAGE_SIZE		(VEVO_PROPERTY_PARAMETER + 5)	///< page size
#define VEVOP_VALUE		(VEVO_PROPERTY_PARAMETER + 6)	///< value 
#define VEVOP_GROUP_ID		(VEVO_PROPERTY_PARAMETER + 7)	///< group id of parameter value
#define VEVOP_ENABLED		(VEVO_PROPERTY_PARAMETER + 8)	///< parameter can be enabled/disabled
#define VEVOP_LIST		(VEVO_PROPERTY_PARAMETER + 9)   ///< parameter selects value from this list
/*
	Channel 

	At plugins side, it can collect the properties VEVOC_WIDTH, VEVOC_HEIGHT
	to allocate its own buffers. These properties are set by the host
	for each data channel the plugin needs.
	Also, the properties VEVOC_PIXELDATA and VEVOC_PIXELINFO are provided
	by the Host. Some properties can change in runtime with or without 
	re-initializing the plugin. 


*/
#define	VEVOC_FLAGS		(VEVO_PROPERTY_CHANNEL + 1)	///< channel optional flags
#define VEVOC_WIDTH		(VEVO_PROPERTY_CHANNEL + 2)	///< image data width
#define VEVOC_HEIGHT		(VEVO_PROPERTY_CHANNEL + 3)	///< image data height
#define VEVOC_FPS		(VEVO_PROPERTY_CHANNEL + 4)	///< video fps
#define VEVOC_INTERLACING	(VEVO_PROPERTY_CHANNEL + 5)	///< video interlacing order (!!)
#define VEVOC_SAME_AS		(VEVO_PROPERTY_CHANNEL + 6)	///< channel must have same properties as ...
#define VEVOC_ENABLED		(VEVO_PROPERTY_CHANNEL + 7)	///< channel is enabled 
#define VEVOC_FORMAT		(VEVO_PROPERTY_CHANNEL + 8)	///< pixel format
#define	VEVOC_SHIFT_V		(VEVO_PROPERTY_CHANNEL + 9)	///< vertical shift value (for YUV planar data)
#define VEVOC_SHIFT_H		(VEVO_PROPERTY_CHANNEL + 10)	///< horizontal shift value (for YUV planar data)
#define VEVOC_PIXELDATA		(VEVO_PROPERTY_CHANNEL + 11)	///< pointer to image data
#define VEVOC_PIXELINFO		(VEVO_PROPERTY_CHANNEL + 12)	///< description of imagedata (byte size)

/*
	Instance

	The plugin can setup flags to hint the host about how it behaves
	(the plugin's algorithm is inplace for example, or it needs to be re-initialized
         when some parameter is changed, ... See instance flags.
	The plugin may require storage of its own private data (buffers, use
	of libraries and such) that is usually a plugin's global (static) variable.
	The plugin must be REENTRENT code always for Hosts supporting
	multiple CPU's. 
	Advanced plugins may require types like FPS to be known at init()
	time to property prepare for calling process().

*/
#define VEVOI_SCALE_X		(VEVO_PROPERTY_INSTANCE + 1)	///< horizontal scale multiplier
#define VEVOI_SCALE_Y		(VEVO_PROPERTY_INSTANCE + 2)	///< vertical scale multiplier
#define VEVOI_FPS		(VEVO_PROPERTY_INSTANCE + 3)	///< (optional) frames per second desired by host
#define VEVOI_VIEWPORT		(VEVO_PROPERTY_INSTANCE + 4)	///< (optional) viewport (x1,y1,w,h)
#define VEVOI_PRIVATE		(VEVO_PROPERTY_INSTANCE + 5)	///< (optional) plugin's private data
#define VEVOI_SMP		(VEVO_PROPERTY_INSTANCE + 6)	///< (optional) plugin's multi threaeding use
#define VEVOI_FLAGS		(VEVO_PROPERTY_INSTANCE + 7)	///< plugin flags

/* 	
	The properties below are setup by the Host , and initialized on calling
	vevo_allocate_instace()
*/
#define VEVOI_N_IN_PARAMETERS 	(VEVO_PROPERTY_INSTANCE + 8)	///< number of input parameters
#define	VEVOI_N_OUT_PARAMETERS 	(VEVO_PROPERTY_INSTANCE + 9)	///< number of output parameters
#define VEVOI_N_IN_CHANNELS	(VEVO_PROPERTY_INSTANCE + 10)	///< number of input channels
#define VEVOI_N_OUT_CHANNELS	(VEVO_PROPERTY_INSTANCE + 11)	///< number of output channels




/*
	Parameter Hints

	The parameter layout hint describes a 'common' parameter that 
	an application can use to represent its own representation
	of a parameter when it can be hinted how it looks like. 

*/
#define	VEVOP_HINT_NORMAL		0			///< parameter is a single value (numeric or text)
#define VEVOP_HINT_RGBA			1			///< parameter is RGBA color key (numeric or text)
#define VEVOP_HINT_COORD2D		2			///< parameter is 2d coordinate (numeric only)
#define VEVOP_HINT_TRANSITION		4			///< parameter is a transition (numeric only)
#define VEVOP_HINT_GROUP		5			///< parameter is a group of values (numeric of text)
typedef int vevo_param_hint_t;

#define	VEVOP_FLAG_REINIT		(1<<0x1)		///< tell host to call init after changing this parameter
#define VEVOP_FLAG_KEYFRAME		(1<<0x2)		///< tell host this parameter is keyframeable
#define VEVOP_FLAG_VISIBLE		(1<<0x3)		///< tell host this parameter should not be visible in GUI




/*
	Channel Hints

	The channel flags can be used to tell the host how to handle
	the plugins channel.
*/	

#define	VEVOC_FLAG_OPTIONAL		(1<<0)			///< (optional) tell host this channel can be disabled
#define VEVOC_FLAG_MASK			(1<<1)			///< (optional) tell host this channel is a mask
#define VEVOC_FLAG_SIZE			(1<<2)			///< (host must check!) tell host this channel size must match size of another channel
#define VEVOC_FLAG_FORMAT		(1<<3)			///< (host must check!) this channel format must match format of another channel
#define VEVOC_FLAG_REINIT		(1<<4)			///< (host must check!) this channels requires calling init on property changes

/*
	Instance Hints
*/
#define	VEVOI_FLAG_CAN_DO_REALTIME		(1<<0x01)		///< plugin is realtime
#define VEVOI_FLAG_REQUIRE_INPLACE		(1<<0x02)		///< inplace operation
#define VEVOI_FLAG_CAN_DO_SCALED 		(1<<0x03)		///< plugin supports parameter scaling 
#define VEVOI_FLAG_CAN_DO_WINDOW 		(1<<0x04)		///< plugin supports viewport 
#define VEVOI_FLAG_CAN_DO_KEYFRAME		(1<<0x05)		///< plugin supports keyframing
#define VEVOI_FLAG_REQUIRE_FPS			(1<<0x07)		///< plugin requires fps information
#define VEVOI_FLAG_REQUIRE_STATIC_SIZES 	(1<<0x08)		///< channel sizes dont change in runtime
#define VEVOI_FLAG_REQUIRE_STATIC_FORMAT	(1<<0x09)		///< no format changes during runtime
#define VEVOI_FLAG_REQUIRE_INTERLACING		(1<<0x0a)		///< plugin requires interlacing information

	


/// @}

/**
	@defgroup vevoports	Vevo Ports


	@{	

*/

/*
	Port identifiers are used to distinguish between control and data.               
	A special type 'instance' identifies global settings for the plugin,
	some of them initialized by the Host.
*/
#define		VEVO_CONTROL					0		///< Port is Parameter
#define		VEVO_DATA					1		///< Port is Data (audio/video)
#define		VEVO_INSTANCE					2		///< Port is Plugin (global properties)

typedef int vevo_port_type_id_t;						///< port type specifier
typedef int vevo_format_t;							///< dataformat identifier
//! Vevo Port type (distinguish control from data)
typedef struct
{
	vevo_port_type_id_t		id;				///< type of port (DATA or CONTROL)
	union
	{	
		vevo_param_hint_t	hint;				///< CONTROL has layout hints
		vevo_format_t		format;				///< DATA has data format
	};		
} vevo_port_type_t;

//! Vevo Port (single parameter, channel or instance) 
typedef struct
{
	vevo_port_type_t		*type;				///< port type description
	vevo_property_t			*properties;			///< list of properties
} vevo_port;


/// @}

typedef double	vevo_timecode_t;					///< timecode (only used for keyframing)

/**
	@defgroup vevoparam Vevo Parameter

	@{
*/
//! Vevo Parameter Template
/**
	@brief setting up templates

	A parameter template is setup by the plugin and describes 
	the properties of a parameter.
*/
struct vevo_parameter_template
{
	const char 			*name;				///< parameter name
	const char 			*help;				///< parameter description
	const char 			*format;			///< string format identifier (currently unused but usable)
	vevo_param_hint_t	hint;					///< hint (must be set!)
	int					flags;			///< optional flags 
	int					arglen;			///< number of atoms inside this parameter
	struct vevo_parameter_template *next; 				///< next parameter
};

typedef struct vevo_parameter_template vevo_parameter_templ_t;
/// @}

/**
	@defgroup vevochan Vevo Channel
	@{
*/


#define		VEVO_INVALID				0	///< denotes end of list of supported colorspaces
#define		VEVO_RGBAFLOAT				1	///< RGBA float , 24 bits per component  (packed)
#define		VEVO_RGBA8888				2	///< RGBA  8 bps, 4 components per pixel (packed)
#define		VEVO_RGB888				3	///< RGBA  8 bps, 3 components per pixel (packed)
#define		VEVO_RGB161616				4	///< RGB  16 bps, 3 components per pixel (packed)
#define		VEVO_RGBA16161616			5	///< RGBA 16 bps, 4 components per pixel (packed)
#define		VEVO_RGB565				6	///< RGB Mask: R=0xf800, G=0x07e0, B = 0x001f 
#define		VEVO_BGR888				7	///< BGR   8 bps, 3 components per pixel (packed)
#define		VEVO_YUV888				8	///< YUV   8 bps, 3 components per pixel (packed), 1 U/V component for every Y 
#define		VEVO_YUVA8888				9	///< YUV with Alpha, 8 bps, 4 components per pixel (packed), 1 U/V component for every Y
#define		VEVO_YUV161616				10	///< YUV  16 bps, 3 components per pixel (packed) , 1 U/V component for every Y 
#define		VEVO_YUVA16161616			11	///< YUV with Alpha, 16 bps, 4 components per pixel (packed), 1 U/V component for every Y
#define		VEVO_YUV422P				12	///< YUV 4:2:2 Planar 
#define		VEVO_YUV420P				13	///< YUV 4:2:0 Planar
#define		VEVO_YUV444P				14	///< YUV 4:4:4 Planar
#define		VEVO_YUYV8888				15	///< YUYV, 32 bits per sample (packed) 
#define		VEVO_UYVY8888				16	///< UYVY, 32 bits per sample (packed)
#define		VEVO_ALPHAFLOAT				17	///< Alpha float
#define		VEVO_ALPHA8				18	///< Alpha 8 bps
#define		VEVO_ALPHA16				19	///< Alpha 16 bps



//!Vevo Channel Template
/**
	@brief channel templates

	A channel template is setup by the Plugin to describe what
	data the Plugin needs.
	Rules for the host how to handle this Channel can be setup with
	the variable 'flags'.	
*/
struct vevo_channel_template
{
	const char		*name;				///< channel name
	const char		*help;				///< channel help
	int			flags;				///< optional flags
	const char		*same_as;			///< must be same as channel <name>
	struct 	vevo_channel_template *next;  			///< next channel
	vevo_format_t	format[];				///< list of supported colorspaces (0 terminated)
};

typedef struct vevo_channel_template vevo_channel_templ_t;

#define VEVO_FRAME_U8 	1		///< image data is of type uint8_t*
#define VEVO_FRAME_U16 2		///< image data is of type uint16_t*
#define VEVO_FRAME_U32 3		///< image data is of type uint32_t*
#define VEVO_FRAME_S8 4			///< image data is of type int8_t*
#define VEVO_FRAME_S16 5		///< image data is of type int16_t*
#define VEVO_FRAME_S32 6		///< image data is of type int32_t*
#define VEVO_FRAME_FLOAT 7		///< image data is of type double*
typedef int vevo_pixel_info_t;		///< image data type specifier


//! Vevo Frame (created from port properties)
/**
	@brief getting image data

	The vevo frame structure is a convenience structure
	for the plugin programmer. It is setup by the function
	vevo_collect_frame_data.   
*/
struct vevo_frame
{
	int			fmt;					///< pixel colorspace 
	vevo_pixel_info_t	type;					///< pixel data format
	union {
		uint8_t		*data_u8[4];				///< defined by 'type'
		uint16_t	*data_u16[4];
		uint32_t	*data_u32[4];
		int8_t		*data_s8[4];
		int16_t		*data_s16[4];
		int32_t		*data_s32[4];
		float		*data_float[4];
	};
	int			 row_strides[4];			///< strides
	int			 width;					///< image width
	int			 height;				///< image height
	int			 shift_h;				///< horizontal shifting value
	int			 shift_v;				///< vertical shifting value
};

typedef struct vevo_frame vevo_frame_t;

/// @}

/**
	@defgroup vevoinstance Vevo Instance
*/
//!Vevo Instance
/**
	@brief instance data

	The instance data is setup by Host and passed to all mandatory plugin's functions.
*/
typedef struct
{
	vevo_port	*self;						///< instance
	vevo_port	**in_channels;					///< input channels
	vevo_port	**out_channels;					///< output channels
	vevo_port	**in_params;					///< input parameters
	vevo_port 	**out_params;					///< output parameters
} vevo_instance_t;

/// @}

/**
	@defgroup vevohostfunc Plugins Functions

	@{
*/


//! Plugin Instance template
/**
	@brief	Plugins Mandatory functions

	The 3 functions below must be provided by the Plugin.
*/
typedef	int		(vevo_init_f)		(vevo_instance_t*);  ///< function to initialize plugin
typedef int		(vevo_deinit_f)		(vevo_instance_t*);  ///< function to deinitialize plugin
typedef int		(vevo_process_f)	(vevo_instance_t*);  ///< function to process
/**
	@brief	Plugin Optional functions

	The function below defines a callback host can use to
	keyframe the plugins parameters.
*/
typedef int		(vevo_cb_f)		(void *ptr, vevo_instance_t *, vevo_port *port, vevo_timecode_t pos, vevo_timecode_t keyframe);


/**
	@brief	plugin's setup

	The Plugin provides a function 'setup' that returns
	this template filled with the appropriate values.
*/
typedef struct 
{
	const char			*name;			///< Name of vevo plugin
	const char			*author;		///< Authors name / email
	const char			*description;		///< Help or general information
	const char			*license;		///< Licensing (GPL, LGPG, etc)
	const char			*version;		///< Version of plugin
	const char			*vevo_version;		///< version of API
	int				flags;			///< hints for host how to handle plugin

	vevo_channel_templ_t		*in_channels;		///< input data (array of channels)
	vevo_channel_templ_t		*out_channels;		///< output data (array of channels)
	vevo_parameter_templ_t 		*in_params;		///< input parameters (array of parameters)
	vevo_parameter_templ_t 		*out_params;		///< output parameters (array of parameters)

	vevo_init_f			*init;			///< pointer to plugin's initialization function
	vevo_deinit_f			*deinit;		///< pointer to plugin's deinitialization function
	vevo_process_f			*process;		///< pointer to plugin's process function
	vevo_cb_f			*next_keyframe;		///< pointer to plugin's next keyframe function 
	vevo_cb_f			*prev_keyframe;		///< pointer to plugin's previous keyframe function

} vevo_instance_templ_t;


typedef vevo_instance_templ_t * (vevo_setup_f) (void);
/**
	@brief  Functions host must setup

*/
typedef void*	(vevo_malloc_f)	(size_t);			///< host memory allocation function
typedef void*	(vevo_free_f)	(void*);			///< host free memory function
typedef void*	(vevo_memset_f) (void *src, int c, size_t);	///< host memset function
typedef void	(vevo_memcpy_f)	(void *to, const void *from, size_t); ///< host memcopy function



/**
	get the number of objects stored in a property
	@param *p pointer to vevo port
	@param t property type
	@param *size pointer to result
	@return Error code
*/
int		vevo_get_num_items( vevo_port *p, vevo_property_type_t type, int *size );
/**
	retrieve the byte size per object in a property
	@param *p pointer to vevo port
	@param t property type
	@param index number of object
	@param *size pointer to result
	@return Error code
*/
int		vevo_get_item_size( vevo_port *p, vevo_property_type_t type,int index, int *size );

/**
	get the type specifier of a property
	@param *p pointer to vevo port
	@param type property type
	@param *dst pointer to result
	@return vevo_atom_type_t
*/
	
vevo_atom_type_t	vevo_get_data_type( vevo_port *p, vevo_property_type_t type, int *dst );

/**
	see if a property is in the list of properties
	@param *p pointer to vevo port
	@param t property type to search
	@return Error code
*/
int		vevo_find_property( vevo_port *p, vevo_property_type_t t );

/**
	put a property as the first item in the list of properties
	@param *p pointer to vevo port
	@param t property type
	@return Error code
*/
int		vevo_sort_property( vevo_port *p, vevo_property_type_t t );
/**
	change flags of a property in the propertylist
	@param *p pointer to vevo port
	@param t property type
	@param ro_flag flag to set (WRITEABLE or READONLY)
	@return Error code
*/
int		vevo_cntl_property( vevo_port *p, vevo_property_type_t t, vevo_cntl_flags_t ro_flag );

/**
	delete a property from the list of properties
	@param *p pointer to vevo port
	@param t property type to delete
	@return Error code
*/
int		vevo_del_property(  vevo_port *p, vevo_property_type_t t );

/**
	put a new property in the list of properties
	@param *p pointer to vevo port
	@param type property type to (re)create
	@param ident the type specifier of the data
	@param arglen the length of data
	@param *value pointer to data
*/
void		vevo_set_property(  vevo_port *p, vevo_property_type_t type, vevo_atom_type_t ident,size_t arglen, void *value );

/**
	put a new property to the list of properties from a different type specifier
	automaticaly scales/converts values between numeric type specifiers.
	@param *p pointer to vevo port
	@param type property
	@param type specifier of data
	@param *value pointer to data
	@return Error code
*/
int		vevo_set_property_by( vevo_port *p, vevo_property_type_t type, vevo_atom_type_t src_ident, size_t arglen, void *value );

/**
	get the data from a property
	@param *p pointer to vevo port
	@param type property
	@param *dst pointer to result
	@return Error code
*/
int		vevo_get_property(  vevo_port *p, vevo_property_type_t type, void *dst );

/**
	get and scale/cast the data from a property
	@param *p pointer to vevo port
	@param type property
	@param ident type specifier of data
	@param *dst pointer to data 
	@return Error code
*/
int		vevo_get_property_as( vevo_port *p, vevo_property_type_t type, vevo_atom_type_t ident, void *dst );

/**
	free memory used by a port (destroys all properties). Used by host
	@param *p pointer to vevo port 
*/	
void		vevo_free_port	(	vevo_port *p );

/**
	free memory used by plugin's instance. Used by host
	@param *p pointer to vevo instance
*/
void		vevo_free_instance( vevo_instance_t * );

/**
	instantiate a parameter based on some template
	@param *info  pointer to template
	@return vevo_port
*/
vevo_port 	*vevo_allocate_parameter( vevo_parameter_templ_t *info );

/**
	instantiate a channel based on some template
	@param *info pointer to template
	@return vevo_port
*/
vevo_port 	*vevo_allocate_channel( vevo_channel_templ_t *info );

/**
	instantiate a plugin based on the plugin's instance template. Used by host
	@param *info pointer to vevo instance template
	@return vevo_instance_t
*/
vevo_instance_t	*vevo_allocate_instance( vevo_instance_templ_t *info );

/**
	fill in a frame structure based on port settings
	@param *p vevo channel port
	@param *dst pointer to vevo_frame to fill
	@return Error code 
*/
int		vevo_collect_frame_data( vevo_port *p, vevo_frame_t *dst );

/**
	prepare a list of properties based on some type
	@param *p vevo parameter port
	@param p_args the length of data
	@param ident the type specifier of data
	@param n_types the number of properties to initialize 
	@param ... variable list of properties (in order of data)
	@return Error code
*/
int		vevo_init_parameter_values( vevo_port *p, int p_args, vevo_atom_type_t ident, void *val, int n_types, ...);

/**
	assign the value of some property to another property
	@param *p pointer to vevo port
	@param left property type to (re)create
	@param right property type to copy values from
	@return Error code
*/

int		vevo_property_assign_value( vevo_port *p, vevo_property_type_t left, vevo_property_type_t right );

/**
	assign the value of some property 
	from a different type specifier
	@param *p pointer to vevo port
	@param left property type to (re)create
	@param ident datatype of pointer to data
	@param *data pointer to data
	@return Error code
*/
int		vevo_property_assign_value_from( vevo_port *p, vevo_property_type_t left, vevo_atom_type_t ident, void *data );

/// @}

#define		VEVO_GET_PARAMS_STRICT( port, property_type, atom_identifier, destination )\
{int err_ =  vevo_get_property_as( port, property_type, atom_identifier, destination );\
if(err_ != ERR_SUCCESS) return err_};

# endif
