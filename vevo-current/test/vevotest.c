/*Copyright (c) 2004-2005 N.Elburg <nelburg@looze.net>

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
#include <include/libvevo.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

static void    out( int ntabs, const char format[], ... )
{
        char buf[1024];
        va_list args;
        va_start( args, format );
        vsnprintf( buf, sizeof(buf) - 1, format, args );
        if( ntabs > 0)
        {
                int i;
                for( i = 0; i < ntabs; i ++ )
                        printf("\t");
        }
        printf("%s\n", buf);
        va_end(args);
}


// simple print routine for testing (incomplete!) 
void		vevo_print_parameter( vevo_parameter_templ_t *info, vevo_port *p )
{
	out(0, "Parameter properties:");
	out(1, "Name  :%s", info->name );
	out(1, "Help  :%s", info->help );
	out(1, "Form  :%s", info->format );


	//vevo_print_properties( p );

}

void	vevo_print_channel( vevo_channel_templ_t *info, vevo_port * p )
{
	out(1,"[%s]", info->name );
	
	int i = 100;
	for( i=100; i < 110; i ++ )
	{
		int dt = 0;
		if( vevo_get_data_type( p, i, &dt ) == VEVO_ERR_SUCCESS )
		{
			if(dt == VEVO_PTR_U8)
			{
				uint8_t *ptr[4];
				if(vevo_get_property(p, i, ptr) == VEVO_ERR_SUCCESS)
					out(1, "Pointer to UINT8_T = %p,%p,%p,%p",
					ptr[0],ptr[1],ptr[2],ptr[3]);
				else
					out(1,"property ptr u8 not there");
			}
			if(dt == VEVO_INT)
			{
				int val = 0;
				if(vevo_get_property(p, i, &val) == VEVO_ERR_SUCCESS)
				{
					out(1,"INT %d is %d",i, val);
				}
				else
				{
					out(1,"INT %d does not exist", i );
				}
			}
			if(dt == VEVO_PTR_DBL)
			{
				double *val = NULL;
				if(vevo_get_property(p, i, &val) == VEVO_ERR_SUCCESS)
				{
					out(1,"Pointer to DOUBLE %d (ptr) is %p",i, val);
				}
				else
				{
					out(1,"property %d does not exist", i );
				}
			}
			if(dt == VEVO_DOUBLE)
			{
				double val = 0;
				if(vevo_get_property(p, i, &val) == VEVO_ERR_SUCCESS)
				{
					out(1,"DOUBLE %d is %g",i, val);
				}
				else
				{
					out(1,"property %d does not exist", i );
				}
			}
		}
	}	
}

vevo_instance_templ_t plugininfo
= {
	"Simple plugin",
	"Niels Elburg",
	"Doesnt do anything",
	"copyleft",
	"1.0",
	"1.0",
	0, 
};


vevo_channel_templ_t frameinfo
=
{
	"Input channel 0",
	"Background layer",
	0,
	NULL,
	NULL,
	{ VEVO_RGB888, VEVO_RGBA8888, VEVO_BGR888, VEVO_YUV888 , VEVO_INVALID }, 
};


vevo_parameter_templ_t p0info = 
{
	"parameter 0",
	"foobar",
	NULL,
	VEVOP_HINT_NORMAL,
	0,
	1,
};


vevo_parameter_templ_t p1info =
{
	"parameter 1",
	"color key",
	NULL,
	VEVOP_HINT_RGBA,
	0,	
	4
};

vevo_parameter_templ_t s1info =
{
	"parameter string",
	"color key",
	NULL,
	VEVOP_HINT_RGBA,
	0,
	1,
};


vevo_parameter_templ_t p2info = 
{
	"parameter 2",
	"text",
	NULL,
	VEVOP_HINT_NORMAL,
	0,
	1,
};

vevo_parameter_templ_t p3info =
{
	"parameter 3",
	"button group",
	NULL,
	VEVOP_HINT_GROUP,
	0,
	2,
};


vevo_parameter_templ_t p4info =
{
	"parameter 4",
	"string list",
	NULL,
	VEVOP_HINT_GROUP,
	0,
	4,
};

int main(int argc, char *argv[])
{

/************* number parameter *********************/

	
	// allocate a parameter
	vevo_port *p0 = vevo_allocate_parameter( &p0info );
	double values[5] = { 0.1, 1.0, 0.33, 0.01, 0.25 };

	out(0, "NUMBER (real)");
	// set all properties
	vevo_set_property( p0, VEVOP_MIN, VEVO_DOUBLE, p0info.arglen, &values[0] );
	vevo_set_property( p0, VEVOP_MAX, VEVO_DOUBLE, p0info.arglen, &values[1] );
	vevo_set_property( p0, VEVOP_DEFAULT, VEVO_DOUBLE, p0info.arglen, &values[2] );
	vevo_set_property( p0, VEVOP_STEP_SIZE, VEVO_DOUBLE, p0info.arglen, &values[3] );
	vevo_set_property( p0, VEVOP_PAGE_SIZE, VEVO_DOUBLE, p0info.arglen, &values[4] );
	vevo_set_property( p0, VEVOP_VALUE, VEVO_DOUBLE,p0info.arglen, &values[2] );

	int i;
	for ( i = 0; i < 100; i ++ )
	{
		double value = (double) i;
		vevo_set_property( p0, VEVOP_VALUE, VEVO_DOUBLE, p0info.arglen, &value );
	}

	int value_int;
	double value_dbl;

	vevo_get_property_as( p0, VEVOP_VALUE, VEVO_INT, &value_int );
	out(1, "Value as INT: %d", value_int );

	vevo_get_property_as( p0, VEVOP_VALUE, VEVO_DOUBLE, &value_dbl );
	out(1, "Value as DOUBLE: %g", value_dbl );

	// free parameter
	vevo_free_port( p0 );

