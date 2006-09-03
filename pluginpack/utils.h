#ifndef LVDUTILSFX
#define LVDUTILSFX

#include <stdio.h>
#include <stdint.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#include <math.h>

#ifdef ARCH_X86
#define fast_sin(d,x) asm("fsin" : "=t" (d) : "0" (x))
#define fast_cos(d,x) asm("fcos" : "=t" (d) : "0" (x))
#else
#define fast_sin(d,x) d = sin(x)
#define fast_cos(d,x) d = cos(x)					       
#endif	       

#ifdef USE_MATRIX_PLACEMENT
typedef struct
{
        int w;
        int h;
} matrix_t;

typedef matrix_t (*matrix_f)(int i, int s, int w, int h);
static matrix_t matrix_placementA(int photoindex, int size, int w , int h);
static matrix_t matrix_placementB(int photoindex, int size, int w , int h);
static matrix_f        get_matrix_func(int type);

static matrix_t matrix_placementA(int photoindex, int size, int w , int h)
{
        matrix_t m;
        m.w = (photoindex % size) * (w/size);
        m.h = (photoindex / size) * (h/size);
        return m;
}

static matrix_t matrix_placementB(int photoindex, int size, int w , int h)
{
        matrix_t m;
        m.w = (photoindex/size) * (w/size);
        m.h = (photoindex % size) * (h/size);
        return m;
}

static matrix_t matrix_placementC(int photoindex, int size, int w , int h)
{
        matrix_t m;
        int n = size*size-1;
        m.w = ((n-photoindex) % size) * (w/size);
        m.h = ((n-photoindex) / size) * (h/size);
        return m;
}

static matrix_t matrix_placementD(int photoindex, int size, int w , int h)
{
        matrix_t m;
        int n = size*size-1;
        m.w = ((n-photoindex) / size) * (w/size);
        m.h = ((n-photoindex) % size) * (h/size);
        return m;
}

static matrix_f        get_matrix_func(int type)
{
        if(type==0)
                return &matrix_placementA;
        if(type==1)
                return &matrix_placementB;
        if(type==2)
                return &matrix_placementC;
        return &matrix_placementD;              
}
#endif

static int power_of(int size)
{
        int power = 1;
        while( size-- )
                power *= 2;

        return power;
}

static int max_power(int w)
{
        int i=1;
        while(power_of(i) < w)
                i++;
        return i;
}

static	int	lvd_is_yuv444( int palette )
{
	if( palette == LIVIDO_PALETTE_YUV444P )
		return 1;
	return 0;
}

static	int	lvd_uv_plane_len( int palette, int w, int h )
{
	switch(palette)
	{	
		case LIVIDO_PALETTE_YUV422P:
			return ( (w * h)/2 );
			break;
		case LIVIDO_PALETTE_YUV420P:
			return ( (w/2) * (h/2) );
			break;
		case LIVIDO_PALETTE_YUV444P:
			return (w*h);
			break;
		default:
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;

	}
	return 0;
}

static	double	lvd_extract_param_number( livido_port_t *instance, const char *pname, int n )
{
	double pn = 0.0;
	livido_port_t *c = NULL;
	int error = livido_property_get( instance, pname,n, &c );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	error = livido_property_get( c, "value", 0, &pn );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	return pn;	
}


static	int	lvd_extract_param_index( livido_port_t *instance, const char *pname, int n )
{
	int pn = 0;
	livido_port_t *c = NULL;
	int error = livido_property_get( instance, pname,n, &c );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	error = livido_property_get( c, "value", 0, &pn );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	return pn;	
}

static	int	lvd_extract_param_boolean( livido_port_t *instance, const char *pname, int n )
{
	int pn = 0;
	livido_port_t *c = NULL;
	int error = livido_property_get( instance, pname,n, &c );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	error = livido_property_get( c, "value", 0, &pn );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	return pn;	
}

static	void	lvd_extract_dimensions( livido_port_t *instance,const char *name, int *w, int *h )
{
	livido_port_t *channel = NULL;
	int error = livido_property_get( instance, name, 0, &channel );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	error = livido_property_get( channel, "width", 0, w );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	error = livido_property_get( channel, "height",0, h );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
}

static	int	lvd_extract_channel_values( livido_port_t *instance,
				    const char *pname,
				    int n,
				    int *w,
				    int *h,
				    uint8_t **pixel_data,
				    int *palette )
{
	livido_port_t	*c = NULL;
	int error = livido_property_get( instance, pname,n, &c );
	error = livido_property_get( c, "width", 0,w );
#ifdef STRICT_CHECKING
	if(error!=LIVIDO_NO_ERROR) printf("%s: width not found\n",__FUNCTION__);
	assert( error == LIVIDO_NO_ERROR );
#endif
	error = livido_property_get( c, "height",0,h );
#ifdef STRICT_CHECKING
	if(error!=LIVIDO_NO_ERROR) printf("%s: height not found\n",__FUNCTION__);
	
	assert( error == LIVIDO_NO_ERROR );
#endif
	error = livido_property_get( c, "current_palette",0, palette );
#ifdef STRICT_CHECKING
	if(error!=LIVIDO_NO_ERROR) printf("%s: current_palette not found\n",__FUNCTION__);
	
	assert( error == LIVIDO_NO_ERROR );
#endif
	int i = 0;
	for( i = 0; i <4 ; i ++ )
	{
		error = livido_property_get( c, "pixel_data", i, &(pixel_data[i]));
#ifdef STRICT_CHECKING
		if( error != LIVIDO_NO_ERROR )
			printf("%s: pixel_data[%d] not set in %s %d\n",__FUNCTION__,i,pname,n);
		assert( error == LIVIDO_NO_ERROR );
#endif
	}
	return LIVIDO_NO_ERROR;
}

#endif
