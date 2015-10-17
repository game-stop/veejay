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
#define fast_sqrt(res,x) asm ("fsqrt" : "=t" (res) : "0" (x)) 
#define sin_cos(si, co, x) asm ("fsincos" : "=t" (co), "=u" (si) : "0" (x))
#else
#define fast_sin(d,x) d = sin(x)
#define fast_cos(d,x) d = cos(x)
#define fast_sqrt( res,x ) res = sqrt(x)
#define sin_cos(si, co, x)     si = sin(x); co = cos(x)
									
#endif	       
static inline int myround(float n) 
{
  if (n >= 0) 
    return (int)(n + 0.5);
  else
    return (int)(n - 0.5);
}

#define _rgb2yuv(r,g,b,y,u,v)\
 {\
        float Ey = (0.299 * (float)r) + (0.587 * (float)g) + (0.114 * (float) b);\
        float Eu = (-0.168736 * (float)r) - (0.331264 * (float)g) + (0.500 * (float)b) + 128.0;\
        float Ev = (0.500 * (float)r) - (0.418688 * (float)g) - (0.081312 * (float)b)+ 128.0;\
        y = myround(Ey);\
        u = myround(Eu);\
        v = myround(Ev);\
        if( y > 0xff ) y = 0xff ; else if ( y < 0 ) y = 0;\
        if( u > 0xff ) u = 0xff ; else if ( u < 0 ) u = 0;\
        if( v > 0xff ) v = 0xff ; else if ( v < 0 ) v = 0;\
 }


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

static	int	lvd_extract_param_index( livido_port_t *instance, const char *pname, int n )
{
	int pn = 0;
	livido_port_t *c = NULL;
	if( livido_property_get( instance, pname,n, &c ) != LIVIDO_NO_ERROR )
		return 0;

	if( livido_property_get( c, "value", 0, &pn ) != LIVIDO_NO_ERROR )
		return 0;
	
	return pn;	
}

static	void	lvd_extract_dimensions( livido_port_t *instance,const char *name, int *w, int *h )
{
	livido_port_t *channel = NULL;
	if( livido_property_get( instance, name, 0, &channel ) != LIVIDO_NO_ERROR )
		return;
	livido_property_get( channel, "width", 0, w );
	livido_property_get( channel, "height",0, h );
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
	if( error != LIVIDO_NO_ERROR )
		return error;

	error = livido_property_get( c, "width", 0,w );
	if( error != LIVIDO_NO_ERROR )
		return error;
	error = livido_property_get( c, "height",0,h );
	if( error != LIVIDO_NO_ERROR )
		return error;
	error = livido_property_get( c, "current_palette",0, palette );
	if( error != LIVIDO_NO_ERROR )
		return error;

	int i = 0;
	for( i = 0; i <4 ; i ++ )
	{
		error = livido_property_get( c, "pixel_data", i, &(pixel_data[i]));
		if( error != LIVIDO_NO_ERROR )
			return error;
	}
	return LIVIDO_NO_ERROR;
}

#endif