/****************** rgba parameter ***************/

	vevo_port *p1 = vevo_allocate_parameter( &p1info );
	double colors[5][4] =
	{
		{ 0.0,0.0,0.0,0.0 },
		{ 1.0,1.0,1.0,1.0 },
		{ 0.5, 0.15, 1.0, 0.45 },
		{ 0.01, 0.01, 0.01, 0.01 },
		{ 0.25, 0.25, 0.25, 0.25 }
	};

	out(0, "\nRGBA (real)");

	vevo_set_property( p1, VEVOP_MIN, VEVO_DOUBLE, p1info.arglen, colors[0] );
	vevo_set_property( p1, VEVOP_MAX,VEVO_DOUBLE, p1info.arglen, colors[1] );
	vevo_set_property( p1, VEVOP_DEFAULT,VEVO_DOUBLE, p1info.arglen, colors[2] );
	vevo_set_property( p1, VEVOP_STEP_SIZE, VEVO_DOUBLE,p1info.arglen, colors[3] );
	vevo_set_property( p1, VEVOP_PAGE_SIZE, VEVO_DOUBLE,p1info.arglen, colors[4] );
	vevo_set_property( p1, VEVOP_VALUE, VEVO_DOUBLE,p1info.arglen, colors[2] );

	int test_cast[4];

	vevo_get_property_as( p1, VEVOP_VALUE, VEVO_INT, test_cast );

	out(1,"\nRGBA {%g,%g,%g,%g} =  {%d,%d,%d,%d}",
		colors[4][0],colors[4][1],colors[4][2],colors[4][3],
		test_cast[0], 
		test_cast[1], test_cast[2], test_cast[3] );

/***************** rgba string -> double -> int  **************/

	out(0, "\nRGBA (string)");

	char *sca_str = "#ff00eeaa";
	vevo_set_property_by( p1, VEVOP_VALUE, VEVO_STRING,strlen(sca_str) , sca_str);
	vevo_get_property_as( p1, VEVOP_VALUE, VEVO_INT, test_cast );
	out(1,"RGBA [%s] -> [%d,%d,%d,%d]",
		sca_str, test_cast[0],test_cast[1], test_cast[2], test_cast[3]);

	double raw_str[4];
	vevo_get_property_as( p1, VEVOP_VALUE, VEVO_DOUBLE, raw_str );
	out(1,"RGBA [%s] -> [%g,%g, %g,%g]",
		sca_str, raw_str[0],raw_str[1],raw_str[2],raw_str[3]);

	vevo_free_port(p1);

	out(0,"\nRGBA (string)");
	vevo_port *s1 = vevo_allocate_parameter( &s1info );
//	char *str_value = strdup("#ff00ff00");
//	char str_value[9] = "#ff00ff00";
	const char *str_value = "#ff00ff00";

	out(1,"RGBA [%s]", str_value);	
	vevo_set_property( s1, VEVOP_VALUE, VEVO_STRING, s1info.arglen, &str_value );

	int cval1[4];
	double cval2[4];

	vevo_get_property_as( s1, VEVOP_VALUE, VEVO_DOUBLE, cval2);
	vevo_get_property_as( s1, VEVOP_VALUE, VEVO_INT ,cval1 );

	out(1,"RGBA [%s] -> {%g,%g,%g,%g} -> {%d,%d,%d,%d}", str_value,
		cval2[0],cval2[1],cval2[2],cval2[3], cval1[0],
		cval1[1],cval1[2], cval1[3] );

	
	vevo_free_port( s1 );

/************* string atom ******************/
	out(0,"\nSTRING (text)");

	vevo_port *p2 = vevo_allocate_parameter( &p2info );
	const char *text = "Veejay Video Objects";
	vevo_set_property( p2, VEVOP_VALUE, VEVO_STRING,p2info.arglen, &text );

	char test[100];
	vevo_get_property( p2, VEVOP_VALUE, &test );

	out(1,"STRING %s has value %s", p2info.name, test );
	vevo_free_port(p2);

/************** boolean group *****************/

	out(0, "\nBOOLEAN (group)");

	vevo_port *p3 = vevo_allocate_parameter( &p3info );
	int group[2] = { 0, 1 };
	vevo_set_property( p3, VEVOP_VALUE, VEVO_INT, p3info.arglen, &group );

	int current[2];
	vevo_get_property( p3, VEVOP_VALUE, &current );

	out(1,"Boolean %s has group values [ %d, %d ]", p3info.name, current[0], current[1] );

	vevo_free_port(p3);

/************ string array *******************/

	out(0, "\nSTRING (array)");

	vevo_port *p4 = vevo_allocate_parameter( &p4info );
	const char *lines[4] = { "Additive", "Difference", "Multiply", "Overlay" };
	char *bogus[4];

	vevo_set_property( p4, VEVOP_VALUE, VEVO_STRING, p4info.arglen, &lines );

	int num_items = 0;
	vevo_get_num_items(p4, VEVOP_VALUE, &num_items );
	for(i = 0; i < num_items; i ++ )
	{
		int size = 0;
		vevo_get_item_size( p4, VEVOP_VALUE,i, &size );
		bogus[i] = (char*) malloc(sizeof(char) * size+1);
	}
      
	
	vevo_get_property( p4, VEVOP_VALUE, bogus );

	out(1,"STRING %s has values:", p4info.name);
	for(i = 0; i < num_items; i ++)
	{
		out(2,"[%s]", bogus[i]);
		free(bogus[i]);
	}
	vevo_free_port(p4);	


/************** a channel **************/

	out(0, "\nPOINTER (to memory)");      

	vevo_port *data = vevo_allocate_channel( &frameinfo );
	//char *src = strdup("Hahahaha");
	uint8_t *blob = (uint8_t*) malloc(sizeof(uint8_t) * 255);
	uint8_t *src = blob; 
	out(1,"Address [%p]", src);	
	out(1,"Frame format  [%d]", frameinfo.format[0]);
	

	vevo_set_property( data, VEVOC_PIXELDATA, VEVO_PTR_U8, 1, &src );

	vevo_print_channel( &frameinfo, data );

	free(blob);
	vevo_free_port( data );


/************** a planar channel **************/

	out(0, "\nARRAY OF POINTERS (planar data)");

	vevo_port *data2 = vevo_allocate_channel( &frameinfo );
	//char *src = strdup("Hahahaha");
	uint8_t *blob2[3];
	int width = 10; 
	int fmt = VEVO_RGB888;
	int height = 10;
	int sto = VEVO_FRAME_U8;
	blob2[0] = (uint8_t*) malloc(sizeof(uint8_t) * 255);
	blob2[1] = (uint8_t*) malloc(sizeof(uint8_t) * 255);
	blob2[2] = (uint8_t*) malloc(sizeof(uint8_t) * 255);

	out(1,"Pointer Array [%p,%p,%p]", blob2[0], blob2[1],blob2[2]);	
	out(1,"Data format  [%d]", frameinfo.format[0]);
	

	vevo_set_property( data2, VEVOC_PIXELDATA, VEVO_PTR_U8, 3, blob2 );

	vevo_set_property( data2, VEVOC_WIDTH, VEVO_INT, 1, &width );
	vevo_set_property( data2, VEVOC_HEIGHT,VEVO_INT, 1, &height );
	vevo_set_property( data2, VEVOC_FORMAT,VEVO_INT,1, &fmt);
	vevo_set_property( data2, VEVOC_PIXELINFO, VEVO_INT,1,&sto); 

	vevo_print_channel( &frameinfo, data2 );


	vevo_frame_t A;
	if(vevo_collect_frame_data( data2, &A ) == VEVO_ERR_SUCCESS )
	{
		out(1,"Frame data:");
		out(2,"Width : %d, Height: %d", A.width,A.height );
		out(2,"Shift U:%d, ShiftV: %d", A.shift_h, A.shift_v );
		out(2,"Row strides [%d][%d][%d][%d]",
			A.row_strides[0], A.row_strides[1], A.row_strides[2],A.row_strides[3]);
		out(2,"Type = %d\n", A.type);
		out(2,"U8 = %p, %p, %p",
			A.data_u8[0], A.data_u8[1], A.data_u8[2] ); 
	}

	free(blob2[0]);
	free(blob2[1]);
	free(blob2[2]);
	vevo_free_port( data2 );

	return 1;

}
